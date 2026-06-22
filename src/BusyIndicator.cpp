#include "BusyIndicator.h"

#include <QTimer>
#include <QPainter>
#include <QPaintEvent>

BusyIndicator::BusyIndicator(QWidget *parent)
    : QWidget(parent)
{
    setFixedSize(18, 18);
    setVisible(false);   // shown only while spinning

    m_timer = new QTimer(this);
    m_timer->setInterval(80);   // ~12 fps, smooth enough for a spinner
    connect(m_timer, &QTimer::timeout, this, [this] {
        m_angle = (m_angle + 30) % 360;
        update();
    });
}

bool BusyIndicator::isSpinning() const
{
    return m_timer->isActive();
}

void BusyIndicator::start()
{
    if (m_timer->isActive())
        return;
    show();
    m_timer->start();
}

void BusyIndicator::stop()
{
    if (!m_timer->isActive())
        return;
    m_timer->stop();
    hide();
}

void BusyIndicator::setSpinning(bool on)
{
    on ? start() : stop();
}

void BusyIndicator::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const qreal side = qMin(width(), height());
    const qreal penW = qMax<qreal>(2.0, side / 9.0);
    const QRectF rect(penW, penW, side - 2.0 * penW, side - 2.0 * penW);

    QPen pen(palette().highlight().color());
    pen.setWidthF(penW);
    pen.setCapStyle(Qt::RoundCap);
    p.setPen(pen);

    // A ~300° arc with a gap, rotated by the current angle. QPainter angles
    // are in 1/16th of a degree; negative spans sweep clockwise.
    const int startAngle = -m_angle * 16;
    const int spanAngle  = -300 * 16;
    p.drawArc(rect, startAngle, spanAngle);
}
