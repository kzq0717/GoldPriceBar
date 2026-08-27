#ifndef LOGGER_H
#define LOGGER_H

#include <QString>

class Logger
{
public:
    static void init();
    static void info(const QString& msg);
    static void warn(const QString& msg);
    static void error(const QString& msg);
    static QString logDir();
    static QString logFilePath();

private:
    static void write(const QString& level, const QString& msg);
    static void messageHandler(QtMsgType type, const QMessageLogContext& ctx, const QString& msg);
};

#endif // LOGGER_H
