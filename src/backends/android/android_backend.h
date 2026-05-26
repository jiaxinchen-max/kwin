/*
    KWin - the KDE window manager
    This file is part of the KDE project.

    SPDX-FileCopyrightText: 2024 Termux Community
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "core/inputbackend.h"
#include "core/outputbackend.h"
#include "core/inputdevice.h"
#include <kwin_export.h>
#include <QByteArray>
#include <QObject>
#include <QSocketNotifier>

// Include termux-render headers for complete type definitions.
// The installed headers are C headers and may not provide C++ linkage guards.
extern "C" {
#include <termux/render/render.h>
#include <termux/render/buffer.h>
}

// Forward declarations for termux-wayland library
struct LorieBuffer;

namespace KWin
{
namespace Android
{

class AndroidBackend;
class AndroidOutput;
class AndroidInputDevice;

/**
 * @brief Android Backend for KWin
 * 
 * This backend connects to the termux-app display service through
 * the termux-wayland protocol. It renders to an AHardwareBuffer
 * that is shared with the Android display application.
 */
class KWIN_EXPORT AndroidBackend : public OutputBackend
{
    Q_OBJECT

public:
    explicit AndroidBackend(QObject *parent = nullptr);
    ~AndroidBackend() override;

    bool initialize() override;
    std::unique_ptr<InputBackend> createInputBackend() override;
    std::unique_ptr<EglBackend> createOpenGLBackend() override;
    std::unique_ptr<QPainterBackend> createQPainterBackend() override;
    
    EglDisplay *sceneEglDisplayObject() const override;
    QList<CompositingType> supportedCompositors() const override;
    QList<BackendOutput *> outputs() const override;

    // Android-specific methods
    LorieBuffer *lorieBuffer() const { return m_lorieBuffer; }
    lorie_shared_server_state *serverState() const { return m_serverState; }
    int connectionFd() const { return m_connFd; }
    int refreshRate() const { return m_refreshRate; }
    
    // Input device accessors
    AndroidInputDevice *touchDevice() const { return m_touchDevice; }
    AndroidInputDevice *keyboardDevice() const { return m_keyboardDevice; }
    AndroidInputDevice *pointerDevice() const { return m_pointerDevice; }
    
    QString supportInformation() const override;

private Q_SLOTS:
    void handleInputEvents();

private:
    bool connectToDisplayServer();
    void waitForProtocolInit();
    void sendScreenConfig();
    void processInputEvent(const lorieEvent &event);
    void createOutput();

    // Socket communication
    int m_socketFd = -1;
    int m_connFd = -1;
    
    // Shared resources from termux-wayland
    LorieBuffer *m_lorieBuffer = nullptr;
    lorie_shared_server_state *m_serverState = nullptr;
    
    // Output
    AndroidOutput *m_output = nullptr;
    bool m_initialized = false;
    bool m_inputEnabled = true;
    
    // Input handling
    QSocketNotifier *m_inputNotifier = nullptr;
    QByteArray m_inputBuffer;
    qint64 m_inputBytesToDiscard = 0;
    AndroidInputDevice *m_touchDevice = nullptr;
    AndroidInputDevice *m_keyboardDevice = nullptr;
    AndroidInputDevice *m_pointerDevice = nullptr;
    
    // Configuration
    int m_width = 1080;
    int m_height = 720;
    int m_refreshRate = 27;
};

/**
 * @brief Input backend for Android
 */
class AndroidInputBackend : public InputBackend
{
    Q_OBJECT

public:
    explicit AndroidInputBackend(AndroidBackend *backend);
    ~AndroidInputBackend() override;

    void initialize() override;

private:
    AndroidBackend *m_backend;
};

/**
 * @brief Input device for Android
 */
class AndroidInputDevice : public InputDevice
{
    Q_OBJECT

public:
    enum class Type {
        Keyboard,
        Pointer,
        Touch
    };

    AndroidInputDevice(Type type, AndroidBackend *backend);
    ~AndroidInputDevice() override;

    QString name() const override;
    bool isEnabled() const override;
    void setEnabled(bool enabled) override;
    
    bool isKeyboard() const override;
    bool isPointer() const override;
    bool isTouchpad() const override;
    bool isTouch() const override;
    bool isTabletTool() const override;
    bool isTabletPad() const override;
    bool isTabletModeSwitch() const override;
    bool isLidSwitch() const override;

private:
    Type m_type;
    bool m_enabled = true;
    AndroidBackend *m_backend;
};

} // namespace Android
} // namespace KWin
