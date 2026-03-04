/*
    KWin - the KDE window manager
    This file is part of the KDE project.

    SPDX-FileCopyrightText: 2024 Termux Community
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "android_egl_backend.h"
#include "android_backend.h"
#include "android_output.h"
#include "opengl/eglcontext.h"
#include "opengl/glframebuffer.h"
#include "opengl/glrendertimequery.h"
#include "opengl/glutils.h"
#include "core/renderloop.h"

#include <QOpenGLContext>
#include <android/hardware_buffer.h>
#include "termux_display_api.h"

namespace KWin
{
namespace Android
{

// AndroidEglLayer implementation

AndroidEglLayer::AndroidEglLayer(AndroidOutput *output, AndroidEglBackend *backend)
    : OutputLayer(output, OutputLayerType::Primary, 0, 0, 0)
    , m_backend(backend)
    , m_output(output)
{
}

AndroidEglLayer::~AndroidEglLayer()
{
    cleanup();
}

void AndroidEglLayer::cleanup()
{
    if (!m_backend->openglContext()) {
        return;
    }
    
    m_backend->openglContext()->makeCurrent();
    
    if (m_framebuffer) {
        glDeleteFramebuffers(1, &m_framebuffer);
        m_framebuffer = 0;
    }
    
    if (m_texture) {
        glDeleteTextures(1, &m_texture);
        m_texture = 0;
    }
    
    if (m_eglImage != EGL_NO_IMAGE) {
        eglDestroyImageKHR(m_backend->eglDisplayObject()->handle(), m_eglImage);
        m_eglImage = EGL_NO_IMAGE;
    }
    
    m_fbo.reset();
    m_initialized = false;
}

bool AndroidEglLayer::setupRenderTarget()
{
    if (m_initialized) {
        return true;
    }
    
    if (!m_backend->openglContext()->makeCurrent()) {
        qCritical() << "Failed to make OpenGL context current";
        return false;
    }
    
    // Get the AHardwareBuffer from termux-wayland
    LorieBuffer *lorieBuffer = m_backend->backend()->lorieBuffer();
    const LorieBuffer_Desc *desc = LorieBuffer_description(lorieBuffer);
    AHardwareBuffer *ahb = desc->buffer;
    
    if (!ahb) {
        qCritical() << "AHardwareBuffer is null";
        return false;
    }
    
    qInfo() << "Setting up render target for AHardwareBuffer:" << desc->width << "x" << desc->height;
    
    // Convert AHardwareBuffer to EGLClientBuffer
    EGLClientBuffer clientBuffer = eglGetNativeClientBufferANDROID(ahb);
    if (!clientBuffer) {
        qCritical() << "eglGetNativeClientBufferANDROID failed";
        return false;
    }
    
    // Create EGLImage from AHardwareBuffer
    const EGLint imageAttribs[] = {
        EGL_IMAGE_PRESERVED_KHR, EGL_TRUE,
        EGL_NONE
    };
    
    m_eglImage = eglCreateImageKHR(
        m_backend->eglDisplayObject()->handle(),
        EGL_NO_CONTEXT,
        EGL_NATIVE_BUFFER_ANDROID,
        clientBuffer,
        imageAttribs
    );
    
    if (m_eglImage == EGL_NO_IMAGE) {
        qCritical() << "eglCreateImageKHR failed:" << eglGetError();
        return false;
    }
    
    qInfo() << "EGLImage created successfully";
    
    // Create OpenGL texture and bind EGLImage to it
    glGenTextures(1, &m_texture);
    glBindTexture(GL_TEXTURE_2D, m_texture);
    
    // Bind EGLImage to texture
    glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, (GLeglImageOES)m_eglImage);
    
    GLenum error = glGetError();
    if (error != GL_NO_ERROR) {
        qCritical() << "glEGLImageTargetTexture2DOES failed:" << error;
        cleanup();
        return false;
    }
    
    // Set texture parameters
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    
    qInfo() << "OpenGL texture created and bound to EGLImage";
    
    // Create framebuffer and attach the texture
    glGenFramebuffers(1, &m_framebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, m_framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_texture, 0);
    
    // Check framebuffer status
    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        qCritical() << "Framebuffer incomplete:" << status;
        cleanup();
        return false;
    }
    
    qInfo() << "Framebuffer created and complete";
    
    // Create GLFramebuffer wrapper
    m_fbo = std::make_unique<GLFramebuffer>(m_framebuffer, QSize(desc->width, desc->height));
    
    // Unbind framebuffer
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    
    m_initialized = true;
    qInfo() << "Render target setup complete";
    
    return true;
}

std::optional<OutputLayerBeginFrameInfo> AndroidEglLayer::doBeginFrame()
{
    if (!m_backend->openglContext()->makeCurrent()) {
        qCritical() << "Failed to make OpenGL context current in doBeginFrame";
        return std::nullopt;
    }
    
    // Setup render target if not already initialized
    if (!m_initialized && !setupRenderTarget()) {
        qCritical() << "Failed to setup render target";
        return std::nullopt;
    }
    
    // Bind our framebuffer for rendering
    glBindFramebuffer(GL_FRAMEBUFFER, m_framebuffer);
    
    // Start render time query
    m_query = std::make_unique<GLRenderTimeQuery>(m_backend->openglContextRef());
    m_query->begin();
    
    // Return render target info
    return OutputLayerBeginFrameInfo{
        .renderTarget = RenderTarget(m_fbo.get()),
        .repaint = Region::infinite(),
    };
}

bool AndroidEglLayer::doEndFrame(const Region &renderedDeviceRegion, const Region &damagedDeviceRegion, OutputFrame *frame)
{
    // End render time query
    if (m_query) {
        m_query->end();
        if (frame) {
            frame->addRenderTimeQuery(std::move(m_query));
        }
    }
    
    // Ensure all rendering commands are flushed to the AHardwareBuffer
    glFlush();
    glFinish();  // Wait for GPU to complete
    
    // Unbind framebuffer
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    
    // Signal termux-app that a new frame is ready
    lorie_shared_server_state *state = m_backend->backend()->serverState();
    if (state) {
        lorie_mutex_lock(&state->lock, &state->lockingPid);
        state->drawRequested = 1;
        pthread_cond_signal(&state->cond);
        lorie_mutex_unlock(&state->lock, &state->lockingPid);
    }
    
    return true;
}

void AndroidEglLayer::releaseBuffers()
{
    // Clean up any allocated resources
    cleanup();
}

DrmDevice *AndroidEglLayer::scanoutDevice() const
{
    return nullptr;
}

QHash<uint32_t, QList<uint64_t>> AndroidEglLayer::supportedDrmFormats() const
{
    // Return empty - we don't support DRM format negotiation
    return {};
}

// AndroidEglBackend implementation

AndroidEglBackend::AndroidEglBackend(AndroidBackend *backend)
    : m_backend(backend)
{
}

AndroidEglBackend::~AndroidEglBackend()
{
    cleanup();
}

void AndroidEglBackend::init()
{
    qInfo() << "Initializing Android EGL backend";
    
    if (!initializeEgl()) {
        setFailed("Failed to initialize EGL");
        return;
    }
    
    if (!createEglContext()) {
        setFailed("Failed to create EGL context");
        return;
    }
    
    // Create output layers for existing outputs
    const auto outputs = m_backend->outputs();
    for (BackendOutput *output : outputs) {
        createOutputLayers(output);
    }
    
    qInfo() << "Android EGL backend initialized successfully";
}

bool AndroidEglBackend::initializeEgl()
{
    qInfo() << "Initializing EGL";
    
    // Get EGL display
    EGLDisplay eglDisplay = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (eglDisplay == EGL_NO_DISPLAY) {
        qCritical() << "eglGetDisplay failed";
        return false;
    }
    
    // Initialize EGL
    EGLint major, minor;
    if (!eglInitialize(eglDisplay, &major, &minor)) {
        qCritical() << "eglInitialize failed:" << eglGetError();
        return false;
    }
    
    qInfo() << "EGL version:" << major << "." << minor;
    
    // Create EglDisplay wrapper
    auto display = EglDisplay::create(eglDisplay);
    if (!display) {
        qCritical() << "Failed to create EglDisplay";
        eglTerminate(eglDisplay);
        return false;
    }
    
    setEglDisplay(display.release());
    
    // Bind OpenGL ES API
    if (!eglBindAPI(EGL_OPENGL_ES_API)) {
        qCritical() << "eglBindAPI(EGL_OPENGL_ES_API) failed:" << eglGetError();
        return false;
    }
    
    qInfo() << "EGL initialized successfully";
    return true;
}

bool AndroidEglBackend::createEglContext()
{
    qInfo() << "Creating EGL context";
    
    // Choose EGL config for OpenGL ES 3.0
    const EGLint configAttribs[] = {
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_NONE
    };
    
    EGLConfig config;
    EGLint numConfigs;
    if (!eglChooseConfig(m_display->handle(), configAttribs, &config, 1, &numConfigs) || numConfigs == 0) {
        qCritical() << "eglChooseConfig failed:" << eglGetError();
        return false;
    }
    
    qInfo() << "EGL config chosen";
    
    // Create EGL context for OpenGL ES 3.0
    if (!createContext(config)) {
        qCritical() << "Failed to create EGL context";
        return false;
    }
    
    // Create a pbuffer surface for makeCurrent (since we render to FBO)
    const EGLint pbufferAttribs[] = {
        EGL_WIDTH, 1,
        EGL_HEIGHT, 1,
        EGL_NONE
    };
    
    EGLSurface pbuffer = eglCreatePbufferSurface(m_display->handle(), config, pbufferAttribs);
    if (pbuffer == EGL_NO_SURFACE) {
        qCritical() << "eglCreatePbufferSurface failed:" << eglGetError();
        return false;
    }
    
    // Make context current
    if (!eglMakeCurrent(m_display->handle(), pbuffer, pbuffer, m_context->handle())) {
        qCritical() << "eglMakeCurrent failed:" << eglGetError();
        eglDestroySurface(m_display->handle(), pbuffer);
        return false;
    }
    
    qInfo() << "EGL context created and made current";
    qInfo() << "GL_VENDOR:" << (const char*)glGetString(GL_VENDOR);
    qInfo() << "GL_RENDERER:" << (const char*)glGetString(GL_RENDERER);
    qInfo() << "GL_VERSION:" << (const char*)glGetString(GL_VERSION);
    
    // Check required extensions
    if (!hasExtension(QByteArrayLiteral("EGL_KHR_image_base"))) {
        qWarning() << "EGL_KHR_image_base not available";
    }
    if (!hasExtension(QByteArrayLiteral("EGL_ANDROID_get_native_client_buffer"))) {
        qWarning() << "EGL_ANDROID_get_native_client_buffer not available";
    }
    if (!hasExtension(QByteArrayLiteral("EGL_ANDROID_image_native_buffer"))) {
        qWarning() << "EGL_ANDROID_image_native_buffer not available";
    }
    
    return true;
}

void AndroidEglBackend::createOutputLayers(BackendOutput *output)
{
    AndroidOutput *androidOutput = static_cast<AndroidOutput *>(output);
    auto layer = std::make_unique<AndroidEglLayer>(androidOutput, this);
    m_outputs[output] = std::move(layer);
}

QList<OutputLayer *> AndroidEglBackend::compatibleOutputLayers(BackendOutput *output)
{
    auto it = m_outputs.find(output);
    if (it == m_outputs.end()) {
        createOutputLayers(output);
        it = m_outputs.find(output);
    }
    return {it->second.get()};
}

DrmDevice *AndroidEglBackend::drmDevice() const
{
    return nullptr;
}

void AndroidEglBackend::cleanupSurfaces()
{
    m_outputs.clear();
}

} // namespace Android
} // namespace KWin
