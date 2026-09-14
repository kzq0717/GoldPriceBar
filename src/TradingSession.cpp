#include "TradingSession.h"
#include "goldsdk/trading_session.hpp"
#include <ctime>

QString TradingSession::normalizeSource(const QString& src)
{
    return QString::fromStdString(goldsdk::TradingSession::normalizeSource(src.toStdString()));
}

bool TradingSession::isTradingNow(const QString& source, const QDateTime& now)
{
    const QDateTime local = now.isValid() ? now.toLocalTime() : QDateTime::currentDateTime();
    std::tm tm{};
    tm.tm_year = local.date().year() - 1900;
    tm.tm_mon = local.date().month() - 1;
    tm.tm_mday = local.date().day();
    tm.tm_hour = local.time().hour();
    tm.tm_min = local.time().minute();
    tm.tm_sec = local.time().second();
    tm.tm_wday = local.date().dayOfWeek() % 7; // Qt: 1=Mon..7=Sun → convert
    // Qt dayOfWeek 1=Mon..7=Sun; std tm_wday 0=Sun..6=Sat
    const int qdow = local.date().dayOfWeek();
    tm.tm_wday = (qdow == 7) ? 0 : qdow;
    return goldsdk::TradingSession::isTradingNow(source.toStdString(), &tm);
}

QString TradingSession::statusText(const QString& source, const QDateTime& now)
{
    const QDateTime local = now.isValid() ? now.toLocalTime() : QDateTime::currentDateTime();
    std::tm tm{};
    const int qdow = local.date().dayOfWeek();
    tm.tm_wday = (qdow == 7) ? 0 : qdow;
    tm.tm_hour = local.time().hour();
    tm.tm_min = local.time().minute();
    tm.tm_sec = local.time().second();
    tm.tm_year = local.date().year() - 1900;
    tm.tm_mon = local.date().month() - 1;
    tm.tm_mday = local.date().day();
    return QString::fromStdString(goldsdk::TradingSession::statusText(source.toStdString(), &tm));
}

QString TradingSession::hoursDescription(const QString& source)
{
    return QString::fromStdString(goldsdk::TradingSession::hoursDescription(source.toStdString()));
}
