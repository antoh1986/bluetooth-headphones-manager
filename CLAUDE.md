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
  D-Bus calls directly.

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
| `src/BluezManager.{h,cpp}` | The only D-Bus layer. Async wrapper over `org.bluez`. Tracks devices in `QMap<QString path, BtDevice>`. Holds the one-click state machine. Emits `devicesChanged`, `deviceConnected`, `deviceDisconnected(path,name,expected)`, `batteryChanged`, `pairingProgress`, `adapterReady`. |
| `src/BtDevice.h` | Plain value object (snapshot) for a device + `isAudio()` heuristic (icon hint / class major field `0x04` / audio profile UUIDs) + `displayName()` (Alias→Name→Address). |
| `src/BtAgent.{h,cpp}` | Auto-accepting `org.bluez.Agent1` (a `QDBusAbstractAdaptor`). Registered by `BluezManager` as the **default** agent so app-initiated `Pair()` completes a full authenticated bond silently (the "yes" `bluetoothctl` asks for). Every callback accepts; never rejects. Object path `/bluetoothheadphonesmanager/agent` (hyphens are illegal in D-Bus paths). |
| `src/BluezTypes.h` | D-Bus marshalling typedefs `InterfaceList` (`a{sa{sv}}`) and `ManagedObjectList` (`a{oa{sa{sv}}}`); registered with `qDBusRegisterMetaType<>()` in `BluezManager::start()`. |
| `src/TrayApp.{h,cpp}` | `QSystemTrayIcon` + context menu (header = connected device+battery, Settings, Quit), left-click opens Settings, disconnect notifications. |
| `src/SettingsWindow.{h,cpp}` | Two `QListWidget`s (Paired / Available), audio-first, check mark on active device, battery, live progress text, Refresh, "Launch on startup". |
| `src/AutoStart.{h,cpp}` | "Launch on startup" state. Default-on comes from a **system-wide** entry the `.deb` installs at `/etc/xdg/autostart/bluetooth-headphones-manager.desktop` (Exec `--minimized`), so it autostarts after install without launching once. The checkbox toggles a **per-user** override at `~/.config/autostart/bluetooth-headphones-manager.desktop`: unchecking writes `Hidden=true` to suppress the system entry, re-checking removes it. With no system entry (build tree), enabling writes a normal per-user entry pointing at the running binary. |
| `src/Icons.{h,cpp}` | App/tray/audio icons from SVG resources, with a `QPainter` fallback if the SVG icon engine is missing. |
| `src/Logger.{h,cpp}` | Installs a Qt message handler; logs to `~/.local/share/bluetooth-headphones-manager/bluetooth-headphones-manager.log`. |
| `resources/` | `resources.qrc` + placeholder SVG icons (`bt-connected`, `bt-disconnected`, `bluetooth-headphones-manager`, `audio`). |
| `packaging/bluetooth-headphones-manager.desktop` | Installed menu entry (`Categories=AudioVideo;Audio;`). |
| `packaging/bluetooth-headphones-manager-autostart.desktop` | System-wide XDG autostart entry, installed to `/etc/xdg/autostart/bluetooth-headphones-manager.desktop` (renamed to that basename; Exec `--minimized`). Makes launch-on-startup default-on after install; registered as a dpkg conffile. |

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
- **Discovery is windowed:** auto-starts when the settings window opens, runs
  ~60 s (single-shot `m_discoveryTimer` → `stopDiscovery`), restartable via
  Refresh, stopped on window close.
- **Modules:** Core, Gui, Widgets, DBus, **Network** (Network only for the
  single-instance `QLocalServer`). Reflect any module change in both
  `CMakeLists.txt` and the `.deb` `Depends`.
- **`.deb` dependencies** list classic Qt6 names with `*t64` alternatives
  (e.g. `libqt6core6 | libqt6core6t64`) for old and new Mint/Ubuntu/Debian,
  plus `libqt6svg6` (SVG icon engine plugin, loaded at runtime) and
  `qt6-qpa-plugins` (Debian splits the xcb platform plugin out of
  `libqt6gui6`; without it the app cannot start).

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
