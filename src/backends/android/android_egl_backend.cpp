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
#include "wayland/linuxdmabufv1clientbuffer.h"
#include "wayland_server.h"

#include <QByteArray>
#include <QDebug>
#include <QLoggingCategory>
#include <dlfcn.h>
#include <drm_fourcc.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cstdlib>
#include <cstring>
#include <limits>
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

Q_LOGGING_CATEGORY(KWIN_ANDROID_EGL, "kwin.android.egl", QtInfoMsg)

#ifdef __ANDROID__
namespace
{

struct NativeHandle
{
    int version;
    int numFds;
    int numInts;
    int data[];
};

using GetNativeHandleProc = const NativeHandle *(*)(const AHardwareBuffer *buffer);
using PlatformDlopenProc = void *(*)(const char *filename, int flags);
using DupDmaBufFdProc = int (*)(const LorieBuffer *buffer);

DupDmaBufFdProc resolveLorieBufferDupDmaBufFd()
{
    static const auto proc = reinterpret_cast<DupDmaBufFdProc>(
        dlsym(RTLD_DEFAULT, "LorieBuffer_dupDmaBufFd"));
    return proc;
}

GetNativeHandleProc resolveGetNativeHandle()
{
    static const GetNativeHandleProc proc = []() -> GetNativeHandleProc {
        auto platformDlopen = reinterpret_cast<PlatformDlopenProc>(dlsym(RTLD_DEFAULT, "platform_dlopen"));
        if (!platformDlopen) {
            void *platformNamespaceLibrary = dlopen("libtermux-platform-ns.so", RTLD_NOW | RTLD_LOCAL);
            if (platformNamespaceLibrary) {
                platformDlopen = reinterpret_cast<PlatformDlopenProc>(dlsym(platformNamespaceLibrary, "platform_dlopen"));
            }
        }
        if (!platformDlopen) {
            qWarning() << "platform_dlopen is unavailable; dma-buf AHardwareBuffer import is disabled";
            return nullptr;
        }

#if defined(__LP64__)
        constexpr const char *systemLibAndroid = "/system/lib64/libandroid.so";
#else
        constexpr const char *systemLibAndroid = "/system/lib/libandroid.so";
#endif
        void *handle = platformDlopen(systemLibAndroid, RTLD_NOW | RTLD_LOCAL);
        if (!handle) {
            qWarning() << "Failed to open Android platform libandroid.so:" << dlerror();
            return nullptr;
        }

        const auto getNativeHandle = reinterpret_cast<GetNativeHandleProc>(dlsym(handle, "AHardwareBuffer_getNativeHandle"));
        if (!getNativeHandle) {
            qWarning() << "AHardwareBuffer_getNativeHandle is unavailable:" << dlerror();
        }
        return getNativeHandle;
    }();
    return proc;
}

std::optional<uint32_t> drmFormatForHardwareBuffer(uint8_t format)
{
    switch (format) {
    case AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM:
        return DRM_FORMAT_ABGR8888;
    case AHARDWAREBUFFER_FORMAT_R8G8B8X8_UNORM:
        return DRM_FORMAT_XBGR8888;
    case AHARDWAREBUFFER_FORMAT_B8G8R8A8_UNORM:
        return DRM_FORMAT_ARGB8888;
    default:
        return std::nullopt;
    }
}

}
#endif

AndroidEglLayer::AndroidEglLayer(BackendOutput *output, AndroidEglBackend *backend)
    : OutputLayer(output, OutputLayerType::Primary)
    , m_backend(backend)
{
    qCDebug(KWIN_ANDROID_EGL) << "Created AndroidEglLayer for output" << output->name();
}

AndroidEglLayer::~AndroidEglLayer()
{
    cleanup();
    qCDebug(KWIN_ANDROID_EGL) << "Destroyed AndroidEglLayer";
}

std::optional<OutputLayerBeginFrameInfo> AndroidEglLayer::doBeginFrame()
{
    const QSize nativeSize(m_output->modeSize());
    qCDebug(KWIN_ANDROID_EGL) << "AndroidEglLayer::doBeginFrame() - size:" << nativeSize;
    
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
        qCDebug(KWIN_ANDROID_EGL) << "Using direct rendering to AHardwareBuffer";
        
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
            
            qCDebug(KWIN_ANDROID_EGL) << "Direct rendering frame completed - zero copy";
        }
        return true;
    }
    
    // Explicit readback output: copy the EGL framebuffer to the shared buffer.
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
            if (desc && shared_buffer && desc->width > 0 && desc->height > 0 && desc->stride >= desc->width) {
                constexpr size_t bytesPerPixel = 4;
                const size_t rowBytes = static_cast<size_t>(desc->width) * bytesPerPixel;
                const size_t destinationRowBytes = static_cast<size_t>(desc->stride) * bytesPerPixel;
                if (static_cast<size_t>(desc->height) > std::numeric_limits<size_t>::max() / rowBytes) {
                    qCritical() << "Android readback buffer size overflow";
                    LorieBuffer_unlock((LorieBuffer *)m_buffer);
                    lorie_mutex_unlock(&serverState->lock, &serverState->lockingPid);
                    return true;
                }

                m_readbackBuffer.resize(rowBytes * static_cast<size_t>(desc->height));

                // Bind our framebuffer to read from it
                glBindFramebuffer(GL_READ_FRAMEBUFFER, m_framebuffer);

                // glReadPixels writes tightly packed rows, while AHardwareBuffer
                // rows may contain padding (desc->stride > desc->width). Read to
                // a tightly packed staging buffer and copy each row using the
                // destination stride to avoid progressively skewing the image.
                glPixelStorei(GL_PACK_ALIGNMENT, 4);
                glReadPixels(0, 0, desc->width, desc->height, GL_RGBA, GL_UNSIGNED_BYTE, m_readbackBuffer.data());
                glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);

                auto *destination = static_cast<uint8_t *>(shared_buffer);
                for (int y = 0; y < desc->height; ++y) {
                    std::memcpy(destination + static_cast<size_t>(y) * destinationRowBytes,
                                m_readbackBuffer.data() + static_cast<size_t>(y) * rowBytes,
                                rowBytes);
                }

                ret = LorieBuffer_unlock((LorieBuffer*)m_buffer);
                if (ret != 0) {
                    qCritical() << "Failed to flush LorieBuffer";
                    lorie_mutex_unlock(&serverState->lock, &serverState->lockingPid);
                    return true;
                }

                // Do not wake the consumer until AHardwareBuffer_unlock has
                // flushed the CPU writes. The shared mutex keeps the producer
                // and consumer from accessing the single buffer concurrently.
                serverState->waitForNextFrame = false;
                serverState->drawRequested = 1;
                if (rendererCond)
                    pthread_cond_signal(rendererCond);
                lorie_mutex_unlock(&serverState->lock, &serverState->lockingPid);

                qCDebug(KWIN_ANDROID_EGL) << "Copied frame to shared buffer:" << desc->width << "x" << desc->height
                                          << "stride:" << desc->stride;
            } else {
                qCritical() << "Invalid LorieBuffer mapping or stride";
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
    qCDebug(KWIN_ANDROID_EGL) << "Setting up render target:" << nativeSize;
    
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
    
    // The renderer pins exactly one output-buffer combination. Never fall back
    // between them: fail loudly so a broken device reveals which combination did
    // not work instead of silently running a different one.
    switch (resolveOutputImport()) {
    case OutputImport::Native:
        if (!trySetupAndroidNativeBufferDirectRendering()) {
            qCritical() << "System GLES pins EGL_NATIVE_BUFFER_ANDROID import, but it failed;"
                        << "refusing to fall back to dma-buf or CPU readback";
            return false;
        }
        m_useDirectRendering = true;
        return true;
    case OutputImport::DmaBuf:
        if (!trySetupDmaBufDirectRendering()) {
            qCritical() << "Zink pins dma-buf AHardwareBuffer import, but it failed;"
                        << "refusing to fall back to native import or CPU readback";
            return false;
        }
        m_useDirectRendering = true;
        return true;
    case OutputImport::Invalid:
        qCritical() << "No valid output buffer import for the selected renderer;"
                    << "check KWIN_ANDROID_GL_MODE and KWIN_ANDROID_AHB_IMPORT";
        return false;
    case OutputImport::Readback:
        break;
    }

    // Pinned CPU readback path (llvmpipe, or KWIN_ANDROID_AHB_IMPORT=off): KWin
    // renders into a plain texture FBO and doEndFrame() copies it into the
    // shared buffer. This is a chosen combination, not a failure fallback.
    qInfo() << "Using CPU readback output path (glReadPixels)";
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
    
    qCDebug(KWIN_ANDROID_EGL) << "Render target setup successful";
    return true;
}

AndroidEglLayer::OutputImport AndroidEglLayer::resolveOutputImport() const
{
    // Each renderer pins one complete output-buffer combination (mirrors the
    // KWIN_ANDROID_AHB_IMPORT table in packaging/TERMUX_AHB_BUILD_GUIDE.md).
    // kwin-android-rendering-env exports the matching value; an explicit
    // KWIN_ANDROID_AHB_IMPORT only overrides it for experiments.
    OutputImport pinned = OutputImport::Invalid;
    switch (m_backend->renderingMode()) {
    case AndroidEglBackend::RenderingMode::SystemGlesHardware:
        pinned = OutputImport::Native;
        break;
    case AndroidEglBackend::RenderingMode::ZinkHardware:
        pinned = OutputImport::DmaBuf;
        break;
    case AndroidEglBackend::RenderingMode::LlvmpipeSoftware:
        pinned = OutputImport::Readback;
        break;
    case AndroidEglBackend::RenderingMode::Invalid:
        return OutputImport::Invalid;
    }

    const QByteArray requested = qgetenv("KWIN_ANDROID_AHB_IMPORT").trimmed().toLower();
    if (requested.isEmpty()) {
        return pinned;
    }
    if (requested == "native") {
        return OutputImport::Native;
    }
    if (requested == "dmabuf") {
        return OutputImport::DmaBuf;
    }
    if (requested == "off" || requested == "0" || requested == "false" || requested == "readback") {
        return OutputImport::Readback;
    }
    qWarning() << "Unsupported KWIN_ANDROID_AHB_IMPORT value:" << requested
               << "- expected native, dmabuf or off";
    return OutputImport::Invalid;
}

bool AndroidEglLayer::trySetupDmaBufDirectRendering()
{
#ifdef __ANDROID__
    // Get LorieBuffer description to access AHardwareBuffer
    const LorieBuffer_Desc *desc = LorieBuffer_description((LorieBuffer*)m_buffer);
    if (!desc) {
        qCDebug(KWIN_ANDROID_EGL) << "No buffer description available";
        return false;
    }
    
    // Check if this is an AHardwareBuffer type
    if (desc->type != LORIEBUFFER_AHARDWAREBUFFER) {
        qCDebug(KWIN_ANDROID_EGL) << "LorieBuffer is not AHardwareBuffer type, got type:" << desc->type;
        return false;
    }
    
    EglDisplay *eglDisplay = m_backend->eglDisplayObject();
    if (!eglDisplay || !eglDisplay->hasExtension(QByteArrayLiteral("EGL_EXT_image_dma_buf_import"))) {
        qCDebug(KWIN_ANDROID_EGL) << "EGL_EXT_image_dma_buf_import is unavailable";
        return false;
    }

    const auto drmFormat = drmFormatForHardwareBuffer(desc->format);
    if (!drmFormat) {
        qWarning() << "Unsupported AHardwareBuffer format for dma-buf import:" << desc->format;
        return false;
    }
    if (desc->width <= 0 || desc->height <= 0 || desc->stride < desc->width) {
        qWarning() << "Invalid AHardwareBuffer geometry for dma-buf import:"
                   << desc->width << desc->height << desc->stride;
        return false;
    }

    int dmaBufFd = -1;
    int nativeHandleFdCount = 0;
    if (const auto dupDmaBufFd = resolveLorieBufferDupDmaBufFd()) {
        dmaBufFd = dupDmaBufFd(reinterpret_cast<LorieBuffer *>(m_buffer));
    }

    if (dmaBufFd < 0 && desc->buffer) {
        const auto getNativeHandle = resolveGetNativeHandle();
        if (!getNativeHandle) {
            return false;
        }

        const NativeHandle *nativeHandle = getNativeHandle(desc->buffer);
        if (!nativeHandle || nativeHandle->version < int(sizeof(NativeHandle))
            || nativeHandle->numFds < 1 || nativeHandle->numFds > 4
            || nativeHandle->numInts < 0) {
            qWarning() << "Invalid AHardwareBuffer native handle";
            return false;
        }
        nativeHandleFdCount = nativeHandle->numFds;
        dmaBufFd = fcntl(nativeHandle->data[0], F_DUPFD_CLOEXEC, 0);
    }

    if (dmaBufFd < 0) {
        qWarning() << "LorieBuffer exposes no usable dma-buf fd:" << strerror(errno);
        return false;
    }

    DmaBufAttributes attributes;
    attributes.planeCount = 1;
    attributes.width = desc->width;
    attributes.height = desc->height;
    attributes.format = *drmFormat;
    attributes.modifier = DRM_FORMAT_MOD_INVALID;
    attributes.fd[0] = FileDescriptor(dmaBufFd);
    attributes.offset[0] = 0;
    attributes.pitch[0] = uint32_t(desc->stride) * 4;

    m_eglImage = eglDisplay->importDmaBufAsImage(attributes);
    if (m_eglImage == EGL_NO_IMAGE_KHR) {
        qWarning() << "Failed to import AHardwareBuffer dma-buf as EGLImage, error:"
                   << Qt::hex << eglGetError();
        return false;
    }

    if (!setupDirectFramebuffer()) {
        cleanupDirectRendering();
        return false;
    }

    qInfo() << "AHardwareBuffer dma-buf direct import setup successful"
            << "size:" << desc->width << "x" << desc->height
            << "stride:" << desc->stride << "format:" << Qt::hex << *drmFormat
            << "native handle fds:" << nativeHandleFdCount;
    return true;
#else
    return false;
#endif
}

bool AndroidEglLayer::trySetupAndroidNativeBufferDirectRendering()
{
#ifdef __ANDROID__
    const LorieBuffer_Desc *desc = LorieBuffer_description((LorieBuffer *)m_buffer);
    if (!desc || desc->type != LORIEBUFFER_AHARDWAREBUFFER || !desc->buffer) {
        return false;
    }

    AHardwareBuffer *ahb = desc->buffer;
    EGLDisplay display = eglGetCurrentDisplay();
    if (display == EGL_NO_DISPLAY) {
        qCDebug(KWIN_ANDROID_EGL) << "No current EGL display";
        return false;
    }
    
    // Check for required EGL extensions
    const char *extensions = eglQueryString(display, EGL_EXTENSIONS);
    if (!extensions) {
        qCDebug(KWIN_ANDROID_EGL) << "Failed to query EGL extensions";
        return false;
    }
    
    bool hasNativeBufferAndroid = strstr(extensions, "EGL_ANDROID_image_native_buffer") != nullptr;
    bool hasImageKHR = strstr(extensions, "EGL_KHR_image") != nullptr || 
                       strstr(extensions, "EGL_KHR_image_base") != nullptr;
    
    if (!hasNativeBufferAndroid || !hasImageKHR) {
        qCDebug(KWIN_ANDROID_EGL) << "Required EGL extensions not available:";
        qCDebug(KWIN_ANDROID_EGL) << "  EGL_ANDROID_image_native_buffer:" << hasNativeBufferAndroid;
        qCDebug(KWIN_ANDROID_EGL) << "  EGL_KHR_image:" << hasImageKHR;
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
        qCDebug(KWIN_ANDROID_EGL) << "Failed to get native client buffer from AHardwareBuffer";
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
        qCDebug(KWIN_ANDROID_EGL) << "Failed to create EGLImage from AHardwareBuffer, error:" << Qt::hex << error;
        return false;
    }
    
    if (!setupDirectFramebuffer()) {
        cleanupDirectRendering();
        return false;
    }

    qInfo() << "Direct rendering through EGL_ANDROID_image_native_buffer setup successful";
    qCDebug(KWIN_ANDROID_EGL) << "Buffer size:" << desc->width << "x" << desc->height << "format:" << desc->format;

    return true;
#else
    return false;
#endif
}

bool AndroidEglLayer::setupDirectFramebuffer()
{
#ifdef __ANDROID__
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
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    
    // Create framebuffer and attach texture
    glGenFramebuffers(1, &m_framebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, m_framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_texture, 0);
    
    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        qCDebug(KWIN_ANDROID_EGL) << "Framebuffer not complete:" << Qt::hex << status;
        cleanupDirectRendering();
        return false;
    }
    
    return true;
#else
    return false;
#endif
}

void AndroidEglLayer::cleanupDirectRendering()
{
    if (m_framebuffer) {
        glDeleteFramebuffers(1, &m_framebuffer);
        m_framebuffer = 0;
    }

    if (m_texture) {
        glDeleteTextures(1, &m_texture);
        m_texture = 0;
    }

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

static const char *renderingModeName(AndroidEglBackend::RenderingMode mode)
{
    switch (mode) {
    case AndroidEglBackend::RenderingMode::SystemGlesHardware:
        return "system (Android EGL/GLES)";
    case AndroidEglBackend::RenderingMode::ZinkHardware:
        return "zink (Mesa on Vulkan)";
    case AndroidEglBackend::RenderingMode::LlvmpipeSoftware:
        return "llvmpipe (Mesa software)";
    case AndroidEglBackend::RenderingMode::Invalid:
        return "none";
    }
    Q_UNREACHABLE_RETURN("none");
}

// Reject a contradictory environment before libepoxy initializes a GL loader.
static bool validateRenderingEnvironment(AndroidEglBackend::RenderingMode mode)
{
    const QByteArray termuxZink = qgetenv("TERMUX_ANDROID_ZINK");
    const bool epoxyLoadsMesa = !termuxZink.isEmpty() && termuxZink != "0";
    const QByteArray mesaDriver = qgetenv("MESA_LOADER_DRIVER_OVERRIDE").toLower();
    const QByteArray compose = qgetenv("KWIN_COMPOSE");
    bool valid = false;
    switch (mode) {
    case AndroidEglBackend::RenderingMode::SystemGlesHardware:
        valid = !epoxyLoadsMesa && mesaDriver.isEmpty() && compose == "O2ES";
        break;
    case AndroidEglBackend::RenderingMode::ZinkHardware:
        valid = epoxyLoadsMesa && mesaDriver == "zink" && compose == "O2";
        break;
    case AndroidEglBackend::RenderingMode::LlvmpipeSoftware:
        valid = epoxyLoadsMesa && mesaDriver == "llvmpipe" && compose == "O2";
        break;
    case AndroidEglBackend::RenderingMode::Invalid:
        break;
    }
    if (!valid) {
        qCritical() << "Requested renderer conflicts with environment:" << renderingModeName(mode)
                    << "TERMUX_ANDROID_ZINK=" << termuxZink
                    << "MESA_LOADER_DRIVER_OVERRIDE=" << mesaDriver << "KWIN_COMPOSE=" << compose;
    }
    return valid;
}

static bool verifyRendererMatchesMode(AndroidEglBackend::RenderingMode mode)
{
    const char *value = reinterpret_cast<const char *>(glGetString(GL_RENDERER));
    const QByteArray renderer(value ? value : "");
    const QByteArray lower = renderer.toLower();
    const bool software = lower.contains("llvmpipe") || lower.contains("lavapipe")
        || lower.contains("softpipe") || lower.contains("software rasterizer");
    bool matches = false;
    switch (mode) {
    case AndroidEglBackend::RenderingMode::SystemGlesHardware:
        matches = !renderer.isEmpty() && !lower.contains("zink") && !software;
        break;
    case AndroidEglBackend::RenderingMode::ZinkHardware:
        matches = lower.contains("zink") && !software;
        break;
    case AndroidEglBackend::RenderingMode::LlvmpipeSoftware:
        matches = lower.contains("llvmpipe") && !lower.contains("zink");
        break;
    case AndroidEglBackend::RenderingMode::Invalid:
        break;
    }
    if (!matches) {
        qCritical() << "Requested renderer" << renderingModeName(mode)
                    << "does not match actual GL_RENDERER:" << renderer << "; refusing to continue";
    }
    return matches;
}

AndroidEglBackend::RenderingMode AndroidEglBackend::renderingModeFromEnvironment()
{
    // kwin-android-rendering-env (kwin_android_select_rendering) is the single
    // place that probes the device. It exports the Mesa, libepoxy and Vulkan
    // environment and leaves KWIN_ANDROID_GL_MODE resolved to exactly one of
    // system, zink or llvmpipe. KWin consumes that value and never re-probes.
    QByteArray mode = qgetenv("KWIN_ANDROID_GL_MODE").trimmed().toLower();
    if (mode == "system") {
        return RenderingMode::SystemGlesHardware;
    }
    if (mode == "zink") {
        return RenderingMode::ZinkHardware;
    }
    if (mode == "llvmpipe") {
        return RenderingMode::LlvmpipeSoftware;
    }

    qCritical() << "KWIN_ANDROID_GL_MODE" << mode << "is a renderer request, not a resolved renderer."
                << "Run kwin_android_select_rendering from kwin-android-rendering-env before kwin_wayland;"
                << "it accepts explicit system, zink, turnip, kgsl, wrapper or llvmpipe only.";
    return RenderingMode::Invalid;
}

AndroidEglBackend::AndroidEglBackend(AndroidBackend *backend)
    : m_backend(backend)
{
    qCDebug(KWIN_ANDROID_EGL) << "Constructing Android EGL backend";

    // Renderer selection is owned by kwin-android-rendering-env; consume it.
    m_renderingMode = renderingModeFromEnvironment();
    m_eglAvailable = (m_renderingMode != RenderingMode::Invalid);
    if (m_eglAvailable) {
        qInfo() << "Android renderer selected by KWIN_ANDROID_GL_MODE:" << renderingModeName(m_renderingMode);
        m_eglAvailable = validateRenderingEnvironment(m_renderingMode);
    } else {
        qWarning() << "No EGL renderer is available";
    }
}

AndroidEglBackend::~AndroidEglBackend()
{
    qCDebug(KWIN_ANDROID_EGL) << "Destroying Android EGL backend";
    
    // Clean up layers
    for (AndroidEglLayer *layer : m_layers) {
        delete layer;
    }
    m_layers.clear();
}

bool AndroidEglBackend::exportDmaBufFeedback()
{
    // Whether KWin advertises the standard linux-dmabuf feedback to Wayland
    // clients - so external GL/Vulkan clients (e.g. Mesa Turnip apps in a proot
    // container) can create dma-buf swapchains and share zero-copy buffers - is
    // an explicit, launcher-controlled decision via KWIN_ANDROID_LINUX_DMABUF,
    // not an auto-detected one. kwin-android-rendering-env sets it per GL mode
    // (on for zink, off for system/llvmpipe). Keeping it explicit keeps the
    // active buffer-sharing path easy to reason about and debug.
    const QByteArray requested = qgetenv("KWIN_ANDROID_LINUX_DMABUF").toLower();
    const bool enabled = requested == "on" || requested == "1" || requested == "true";
    const bool disabled = requested == "off" || requested == "0" || requested == "false";
    if (!enabled && !disabled) {
        qCCritical(KWIN_ANDROID_EGL) << "KWIN_ANDROID_LINUX_DMABUF must explicitly specify on or off; got:" << requested;
        return false;
    }
    if (disabled) {
        qCInfo(KWIN_ANDROID_EGL) << "linux-dmabuf client advertising explicitly disabled";
        return true;
    }
    if (!WaylandServer::self() || !eglDisplayObject()) {
        qCCritical(KWIN_ANDROID_EGL) << "Cannot enable requested linux-dmabuf without Wayland/EGL display";
        return false;
    }
    EglDisplay *display = eglDisplayObject();

    // The generic EglBackend::initWayland() path is not used here: the Android
    // backend has no DrmDevice (KGSL exposes no DRM render node). Build the
    // feedback directly from the EGL display's import formats. For the main
    // device, prefer the driver's render node if it reports one, otherwise use
    // the dev_t of the GPU device node (/dev/kgsl-3d0); a client rendering on the
    // same KGSL/Turnip GPU sees the same node, so Mesa's Wayland WSI device
    // matching lines up.
    //
    // KWIN_ANDROID_LINUX_DMABUF=on only makes sense with an EGL display that can
    // import dma-buf (the Zink/Turnip path exposes EGL_EXT_image_dma_buf_import);
    // Android system GLES cannot. Fail loudly rather than advertise a path the
    // compositor cannot honour, consistent with the pinned-combination model.
    const auto formats = display->nonExternalOnlySupportedDrmFormats();
    if (formats.isEmpty()) {
        qCCritical(KWIN_ANDROID_EGL) << "KWIN_ANDROID_LINUX_DMABUF=on but the EGL display exposes no"
                                    << "dma-buf import formats; the current renderer cannot import client"
                                    << "dma-buf; refusing to continue with a different configuration.";
        return false;
    }

    dev_t mainDevice = 0;
    if (const std::optional<dev_t> renderNode = display->renderDevNode()) {
        mainDevice = *renderNode;
    } else {
        const QByteArray kgslDevice = qEnvironmentVariableIsSet("KWIN_ANDROID_KGSL_DEVICE")
            ? qgetenv("KWIN_ANDROID_KGSL_DEVICE")
            : QByteArrayLiteral("/dev/kgsl-3d0");
        struct stat deviceStat = {};
        if (stat(kgslDevice.constData(), &deviceStat) == 0) {
            mainDevice = deviceStat.st_rdev;
        } else {
            qCWarning(KWIN_ANDROID_EGL) << "Could not stat" << kgslDevice
                                        << "for linux-dmabuf main device; advertising device 0";
        }
    }

    QList<LinuxDmaBufV1Feedback::Tranche> tranches;
    tranches.append(LinuxDmaBufV1Feedback::Tranche{
        .device = mainDevice,
        .flags = {},
        .formatTable = formats,
    });

    LinuxDmaBufV1ClientBufferIntegration *dmabuf = waylandServer()->linuxDmabuf();
    dmabuf->setRenderBackend(this);
    dmabuf->setSupportedFormatsWithModifiers(tranches);
    // Note: do NOT call waylandServer()->setRenderBackend() here. It sets up the
    // DRM syncobj-timeline protocol and unconditionally dereferences
    // backend->drmDevice(), which is null on the Android/KGSL backend (segfault).
    // That protocol is DRM-only and irrelevant here; clients use implicit sync.

    qCInfo(KWIN_ANDROID_EGL) << "Advertised linux-dmabuf to clients:" << formats.count()
                             << "formats, main_device" << mainDevice;
    return true;
}

void AndroidEglBackend::init()
{
    qCDebug(KWIN_ANDROID_EGL) << "Initializing Android EGL backend";
    
    if (!initializeEgl()) {
        setFailed("Failed to initialize EGL");
        return;
    }

    // Let external Wayland clients import dma-buf buffers (zink/Turnip path only).
    if (!exportDmaBufFeedback()) {
        setFailed("Requested linux-dmabuf configuration is unavailable");
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
    qCDebug(KWIN_ANDROID_EGL) << "Initializing EGL display and context";
    if (!m_eglAvailable) {
        qCritical() << "No EGL renderer is available";
        return false;
    }

    const char *clientExtensions = eglQueryString(EGL_NO_DISPLAY, EGL_EXTENSIONS);
    const QByteArray clientExtensionsString = clientExtensions ? QByteArray(clientExtensions) : QByteArray();

    const bool systemGles = m_renderingMode == RenderingMode::SystemGlesHardware;
    EGLDisplay display = EGL_NO_DISPLAY;
    if (systemGles) {
        // Android exposes its standard EGL_DEFAULT_DISPLAY through eglGetDisplay;
        // the optional KHR platform-Android extension is not required for this API.
        display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    } else {
        using GetPlatformDisplayExtProc = EGLDisplay(EGLAPIENTRYP)(EGLenum platform, void *nativeDisplay, const EGLint *attribList);
        const auto getPlatformDisplay = reinterpret_cast<GetPlatformDisplayExtProc>(eglGetProcAddress("eglGetPlatformDisplayEXT"));
        if (!getPlatformDisplay || !clientExtensionsString.split(' ').contains(QByteArrayLiteral("EGL_MESA_platform_surfaceless"))) {
            qCritical() << "Requested Mesa renderer requires EGL_MESA_platform_surfaceless";
            return false;
        }
        display = getPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, nullptr);
    }
    if (display == EGL_NO_DISPLAY) {
        qCritical() << "Failed to get EGL display";
        return false;
    }

    // Create EGL display wrapper
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
    return verifyRendererMatchesMode(m_renderingMode);
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

// Buffer management is handled by termux-render library
// No need to create/destroy buffers - they are global shared resources

void AndroidEglBackend::addOutput(BackendOutput *output)
{
    qCDebug(KWIN_ANDROID_EGL) << "Adding output to EGL backend:" << output->name();
    
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
