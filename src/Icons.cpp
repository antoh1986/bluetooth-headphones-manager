#include "Icons.h"

#include <QPixmap>
#include <QPainter>
#include <QPolygonF>
#include <QPointF>
#include <QRectF>
#include <QSize>
#include <QColor>
#include <QPen>
#include <QtGlobal>

namespace {

// The tray glyph is a filled circle with a white Bluetooth rune on top.
// When a battery level is known (batteryPercent >= 0) the circle becomes a
// gauge: black "empty" disc filled from the bottom with blue in proportion to
// the charge (100% = fully blue, 50% = bottom half blue, etc.).
QIcon drawnTray(bool connected, int batteryPercent = -1)
{
    QPixmap pm(64, 64);
    pm.fill(Qt::transparent);

    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);

    // Circle bounding box: x/y [2,62], i.e. centre (32,32), radius 30.
    const QRectF disc(2, 2, 60, 60);
    const QColor blue("#2196F3");

    p.setPen(Qt::NoPen);
    if (!connected) {
        p.setBrush(QColor("#9E9E9E"));
        p.drawEllipse(disc);
    } else if (batteryPercent < 0) {
        // Battery unknown: solid blue, as before.
        p.setBrush(blue);
        p.drawEllipse(disc);
    } else {
        // Battery gauge: black disc, blue fill rising from the bottom.
        const int pct = qBound(0, batteryPercent, 100);
        p.setBrush(QColor("#000000"));
        p.drawEllipse(disc);

        const qreal waterline = disc.bottom() - disc.height() * pct / 100.0;
        p.save();
        p.setClipRect(QRectF(0, waterline, pm.width(), pm.height() - waterline));
        p.setBrush(blue);
        p.drawEllipse(disc);
        p.restore();
    }

    // Thin blue ring around the whole glyph so the dark "empty" part of the
    // gauge still reads as a bounded circle (e.g. on dark tray backgrounds).
    if (connected) {
        QPen ring(blue);
        ring.setWidthF(2.5);
        p.setPen(ring);
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(disc);
    }

    QPen pen(Qt::white);
    pen.setWidthF(4.0);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);

    // The Bluetooth rune as a single poly-line.
    QPolygonF rune;
    rune << QPointF(22, 24) << QPointF(42, 42) << QPointF(32, 50)
         << QPointF(32, 14) << QPointF(42, 24) << QPointF(22, 42);
    p.drawPolyline(rune);
    p.end();

    return QIcon(pm);
}

QIcon drawnAudio()
{
    QPixmap pm(48, 48);
    pm.fill(Qt::transparent);

    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);

    const QColor c("#1565C0");
    QPen pen(c);
    pen.setWidth(4);
    pen.setCapStyle(Qt::RoundCap);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    p.drawArc(QRectF(10, 12, 28, 28), 20 * 16, 140 * 16); // head band

    p.setPen(Qt::NoPen);
    p.setBrush(c);
    p.drawRoundedRect(QRectF(8, 26, 8, 14), 3, 3);   // left ear cup
    p.drawRoundedRect(QRectF(32, 26, 8, 14), 3, 3);  // right ear cup
    p.end();

    return QIcon(pm);
}

bool renderable(const QIcon &icon)
{
    return !icon.isNull() && !icon.pixmap(64, 64).isNull();
}

// Tray hosts that receive the icon as pixmaps -- LXQt's StatusNotifierItem
// sends one per QIcon::availableSizes() entry -- get nothing from an
// SVG-backed icon (it reports no sizes) and draw a generic "unknown" glyph.
// Hand them pre-rendered frames at the usual panel sizes instead.
QIcon rasterized(const QIcon &icon)
{
    QIcon out;
    for (int px : {16, 22, 24, 32, 48, 64})
        out.addPixmap(icon.pixmap(QSize(px, px), 1.0));
    return out;
}

} // namespace

namespace Icons {

QIcon tray(bool connected, int batteryPercent)
{
    // Connected always goes through the painter so the battery gauge can be
    // drawn; at full/unknown charge it is pixel-identical to the SVG anyway.
    if (connected)
        return rasterized(drawnTray(true, batteryPercent));

    QIcon icon(QStringLiteral(":/icons/bt-disconnected.svg"));
    return rasterized(renderable(icon) ? icon : drawnTray(false));
}

QIcon audio()
{
    QIcon icon(QStringLiteral(":/icons/audio.svg"));
    return renderable(icon) ? icon : drawnAudio();
}

QIcon app()
{
    QIcon icon(QStringLiteral(":/icons/bluetooth-headphones-manager.svg"));
    return renderable(icon) ? icon : drawnTray(true);
}

} // namespace Icons
