#!/usr/bin/env bash
#
# Build bluetooth-headphones-manager and package it into a .deb installable on
# Linux Mint / Ubuntu / Debian with the distribution's Qt 6 (>= 6.2).
#
# Usage:
#   ./build_deb.sh                # build + package
#   ./build_deb.sh clean          # remove build artefacts
#
# Qt 6 is located through CMake's normal search. If the Qt 6 development files
# are not installed system-wide (qt6-base-dev), point CMake at another prefix:
#   CMAKE_PREFIX_PATH=/path/to/qt6/usr ./build_deb.sh
#
set -euo pipefail

PKG_NAME="bluetooth-headphones-manager"
ARCH="amd64"
MAINTAINER="antoh1986 <antoh86@gmail.com>"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build"
STAGE_DIR="${BUILD_DIR}/pkgroot"
VERSION_SRC="${SCRIPT_DIR}/VERSION"

if [[ "${1:-}" == "clean" ]]; then
    echo "Cleaning build artefacts..."
    rm -rf "${BUILD_DIR}" "${SCRIPT_DIR}/${PKG_NAME}_"*.deb
    exit 0
fi

# --- Auto-increment the version (minor) on every package build -------------
# VERSION is the single source of truth read by CMake. Bump it BEFORE
# configuring so the new number is compiled into the binary (and thus shown in
# the GUI) and used for the .deb filename / control file. MAJOR is bumped by
# hand for real releases; bumping MINOR resets PATCH to 0.
# Set BHM_NO_BUMP=1 (CI / tagged release builds) to keep the VERSION file as-is
# instead of auto-incrementing — the git tag is the source of truth there.
if [[ ! -s "${VERSION_SRC}" ]]; then
    echo "Error: version file ${VERSION_SRC} is missing" >&2
    exit 1
fi
OLD_VERSION="$(tr -d '[:space:]' < "${VERSION_SRC}")"
if [[ ! "${OLD_VERSION}" =~ ^([0-9]+)\.([0-9]+)\.([0-9]+)$ ]]; then
    echo "Error: ${VERSION_SRC} must contain MAJOR.MINOR.PATCH (got '${OLD_VERSION}')" >&2
    exit 1
fi
if [[ -n "${BHM_NO_BUMP:-}" ]]; then
    echo "==> Version pinned (no bump): ${OLD_VERSION}"
else
    V_MAJOR="${BASH_REMATCH[1]}"
    V_MINOR="${BASH_REMATCH[2]}"
    NEW_VERSION="${V_MAJOR}.$(( V_MINOR + 1 )).0"
    printf '%s\n' "${NEW_VERSION}" > "${VERSION_SRC}"
    echo "==> Version bumped (minor): ${OLD_VERSION} -> ${NEW_VERSION}"
fi

JOBS="$(nproc 2>/dev/null || echo 2)"

echo "==> Configuring (CMake, Release)"
# No RPATH: the packaged binary must load Qt from the system library path, even
# when it was built against a Qt prefix outside the default search path.
cmake -S "${SCRIPT_DIR}" -B "${BUILD_DIR}" -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_SKIP_RPATH=ON

VERSION_FILE="${BUILD_DIR}/project-version.txt"
if [[ ! -s "${VERSION_FILE}" ]]; then
    echo "Error: CMake did not generate ${VERSION_FILE}" >&2
    exit 1
fi
VERSION="$(tr -d '[:space:]' < "${VERSION_FILE}")"
if [[ -z "${VERSION}" ]]; then
    echo "Error: project version is empty" >&2
    exit 1
fi
DEB_OUT="${SCRIPT_DIR}/${PKG_NAME}_${VERSION}_${ARCH}.deb"

echo "==> Building version ${VERSION}"
cmake --build "${BUILD_DIR}" --parallel "${JOBS}"

echo "==> Staging package tree"
rm -rf "${STAGE_DIR}"
install -d "${STAGE_DIR}/usr/bin"
install -d "${STAGE_DIR}/usr/share/applications"
install -d "${STAGE_DIR}/usr/share/icons/hicolor/scalable/apps"
install -d "${STAGE_DIR}/usr/share/doc/${PKG_NAME}"
install -d "${STAGE_DIR}/etc/xdg/autostart"
install -d "${STAGE_DIR}/DEBIAN"

install -m 0755 "${BUILD_DIR}/${PKG_NAME}" \
    "${STAGE_DIR}/usr/bin/${PKG_NAME}"
if readelf -d "${STAGE_DIR}/usr/bin/${PKG_NAME}" | grep -qE '\((RPATH|RUNPATH)\)'; then
    echo "Error: packaged binary carries an RPATH/RUNPATH" >&2
    exit 1
fi
install -m 0644 "${SCRIPT_DIR}/packaging/${PKG_NAME}.desktop" \
    "${STAGE_DIR}/usr/share/applications/${PKG_NAME}.desktop"
install -m 0644 "${SCRIPT_DIR}/resources/icons/${PKG_NAME}.svg" \
    "${STAGE_DIR}/usr/share/icons/hicolor/scalable/apps/${PKG_NAME}.svg"
# System-wide autostart entry -> "launch on startup" is on by default after
# install. Installed under the per-user entry's basename so a per-user override
# (~/.config/autostart/bluetooth-headphones-manager.desktop) can suppress it.
install -m 0644 "${SCRIPT_DIR}/packaging/${PKG_NAME}-autostart.desktop" \
    "${STAGE_DIR}/etc/xdg/autostart/${PKG_NAME}.desktop"

# Licence notices: the MIT licence of this program, plus the notice that it
# uses the Qt libraries under the GNU LGPL v3 (LGPLv3 section 3 asks for it
# with object code that incorporates material from the Qt headers). Qt itself
# is not shipped: the binary links dynamically to the distribution's Qt
# packages. Debian-based systems always provide the GPL/LGPL texts in
# /usr/share/common-licenses.
COPYRIGHT_FILE="${STAGE_DIR}/usr/share/doc/${PKG_NAME}/copyright"
cat > "${COPYRIGHT_FILE}" <<EOF
Format: https://www.debian.org/doc/packaging-manuals/copyright-format/1.0/
Upstream-Name: ${PKG_NAME}
Upstream-Contact: ${MAINTAINER}
Source: https://github.com/antoh1986/bluetooth-headphones-manager
Comment: This program uses the Qt 6 libraries (Qt Core, Qt Gui, Qt Widgets,
 Qt D-Bus, Qt Network and the Qt SVG icon plugin), Copyright (C) The Qt
 Company Ltd. and other contributors, under the terms of the GNU Lesser
 General Public License version 3. Qt is not included in this package: it is
 linked dynamically from the distribution's own Qt packages, which can be
 upgraded or replaced independently. The LGPL v3 text is in
 /usr/share/common-licenses/LGPL-3 and the GPL v3 text it supplements is in
 /usr/share/common-licenses/GPL-3. Qt source code is available from
 https://download.qt.io/ and from the distribution's source packages.
 It also links dynamically to the PulseAudio client libraries (libpulse,
 libpulse-mainloop-glib) from the distribution, used under the GNU Lesser
 General Public License version 2.1 or later
 (/usr/share/common-licenses/LGPL-2.1).

Files: *
Copyright: $(sed -n 's/^Copyright (c) //p' "${SCRIPT_DIR}/LICENSE")
License: Expat
EOF
# Append the MIT licence text (from "Permission ..." on) in the
# copyright-format continuation syntax: one leading space, "." for blank lines.
sed -n '/^Permission/,$p' "${SCRIPT_DIR}/LICENSE" \
    | sed -e 's/^$/./' -e 's/^/ /' >> "${COPYRIGHT_FILE}"
chmod 0644 "${COPYRIGHT_FILE}"

# Approximate installed size (KiB) for the control file.
INSTALLED_SIZE="$(du -sk "${STAGE_DIR}" | cut -f1)"

echo "==> Writing DEBIAN/control"
cat > "${STAGE_DIR}/DEBIAN/control" <<EOF
Package: ${PKG_NAME}
Version: ${VERSION}
Section: utils
Priority: optional
Architecture: ${ARCH}
Maintainer: ${MAINTAINER}
Installed-Size: ${INSTALLED_SIZE}
Homepage: https://github.com/antoh1986/bluetooth-headphones-manager
Depends: libc6, libstdc++6, libqt6core6 | libqt6core6t64, libqt6gui6 | libqt6gui6t64, libqt6widgets6 | libqt6widgets6t64, libqt6dbus6 | libqt6dbus6t64, libqt6network6 | libqt6network6t64, libqt6svg6, qt6-qpa-plugins, libpulse0, libpulse-mainloop-glib0, bluez
Description: Bluetooth Headphones Manager
 System tray application for Linux Mint / Cinnamon, LXQt and other Linux
 desktops that pairs, trusts and connects Bluetooth audio devices with a
 single click. The tray icon colour reflects the connection state, the
 right-click menu shows the currently connected device and its battery level,
 and a settings window lists paired and available devices with audio devices
 shown first, shows whether the system sound plays through the connected
 device (one click routes it there) and switches its audio profile (codec).
EOF

echo "==> Writing DEBIAN/conffiles"
# Mark the autostart entry as a conffile so dpkg preserves user edits across
# upgrades and cleans it up on purge.
cat > "${STAGE_DIR}/DEBIAN/conffiles" <<EOF
/etc/xdg/autostart/${PKG_NAME}.desktop
EOF

echo "==> Writing DEBIAN/postinst"
cat > "${STAGE_DIR}/DEBIAN/postinst" <<'EOF'
#!/bin/sh
set -e
if command -v gtk-update-icon-cache >/dev/null 2>&1; then
    gtk-update-icon-cache -q -t -f /usr/share/icons/hicolor || true
fi
if command -v update-desktop-database >/dev/null 2>&1; then
    update-desktop-database -q /usr/share/applications || true
fi

# --- Best-effort immediate launch in the desktop user's session ------------
# A fresh install / upgrade then shows up in the tray right away, without a
# logout or reboot. Future boots are still handled by the XDG autostart entry
# (/etc/xdg/autostart). Everything here is best-effort: never fail the install.
app=/usr/bin/bluetooth-headphones-manager
comm=bluetooth-headp   # /proc comm is truncated to 15 chars

# Who is the desktop user? Prefer the invoker of `sudo apt install`, else the
# owner of the first active seat (graphical) session.
target_user=""
if [ -n "${SUDO_USER:-}" ] && [ "${SUDO_USER}" != "root" ]; then
    target_user="${SUDO_USER}"
elif command -v loginctl >/dev/null 2>&1; then
    target_user="$(loginctl list-sessions --no-legend 2>/dev/null \
        | awk '$0 ~ /seat/ {print $3; exit}')"
fi

if [ -n "$target_user" ] && [ -x "$app" ] && command -v loginctl >/dev/null 2>&1; then
    uid="$(id -u "$target_user" 2>/dev/null || true)"
    if [ -n "$uid" ] && [ -S "/run/user/$uid/bus" ]; then
        runtime="/run/user/$uid"
        # Resolve the user's active session, then its X display.
        sess="$(loginctl show-user "$uid" -p Display --value 2>/dev/null || true)"
        display="$(loginctl show-session "$sess" -p Display --value 2>/dev/null || true)"
        [ -n "$display" ] || display=":0"

        # On upgrade an older instance is still running and holds the
        # single-instance lock; replace it. -x matches the (truncated) comm so
        # this never hits the su/sh shell running the command below.
        su "$target_user" -s /bin/sh -c "\
            pkill -u '$target_user' -x '$comm' >/dev/null 2>&1 || true; \
            sleep 1; \
            DISPLAY='$display' \
            XDG_RUNTIME_DIR='$runtime' \
            DBUS_SESSION_BUS_ADDRESS='unix:path=$runtime/bus' \
            setsid '$app' --minimized >/dev/null 2>&1 < /dev/null &" \
            >/dev/null 2>&1 || true
    fi
fi

exit 0
EOF
chmod 0755 "${STAGE_DIR}/DEBIAN/postinst"

echo "==> Building .deb"
# fakeroot gives the packaged files root:root ownership without being root.
if command -v fakeroot >/dev/null 2>&1; then
    fakeroot dpkg-deb --build "${STAGE_DIR}" "${DEB_OUT}"
else
    dpkg-deb --build "${STAGE_DIR}" "${DEB_OUT}"
fi

echo
echo "Done: ${DEB_OUT}"
echo "Install with: sudo apt install ${DEB_OUT}"
