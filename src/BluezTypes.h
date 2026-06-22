#pragma once

// D-Bus marshalling helper types for the BlueZ ObjectManager API.
//
//   InterfaceList     == a{sa{sv}}      (interface name -> properties)
//   ManagedObjectList == a{oa{sa{sv}}}  (object path    -> interfaces)
//
// These must be registered with qDBusRegisterMetaType<>() before the
// corresponding signals are connected or replies demarshalled.

#include <QMap>
#include <QString>
#include <QVariantMap>
#include <QDBusObjectPath>
#include <QMetaType>

typedef QMap<QString, QVariantMap> InterfaceList;
typedef QMap<QDBusObjectPath, InterfaceList> ManagedObjectList;

Q_DECLARE_METATYPE(InterfaceList)
Q_DECLARE_METATYPE(ManagedObjectList)
