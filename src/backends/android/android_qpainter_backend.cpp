/*
    KWin - the KDE window manager
    This file is part of the KDE project.

    SPDX-FileCopyrightText: 2024 Termux Community
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "android_qpainter_backend.h"
#include "android_backend.h"
#include "android_output.h"
#include "utils/softwarevsyncmonitor.h"

#include <drm_fourcc.h>
#include <QDebug>

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
    
    if (m_image.size() != nativeSize || m_image.format() != QImage::Format_ARGB32_Premultiplied) {
        qInfo() << "Creating Android QPainter image for size" << nativeSize;
        m_image = QImage(nativeSize, QImage::Format_ARGB32_Premultiplied);
        if (m_image.isNull()) {
            qCritical() << "Failed to allocate Android QPainter image";
            return std::nullopt;
        }
    }

    // Get the global termux-render buffer (initialized by connectToRender)
    if (!m_buffer) {
        m_buffer = reinterpret_cast<Buffer *>(m_backend->androidBackend()->lorieBuffer());
        if (!m_buffer) {
            qCritical() << "Failed to get Android render buffer";
            return std::nullopt;
        }
    }

    m_renderTime = std::make_unique<CpuRenderTimeQuery>();
    
    return OutputLayerBeginFrameInfo{
        .renderTarget = RenderTarget(&m_image),
        .repaint = Region::infinite(),
    };
}

bool AndroidQPainterLayer::doEndFrame(const Region &renderedDeviceRegion, const Region &damagedDeviceRegion, OutputFrame *frame)
{
    m_renderTime->end();
    frame->addRenderTimeQuery(std::move(m_renderTime));

    return true;
}

QImage *AndroidQPainterLayer::image()
{
    return m_image.isNull() ? nullptr : &m_image;
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
    m_image = QImage();
    
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
