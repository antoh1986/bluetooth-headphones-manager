#include "AutoStart.h"

#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QDebug>
#include <QCoreApplication>
#include <QStandardPaths>

namespace {
const char *kDesktopName = "bluetooth-headphones-manager.desktop";

// Per-user autostart directory (GenericConfigLocation == ~/.config).
QString userAutostartDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
           + QStringLiteral("/autostart");
}

QString userEntryPath()
{
    return userAutostartDir() + QLatin1Char('/') + QLatin1String(kDesktopName);
}

// System-wide autostart entry shipped by the .deb. Must share the basename of
// the per-user entry so the latter can override it.
QString systemEntryPath()
{
    return QStringLiteral("/etc/xdg/autostart/") + QLatin1String(kDesktopName);
}

bool systemEntryExists()
{
    return QFile::exists(systemEntryPath());
}

// True if the per-user entry exists and disables autostart, i.e. carries
// Hidden=true or X-GNOME-Autostart-enabled=false.
bool userEntryDisables()
{
    QFile f(userEntryPath());
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return false;

    QTextStream ts(&f);
    while (!ts.atEnd()) {
        const QString line = ts.readLine().trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
            continue;
        const QString key = line.section(QLatin1Char('='), 0, 0).trimmed().toLower();
        const QString val = line.section(QLatin1Char('='), 1).trimmed().toLower();
        if (key == QLatin1String("hidden") && val == QLatin1String("true"))
            return true;
        if (key == QLatin1String("x-gnome-autostart-enabled") &&
            val == QLatin1String("false"))
            return true;
    }
    return false;
}

// Writes the per-user entry. When `enabled` is false the entry only acts as a
// suppressor for the system-wide entry (Hidden=true). When true it is a fully
// formed autostart entry pointing at the running binary (used when there is no
// system entry, e.g. running from a build tree).
bool writeUserEntry(bool enabled)
{
    QDir().mkpath(userAutostartDir());

    QFile f(userEntryPath());
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        qWarning() << "Failed to write autostart entry" << userEntryPath();
        return false;
    }

    const QString exec = QCoreApplication::applicationFilePath();
    QTextStream ts(&f);
    ts << "[Desktop Entry]\n"
       << "Type=Application\n"
       << "Name=Bluetooth Headphones Manager\n"
       << "Comment=One-click Bluetooth audio device manager for Linux\n"
       << "Exec=" << exec << " --minimized\n"
       << "Icon=bluetooth-headphones-manager\n"
       << "Terminal=false\n"
       << "X-LXQt-Need-Tray=true\n" // lxqt-session: start once the tray is up
       << "X-GNOME-Autostart-enabled=" << (enabled ? "true" : "false") << "\n"
       << "Hidden=" << (enabled ? "false" : "true") << "\n";
    ts.flush();
    f.close();
    return true;
}

bool removeUserEntry()
{
    const QString path = userEntryPath();
    if (!QFile::exists(path))
        return true;
    if (!QFile::remove(path)) {
        qWarning() << "Failed to remove autostart entry" << path;
        return false;
    }
    return true;
}
} // namespace

namespace AutoStart {

QString desktopFilePath()
{
    return userEntryPath();
}

bool isEnabled()
{
    // A per-user entry, if present, takes precedence over the system entry.
    if (QFile::exists(userEntryPath()))
        return !userEntryDisables();
    // Otherwise the system-wide entry (shipped by the .deb) governs the state.
    return systemEntryExists();
}

void setEnabled(bool enabled)
{
    if (enabled) {
        if (systemEntryExists()) {
            // Default-on already comes from the system entry; just drop any
            // per-user suppressor.
            if (removeUserEntry())
                qInfo() << "Autostart enabled (system entry active)";
        } else if (writeUserEntry(true)) {
            // No system entry (e.g. build tree): create a per-user entry.
            qInfo() << "Autostart enabled ->" << userEntryPath();
        }
        return;
    }

    if (systemEntryExists()) {
        // Suppress the system entry with a per-user override.
        if (writeUserEntry(false))
            qInfo() << "Autostart disabled (override ->" << userEntryPath() << ")";
    } else if (removeUserEntry()) {
        qInfo() << "Autostart disabled";
    }
}

} // namespace AutoStart
