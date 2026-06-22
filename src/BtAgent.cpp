#include "BtAgent.h"

#include <QDebug>

// Every method accepts: returning normally (and, where a value is expected,
// returning a sane default) tells BlueZ to proceed with the bond. To reject we
// would sendErrorReply("org.bluez.Error.Rejected"); we never do.

BtAgent::BtAgent(QObject *parent)
    : QDBusAbstractAdaptor(parent)
{
}

void BtAgent::Release()
{
    // BlueZ calls this when it unregisters the agent (e.g. another process took
    // over as default agent, or the adapter went away). Nothing to clean up.
    qInfo() << "BtAgent: Release";
}

QString BtAgent::RequestPinCode(const QDBusObjectPath &device)
{
    qInfo() << "BtAgent: RequestPinCode" << device.path() << "-> 0000";
    return QStringLiteral("0000");
}

void BtAgent::DisplayPinCode(const QDBusObjectPath &device, const QString &pincode)
{
    qInfo() << "BtAgent: DisplayPinCode" << device.path() << pincode;
}

quint32 BtAgent::RequestPasskey(const QDBusObjectPath &device)
{
    qInfo() << "BtAgent: RequestPasskey" << device.path() << "-> 0";
    return 0;
}

void BtAgent::DisplayPasskey(const QDBusObjectPath &device, quint32 passkey, quint16 entered)
{
    qInfo() << "BtAgent: DisplayPasskey" << device.path() << passkey << entered;
}

void BtAgent::RequestConfirmation(const QDBusObjectPath &device, quint32 passkey)
{
    // The "yes" that bluetoothctl asks for -- auto-confirmed here.
    qInfo() << "BtAgent: RequestConfirmation" << device.path() << passkey << "-> accept";
}

void BtAgent::RequestAuthorization(const QDBusObjectPath &device)
{
    qInfo() << "BtAgent: RequestAuthorization" << device.path() << "-> accept";
}

void BtAgent::AuthorizeService(const QDBusObjectPath &device, const QString &uuid)
{
    // Auto-authorize profiles (A2DP/AVRCP/HFP) so connecting never stalls on a
    // "allow this service?" prompt.
    qInfo() << "BtAgent: AuthorizeService" << device.path() << uuid << "-> accept";
}

void BtAgent::Cancel()
{
    qInfo() << "BtAgent: Cancel (request aborted by BlueZ)";
}
