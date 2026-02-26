/*
    KWin - the KDE window manager
    This file is part of the KDE project.

    SPDX-FileCopyrightText: 2024 Termux Community
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "android_output.h"
#include "android_backend.h"
#include "termux_display_api.h"

namespace KWin
{
namespace Android
{

AndroidOutput::AndroidOutput(AndroidBackend *backend)
    : m_backend(backend)
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

void AndroidOutput::present()
{
    // Signal the termux-app display server that a new frame is ready
    lorie_shared_server_state *state = m_backend->serverState();
    if (state) {
        lorie_mutex_lock(&state->lock, &state->lockingPid);
        state->drawRequested = 1;
        pthread_cond_signal(&state->cond);
        lorie_mutex_unlock(&state->lock, &state->lockingPid);
    }
}

void AndroidOutput::updateEnabled(bool enabled)
{
    setEnabled(enabled);
}

} // namespace Android
} // namespace KWin
