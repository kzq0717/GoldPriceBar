#include "Logger.h"
#include "AppSettings.h"

#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QDateTime>
#include <QDate>
#include <QMutex>
#include <QMutexLocker>
#include <QStandardPaths>
#include <cstdio>

static QMutex s_logMutex;
static QString s_logFile;
static bool s_inited = false;

QString Logger::logDir()
{
    QString base = AppSettings::instance().resolvedDatabaseDir();
    if (base.isEmpty())
        base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return QDir(base).filePath(QStringLiteral("logs"));
}

QString Logger::logFilePath()
{
    if (!s_logFile.isEmpty())
        return s_logFile;
    const QString name = QStringLiteral("app-%1.log")
                             .arg(QDate::currentDate().toString(QStringLiteral("yyyyMMdd")));
    return QDir(logDir()).filePath(name);
}

void Logger::init()
{
    QMutexLocker locker(&s_logMutex);
    if (s_inited)
        return;

    const QString dir = logDir();
    QDir().mkpath(dir);
    s_logFile = QDir(dir).filePath(
        QStringLiteral("app-%1.log")
            .arg(QDate::currentDate().toString(QStringLiteral("yyyyMMdd"))));
    s_inited = true;
    locker.unlock();

    qInstallMessageHandler(&Logger::messageHandler);
    info(QStringLiteral("======== Logger started ========"));
    info(QStringLiteral("Log file: %1").arg(s_logFile));
}

void Logger::write(const QString& level, const QString& msg)
{
    QMutexLocker locker(&s_logMutex);
    if (!s_inited) {
        const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                            + QStringLiteral("/logs");
        QDir().mkpath(dir);
        s_logFile = QDir(dir).filePath(
            QStringLiteral("app-%1.log")
                .arg(QDate::currentDate().toString(QStringLiteral("yyyyMMdd"))));
        s_inited = true;
    }

    QFile f(s_logFile);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
        return;

    QTextStream ts(&f);
    ts << QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd hh:mm:ss.zzz"))
       << " [" << level << "] " << msg << '\n';
    ts.flush();
    f.close();
}

void Logger::info(const QString& msg)  { write(QStringLiteral("INFO"), msg); }
void Logger::warn(const QString& msg)  { write(QStringLiteral("WARN"), msg); }
void Logger::error(const QString& msg) { write(QStringLiteral("ERROR"), msg); }

void Logger::messageHandler(QtMsgType type, const QMessageLogContext& ctx, const QString& msg)
{
    QString level = QStringLiteral("INFO");
    switch (type) {
    case QtDebugMsg:    level = QStringLiteral("DEBUG"); break;
    case QtInfoMsg:     level = QStringLiteral("INFO"); break;
    case QtWarningMsg:  level = QStringLiteral("WARN"); break;
    case QtCriticalMsg: level = QStringLiteral("ERROR"); break;
    case QtFatalMsg:    level = QStringLiteral("FATAL"); break;
    }

    QString line = msg;
    if (ctx.file && ctx.file[0])
        line += QStringLiteral(" (%1:%2)").arg(QString::fromUtf8(ctx.file)).arg(ctx.line);
    write(level, line);
    fprintf(stderr, "%s\n", qPrintable(line));
    fflush(stderr);
    if (type == QtFatalMsg)
        abort();
}
