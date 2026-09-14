#include "TradingSession.h"
#include "goldsdk/trading_session.hpp"
#include <ctime>

namespace {

QString localizeStatus(const std::string& code)
{
    if (code == "open")
        return QStringLiteral("交易时段");
    if (code == "weekend_closed")
        return QStringLiteral("周末休市");
    if (code == "pre_open")
        return QStringLiteral("开盘前");
    if (code == "closed")
        return QStringLiteral("已收市");
    return QString::fromStdString(code);
}

std::tm toTm(const QDateTime& now)
{
    const QDateTime local = now.isValid() ? now.toLocalTime() : QDateTime::currentDateTime();
    std::tm tm{};
    const int qdow = local.date().dayOfWeek(); // 1=Mon .. 7=Sun
    tm.tm_wday = (qdow == 7) ? 0 : qdow;
    tm.tm_year = local.date().year() - 1900;
    tm.tm_mon = local.date().month() - 1;
    tm.tm_mday = local.date().day();
    tm.tm_hour = local.time().hour();
    tm.tm_min = local.time().minute();
    tm.tm_sec = local.time().second();
    return tm;
}

} // namespace

QString TradingSession::normalizeSource(const QString& src)
{
    return QString::fromStdString(goldsdk::TradingSession::normalizeSource(src.toStdString()));
}

bool TradingSession::isTradingNow(const QString& source, const QDateTime& now)
{
    const std::tm tm = toTm(now);
    return goldsdk::TradingSession::isTradingNow(source.toStdString(), &tm);
}

QString TradingSession::statusText(const QString& source, const QDateTime& now)
{
    const std::tm tm = toTm(now);
    return localizeStatus(goldsdk::TradingSession::statusText(source.toStdString(), &tm));
}

QString TradingSession::hoursDescription(const QString& source)
{
    const std::string src = goldsdk::TradingSession::normalizeSource(source.toStdString());
    if (src == "gj")
        return QStringLiteral("伦敦金约：周一07:00–周六04:00（北京时间，示意）");
    return QStringLiteral("积存金约：工作日 09:00–23:30（北京时间，示意）");
}
