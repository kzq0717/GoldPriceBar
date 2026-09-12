#include "TradingSession.h"
#include <QLocale>

QString TradingSession::normalizeSource(const QString& src)
{
    const QString s = src.trimmed().toLower();
    if (s == QStringLiteral("xau") || s == QStringLiteral("london"))
        return QStringLiteral("gj");
    if (s == QStringLiteral("ms"))
        return QStringLiteral("ms");
    if (s == QStringLiteral("gj"))
        return QStringLiteral("gj");
    return QStringLiteral("zs");
}

bool TradingSession::isTradingNow(const QString& source, const QDateTime& now)
{
    const QString src = normalizeSource(source);
    // 使用本地系统时间；用户在国内一般即北京时间
    const int dow = now.date().dayOfWeek(); // 1=Mon .. 7=Sun
    const QTime t = now.time();

    if (src == QStringLiteral("gj")) {
        // 周末：周六 04:00 后至周一 07:00 前休市
        if (dow == 7) // Sunday
            return false;
        if (dow == 6) // Saturday
            return t < QTime(4, 0);
        if (dow == 1) // Monday
            return t >= QTime(7, 0);
        return true; // Tue–Fri almost all day
    }

    // 积存金：周末休市；工作日 09:00–23:30
    if (dow == 6 || dow == 7)
        return false;
    return t >= QTime(9, 0) && t <= QTime(23, 30);
}

QString TradingSession::statusText(const QString& source, const QDateTime& now)
{
    const QString src = normalizeSource(source);
    if (isTradingNow(src, now))
        return QStringLiteral("交易时段");
    const int dow = now.date().dayOfWeek();
    if (src == QStringLiteral("gj")) {
        if (dow == 7 || (dow == 6 && now.time() >= QTime(4, 0)))
            return QStringLiteral("周末休市");
        if (dow == 1 && now.time() < QTime(7, 0))
            return QStringLiteral("开盘前");
        return QStringLiteral("非交易时段");
    }
    if (dow == 6 || dow == 7)
        return QStringLiteral("周末休市");
    if (now.time() < QTime(9, 0))
        return QStringLiteral("开盘前");
    return QStringLiteral("已收市");
}

QString TradingSession::hoursDescription(const QString& source)
{
    const QString src = normalizeSource(source);
    if (src == QStringLiteral("gj"))
        return QStringLiteral("伦敦金约：周一07:00–周六04:00（北京时间，示意）");
    return QStringLiteral("积存金约：工作日 09:00–23:30（北京时间，示意）");
}
