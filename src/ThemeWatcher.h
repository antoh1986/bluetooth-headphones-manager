#pragma once

#include <QObject>
#include <QPalette>
#include <QString>

class QDBusVariant;

// Follows the desktop's light/dark preference, the freedesktop portal setting
// org.freedesktop.appearance color-scheme (GNOME, Cinnamon, KDE, ...). Qt
// before 6.5 -- what Ubuntu 22.04/24.04 and Linux Mint 21/22 ship -- does not
// take a GTK desktop's dark mode over, so while the desktop prefers dark but
// the application palette is light, this switches the app to Fusion with a
// dark palette. A platform theme that already gives a dark palette (KDE,
// Qt >= 6.5 on GTK desktops) or one where the user styles Qt explicitly
// (LXQt, qt5ct/qt6ct) is left alone.
class ThemeWatcher : public QObject
{
    Q_OBJECT
public:
    explicit ThemeWatcher(QObject *parent = nullptr);

    void start();

    // Whether the application palette is a dark one (ours or the platform's).
    static bool isDark();

private slots:
    void onSettingChanged(const QString &ns, const QString &key, const QDBusVariant &value);

private:
    void apply(uint colorScheme);

    QPalette m_systemPalette;   // what Qt gave us, restored on light mode
    QString  m_systemStyle;
    bool     m_applied = false; // our dark palette is active
};
