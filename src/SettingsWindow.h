#pragma once

#include <QWidget>
#include <QHash>
#include <QString>

#include "BtDevice.h"

class BluezManager;
class BusyIndicator;
class QListWidget;
class QListWidgetItem;
class QPushButton;
class QCheckBox;
class QLabel;

// The settings window: two device lists (paired / available), a Refresh
// button and the "Launch on startup" toggle. Opening it starts discovery,
// closing it stops discovery (to save power / airtime).
class SettingsWindow : public QWidget
{
    Q_OBJECT
public:
    explicit SettingsWindow(BluezManager *mgr, QWidget *parent = nullptr);

    void openAndDiscover();   // show + raise + start discovery

public slots:
    void refreshLists();
    void onPairingProgress(const QString &path, const QString &status);

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    void onDeviceClicked(QListWidgetItem *item);
    void onRefreshClicked();
    void onResetAdapterClicked();
    void onLaunchToggled(bool on);

private:
    QWidget *makeRow(const BtDevice &d, const QString &progress);

    BluezManager *m_mgr = nullptr;
    QListWidget  *m_paired = nullptr;
    QListWidget  *m_available = nullptr;
    QPushButton  *m_refresh = nullptr;
    QPushButton  *m_resetAdapter = nullptr;
    BusyIndicator *m_spinner = nullptr;
    QCheckBox    *m_launch = nullptr;
    QLabel       *m_status = nullptr;

    // Transient per-device progress text (Pairing.../Connecting.../Failed).
    QHash<QString, QString> m_progress;
};
