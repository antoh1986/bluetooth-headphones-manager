#include "TrayApp.h"
#include "BluezManager.h"
#include "SettingsWindow.h"
#include "Icons.h"

#include <QMenu>
#include <QAction>
#include <QApplication>

TrayApp::TrayApp(BluezManager *mgr, QObject *parent)
    : QObject(parent), m_mgr(mgr)
{
    m_window = new SettingsWindow(mgr);

    m_tray = new QSystemTrayIcon(this);
    m_tray->setIcon(Icons::tray(false));
    m_tray->setToolTip(tr("Bluetooth: not connected"));

    m_menu = new QMenu;

    m_headerAction = m_menu->addAction(tr("Not connected"));
    m_headerAction->setEnabled(false);

    m_menu->addSeparator();

    QAction *settingsAction = m_menu->addAction(tr("Settings"));
    connect(settingsAction, &QAction::triggered, this, &TrayApp::openSettings);

    m_menu->addSeparator();

    QAction *quitAction = m_menu->addAction(tr("Quit"));
    connect(quitAction, &QAction::triggered, this, [this]() {
        m_mgr->stopDiscovery();
        qApp->quit();
    });

    m_tray->setContextMenu(m_menu);

    connect(m_tray, &QSystemTrayIcon::activated, this, &TrayApp::onActivated);

    connect(m_mgr, &BluezManager::devicesChanged,     this, &TrayApp::updateState);
    connect(m_mgr, &BluezManager::deviceConnected,    this, &TrayApp::onDeviceConnected);
    connect(m_mgr, &BluezManager::deviceDisconnected, this, &TrayApp::onDeviceDisconnected);
    connect(m_mgr, &BluezManager::batteryChanged, this,
            [this](const QString &, int) { updateState(); });

    updateState();
}

void TrayApp::show()
{
    m_tray->show();
}

void TrayApp::openSettings()
{
    m_window->openAndDiscover();
}

void TrayApp::onActivated(QSystemTrayIcon::ActivationReason reason)
{
    // Left click (Trigger) or double click also opens the settings window.
    if (reason == QSystemTrayIcon::Trigger ||
        reason == QSystemTrayIcon::DoubleClick)
        openSettings();
}

void TrayApp::updateState()
{
    const BtDevice cur = m_mgr->currentConnected();
    const bool connected = cur.isValid();
    m_connected = connected;

    m_tray->setIcon(Icons::tray(connected, cur.battery));

    QString header;
    if (connected) {
        header = cur.displayName();
        if (cur.battery >= 0)
            header += QStringLiteral(" — %1%").arg(cur.battery); // em dash
    } else {
        header = tr("Not connected");
    }
    m_headerAction->setText(header);
    m_tray->setToolTip(connected ? header : tr("Bluetooth: not connected"));
}

void TrayApp::onDeviceConnected(const QString &path, const QString &name)
{
    Q_UNUSED(path);
    updateState();
    m_tray->showMessage(tr("Bluetooth"),
                        tr("%1 connected").arg(name),
                        Icons::tray(true, m_mgr->currentConnected().battery), 4000);
}

void TrayApp::onDeviceDisconnected(const QString &path, const QString &name, bool expected)
{
    Q_UNUSED(path);
    updateState();
    if (!expected) {
        // Unexpected drop (out of range / battery dead): warn the user.
        m_tray->showMessage(tr("Bluetooth"),
                            tr("%1 disconnected").arg(name),
                            QSystemTrayIcon::Warning, 5000);
    }
}
