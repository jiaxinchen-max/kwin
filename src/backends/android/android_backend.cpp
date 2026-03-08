/*
    KWin - the KDE window manager
    This file is part of the KDE project.

    SPDX-FileCopyrightText: 2024 Termux Community
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "android_backend.h"
#include "android_egl_backend.h"
#include "android_qpainter_backend.h"
#include "android_output.h"
#include "core/session.h"
#include "input.h"

#include <QSocketNotifier>
#include <linux/input-event-codes.h>
#include <unistd.h>

// termux-render library headers
#include <termux/render/render.h>  // for connectToRender, get_connFd, android_to_linux_keycode, etc.

// External symbol from termux-render library
extern "C" {
    extern int android_to_linux_keycode[304];
}

namespace KWin
{
namespace Android
{

// Use the keycode conversion table from termux-render library
// (defined in termux/render/render.h)

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
    
    if (m_socketFd >= 0) {
        close(m_socketFd);
    }
    if (m_connFd >= 0) {
        close(m_connFd);
    }
}

bool AndroidBackend::initialize()
{
    qInfo() << "Initializing Android Backend";
    
    // Connect to termux-app display server
    if (!connectToDisplayServer()) {
        qCritical() << "Failed to connect to display server";
        return false;
    }
    
    // Create output
    createOutput();
    
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
    return true;
}

bool AndroidBackend::connectToDisplayServer()
{
    qDebug() << "Connecting to termux display server...";
    
    // Set screen configuration
    setScreenConfig(m_width, m_height, m_refreshRate);
    
    // Connect using termux-wayland library
    if (connectToRender() != 0) {
        qCritical() << "connectToRender() failed";
        return false;
    }
    
    // Get shared resources from termux-wayland library
    m_lorieBuffer = get_lorieBuffer();
    m_serverState = get_serverState();
    m_connFd = get_connFd();
    
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
    return std::make_unique<AndroidInputBackend>(this);
}

std::unique_ptr<EglBackend> AndroidBackend::createOpenGLBackend()
{
    qInfo() << "Creating Android OpenGL backend";
    
    // Check if Mesa is available for software rendering
    if (AndroidEglBackend::isMesaAvailable()) {
        qInfo() << "Mesa detected - creating EGL backend with software rendering";
        return std::make_unique<AndroidEglBackend>(this);
    } else {
        qWarning() << "Mesa not available - OpenGL backend not supported";
        qWarning() << "Install mesa package: pkg install mesa";
        return nullptr;
    }
}

std::unique_ptr<QPainterBackend> AndroidBackend::createQPainterBackend()
{
    qInfo() << "Creating Android QPainter backend";
    return std::make_unique<AndroidQPainterBackend>(this);
}

EglDisplay *AndroidBackend::sceneEglDisplayObject() const
{
    // Will be set by AndroidEglBackend if Mesa is available
    return nullptr;
}

QList<CompositingType> AndroidBackend::supportedCompositors() const
{
    QList<CompositingType> compositors;
    
    // Check if Mesa is available for OpenGL software rendering
    if (AndroidEglBackend::isMesaAvailable()) {
        qInfo() << "Mesa available - supporting OpenGL compositing";
        compositors.append(OpenGLCompositing);
    } else {
        qInfo() << "Mesa not available - OpenGL compositing not supported";
    }
    
    // QPainter is always supported as fallback
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
    lorieEvent e;
    while (read(m_connFd, &e, sizeof(e)) == sizeof(e)) {
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
        
        // Convert Android keycode to Linux keycode if needed
        // Use the android_to_linux_keycode array from termux-render library
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
    Q_EMIT deviceAdded(m_backend->touchDevice());
    Q_EMIT deviceAdded(m_backend->keyboardDevice());
    Q_EMIT deviceAdded(m_backend->pointerDevice());
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
