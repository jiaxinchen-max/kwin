/*
    KWin - the KDE window manager
    This file is part of the KDE project.

    SPDX-FileCopyrightText: 2024 Termux Community
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "android_output.h"
#include "android_backend.h"
#include "android_qpainter_backend.h"
// termux_display_api.h removed - using termux/render/render.h directly
#include "core/outputlayer.h"
#include "core/renderloop.h"
#include <QDebug>
#include <QImage>
#include <algorithm>
#include <cstring>

namespace KWin
{
namespace Android
{

AndroidOutput::AndroidOutput(AndroidBackend *backend)
    : BackendOutput()
    , m_backend(backend)
    , m_renderLoop(std::make_unique<RenderLoop>(this))
{
    setInformation(Information{
        .name = QStringLiteral("AndroidScreen-0"),
        .manufacturer = QStringLiteral("Termux"),
        .model = QStringLiteral("Android Display"),
        .serialNumber = QStringLiteral("0"),
        .eisaId = QStringLiteral(""),
    });
    
    updateMode();
}

AndroidOutput::~AndroidOutput()
{
}

void AndroidOutput::updateMode()
{
    const LorieBuffer_Desc *desc = LorieBuffer_description(m_backend->lorieBuffer());
    
    // Create mode
    auto mode = std::make_shared<OutputMode>(
        QSize(desc->width, desc->height),
        m_backend->refreshRate() * 1000
    );
    
    setState(State{
        .position = QPoint(0, 0),
        .modes = {mode},
        .currentMode = mode,
        .enabled = true,
    });
}

bool AndroidOutput::present(const QList<OutputLayer *> &layersToUpdate, const std::shared_ptr<OutputFrame> &frame)
{
    Q_UNUSED(layersToUpdate)
    Q_UNUSED(frame)
    
    if (auto layer = dynamic_cast<AndroidQPainterLayer *>(m_outputLayer)) {
        QImage *sourceImage = layer->image();
        LorieBuffer *buffer = m_backend->lorieBuffer();
        lorie_shared_server_state *state = m_backend->serverState();
        if (sourceImage && !sourceImage->isNull() && buffer && state) {
            const bool canCopySourceFormat = sourceImage->format() == QImage::Format_ARGB32_Premultiplied
                || sourceImage->format() == QImage::Format_ARGB32;
            const QImage image = canCopySourceFormat
                ? *sourceImage
                : sourceImage->convertToFormat(QImage::Format_ARGB32_Premultiplied);
            void *sharedBuffer = nullptr;
            lorie_mutex_lock(&state->lock, &state->lockingPid);
            const int ret = LorieBuffer_lock(buffer, &sharedBuffer);
            if (ret != 0) {
                qWarning() << "Dropping Android frame: failed to lock LorieBuffer in present()" << ret;
                lorie_mutex_unlock(&state->lock, &state->lockingPid);
                return true;
            }

            const LorieBuffer_Desc *desc = LorieBuffer_description(buffer);
            const qsizetype targetBytesPerLine = qsizetype(desc->stride) * 4;
            const int width = std::min(desc->width, std::min(desc->stride, image.width()));
            const int height = std::min(desc->height, image.height());

            if (width < desc->width || height < desc->height) {
                std::memset(sharedBuffer, 0, size_t(targetBytesPerLine) * size_t(desc->height));
            }

            if (desc->format == AHARDWAREBUFFER_FORMAT_B8G8R8A8_UNORM) {
                const size_t copyBytes = size_t(width) * 4;
                for (int y = 0; y < height; ++y) {
                    auto *target = static_cast<unsigned char *>(sharedBuffer) + qsizetype(y) * targetBytesPerLine;
                    std::memcpy(target, image.constScanLine(y), copyBytes);
                }
            } else {
                for (int y = 0; y < height; ++y) {
                    auto *target = static_cast<unsigned char *>(sharedBuffer) + qsizetype(y) * targetBytesPerLine;
                    const auto *source = reinterpret_cast<const QRgb *>(image.constScanLine(y));
                    for (int x = 0; x < width; ++x) {
                        target[x * 4 + 0] = qRed(source[x]);
                        target[x * 4 + 1] = qGreen(source[x]);
                        target[x * 4 + 2] = qBlue(source[x]);
                        target[x * 4 + 3] = qAlpha(source[x]);
                    }
                }
            }

            state->waitForNextFrame = false;
            state->drawRequested = 1;
            pthread_cond_signal(&state->cond);

            lorie_mutex_unlock(&state->lock, &state->lockingPid);
            LorieBuffer_unlock(buffer);

            qDebug() << "Presented Android frame" << desc->width << "x" << desc->height
                     << "type:" << desc->type << "format:" << desc->format;
            return true;
        }
    }

    // Signal the termux-app display server even if no software layer image is available.
    lorie_shared_server_state *state = m_backend->serverState();
    if (state) {
        lorie_mutex_lock(&state->lock, &state->lockingPid);
        state->drawRequested = 1;
        pthread_cond_signal(&state->cond);
        lorie_mutex_unlock(&state->lock, &state->lockingPid);
        return true;
    }
    return false;
}

RenderLoop *AndroidOutput::renderLoop() const
{
    return m_renderLoop.get();
}

bool AndroidOutput::testPresentation(const std::shared_ptr<OutputFrame> &frame)
{
    Q_UNUSED(frame)
    // For Android, we always accept presentation
    return true;
}

void AndroidOutput::updateEnabled(bool enabled)
{
    State state = m_state;
    state.enabled = enabled;
    setState(state);
}

void AndroidOutput::setOutputLayer(OutputLayer *layer)
{
    m_outputLayer = layer;
}

OutputLayer *AndroidOutput::outputLayer() const
{
    return m_outputLayer;
}

} // namespace Android
} // namespace KWin
