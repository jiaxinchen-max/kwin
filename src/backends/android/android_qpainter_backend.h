/*
    KWin - the KDE window manager
    This file is part of the KDE project.

    SPDX-FileCopyrightText: 2024 Termux Community
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "core/outputlayer.h"
#include "core/renderbackend.h"
#include "qpainter/qpainterbackend.h"

#include <QImage>
#include <QList>
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
class AndroidQPainterBackend;

/**
 * @brief QPainter rendering layer for Android backend
 * 
 * This layer handles CPU-based rendering using QPainter to a shared buffer
 * that can be displayed by the Android termux-app.
 */
class AndroidQPainterLayer : public OutputLayer
{
public:
    AndroidQPainterLayer(BackendOutput *output, AndroidQPainterBackend *backend);
    ~AndroidQPainterLayer() override;

    std::optional<OutputLayerBeginFrameInfo> doBeginFrame() override;
    bool doEndFrame(const Region &renderedDeviceRegion, const Region &damagedDeviceRegion, OutputFrame *frame) override;
    
    QImage *image();
    DrmDevice *scanoutDevice() const override;
    QHash<uint32_t, QList<uint64_t>> supportedDrmFormats() const override;
    void releaseBuffers() override;
    
    BackendOutput *output() const { return m_output; }

private:
    AndroidQPainterBackend *const m_backend;
    QImage m_image;
    std::unique_ptr<CpuRenderTimeQuery> m_renderTime;
    
    // Buffer management using termux-render library
    Buffer *m_buffer = nullptr;
};

/**
 * @brief QPainter backend for Android
 * 
 * This backend provides CPU-based rendering using QPainter. It renders to
 * memory buffers managed by the termux-render library, which can then be
 * shared with the Android display process without requiring EGL/OpenGL.
 */
class AndroidQPainterBackend : public QPainterBackend
{
    Q_OBJECT

public:
    explicit AndroidQPainterBackend(AndroidBackend *backend);
    ~AndroidQPainterBackend() override;

    QList<OutputLayer *> compatibleOutputLayers(BackendOutput *output) override;
    
    // Android-specific methods
    AndroidBackend *androidBackend() const { return m_backend; }
    
    // Buffer management
    // Buffer management is handled by termux-render library

private:
    void addOutput(BackendOutput *output);

    AndroidBackend *const m_backend;
    QList<AndroidQPainterLayer *> m_layers;
};

} // namespace Android
} // namespace KWin
