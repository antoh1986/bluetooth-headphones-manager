#pragma once

#include <QString>

// Simple file logger. init() installs a Qt message handler so every
// qDebug/qInfo/qWarning/qCritical is timestamped and appended to a log
// file under the user's data directory (and echoed to stderr).
namespace Logger {
    void    init();
    QString logFilePath();
}
