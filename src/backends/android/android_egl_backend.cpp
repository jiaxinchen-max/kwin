/*
    KWin - the KDE window manager
    This file is part of the KDE project.

    SPDX-FileCopyrightText: 2024 Termux Community
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "android_egl_backend.h"
#include "android_backend.h"
#include "android_output.h"
#include "core/graphicsbuffer.h"
#include "core/region.h"
#include "core/renderbackend.h"
#include "core/rendertarget.h"
#include "opengl/egldisplay.h"
#include "opengl/eglcontext.h"
#include "opengl/glframebuffer.h"
#include "utils/softwarevsyncmonitor.h"

#include <QDebug>
#include <QFile>
#include <QProcess>
#include <dlfcn.h>
#include <unistd.h>
#include <cstdlib>

// Include termux-render headers
#include <termux/render/buffer.h>
#include <termux/render/render.h>

// LorieBuffer constants from buffer.h
#ifndef LORIEBUFFER_AHARDWAREBUFFER
#define LORIEBUFFER_AHARDWAREBUFFER 3
#endif
#include <termux/render/tlog.h>

// External functions from termux-render library
extern LorieBuffer* get_lorieBuffer();
extern struct lorie_shared_server_state* get_serverState();

// Define Buffer type properly
struct Buffer_Desc {
    int width;
    int height; 
    int format;
    void *data;
    size_t size;
};
typedef struct Buffer_Desc Buffer;

// EGL and OpenGL headers
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

// Android AHardwareBuffer support (obtained from termux-render)
#if __ANDROID_API__ >= 26
#include <android/hardware_buffer.h>

// EGL extensions for AHardwareBuffer
#ifndef EGL_ANDROID_image_native_buffer
#define EGL_ANDROID_image_native_buffer 1
#define EGL_NATIVE_BUFFER_ANDROID         0x3140
#endif

#ifndef EGL_KHR_image
#define EGL_KHR_image 1
typedef void *EGLImageKHR;
#define EGL_NO_IMAGE_KHR                  ((EGLImageKHR)0)
#define EGL_IMAGE_PRESERVED_KHR           0x30D2
EGLAPI EGLImageKHR EGLAPIENTRY eglCreateImageKHR(EGLDisplay dpy, EGLContext ctx, EGLenum target, EGLClientBuffer buffer, const EGLint *attrib_list);
EGLAPI EGLBoolean EGLAPIENTRY eglDestroyImageKHR(EGLDisplay dpy, EGLImageKHR image);
EGLAPI EGLClientBuffer EGLAPIENTRY eglGetNativeClientBufferANDROID(const struct AHardwareBuffer *buffer);
#endif

#ifndef GL_OES_EGL_image
#define GL_OES_EGL_image 1
typedef void* GLeglImageOES;
GLAPI void GLAPIENTRY glEGLImageTargetTexture2DOES(GLenum target, GLeglImageOES image);
#endif

#endif

namespace KWin
{
namespace Android
{

AndroidEglLayer::AndroidEglLayer(BackendOutput *output, AndroidEglBackend *backend)
    : OutputLayer(output, OutputLayerType::Primary)
    , m_backend(backend)
{
    qInfo() << "Created AndroidEglLayer for output" << output->name();
}

AndroidEglLayer::~AndroidEglLayer()
{
    cleanup();
    qInfo() << "Destroyed AndroidEglLayer";
}

std::optional<OutputLayerBeginFrameInfo> AndroidEglLayer::doBeginFrame()
{
    const QSize nativeSize(m_output->modeSize());
    qDebug() << "AndroidEglLayer::doBeginFrame() - size:" << nativeSize;
    
    // Setup render target if needed
    if (m_width != nativeSize.width() || m_height != nativeSize.height() || !m_fbo) {
        if (!setupRenderTarget()) {
            qCritical() << "Failed to setup render target";
            return std::nullopt;
        }
    }
    
    if (m_useDirectRendering) {
        // Bind the AHardwareBuffer framebuffer for direct rendering
        glBindFramebuffer(GL_FRAMEBUFFER, m_framebuffer);
        glViewport(0, 0, m_width, m_height);
        qDebug() << "Using direct rendering to AHardwareBuffer";
        
        // Create a GLFramebuffer wrapper for the AHardwareBuffer framebuffer
        // This allows KWin to use the AHardwareBuffer framebuffer with existing rendering code
        if (!m_fbo) {
            m_fbo = std::make_unique<GLFramebuffer>(m_framebuffer, QSize(m_width, m_height));
        }
    } else if (!m_fbo) {
        qCritical() << "No framebuffer available";
        return std::nullopt;
    }
    
    m_renderTime = std::make_unique<CpuRenderTimeQuery>();
    
    return OutputLayerBeginFrameInfo{
        .renderTarget = RenderTarget(m_fbo.get()),
        .repaint = Region(0, 0, m_width, m_height),
    };
}

bool AndroidEglLayer::doEndFrame(const Region &renderedDeviceRegion, const Region &damagedDeviceRegion, OutputFrame *frame)
{
    m_renderTime->end();
    frame->addRenderTimeQuery(std::move(m_renderTime));
    
    if (m_useDirectRendering) {
        // Direct rendering mode - Mesa renders directly to AHardwareBuffer
        // No pixel copy needed, just signal frame completion
        struct lorie_shared_server_state *serverState = get_serverState();
        if (serverState) {
            lorie_mutex_lock(&serverState->lock, &serverState->lockingPid);
            serverState->waitForNextFrame = false;
            serverState->drawRequested = 1;
            pthread_cond_signal(&serverState->cond);
            lorie_mutex_unlock(&serverState->lock, &serverState->lockingPid);
            
            qDebug() << "Direct rendering frame completed - zero copy";
        }
        return true;
    }
    
    // Fallback: Copy EGL framebuffer to shared buffer for display
    // This is used when direct rendering is not available
    {
        if (m_buffer && m_framebuffer) {
            // Get server state for locking
            struct lorie_shared_server_state *serverState = get_serverState();
            if (!serverState) {
                qCritical() << "Failed to get server state";
                return true;
            }
            
            // Lock the shared buffer
            void *shared_buffer;
            lorie_mutex_lock(&serverState->lock, &serverState->lockingPid);
            int ret = LorieBuffer_lock((LorieBuffer*)m_buffer, &shared_buffer);
            if (ret != 0) {
                qCritical() << "Failed to lock LorieBuffer";
                lorie_mutex_unlock(&serverState->lock, &serverState->lockingPid);
                return true;
            }
            
            // Get buffer description
            const LorieBuffer_Desc *desc = LorieBuffer_description((LorieBuffer*)m_buffer);
            if (desc && shared_buffer) {
                // Bind our framebuffer to read from it
                glBindFramebuffer(GL_READ_FRAMEBUFFER, m_framebuffer);
                
                // Read pixels directly into the shared buffer
                glReadPixels(0, 0, desc->width, desc->height, GL_RGBA, GL_UNSIGNED_BYTE, shared_buffer);
                
                // Signal that drawing is requested
                serverState->waitForNextFrame = false;
                serverState->drawRequested = 1;
                pthread_cond_signal(&serverState->cond);
                
                glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
                qDebug() << "Copied frame to shared buffer:" << desc->width << "x" << desc->height;
            }
            
            // Unlock the buffer
            LorieBuffer_unlock((LorieBuffer*)m_buffer);
            lorie_mutex_unlock(&serverState->lock, &serverState->lockingPid);
        }
    }
    
    return true;
}

bool AndroidEglLayer::setupRenderTarget()
{
    const QSize nativeSize(m_output->modeSize());
    qInfo() << "Setting up render target:" << nativeSize;
    
    // Clean up existing resources
    cleanup();
    
    m_width = nativeSize.width();
    m_height = nativeSize.height();
    
    // Get the global termux-render buffer (initialized by connectToRender)
    m_buffer = (Buffer*)get_lorieBuffer();
    if (!m_buffer) {
        qCritical() << "Failed to get termux-render buffer - is connectToRender() called?";
        return false;
    }
    
    // Check if we can use Mesa direct rendering with AHardwareBuffer
    if (trySetupDirectRendering()) {
        qInfo() << "Using Mesa direct rendering to AHardwareBuffer";
        m_useDirectRendering = true;
        return true;
    }
    
    qInfo() << "Falling back to traditional rendering with glReadPixels";
    m_useDirectRendering = false;
    
    // Create OpenGL texture
    glGenTextures(1, &m_texture);
    glBindTexture(GL_TEXTURE_2D, m_texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, m_width, m_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    
    // Create framebuffer
    glGenFramebuffers(1, &m_framebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, m_framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_texture, 0);
    
    // Check framebuffer completeness
    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        qCritical() << "Framebuffer incomplete:" << status;
        cleanup();
        return false;
    }
    
    // Create GLFramebuffer wrapper
    m_fbo = std::make_unique<GLFramebuffer>(m_framebuffer, QSize(m_width, m_height));
    
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
    
    qInfo() << "Render target setup successful";
    return true;
}

bool AndroidEglLayer::trySetupDirectRendering()
{
#if __ANDROID_API__ >= 26
    qInfo() << "Attempting Mesa direct rendering setup...";
    
    // Get LorieBuffer description to access AHardwareBuffer
    const LorieBuffer_Desc *desc = LorieBuffer_description((LorieBuffer*)m_buffer);
    if (!desc) {
        qDebug() << "No buffer description available";
        return false;
    }
    
    // Check if this is an AHardwareBuffer type
    if (desc->type != LORIEBUFFER_AHARDWAREBUFFER) {
        qDebug() << "LorieBuffer is not AHardwareBuffer type, got type:" << desc->type;
        return false;
    }
    
    // Get the AHardwareBuffer
    AHardwareBuffer *ahb = desc->buffer;
    if (!ahb) {
        qDebug() << "LorieBuffer description has no AHardwareBuffer";
        return false;
    }
    
    EGLDisplay display = eglGetCurrentDisplay();
    if (display == EGL_NO_DISPLAY) {
        qDebug() << "No current EGL display";
        return false;
    }
    
    // Check for required EGL extensions
    const char *extensions = eglQueryString(display, EGL_EXTENSIONS);
    if (!extensions) {
        qDebug() << "Failed to query EGL extensions";
        return false;
    }
    
    bool hasNativeBufferAndroid = strstr(extensions, "EGL_ANDROID_image_native_buffer") != nullptr;
    bool hasImageKHR = strstr(extensions, "EGL_KHR_image") != nullptr || 
                       strstr(extensions, "EGL_KHR_image_base") != nullptr;
    
    if (!hasNativeBufferAndroid || !hasImageKHR) {
        qDebug() << "Required EGL extensions not available:";
        qDebug() << "  EGL_ANDROID_image_native_buffer:" << hasNativeBufferAndroid;
        qDebug() << "  EGL_KHR_image:" << hasImageKHR;
        return false;
    }
    
    // Create EGLImage from AHardwareBuffer
    EGLClientBuffer clientBuffer = eglGetNativeClientBufferANDROID(ahb);
    if (!clientBuffer) {
        qDebug() << "Failed to get native client buffer from AHardwareBuffer";
        return false;
    }
    
    EGLint imageAttribs[] = {
        EGL_IMAGE_PRESERVED_KHR, EGL_TRUE,
        EGL_NONE
    };
    
    m_eglImage = eglCreateImageKHR(display, EGL_NO_CONTEXT, EGL_NATIVE_BUFFER_ANDROID, 
                                   clientBuffer, imageAttribs);
    
    if (m_eglImage == EGL_NO_IMAGE_KHR) {
        EGLint error = eglGetError();
        qDebug() << "Failed to create EGLImage from AHardwareBuffer, error:" << Qt::hex << error;
        return false;
    }
    
    // Create and bind texture from EGLImage
    glGenTextures(1, &m_texture);
    glBindTexture(GL_TEXTURE_2D, m_texture);
    glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, m_eglImage);
    
    // Create framebuffer and attach texture
    glGenFramebuffers(1, &m_framebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, m_framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_texture, 0);
    
    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        qDebug() << "Framebuffer not complete:" << Qt::hex << status;
        cleanupDirectRendering();
        return false;
    }
    
    qInfo() << "Mesa direct rendering to AHardwareBuffer setup successful";
    qInfo() << "Buffer size:" << desc->width << "x" << desc->height << "format:" << desc->format;
    
    return true;
    
#else
    qDebug() << "AHardwareBuffer requires Android API >= 26";
    return false;
#endif
}

void AndroidEglLayer::cleanupDirectRendering()
{
    if (m_eglImage != EGL_NO_IMAGE_KHR) {
        EGLDisplay display = eglGetCurrentDisplay();
        if (display != EGL_NO_DISPLAY) {
            eglDestroyImageKHR(display, m_eglImage);
        }
        m_eglImage = EGL_NO_IMAGE_KHR;
    }
}

void AndroidEglLayer::cleanup()
{
    // Clean up direct rendering resources
    cleanupDirectRendering();
    
    if (m_framebuffer) {
        glDeleteFramebuffers(1, &m_framebuffer);
        m_framebuffer = 0;
    }
    
    if (m_texture) {
        glDeleteTextures(1, &m_texture);
        m_texture = 0;
    }
    
    // Don't release the buffer - it's a global shared resource
    m_buffer = nullptr;
    m_useDirectRendering = false;
    
    m_fbo.reset();
}

DrmDevice *AndroidEglLayer::scanoutDevice() const
{
    // Android backend doesn't use DRM
    return nullptr;
}

QHash<uint32_t, QList<uint64_t>> AndroidEglLayer::supportedDrmFormats() const
{
    // Return common formats for software rendering
    return {{DRM_FORMAT_ARGB8888, {DRM_FORMAT_MOD_LINEAR}}};
}

void AndroidEglLayer::releaseBuffers()
{
    cleanup();
}

AndroidEglBackend::AndroidEglBackend(AndroidBackend *backend)
    : m_backend(backend)
{
    qInfo() << "Initializing Android EGL backend with Mesa rendering";
    
    // Detect best rendering mode
    RenderingMode mode = detectBestRenderingMode();
    m_mesaAvailable = (mode != RenderingMode::Fallback);
    
    if (!m_mesaAvailable) {
        qWarning() << "Mesa not available - EGL backend may not work";
    } else {
        setupMesaRendering(mode);
    }
}

AndroidEglBackend::~AndroidEglBackend()
{
    qInfo() << "Destroying Android EGL backend";
    
    // Clean up layers
    for (AndroidEglLayer *layer : m_layers) {
        delete layer;
    }
    m_layers.clear();
}

void AndroidEglBackend::init()
{
    qInfo() << "Initializing Android EGL backend";
    
    if (!initializeEgl()) {
        setFailed("Failed to initialize EGL");
        return;
    }
    
    // Connect to backend signals
    connect(m_backend, &AndroidBackend::outputAdded, this, &AndroidEglBackend::addOutput);
    
    // Add existing outputs
    const auto outputs = m_backend->outputs();
    for (BackendOutput *output : outputs) {
        addOutput(output);
    }
    
    qInfo() << "Android EGL backend initialized with" << m_layers.size() << "layers";
}

bool AndroidEglBackend::initializeEgl()
{
    qInfo() << "Initializing Android EGL backend with Mesa software rendering";
    
    // Set Mesa environment for software rendering
    setenv("LIBGL_ALWAYS_SOFTWARE", "1", 1);
    setenv("GALLIUM_DRIVER", "llvmpipe", 1);
    setenv("MESA_GL_VERSION_OVERRIDE", "3.0", 1);
    
    // Get EGL display - use default display for software rendering
    EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (display == EGL_NO_DISPLAY) {
        qCritical() << "Failed to get EGL display";
        return false;
    }
    
    // Initialize EGL
    EGLint major, minor;
    if (!eglInitialize(display, &major, &minor)) {
        EGLint error = eglGetError();
        qCritical() << "eglInitialize failed:" << Qt::hex << error;
        qCritical() << "Mesa software rendering may not be available";
        return false;
    }
    
    qInfo() << "EGL initialized with Mesa - version:" << major << "." << minor;
    
    // Bind OpenGL ES API
    if (!eglBindAPI(EGL_OPENGL_ES_API)) {
        qCritical() << "Failed to bind OpenGL ES API";
        return false;
    }
    
    // Create EGL display wrapper
    auto eglDisplay = EglDisplay::create(display);
    if (!eglDisplay) {
        qCritical() << "Failed to create EglDisplay";
        return false;
    }
    
    setEglDisplay(eglDisplay.release());
    
    // Only create real EGL context for Mesa software rendering mode
    // (VirtualGL mode already returned above)
    
    // Choose EGL config
    const EGLint configAttribs[] = {
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_NONE
    };
    
    EGLConfig config;
    EGLint numConfigs;
    if (!eglChooseConfig(display, configAttribs, &config, 1, &numConfigs) || numConfigs == 0) {
        qCritical() << "Failed to choose EGL config";
        return false;
    }
    
    // Create EGL context
    const EGLint contextAttribs[] = {
        EGL_CONTEXT_CLIENT_VERSION, 2,
        EGL_NONE
    };
    
    EGLContext context = eglCreateContext(display, config, EGL_NO_CONTEXT, contextAttribs);
    if (context == EGL_NO_CONTEXT) {
        qCritical() << "Failed to create EGL context";
        return false;
    }
    
    // Create pbuffer surface for off-screen rendering
    const EGLint pbufferAttribs[] = {
        EGL_WIDTH, 1,
        EGL_HEIGHT, 1,
        EGL_NONE
    };
    
    EGLSurface surface = eglCreatePbufferSurface(display, config, pbufferAttribs);
    if (surface == EGL_NO_SURFACE) {
        qCritical() << "Failed to create pbuffer surface";
        eglDestroyContext(display, context);
        return false;
    }
    
    // Make context current
    if (!eglMakeCurrent(display, surface, surface, context)) {
        qCritical() << "Failed to make EGL context current";
        eglDestroySurface(display, surface);
        eglDestroyContext(display, context);
        return false;
    }
    
    qInfo() << "Mesa EGL context created and made current";
    
    // Log OpenGL information
    qInfo() << "OpenGL Vendor:" << (const char*)glGetString(GL_VENDOR);
    qInfo() << "OpenGL Renderer:" << (const char*)glGetString(GL_RENDERER);
    qInfo() << "OpenGL Version:" << (const char*)glGetString(GL_VERSION);
    
    return true;
}

void AndroidEglBackend::present(BackendOutput *output, const std::shared_ptr<OutputFrame> &frame)
{
    // For software rendering, presentation is handled in doEndFrame
    // where we copy the framebuffer to the termux-render buffer
    Q_UNUSED(output)
    Q_UNUSED(frame)
}

BackendOutput *AndroidEglBackend::findOutput(EGLNativeWindowType window) const
{
    // Not applicable for Android backend
    Q_UNUSED(window)
    return nullptr;
}

QList<OutputLayer *> AndroidEglBackend::compatibleOutputLayers(BackendOutput *output)
{
    // Find the layer for this output
    for (AndroidEglLayer *layer : m_layers) {
        if (layer->output() == output) {
            return {layer};
        }
    }
    
    // If no layer exists, create one
    addOutput(output);
    
    // Try again
    for (AndroidEglLayer *layer : m_layers) {
        if (layer->output() == output) {
            return {layer};
        }
    }
    
    return {};
}

bool AndroidEglBackend::isMesaAvailable()
{
    // Check if Mesa library is available
    void *mesaLib = dlopen("libGL.so", RTLD_LAZY | RTLD_LOCAL);
    if (!mesaLib) {
        mesaLib = dlopen("libGL.so.1", RTLD_LAZY | RTLD_LOCAL);
    }
    
    if (mesaLib) {
        dlclose(mesaLib);
        qInfo() << "Mesa library detected";
        return true;
    }
    
    // Check if mesa package is installed
    if (QFile::exists("/data/data/com.termux/files/usr/lib/libGL.so") ||
        QFile::exists("/data/data/com.termux/files/usr/lib/libGL.so.1")) {
        qInfo() << "Mesa library found in Termux";
        return true;
    }
    
    qWarning() << "Mesa library not found";
    return false;
}

bool AndroidEglBackend::isZinkAvailable()
{
    // Check if Vulkan is available (required for Zink)
    void *vulkanLib = dlopen("libvulkan.so", RTLD_LAZY | RTLD_LOCAL);
    if (!vulkanLib) {
        vulkanLib = dlopen("libvulkan.so.1", RTLD_LAZY | RTLD_LOCAL);
    }
    
    if (!vulkanLib) {
        qInfo() << "Vulkan library not found - Zink not available";
        return false;
    }
    
    dlclose(vulkanLib);
    
    // Detect environment type
    bool inPRoot = detectPRootEnvironment();
    bool inContainer = detectContainerEnvironment();
    
    qInfo() << "Environment detection:";
    qInfo() << "  PRoot:" << inPRoot;
    qInfo() << "  Container:" << inContainer;
    qInfo() << "  Plain Termux:" << (!inPRoot && !inContainer);
    
    // Check GPU device access based on environment
    if (inContainer) {
        // Real containers: check actual device files
        if (QFile::exists("/dev/dri/card0") || QFile::exists("/dev/dri/renderD128")) {
            qInfo() << "Container: GPU device files found - Zink available";
            return true;
        }
    } else if (inPRoot) {
        // PRoot: check both real and virtual device files
        if (checkPRootGPUAccess()) {
            qInfo() << "PRoot: GPU access available - Zink may work";
            return true;
        }
    } else {
        // Plain Termux: very limited GPU access
        qInfo() << "Plain Termux: GPU devices not accessible - Zink not available";
        return false;
    }
    
    qInfo() << "No GPU access found - Zink not available";
    return false;
}

bool AndroidEglBackend::detectPRootEnvironment()
{
    // Check for PRoot-specific environment variables
    if (getenv("PROOT_TMP_DIR") || getenv("PROOT_LOADER")) {
        return true;
    }
    
    // Check if we're in a chroot-like environment
    QFile procMounts("/proc/mounts");
    if (procMounts.open(QIODevice::ReadOnly)) {
        QString content = procMounts.readAll();
        if (content.contains("proot") || content.contains("bind")) {
            return true;
        }
    }
    
    // Check for typical PRoot mount points
    if (QFile::exists("/proc/version") && QFile::exists("/system/bin")) {
        QFile version("/proc/version");
        if (version.open(QIODevice::ReadOnly)) {
            QString versionStr = version.readAll();
            // PRoot often shows different kernel version than Android
            if (!versionStr.contains("Android")) {
                return true;
            }
        }
    }
    
    return false;
}

bool AndroidEglBackend::detectContainerEnvironment()
{
    // Check for container-specific files
    if (QFile::exists("/.dockerenv")) {
        return true;
    }
    
    // Check cgroup for container indicators
    QFile cgroup("/proc/1/cgroup");
    if (cgroup.open(QIODevice::ReadOnly)) {
        QString content = cgroup.readAll();
        if (content.contains("docker") || content.contains("lxc") || 
            content.contains("systemd") || content.contains("container")) {
            return true;
        }
    }
    
    return false;
}

bool AndroidEglBackend::checkPRootGPUAccess()
{
    // In PRoot, GPU devices might be mapped differently
    QStringList possiblePaths = {
        "/dev/dri/card0",
        "/dev/dri/renderD128",
        "/dev/dri/renderD129",
        "/sys/class/drm/card0",
        "/proc/dri/0",
        // PRoot might map these to different locations
        "/dev/graphics/fb0",
        "/dev/mali",
        "/dev/kgsl-3d0"
    };
    
    for (const QString &path : possiblePaths) {
        if (QFile::exists(path)) {
            qInfo() << "Found potential GPU device:" << path;
            
            // Try to open the device to test actual access
            QFile device(path);
            if (device.open(QIODevice::ReadOnly)) {
                qInfo() << "GPU device accessible:" << path;
                device.close();
                return true;
            } else {
                qInfo() << "GPU device exists but not accessible:" << path;
            }
        }
    }
    
    // Check if Vulkan can actually enumerate devices
    return testVulkanDeviceEnumeration();
}

bool AndroidEglBackend::testVulkanDeviceEnumeration()
{
    // Try to load Vulkan and enumerate devices
    void *vulkanLib = dlopen("libvulkan.so", RTLD_LAZY | RTLD_LOCAL);
    if (!vulkanLib) {
        vulkanLib = dlopen("libvulkan.so.1", RTLD_LAZY | RTLD_LOCAL);
    }
    
    if (!vulkanLib) {
        return false;
    }
    
    // Get Vulkan function pointers
    typedef int (*vkEnumerateInstanceExtensionPropertiesFunc)(const char*, uint32_t*, void*);
    auto vkEnumerateInstanceExtensionProperties = 
        (vkEnumerateInstanceExtensionPropertiesFunc)dlsym(vulkanLib, "vkEnumerateInstanceExtensionProperties");
    
    if (vkEnumerateInstanceExtensionProperties) {
        uint32_t extensionCount = 0;
        int result = vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, nullptr);
        
        dlclose(vulkanLib);
        
        if (result == 0 && extensionCount > 0) {
            qInfo() << "Vulkan found" << extensionCount << "extensions - GPU may be accessible";
            return true;
        }
    }
    
    dlclose(vulkanLib);
    return false;
}

AndroidEglBackend::RenderingMode AndroidEglBackend::detectBestRenderingMode()
{
    // Priority order: Zink (hardware) > llvmpipe (software) > fallback
    
    if (isZinkAvailable() && isMesaAvailable()) {
        qInfo() << "Zink hardware acceleration available";
        return RenderingMode::ZinkHardware;
    }
    
    if (isMesaAvailable()) {
        qInfo() << "Mesa software rendering available";
        return RenderingMode::LlvmpipeSoftware;
    }
    
    qWarning() << "No Mesa support - falling back to QPainter";
    return RenderingMode::Fallback;
}

void AndroidEglBackend::setupMesaRendering(RenderingMode mode)
{
    switch (mode) {
    case RenderingMode::ZinkHardware:
        qInfo() << "Configuring Mesa for Zink hardware acceleration";
        
        // Use Zink driver for hardware acceleration
        setenv("MESA_LOADER_DRIVER_OVERRIDE", "zink", 1);
        setenv("GALLIUM_DRIVER", "zink", 1);
        setenv("MESA_GL_VERSION_OVERRIDE", "3.3", 1);
        setenv("MESA_GLSL_VERSION_OVERRIDE", "330", 1);
        
        // Enable hardware features
        unsetenv("LIBGL_ALWAYS_SOFTWARE");
        setenv("MESA_NO_ERROR", "1", 1);
        
        // Vulkan optimization
        setenv("MESA_VK_DEVICE_SELECT_FORCE_DEFAULT_DEVICE", "1", 1);
        
        qInfo() << "Zink hardware acceleration configured";
        qInfo() << "GALLIUM_DRIVER=" << getenv("GALLIUM_DRIVER");
        break;
        
    case RenderingMode::LlvmpipeSoftware:
        qInfo() << "Configuring Mesa for llvmpipe software rendering";
        
        // Force software rendering
        setenv("LIBGL_ALWAYS_SOFTWARE", "1", 1);
        setenv("MESA_LOADER_DRIVER_OVERRIDE", "llvmpipe", 1);
        setenv("GALLIUM_DRIVER", "llvmpipe", 1);
        setenv("MESA_GL_VERSION_OVERRIDE", "2.1", 1);
        setenv("MESA_GLSL_VERSION_OVERRIDE", "120", 1);
        
        // Optimize software rendering
        setenv("MESA_NO_ERROR", "1", 1);
        setenv("LP_NUM_THREADS", "4", 1);  // Use multiple CPU threads
        
        qInfo() << "Mesa software rendering configured";
        qInfo() << "LIBGL_ALWAYS_SOFTWARE=" << getenv("LIBGL_ALWAYS_SOFTWARE");
        break;
        
    case RenderingMode::Fallback:
        qWarning() << "No Mesa rendering configured - will use QPainter fallback";
        break;
    }
    
    // Common Mesa settings
    if (mode != RenderingMode::Fallback) {
        setenv("MESA_DEBUG", "silent", 1);
        qInfo() << "Mesa rendering mode configured successfully";
    }
}

// Buffer management is handled by termux-render library
// No need to create/destroy buffers - they are global shared resources

void AndroidEglBackend::addOutput(BackendOutput *output)
{
    qInfo() << "Adding output to EGL backend:" << output->name();
    
    // Create layer for this output
    AndroidEglLayer *layer = new AndroidEglLayer(output, this);
    m_layers.append(layer);
    
    // Set the layer on the output if it's an AndroidOutput
    if (auto androidOutput = qobject_cast<AndroidOutput *>(output)) {
        androidOutput->setOutputLayer(layer);
    }
}

} // namespace Android
} // namespace KWin

#include "android_egl_backend.moc"