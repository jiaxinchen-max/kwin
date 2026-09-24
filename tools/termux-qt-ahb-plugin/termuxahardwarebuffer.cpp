/* SPDX-License-Identifier: LGPL-3.0-only */
#include "termuxahardwarebuffer.h"

#include <QtWaylandClient/private/qwaylanddisplay_p.h>
#include <QtGui/QOffscreenSurface>
#include <QtGui/QWindow>

#include <algorithm>
#include <android/dlext.h>
#include <cstdio>
#include <dlfcn.h>
#include <sys/socket.h>
#include <sys/system_properties.h>
#include <unistd.h>

#ifndef AHARDWAREBUFFER_FORMAT_B8G8R8A8_UNORM
#define AHARDWAREBUFFER_FORMAT_B8G8R8A8_UNORM 5
#endif

QT_BEGIN_NAMESPACE

namespace QtWaylandClient {

static const wl_interface *s_createBufferTypes[] = {
    &wl_buffer_interface,
    nullptr,
};

static const wl_message s_managerRequests[] = {
    { "destroy", "", nullptr },
    { "create_buffer", "nh", s_createBufferTypes },
};

static const wl_interface s_managerInterface = {
    "termux_ahardware_buffer_manager_v1",
    1,
    2,
    s_managerRequests,
    0,
    nullptr,
};

static wl_buffer *createWaylandBuffer(wl_proxy *manager, int socket)
{
    return reinterpret_cast<wl_buffer *>(wl_proxy_marshal_flags(
        manager, 1, &wl_buffer_interface, wl_proxy_get_version(manager), 0, nullptr, socket));
}

TermuxAHardwareBufferIntegration::TermuxAHardwareBufferIntegration() = default;

TermuxAHardwareBufferIntegration::~TermuxAHardwareBufferIntegration()
{
    if (m_display)
        m_display->removeListener(&TermuxAHardwareBufferIntegration::registryGlobal, this);
    if (m_bufferManager)
        wl_proxy_destroy(m_bufferManager);
    if (m_eglDisplay != EGL_NO_DISPLAY && m_terminate)
        m_terminate(m_eglDisplay);
    if (m_androidLibrary)
        dlclose(m_androidLibrary);
    if (m_eglLibrary)
        dlclose(m_eglLibrary);
    if (m_epoxyLibrary)
        dlclose(m_epoxyLibrary);
    if (m_glesLibrary)
        dlclose(m_glesLibrary);
    if (m_dlAndroidLibrary)
        dlclose(m_dlAndroidLibrary);
}

void TermuxAHardwareBufferIntegration::initialize(QWaylandDisplay *display)
{
    m_display = display;
    display->addRegistryListener(&TermuxAHardwareBufferIntegration::registryGlobal, this);

    m_androidLibrary = dlopen("libandroid.so", RTLD_NOW | RTLD_LOCAL);
    // QtGui has already loaded libEGL.so.1 at this point. The launcher puts
    // the private Android-EGL forwarding library first in LD_LIBRARY_PATH, so
    // use that same dispatch table instead of opening a second EGL instance.
    m_eglLibrary = dlopen("libEGL.so.1", RTLD_NOW | RTLD_GLOBAL);
    if (m_eglLibrary) {
        m_getDisplay = reinterpret_cast<PFNEGLGETDISPLAYPROC>(dlsym(m_eglLibrary, "eglGetDisplay"));
        m_initialize = reinterpret_cast<PFNEGLINITIALIZEPROC>(dlsym(m_eglLibrary, "eglInitialize"));
        m_terminate = reinterpret_cast<PFNEGLTERMINATEPROC>(dlsym(m_eglLibrary, "eglTerminate"));
        m_getProcAddress = reinterpret_cast<PFNEGLGETPROCADDRESSPROC>(dlsym(m_eglLibrary, "eglGetProcAddress"));
        m_chooseConfig = reinterpret_cast<PFNEGLCHOOSECONFIGPROC>(dlsym(m_eglLibrary, "eglChooseConfig"));
        m_bindApi = reinterpret_cast<PFNEGLBINDAPIPROC>(dlsym(m_eglLibrary, "eglBindAPI"));
        m_createContext = reinterpret_cast<PFNEGLCREATECONTEXTPROC>(dlsym(m_eglLibrary, "eglCreateContext"));
        m_destroyContext = reinterpret_cast<PFNEGLDESTROYCONTEXTPROC>(dlsym(m_eglLibrary, "eglDestroyContext"));
        m_createPbufferSurface = reinterpret_cast<PFNEGLCREATEPBUFFERSURFACEPROC>(dlsym(m_eglLibrary, "eglCreatePbufferSurface"));
        m_destroySurface = reinterpret_cast<PFNEGLDESTROYSURFACEPROC>(dlsym(m_eglLibrary, "eglDestroySurface"));
        m_getError = reinterpret_cast<PFNEGLGETERRORPROC>(dlsym(m_eglLibrary, "eglGetError"));
        m_makeCurrent = reinterpret_cast<PFNEGLMAKECURRENTPROC>(dlsym(m_eglLibrary, "eglMakeCurrent"));
        m_getCurrentContext = reinterpret_cast<PFNEGLGETCURRENTCONTEXTPROC>(dlsym(m_eglLibrary, "eglGetCurrentContext"));
        if (m_getProcAddress) {
            m_createImage = reinterpret_cast<PFNEGLCREATEIMAGEKHRPROC>(m_getProcAddress("eglCreateImageKHR"));
            m_destroyImage = reinterpret_cast<PFNEGLDESTROYIMAGEKHRPROC>(m_getProcAddress("eglDestroyImageKHR"));
            m_getNativeClientBuffer = reinterpret_cast<PFNEGLGETNATIVECLIENTBUFFERANDROIDPROC>(
                m_getProcAddress("eglGetNativeClientBufferANDROID"));
        }
        Dl_info eglInfo = {};
        if (dladdr(reinterpret_cast<void *>(m_getDisplay), &eglInfo) && eglInfo.dli_fname)
            qInfo("Termux AHardwareBuffer: EGL dispatch from %s", eglInfo.dli_fname);
    } else {
        qWarning("Termux AHardwareBuffer: cannot load EGL dispatch: %s", dlerror());
    }
    if (m_androidLibrary) {
        m_allocate = reinterpret_cast<decltype(m_allocate)>(dlsym(m_androidLibrary, "AHardwareBuffer_allocate"));
        m_release = reinterpret_cast<decltype(m_release)>(dlsym(m_androidLibrary, "AHardwareBuffer_release"));
        m_sendHandle = reinterpret_cast<decltype(m_sendHandle)>(
            dlsym(m_androidLibrary, "AHardwareBuffer_sendHandleToUnixSocket"));
    }

    m_eglDisplay = m_getDisplay ? m_getDisplay(EGL_DEFAULT_DISPLAY) : EGL_NO_DISPLAY;
    EGLint major = 0;
    EGLint minor = 0;
    if (m_eglDisplay == EGL_NO_DISPLAY || !m_initialize
        || !m_initialize(m_eglDisplay, &major, &minor))
        m_eglDisplay = EGL_NO_DISPLAY;

    if (!isValid()) {
        qWarning("Termux AHardwareBuffer: initialization failed manager=%p display=%p "
                 "android=%d/%d/%d egl=%d/%d/%d/%d/%d/%d/%d image=%d/%d/%d error=0x%x",
                 static_cast<void *>(m_bufferManager), m_eglDisplay,
                 bool(m_allocate), bool(m_release), bool(m_sendHandle),
                 bool(m_getProcAddress), bool(m_chooseConfig), bool(m_bindApi),
                 bool(m_createContext), bool(m_createPbufferSurface), bool(m_makeCurrent),
                 bool(m_getCurrentContext), bool(m_createImage), bool(m_destroyImage),
                 bool(m_getNativeClientBuffer), unsigned(error()));
    } else {
        qInfo("Termux AHardwareBuffer: using Android EGL/GLES client buffers");
    }
}

void TermuxAHardwareBufferIntegration::registryGlobal(void *data, wl_registry *registry,
                                                       uint32_t id, const QString &interface,
                                                       uint32_t version)
{
    auto *integration = static_cast<TermuxAHardwareBufferIntegration *>(data);
    if (!integration->m_bufferManager
        && interface == QLatin1String(s_managerInterface.name)) {
        integration->m_bufferManager = reinterpret_cast<wl_proxy *>(wl_registry_bind(
            registry, id, &s_managerInterface, std::min(version, 1u)));
    }
}

bool TermuxAHardwareBufferIntegration::isValid() const
{
    return m_bufferManager && m_eglDisplay != EGL_NO_DISPLAY && m_allocate && m_release
        && m_sendHandle && m_getProcAddress && m_chooseConfig && m_bindApi && m_createContext
        && m_destroyContext && m_createPbufferSurface && m_destroySurface && m_makeCurrent
        && m_getCurrentContext && m_createImage && m_destroyImage && m_getNativeClientBuffer;
}

bool TermuxAHardwareBufferIntegration::supportsThreadedOpenGL() const
{
    return true;
}

QWaylandWindow *TermuxAHardwareBufferIntegration::createEglWindow(QWindow *window)
{
    return new TermuxAHardwareBufferWindow(window, m_display, this);
}

QPlatformOpenGLContext *TermuxAHardwareBufferIntegration::createPlatformOpenGLContext(
    const QSurfaceFormat &format, QPlatformOpenGLContext *share) const
{
    QSurfaceFormat requested = format;
    requested.setRenderableType(QSurfaceFormat::OpenGLES);
    return new TermuxAHardwareBufferContext(
        const_cast<TermuxAHardwareBufferIntegration *>(this), requested, share);
}

QOpenGLContext *TermuxAHardwareBufferIntegration::createOpenGLContext(
    EGLContext context, EGLDisplay contextDisplay, QOpenGLContext *shareContext) const
{
    Q_UNUSED(context);
    Q_UNUSED(contextDisplay);
    Q_UNUSED(shareContext);
    return nullptr;
}

bool TermuxAHardwareBufferIntegration::canCreatePlatformOffscreenSurface() const
{
    return true;
}

QPlatformOffscreenSurface *TermuxAHardwareBufferIntegration::createPlatformOffscreenSurface(
    QOffscreenSurface *surface) const
{
    return new QPlatformOffscreenSurface(surface);
}

void *TermuxAHardwareBufferIntegration::nativeResource(NativeResource resource)
{
    return resource == EglDisplay ? m_eglDisplay : nullptr;
}

void *TermuxAHardwareBufferIntegration::nativeResourceForContext(
    NativeResource resource, QPlatformOpenGLContext *context)
{
    auto *eglContext = static_cast<TermuxAHardwareBufferContext *>(context);
    switch (resource) {
    case EglConfig:
        return eglContext->eglConfig();
    case EglContext:
        return eglContext->eglContext();
    case EglDisplay:
        return m_eglDisplay;
    }
    return nullptr;
}

EGLDisplay TermuxAHardwareBufferIntegration::eglDisplay() const
{
    return m_eglDisplay;
}

wl_proxy *TermuxAHardwareBufferIntegration::bufferManager() const
{
    return m_bufferManager;
}

QFunctionPointer TermuxAHardwareBufferIntegration::procAddress(const char *name) const
{
    if (!m_glesLibrary && !m_vendorLoadAttempted) {
        m_vendorLoadAttempted = true;
#if defined(__LP64__)
        m_dlAndroidLibrary = dlopen("/system/lib64/libdl_android.so", RTLD_NOW | RTLD_LOCAL);
#else
        m_dlAndroidLibrary = dlopen("/system/lib/libdl_android.so", RTLD_NOW | RTLD_LOCAL);
#endif
        using GetExportedNamespace = android_namespace_t *(*)(const char *);
        using AndroidDlopenExt = void *(*)(const char *, int, const android_dlextinfo *);
        auto getExportedNamespace = reinterpret_cast<GetExportedNamespace>(
            dlsym(RTLD_DEFAULT, "android_get_exported_namespace"));
        if (!getExportedNamespace) {
            getExportedNamespace = reinterpret_cast<GetExportedNamespace>(
                dlsym(RTLD_DEFAULT, "__loader_android_get_exported_namespace"));
        }
        if (!getExportedNamespace && m_dlAndroidLibrary) {
            getExportedNamespace = reinterpret_cast<GetExportedNamespace>(
                dlsym(m_dlAndroidLibrary, "android_get_exported_namespace"));
        }
        auto androidDlopenExt = reinterpret_cast<AndroidDlopenExt>(
            dlsym(RTLD_DEFAULT, "android_dlopen_ext"));
        if (!androidDlopenExt) {
            androidDlopenExt = reinterpret_cast<AndroidDlopenExt>(
                dlsym(RTLD_DEFAULT, "__loader_android_dlopen_ext"));
        }
        if (!androidDlopenExt && m_dlAndroidLibrary) {
            androidDlopenExt = reinterpret_cast<AndroidDlopenExt>(
                dlsym(m_dlAndroidLibrary, "android_dlopen_ext"));
        }
        if (getExportedNamespace && androidDlopenExt) {
            android_namespace_t *sphal = getExportedNamespace("sphal");
            if (sphal) {
                android_dlextinfo info = {};
                info.flags = ANDROID_DLEXT_USE_NAMESPACE;
                info.library_namespace = sphal;
                char driver[PROP_VALUE_MAX] = {};
                __system_property_get("ro.hardware.egl", driver);
#if defined(__LP64__)
                const char *patterns[] = {
                    "/vendor/lib64/egl/libGLESv2_%s.so",
                    "/vendor/lib64/egl/libGLES_%s.so",
                    "/system/lib64/egl/libGLESv2_%s.so",
                    "/system/lib64/egl/libGLES_%s.so",
                };
#else
                const char *patterns[] = {
                    "/vendor/lib/egl/libGLESv2_%s.so",
                    "/vendor/lib/egl/libGLES_%s.so",
                    "/system/lib/egl/libGLESv2_%s.so",
                    "/system/lib/egl/libGLES_%s.so",
                };
#endif
                if (driver[0]) {
                    for (const char *pattern : patterns) {
                        char path[256];
                        std::snprintf(path, sizeof(path), pattern, driver);
                        m_glesLibrary = androidDlopenExt(path, RTLD_NOW | RTLD_LOCAL, &info);
                        if (m_glesLibrary)
                            break;
                    }
                }
            }
        }
        if (m_glesLibrary)
            qInfo("Termux AHardwareBuffer: using GLES from Android sphal namespace");
        else
            qWarning("Termux AHardwareBuffer: vendor GLES namespace load failed "
                     "libdl_android=%p namespace_api=%d dlopen_ext=%d: %s",
                     m_dlAndroidLibrary, bool(getExportedNamespace), bool(androidDlopenExt), dlerror());
    }
    if (m_glesLibrary) {
        if (void *symbol = dlsym(m_glesLibrary, name))
            return reinterpret_cast<QFunctionPointer>(symbol);
    }

    // Termux's patched libepoxy selects the Android system EGL/GLES libraries
    // by absolute path. Its generated thunks are also insulated from the
    // GLVND symbols pulled into the process by QtGui.
    if (!m_epoxyLibrary)
        m_epoxyLibrary = dlopen("libepoxy.so", RTLD_NOW | RTLD_LOCAL);
    if (m_epoxyLibrary) {
        if (void *symbol = dlsym(m_epoxyLibrary, name))
            return reinterpret_cast<QFunctionPointer>(symbol);
    }

    // Android EGL owns the per-thread GLES dispatch table. Asking it first is
    // required on gfxstream; dlsym(libGLESv2.so) can return public no-op
    // trampolines when another EGL ABI was present during process startup.
    if (m_getProcAddress) {
        if (auto symbol = m_getProcAddress(name))
            return reinterpret_cast<QFunctionPointer>(symbol);
    }

    return nullptr;
}

EGLBoolean TermuxAHardwareBufferIntegration::makeCurrent(
    EGLSurface draw, EGLSurface read, EGLContext context) const
{
    return m_makeCurrent
        ? m_makeCurrent(m_eglDisplay, draw, read, context)
        : EGL_FALSE;
}

EGLContext TermuxAHardwareBufferIntegration::currentContext() const
{
    return m_getCurrentContext ? m_getCurrentContext() : EGL_NO_CONTEXT;
}

EGLBoolean TermuxAHardwareBufferIntegration::chooseConfig(
    const EGLint *attributes, EGLConfig *config, EGLint size, EGLint *count) const
{
    return m_chooseConfig(m_eglDisplay, attributes, config, size, count);
}

EGLBoolean TermuxAHardwareBufferIntegration::bindApi(EGLenum api) const
{
    return m_bindApi(api);
}

EGLContext TermuxAHardwareBufferIntegration::createContext(
    EGLConfig config, EGLContext share, const EGLint *attributes) const
{
    return m_createContext(m_eglDisplay, config, share, attributes);
}

void TermuxAHardwareBufferIntegration::destroyContext(EGLContext context) const
{
    m_destroyContext(m_eglDisplay, context);
}

EGLSurface TermuxAHardwareBufferIntegration::createPbufferSurface(
    EGLConfig config, const EGLint *attributes) const
{
    return m_createPbufferSurface(m_eglDisplay, config, attributes);
}

void TermuxAHardwareBufferIntegration::destroySurface(EGLSurface surface) const
{
    m_destroySurface(m_eglDisplay, surface);
}

EGLint TermuxAHardwareBufferIntegration::error() const
{
    return m_getError ? m_getError() : EGL_BAD_ACCESS;
}

EGLImageKHR TermuxAHardwareBufferIntegration::createImage(
    EGLenum target, EGLClientBuffer buffer, const EGLint *attributes) const
{
    return m_createImage(m_eglDisplay, EGL_NO_CONTEXT, target, buffer, attributes);
}

void TermuxAHardwareBufferIntegration::destroyImage(EGLImageKHR image) const
{
    m_destroyImage(m_eglDisplay, image);
}

EGLClientBuffer TermuxAHardwareBufferIntegration::nativeClientBuffer(AHardwareBuffer *buffer) const
{
    return m_getNativeClientBuffer(buffer);
}

bool TermuxAHardwareBufferIntegration::waitForBufferRelease() const
{
    return m_display && wl_display_roundtrip(m_display->wl_display()) >= 0;
}

int TermuxAHardwareBufferIntegration::allocate(const AHardwareBuffer_Desc *description,
                                                AHardwareBuffer **buffer) const
{
    return m_allocate(description, buffer);
}

void TermuxAHardwareBufferIntegration::release(AHardwareBuffer *buffer) const
{
    m_release(buffer);
}

int TermuxAHardwareBufferIntegration::sendHandle(const AHardwareBuffer *buffer, int socket) const
{
    return m_sendHandle(buffer, socket);
}

TermuxAHardwareBufferWindow::TermuxAHardwareBufferWindow(
    QWindow *window, QWaylandDisplay *display, TermuxAHardwareBufferIntegration *integration)
    : QWaylandWindow(window, display)
    , m_integration(integration)
{
    for (Slot &slot : m_slots)
        slot.window = this;
    ensureSize();
}

TermuxAHardwareBufferWindow::~TermuxAHardwareBufferWindow()
{
    destroySlots(m_integration->eglDisplay());
}

QWaylandWindow::WindowType TermuxAHardwareBufferWindow::windowType() const
{
    return QWaylandWindow::Egl;
}

void TermuxAHardwareBufferWindow::ensureSize()
{
    QMutexLocker locker(&m_mutex);
    const QMargins margins = clientSideMargins();
    m_bufferSize = (geometry().size()
                    + QSize(margins.left() + margins.right(), margins.top() + margins.bottom()))
        * scale();
    if (!m_bufferSize.isEmpty() && window()->isVisible()) {
        const QSize logicalSize = geometry().size();
        QMetaObject::invokeMethod(this, [this, logicalSize] {
            sendExposeEvent(QRect(QPoint(), logicalSize));
        }, Qt::QueuedConnection);
    }
}

void TermuxAHardwareBufferWindow::invalidateSurface()
{
    QMutexLocker locker(&m_mutex);
    m_bufferSize = QSize();
    m_current = nullptr;
}

bool TermuxAHardwareBufferWindow::createSlot(Slot &slot, const QSize &size, EGLDisplay display)
{
    AHardwareBuffer_Desc description = {};
    description.width = size.width();
    description.height = size.height();
    description.layers = 1;
    description.format = AHARDWAREBUFFER_FORMAT_B8G8R8A8_UNORM;
    description.usage = AHARDWAREBUFFER_USAGE_GPU_FRAMEBUFFER
        | AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE;
    if (m_integration->allocate(&description, &slot.hardwareBuffer) != 0) {
        qWarning("Termux AHardwareBuffer: allocation failed for %dx%d", size.width(), size.height());
        return false;
    }
    auto imageTargetTexture = reinterpret_cast<PFNGLEGLIMAGETARGETTEXTURE2DOESPROC>(
        m_integration->procAddress("glEGLImageTargetTexture2DOES"));
    auto genTextures = reinterpret_cast<PFNGLGENTEXTURESPROC>(m_integration->procAddress("glGenTextures"));
    auto bindTexture = reinterpret_cast<PFNGLBINDTEXTUREPROC>(m_integration->procAddress("glBindTexture"));
    auto texParameteri = reinterpret_cast<PFNGLTEXPARAMETERIPROC>(m_integration->procAddress("glTexParameteri"));
    auto genFramebuffers = reinterpret_cast<PFNGLGENFRAMEBUFFERSPROC>(m_integration->procAddress("glGenFramebuffers"));
    auto bindFramebuffer = reinterpret_cast<PFNGLBINDFRAMEBUFFERPROC>(m_integration->procAddress("glBindFramebuffer"));
    auto framebufferTexture2D = reinterpret_cast<PFNGLFRAMEBUFFERTEXTURE2DPROC>(m_integration->procAddress("glFramebufferTexture2D"));
    auto checkFramebufferStatus = reinterpret_cast<PFNGLCHECKFRAMEBUFFERSTATUSPROC>(m_integration->procAddress("glCheckFramebufferStatus"));
    auto getError = reinterpret_cast<PFNGLGETERRORPROC>(m_integration->procAddress("glGetError"));
    auto getString = reinterpret_cast<PFNGLGETSTRINGPROC>(m_integration->procAddress("glGetString"));
    if (!imageTargetTexture || !genTextures
        || !bindTexture || !texParameteri || !genFramebuffers || !bindFramebuffer
        || !framebufferTexture2D || !checkFramebufferStatus || !getError || !getString) {
        qWarning("Termux AHardwareBuffer: required EGL/GL entry point missing");
        return false;
    }
    static bool rendererLogged = false;
    if (!rendererLogged) {
        qInfo("Termux AHardwareBuffer: %s", getString(GL_VERSION));
        rendererLogged = true;
    }

    const EGLint imageAttributes[] = { EGL_IMAGE_PRESERVED_KHR, EGL_TRUE, EGL_NONE };
    slot.image = m_integration->createImage(EGL_NATIVE_BUFFER_ANDROID,
                                            m_integration->nativeClientBuffer(slot.hardwareBuffer),
                                            imageAttributes);
    if (slot.image == EGL_NO_IMAGE_KHR) {
        qWarning("Termux AHardwareBuffer: eglCreateImageKHR failed: 0x%x", m_integration->error());
        return false;
    }

    genTextures(1, &slot.texture);
    bindTexture(GL_TEXTURE_2D, slot.texture);
    texParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    texParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    texParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    texParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    imageTargetTexture(GL_TEXTURE_2D, slot.image);

    genFramebuffers(1, &slot.framebuffer);
    bindFramebuffer(GL_FRAMEBUFFER, slot.framebuffer);
    framebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, slot.texture, 0);
    const GLenum framebufferStatus = checkFramebufferStatus(GL_FRAMEBUFFER);
    if (framebufferStatus != GL_FRAMEBUFFER_COMPLETE) {
        qWarning("Termux AHardwareBuffer: framebuffer incomplete: 0x%x, GL error: 0x%x, texture=%u, fbo=%u",
                 framebufferStatus, getError(), slot.texture, slot.framebuffer);
        return false;
    }

    int sockets[2] = { -1, -1 };
    if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, sockets) != 0) {
        qWarning("Termux AHardwareBuffer: socketpair failed");
        return false;
    }
    const int sendResult = m_integration->sendHandle(slot.hardwareBuffer, sockets[0]);
    if (sendResult == 0)
        slot.waylandBuffer = createWaylandBuffer(m_integration->bufferManager(), sockets[1]);
    ::close(sockets[0]);
    ::close(sockets[1]);
    if (!slot.waylandBuffer) {
        qWarning("Termux AHardwareBuffer: buffer export failed: %d", sendResult);
        return false;
    }

    static const wl_buffer_listener listener = { &TermuxAHardwareBufferWindow::bufferReleased };
    wl_buffer_add_listener(slot.waylandBuffer, &listener, &slot);
    return true;
}

void TermuxAHardwareBufferWindow::destroySlot(Slot &slot, EGLDisplay display)
{
    if (slot.waylandBuffer)
        wl_buffer_destroy(slot.waylandBuffer);
    auto deleteFramebuffers = reinterpret_cast<PFNGLDELETEFRAMEBUFFERSPROC>(m_integration->procAddress("glDeleteFramebuffers"));
    auto deleteRenderbuffers = reinterpret_cast<PFNGLDELETERENDERBUFFERSPROC>(m_integration->procAddress("glDeleteRenderbuffers"));
    auto deleteTextures = reinterpret_cast<PFNGLDELETETEXTURESPROC>(m_integration->procAddress("glDeleteTextures"));
    if (slot.framebuffer && deleteFramebuffers)
        deleteFramebuffers(1, &slot.framebuffer);
    if (slot.depthStencil && deleteRenderbuffers)
        deleteRenderbuffers(1, &slot.depthStencil);
    if (slot.texture && deleteTextures)
        deleteTextures(1, &slot.texture);
    if (slot.image != EGL_NO_IMAGE_KHR) {
        m_integration->destroyImage(slot.image);
    }
    if (slot.hardwareBuffer)
        m_integration->release(slot.hardwareBuffer);
    slot = Slot{ this };
}

void TermuxAHardwareBufferWindow::destroySlots(EGLDisplay display)
{
    for (Slot &slot : m_slots)
        destroySlot(slot, display);
    m_allocatedSize = QSize();
    m_current = nullptr;
}

bool TermuxAHardwareBufferWindow::acquireBuffer(EGLDisplay display)
{
    QMutexLocker locker(&m_mutex);
    if (m_bufferSize.isEmpty()) {
        qWarning("Termux AHardwareBuffer: window buffer size is empty");
        return false;
    }

    if (m_allocatedSize != m_bufferSize) {
        auto finish = reinterpret_cast<PFNGLFINISHPROC>(m_integration->procAddress("glFinish"));
        if (finish)
            finish();
        destroySlots(display);
        m_allocatedSize = m_bufferSize;
    }

    auto findAvailableSlot = [this]() {
        return std::find_if(m_slots.begin(), m_slots.end(), [](const Slot &slot) {
            return !slot.busy;
        });
    };
    auto it = findAvailableSlot();
    if (it == m_slots.end()) {
        locker.unlock();
        const bool dispatched = m_integration->waitForBufferRelease();
        locker.relock();
        if (!dispatched)
            return false;
        it = findAvailableSlot();
        if (it == m_slots.end()) {
            qWarning("Termux AHardwareBuffer: no buffer released after display roundtrip");
            return false;
        }
    }
    if (!it->hardwareBuffer && !createSlot(*it, m_allocatedSize, display)) {
        destroySlot(*it, display);
        return false;
    }
    m_current = &*it;
    auto bindFramebuffer = reinterpret_cast<PFNGLBINDFRAMEBUFFERPROC>(m_integration->procAddress("glBindFramebuffer"));
    if (!bindFramebuffer)
        return false;
    bindFramebuffer(GL_FRAMEBUFFER, m_current->framebuffer);
    return true;
}

void TermuxAHardwareBufferWindow::present()
{
    QMutexLocker locker(&m_mutex);
    if (!m_current || !wlSurface())
        return;

    auto finish = reinterpret_cast<PFNGLFINISHPROC>(m_integration->procAddress("glFinish"));
    if (!finish)
        return;
    finish();
    handleUpdate();
    m_current->busy = true;
    // Qt Quick renders a window render target in top-left-oriented coordinates,
    // while the imported OpenGL texture has a bottom-left origin.
    wl_surface_set_buffer_transform(wlSurface(), WL_OUTPUT_TRANSFORM_FLIPPED_180);
    wl_surface_attach(wlSurface(), m_current->waylandBuffer, 0, 0);
    wl_surface_damage_buffer(wlSurface(), 0, 0, m_allocatedSize.width(), m_allocatedSize.height());
    wl_surface_commit(wlSurface());
    m_current = nullptr;
}

GLuint TermuxAHardwareBufferWindow::framebuffer() const
{
    QMutexLocker locker(&m_mutex);
    return m_current ? m_current->framebuffer : 0;
}

void TermuxAHardwareBufferWindow::bufferReleased(void *data, wl_buffer *)
{
    auto *slot = static_cast<Slot *>(data);
    QMutexLocker locker(&slot->window->m_mutex);
    slot->busy = false;
}

TermuxAHardwareBufferContext::TermuxAHardwareBufferContext(
    TermuxAHardwareBufferIntegration *integration, const QSurfaceFormat &format,
    QPlatformOpenGLContext *share)
    : m_integration(integration)
    , m_format(format)
{
    // AHardwareBuffer image-backed FBOs are reliable on the Android emulator's
    // GLES 3 implementation; expose GLES 3 even when Qt's default request is 2.0.
    const int requestedMajor = 3;
    EGLint renderableType = requestedMajor >= 3 ? EGL_OPENGL_ES3_BIT_KHR : EGL_OPENGL_ES2_BIT;
    const EGLint configAttributes[] = {
        EGL_RENDERABLE_TYPE, renderableType,
        EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_NONE,
    };
    EGLint count = 0;
    if (!integration->chooseConfig(configAttributes, &m_config, 1, &count)
        || count == 0) {
        return;
    }

    integration->bindApi(EGL_OPENGL_ES_API);
    EGLContext shareContext = EGL_NO_CONTEXT;
    if (auto *termuxShare = dynamic_cast<TermuxAHardwareBufferContext *>(share)) {
        shareContext = termuxShare->eglContext();
        m_sharing = shareContext != EGL_NO_CONTEXT;
    }
    const EGLint contextAttributes[] = { EGL_CONTEXT_CLIENT_VERSION, requestedMajor, EGL_NONE };
    m_context = integration->createContext(m_config, shareContext, contextAttributes);
    if (m_context != EGL_NO_CONTEXT) {
        const EGLint pbufferAttributes[] = {
            EGL_WIDTH, 1,
            EGL_HEIGHT, 1,
            EGL_NONE,
        };
        m_pbuffer = integration->createPbufferSurface(m_config, pbufferAttributes);
        if (m_pbuffer == EGL_NO_SURFACE
            || !integration->makeCurrent(m_pbuffer, m_pbuffer, m_context)) {
            qWarning("Termux AHardwareBuffer: initial eglMakeCurrent failed: 0x%x", integration->error());
            if (m_pbuffer != EGL_NO_SURFACE) {
                integration->destroySurface(m_pbuffer);
                m_pbuffer = EGL_NO_SURFACE;
            }
            integration->destroyContext(m_context);
            m_context = EGL_NO_CONTEXT;
            return;
        }
        m_format.setRenderableType(QSurfaceFormat::OpenGLES);
        m_format.setVersion(requestedMajor, 0);
    }
}

TermuxAHardwareBufferContext::~TermuxAHardwareBufferContext()
{
    if (m_pbuffer != EGL_NO_SURFACE)
        m_integration->destroySurface(m_pbuffer);
    if (m_context != EGL_NO_CONTEXT)
        m_integration->destroyContext(m_context);
}

QSurfaceFormat TermuxAHardwareBufferContext::format() const
{
    return m_format;
}

bool TermuxAHardwareBufferContext::makeCurrent(QPlatformSurface *surface)
{
    if (surface->surface()->surfaceClass() == QSurface::Offscreen) {
        m_currentWindow = nullptr;
        return m_integration->makeCurrent(m_pbuffer, m_pbuffer, m_context) == EGL_TRUE;
    }
    if (!m_integration->makeCurrent(m_pbuffer, m_pbuffer, m_context))
        return false;

    m_currentWindow = static_cast<TermuxAHardwareBufferWindow *>(surface);
    if (!m_currentWindow->acquireBuffer(m_integration->eglDisplay())) {
        qWarning("Termux AHardwareBuffer: failed to acquire window buffer");
        return false;
    }
    return true;
}

void TermuxAHardwareBufferContext::doneCurrent()
{
    m_currentWindow = nullptr;
    m_integration->makeCurrent(EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
}

void TermuxAHardwareBufferContext::swapBuffers(QPlatformSurface *surface)
{
    if (surface->surface()->surfaceClass() == QSurface::Offscreen) {
        auto flush = reinterpret_cast<PFNGLFLUSHPROC>(m_integration->procAddress("glFlush"));
        if (flush)
            flush();
        return;
    }
    static_cast<TermuxAHardwareBufferWindow *>(surface)->present();
}

GLuint TermuxAHardwareBufferContext::defaultFramebufferObject(QPlatformSurface *surface) const
{
    return surface->surface()->surfaceClass() == QSurface::Window
        ? static_cast<TermuxAHardwareBufferWindow *>(surface)->framebuffer()
        : 0;
}

void TermuxAHardwareBufferContext::beginFrame()
{
    if (m_currentWindow)
        m_currentWindow->beginFrame();
}

void TermuxAHardwareBufferContext::endFrame()
{
    if (m_currentWindow)
        m_currentWindow->endFrame();
}

bool TermuxAHardwareBufferContext::isSharing() const
{
    return m_sharing;
}

bool TermuxAHardwareBufferContext::isValid() const
{
    return m_context != EGL_NO_CONTEXT;
}

QFunctionPointer TermuxAHardwareBufferContext::getProcAddress(const char *name)
{
    return m_integration->procAddress(name);
}

EGLConfig TermuxAHardwareBufferContext::eglConfig() const
{
    return m_config;
}

EGLContext TermuxAHardwareBufferContext::eglContext() const
{
    return m_context;
}

}

QT_END_NAMESPACE
