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
#include <optional>

namespace KWin
{
namespace Android
{

static std::optional<QImage::Format> imageFormatForLorieFormat(int format)
{
    if (format == AHARDWAREBUFFER_FORMAT_B8G8R8A8_UNORM) {
        return QImage::Format_ARGB32_Premultiplied;
    }
    if (format == AHARDWAREBUFFER_FORMAT_R8G8B8X8_UNORM) {
        return QImage::Format_RGBX8888;
    }
    return std::nullopt;
}

AndroidQPainterLayer::AndroidQPainterLayer(BackendOutput *output, AndroidQPainterBackend *backend)
    : OutputLayer(output, OutputLayerType::Primary)
    , m_backend(backend)
{
    qInfo() << "Created AndroidQPainterLayer for output" << output->name();
}

AndroidQPainterLayer::~AndroidQPainterLayer()
{
    unlockBuffer();
    qInfo() << "Destroyed AndroidQPainterLayer";
}

std::optional<OutputLayerBeginFrameInfo> AndroidQPainterLayer::doBeginFrame()
{
    if (!ensureBuffer()) {
        return std::nullopt;
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

bool AndroidQPainterLayer::flushBuffer()
{
    if (!m_buffer || !m_lockedData) {
        return true;
    }

    int ret = LorieBuffer_unlock(m_buffer);
    m_lockedData = nullptr;
    m_image = QImage();
    if (ret != 0) {
        qWarning() << "Failed to unlock Android render buffer" << ret;
        m_buffer = nullptr;
        return false;
    }
    return true;
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
    unlockBuffer();
}

bool AndroidQPainterLayer::ensureBuffer()
{
    LorieBuffer *buffer = m_backend->androidBackend()->lorieBuffer();
    if (!buffer) {
        qCritical() << "Failed to get Android render buffer";
        return false;
    }

    if (m_buffer != buffer) {
        unlockBuffer();
        m_buffer = buffer;
    }

    if (!m_lockedData) {
        void *data = nullptr;
        const int ret = LorieBuffer_lock(m_buffer, &data);
        if (ret != 0 || !data) {
            qWarning() << "Failed to lock Android render buffer" << ret << data;
            m_buffer = nullptr;
            return false;
        }
        m_lockedData = data;
    }

    const LorieBuffer_Desc *desc = LorieBuffer_description(m_buffer);
    const auto imageFormat = imageFormatForLorieFormat(desc->format);
    if (!imageFormat) {
        qWarning() << "Unsupported Android render buffer format" << desc->format;
        unlockBuffer();
        return false;
    }

    const QSize size(desc->width, desc->height);
    const qsizetype bytesPerLine = qsizetype(desc->stride) * 4;
    if (m_image.bits() != static_cast<uchar *>(m_lockedData) || m_image.size() != size
        || m_image.bytesPerLine() != bytesPerLine || m_image.format() != *imageFormat) {
        m_image = QImage(static_cast<uchar *>(m_lockedData), desc->width, desc->height,
                         bytesPerLine, *imageFormat);
    }

    return !m_image.isNull();
}

void AndroidQPainterLayer::unlockBuffer()
{
    m_image = QImage();
    if (m_buffer && m_lockedData) {
        LorieBuffer_unlock(m_buffer);
    }
    m_lockedData = nullptr;
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
