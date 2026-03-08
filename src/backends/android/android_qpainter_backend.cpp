/*
    KWin - the KDE window manager
    This file is part of the KDE project.

    SPDX-FileCopyrightText: 2024 Termux Community
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "android_qpainter_backend.h"
#include "android_backend.h"
#include "android_output.h"
#include "core/graphicsbufferview.h"
#include "core/shmgraphicsbufferallocator.h"
#include "qpainter/qpainterswapchain.h"
#include "utils/softwarevsyncmonitor.h"

#include <drm_fourcc.h>
#include <QDebug>

// Include termux-render headers
#include <termux/render/buffer.h>
#include <termux/render/render.h>
#include <termux/render/tlog.h>

// External functions from termux-render library
extern LorieBuffer* get_lorieBuffer();
extern struct lorie_shared_server_state* get_serverState();

// Define Buffer type properly
struct Buffer_Desc {
    int width;
    int height; 
    int format;
    void *data;
    size_t size;
};
typedef struct Buffer_Desc Buffer;

namespace KWin
{
namespace Android
{

AndroidQPainterLayer::AndroidQPainterLayer(BackendOutput *output, AndroidQPainterBackend *backend)
    : OutputLayer(output, OutputLayerType::Primary)
    , m_backend(backend)
{
    qInfo() << "Created AndroidQPainterLayer for output" << output->name();
}

AndroidQPainterLayer::~AndroidQPainterLayer()
{
    // Don't release the buffer - it's a global shared resource
    m_buffer = nullptr;
    qInfo() << "Destroyed AndroidQPainterLayer";
}

std::optional<OutputLayerBeginFrameInfo> AndroidQPainterLayer::doBeginFrame()
{
    const QSize nativeSize(m_output->modeSize());
    qDebug() << "AndroidQPainterLayer::doBeginFrame() - size:" << nativeSize;
    
    // Create or recreate swapchain if size changed
    if (!m_swapchain || m_swapchain->size() != nativeSize) {
        qInfo() << "Creating new QPainter swapchain for size" << nativeSize;
        // Note: We don't have a GraphicsBufferAllocator in AndroidBackend yet,
        // so we'll create a simple SHM allocator for now
        static auto allocator = std::make_unique<ShmGraphicsBufferAllocator>();
        m_swapchain = std::make_unique<QPainterSwapchain>(allocator.get(), nativeSize, DRM_FORMAT_XRGB8888);
    }

    m_current = m_swapchain->acquire();
    if (!m_current) {
        qCritical() << "Failed to acquire swapchain slot";
        return std::nullopt;
    }

    // Get the global termux-render buffer (initialized by connectToRender)
    if (!m_buffer) {
        m_buffer = (Buffer*)get_lorieBuffer();
        if (!m_buffer) {
            qCritical() << "Failed to get termux-render buffer - is connectToRender() called?";
            return std::nullopt;
        }
    }

    m_renderTime = std::make_unique<CpuRenderTimeQuery>();
    
    return OutputLayerBeginFrameInfo{
        .renderTarget = RenderTarget(m_current->view()->image()),
        .repaint = Region::infinite(),
    };
}

bool AndroidQPainterLayer::doEndFrame(const Region &renderedDeviceRegion, const Region &damagedDeviceRegion, OutputFrame *frame)
{
    m_renderTime->end();
    frame->addRenderTimeQuery(std::move(m_renderTime));
    
    // Copy the rendered image to the termux-render buffer
    if (m_buffer && m_current) {
        QImage *sourceImage = m_current->view()->image();
        if (sourceImage && !sourceImage->isNull()) {
            // Get server state for locking
            struct lorie_shared_server_state *serverState = get_serverState();
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
                // Convert QImage to the buffer format
                QImage convertedImage = sourceImage->convertToFormat(QImage::Format_ARGB32);
                
                // Copy image data to shared buffer
                const int bytesPerLine = desc->width * 4; // ARGB32 = 4 bytes per pixel
                const int imageBytesPerLine = convertedImage.bytesPerLine();
                const int copyBytesPerLine = qMin(bytesPerLine, imageBytesPerLine);
                
                for (int y = 0; y < qMin(desc->height, convertedImage.height()); ++y) {
                    memcpy(
                        static_cast<char*>(shared_buffer) + y * bytesPerLine,
                        convertedImage.constScanLine(y),
                        copyBytesPerLine
                    );
                }
                
                // Signal that drawing is requested
                serverState->waitForNextFrame = false;
                serverState->drawRequested = 1;
                pthread_cond_signal(&serverState->cond);
                
                qDebug() << "Copied frame to shared buffer:" << desc->width << "x" << desc->height;
            }
            
            // Unlock the buffer
            LorieBuffer_unlock((LorieBuffer*)m_buffer);
            lorie_mutex_unlock(&serverState->lock, &serverState->lockingPid);
        }
    }
    
    return true;
}

QImage *AndroidQPainterLayer::image()
{
    return m_current ? m_current->view()->image() : nullptr;
}

DrmDevice *AndroidQPainterLayer::scanoutDevice() const
{
    // Android backend doesn't use DRM
    return nullptr;
}

QHash<uint32_t, QList<uint64_t>> AndroidQPainterLayer::supportedDrmFormats() const
{
    return {{DRM_FORMAT_ARGB8888, {DRM_FORMAT_MOD_LINEAR}}};
}

void AndroidQPainterLayer::releaseBuffers()
{
    m_current.reset();
    m_swapchain.reset();
    
    // Don't release the buffer - it's a global shared resource
    m_buffer = nullptr;
}

AndroidQPainterBackend::AndroidQPainterBackend(AndroidBackend *backend)
    : m_backend(backend)
{
    qInfo() << "Initializing Android QPainter backend";
    
    // Connect to backend signals
    connect(backend, &AndroidBackend::outputAdded, this, &AndroidQPainterBackend::addOutput);
    
    // Add existing outputs
    const auto outputs = backend->outputs();
    for (BackendOutput *output : outputs) {
        addOutput(output);
    }
    
    qInfo() << "Android QPainter backend initialized with" << m_layers.size() << "layers";
}

AndroidQPainterBackend::~AndroidQPainterBackend()
{
    qInfo() << "Destroying Android QPainter backend";
    
    // Clean up layers
    for (AndroidQPainterLayer *layer : m_layers) {
        delete layer;
    }
    m_layers.clear();
}

QList<OutputLayer *> AndroidQPainterBackend::compatibleOutputLayers(BackendOutput *output)
{
    // Find the layer for this output
    for (AndroidQPainterLayer *layer : m_layers) {
        if (layer->output() == output) {
            return {layer};
        }
    }
    
    // If no layer exists, create one
    addOutput(output);
    
    // Try again
    for (AndroidQPainterLayer *layer : m_layers) {
        if (layer->output() == output) {
            return {layer};
        }
    }
    
    return {};
}

// Buffer management is handled by termux-render library
// No need to create/destroy buffers - they are global shared resources

void AndroidQPainterBackend::addOutput(BackendOutput *output)
{
    qInfo() << "Adding output to QPainter backend:" << output->name();
    
    // Create layer for this output
    AndroidQPainterLayer *layer = new AndroidQPainterLayer(output, this);
    m_layers.append(layer);
    
    // Set the layer on the output if it's an AndroidOutput
    if (auto androidOutput = qobject_cast<AndroidOutput *>(output)) {
        androidOutput->setOutputLayer(layer);
    }
}

} // namespace Android
} // namespace KWin

#include "android_qpainter_backend.moc"