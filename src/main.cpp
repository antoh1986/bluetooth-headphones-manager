#include <QApplication>
#include <QLocalServer>
#include <QLocalSocket>
#include <QMessageBox>
#include <QSystemTrayIcon>
#include <QDebug>

#include "Logger.h"
#include "Icons.h"
#include "BluezManager.h"
#include "AudioManager.h"
#include "ThemeWatcher.h"
#include "TrayApp.h"

namespace {
const char *kServerName = "bluetooth-headphones-manager-singleinstance";

// Returns true if another instance is already running (and was asked to show
// its window). Otherwise sets up the local server for future instances.
bool handleSingleInstance(QLocalServer &server)
{
    QLocalSocket probe;
    probe.connectToServer(kServerName);
    if (probe.waitForConnected(200)) {
        probe.write("show");
        probe.flush();
        probe.waitForBytesWritten(200);
        probe.disconnectFromServer();
        return true;
    }

    // Stale socket left over from a crash? Remove and (re)listen.
    QLocalServer::removeServer(kServerName);
    if (!server.listen(kServerName))
        qWarning() << "Could not start single-instance server:" << server.errorString();
    return false;
}
} // namespace

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setOrganizationName(QStringLiteral("bluetooth-headphones-manager"));
    app.setApplicationName(QStringLiteral("bluetooth-headphones-manager"));
    app.setApplicationDisplayName(QStringLiteral("Bluetooth Headphones Manager"));
    app.setApplicationVersion(QStringLiteral(BLUETOOTH_HEADPHONES_MANAGER_VERSION));
    app.setWindowIcon(Icons::app());
    app.setQuitOnLastWindowClosed(false); // keep running in the tray

    Logger::init();
    qInfo() << "bluetooth-headphones-manager" << app.applicationVersion()
            << "starting; log file:" << Logger::logFilePath();

    QLocalServer server;
    if (handleSingleInstance(server)) {
        qInfo() << "Another instance is already running; focusing it and exiting";
        return 0;
    }

    ThemeWatcher theme;
    theme.start();

    const QStringList args = QApplication::arguments();
    const bool minimized = args.contains(QStringLiteral("--minimized")) ||
                           args.contains(QStringLiteral("--tray"));

    if (!QSystemTrayIcon::isSystemTrayAvailable())
        qWarning() << "System tray is not available in this environment";

    BluezManager manager;
    AudioManager audio;
    TrayApp tray(&manager, &audio);
    tray.show();
    manager.start();
    audio.start();

    // A second invocation connects to our server -> bring up the window.
    QObject::connect(&server, &QLocalServer::newConnection, [&]() {
        QLocalSocket *conn = server.nextPendingConnection();
        if (conn)
            conn->deleteLater();
        tray.openSettings();
    });

    if (!minimized)
        tray.openSettings();

    return app.exec();
}
