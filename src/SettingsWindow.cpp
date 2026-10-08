#include "SettingsWindow.h"
#include "BluezManager.h"
#include "AudioManager.h"
#include "AutoStart.h"
#include "BusyIndicator.h"
#include "Icons.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPushButton>
#include <QToolButton>
#include <QMenu>
#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QDialog>
#include <QDialogButtonBox>
#include <QLabel>
#include <QLineEdit>
#include <QFont>
#include <QFontDatabase>
#include <QFormLayout>
#include <QPlainTextEdit>
#include <QCloseEvent>
#include <QCoreApplication>
#include <QDateTime>
#include <QTimer>

namespace {

QLineEdit *selectableField(const QString &value, QWidget *parent)
{
    auto *field = new QLineEdit(value, parent);
    field->setReadOnly(true);
    field->setCursorPosition(0);
    field->setToolTip(QObject::tr("Select the value and press Ctrl+C to copy it"));
    return field;
}

QString availableValue(const QString &value)
{
    return value.isEmpty() ? QObject::tr("Not available") : value;
}

// Shown next to an audio profile so the best-sounding one stands out.
QString qualityLabel(const AudioProfile &p)
{
    switch (p.quality) {
    case AudioProfile::Quality::Best:
        return QObject::tr("Best quality");
    case AudioProfile::Quality::Medium:
        return QObject::tr("Medium quality");
    case AudioProfile::Quality::Low:
        return p.hasInput ? QObject::tr("Low quality, with microphone")
                          : QObject::tr("Low quality");
    case AudioProfile::Quality::None:
        break;
    }
    return QString();
}

class DeviceInfoDialog final : public QDialog
{
public:
    explicit DeviceInfoDialog(const BtDevice &device, QWidget *parent = nullptr)
        : QDialog(parent)
    {
        setWindowTitle(tr("Device information"));
        setWindowIcon(Icons::app());
        setMinimumWidth(480);

        auto *root = new QVBoxLayout(this);

        auto *title = new QLabel(device.displayName(), this);
        QFont titleFont = title->font();
        titleFont.setBold(true);
        titleFont.setPointSize(titleFont.pointSize() + 2);
        title->setFont(titleFont);
        title->setTextInteractionFlags(Qt::TextSelectableByMouse |
                                       Qt::TextSelectableByKeyboard);
        root->addWidget(title);

        auto *form = new QFormLayout;
        form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

        form->addRow(tr("Display name:"), selectableField(device.displayName(), this));
        form->addRow(tr("Device name:"),
                     selectableField(availableValue(device.rawName), this));
        form->addRow(tr("Alias:"), selectableField(availableValue(device.alias), this));

        const QString mac = availableValue(device.address.toUpper());
        auto *macRow = new QWidget(this);
        auto *macLayout = new QHBoxLayout(macRow);
        macLayout->setContentsMargins(0, 0, 0, 0);
        macLayout->setSpacing(6);
        auto *macField = selectableField(mac, macRow);
        macField->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
        auto *copyMac = new QPushButton(tr("Copy"), macRow);
        copyMac->setEnabled(!device.address.isEmpty());
        copyMac->setToolTip(tr("Copy MAC address to the clipboard"));
        macLayout->addWidget(macField, 1);
        macLayout->addWidget(copyMac);
        form->addRow(tr("MAC address:"), macRow);

        QString addressType;
        if (device.addressType == QLatin1String("public"))
            addressType = tr("Public");
        else if (device.addressType == QLatin1String("random"))
            addressType = tr("Random");
        else
            addressType = availableValue(device.addressType);
        form->addRow(tr("Address type:"), selectableField(addressType, this));
        form->addRow(tr("Device type:"),
                     selectableField(device.isAudio() ? tr("Audio") : tr("Other"), this));
        form->addRow(tr("Connection:"),
                     selectableField(device.connected ? tr("Connected")
                                                       : tr("Disconnected"), this));
        form->addRow(tr("Paired:"), selectableField(device.paired ? tr("Yes") : tr("No"), this));
        form->addRow(tr("Trusted:"), selectableField(device.trusted ? tr("Yes") : tr("No"), this));
        form->addRow(tr("Battery:"),
                     selectableField(device.battery >= 0
                                         ? tr("%1%").arg(device.battery)
                                         : tr("Not available"),
                                     this));
        form->addRow(tr("Signal strength:"),
                     selectableField(device.hasRssi
                                         ? tr("%1 dBm").arg(device.rssi)
                                         : tr("Not available"),
                                     this));
        form->addRow(tr("Bluetooth class:"),
                     selectableField(device.cls != 0
                                         ? QStringLiteral("0x%1").arg(device.cls, 6, 16,
                                                                       QLatin1Char('0')).toUpper()
                                         : tr("Not available"),
                                     this));

        auto *profiles = new QPlainTextEdit(this);
        profiles->setReadOnly(true);
        profiles->setPlainText(device.uuids.isEmpty()
                                   ? tr("Not available")
                                   : device.uuids.join(QLatin1Char('\n')));
        profiles->setMaximumHeight(110);
        profiles->setToolTip(tr("Select the value and press Ctrl+C to copy it"));
        form->addRow(tr("Profile UUIDs:"), profiles);
        root->addLayout(form);

        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
        connect(copyMac, &QPushButton::clicked, this, [copyMac, address = device.address]() {
            QClipboard *clipboard = QApplication::clipboard();
            clipboard->setText(address.toUpper(), QClipboard::Clipboard);
            if (clipboard->supportsSelection())
                clipboard->setText(address.toUpper(), QClipboard::Selection);
            copyMac->setText(QObject::tr("Copied"));
            QTimer::singleShot(1200, copyMac, [copyMac]() {
                copyMac->setText(QObject::tr("Copy"));
            });
        });
        root->addWidget(buttons);
    }
};

} // namespace

SettingsWindow::SettingsWindow(BluezManager *mgr, AudioManager *audio, QWidget *parent)
    : QWidget(parent), m_mgr(mgr), m_audio(audio)
{
    setWindowTitle(tr("Bluetooth Headphones Manager"));
    setWindowIcon(Icons::app());
    resize(440, 600);

    auto *root = new QVBoxLayout(this);

    // -- Paired devices --------------------------------------------------
    auto *pairedBox = new QGroupBox(tr("Paired devices"), this);
    auto *pairedLay = new QVBoxLayout(pairedBox);
    m_paired = new QListWidget(pairedBox);
    m_paired->setSelectionMode(QAbstractItemView::NoSelection);
    m_paired->setFocusPolicy(Qt::NoFocus);
    pairedLay->addWidget(m_paired);
    root->addWidget(pairedBox, 1);

    // -- Available devices ----------------------------------------------
    auto *availBox = new QGroupBox(tr("Available / Saved devices"), this);
    auto *availLay = new QVBoxLayout(availBox);
    m_available = new QListWidget(availBox);
    m_available->setSelectionMode(QAbstractItemView::NoSelection);
    m_available->setFocusPolicy(Qt::NoFocus);
    availLay->addWidget(m_available);
    root->addWidget(availBox, 1);

    // -- Controls --------------------------------------------------------
    auto *controls = new QHBoxLayout;
    m_refresh = new QPushButton(tr("Refresh"), this);
    m_resetAdapter = new QPushButton(tr("Reset adapter"), this);
    m_resetAdapter->setToolTip(
        tr("Power-cycle the Bluetooth adapter — fixes earbuds stuck on another "
           "device (multipoint)"));
    m_spinner = new BusyIndicator(this);
    m_launch  = new QCheckBox(tr("Launch on startup"), this);
    m_launch->setChecked(AutoStart::isEnabled());
    controls->addWidget(m_refresh);
    controls->addWidget(m_resetAdapter);
    controls->addWidget(m_spinner);
    controls->addStretch();
    controls->addWidget(m_launch);
    root->addLayout(controls);

    auto *footer = new QHBoxLayout;
    m_status = new QLabel(this);
    m_status->setStyleSheet(QStringLiteral("color: gray;"));
    auto *version = new QLabel(
        tr("Version %1").arg(QCoreApplication::applicationVersion()), this);
    version->setStyleSheet(QStringLiteral("color: gray;"));
    footer->addWidget(m_status);
    footer->addStretch();
    footer->addWidget(version);
    root->addLayout(footer);

    // -- Wiring ----------------------------------------------------------
    connect(m_refresh, &QPushButton::clicked, this, &SettingsWindow::onRefreshClicked);
    connect(m_resetAdapter, &QPushButton::clicked, this, &SettingsWindow::onResetAdapterClicked);
    connect(m_launch,  &QCheckBox::toggled,   this, &SettingsWindow::onLaunchToggled);
    connect(m_paired,    &QListWidget::itemClicked, this, &SettingsWindow::onDeviceClicked);
    connect(m_available, &QListWidget::itemClicked, this, &SettingsWindow::onDeviceClicked);

    connect(m_mgr, &BluezManager::devicesChanged, this, &SettingsWindow::refreshLists);
    connect(m_mgr, &BluezManager::discoveringChanged, this, &SettingsWindow::refreshLists);
    // The adapter showed up while the window is open (startup, dongle
    // plugged in, bluetoothd started late): scan as if the window had just
    // been opened.
    connect(m_mgr, &BluezManager::adapterReady, this, [this]() {
        if (isVisible())
            m_mgr->startDiscovery();
    });
    connect(m_mgr, &BluezManager::pairingProgress, this, &SettingsWindow::onPairingProgress);
    connect(m_mgr, &BluezManager::batteryChanged, this,
            [this](const QString &, int) { refreshLists(); });
    connect(m_mgr, &BluezManager::deviceConnected, this,
            [this](const QString &p, const QString &) { m_progress.remove(p); refreshLists(); });
    connect(m_mgr, &BluezManager::deviceDisconnected, this,
            [this](const QString &p, const QString &, bool) { m_progress.remove(p); refreshLists(); });
    connect(m_audio, &AudioManager::changed, this, &SettingsWindow::refreshLists);

    refreshLists();
}

// Left of the check mark of a connected audio device: shows whether the
// system sound plays through it; when it does not, a click routes it there.
// nullptr when the sound server knows nothing about the device.
QWidget *SettingsWindow::makeOutputIndicator(const BtDevice &d, QWidget *row)
{
    const AudioManager::Output out = m_audio->output(d.address);
    if (out == AudioManager::Output::Unknown)
        return nullptr;

    if (out == AudioManager::Output::Here) {
        auto *here = new QLabel(row);
        here->setPixmap(Icons::output(true).pixmap(22, 22));
        here->setAlignment(Qt::AlignCenter);
        here->setFixedSize(30, 30);
        here->setToolTip(tr("System sound is playing through %1").arg(d.displayName()));
        return here;
    }

    const QString current = m_audio->defaultOutputName();
    auto *route = new QToolButton(row);
    route->setIcon(Icons::output(false));
    route->setIconSize(QSize(22, 22));
    route->setAutoRaise(true);
    route->setFixedSize(30, 30);
    route->setToolTip(
        (current.isEmpty() ? tr("System sound is not playing through this device.")
                           : tr("System sound is playing through %1.").arg(current)) +
        QLatin1Char('\n') + tr("Click to play it through %1.").arg(d.displayName()));
    route->setAccessibleName(tr("Play sound through %1").arg(d.displayName()));
    connect(route, &QToolButton::clicked, this,
            [this, address = d.address]() { m_audio->routeOutputTo(address); });
    return route;
}

// "Audio profile" submenu (A2DP codecs, headset mode, off) as the sound
// server offers them for a connected device, each with its quality tier in
// the right-hand (shortcut) column.
void SettingsWindow::addProfileMenu(QMenu *menu, const BtDevice &d)
{
    const QList<AudioProfile> profiles = m_audio->profiles(d.address);
    if (!d.connected || profiles.isEmpty())
        return;

    QMenu *sub = menu->addMenu(tr("Audio profile"));
    auto *group = new QActionGroup(sub); // exclusive: radio items
    const QString active = m_audio->activeProfile(d.address);
    for (const AudioProfile &p : profiles) {
        const QString quality = qualityLabel(p);
        QAction *action = sub->addAction(
            quality.isEmpty() ? p.description : p.description + QLatin1Char('\t') + quality);
        action->setCheckable(true);
        action->setChecked(p.name == active);
        action->setEnabled(p.available);
        group->addAction(action);
        connect(action, &QAction::triggered, this,
                [this, address = d.address, name = p.name](bool) {
                    m_audio->setProfile(address, name);
                });
    }
}

QWidget *SettingsWindow::makeRow(const BtDevice &d, const QString &progress)
{
    auto *row = new QWidget;

    auto *h = new QHBoxLayout(row);
    h->setContentsMargins(8, 6, 8, 6);
    h->setSpacing(10);

    // "Present" == reachable right now: either connected, or sighted on air
    // recently. TWS earbuds advertise only in brief sporadic bursts (often a
    // single RSSI blip per scan), and BlueZ drops RSSI the instant discovery
    // stops, so a momentary `hasRssi` is too flaky to drive the UI. Instead we
    // keep a device "present" for a grace window after its last sighting, so one
    // caught burst yields a stable "available" state. A saved/trusted device
    // that is powered off is never sighted, so it stays greyed-out as offline.
    const qint64 PRESENCE_GRACE_MS = 120000; // 2 min since last on-air sighting
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const bool present = d.connected ||
        (d.lastSeenMs > 0 && now - d.lastSeenMs < PRESENCE_GRACE_MS);

    auto *iconLbl = new QLabel(row);
    const QIcon ico = d.isAudio() ? Icons::audio() : Icons::tray(d.connected);
    iconLbl->setPixmap(ico.pixmap(28, 28));
    iconLbl->setEnabled(present); // disabled palette dims the pixmap when offline
    h->addWidget(iconLbl);

    auto *vbox = new QVBoxLayout;
    vbox->setSpacing(1);

    auto *nameLbl = new QLabel(d.displayName(), row);
    QFont nf = nameLbl->font();
    nf.setBold(true);
    nameLbl->setFont(nf);
    if (!present) // saved-but-offline device: greyed so it reads as unreachable
        nameLbl->setStyleSheet(QStringLiteral("color: #9e9e9e;"));
    vbox->addWidget(nameLbl);

    QString sub;
    if (!progress.isEmpty() && progress != QLatin1String("Connected")) {
        sub = progress;
    } else {
        QStringList parts;
        if (d.connected)         parts << tr("Connected");
        else if (d.paired)       parts << tr("Paired");
        else if (d.trusted)      parts << tr("Saved");
        if (!present && !d.connected) parts << tr("Offline");
        if (d.battery >= 0)      parts << QStringLiteral("%1%").arg(d.battery);
        if (d.isAudio())         parts << tr("Audio");
        sub = parts.join(QStringLiteral("   \u00B7   "));
    }
    auto *subLbl = new QLabel(sub, row);
    subLbl->setStyleSheet(QStringLiteral("color: gray;"));
    vbox->addWidget(subLbl);
    h->addLayout(vbox);

    h->addStretch();

    QWidget *output = d.connected ? makeOutputIndicator(d, row) : nullptr;
    if (output)
        h->addWidget(output);

    if (d.connected) {
        auto *check = new QLabel(QStringLiteral("✓"), row); // check mark
        check->setStyleSheet(
            QStringLiteral("color: %1; font-weight: bold; font-size: 18px;")
                .arg(Icons::okGreen().name()));
        h->addWidget(check);
    }

    auto *more = new QToolButton(row);
    more->setText(QStringLiteral("\u22ee"));
    more->setToolTip(tr("Device actions"));
    more->setAccessibleName(tr("Actions for %1").arg(d.displayName()));
    more->setAutoRaise(true);
    more->setFixedSize(30, 30);
    more->setStyleSheet(QStringLiteral("QToolButton { font-size: 20px; }"));

    auto *menu = new QMenu(more);
    QAction *info = menu->addAction(tr("Info"));
    connect(info, &QAction::triggered, this, [this, device = d](bool) {
        DeviceInfoDialog dialog(device, this);
        dialog.exec();
    });

    addProfileMenu(menu, d);

    menu->addSeparator();
    QAction *disconnect = menu->addAction(tr("Disconnect"));
    disconnect->setEnabled(d.connected);
    connect(disconnect, &QAction::triggered, this,
            [this, path = d.path](bool) { m_mgr->disconnectDevice(path); });

    QAction *forget = menu->addAction(tr("Forget device"));
    forget->setEnabled(d.paired || d.trusted);
    connect(forget, &QAction::triggered, this,
            [this, path = d.path](bool) { m_mgr->forgetDevice(path); });

    more->setMenu(menu);
    more->setPopupMode(QToolButton::InstantPopup);
    h->addWidget(more);

    // Let clicks on labels fall through to the list while the actions button
    // remains interactive.
    const auto labels = row->findChildren<QLabel *>();
    for (QLabel *label : labels)
        label->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    if (output) // keep its tooltip
        output->setAttribute(Qt::WA_TransparentForMouseEvents, false);

    return row;
}

void SettingsWindow::refreshLists()
{
    const bool scanning = m_mgr->isDiscovering();

    m_paired->clear();
    m_available->clear();

    const QList<BtDevice> devs = m_mgr->devices(); // already sorted audio-first
    for (const BtDevice &d : devs) {
        QListWidget *list = d.paired ? m_paired : m_available;
        auto *item = new QListWidgetItem(list);
        item->setData(Qt::UserRole, d.path);
        QWidget *row = makeRow(d, m_progress.value(d.path));
        item->setSizeHint(row->sizeHint());
        list->setItemWidget(item, row);
    }

    if (m_paired->count() == 0) {
        auto *placeholder = new QListWidgetItem(tr("No paired devices yet"), m_paired);
        placeholder->setFlags(Qt::NoItemFlags);
    }
    if (m_available->count() == 0) {
        auto *placeholder = new QListWidgetItem(
            scanning ? tr("Scanning\xE2\x80\xA6") : tr("No devices found"),
            m_available);
        placeholder->setFlags(Qt::NoItemFlags);
    }

    m_spinner->setSpinning(scanning);
    m_status->setText(scanning ? tr("Scanning for devices\xE2\x80\xA6")
                               : tr("Click a device to connect."));
}

void SettingsWindow::onPairingProgress(const QString &path, const QString &status)
{
    if (status == QLatin1String("Connected") || status == QLatin1String("Forgotten"))
        m_progress.remove(path);
    else
        m_progress[path] = status;
    refreshLists();
}

void SettingsWindow::onDeviceClicked(QListWidgetItem *item)
{
    if (!item)
        return;
    const QString path = item->data(Qt::UserRole).toString();
    if (path.isEmpty())
        return;
    m_mgr->smartConnect(path);
}

void SettingsWindow::onRefreshClicked()
{
    m_mgr->refresh();
    refreshLists();
}

void SettingsWindow::onResetAdapterClicked()
{
    m_status->setText(tr("Resetting Bluetooth adapter\xE2\x80\xA6"));
    m_resetAdapter->setEnabled(false);
    m_mgr->resetAdapter();
    // The power cycle takes a few seconds; re-enable and rescan once it settles.
    QTimer::singleShot(3500, this, [this]() {
        m_resetAdapter->setEnabled(true);
        m_mgr->startDiscovery();
    });
}

void SettingsWindow::onLaunchToggled(bool on)
{
    AutoStart::setEnabled(on);
}

void SettingsWindow::openAndDiscover()
{
    m_launch->setChecked(AutoStart::isEnabled());
    show();
    raise();
    activateWindow();
    refreshLists();
    m_mgr->startDiscovery();
}

void SettingsWindow::changeEvent(QEvent *event)
{
    // Light/dark switch: rebuild the rows, whose greens follow the palette.
    if (event->type() == QEvent::PaletteChange)
        refreshLists();
    QWidget::changeEvent(event);
}

void SettingsWindow::closeEvent(QCloseEvent *event)
{
    m_mgr->stopDiscovery();
    hide();
    event->accept();
}
