/* SPDX-License-Identifier: LGPL-3.0-only */
#pragma once

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include <GLES2/gl2ext.h>

#include <QtWaylandClient/private/qwaylandclientbufferintegration_p.h>
#include <QtWaylandClient/private/qwaylandwindow_p.h>
#include <qpa/qplatformopenglcontext.h>
#include <qpa/qplatformoffscreensurface.h>

#include <android/hardware_buffer.h>
#include <wayland-client-core.h>

#include <array>

QT_BEGIN_NAMESPACE

namespace QtWaylandClient {

class TermuxAHardwareBufferWindow;

class TermuxAHardwareBufferIntegration final : public QWaylandClientBufferIntegration
{
public:
    TermuxAHardwareBufferIntegration();
    ~TermuxAHardwareBufferIntegration() override;

    void initialize(QWaylandDisplay *display) override;
    bool isValid() const override;
    bool supportsThreadedOpenGL() const override;
    QWaylandWindow *createEglWindow(QWindow *window) override;
    QPlatformOpenGLContext *createPlatformOpenGLContext(const QSurfaceFormat &format,
                                                         QPlatformOpenGLContext *share) const override;
    QOpenGLContext *createOpenGLContext(EGLContext context, EGLDisplay contextDisplay,
                                         QOpenGLContext *shareContext) const override;
    bool canCreatePlatformOffscreenSurface() const override;
    QPlatformOffscreenSurface *createPlatformOffscreenSurface(QOffscreenSurface *surface) const override;
    void *nativeResource(NativeResource resource) override;
    void *nativeResourceForContext(NativeResource resource, QPlatformOpenGLContext *context) override;

    EGLDisplay eglDisplay() const;
    wl_proxy *bufferManager() const;
    QFunctionPointer procAddress(const char *name) const;
    EGLBoolean makeCurrent(EGLSurface draw, EGLSurface read, EGLContext context) const;
    EGLContext currentContext() const;
    EGLBoolean chooseConfig(const EGLint *attributes, EGLConfig *config, EGLint size,
                            EGLint *count) const;
    EGLBoolean bindApi(EGLenum api) const;
    EGLContext createContext(EGLConfig config, EGLContext share, const EGLint *attributes) const;
    void destroyContext(EGLContext context) const;
    EGLSurface createPbufferSurface(EGLConfig config, const EGLint *attributes) const;
    void destroySurface(EGLSurface surface) const;
    EGLint error() const;
    EGLImageKHR createImage(EGLenum target, EGLClientBuffer buffer, const EGLint *attributes) const;
    void destroyImage(EGLImageKHR image) const;
    EGLClientBuffer nativeClientBuffer(AHardwareBuffer *buffer) const;
    bool waitForBufferRelease() const;

    int allocate(const AHardwareBuffer_Desc *description, AHardwareBuffer **buffer) const;
    void release(AHardwareBuffer *buffer) const;
    int sendHandle(const AHardwareBuffer *buffer, int socket) const;

private:
    static void registryGlobal(void *data, wl_registry *registry, uint32_t id,
                               const QString &interface, uint32_t version);

    QWaylandDisplay *m_display = nullptr;
    EGLDisplay m_eglDisplay = EGL_NO_DISPLAY;
    wl_proxy *m_bufferManager = nullptr;
    void *m_androidLibrary = nullptr;
    void *m_eglLibrary = nullptr;
    mutable void *m_epoxyLibrary = nullptr;
    mutable void *m_dlAndroidLibrary = nullptr;
    mutable void *m_glesLibrary = nullptr;
    mutable bool m_vendorLoadAttempted = false;
    PFNEGLGETDISPLAYPROC m_getDisplay = nullptr;
    PFNEGLINITIALIZEPROC m_initialize = nullptr;
    PFNEGLTERMINATEPROC m_terminate = nullptr;
    PFNEGLGETPROCADDRESSPROC m_getProcAddress = nullptr;
    PFNEGLCHOOSECONFIGPROC m_chooseConfig = nullptr;
    PFNEGLBINDAPIPROC m_bindApi = nullptr;
    PFNEGLCREATECONTEXTPROC m_createContext = nullptr;
    PFNEGLDESTROYCONTEXTPROC m_destroyContext = nullptr;
    PFNEGLCREATEPBUFFERSURFACEPROC m_createPbufferSurface = nullptr;
    PFNEGLDESTROYSURFACEPROC m_destroySurface = nullptr;
    PFNEGLGETERRORPROC m_getError = nullptr;
    PFNEGLMAKECURRENTPROC m_makeCurrent = nullptr;
    PFNEGLGETCURRENTCONTEXTPROC m_getCurrentContext = nullptr;
    PFNEGLCREATEIMAGEKHRPROC m_createImage = nullptr;
    PFNEGLDESTROYIMAGEKHRPROC m_destroyImage = nullptr;
    PFNEGLGETNATIVECLIENTBUFFERANDROIDPROC m_getNativeClientBuffer = nullptr;
    int (*m_allocate)(const AHardwareBuffer_Desc *, AHardwareBuffer **) = nullptr;
    void (*m_release)(AHardwareBuffer *) = nullptr;
    int (*m_sendHandle)(const AHardwareBuffer *, int) = nullptr;
};

class TermuxAHardwareBufferWindow final : public QWaylandWindow
{
public:
    TermuxAHardwareBufferWindow(QWindow *window, QWaylandDisplay *display,
                                TermuxAHardwareBufferIntegration *integration);
    ~TermuxAHardwareBufferWindow() override;

    WindowType windowType() const override;
    void ensureSize() override;
    void invalidateSurface() override;

    bool acquireBuffer(EGLDisplay display);
    void present();
    GLuint framebuffer() const;

private:
    struct Slot {
        TermuxAHardwareBufferWindow *window = nullptr;
        AHardwareBuffer *hardwareBuffer = nullptr;
        EGLImageKHR image = EGL_NO_IMAGE_KHR;
        GLuint texture = 0;
        GLuint framebuffer = 0;
        GLuint depthStencil = 0;
        wl_buffer *waylandBuffer = nullptr;
        bool busy = false;
    };

    bool createSlot(Slot &slot, const QSize &size, EGLDisplay display);
    void destroySlot(Slot &slot, EGLDisplay display);
    void destroySlots(EGLDisplay display);
    static void bufferReleased(void *data, wl_buffer *buffer);

    TermuxAHardwareBufferIntegration *m_integration = nullptr;
    QSize m_bufferSize;
    QSize m_allocatedSize;
    std::array<Slot, 3> m_slots;
    Slot *m_current = nullptr;
    mutable QMutex m_mutex;
};

class TermuxAHardwareBufferContext final : public QPlatformOpenGLContext
{
public:
    TermuxAHardwareBufferContext(TermuxAHardwareBufferIntegration *integration,
                                 const QSurfaceFormat &format, QPlatformOpenGLContext *share);
    ~TermuxAHardwareBufferContext() override;

    QSurfaceFormat format() const override;
    bool makeCurrent(QPlatformSurface *surface) override;
    void doneCurrent() override;
    void swapBuffers(QPlatformSurface *surface) override;
    GLuint defaultFramebufferObject(QPlatformSurface *surface) const override;
    void beginFrame() override;
    void endFrame() override;
    bool isSharing() const override;
    bool isValid() const override;
    QFunctionPointer getProcAddress(const char *name) override;

    EGLConfig eglConfig() const;
    EGLContext eglContext() const;

private:
    TermuxAHardwareBufferIntegration *m_integration = nullptr;
    QSurfaceFormat m_format;
    EGLConfig m_config = nullptr;
    EGLContext m_context = EGL_NO_CONTEXT;
    EGLSurface m_pbuffer = EGL_NO_SURFACE;
    bool m_sharing = false;
    TermuxAHardwareBufferWindow *m_currentWindow = nullptr;
};

}

QT_END_NAMESPACE
