#pragma once

#include <QDBusAbstractAdaptor>
#include <QDBusObjectPath>
#include <QString>

// Auto-accepting BlueZ pairing agent (org.bluez.Agent1).
//
// Registered by BluezManager as the *default* agent so that app-initiated
// Device1.Pair() completes a full, authenticated SSP bond silently -- exactly
// what bluetoothctl does when its own agent prompts you and you answer "yes".
// Without an own agent the app relied on the Cinnamon system agent, which does
// not reliably engage for programmatic pairing, so the bond stayed incomplete
// (audio worked once but the earbuds never stored Linux for directed-advertising
// auto-reconnect). Every callback here simply accepts; we never reject.
//
// This is a QDBusAbstractAdaptor: it is attached to a plain host QObject which
// BluezManager exports on the system bus, and it presents the org.bluez.Agent1
// interface on that object path.
class BtAgent : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.bluez.Agent1")
public:
    explicit BtAgent(QObject *parent);

    // D-Bus object path we register the agent at. Hyphens are illegal in D-Bus
    // object-path elements, so this is the de-hyphenated form of the canonical
    // identifier rather than "bluetooth-headphones-manager".
    static const char *objectPath() { return "/bluetoothheadphonesmanager/agent"; }

public slots:
    void    Release();
    QString RequestPinCode(const QDBusObjectPath &device);
    void    DisplayPinCode(const QDBusObjectPath &device, const QString &pincode);
    quint32 RequestPasskey(const QDBusObjectPath &device);
    void    DisplayPasskey(const QDBusObjectPath &device, quint32 passkey, quint16 entered);
    void    RequestConfirmation(const QDBusObjectPath &device, quint32 passkey);
    void    RequestAuthorization(const QDBusObjectPath &device);
    void    AuthorizeService(const QDBusObjectPath &device, const QString &uuid);
    void    Cancel();
};
