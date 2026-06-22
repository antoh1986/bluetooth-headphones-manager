cmake_minimum_required(VERSION 3.16)

# The project version lives in the top-level VERSION file (single source of
# truth). build_deb.sh auto-bumps it (minor) on each package build; CMake feeds
# it to the executable (BLUETOOTH_HEADPHONES_MANAGER_VERSION -> GUI footer) and
# the .deb. CMAKE_CURRENT_LIST_DIR is valid here, before the first project().
file(STRINGS "${CMAKE_CURRENT_LIST_DIR}/VERSION" _bhm_version LIMIT_COUNT 1)
string(STRIP "${_bhm_version}" _bhm_version)
if(NOT _bhm_version MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
    message(FATAL_ERROR
        "VERSION file must contain MAJOR.MINOR.PATCH (got '${_bhm_version}')")
endif()

project(bluetooth-headphones-manager
    VERSION ${_bhm_version}
    DESCRIPTION "One-click Bluetooth audio device manager and system tray app for Linux Mint/Cinnamon"
    HOMEPAGE_URL "https://github.com/antoh1986/bluetooth-headphones-manager"
    LANGUAGES CXX)

# Use the canonical repository slug for every technical identifier.

# Keep the CMake project version as the single source of truth.  The package
# builder reads this generated file after configuring the project.
file(GENERATE
    OUTPUT "${CMAKE_BINARY_DIR}/project-version.txt"
    CONTENT "${PROJECT_VERSION}\n")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Qt code generators
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# Qt 5.15 (Network is needed for the QLocalServer single-instance guard).
find_package(Qt5 5.15 REQUIRED COMPONENTS Core Gui Widgets DBus Network)

set(SOURCES
    src/main.cpp
    src/Logger.cpp
    src/Icons.cpp
    src/AutoStart.cpp
    src/BluezManager.cpp
    src/BtAgent.cpp
    src/SettingsWindow.cpp
    src/BusyIndicator.cpp
    src/TrayApp.cpp
    resources/resources.qrc
)

set(HEADERS
    src/Logger.h
    src/Icons.h
    src/AutoStart.h
    src/BluezTypes.h
    src/BtDevice.h
    src/BluezManager.h
    src/BtAgent.h
    src/SettingsWindow.h
    src/BusyIndicator.h
    src/TrayApp.h
)

add_executable(bluetooth-headphones-manager ${SOURCES} ${HEADERS})

target_compile_options(bluetooth-headphones-manager PRIVATE -Wall -Wextra)

target_compile_definitions(bluetooth-headphones-manager PRIVATE
    BLUETOOTH_HEADPHONES_MANAGER_VERSION="${PROJECT_VERSION}")

target_link_libraries(bluetooth-headphones-manager PRIVATE
    Qt5::Core
    Qt5::Gui
    Qt5::Widgets
    Qt5::DBus
    Qt5::Network
)

# ---------------------------------------------------------------------------
# Installation (used both by `make install` and by the .deb staging step)
# ---------------------------------------------------------------------------
include(GNUInstallDirs)

install(TARGETS bluetooth-headphones-manager
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

install(FILES packaging/bluetooth-headphones-manager.desktop
    DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/applications)

# System-wide XDG autostart entry -> "launch on startup" defaults to on after
# install. Renamed to match the per-user entry's basename so a per-user
# override can suppress it.
install(FILES packaging/bluetooth-headphones-manager-autostart.desktop
    DESTINATION ${CMAKE_INSTALL_SYSCONFDIR}/xdg/autostart
    RENAME bluetooth-headphones-manager.desktop)

install(FILES resources/icons/bluetooth-headphones-manager.svg
    DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/scalable/apps)
