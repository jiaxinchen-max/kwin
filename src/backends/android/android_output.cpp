/*
    KWin - the KDE window manager
    This file is part of the KDE project.

    SPDX-FileCopyrightText: 2024 Termux Community
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "android_output.h"
#include "android_backend.h"
#include "termux_display_api.h"
#include "core/outputlayer.h"
#include "core/renderloop.h"

namespace KWin
{
namespace Android
{

AndroidOutput::AndroidOutput(AndroidBackend *backend)
    : m_backend(backend)
    , m_renderLoop(std::make_unique<RenderLoop>())
{
    setInformation(Information{
        .name = QStringLiteral("AndroidScreen-0"),
        .manufacturer = QStringLiteral("Termux"),
        .model = QStringLiteral("Android Display"),
        .serialNumber = QStringLiteral("0"),
    });
    
    updateMode();
    setEnabled(true);
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
        60000  // 60Hz in millihertz
    );
    
    setState(State{
        .modes = {mode},
        .currentMode = mode,
    });
}

bool AndroidOutput::present(const QList<OutputLayer *> &layersToUpdate, const std::shared_ptr<OutputFrame> &frame)
{
    Q_UNUSED(layersToUpdate)
    Q_UNUSED(frame)
    
    // Signal the termux-app display server that a new frame is ready
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
    setEnabled(enabled);
}

} // namespace Android
} // namespace KWin
