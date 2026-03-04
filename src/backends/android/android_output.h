/*
    KWin - the KDE window manager
    This file is part of the KDE project.

    SPDX-FileCopyrightText: 2024 Termux Community
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "core/backendoutput.h"
#include <memory>

namespace KWin
{

class OutputLayer;
class OutputFrame;
class RenderLoop;

namespace Android
{

class AndroidBackend;

/**
 * @brief Virtual output for Android display
 */
class AndroidOutput : public BackendOutput
{
    Q_OBJECT

public:
    explicit AndroidOutput(AndroidBackend *backend);
    ~AndroidOutput() override;

    RenderLoop *renderLoop() const override;
    bool testPresentation(const std::shared_ptr<OutputFrame> &frame) override;
    bool present(const QList<OutputLayer *> &layersToUpdate, const std::shared_ptr<OutputFrame> &frame) override;
    void updateEnabled(bool enabled);

private:
    void updateMode();
    
    AndroidBackend *m_backend;
    std::unique_ptr<RenderLoop> m_renderLoop;
};

} // namespace Android
} // namespace KWin
