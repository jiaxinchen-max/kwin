/*
    KWin - the KDE window manager
    This file is part of the KDE project.

    SPDX-FileCopyrightText: 2024 Termux Community
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "core/outputlayer.h"
#include "core/renderbackend.h"
#include "opengl/eglbackend.h"
#include "opengl/glframebuffer.h"

#include <QObject>
#include <cstdint>
#include <memory>
#include <vector>

// Forward declare Buffer type from termux-render
struct Buffer_Desc;
typedef struct Buffer_Desc Buffer;

namespace KWin
{
namespace Android
{

class AndroidBackend;
class AndroidEglBackend;

/**
 * @brief EGL rendering layer for the Android backend.
 * 
 * Rendering can use Android system GLES, llvmpipe, or Mesa Zink backed by a
 * Vulkan ICD. The output is imported from termux-render as an AHardwareBuffer.
 */
class AndroidEglLayer : public OutputLayer
{
public:
    AndroidEglLayer(BackendOutput *output, AndroidEglBackend *backend);
    ~AndroidEglLayer() override;

    std::optional<OutputLayerBeginFrameInfo> doBeginFrame() override;
    bool doEndFrame(const Region &renderedDeviceRegion, const Region &damagedDeviceRegion, OutputFrame *frame) override;
    
    DrmDevice *scanoutDevice() const override;
    QHash<uint32_t, QList<uint64_t>> supportedDrmFormats() const override;
    void releaseBuffers() override;
    
    // Access to the output
    BackendOutput *output() const { return m_output; }

    // Setup render target using termux-render buffer
    bool setupRenderTarget();
    void cleanup();
    
    // Output buffer import. The renderer pins exactly one of these; there is no
    // implicit fallback between them (see resolveOutputImport()).
    enum class OutputImport {
        Native,   // EGL_NATIVE_BUFFER_ANDROID zero-copy, paired with system GLES
        DmaBuf,   // dma-buf EGLImage zero-copy, paired with Mesa Zink
        Readback, // CPU glReadPixels into the shared buffer, paired with llvmpipe
        Invalid,
    };
    OutputImport resolveOutputImport() const;
    bool trySetupDmaBufDirectRendering();
    bool trySetupAndroidNativeBufferDirectRendering();
    bool setupDirectFramebuffer();
    void cleanupDirectRendering();

private:
    AndroidEglBackend *const m_backend;
    std::unique_ptr<GLFramebuffer> m_fbo;
    std::unique_ptr<CpuRenderTimeQuery> m_renderTime;
    
    // Buffer management using termux-render library
    Buffer *m_buffer = nullptr;
    GLuint m_texture = 0;
    GLuint m_framebuffer = 0;
    std::vector<uint8_t> m_readbackBuffer;
    
    // AHardwareBuffer integration
    EGLImageKHR m_eglImage = EGL_NO_IMAGE_KHR;
    bool m_useDirectRendering = false;
    
    int m_width = 0;
    int m_height = 0;
};

/**
 * @brief EGL backend for Android.
 * 
 * The renderer (Android system EGL/GLES, Mesa Zink or llvmpipe) is chosen by
 * the kwin-android-rendering-env shell helper, which probes the device and
 * exports the environment. This backend only consumes that decision.
 */
class AndroidEglBackend : public EglBackend
{
    Q_OBJECT

public:
    explicit AndroidEglBackend(AndroidBackend *backend);
    ~AndroidEglBackend() override;

    void init() override;
    void present(BackendOutput *output, const std::shared_ptr<OutputFrame> &frame);
    BackendOutput *findOutput(EGLNativeWindowType window) const;
    
    QList<OutputLayer *> compatibleOutputLayers(BackendOutput *output) override;
    
    // Android-specific methods
    AndroidBackend *androidBackend() const { return m_backend; }
    
    // Renderer as resolved by kwin-android-rendering-env. The script leaves
    // KWIN_ANDROID_GL_MODE set to system, zink or llvmpipe; anything else is
    // an unresolved request and yields Fallback.
    enum class RenderingMode {
        SystemGlesHardware, // Android system EGL/GLES
        ZinkHardware,      // Mesa Zink on a Vulkan ICD
        LlvmpipeSoftware,  // Mesa llvmpipe software rendering
        Fallback           // No usable EGL renderer
    };

    static RenderingMode renderingModeFromEnvironment();
    RenderingMode renderingMode() const { return m_renderingMode; }
    
    // Buffer management
    // Buffer management is handled by termux-render library

private:
    bool initializeEgl();
    void addOutput(BackendOutput *output);

    AndroidBackend *const m_backend;
    QList<AndroidEglLayer *> m_layers;
    
    bool m_eglAvailable = false;
    RenderingMode m_renderingMode = RenderingMode::Fallback;
};

} // namespace Android
} // namespace KWin
