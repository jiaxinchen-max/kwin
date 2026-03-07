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
 * @brief EGL rendering layer for Android backend using Mesa software rendering
 * 
 * This layer handles OpenGL ES rendering using Mesa's llvmpipe software renderer.
 * It avoids JavaVM dependencies by using Mesa's software implementation.
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

private:
    AndroidEglBackend *const m_backend;
    std::unique_ptr<GLFramebuffer> m_fbo;
    std::unique_ptr<CpuRenderTimeQuery> m_renderTime;
    
    // Buffer management using termux-render library
    Buffer *m_buffer = nullptr;
    GLuint m_texture = 0;
    GLuint m_framebuffer = 0;
    
    int m_width = 0;
    int m_height = 0;
};

/**
 * @brief EGL backend for Android using Mesa software rendering
 * 
 * This backend uses Mesa's llvmpipe software renderer to provide OpenGL ES
 * functionality without requiring hardware GPU access or JavaVM integration.
 * It's specifically designed for Termux environments.
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
        ZinkHardware,      // Zink driver with GPU acceleration
        LlvmpipeSoftware,  // llvmpipe software rendering
        Fallback           // No Mesa support
    };
    
    static bool isMesaAvailable();
    static bool isZinkAvailable();
    static RenderingMode detectBestRenderingMode();
    static void setupMesaRendering(RenderingMode mode);
    
    // Environment detection
    static bool detectPRootEnvironment();
    static bool detectContainerEnvironment();
    static bool checkPRootGPUAccess();
    static bool testVulkanDeviceEnumeration();
    
    // Buffer management
    Buffer *createBuffer(int width, int height);
    void releaseBuffer(Buffer *buffer);

private:
    bool initializeEgl();
    void addOutput(BackendOutput *output);

    AndroidBackend *const m_backend;
    QList<AndroidEglLayer *> m_layers;
    
    // Mesa detection
    bool m_mesaAvailable = false;
};

} // namespace Android
} // namespace KWin