/*
    KWin - the KDE window manager
    This file is part of the KDE project.

    SPDX-FileCopyrightText: 2024 Termux Community
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "android_backend.h"
#include "android_qpainter_backend.h"
#include "android_output.h"
#include "core/session.h"
#include "input.h"

#include <QSocketNotifier>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <linux/input-event-codes.h>
#include <unistd.h>

namespace KWin
{
namespace Android
{

static void handleRenderServerStopped()
{
    qWarning("Termux render server stopped");
}

// Use the keycode conversion table from termux/render/render.h.

AndroidBackend::AndroidBackend(QObject *parent)
    : OutputBackend(parent)
{
}

AndroidBackend::~AndroidBackend()
{
    if (m_inputNotifier) {
        delete m_inputNotifier;
    }
    
    if (m_touchDevice) {
        delete m_touchDevice;
    }
    if (m_keyboardDevice) {
        delete m_keyboardDevice;
    }
    if (m_pointerDevice) {
        delete m_pointerDevice;
    }
    
    if (m_output) {
        Q_EMIT outputRemoved(m_output);
        delete m_output;
    }
    
    if (m_lorieBuffer || m_serverState || m_connFd >= 0) {
        stopEventLoop();
        m_lorieBuffer = nullptr;
        m_serverState = nullptr;
        m_connFd = -1;
    }
}

bool AndroidBackend::initialize()
{
    if (m_initialized) {
        return true;
    }

    qInfo() << "Initializing Android Backend";
    
    // Connect to termux-app display server
    if (!connectToDisplayServer()) {
        qCritical() << "Failed to connect to display server";
        return false;
    }
    
    // Create output
    createOutput();

    m_inputEnabled = qEnvironmentVariableIntValue("KWIN_ANDROID_DISABLE_INPUT") != 1;
    if (!m_inputEnabled) {
        qInfo() << "Android input disabled";
        qInfo() << "Android Backend initialized successfully";
        m_initialized = true;
        return true;
    }
    
    // Initialize input devices
    m_touchDevice = new AndroidInputDevice(AndroidInputDevice::Type::Touch, this);
    m_keyboardDevice = new AndroidInputDevice(AndroidInputDevice::Type::Keyboard, this);
    m_pointerDevice = new AndroidInputDevice(AndroidInputDevice::Type::Pointer, this);
    
    // Setup input event listener
    if (m_connFd >= 0) {
        m_inputNotifier = new QSocketNotifier(m_connFd, QSocketNotifier::Read, this);
        connect(m_inputNotifier, &QSocketNotifier::activated, this, &AndroidBackend::handleInputEvents);
    }
    
    qInfo() << "Android Backend initialized successfully";
    m_initialized = true;
    return true;
}

bool AndroidBackend::connectToDisplayServer()
{
    qDebug() << "Connecting to termux display server...";

    bool ok = false;
    const int width = qEnvironmentVariableIntValue("KWIN_ANDROID_WIDTH", &ok);
    if (ok && width > 0) {
        m_width = width;
    }
    const int height = qEnvironmentVariableIntValue("KWIN_ANDROID_HEIGHT", &ok);
    if (ok && height > 0) {
        m_height = height;
    }
    const int refreshRate = qEnvironmentVariableIntValue("KWIN_ANDROID_REFRESH_RATE", &ok);
    if (ok && refreshRate > 0) {
        m_refreshRate = refreshRate;
    }
    
    // Set screen configuration
    setScreenConfig(m_width, m_height, m_refreshRate);

    const QByteArray waylandDisplay = qgetenv("WAYLAND_DISPLAY");
    const bool hadWaylandDisplay = qEnvironmentVariableIsSet("WAYLAND_DISPLAY");
    qunsetenv("WAYLAND_DISPLAY");

    // Connect using termux-wayland library
    if (connectToRender() != 0) {
        if (hadWaylandDisplay) {
            qputenv("WAYLAND_DISPLAY", waylandDisplay);
        }
        qCritical() << "connectToRender() failed";
        stopEventLoop();
        return false;
    }
    if (hadWaylandDisplay) {
        qputenv("WAYLAND_DISPLAY", waylandDisplay);
    }
    setExitCallback(handleRenderServerStopped);
    
    // Get shared resources from termux-wayland library
    m_lorieBuffer = get_lorieBuffer();
    m_serverState = get_serverState();
    m_connFd = get_conn_fd();

    if (m_connFd >= 0) {
        const int flags = fcntl(m_connFd, F_GETFL, 0);
        if (flags >= 0) {
            fcntl(m_connFd, F_SETFL, flags | O_NONBLOCK);
        }
    }
    
    if (!m_lorieBuffer) {
        qCritical() << "Failed to get LorieBuffer";
        return false;
    }
    
    if (!m_serverState) {
        qCritical() << "Failed to get server state";
        return false;
    }
    
    const LorieBuffer_Desc *desc = LorieBuffer_description(m_lorieBuffer);
    m_width = desc->width;
    m_height = desc->height;
    
    qInfo() << "Connected to display server";
    qInfo() << "Buffer size:" << m_width << "x" << m_height;
    
    return true;
}

void AndroidBackend::createOutput()
{
    m_output = new AndroidOutput(this);
    Q_EMIT outputAdded(m_output);
}

std::unique_ptr<InputBackend> AndroidBackend::createInputBackend()
{
    if (!m_inputEnabled) {
        return nullptr;
    }
    return std::make_unique<AndroidInputBackend>(this);
}

std::unique_ptr<EglBackend> AndroidBackend::createOpenGLBackend()
{
    qInfo() << "Android OpenGL backend disabled";
    return nullptr;
}

std::unique_ptr<QPainterBackend> AndroidBackend::createQPainterBackend()
{
    qInfo() << "Creating Android QPainter backend";
    return std::make_unique<AndroidQPainterBackend>(this);
}

EglDisplay *AndroidBackend::sceneEglDisplayObject() const
{
    return nullptr;
}

QList<CompositingType> AndroidBackend::supportedCompositors() const
{
    QList<CompositingType> compositors;
    compositors.append(QPainterCompositing);
    return compositors;
}

QList<BackendOutput *> AndroidBackend::outputs() const
{
    if (m_output) {
        return {m_output};
    }
    return {};
}

QString AndroidBackend::supportInformation() const
{
    QString support = QStringLiteral("Name: Android\n");
    support.append(QStringLiteral("Output: %1x%2@%3Hz\n").arg(m_width).arg(m_height).arg(m_refreshRate));
    support.append(QStringLiteral("Buffer: AHardwareBuffer\n"));
    return support;
}

void AndroidBackend::handleInputEvents()
{
    char buffer[sizeof(lorieEvent) * 16];
    while (true) {
        const ssize_t bytesRead = read(m_connFd, buffer, sizeof(buffer));
        if (bytesRead > 0) {
            m_inputBuffer.append(buffer, static_cast<qsizetype>(bytesRead));
        } else if (bytesRead == 0) {
            qWarning() << "Android input connection closed";
            return;
        } else if (errno == EINTR) {
            continue;
        } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
            break;
        } else {
            qWarning() << "Failed to read Android input event:" << strerror(errno);
            return;
        }
    }

    const qsizetype eventSize = static_cast<qsizetype>(sizeof(lorieEvent));
    while (m_inputBuffer.size() >= eventSize) {
        lorieEvent e = {};
        std::memcpy(&e, m_inputBuffer.constData(), sizeof(e));
        m_inputBuffer.remove(0, eventSize);
        processInputEvent(e);
    }
}

void AndroidBackend::processInputEvent(const lorieEvent &e)
{
    switch (e.type) {
    case EVENT_TOUCH: {
        if (!m_touchDevice) break;
        
        const auto &touch = e.touch;
        const QPointF pos(touch.x, touch.y);
        
        switch (touch.type) {
        case 0: // Down
            Q_EMIT m_touchDevice->touchDown(touch.id, pos, std::chrono::milliseconds(0), m_touchDevice);
            break;
        case 1: // Up
            Q_EMIT m_touchDevice->touchUp(touch.id, std::chrono::milliseconds(0), m_touchDevice);
            break;
        case 2: // Motion
            Q_EMIT m_touchDevice->touchMotion(touch.id, pos, std::chrono::milliseconds(0), m_touchDevice);
            break;
        }
        Q_EMIT m_touchDevice->touchFrame(m_touchDevice);
        break;
    }
    
    case EVENT_MOUSE: {
        if (!m_pointerDevice) break;
        
        const auto &mouse = e.mouse;
        const QPointF pos(mouse.x, mouse.y);
        
        if (mouse.relative) {
            QPointF delta(mouse.x, mouse.y);
            Q_EMIT m_pointerDevice->pointerMotion(delta, delta, std::chrono::microseconds(0), m_pointerDevice);
        } else {
            Q_EMIT m_pointerDevice->pointerMotionAbsolute(pos, std::chrono::microseconds(0), m_pointerDevice);
        }
        
        if (mouse.detail > 0) {
            PointerButtonState state = mouse.down ? PointerButtonState::Pressed : PointerButtonState::Released;
            Q_EMIT m_pointerDevice->pointerButtonChanged(mouse.detail, state, std::chrono::milliseconds(0), m_pointerDevice);
        }
        
        Q_EMIT m_pointerDevice->pointerFrame(m_pointerDevice);
        break;
    }
    
    case EVENT_KEY: {
        if (!m_keyboardDevice) break;
        
        const auto &key = e.key;
        int linuxKeycode = key.key;
        
        // Convert Android keycode to Linux keycode if needed.
        // android_to_linux_keycode is a static table from termux/render/render.h.
        if (key.key < 304 && android_to_linux_keycode[key.key] != 0) {
            linuxKeycode = android_to_linux_keycode[key.key];
        }
        
        KeyboardKeyState state = key.state ? KeyboardKeyState::Pressed : KeyboardKeyState::Released;
        Q_EMIT m_keyboardDevice->keyChanged(linuxKeycode, state, std::chrono::milliseconds(0), m_keyboardDevice);
        break;
    }
    
    default:
        break;
    }
}

// AndroidInputBackend implementation
AndroidInputBackend::AndroidInputBackend(AndroidBackend *backend)
    : m_backend(backend)
{
}

AndroidInputBackend::~AndroidInputBackend()
{
}

void AndroidInputBackend::initialize()
{
    // Input devices are created by AndroidBackend
    if (m_backend->touchDevice()) {
        Q_EMIT deviceAdded(m_backend->touchDevice());
    }
    if (m_backend->keyboardDevice()) {
        Q_EMIT deviceAdded(m_backend->keyboardDevice());
    }
    if (m_backend->pointerDevice()) {
        Q_EMIT deviceAdded(m_backend->pointerDevice());
    }
}

// AndroidInputDevice implementation
AndroidInputDevice::AndroidInputDevice(Type type, AndroidBackend *backend)
    : m_type(type)
    , m_backend(backend)
{
}

AndroidInputDevice::~AndroidInputDevice()
{
}

QString AndroidInputDevice::name() const
{
    switch (m_type) {
    case Type::Keyboard:
        return QStringLiteral("Android Virtual Keyboard");
    case Type::Pointer:
        return QStringLiteral("Android Virtual Pointer");
    case Type::Touch:
        return QStringLiteral("Android Virtual Touchscreen");
    }
    return QStringLiteral("Android Virtual Input");
}

bool AndroidInputDevice::isEnabled() const
{
    return m_enabled;
}

void AndroidInputDevice::setEnabled(bool enabled)
{
    m_enabled = enabled;
}

bool AndroidInputDevice::isKeyboard() const
{
    return m_type == Type::Keyboard;
}

bool AndroidInputDevice::isPointer() const
{
    return m_type == Type::Pointer;
}

bool AndroidInputDevice::isTouchpad() const
{
    return false;
}

bool AndroidInputDevice::isTouch() const
{
    return m_type == Type::Touch;
}

bool AndroidInputDevice::isTabletTool() const
{
    return false;
}

bool AndroidInputDevice::isTabletPad() const
{
    return false;
}

bool AndroidInputDevice::isTabletModeSwitch() const
{
    return false;
}

bool AndroidInputDevice::isLidSwitch() const
{
    return false;
}

} // namespace Android
} // namespace KWin
