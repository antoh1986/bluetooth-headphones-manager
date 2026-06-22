#pragma once

#include <QString>
#include <QStringList>
#include <QtGlobal>

// Plain value object describing a single org.bluez.Device1 (plus optional
// org.bluez.Battery1) as seen on the bus. Copyable; UI works on snapshots.
struct BtDevice {
    QString path;          // D-Bus object path, e.g. /org/bluez/hci0/dev_xx_..
    QString alias;         // Device1.Alias (user friendly, preferred for display)
    QString rawName;       // Device1.Name
    QString address;       // Device1.Address
    QString addressType;   // Device1.AddressType ("public" or "random")
    bool    paired    = false;
    bool    trusted   = false;
    bool    connected = false;
    bool    hasRssi   = false;
    int     rssi      = 0;
    qint64  lastSeenMs = 0; // epoch ms of the last on-air sighting (RSSI), 0 = never
    QString icon;          // Device1.Icon (freedesktop icon name hint)
    quint32 cls       = 0; // Device1.Class (Bluetooth class of device)
    QStringList uuids;     // Device1.UUIDs (supported profiles)
    int     battery   = -1; // Battery1.Percentage, -1 when unknown

    bool isValid() const { return !path.isEmpty(); }

    QString displayName() const {
        if (!alias.isEmpty())   return alias;
        if (!rawName.isEmpty()) return rawName;
        if (!address.isEmpty()) return address;
        return QStringLiteral("Unknown device");
    }

    // Heuristic: is this an audio device (headset/headphones/speaker)?
    // Checked via the freedesktop icon hint, the BT class major device
    // field (Audio/Video == 0x04) and well-known audio profile UUIDs.
    bool isAudio() const {
        const QString ic = icon.toLower();
        if (ic.contains("audio") || ic.contains("headset") ||
            ic.contains("headphone") || ic.contains("speaker") ||
            ic.contains("hifi") || ic.contains("portable-audio"))
            return true;

        if (((cls >> 8) & 0x1F) == 0x04) // major device class: Audio/Video
            return true;

        for (const QString &u : uuids) {
            const QString s = u.toLower();
            if (s.startsWith(QLatin1String("0000110a")) || // Audio Source
                s.startsWith(QLatin1String("0000110b")) || // Audio Sink (A2DP)
                s.startsWith(QLatin1String("0000110d")) || // Advanced Audio
                s.startsWith(QLatin1String("00001108")) || // Headset
                s.startsWith(QLatin1String("0000111e")) || // Handsfree
                s.startsWith(QLatin1String("00001131")))   // Headset AG
                return true;
        }
        return false;
    }
};
