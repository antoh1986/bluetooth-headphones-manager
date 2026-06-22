#pragma once

#include <QWidget>

class QTimer;

// Small indeterminate "busy" spinner: a rotating arc that signals a background
// operation (Bluetooth discovery) is in progress. Lightweight -- it only
// repaints while spinning, and hides itself when stopped.
class BusyIndicator : public QWidget
{
    Q_OBJECT
public:
    explicit BusyIndicator(QWidget *parent = nullptr);

    bool isSpinning() const;

public slots:
    void start();
    void stop();
    void setSpinning(bool on);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QTimer *m_timer = nullptr;
    int     m_angle = 0;   // current rotation, degrees
};
