#pragma once

#include <QString>

// Manages the "Launch on startup" (XDG autostart) state.
//
// Default-on is provided by a *system-wide* entry that the .deb installs at
//   /etc/xdg/autostart/bluetooth-headphones-manager.desktop
// so the app starts (minimized, into the tray) on login for every user right
// after install — no need to launch it once first.
//
// The checkbox toggles a *per-user* override at
//   ~/.config/autostart/bluetooth-headphones-manager.desktop
// Unchecking writes an override carrying Hidden=true, which suppresses the
// system entry (per the XDG spec a user entry replaces the system one of the
// same name). Re-checking removes the override. When no system entry exists
// (e.g. running straight from a build tree) enabling instead writes a normal
// per-user entry pointing at the running binary.
namespace AutoStart {
    bool    isEnabled();
    void    setEnabled(bool enabled);
    QString desktopFilePath();   // per-user entry path
}
