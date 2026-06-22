#pragma once

#include <QIcon>

// Central place for the application icons. Each accessor first tries the SVG
// resource and, if the SVG icon engine is unavailable at runtime, falls back
// to drawing the icon with QPainter so the tray always renders correctly.
namespace Icons {
    QIcon tray(bool connected, int batteryPercent = -1); // colour + battery gauge
    QIcon audio();                // small badge for audio devices
    QIcon app();                  // application / window icon
}
