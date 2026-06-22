
# Вариант 1 — просто запустить готовый бинарник:


cd /home/anton/1/workspace_linux/bluetooth-headphones-manager
./build/bluetooth-headphones-manager
Откроется окно настроек, и в трее появится иконка. Чтобы стартовать сразу свёрнутым в трей (как при автозапуске):


./build/bluetooth-headphones-manager --minimized
# Вариант 2 — пересобрать с нуля и запустить:


cd /home/anton/1/workspace_linux/bluetooth-headphones-manager
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
./build/bluetooth-headphones-manager
# Вариант 3 — установить как .deb (появится в меню «Пуск» и в автозапуске):


sudo apt install ./bluetooth-headphones-manager_1.1.0_amd64.deb
bluetooth-headphones-manager


# Лог пишется в ~/.local/share/bluetooth-headphones-manager/bluetooth-headphones-manager.log



##  Bluetooth Headphones Manager

A lightweight **system tray** application for **Linux Mint / Cinnamon** that
manages Bluetooth audio devices with an "Android-like" one-click experience:
click a device and it is automatically **paired → trusted → connected** — no
manual `pair` / `trust` / `connect` steps.

It talks to **BlueZ over D-Bus** (system bus) natively through Qt's `QtDBus`
module — it never shells out to `bluetoothctl`.

* Tray icon whose colour reflects the connection state (blue = connected,
  grey = not connected).
* Right-click menu showing the currently connected device + battery level,
  **Settings** and **Quit**. Left-click opens **Settings**.
* Settings window with two lists — **Paired** and **Available** — audio
  devices shown first, a check mark on the active device, battery levels, and
  live per-device progress (`Pairing… / Connecting… / Connected / Failed`).
* Automatic discovery while the settings window is open (auto-stops after
  60 s and on close) plus a manual **Refresh** button.
* Desktop notification when a connected device unexpectedly drops.
* **Launch on startup** (XDG autostart, enabled by default), starts minimized
  to tray.
* Single-instance; writes a log file.

## Requirements

* Qt 5.15 (Core, Gui, Widgets, DBus, Network) + the SVG plugin
* CMake ≥ 3.16, a C++17 compiler (GCC)
* BlueZ running on the system bus (the standard `bluez` package)

On Linux Mint / Ubuntu the build dependencies are:

```bash
sudo apt install build-essential cmake qtbase5-dev libqt5svg5-dev
```

## Build (CMake)

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

The binary is produced at `build/bluetooth-headphones-manager`. Run it directly:

```bash
./build/bluetooth-headphones-manager             # opens the settings window
./build/bluetooth-headphones-manager --minimized # starts hidden in the tray
```

## Build a `.deb` package

```bash
./build_deb.sh            # configures, builds and packages
./build_deb.sh clean      # removes build artefacts and .deb files
```

This produces `bluetooth-headphones-manager_1.1.0_amd64.deb` containing:

| Path | Purpose |
|------|---------|
| `/usr/bin/bluetooth-headphones-manager` | the executable |
| `/usr/share/applications/bluetooth-headphones-manager.desktop` | menu entry (`AudioVideo;Audio;`) |
| `/usr/share/icons/hicolor/scalable/apps/bluetooth-headphones-manager.svg` | application icon |

The package declares dependencies on the system Qt 5 runtime
(`libqt5core5a`, `libqt5gui5`, `libqt5widgets5`, `libqt5dbus5`,
`libqt5network5`, `libqt5svg5` — with `*t64` alternatives for newer
Mint/Ubuntu releases) and `bluez`.

## Install

```bash
sudo apt install ./bluetooth-headphones-manager_1.1.0_amd64.deb
```

`apt` pulls in any missing Qt libraries from the distro repositories. After
installation the app appears in the menu under **Sound & Video** and
starts automatically on the next login (minimized to tray).

To remove it:

```bash
sudo apt remove bluetooth-headphones-manager
```

## Usage

* **Left-click** the tray icon (or pick **Settings**) to open the window.
* **Click any device** to connect to it. New devices are paired, trusted and
  connected automatically; if another device is connected it is disconnected
  first.
* Toggle **Launch on startup** to enable/disable the autostart entry
  (`~/.config/autostart/bluetooth-headphones-manager.desktop`).
* **Quit** from the tray menu exits the application.

## Log file

Runtime activity (and any pairing/connection errors) is logged to:

```
~/.local/share/bluetooth-headphones-manager/bluetooth-headphones-manager.log
```

## Project layout

```
.
├── CMakeLists.txt
├── build_deb.sh                 # CMake build + dpkg-deb packaging
├── README.md
├── packaging/
│   ├── bluetooth-headphones-manager.desktop
│   └── bluetooth-headphones-manager-autostart.desktop
├── resources/
│   ├── resources.qrc
│   └── icons/                   # SVG placeholders (replaceable)
│       ├── bt-connected.svg
│       ├── bt-disconnected.svg
│       ├── bluetooth-headphones-manager.svg
│       └── audio.svg
└── src/
    ├── main.cpp                 # entry point, single-instance, wiring
    ├── BluezManager.{h,cpp}     # async BlueZ/D-Bus wrapper (signals/slots)
    ├── BtDevice.h               # device value object + audio detection
    ├── BluezTypes.h             # D-Bus ObjectManager marshalling types
    ├── TrayApp.{h,cpp}          # tray icon, menu, notifications
    ├── SettingsWindow.{h,cpp}   # the two device lists + controls
    ├── AutoStart.{h,cpp}        # XDG autostart entry management
    ├── Icons.{h,cpp}            # SVG icons with QPainter fallback
    └── Logger.{h,cpp}           # file logger
```

## Notes on the authentication agent

The base version relies on the **system (Cinnamon) Bluetooth agent**, which
handles "Just Works" pairing silently and shows a native dialog for
PIN/passkey devices. If you need fully silent auto-confirmation you can
register your own `org.bluez.Agent1` — see the marked stub at the bottom of
`src/BluezManager.cpp`.
