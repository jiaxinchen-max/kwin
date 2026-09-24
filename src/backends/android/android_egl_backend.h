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
#include <memory>

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
    
    // Direct rendering support
    bool trySetupDirectRendering();
    void cleanupDirectRendering();

private:
    AndroidEglBackend *const m_backend;
    std::unique_ptr<GLFramebuffer> m_fbo;
    std::unique_ptr<CpuRenderTimeQuery> m_renderTime;
    
    // Buffer management using termux-render library
    Buffer *m_buffer = nullptr;
    GLuint m_texture = 0;
    GLuint m_framebuffer = 0;
    
    // AHardwareBuffer integration
    EGLImageKHR m_eglImage = EGL_NO_IMAGE_KHR;
    bool m_useDirectRendering = false;
    
    int m_width = 0;
    int m_height = 0;
};

/**
 * @brief EGL backend for Android.
 * 
 * The preferred path uses Android's system EGL/GLES. Zink and llvmpipe remain
 * available for comparison and fallback.
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
    
    // Rendering mode detection
    enum class RenderingMode {
        SystemGlesHardware, // Android system EGL/GLES
        ZinkHardware,      // Zink driver with GPU acceleration
        LlvmpipeSoftware,  // llvmpipe software rendering
        Fallback           // No EGL support
    };
    
    static bool isMesaAvailable();
    static bool isZinkAvailable();
    static RenderingMode detectBestRenderingMode();
    static void setupRendering(RenderingMode mode);
    
    // Environment detection
    static bool detectPRootEnvironment();
    static bool detectContainerEnvironment();
    static bool checkPRootGPUAccess();
    static bool testVulkanDeviceEnumeration();
    
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
