# CLAUDE.md

Guidance for working in this repository.

## Project

**Bluetooth Headphones Manager** — a one-click Bluetooth audio device manager
and system-tray app for **Linux Mint / Cinnamon** and **LXQt**, built with
Qt 6 and BlueZ D-Bus. It pairs, trusts and connects Bluetooth audio devices
with an "Android-like" UX. All Bluetooth control goes through **BlueZ over
D-Bus (system bus)** using Qt's `QtDBus` module.

## Public repository metadata

- **Display name:** `Bluetooth Headphones Manager`
- **GitHub repository slug:** `bluetooth-headphones-manager`
- **GitHub description:** `One-click Bluetooth audio device manager and system tray app for Linux Mint/Cinnamon and LXQt, built with Qt 6 and BlueZ D-Bus.`
- **GitHub topics:** `bluetooth`, `bluetooth-manager`, `bluetooth-headphones`,
  `bluez`, `linux`, `linux-mint`, `cinnamon`, `lxqt`, `qt`, `qt6`, `cpp`,
  `cpp17`, `dbus`, `audio`, `headphones`, `system-tray`
- Keep the public name and the Bluetooth/BlueZ/Linux/Cinnamon/LXQt search
  terms near the top of `README.md`. GitHub topics are repository settings and
  must be applied in the GitHub UI (or API) after the repository is created.
- Use the canonical identifier `bluetooth-headphones-manager` consistently for
  the checkout directory, CMake project and target, executable, Debian package,
  desktop/autostart files, application ID, icon, settings, logs and
  single-instance key. Do not introduce shortened aliases.

## Hard constraints (do not violate)

- **Target toolchain:** Qt 6 (minimum **6.2**, the Qt of Ubuntu 22.04 / Mint
  21 that CI builds the release `.deb` against), C++17, GCC, x86-64. Keep it
  building there: don't use Qt APIs newer than 6.2 without a version guard.
  Qt 5 is no longer supported.
- **Qt licensing (LGPLv3).** Qt is used under the LGPL v3. Always link it
  dynamically from the distribution's packages; never link statically or
  bundle Qt libraries/plugins into the `.deb` (that would add source-offer and
  relinking obligations). `build_deb.sh` ships the required notice in
  `/usr/share/doc/bluetooth-headphones-manager/copyright` and refuses to package
  a binary that carries an RPATH/RUNPATH. Don't add GPL-only Qt modules (e.g.
  Qt Charts) — they would force the app off MIT.
- **Prefer D-Bus.** Use native `QtDBus` against `org.bluez` for Bluetooth
  control. Shelling out (e.g. `rfkill` for a kernel-level adapter reset) is
  permitted where D-Bus has no clean equivalent, but avoid commands that need
  `sudo`/root and would prompt the user for a password — most adapter actions
  (including a power-cycle via `Adapter1.Powered`) work over D-Bus without it.
- **Never hardcode `hci0`.** Discover the adapter via the ObjectManager
  (`GetManagedObjects`, first `org.bluez.Adapter1`).
- **All D-Bus calls are asynchronous** (`QDBusPendingCallWatcher`). No
  `.waitForFinished()` / blocking calls on the GUI thread.
- **No polling.** Track state via signals: `InterfacesAdded`,
  `InterfacesRemoved`, `PropertiesChanged`.
- **UI language is English.** All user-facing strings in English.
- **UI ↔ BlueZ only via signals/slots** on `BluezManager`. The UI never makes
  D-Bus calls directly. (The session-bus exceptions are about the desktop,
  not BlueZ: `TrayApp` watches for the tray host appearing, and
  `ThemeWatcher` reads the portal's color scheme.)
- **Sound server only through `AudioManager`.** Default output and card
  profiles have no BlueZ/D-Bus equivalent; they go through libpulse (the
  PulseAudio API, also served by `pipewire-pulse`), with the same rules:
  asynchronous, event-driven (server subscription), no polling, and the UI
  only uses `AudioManager`'s accessors/slots and its `changed()` signal.

## Build / run / package

```bash
# Build
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel

# Run
./build/bluetooth-headphones-manager             # opens settings window
./build/bluetooth-headphones-manager --minimized # starts hidden in the tray

# Qt 6 dev files outside the default prefix (not installed via apt)?
# CMake reads the environment variable for both commands above and below:
#   CMAKE_PREFIX_PATH=/path/to/qt6/usr cmake -S . -B build ...

# Package (.deb) — also runs the CMake build (with RPATH disabled)
./build_deb.sh
./build_deb.sh clean              # remove build/ and *.deb

# Install (glob picks up the auto-versioned .deb; `clean` first if several exist)
sudo apt install ./bluetooth-headphones-manager_*_amd64.deb

# Uninstall — stop processes, purge the package, wipe per-user state
./uninstall.sh                    # idempotent; does NOT touch the source/build tree
```

There are no automated tests; verify by building warning-free (`-Wall
-Wextra` is on) and running against the live BlueZ stack.

## Architecture

| File | Responsibility |
|------|----------------|
| `src/main.cpp` | Entry point: `QApplication` setup, logger init, single-instance guard (`QLocalServer`/`QLocalSocket`), `--minimized`/`--tray` parsing, wires `BluezManager` + `TrayApp`. |
| `src/BluezManager.{h,cpp}` | The only D-Bus layer. Async wrapper over `org.bluez`. Tracks devices in `QMap<QString path, BtDevice>`. Holds the one-click state machine. Follows bluetoothd coming and going (see *BlueZ lifecycle* below). Emits `devicesChanged`, `deviceConnected`, `deviceDisconnected(path,name,expected)`, `batteryChanged`, `pairingProgress`, `adapterReady` (once the adapter is powered), `discoveringChanged`. |
| `src/BtDevice.h` | Plain value object (snapshot) for a device + `isAudio()` heuristic (icon hint / class major field `0x04` / audio profile UUIDs) + `displayName()` (Alias→Name→Address). |
| `src/AudioManager.{h,cpp}` | The only sound-server layer (libpulse on its GLib main loop, which runs on Qt's GLib event dispatcher). Tracks Bluetooth cards (profiles, active profile) and sinks by Bluetooth address, plus the default sink. `output(address)` → `Unknown`/`Elsewhere`/`Here`; `routeOutputTo(address)` makes the device the default sink (switching an output-less profile such as "Off" to the best playback one first); `setProfile(address, name)`. Emits `changed()` only on relevant changes. Reconnects with back-off if the server goes away. |
| `src/BtAgent.{h,cpp}` | Auto-accepting `org.bluez.Agent1` (a `QDBusAbstractAdaptor`). Registered by `BluezManager` as the **default** agent so app-initiated `Pair()` completes a full authenticated bond silently (the "yes" `bluetoothctl` asks for). Every callback accepts; never rejects. Object path `/bluetoothheadphonesmanager/agent` (hyphens are illegal in D-Bus paths). |
| `src/BluezTypes.h` | D-Bus marshalling typedefs `InterfaceList` (`a{sa{sv}}`) and `ManagedObjectList` (`a{oa{sa{sv}}}`); registered with `qDBusRegisterMetaType<>()` in `BluezManager::start()`. |
| `src/TrayApp.{h,cpp}` | `QSystemTrayIcon` + context menu (header = connected device+battery, Settings, Quit), left-click opens Settings, disconnect notifications. Re-creates the icon when a tray host appears (see *Tray icon under LXQt* below). |
| `src/SettingsWindow.{h,cpp}` | Two `QListWidget`s (Paired / Available), audio-first, check mark on active device, battery, live progress text, Refresh, "Launch on startup". Left of the check mark: the sound-output indicator (green speaker = default output is this device; otherwise a framed "Play here" button with a grey crossed speaker that routes it here — also when the active profile has no output). Device actions menu has an "Audio profile" radio submenu. |
| `src/AutoStart.{h,cpp}` | "Launch on startup" state. Default-on comes from a **system-wide** entry the `.deb` installs at `/etc/xdg/autostart/bluetooth-headphones-manager.desktop` (Exec `--minimized`), so it autostarts after install without launching once. The checkbox toggles a **per-user** override at `~/.config/autostart/bluetooth-headphones-manager.desktop`: unchecking writes `Hidden=true` to suppress the system entry, re-checking removes it. With no system entry (build tree), enabling writes a normal per-user entry pointing at the running binary. |
| `src/ThemeWatcher.{h,cpp}` | Follows the desktop's dark/light preference: reads `org.freedesktop.portal.Settings` `org.freedesktop.appearance` / `color-scheme` (async) and its `SettingChanged` signal. Qt < 6.5 ignores a GTK desktop's dark mode, so while the portal says "prefer dark" (1) and Qt's own palette is light it applies Fusion + a dark palette, restoring Qt's palette/style otherwise. Skipped when `QT_QPA_PLATFORMTHEME` is lxqt/kde/qt5ct/qt6ct (the user styles Qt there) and when Qt's palette is already dark. |
| `src/Icons.{h,cpp}` | App/tray/audio icons from SVG resources, with a `QPainter` fallback if the SVG icon engine is missing. Tray icons are returned as raster frames at several sizes. Painter-drawn sound-output speaker; `okGreen()` picks a green readable on the current (light/dark) palette — `SettingsWindow` rebuilds its rows on `PaletteChange`. |
| `src/Logger.{h,cpp}` | Installs a Qt message handler; logs to `~/.local/share/bluetooth-headphones-manager/bluetooth-headphones-manager.log`. |
| `resources/` | `resources.qrc` + placeholder SVG icons (`bt-connected`, `bt-disconnected`, `bluetooth-headphones-manager`, `audio`). |
| `packaging/bluetooth-headphones-manager.desktop` | Installed menu entry (`Categories=AudioVideo;Audio;`). |
| `packaging/bluetooth-headphones-manager-autostart.desktop` | System-wide XDG autostart entry, installed to `/etc/xdg/autostart/bluetooth-headphones-manager.desktop` (renamed to that basename; Exec `--minimized`). Makes launch-on-startup default-on after install; registered as a dpkg conffile. Carries `X-LXQt-Need-Tray=true` (lxqt-session starts it once the tray is up; other desktops ignore it), as does the per-user entry `AutoStart` writes. |

## Key implementation details to preserve

- **One-click magic** (`BluezManager::smartConnect`): already connected → no-op;
  another device connected → `Disconnect()` it first, then chain; not paired →
  `Pair()` → set `Trusted=true` → `Connect()`; paired-not-connected → trust +
  `Connect()`. Progress surfaced via `pairingProgress(path, "Pairing…" /
  "Connecting…" / "Connected" / "Failed: <reason>")`.
- **`PropertiesChanged` path recovery:** subscribed with an empty path
  (wildcard); the originating object path is read from a trailing
  `const QDBusMessage&` slot parameter. Keep that slot signature intact.
- **Pair/Connect use a longer timeout** (`OP_TIMEOUT_MS`, 45 s) since the
  default 25 s D-Bus timeout is too short for pairing.
- **Disconnect notification suppression:** intentional disconnects (device
  switching, explicit disconnect) are added to `m_intentionalDisconnect` so the
  "device disconnected" notification only fires on *unexpected* drops
  (`expected == false`). No auto-reconnect.
- **Discovery is windowed:** auto-starts when the settings window opens (or
  on `adapterReady` while it is open), runs ~60 s (single-shot
  `m_discoveryTimer` → `stopDiscovery`), restartable via Refresh, stopped on
  window close.
- **BlueZ lifecycle:** `start()` asks the bus `NameHasOwner("org.bluez")`
  first and only talks to BlueZ if it is running — a blind call would
  D-Bus-activate bluetoothd, which with no adapter times out after 25 s. A
  `QDBusServiceWatcher` on `org.bluez` then re-registers the agent and
  re-reads the managed objects every time bluetoothd starts, and drops all
  state when it stops. Removing the used adapter (`InterfacesRemoved` with
  `Adapter1`) forgets it; the next one that appears is taken. Don't call BlueZ
  from those teardown paths (it would re-activate the daemon).
- **Tray icon under LXQt:** with `QT_QPA_PLATFORMTHEME=lxqt` the tray is
  served by lxqt-qtplugin's own StatusNotifierItem, which sends one pixmap
  per `QIcon::availableSizes()` entry. An SVG-backed `QIcon` reports no
  sizes, so the panel shows an "unknown" icon — keep `Icons::tray()`
  returning raster frames. Qt (and the LXQt plugin) also choose between
  StatusNotifierItem and XEmbed only once, when the `QSystemTrayIcon` is
  constructed, so `TrayApp` re-creates it when `org.kde.StatusNotifierWatcher`
  registers: an icon created before the panel would otherwise never show on
  Wayland (no XEmbed there).
- **Modules:** Core, Gui, Widgets, DBus, **Network** (Network only for the
  single-instance `QLocalServer`), plus `libpulse` + `libpulse-mainloop-glib`
  via pkg-config. Reflect any module change in both `CMakeLists.txt` and the
  `.deb` `Depends` (and the build packages in both CI workflows and the README).
- **Sound output / profiles:** cards and sinks are matched to a device by
  Bluetooth address (`api.bluez5.address` on PipeWire, `device.string` with
  `device.bus=bluetooth` on PulseAudio). "Plays through the device" means the
  server's default sink is one of its sinks; routing just sets the default
  sink, like the desktop's sound settings (PipeWire moves streams that follow
  the default) — and the card's active profile must have an output:
  WirePlumber 0.5 (PipeWire ≥ 1.2) keeps a persistent
  `bluez_output.<addr>` sink across profile changes, even on "Off", while
  older stacks drop the sink (`…<addr>.1`) and `routeOutputTo` waits for it
  to reappear. Profiles are listed Off → playback-only (A2DP) → headset,
  each tagged Best / Medium / Low quality (`profileQuality()`: LDAC, aptX
  HD/Lossless, LC3plus = best; headset modes = low; other A2DP = medium —
  the server's priority does not track quality, e.g. SBC > SBC-XQ).
  `AudioManager::start()` disables itself if Qt's event dispatcher is not GLib
  based (`QT_NO_GLIB`).
- **`.deb` dependencies** list classic Qt6 names with `*t64` alternatives
  (e.g. `libqt6core6 | libqt6core6t64`) for old and new Mint/Ubuntu/Debian,
  plus `libqt6svg6` (SVG icon engine plugin, loaded at runtime),
  `qt6-qpa-plugins` (Debian splits the xcb platform plugin out of
  `libqt6gui6`; without it the app cannot start) and `libpulse0`,
  `libpulse-mainloop-glib0`.

## Extension points

- Silent auto-confirm agent: **implemented** in `src/BtAgent.{h,cpp}` and
  registered as the default agent in `BluezManager::registerAgent()` (called
  from `start()`). It makes `Pair()` produce a real authenticated, mutual bond
  without the Cinnamon agent — which is what lets multipoint earbuds (e.g.
  realme Buds) store this host and directed-advertise for auto-reconnect.
  `beginConnectSequence()` sets `Adapter1.Pairable=true` before `Pair()`. No
  explicit `UnregisterAgent` on exit: BlueZ drops the agent when our D-Bus
  connection closes.

## Gotchas

- Qt6Gui's CMake config hard-requires the OpenGL headers (`GL/gl.h`, package
  `libgl-dev`). On Ubuntu 22.04 `qt6-base-dev` does not pull it in, so CI and
  the README install it explicitly — keep it there.
- Include the libpulse headers before any Qt header in a translation unit
  (`pulse/glib-mainloop.h` pulls in GLib, which must not see Qt's
  `signals`/`slots` macros).

- `organizationName` and `applicationName` are both
  `bluetooth-headphones-manager`, while `applicationDisplayName` is
  `Bluetooth Headphones Manager`. The matching organization/application IDs
  double `AppLocalDataLocation`; the logger deliberately uses
  `GenericDataLocation + "/bluetooth-headphones-manager"` to keep a single
  directory.
- The app sets `setQuitOnLastWindowClosed(false)`; closing the settings window
  hides it (and stops discovery) rather than quitting. Quit is via the tray.
- SVG icons are functional placeholders meant to be replaced; keep the four
  filenames/qrc aliases stable so code references stay valid.
