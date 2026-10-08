#include "BluezManager.h"
#include "BtAgent.h"

#include <QTimer>
#include <QDateTime>
#include <QDebug>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusVariant>
#include <QDBusMetaType>
#include <QDBusServiceWatcher>

#include <algorithm>
#include <utility>

namespace {
const char *DBUS_SERVICE   = "org.freedesktop.DBus";
const char *DBUS_PATH      = "/org/freedesktop/DBus";
const char *BLUEZ          = "org.bluez";
const char *OBJ_MANAGER    = "org.freedesktop.DBus.ObjectManager";
const char *PROPS_IFACE    = "org.freedesktop.DBus.Properties";
const char *ADAPTER_IFACE  = "org.bluez.Adapter1";
const char *DEVICE_IFACE   = "org.bluez.Device1";
const char *BATTERY_IFACE  = "org.bluez.Battery1";
const char *AGENT_MGR_PATH  = "/org/bluez";
const char *AGENT_MGR_IFACE = "org.bluez.AgentManager1";
// Same capability bluetoothctl uses; handles "Just Works" / numeric-comparison
// pairing silently via the auto-accepting BtAgent.
const char *AGENT_CAPABILITY = "KeyboardDisplay";

// BlueZ pairing/connecting can take a while; bump the default 25s timeout.
const int   OP_TIMEOUT_MS  = 45000;
const int   DISCOVERY_MS   = 60000;
} // namespace

BluezManager::BluezManager(QObject *parent)
    : QObject(parent)
{
}

void BluezManager::start()
{
    qDBusRegisterMetaType<InterfaceList>();
    qDBusRegisterMetaType<ManagedObjectList>();

    QDBusConnection bus = QDBusConnection::systemBus();
    if (!bus.isConnected()) {
        qWarning() << "Cannot connect to the system D-Bus";
        return;
    }

    bool ok = true;
    ok &= bus.connect(BLUEZ, "/", OBJ_MANAGER, "InterfacesAdded", this,
                SLOT(onInterfacesAdded(QDBusObjectPath, InterfaceList)));
    ok &= bus.connect(BLUEZ, "/", OBJ_MANAGER, "InterfacesRemoved", this,
                SLOT(onInterfacesRemoved(QDBusObjectPath, QStringList)));
    // Empty path == match PropertiesChanged from every org.bluez object.
    // The originating object path is recovered from the trailing QDBusMessage
    // argument (a QtDBus feature: the raw message is appended to the slot).
    ok &= bus.connect(BLUEZ, QString(), PROPS_IFACE, "PropertiesChanged", this,
                SLOT(onPropertiesChanged(QString, QVariantMap, QStringList, QDBusMessage)));
    if (!ok)
        qWarning() << "Failed to subscribe to one or more BlueZ D-Bus signals";

    m_discoveryTimer = new QTimer(this);
    m_discoveryTimer->setSingleShot(true);
    connect(m_discoveryTimer, &QTimer::timeout, this, &BluezManager::stopDiscovery);

    // Follow bluetoothd coming and going (no adapter at login, USB dongle
    // plugged in later, service restart). BlueZ forgets our agent when it
    // exits, so register it and re-read the objects every time it starts.
    auto *bluez = new QDBusServiceWatcher(BLUEZ, bus,
                                          QDBusServiceWatcher::WatchForOwnerChange, this);
    connect(bluez, &QDBusServiceWatcher::serviceRegistered,
            this, &BluezManager::onBluezStarted);
    connect(bluez, &QDBusServiceWatcher::serviceUnregistered,
            this, &BluezManager::onBluezStopped);

    // Only talk to BlueZ now if it is already running. A call to it otherwise
    // makes D-Bus try to activate bluetoothd, which with no adapter present
    // just times out after 25 s; the watcher catches it starting later.
    QDBusMessage probe = QDBusMessage::createMethodCall(
        DBUS_SERVICE, DBUS_PATH, DBUS_SERVICE, QStringLiteral("NameHasOwner"));
    probe << QString(BLUEZ);
    auto *w = new QDBusPendingCallWatcher(bus.asyncCall(probe), this);
    connect(w, &QDBusPendingCallWatcher::finished, this,
            [this](QDBusPendingCallWatcher *cw) {
        cw->deleteLater();
        QDBusPendingReply<bool> reply = *cw;
        if (reply.isError() || reply.value())
            onBluezStarted();
        else
            qInfo() << "BlueZ is not running; waiting for it to start";
    });
}

void BluezManager::onBluezStarted()
{
    if (m_bluezRunning)
        return; // the startup probe and the watcher can both report it
    m_bluezRunning = true;
    qInfo() << "BlueZ is running";
    registerAgent();
    fetchManagedObjects();
}

void BluezManager::onBluezStopped()
{
    m_bluezRunning = false;
    qWarning() << "BlueZ stopped; waiting for it to come back";

    // Every BlueZ object is gone with the daemon.
    forgetAdapter();
    const QMap<QString, BtDevice> gone = std::exchange(m_devices, {});
    for (const BtDevice &d : gone) {
        if (d.connected)
            emit deviceDisconnected(d.path, d.displayName(),
                                    m_intentionalDisconnect.contains(d.path));
    }
    m_intentionalDisconnect.clear();
    emit devicesChanged();
}

// ---------------------------------------------------------------------------
// Reading state
// ---------------------------------------------------------------------------

void BluezManager::fetchManagedObjects()
{
    QDBusMessage call = QDBusMessage::createMethodCall(
        BLUEZ, "/", OBJ_MANAGER, "GetManagedObjects");
    QDBusPendingCall pending = QDBusConnection::systemBus().asyncCall(call);
    auto *w = new QDBusPendingCallWatcher(pending, this);

    connect(w, &QDBusPendingCallWatcher::finished, this,
            [this](QDBusPendingCallWatcher *cw) {
        cw->deleteLater();
        QDBusPendingReply<ManagedObjectList> reply = *cw;
        if (reply.isError()) {
            qWarning() << "GetManagedObjects failed:" << reply.error().message();
            return;
        }

        const ManagedObjectList objs = reply.value();
        for (auto it = objs.constBegin(); it != objs.constEnd(); ++it) {
            const QString p = it.key().path();
            const InterfaceList &ifaces = it.value();

            if (ifaces.contains(ADAPTER_IFACE))
                handleAdapter(p, ifaces.value(ADAPTER_IFACE));

            if (ifaces.contains(DEVICE_IFACE)) {
                handleDeviceInterface(p, ifaces.value(DEVICE_IFACE));
                if (ifaces.contains(BATTERY_IFACE))
                    m_devices[p].battery =
                        ifaces.value(BATTERY_IFACE).value("Percentage").toInt();
            }
        }
        qInfo() << "Initial scan:" << m_devices.size() << "devices,"
                << (m_adapterPath.isEmpty() ? "no adapter" : qPrintable(m_adapterPath));
        emit devicesChanged();
    });
}

void BluezManager::handleAdapter(const QString &path, const QVariantMap &props)
{
    if (m_adapterPath.isEmpty()) {
        m_adapterPath = path;
        qInfo() << "Using adapter" << path;
        setAdapterPowered(true);
        // Discovery on a still unpowered adapter (just plugged in, bluetoothd
        // just started) fails with NotReady, so report the adapter ready once
        // Powered is true: now, or when PropertiesChanged says so.
        m_adapterReadyPending = !props.value("Powered").toBool();
        if (!m_adapterReadyPending)
            emit adapterReady();
    }
}

void BluezManager::forgetAdapter()
{
    if (m_adapterPath.isEmpty())
        return;
    qInfo() << "Adapter gone:" << m_adapterPath;
    m_adapterPath.clear();
    m_adapterReadyPending = false;
    m_discoveryTimer->stop();
    if (m_discovering) {
        m_discovering = false;
        emit discoveringChanged(false);
    }
}

void BluezManager::handleDeviceInterface(const QString &path, const QVariantMap &props)
{
    BtDevice &d = m_devices[path]; // inserts a default if missing
    d.path = path;
    applyDeviceProps(d, props);
}

void BluezManager::applyDeviceProps(BtDevice &d, const QVariantMap &p)
{
    if (p.contains("Name"))      d.rawName   = p.value("Name").toString();
    if (p.contains("Alias"))     d.alias     = p.value("Alias").toString();
    if (p.contains("Address"))   d.address   = p.value("Address").toString();
    if (p.contains("AddressType")) d.addressType = p.value("AddressType").toString();
    if (p.contains("Paired"))    d.paired    = p.value("Paired").toBool();
    if (p.contains("Trusted"))   d.trusted   = p.value("Trusted").toBool();
    if (p.contains("Connected")) d.connected = p.value("Connected").toBool();
    if (p.contains("RSSI"))    { d.rssi = p.value("RSSI").toInt(); d.hasRssi = true;
                                 d.lastSeenMs = QDateTime::currentMSecsSinceEpoch(); }
    if (p.contains("Icon"))      d.icon      = p.value("Icon").toString();
    if (p.contains("Class"))     d.cls       = p.value("Class").toUInt();
    if (p.contains("UUIDs"))     d.uuids     = p.value("UUIDs").toStringList();
}

QList<BtDevice> BluezManager::devices() const
{
    QList<BtDevice> list = m_devices.values();
    std::sort(list.begin(), list.end(), [](const BtDevice &a, const BtDevice &b) {
        if (a.isAudio() != b.isAudio())
            return a.isAudio();          // audio devices float to the top
        return a.displayName().compare(b.displayName(), Qt::CaseInsensitive) < 0;
    });
    return list;
}

BtDevice BluezManager::currentConnected() const
{
    for (const BtDevice &d : m_devices)
        if (d.connected && d.isAudio())
            return d;
    for (const BtDevice &d : m_devices)
        if (d.connected)
            return d;
    return BtDevice();
}

// ---------------------------------------------------------------------------
// Live signal handling
// ---------------------------------------------------------------------------

void BluezManager::onInterfacesAdded(const QDBusObjectPath &path,
                                     const InterfaceList &ifaces)
{
    const QString p = path.path();
    bool changed = false;

    if (ifaces.contains(ADAPTER_IFACE))
        handleAdapter(p, ifaces.value(ADAPTER_IFACE));

    if (ifaces.contains(DEVICE_IFACE)) {
        handleDeviceInterface(p, ifaces.value(DEVICE_IFACE));
        changed = true;
    }
    if (ifaces.contains(BATTERY_IFACE) && m_devices.contains(p)) {
        const int pct = ifaces.value(BATTERY_IFACE).value("Percentage").toInt();
        m_devices[p].battery = pct;
        emit batteryChanged(p, pct);
        changed = true;
    }

    if (changed)
        emit devicesChanged();
}

void BluezManager::onInterfacesRemoved(const QDBusObjectPath &path,
                                       const QStringList &ifaces)
{
    const QString p = path.path();

    if (ifaces.contains(DEVICE_IFACE)) {
        if (m_devices.contains(p)) {
            const BtDevice d = m_devices.take(p);
            if (d.connected) {
                const bool expected = m_intentionalDisconnect.remove(p);
                emit deviceDisconnected(p, d.displayName(), expected);
            }
            emit devicesChanged();
        }
    } else if (ifaces.contains(BATTERY_IFACE) && m_devices.contains(p)) {
        m_devices[p].battery = -1;
        emit devicesChanged();
    }

    // Our adapter was unplugged (BlueZ has already removed its devices), or
    // bluetoothd is shutting down. handleAdapter() takes the next one that
    // shows up. No re-scan for a second adapter here: calling BlueZ while it
    // exits would just D-Bus-activate it again (Refresh does that re-scan).
    if (ifaces.contains(ADAPTER_IFACE) && p == m_adapterPath)
        forgetAdapter();
}

void BluezManager::onPropertiesChanged(const QString &iface,
                                       const QVariantMap &changed,
                                       const QStringList &invalidated,
                                       const QDBusMessage &msg)
{
    const QString p = msg.path();
    if (p == m_adapterPath) {
        if (m_adapterReadyPending && iface == QLatin1String(ADAPTER_IFACE) &&
            changed.value("Powered").toBool()) {
            m_adapterReadyPending = false;
            emit adapterReady();
        }
        return;
    }
    if (!m_devices.contains(p))
        return;

    if (iface == QLatin1String(DEVICE_IFACE)) {
        BtDevice &d = m_devices[p];
        const bool wasConnected = d.connected;
        applyDeviceProps(d, changed);

        // BlueZ invalidates RSSI when the device stops being seen on air
        // (out of range / discovery ended); reflect that so the UI can tell a
        // present device from a merely-remembered one.
        if (invalidated.contains(QLatin1String("RSSI"))) {
            d.hasRssi = false;
            d.rssi = 0;
        }

        if (changed.contains("Connected")) {
            if (d.connected && !wasConnected) {
                qInfo() << "Connected:" << d.displayName();
                emit deviceConnected(p, d.displayName());
            } else if (!d.connected && wasConnected) {
                const bool expected = m_intentionalDisconnect.remove(p);
                qInfo() << "Disconnected:" << d.displayName()
                        << (expected ? "(expected)" : "(unexpected)");
                // An *unexpected* drop usually means the device powered off
                // (e.g. put back in the case) or went out of range, so a
                // sighting from before the drop no longer proves it is
                // reachable: clear the on-air timestamp to grey it out as
                // Offline at once. Intentional disconnects (device switch /
                // manual Disconnect) leave it powered on, so keep the grace
                // window. A fresh discovery sighting revives presence either way.
                if (!expected) {
                    d.lastSeenMs = 0;
                    d.hasRssi = false;
                    d.rssi = 0;
                }
                emit deviceDisconnected(p, d.displayName(), expected);
            }
        }
        emit devicesChanged();
    } else if (iface == QLatin1String(BATTERY_IFACE)) {
        if (changed.contains("Percentage")) {
            const int pct = changed.value("Percentage").toInt();
            m_devices[p].battery = pct;
            emit batteryChanged(p, pct);
            emit devicesChanged();
        }
    }
}

// ---------------------------------------------------------------------------
// "One click magic"
// ---------------------------------------------------------------------------

void BluezManager::smartConnect(const QString &path)
{
    if (!m_devices.contains(path)) {
        qWarning() << "smartConnect: unknown device" << path;
        return;
    }
    const BtDevice dev = m_devices.value(path);

    if (dev.connected) {
        emit pairingProgress(path, QStringLiteral("Connected"));
        return;
    }

    // Find another currently-connected device to drop first.
    QString other;
    for (auto it = m_devices.constBegin(); it != m_devices.constEnd(); ++it) {
        if (it.key() != path && it.value().connected) {
            other = it.key();
            break;
        }
    }

    if (!other.isEmpty()) {
        qInfo() << "Switching: disconnecting" << m_devices.value(other).displayName()
                << "before connecting" << dev.displayName();
        emit pairingProgress(path, QStringLiteral("Switching device\u2026"));
        m_intentionalDisconnect.insert(other);
        auto *w = callDevice(other, QStringLiteral("Disconnect"));
        connect(w, &QDBusPendingCallWatcher::finished, this,
                [this, path](QDBusPendingCallWatcher *cw) {
            cw->deleteLater();
            beginConnectSequence(path);
        });
    } else {
        beginConnectSequence(path);
    }
}

void BluezManager::beginConnectSequence(const QString &path)
{
    if (!m_devices.contains(path))
        return;
    const BtDevice dev = m_devices.value(path);

    if (!dev.paired) {
        // Make sure the controller is bondable; ordered before Pair on the same
        // connection. With our own default agent answering, this yields a full
        // authenticated bond (like bluetoothctl) rather than a transient link.
        setAdapterPairable(true);
        emit pairingProgress(path, QStringLiteral("Pairing\u2026"));
        auto *w = callDevice(path, QStringLiteral("Pair"), OP_TIMEOUT_MS);
        connect(w, &QDBusPendingCallWatcher::finished, this,
                [this, path](QDBusPendingCallWatcher *cw) {
            cw->deleteLater();
            QDBusPendingReply<> reply = *cw;
            if (reply.isError()) {
                const QString e = humanError(reply.reply());
                qWarning() << "Pair failed for" << path << ":" << e;
                emit pairingProgress(path, QStringLiteral("Failed: ") + e);
                return;
            }
            setTrusted(path, true);
            doConnect(path);
        });
    } else {
        setTrusted(path, true);
        doConnect(path);
    }
}

void BluezManager::doConnect(const QString &path)
{
    emit pairingProgress(path, QStringLiteral("Connecting\u2026"));
    auto *w = callDevice(path, QStringLiteral("Connect"), OP_TIMEOUT_MS);
    connect(w, &QDBusPendingCallWatcher::finished, this,
            [this, path](QDBusPendingCallWatcher *cw) {
        cw->deleteLater();
        QDBusPendingReply<> reply = *cw;
        if (reply.isError()) {
            const QString e = humanError(reply.reply());
            qWarning() << "Connect failed for" << path << ":" << e;
            emit pairingProgress(path, QStringLiteral("Failed: ") + e);
        } else {
            // The actual state flip arrives via PropertiesChanged(Connected);
            // this is just immediate UI feedback.
            emit pairingProgress(path, QStringLiteral("Connected"));
        }
    });
}

void BluezManager::disconnectDevice(const QString &path)
{
    if (!m_devices.contains(path) || !m_devices.value(path).connected)
        return;

    emit pairingProgress(path, QStringLiteral("Disconnecting\u2026"));
    m_intentionalDisconnect.insert(path);
    auto *w = callDevice(path, QStringLiteral("Disconnect"));
    connect(w, &QDBusPendingCallWatcher::finished, this,
            [this, path](QDBusPendingCallWatcher *cw) {
        cw->deleteLater();
        QDBusPendingReply<> reply = *cw;
        if (reply.isError()) {
            m_intentionalDisconnect.remove(path);
            const QString e = humanError(reply.reply());
            qWarning() << "Disconnect failed for" << path << ":" << e;
            emit pairingProgress(path, QStringLiteral("Failed: ") + e);
        }
    });
}

void BluezManager::forgetDevice(const QString &path)
{
    if (!m_devices.contains(path) || m_adapterPath.isEmpty())
        return;

    emit pairingProgress(path, QStringLiteral("Forgetting\u2026"));
    const bool wasConnected = m_devices.value(path).connected;
    if (wasConnected)
        m_intentionalDisconnect.insert(path);

    QDBusMessage msg = QDBusMessage::createMethodCall(
        BLUEZ, m_adapterPath, ADAPTER_IFACE, QStringLiteral("RemoveDevice"));
    msg << QVariant::fromValue(QDBusObjectPath(path));
    QDBusPendingCall call = QDBusConnection::systemBus().asyncCall(msg);
    auto *w = new QDBusPendingCallWatcher(call, this);
    connect(w, &QDBusPendingCallWatcher::finished, this,
            [this, path, wasConnected](QDBusPendingCallWatcher *cw) {
        cw->deleteLater();
        QDBusPendingReply<> reply = *cw;
        if (reply.isError()) {
            if (wasConnected)
                m_intentionalDisconnect.remove(path);
            const QString e = humanError(reply.reply());
            qWarning() << "RemoveDevice failed for" << path << ":" << e;
            emit pairingProgress(path, QStringLiteral("Failed: ") + e);
        } else {
            emit pairingProgress(path, QStringLiteral("Forgotten"));
        }
        // On success BlueZ removes Device1 and InterfacesRemoved updates the
        // device list. No extra state mutation or polling is needed here.
    });
}

void BluezManager::setTrusted(const QString &path, bool trusted)
{
    QDBusMessage msg = QDBusMessage::createMethodCall(
        BLUEZ, path, PROPS_IFACE, QStringLiteral("Set"));
    msg << QString(DEVICE_IFACE) << QString("Trusted")
        << QVariant::fromValue(QDBusVariant(trusted));
    QDBusPendingCall call = QDBusConnection::systemBus().asyncCall(msg);
    auto *w = new QDBusPendingCallWatcher(call, this);
    connect(w, &QDBusPendingCallWatcher::finished, this,
            [path](QDBusPendingCallWatcher *cw) {
        cw->deleteLater();
        QDBusPendingReply<> reply = *cw;
        if (reply.isError())
            qWarning() << "Set Trusted failed for" << path << ":"
                       << reply.error().message();
    });
}

// ---------------------------------------------------------------------------
// Discovery
// ---------------------------------------------------------------------------

void BluezManager::setDiscoveryFilter()
{
    if (m_adapterPath.isEmpty())
        return;

    // Widen discovery so sporadic earbud advertising is caught more reliably:
    //  - Transport "auto"  -> scan both BR/EDR and LE (directed/undirected
    //    reconnect advertising from multipoint earbuds is LE).
    //  - DuplicateData true -> report *every* advertisement instead of
    //    de-duplicating, so each short burst yields an RSSI update (refreshes
    //    lastSeenMs and keeps the device flagged "present").
    QVariantMap filter;
    filter.insert(QStringLiteral("Transport"), QStringLiteral("auto"));
    filter.insert(QStringLiteral("DuplicateData"), true);

    QDBusMessage msg = QDBusMessage::createMethodCall(
        BLUEZ, m_adapterPath, ADAPTER_IFACE, QStringLiteral("SetDiscoveryFilter"));
    msg << QVariant::fromValue(filter);
    QDBusPendingCall call = QDBusConnection::systemBus().asyncCall(msg);
    auto *w = new QDBusPendingCallWatcher(call, this);
    connect(w, &QDBusPendingCallWatcher::finished, this,
            [](QDBusPendingCallWatcher *cw) {
        cw->deleteLater();
        QDBusPendingReply<> reply = *cw;
        if (reply.isError())
            qWarning() << "SetDiscoveryFilter failed:" << reply.error().message();
    });
}

void BluezManager::startDiscovery()
{
    if (m_adapterPath.isEmpty()) {
        qWarning() << "startDiscovery: no adapter yet";
        return;
    }
    setDiscoveryFilter(); // ordered before StartDiscovery on the same connection
    callAdapter(QStringLiteral("StartDiscovery"));
    const bool was = m_discovering;
    m_discovering = true;
    m_discoveryTimer->start(DISCOVERY_MS);
    qInfo() << "Discovery started";
    if (!was)
        emit discoveringChanged(true);
}

void BluezManager::stopDiscovery()
{
    if (m_adapterPath.isEmpty() || !m_discovering)
        return;
    callAdapter(QStringLiteral("StopDiscovery"));
    m_discovering = false;
    m_discoveryTimer->stop();
    qInfo() << "Discovery stopped";
    emit discoveringChanged(false);
}

void BluezManager::refresh()
{
    fetchManagedObjects();
    startDiscovery();
}

void BluezManager::resetAdapter()
{
    if (m_adapterPath.isEmpty()) {
        qWarning() << "resetAdapter: no adapter";
        return;
    }
    qInfo() << "Resetting adapter (power cycle)" << m_adapterPath;

    // A power-off drops every active link; flag those as intentional so the
    // tray does not raise "unexpectedly disconnected" notifications.
    for (auto it = m_devices.constBegin(); it != m_devices.constEnd(); ++it)
        if (it.value().connected)
            m_intentionalDisconnect.insert(it.key());

    if (m_discovering)
        stopDiscovery();

    // Toggle Adapter1.Powered off -> (pause) -> on. This is the D-Bus
    // equivalent of "turn the adapter off and on" and needs no sudo. The short
    // gaps let BlueZ settle between steps; not polling, just sequencing.
    setAdapterPowered(false);
    QTimer::singleShot(1500, this, [this]() {
        setAdapterPowered(true);
        QTimer::singleShot(1200, this, [this]() {
            fetchManagedObjects(); // re-read state once the adapter is back up
        });
    });
}

// ---------------------------------------------------------------------------
// Low level helpers
// ---------------------------------------------------------------------------

QDBusPendingCallWatcher *BluezManager::callDevice(const QString &path,
                                                  const QString &method,
                                                  int timeoutMs)
{
    QDBusMessage msg = QDBusMessage::createMethodCall(BLUEZ, path, DEVICE_IFACE, method);
    QDBusPendingCall call = QDBusConnection::systemBus().asyncCall(msg, timeoutMs);
    return new QDBusPendingCallWatcher(call, this);
}

void BluezManager::callAdapter(const QString &method)
{
    QDBusMessage msg =
        QDBusMessage::createMethodCall(BLUEZ, m_adapterPath, ADAPTER_IFACE, method);
    QDBusPendingCall call = QDBusConnection::systemBus().asyncCall(msg);
    auto *w = new QDBusPendingCallWatcher(call, this);
    connect(w, &QDBusPendingCallWatcher::finished, this,
            [method](QDBusPendingCallWatcher *cw) {
        cw->deleteLater();
        QDBusPendingReply<> reply = *cw;
        if (reply.isError())
            qWarning() << method << "failed:" << reply.error().message();
    });
}

void BluezManager::setAdapterPowered(bool on)
{
    QDBusMessage msg = QDBusMessage::createMethodCall(
        BLUEZ, m_adapterPath, PROPS_IFACE, QStringLiteral("Set"));
    msg << QString(ADAPTER_IFACE) << QString("Powered")
        << QVariant::fromValue(QDBusVariant(on));
    QDBusPendingCall call = QDBusConnection::systemBus().asyncCall(msg);
    auto *w = new QDBusPendingCallWatcher(call, this);
    connect(w, &QDBusPendingCallWatcher::finished, this,
            [](QDBusPendingCallWatcher *cw) { cw->deleteLater(); });
}

void BluezManager::setAdapterPairable(bool pairable)
{
    if (m_adapterPath.isEmpty())
        return;
    QDBusMessage msg = QDBusMessage::createMethodCall(
        BLUEZ, m_adapterPath, PROPS_IFACE, QStringLiteral("Set"));
    msg << QString(ADAPTER_IFACE) << QString("Pairable")
        << QVariant::fromValue(QDBusVariant(pairable));
    QDBusPendingCall call = QDBusConnection::systemBus().asyncCall(msg);
    auto *w = new QDBusPendingCallWatcher(call, this);
    connect(w, &QDBusPendingCallWatcher::finished, this,
            [](QDBusPendingCallWatcher *cw) {
        cw->deleteLater();
        QDBusPendingReply<> reply = *cw;
        if (reply.isError())
            qWarning() << "Set Pairable failed:" << reply.error().message();
    });
}

QString BluezManager::humanError(const QDBusMessage &reply)
{
    // reply is an error message; build a short, user friendly string.
    QString name  = reply.errorName();
    QString text  = reply.errorMessage();
    if (text.isEmpty())
        text = name.section('.', -1); // last component of the error name
    return text;
}

// ---------------------------------------------------------------------------
// Pairing agent
// ---------------------------------------------------------------------------

// Expose our own org.bluez.Agent1 and make it the default agent. This is what
// lets app-initiated Pair() complete a real authenticated bond silently --
// the same effect as bluetoothctl's agent answering "yes" -- instead of
// depending on the Cinnamon system agent (which does not reliably engage for
// programmatic pairing). A full bond is mutual, so the earbuds also store this
// host and directed-advertise to it for auto-reconnect on power-on.
//
// No sudo is needed: registering an agent for the active session is permitted
// by polkit. On process exit the D-Bus name drops and BlueZ auto-removes the
// agent, so no explicit UnregisterAgent is required. Called again each time
// bluetoothd (re)starts, as a new daemon knows no agents.
void BluezManager::registerAgent()
{
    QDBusConnection bus = QDBusConnection::systemBus();

    // Host object that carries the BtAgent adaptor; exported once, under
    // default options (ExportAdaptors) so the org.bluez.Agent1 interface is
    // published.
    if (!m_agentObject) {
        m_agentObject = new QObject(this);
        new BtAgent(m_agentObject);
        if (!bus.registerObject(QString::fromLatin1(BtAgent::objectPath()), m_agentObject)) {
            qWarning() << "Failed to export pairing agent at" << BtAgent::objectPath();
            delete m_agentObject;
            m_agentObject = nullptr;
            return;
        }
    }

    QDBusMessage reg = QDBusMessage::createMethodCall(
        BLUEZ, AGENT_MGR_PATH, AGENT_MGR_IFACE, QStringLiteral("RegisterAgent"));
    reg << QVariant::fromValue(QDBusObjectPath(BtAgent::objectPath()))
        << QString(AGENT_CAPABILITY);
    auto *w = new QDBusPendingCallWatcher(bus.asyncCall(reg), this);
    connect(w, &QDBusPendingCallWatcher::finished, this,
            [this](QDBusPendingCallWatcher *cw) {
        cw->deleteLater();
        QDBusPendingReply<> reply = *cw;
        if (reply.isError()) {
            qWarning() << "RegisterAgent failed:" << reply.error().message();
            return;
        }
        qInfo() << "Pairing agent registered at" << BtAgent::objectPath();

        // Become the default agent so BlueZ routes pairing authorization to us.
        QDBusMessage def = QDBusMessage::createMethodCall(
            BLUEZ, AGENT_MGR_PATH, AGENT_MGR_IFACE, QStringLiteral("RequestDefaultAgent"));
        def << QVariant::fromValue(QDBusObjectPath(BtAgent::objectPath()));
        auto *dw = new QDBusPendingCallWatcher(
            QDBusConnection::systemBus().asyncCall(def), this);
        connect(dw, &QDBusPendingCallWatcher::finished, this,
                [](QDBusPendingCallWatcher *dcw) {
            dcw->deleteLater();
            QDBusPendingReply<> dr = *dcw;
            if (dr.isError())
                qWarning() << "RequestDefaultAgent failed:" << dr.error().message();
            else
                qInfo() << "Now the default Bluetooth pairing agent";
        });
    });
}
