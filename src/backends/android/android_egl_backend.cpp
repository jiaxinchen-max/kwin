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

#include <QByteArray>
#include <QDebug>
#include <QFile>
#include <QProcess>
#include <dlfcn.h>
#include <unistd.h>
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <vector>

// LorieBuffer constants from buffer.h
#ifndef LORIEBUFFER_AHARDWAREBUFFER
#define LORIEBUFFER_AHARDWAREBUFFER 3
#endif

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
#ifdef __ANDROID__
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

#ifndef EGL_PLATFORM_SURFACELESS_MESA
#define EGL_PLATFORM_SURFACELESS_MESA 0x31DD
#endif

#ifndef EGL_PLATFORM_ANDROID_KHR
#define EGL_PLATFORM_ANDROID_KHR 0x3141
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
        // OpenGL framebuffers use a bottom-left origin, while termux-render
        // samples the AHardwareBuffer with Android's top-left origin.
        .renderTarget = RenderTarget(m_fbo.get(), OutputTransform::FlipY),
        .repaint = Region(0, 0, m_width, m_height),
    };
}

bool AndroidEglLayer::doEndFrame(const Region &renderedDeviceRegion, const Region &damagedDeviceRegion, OutputFrame *frame)
{
    m_renderTime->end();
    frame->addRenderTimeQuery(std::move(m_renderTime));
    
    if (m_useDirectRendering) {
        // Direct rendering mode - render directly to AHardwareBuffer.
        // No pixel copy needed, just signal frame completion
        struct lorie_shared_server_state *serverState = m_backend->androidBackend()->serverState();
        if (serverState) {
            // termux-render currently has no native-fence handoff. Wait for the
            // producer before waking the consumer; replace this with an EGL
            // native fence once the protocol can carry its fd.
            glFinish();
            lorie_mutex_lock(&serverState->lock, &serverState->lockingPid);
            serverState->waitForNextFrame = false;
            serverState->drawRequested = 1;
            if (rendererCond)
                pthread_cond_signal(rendererCond);
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
            struct lorie_shared_server_state *serverState = m_backend->androidBackend()->serverState();
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
                glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);

                serverState->waitForNextFrame = false;
                serverState->drawRequested = 1;
                if (rendererCond)
                    pthread_cond_signal(rendererCond);
                lorie_mutex_unlock(&serverState->lock, &serverState->lockingPid);

                ret = LorieBuffer_unlock((LorieBuffer*)m_buffer);
                if (ret != 0) {
                    qCritical() << "Failed to flush LorieBuffer";
                    return true;
                }

                qDebug() << "Copied frame to shared buffer:" << desc->width << "x" << desc->height;
            } else {
                LorieBuffer_unlock((LorieBuffer*)m_buffer);
                lorie_mutex_unlock(&serverState->lock, &serverState->lockingPid);
            }
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
    m_buffer = reinterpret_cast<Buffer *>(m_backend->androidBackend()->lorieBuffer());
    if (!m_buffer) {
        qCritical() << "Failed to get Android render buffer";
        return false;
    }
    
    // Check if we can use direct rendering with AHardwareBuffer.
    if (trySetupDirectRendering()) {
        qInfo() << "Using direct rendering to AHardwareBuffer";
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
#ifdef __ANDROID__
    qInfo() << "Attempting AHardwareBuffer direct rendering setup...";
    
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
    using GetNativeClientBufferAndroidProc = EGLClientBuffer(EGLAPIENTRYP)(const AHardwareBuffer *buffer);
    const auto getNativeClientBufferAndroid = reinterpret_cast<GetNativeClientBufferAndroidProc>(eglGetProcAddress("eglGetNativeClientBufferANDROID"));
    if (!getNativeClientBufferAndroid) {
        qWarning() << "eglGetNativeClientBufferANDROID is unavailable";
        return false;
    }
    EGLClientBuffer clientBuffer = getNativeClientBufferAndroid(ahb);
    if (!clientBuffer) {
        qDebug() << "Failed to get native client buffer from AHardwareBuffer";
        return false;
    }
    
    EGLint imageAttribs[] = {
        EGL_IMAGE_PRESERVED_KHR, EGL_TRUE,
        EGL_NONE
    };
    
    m_eglImage = m_backend->eglDisplayObject()->createImage(EGL_NO_CONTEXT, EGL_NATIVE_BUFFER_ANDROID,
                                                            clientBuffer, imageAttribs);
    
    if (m_eglImage == EGL_NO_IMAGE_KHR) {
        EGLint error = eglGetError();
        qDebug() << "Failed to create EGLImage from AHardwareBuffer, error:" << Qt::hex << error;
        return false;
    }
    
    // Create and bind texture from EGLImage
    glGenTextures(1, &m_texture);
    glBindTexture(GL_TEXTURE_2D, m_texture);
    using ImageTargetTexture2DProc = void(GL_APIENTRYP)(GLenum target, GLeglImageOES image);
    const auto imageTargetTexture2D = reinterpret_cast<ImageTargetTexture2DProc>(eglGetProcAddress("glEGLImageTargetTexture2DOES"));
    if (!imageTargetTexture2D) {
        qWarning() << "glEGLImageTargetTexture2DOES is unavailable";
        cleanupDirectRendering();
        return false;
    }
    imageTargetTexture2D(GL_TEXTURE_2D, m_eglImage);
    
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
    
    qInfo() << "Direct rendering to AHardwareBuffer setup successful";
    qInfo() << "Buffer size:" << desc->width << "x" << desc->height << "format:" << desc->format;
    
    return true;
    
#else
    qDebug() << "AHardwareBuffer direct rendering is only available on Android";
    return false;
#endif
}

void AndroidEglLayer::cleanupDirectRendering()
{
    if (m_eglImage != EGL_NO_IMAGE_KHR) {
        EGLDisplay display = eglGetCurrentDisplay();
        if (display != EGL_NO_DISPLAY) {
            m_backend->eglDisplayObject()->destroyImage(m_eglImage);
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
    qInfo() << "Initializing Android EGL backend";
    
    // Detect best rendering mode
    m_renderingMode = detectBestRenderingMode();
    m_eglAvailable = (m_renderingMode != RenderingMode::Fallback);
    
    if (!m_eglAvailable) {
        qWarning() << "No EGL renderer is available";
    } else {
        setupRendering(m_renderingMode);
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
    qInfo() << "Initializing Android EGL backend";
    if (!m_eglAvailable) {
        qCritical() << "No EGL renderer is available";
        return false;
    }

    setupRendering(m_renderingMode);
    
    const char *clientExtensions = eglQueryString(EGL_NO_DISPLAY, EGL_EXTENSIONS);
    const QByteArray clientExtensionsString = clientExtensions ? QByteArray(clientExtensions) : QByteArray();

    EGLDisplay display = EGL_NO_DISPLAY;
    using GetPlatformDisplayExtProc = EGLDisplay(EGLAPIENTRYP)(EGLenum platform, void *nativeDisplay, const EGLint *attribList);
    auto getPlatformDisplay = reinterpret_cast<GetPlatformDisplayExtProc>(eglGetProcAddress("eglGetPlatformDisplayEXT"));
    if (getPlatformDisplay && clientExtensionsString.split(' ').contains(QByteArrayLiteral("EGL_KHR_platform_android"))) {
        display = getPlatformDisplay(EGL_PLATFORM_ANDROID_KHR, EGL_DEFAULT_DISPLAY, nullptr);
        qInfo() << "Using Android EGL platform";
    }
    if (display == EGL_NO_DISPLAY && getPlatformDisplay
        && clientExtensionsString.split(' ').contains(QByteArrayLiteral("EGL_MESA_platform_surfaceless"))) {
        display = getPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, nullptr);
        qInfo() << "Using surfaceless EGL platform";
    }

    if (display == EGL_NO_DISPLAY) {
        qInfo() << "Using default EGL display";
        display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    }
    if (display == EGL_NO_DISPLAY) {
        qCritical() << "Failed to get EGL display";
        return false;
    }

    // Create EGL display wrapper
    const bool systemGles = m_renderingMode == RenderingMode::SystemGlesHardware;
    auto eglDisplay = EglDisplay::create(display, true, !systemGles);
    if (!eglDisplay) {
        qCritical() << "Failed to create EglDisplay";
        return false;
    }
    
    setEglDisplay(eglDisplay.release());

    EGLConfig config = EGL_NO_CONFIG_KHR;
    if (systemGles) {
        const EGLint configAttributes[] = {
            EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT | EGL_OPENGL_ES3_BIT_KHR,
            EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
            EGL_RED_SIZE, 8,
            EGL_GREEN_SIZE, 8,
            EGL_BLUE_SIZE, 8,
            EGL_ALPHA_SIZE, 8,
            EGL_NONE,
        };
        EGLint configCount = 0;
        if (!eglChooseConfig(display, configAttributes, &config, 1, &configCount) || configCount == 0) {
            qCritical() << "Failed to choose Android system EGL config";
            return false;
        }
    }
    if (!createContext(config) || !openglContext()->makeCurrent()) {
        qCritical() << "Failed to create KWin EGL context";
        return false;
    }
    
    qInfo() << "Android EGL context created and made current";
    
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
    // Zink requires a Vulkan implementation, not a DRM render node. In native
    // Termux an ICD can use Android's vendor Vulkan stack without exposing
    // /dev/dri, so probe the Vulkan loader and its selected ICD directly.
    void *vulkanLib = dlopen("libvulkan.so", RTLD_LAZY | RTLD_LOCAL);
    if (!vulkanLib) {
        vulkanLib = dlopen("libvulkan.so.1", RTLD_LAZY | RTLD_LOCAL);
    }
    
    if (!vulkanLib) {
        qInfo() << "Vulkan library not found - Zink not available";
        return false;
    }
    
    dlclose(vulkanLib);
    return testVulkanDeviceEnumeration();
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
    
    qInfo() << "PRoot: no accessible GPU device found";
    return false;
}

bool AndroidEglBackend::testVulkanDeviceEnumeration()
{
    // Keep this probe dynamically linked: Vulkan is optional for the Android
    // backend and KWin must still build where Vulkan headers are unavailable.
    using VkResult = int32_t;
    using VkInstance = void *;
    using VkPhysicalDevice = void *;
    constexpr VkResult VK_SUCCESS = 0;
    constexpr uint32_t VK_STRUCTURE_TYPE_APPLICATION_INFO = 0;
    constexpr uint32_t VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO = 1;
    constexpr uint32_t VK_API_VERSION_1_1 = (1U << 22) | (1U << 12);

    struct VkApplicationInfo {
        uint32_t sType;
        const void *pNext;
        const char *pApplicationName;
        uint32_t applicationVersion;
        const char *pEngineName;
        uint32_t engineVersion;
        uint32_t apiVersion;
    };
    struct VkInstanceCreateInfo {
        uint32_t sType;
        const void *pNext;
        uint32_t flags;
        const VkApplicationInfo *pApplicationInfo;
        uint32_t enabledLayerCount;
        const char *const *ppEnabledLayerNames;
        uint32_t enabledExtensionCount;
        const char *const *ppEnabledExtensionNames;
    };
    using CreateInstance = VkResult (*)(const VkInstanceCreateInfo *, const void *, VkInstance *);
    using DestroyInstance = void (*)(VkInstance, const void *);
    using EnumeratePhysicalDevices = VkResult (*)(VkInstance, uint32_t *, VkPhysicalDevice *);
    struct VkExtensionProperties {
        char extensionName[256];
        uint32_t specVersion;
    };
    struct VkPhysicalDeviceFeatures2 {
        uint32_t sType;
        void *pNext;
        uint32_t features[55];
    };
    struct VkPhysicalDeviceRobustness2FeaturesEXT {
        uint32_t sType;
        void *pNext;
        uint32_t robustBufferAccess2;
        uint32_t robustImageAccess2;
        uint32_t nullDescriptor;
    };
    using EnumerateDeviceExtensionProperties = VkResult (*)(VkPhysicalDevice, const char *, uint32_t *, VkExtensionProperties *);
    using GetPhysicalDeviceFeatures2 = void (*)(VkPhysicalDevice, VkPhysicalDeviceFeatures2 *);
    struct alignas(8) VkPhysicalDeviceProperties {
        uint32_t apiVersion;
        uint32_t driverVersion;
        uint32_t vendorID;
        uint32_t deviceID;
        uint32_t deviceType;
        char deviceName[256];
        uint8_t pipelineCacheUUID[16];
        uint8_t remainingProperties[4096];
    };
    using GetPhysicalDeviceProperties = void (*)(VkPhysicalDevice, VkPhysicalDeviceProperties *);
    constexpr uint32_t VK_PHYSICAL_DEVICE_TYPE_CPU = 4;

    void *vulkanLib = dlopen("libvulkan.so", RTLD_LAZY | RTLD_LOCAL);
    if (!vulkanLib) {
        vulkanLib = dlopen("libvulkan.so.1", RTLD_LAZY | RTLD_LOCAL);
    }
    
    if (!vulkanLib) {
        return false;
    }
    
    const auto createInstance = reinterpret_cast<CreateInstance>(dlsym(vulkanLib, "vkCreateInstance"));
    const auto destroyInstance = reinterpret_cast<DestroyInstance>(dlsym(vulkanLib, "vkDestroyInstance"));
    const auto enumeratePhysicalDevices = reinterpret_cast<EnumeratePhysicalDevices>(dlsym(vulkanLib, "vkEnumeratePhysicalDevices"));
    const auto enumerateDeviceExtensionProperties = reinterpret_cast<EnumerateDeviceExtensionProperties>(dlsym(vulkanLib, "vkEnumerateDeviceExtensionProperties"));
    const auto getPhysicalDeviceFeatures2 = reinterpret_cast<GetPhysicalDeviceFeatures2>(dlsym(vulkanLib, "vkGetPhysicalDeviceFeatures2"));
    const auto getPhysicalDeviceProperties = reinterpret_cast<GetPhysicalDeviceProperties>(dlsym(vulkanLib, "vkGetPhysicalDeviceProperties"));
    if (!createInstance || !destroyInstance || !enumeratePhysicalDevices || !enumerateDeviceExtensionProperties || !getPhysicalDeviceFeatures2 || !getPhysicalDeviceProperties) {
        qWarning() << "Vulkan loader lacks required instance entry points";
        dlclose(vulkanLib);
        return false;
    }

    const VkApplicationInfo applicationInfo = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pNext = nullptr,
        .pApplicationName = "kwin-android-zink-probe",
        .applicationVersion = 0,
        .pEngineName = "KWin",
        .engineVersion = 0,
        // vkGetPhysicalDeviceFeatures2 is a Vulkan 1.1 core command. With a
        // Vulkan 1.0 instance, Turnip leaves the robustness2 feature chain
        // untouched and nullDescriptor is incorrectly observed as false.
        .apiVersion = VK_API_VERSION_1_1,
    };
    const VkInstanceCreateInfo createInfo = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .pApplicationInfo = &applicationInfo,
        .enabledLayerCount = 0,
        .ppEnabledLayerNames = nullptr,
        .enabledExtensionCount = 0,
        .ppEnabledExtensionNames = nullptr,
    };
    VkInstance instance = nullptr;
    const VkResult createResult = createInstance(&createInfo, nullptr, &instance);
    if (createResult != VK_SUCCESS || !instance) {
        qWarning() << "Vulkan instance creation failed:" << createResult;
        dlclose(vulkanLib);
        return false;
    }

    uint32_t deviceCount = 0;
    VkResult enumerateResult = enumeratePhysicalDevices(instance, &deviceCount, nullptr);
    std::vector<VkPhysicalDevice> devices(deviceCount);
    if (enumerateResult == VK_SUCCESS && deviceCount > 0) {
        enumerateResult = enumeratePhysicalDevices(instance, &deviceCount, devices.data());
        devices.resize(deviceCount);
    }

    bool zinkCompatibleHardwareFound = false;
    if (enumerateResult == VK_SUCCESS) {
        for (VkPhysicalDevice device : devices) {
            VkPhysicalDeviceProperties properties = {};
            getPhysicalDeviceProperties(device, &properties);
            const bool isHardware = properties.deviceType != VK_PHYSICAL_DEVICE_TYPE_CPU;
            uint32_t extensionCount = 0;
            const VkResult extensionResult = enumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);
            std::vector<VkExtensionProperties> extensions(extensionCount);
            if (extensionResult == VK_SUCCESS && extensionCount > 0) {
                enumerateDeviceExtensionProperties(device, nullptr, &extensionCount, extensions.data());
                extensions.resize(extensionCount);
            }
            const bool hasRobustness2 = std::any_of(extensions.cbegin(), extensions.cend(), [](const VkExtensionProperties &extension) {
                return std::strcmp(extension.extensionName, "VK_EXT_robustness2") == 0;
            });
            VkPhysicalDeviceRobustness2FeaturesEXT robustness2 = {
                .sType = 1000286000,
                .pNext = nullptr,
                .robustBufferAccess2 = 0,
                .robustImageAccess2 = 0,
                .nullDescriptor = 0,
            };
            VkPhysicalDeviceFeatures2 features2 = {
                .sType = 1000059000,
                .pNext = &robustness2,
                .features = {},
            };
            if (hasRobustness2) {
                getPhysicalDeviceFeatures2(device, &features2);
            }
            const bool supportsNullDescriptor = hasRobustness2 && robustness2.nullDescriptor;
            qInfo() << "Vulkan physical device:" << properties.deviceName
                    << "type:" << properties.deviceType
                    << "vendor:" << Qt::hex << properties.vendorID << Qt::dec
                    << (isHardware ? "hardware" : "CPU")
                    << "VK_EXT_robustness2.nullDescriptor:" << supportsNullDescriptor;
            zinkCompatibleHardwareFound |= isHardware && supportsNullDescriptor;
        }
    }
    destroyInstance(instance, nullptr);
    dlclose(vulkanLib);
    if (enumerateResult != VK_SUCCESS || deviceCount == 0 || !zinkCompatibleHardwareFound) {
        qWarning() << "No Zink-compatible Vulkan device found:" << enumerateResult << "devices:" << deviceCount
                   << "(Zink requires VK_EXT_robustness2 with nullDescriptor)";
        return false;
    }

    qInfo() << "Vulkan ICD probe found a Zink-compatible hardware device among" << deviceCount << "physical device(s)";
    return true;
}

AndroidEglBackend::RenderingMode AndroidEglBackend::detectBestRenderingMode()
{
    const QByteArray requestedMode = qgetenv("KWIN_ANDROID_GL_MODE").toLower();
    if (requestedMode == "system" || requestedMode == "system-gles" || requestedMode == "gles") {
        qInfo() << "KWIN_ANDROID_GL_MODE requests Android system GLES";
        return RenderingMode::SystemGlesHardware;
    }
    if (requestedMode == "llvmpipe" || requestedMode == "software") {
        qInfo() << "KWIN_ANDROID_GL_MODE requests llvmpipe";
        return isMesaAvailable() ? RenderingMode::LlvmpipeSoftware : RenderingMode::Fallback;
    }
    if (requestedMode == "zink" || requestedMode == "hardware") {
        qInfo() << "KWIN_ANDROID_GL_MODE requests zink";
        if (isZinkAvailable() && isMesaAvailable()) {
            return RenderingMode::ZinkHardware;
        }
        qWarning() << "Requested Zink rendering is unavailable; falling back to Android system GLES";
        return RenderingMode::SystemGlesHardware;
    }

    const QByteArray mesaDriver = qgetenv("MESA_LOADER_DRIVER_OVERRIDE").toLower();
    const QByteArray galliumDriver = qgetenv("GALLIUM_DRIVER").toLower();
    if (mesaDriver == "llvmpipe" || mesaDriver == "swrast" || galliumDriver == "llvmpipe") {
        qInfo() << "Existing Mesa environment requests llvmpipe";
        return isMesaAvailable() ? RenderingMode::LlvmpipeSoftware : RenderingMode::Fallback;
    }
    if (mesaDriver == "zink" || galliumDriver == "zink") {
        qInfo() << "Existing Mesa environment requests zink";
        if (isZinkAvailable() && isMesaAvailable()) {
            return RenderingMode::ZinkHardware;
        }
        qWarning() << "Configured Zink device is not hardware-backed; falling back to Android system GLES";
        return RenderingMode::SystemGlesHardware;
    }

    // The Android backend defaults to the platform EGL/GLES implementation.
    // This works without DRM devices or Vulkan feature emulation.
    if (requestedMode.isEmpty()) {
        qInfo() << "Selecting Android system GLES by default";
        return RenderingMode::SystemGlesHardware;
    }

    // Priority order for explicitly configured Mesa paths:
    // Zink (hardware) > llvmpipe (software) > fallback.
    
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

void AndroidEglBackend::setupRendering(RenderingMode mode)
{
    // Android's compositor path uses OpenGL ES. Respect an explicit user value.
    setenv("KWIN_COMPOSE", "O2ES", 0);
    unsetenv("MESA_GLSL_VERSION_OVERRIDE");

    switch (mode) {
    case RenderingMode::SystemGlesHardware:
        qInfo() << "Configuring Android system EGL/GLES hardware acceleration";
        setenv("KWIN_COMPOSE", "O2ES", 1);
        unsetenv("LIBGL_ALWAYS_SOFTWARE");
        unsetenv("MESA_LOADER_DRIVER_OVERRIDE");
        unsetenv("GALLIUM_DRIVER");
        unsetenv("MESA_GL_VERSION_OVERRIDE");
        unsetenv("MESA_GLES_VERSION_OVERRIDE");
        unsetenv("MESA_VK_DEVICE_SELECT_FORCE_DEFAULT_DEVICE");
        unsetenv("MESA_NO_ERROR");
        unsetenv("MESA_DEBUG");
        break;
    case RenderingMode::ZinkHardware:
        qInfo() << "Configuring Mesa for Zink hardware acceleration";
        
        // Use Zink driver for hardware acceleration
        setenv("MESA_LOADER_DRIVER_OVERRIDE", "zink", 1);
        setenv("GALLIUM_DRIVER", "zink", 1);
        setenv("MESA_GL_VERSION_OVERRIDE", "3.3", 1);
        setenv("MESA_GLES_VERSION_OVERRIDE", "3.0", 1);
        
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
        setenv("MESA_GL_VERSION_OVERRIDE", "3.3", 1);
        setenv("MESA_GLES_VERSION_OVERRIDE", "3.0", 1);
        unsetenv("MESA_VK_DEVICE_SELECT_FORCE_DEFAULT_DEVICE");
        
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
    if (mode == RenderingMode::ZinkHardware || mode == RenderingMode::LlvmpipeSoftware) {
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
