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
#include <termux/render/render.h>  // for lorie_mutex_lock/unlock
#include <termux/render/buffer.h>  // for LorieBuffer_description

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
    
    // Clean up dummy framebuffer
    if (m_fbo) {
        GLuint fboId = m_fbo->handle();
        if (fboId != 0) {
            glDeleteFramebuffers(1, &fboId);
        }
        m_fbo.reset();
    }
    
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
    
    qInfo() << "Setting up render target using termux-render library:" << desc->width << "x" << desc->height;
    
    // Use the shared library's EGL renderer to setup the render target
    EglRenderer *renderer = m_backend->eglRenderer();
    if (!egl_renderer_setup_target(renderer, ahb, desc->width, desc->height)) {
        qCritical() << "Failed to setup render target using shared library";
        return false;
    }
    
    // Create GLFramebuffer wrapper using a dummy framebuffer ID
    // The actual rendering is managed by the shared library
    // We create a minimal wrapper to satisfy KWin's rendering pipeline
    GLuint dummyFbo = 0;
    glGenFramebuffers(1, &dummyFbo);
    m_fbo = std::make_unique<GLFramebuffer>(dummyFbo, QSize(desc->width, desc->height));
    
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
    
    // Use shared library to begin frame
    EglRenderer *renderer = m_backend->eglRenderer();
    if (!egl_renderer_begin_frame(renderer)) {
        qCritical() << "Failed to begin frame using shared library";
        return std::nullopt;
    }
    
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
    
    // Use shared library to end frame
    EglRenderer *renderer = m_backend->eglRenderer();
    if (!egl_renderer_end_frame(renderer)) {
        qCritical() << "Failed to end frame using shared library";
        return false;
    }
    
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
    // Initialize EGL renderer from termux-render library
    memset(&m_eglRenderer, 0, sizeof(m_eglRenderer));
}

AndroidEglBackend::~AndroidEglBackend()
{
    cleanup();
    egl_renderer_cleanup(&m_eglRenderer);
}

void AndroidEglBackend::init()
{
    qInfo() << "Initializing Android EGL backend using termux-render library";
    
    // Initialize EGL renderer from shared library
    if (!egl_renderer_init(&m_eglRenderer)) {
        setFailed("Failed to initialize EGL renderer from termux-render library");
        return;
    }
    
    // Print renderer information
    egl_renderer_print_info(&m_eglRenderer);
    
    if (!initializeEgl()) {
        setFailed("Failed to initialize EGL");
        return;
    }
    
    // Create output layers for existing outputs
    const auto outputs = m_backend->outputs();
    for (BackendOutput *output : outputs) {
        createOutputLayers(output);
    }
    
    qInfo() << "Android EGL backend initialized successfully using shared library";
}

bool AndroidEglBackend::initializeEgl()
{
    qInfo() << "Using EGL context from termux-render shared library";
    
    // Use the EGL display from the shared library renderer
    if (!m_eglRenderer.initialized) {
        qCritical() << "EGL renderer not initialized";
        return false;
    }
    
    // Create EglDisplay wrapper from the shared library's EGL display
    auto display = EglDisplay::create(m_eglRenderer.display);
    if (!display) {
        qCritical() << "Failed to create EglDisplay from shared library";
        return false;
    }
    
    setEglDisplay(display.release());
    
    // Create EglContext wrapper from the shared library's EGL context
    auto context = EglContext::create(m_eglRenderer.context, m_eglRenderer.display, m_eglRenderer.config);
    if (!context) {
        qCritical() << "Failed to create EglContext from shared library";
        return false;
    }
    
    setContext(context.release());
    
    qInfo() << "EGL initialized successfully using shared library";
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
