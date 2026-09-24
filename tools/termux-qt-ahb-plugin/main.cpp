/* SPDX-License-Identifier: LGPL-3.0-only */
#include "termuxahardwarebuffer.h"

#include <QtWaylandClient/private/qwaylandclientbufferintegrationplugin_p.h>

QT_BEGIN_NAMESPACE

namespace QtWaylandClient {

class TermuxAHardwareBufferPlugin final : public QWaylandClientBufferIntegrationPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID QWaylandClientBufferIntegrationFactoryInterface_iid FILE "termux-ahardware-buffer.json")

public:
    QWaylandClientBufferIntegration *create(const QString &, const QStringList &) override
    {
        return new TermuxAHardwareBufferIntegration;
    }
};

}

QT_END_NAMESPACE

#include "main.moc"
