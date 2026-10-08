#include "ThemeWatcher.h"

#include <QApplication>
#include <QStyle>
#include <QColor>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusVariant>
#include <QDebug>

namespace {

const char *PORTAL_SERVICE   = "org.freedesktop.portal.Desktop";
const char *PORTAL_PATH      = "/org/freedesktop/portal/desktop";
const char *PORTAL_SETTINGS  = "org.freedesktop.portal.Settings";
const char *APPEARANCE       = "org.freedesktop.appearance";
const char *COLOR_SCHEME     = "color-scheme";
const uint  PREFER_DARK      = 1; // 0 = no preference, 2 = prefer light

// Settings.Read wraps the value in one variant too many on most portal
// versions; peel off every level.
QVariant unwrap(QVariant v)
{
    while (v.userType() == qMetaTypeId<QDBusVariant>())
        v = qvariant_cast<QDBusVariant>(v).variant();
    return v;
}

bool paletteIsDark(const QPalette &p)
{
    return p.color(QPalette::Window).lightness() < 128;
}

QPalette darkPalette()
{
    const QColor window(53, 53, 53);
    const QColor base(42, 42, 42);
    const QColor text(230, 230, 230);
    const QColor disabled(127, 127, 127);
    const QColor accent(42, 130, 218);

    QPalette p;
    p.setColor(QPalette::Window, window);
    p.setColor(QPalette::WindowText, text);
    p.setColor(QPalette::Base, base);
    p.setColor(QPalette::AlternateBase, QColor(66, 66, 66));
    p.setColor(QPalette::ToolTipBase, window);
    p.setColor(QPalette::ToolTipText, text);
    p.setColor(QPalette::PlaceholderText, disabled);
    p.setColor(QPalette::Text, text);
    p.setColor(QPalette::Button, window);
    p.setColor(QPalette::ButtonText, text);
    p.setColor(QPalette::BrightText, Qt::red);
    p.setColor(QPalette::Light, QColor(75, 75, 75));
    p.setColor(QPalette::Midlight, QColor(64, 64, 64));
    p.setColor(QPalette::Mid, QColor(45, 45, 45));
    p.setColor(QPalette::Dark, QColor(35, 35, 35));
    p.setColor(QPalette::Shadow, QColor(20, 20, 20));
    p.setColor(QPalette::Link, accent.lighter(130));
    p.setColor(QPalette::Highlight, accent);
    p.setColor(QPalette::HighlightedText, Qt::white);
    p.setColor(QPalette::Disabled, QPalette::WindowText, disabled);
    p.setColor(QPalette::Disabled, QPalette::Text, disabled);
    p.setColor(QPalette::Disabled, QPalette::ButtonText, disabled);
    p.setColor(QPalette::Disabled, QPalette::Highlight, QColor(80, 80, 80));
    p.setColor(QPalette::Disabled, QPalette::HighlightedText, disabled);
    return p;
}

} // namespace

ThemeWatcher::ThemeWatcher(QObject *parent)
    : QObject(parent)
{
}

bool ThemeWatcher::isDark()
{
    return paletteIsDark(QApplication::palette());
}

void ThemeWatcher::start()
{
    const QByteArray platformTheme = qgetenv("QT_QPA_PLATFORMTHEME").toLower();
    for (const char *managed : {"lxqt", "kde", "qt5ct", "qt6ct"}) {
        if (platformTheme.startsWith(managed))
            return; // the user's Qt palette is set there
    }

    m_systemPalette = QApplication::palette();
    m_systemStyle = QApplication::style()->name();

    QDBusConnection bus = QDBusConnection::sessionBus();
    bus.connect(QLatin1String(PORTAL_SERVICE), QLatin1String(PORTAL_PATH),
                QLatin1String(PORTAL_SETTINGS), QStringLiteral("SettingChanged"), this,
                SLOT(onSettingChanged(QString,QString,QDBusVariant)));

    QDBusMessage read = QDBusMessage::createMethodCall(
        QLatin1String(PORTAL_SERVICE), QLatin1String(PORTAL_PATH),
        QLatin1String(PORTAL_SETTINGS), QStringLiteral("Read"));
    read << QLatin1String(APPEARANCE) << QLatin1String(COLOR_SCHEME);
    auto *watcher = new QDBusPendingCallWatcher(bus.asyncCall(read), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this](QDBusPendingCallWatcher *w) {
                w->deleteLater();
                const QDBusMessage reply = w->reply();
                if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty()) {
                    qInfo().noquote() << "Theme: no desktop color scheme"
                                      << "(" + reply.errorMessage() + ")";
                    return;
                }
                apply(unwrap(reply.arguments().constFirst()).toUInt());
            });
}

void ThemeWatcher::onSettingChanged(const QString &ns, const QString &key,
                                    const QDBusVariant &value)
{
    if (ns == QLatin1String(APPEARANCE) && key == QLatin1String(COLOR_SCHEME))
        apply(unwrap(value.variant()).toUInt());
}

void ThemeWatcher::apply(uint colorScheme)
{
    const bool wantDark = colorScheme == PREFER_DARK && !paletteIsDark(m_systemPalette);
    if (wantDark == m_applied)
        return;
    m_applied = wantDark;

    if (wantDark) {
        qInfo() << "Theme: the desktop prefers dark; using a dark palette";
        QApplication::setStyle(QStringLiteral("Fusion"));
        QApplication::setPalette(darkPalette());
    } else {
        qInfo() << "Theme: back to the system palette";
        QApplication::setStyle(m_systemStyle);
        QApplication::setPalette(m_systemPalette);
    }
}
