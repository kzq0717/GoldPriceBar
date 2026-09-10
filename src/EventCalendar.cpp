#include "EventCalendar.h"

static QVector<EventCalendar::Event> allEvents()
{
    // 精简表：2026 主要美联储/非农/CPI 窗口（示意，可随版本更新）
    // impact: 3=高波动预期
    QVector<EventCalendar::Event> e;
    auto add = [&](int y, int m, int d, const QString& t, int imp) {
        e.append({QDate(y, m, d), t, imp});
    };
    // 2026 非农大致首周五（近似）
    add(2026, 1, 9, QStringLiteral("美国非农"), 3);
    add(2026, 2, 6, QStringLiteral("美国非农"), 3);
    add(2026, 3, 6, QStringLiteral("美国非农"), 3);
    add(2026, 4, 3, QStringLiteral("美国非农"), 3);
    add(2026, 5, 8, QStringLiteral("美国非农"), 3);
    add(2026, 6, 5, QStringLiteral("美国非农"), 3);
    add(2026, 7, 2, QStringLiteral("美国非农"), 3);
    add(2026, 8, 7, QStringLiteral("美国非农"), 3);
    add(2026, 9, 4, QStringLiteral("美国非农"), 3);
    add(2026, 10, 2, QStringLiteral("美国非农"), 3);
    add(2026, 11, 6, QStringLiteral("美国非农"), 3);
    add(2026, 12, 4, QStringLiteral("美国非农"), 3);
    // FOMC 决议日（2026 公开日程近似）
    add(2026, 1, 28, QStringLiteral("FOMC利率决议"), 3);
    add(2026, 3, 18, QStringLiteral("FOMC利率决议"), 3);
    add(2026, 5, 6, QStringLiteral("FOMC利率决议"), 3);
    add(2026, 6, 17, QStringLiteral("FOMC利率决议"), 3);
    add(2026, 7, 29, QStringLiteral("FOMC利率决议"), 3);
    add(2026, 9, 16, QStringLiteral("FOMC利率决议"), 3);
    add(2026, 11, 4, QStringLiteral("FOMC利率决议"), 3);
    add(2026, 12, 16, QStringLiteral("FOMC利率决议"), 3);
    // CPI 中旬窗口
    add(2026, 1, 14, QStringLiteral("美国CPI"), 3);
    add(2026, 2, 11, QStringLiteral("美国CPI"), 3);
    add(2026, 3, 11, QStringLiteral("美国CPI"), 3);
    add(2026, 4, 10, QStringLiteral("美国CPI"), 3);
    add(2026, 5, 13, QStringLiteral("美国CPI"), 3);
    add(2026, 6, 10, QStringLiteral("美国CPI"), 3);
    add(2026, 7, 14, QStringLiteral("美国CPI"), 3);
    add(2026, 8, 12, QStringLiteral("美国CPI"), 3);
    add(2026, 9, 10, QStringLiteral("美国CPI"), 3);
    add(2026, 10, 14, QStringLiteral("美国CPI"), 3);
    add(2026, 11, 12, QStringLiteral("美国CPI"), 3);
    add(2026, 12, 10, QStringLiteral("美国CPI"), 3);
    // 国内参考
    add(2026, 3, 5, QStringLiteral("两会开幕(参考)"), 2);
    add(2026, 10, 1, QStringLiteral("国庆假期(流动性)"), 2);
    return e;
}

QVector<EventCalendar::Event> EventCalendar::eventsNear(const QDate& around, int daysBefore, int daysAfter)
{
    QVector<Event> out;
    const QDate a = around.addDays(-daysBefore);
    const QDate b = around.addDays(daysAfter);
    for (const auto& e : allEvents()) {
        if (e.date >= a && e.date <= b)
            out.append(e);
    }
    return out;
}

QString EventCalendar::summaryNear(const QDate& around)
{
    const auto list = eventsNear(around, 0, 10);
    if (list.isEmpty())
        return QStringLiteral("近10日无内置高波动日程");
    QString s;
    for (const auto& e : list) {
        const QString tag = e.impact >= 3 ? QStringLiteral("★") : QStringLiteral("·");
        s += QStringLiteral("%1 %2 %3\n").arg(e.date.toString(QStringLiteral("MM-dd")), tag, e.title);
    }
    return s.trimmed();
}

bool EventCalendar::isHighImpactDay(const QDate& d)
{
    for (const auto& e : allEvents()) {
        if (e.date == d && e.impact >= 3)
            return true;
    }
    return false;
}

QString EventCalendar::pendingAlertText()
{
    const QDate today = QDate::currentDate();
    const QDate tomorrow = today.addDays(1);
    QStringList parts;
    for (const auto& e : allEvents()) {
        if (e.impact < 3)
            continue;
        if (e.date == today)
            parts << QStringLiteral("今日 %1").arg(e.title);
        else if (e.date == tomorrow)
            parts << QStringLiteral("明日 %1").arg(e.title);
    }
    return parts.join(QStringLiteral("；"));
}
