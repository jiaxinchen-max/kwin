/*
    KWin - the KDE window manager
    This file is part of the KDE project.

    SPDX-FileCopyrightText: 2024 Termux Community
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "opengl/eglbackend.h"
#include "core/outputlayer.h"

#include <epoxy/egl.h>
#include <epoxy/gl.h>
#include <unordered_map>
#include <memory>
#include <dlfcn.h>
#include <termux/render/egl_renderer.h>

namespace KWin
{

class GLFramebuffer;
class GLRenderTimeQuery;

namespace Android
{

class AndroidBackend;
class AndroidOutput;
class AndroidEglBackend;

/**
 * @brief OpenGL rendering layer for Android
 * 
 * This layer renders directly into the AHardwareBuffer provided by
 * the termux-app display service.
 */
class AndroidEglLayer : public OutputLayer
{
    Q_OBJECT

public:
    AndroidEglLayer(AndroidOutput *output, AndroidEglBackend *backend);
    ~AndroidEglLayer() override;

    std::optional<OutputLayerBeginFrameInfo> doBeginFrame() override;
    bool doEndFrame(const Region &renderedDeviceRegion, const Region &damagedDeviceRegion, OutputFrame *frame) override;
    void releaseBuffers() override;
    DrmDevice *scanoutDevice() const override;
    QHash<uint32_t, QList<uint64_t>> supportedDrmFormats() const override;

private:
    bool setupRenderTarget();
    void cleanup();
    
    AndroidEglBackend *m_backend;
    AndroidOutput *m_output;
    
    // OpenGL resources (EGL resources managed by shared library)
    std::unique_ptr<GLFramebuffer> m_fbo;
    std::unique_ptr<GLRenderTimeQuery> m_query;
    
    bool m_initialized = false;
};

/**
 * @brief OpenGL ES backend for Android
 * 
 * Uses EGL to create an OpenGL ES context and imports the AHardwareBuffer
 * from termux-app as the render target via EGLImage.
 */
class AndroidEglBackend : public EglBackend
{
    Q_OBJECT

public:
    explicit AndroidEglBackend(AndroidBackend *backend);
    ~AndroidEglBackend() override;

    void init() override;
    QList<OutputLayer *> compatibleOutputLayers(BackendOutput *output) override;
    DrmDevice *drmDevice() const override;
    
    AndroidBackend *backend() const { return m_backend; }
    bool isAndroidEglExtensionsAvailable() const { return egl_renderer_has_android_extensions(&m_eglRenderer); }
    EglRenderer *eglRenderer() { return &m_eglRenderer; }

private:
    bool initializeEgl();
    void createOutputLayers(BackendOutput *output);
    void cleanupSurfaces() override;
    
    AndroidBackend *m_backend;
    std::unordered_map<BackendOutput *, std::unique_ptr<AndroidEglLayer>> m_outputs;
    
    // Use termux-render shared library for EGL operations
    EglRenderer m_eglRenderer;
};

} // namespace Android
} // namespace KWin
