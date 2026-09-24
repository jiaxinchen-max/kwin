/*
    SPDX-FileCopyrightText: 2026 Termux Community
    SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL
*/
#pragma once

#include "core/graphicsbuffer.h"
#include "qwayland-server-termux-ahardware-buffer-v1.h"

#include <QObject>

struct AHardwareBuffer;

namespace KWin
{

class Display;

class AndroidHardwareBufferManagerV1 : public QObject, private QtWaylandServer::termux_ahardware_buffer_manager_v1
{
public:
    explicit AndroidHardwareBufferManagerV1(Display *display, QObject *parent);

private:
    void termux_ahardware_buffer_manager_v1_destroy(Resource *resource) override;
    void termux_ahardware_buffer_manager_v1_create_buffer(Resource *resource, uint32_t id, int32_t socketFd, uint32_t flags) override;
    void termux_ahardware_buffer_manager_v1_probe_buffer(Resource *resource, int32_t socketFd, uint32_t token) override;
};

class AndroidHardwareClientBuffer : public GraphicsBuffer
{
    Q_OBJECT

public:
    AndroidHardwareClientBuffer(AHardwareBuffer *buffer, wl_client *client, uint32_t id, uint32_t flags);
    ~AndroidHardwareClientBuffer() override;

    QSize size() const override;
    bool hasAlphaChannel() const override;
    const AndroidHardwareBufferAttributes *androidHardwareBufferAttributes() const override;

    static AndroidHardwareClientBuffer *get(wl_resource *resource);

private:
    static void buffer_destroy_resource(wl_resource *resource);
    static void buffer_destroy(wl_client *client, wl_resource *resource);
    static const struct wl_buffer_interface implementation;

    AHardwareBuffer *m_buffer;
    AndroidHardwareBufferAttributes m_attributes;
    QSize m_size;
    wl_resource *m_resource;
};

}
