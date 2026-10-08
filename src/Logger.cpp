#include "Logger.h"

#include <QFile>
#include <QDir>
#include <QMutex>
#include <QTextStream>
#include <QDateTime>
#include <QStandardPaths>
#include <QtGlobal>

#include <cstdio>

namespace {
QFile         g_logFile;
QMutex        g_mutex;
QtMessageHandler g_previousHandler = nullptr;
QString       g_path;

const char *levelName(QtMsgType type)
{
    switch (type) {
    case QtDebugMsg:    return "DEBUG";
    case QtInfoMsg:     return "INFO ";
    case QtWarningMsg:  return "WARN ";
    case QtCriticalMsg: return "ERROR";
    case QtFatalMsg:    return "FATAL";
    }
    return "?????";
}

void messageHandler(QtMsgType type, const QMessageLogContext &ctx, const QString &msg)
{
    const QString line = QStringLiteral("%1 [%2] %3")
        .arg(QDateTime::currentDateTime().toString(Qt::ISODateWithMs))
        .arg(levelName(type))
        .arg(msg);

    {
        QMutexLocker locker(&g_mutex);
        if (g_logFile.isOpen()) {
            QTextStream ts(&g_logFile);
            ts << line << '\n';
            ts.flush();
        }
    }

    // Also keep stderr output for when the app is run from a terminal.
    fprintf(stderr, "%s\n", qPrintable(line));
    fflush(stderr);

    if (type == QtFatalMsg)
        abort();

    Q_UNUSED(ctx);
}
} // namespace

namespace Logger {

void init()
{
    // GenericDataLocation == ~/.local/share ; keep a single app subdir so the
    // path does not get doubled when org and application names are identical.
    const QString dir =
        QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
        + QStringLiteral("/bluetooth-headphones-manager");
    QDir().mkpath(dir);

    g_path = dir + QStringLiteral("/bluetooth-headphones-manager.log");
    g_logFile.setFileName(g_path);
    // Not fatal: messageHandler() still writes to stderr.
    if (!g_logFile.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
        fprintf(stderr, "Cannot open log file %s\n", qPrintable(g_path));

    g_previousHandler = qInstallMessageHandler(messageHandler);
}

QString logFilePath()
{
    return g_path;
}

} // namespace Logger
