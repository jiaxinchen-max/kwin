/*
    KWin - the KDE window manager
    This file is part of the KDE project.

    SPDX-FileCopyrightText: 2024 Termux Community
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "core/output.h"

namespace KWin
{
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

    void present() override;
    void updateEnabled(bool enabled) override;

private:
    void updateMode();
    
    AndroidBackend *m_backend;
};

} // namespace Android
} // namespace KWin
