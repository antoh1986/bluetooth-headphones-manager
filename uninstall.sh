#!/usr/bin/env bash
#
# uninstall.sh — completely remove every trace of bluetooth-headphones-manager
# (Bluetooth Headphones Manager) from the system, as if it had never
# been installed.
#
# It:
#   1. Stops every running bluetooth-headphones-manager process.
#   2. Purges the .deb package (or removes the installed files by hand).
#   3. Removes the per-user autostart entry / override.
#   4. Removes the per-user config dir (incl. the autostart-checkbox config
#      ~/.config/bluetooth-headphones-manager/bluetooth-headphones-manager.conf).
#   5. Removes the per-user data/log dir (~/.local/share/bluetooth-headphones-manager).
#   6. Removes the single-instance runtime socket.
#   7. Refreshes the desktop / icon caches.
#
# This script only removes what *installing* and *running* the app put on the
# system. It does NOT touch the source/build tree (this script, src/,
# CMakeLists.txt, build/, *.deb …). To clean local build artefacts use
# `./build_deb.sh clean`.
#
# Usage:
#   ./uninstall.sh            # remove everything (prompts for sudo if needed)
#
# Safe to run repeatedly; every step is idempotent.

set -u

PKG_NAME="bluetooth-headphones-manager"

# ---- pretty logging -------------------------------------------------------
if [[ -t 1 ]]; then
    C_GREEN=$'\033[1;32m'; C_YELLOW=$'\033[1;33m'; C_RED=$'\033[1;31m'; C_OFF=$'\033[0m'
else
    C_GREEN=''; C_YELLOW=''; C_RED=''; C_OFF=''
fi
info()  { printf '%s==>%s %s\n'  "$C_GREEN"  "$C_OFF" "$*"; }
warn()  { printf '%s!!%s %s\n'   "$C_YELLOW" "$C_OFF" "$*"; }
err()   { printf '%sxx%s %s\n'   "$C_RED"    "$C_OFF" "$*" >&2; }

# ---- sudo helper ----------------------------------------------------------
# Run a command with root privileges only when needed/possible.
SUDO=""
if [[ "$(id -u)" -ne 0 ]]; then
    if command -v sudo >/dev/null 2>&1; then
        SUDO="sudo"
    fi
fi
as_root() {
    if [[ "$(id -u)" -eq 0 ]]; then
        "$@"
    elif [[ -n "$SUDO" ]]; then
        $SUDO "$@"
    else
        warn "need root for: $* (no sudo available, skipping)"
        return 1
    fi
}

rm_path() {
    # rm_path <root|user> <path>
    local scope="$1" path="$2"
    [[ -e "$path" || -L "$path" ]] || return 0
    if [[ "$scope" == "root" ]]; then
        as_root rm -rf -- "$path" && info "removed $path" || err "failed to remove $path"
    else
        rm -rf -- "$path" && info "removed $path" || err "failed to remove $path"
    fi
}

# ===========================================================================
# 1. Stop running processes
# ===========================================================================
info "Stopping running ${PKG_NAME} processes…"
stopped=0
# Match the exact process name (comm) first — covers the installed binary and
# the build-tree binary regardless of path.
if pgrep -x "$PKG_NAME" >/dev/null 2>&1; then
    pkill -TERM -x "$PKG_NAME" 2>/dev/null && stopped=1
    # Give them a moment to exit cleanly, then force-kill stragglers.
    for _ in 1 2 3 4 5; do
        pgrep -x "$PKG_NAME" >/dev/null 2>&1 || break
        sleep 0.3
    done
    if pgrep -x "$PKG_NAME" >/dev/null 2>&1; then
        pkill -KILL -x "$PKG_NAME" 2>/dev/null
        warn "force-killed remaining ${PKG_NAME} process(es)"
    fi
fi
if [[ "$stopped" -eq 1 ]]; then
    info "processes stopped"
else
    info "no running ${PKG_NAME} process found"
fi

# ===========================================================================
# 2. Remove the system-wide installation
# ===========================================================================
info "Removing system-wide installation…"
if command -v dpkg >/dev/null 2>&1 && dpkg -s "$PKG_NAME" >/dev/null 2>&1; then
    # Installed as a real package — purge it (also drops the conffile
    # /etc/xdg/autostart/bluetooth-headphones-manager.desktop and runs maintainer scripts).
    info "package is dpkg-managed; purging…"
    if command -v apt-get >/dev/null 2>&1; then
        as_root apt-get purge -y "$PKG_NAME" || as_root dpkg --purge "$PKG_NAME" || \
            err "failed to purge package $PKG_NAME"
    else
        as_root dpkg --purge "$PKG_NAME" || err "failed to purge package $PKG_NAME"
    fi
else
    info "no dpkg package registered; removing installed files directly…"
fi

# Remove the files unconditionally too, in case the package was installed by
# copying files (not via dpkg) or a purge left something behind.
rm_path root "/usr/bin/${PKG_NAME}"
rm_path root "/usr/share/applications/${PKG_NAME}.desktop"
rm_path root "/usr/share/icons/hicolor/scalable/apps/${PKG_NAME}.svg"
rm_path root "/etc/xdg/autostart/${PKG_NAME}.desktop"

# ===========================================================================
# 3-5. Remove per-user state
# ===========================================================================
info "Removing per-user autostart, config and data…"

# 3. Per-user autostart entry / suppression override (the "Launch on startup"
#    checkbox state, written by AutoStart.cpp).
rm_path user "${XDG_CONFIG_HOME:-$HOME/.config}/autostart/${PKG_NAME}.desktop"

# 4. Per-user config dir — holds bluetooth-headphones-manager.conf
#    (autostartInitialized=…),
#    i.e. the autostart-checkbox config the app stores.
rm_path user "${XDG_CONFIG_HOME:-$HOME/.config}/${PKG_NAME}"

# 5. Per-user data dir — the log directory
#    (~/.local/share/bluetooth-headphones-manager).
rm_path user "${XDG_DATA_HOME:-$HOME/.local/share}/${PKG_NAME}"

# ===========================================================================
# 6. Remove the single-instance runtime socket
# ===========================================================================
info "Removing single-instance socket…"
RUNTIME_DIR="${XDG_RUNTIME_DIR:-/run/user/$(id -u)}"
# QLocalServer name is "bluetooth-headphones-manager-singleinstance"; clean it from the
# runtime dir and the legacy /tmp fallback.
rm_path user "${RUNTIME_DIR}/${PKG_NAME}-singleinstance"
rm_path user "/tmp/${PKG_NAME}-singleinstance"

# ===========================================================================
# 7. Refresh desktop / icon caches
# ===========================================================================
info "Refreshing desktop / icon caches…"
if command -v update-desktop-database >/dev/null 2>&1; then
    as_root update-desktop-database -q /usr/share/applications 2>/dev/null || true
fi
if command -v gtk-update-icon-cache >/dev/null 2>&1; then
    as_root gtk-update-icon-cache -q -t -f /usr/share/icons/hicolor 2>/dev/null || true
fi

echo
info "Done. ${PKG_NAME} has been completely removed."
