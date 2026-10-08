#pragma once

#include <QObject>
#include <QSystemTrayIcon>

class BluezManager;
class SettingsWindow;
class QMenu;
class QAction;

// Owns the system tray icon and its context menu, and drives the settings
// window. Reacts to BluezManager signals to recolour the icon, refresh the
// "currently connected" header and show a notification when a device drops.
class TrayApp : public QObject
{
    Q_OBJECT
public:
    explicit TrayApp(BluezManager *mgr, QObject *parent = nullptr);

    void show();   // make the tray icon visible

public slots:
    void openSettings();

private slots:
    void onActivated(QSystemTrayIcon::ActivationReason reason);
    void updateState();
    void onDeviceConnected(const QString &path, const QString &name);
    void onDeviceDisconnected(const QString &path, const QString &name, bool expected);
    void recreateTray();   // a tray host appeared after us

private:
    void createTray();

    BluezManager   *m_mgr = nullptr;
    QSystemTrayIcon *m_tray = nullptr;
    QMenu          *m_menu = nullptr;
    QAction        *m_headerAction = nullptr;
    SettingsWindow *m_window = nullptr;
    bool            m_connected = false;
    int             m_iconKey = -3; // shown icon: battery %, -1 unknown, -2 off, -3 none
};
