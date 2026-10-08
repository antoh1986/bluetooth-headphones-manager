#pragma once

#include <QObject>
#include <QMap>
#include <QSet>
#include <QList>
#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <QDBusObjectPath>
#include <QDBusMessage>

#include "BtDevice.h"
#include "BluezTypes.h"

class QTimer;
class QDBusPendingCallWatcher;

// Thin asynchronous wrapper around BlueZ (org.bluez) over the system bus.
//
// The UI only ever talks to BlueZ through this class' signals and slots;
// every D-Bus call is non-blocking (QDBusPendingCallWatcher based) so the
// GUI thread never stalls. Device state is tracked live through
// InterfacesAdded / InterfacesRemoved / PropertiesChanged signals -- no
// polling.
class BluezManager : public QObject
{
    Q_OBJECT
public:
    explicit BluezManager(QObject *parent = nullptr);

    void start();                       // register types, hook signals, scan

    QList<BtDevice> devices() const;    // snapshot, sorted audio-first
    BtDevice currentConnected() const;  // active device (audio preferred)
    bool hasAdapter() const { return !m_adapterPath.isEmpty(); }
    bool isDiscovering() const { return m_discovering; }

public slots:
    // "One click magic": pair (if needed) -> trust -> connect, after first
    // disconnecting any other currently connected device.
    void smartConnect(const QString &path);
    void disconnectDevice(const QString &path);
    void forgetDevice(const QString &path);

    void startDiscovery();
    void stopDiscovery();
    void refresh();                     // re-scan managed objects + discover
    void resetAdapter();                // power-cycle adapter (multipoint unstick)

signals:
    void devicesChanged();
    void deviceConnected(const QString &path, const QString &name);
    void deviceDisconnected(const QString &path, const QString &name, bool expected);
    void batteryChanged(const QString &path, int percentage);
    void pairingProgress(const QString &path, const QString &status);
    void adapterReady();
    void discoveringChanged(bool active);

private slots:
    void onInterfacesAdded(const QDBusObjectPath &path, const InterfaceList &ifaces);
    void onInterfacesRemoved(const QDBusObjectPath &path, const QStringList &ifaces);
    void onPropertiesChanged(const QString &iface, const QVariantMap &changed,
                             const QStringList &invalidated, const QDBusMessage &msg);
    void onBluezStarted();              // org.bluez got an owner (bluetoothd up)
    void onBluezStopped();              // ... and lost it again

private:
    void fetchManagedObjects();
    void handleAdapter(const QString &path, const QVariantMap &props);
    void forgetAdapter();               // adapter unplugged / bluetoothd gone
    void handleDeviceInterface(const QString &path, const QVariantMap &props);
    static void applyDeviceProps(BtDevice &d, const QVariantMap &props);

    void registerAgent();               // own org.bluez.Agent1 as default agent
    void beginConnectSequence(const QString &path);
    void doConnect(const QString &path);
    void setTrusted(const QString &path, bool trusted);
    void setAdapterPairable(bool pairable);

    QDBusPendingCallWatcher *callDevice(const QString &path, const QString &method,
                                        int timeoutMs = -1);
    void callAdapter(const QString &method);
    void setAdapterPowered(bool on);
    void setDiscoveryFilter();          // widen scan to catch directed adv

    static QString humanError(const QDBusMessage &reply);

    bool                  m_bluezRunning = false;
    QString               m_adapterPath;
    bool                  m_adapterReadyPending = false; // adapterReady once Powered
    QObject              *m_agentObject = nullptr; // host for the BtAgent adaptor
    QMap<QString, BtDevice> m_devices;        // keyed by object path
    QSet<QString>         m_intentionalDisconnect; // suppress "lost" notify
    bool                  m_discovering = false;
    QTimer               *m_discoveryTimer = nullptr;
};
