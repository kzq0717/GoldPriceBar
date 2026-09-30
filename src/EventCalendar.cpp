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

static bool getEventTimeWindow(const EventCalendar::Event& e, const QDate& curDate,
                               int& startM, int& endM, QString& timeDesc)
{
    const int month = curDate.month();
    const bool isDst = (month >= 4 && month <= 10); // 夏令时判断

    if (e.title.contains(QStringLiteral("非农")) || e.title.contains(QStringLiteral("CPI"))) {
        if (e.date == curDate) {
            if (isDst) {
                startM = 20 * 60 + 15; // 20:15
                endM = 20 * 60 + 45;   // 20:45
                timeDesc = QStringLiteral("20:30");
            } else {
                startM = 21 * 60 + 15; // 21:15
                endM = 21 * 60 + 45;   // 21:45
                timeDesc = QStringLiteral("21:30");
            }
            return true;
        }
    } else if (e.title.contains(QStringLiteral("FOMC"))) {
        // 美联储决议北京时间通常在日程当天深夜或次日凌晨 (02:00/03:00)
        if (e.date == curDate || e.date.addDays(1) == curDate) {
            if (isDst) {
                startM = 1 * 60 + 50;  // 01:50
                endM = 2 * 60 + 45;    // 02:45
                timeDesc = QStringLiteral("02:00");
            } else {
                startM = 2 * 60 + 50;  // 02:50
                endM = 3 * 60 + 45;    // 03:45
                timeDesc = QStringLiteral("03:00");
            }
            return true;
        }
    }
    return false;
}

bool EventCalendar::isEventBlackoutWindow(const QDateTime& dt)
{
    const QDate curDate = dt.date();
    const int curM = dt.time().hour() * 60 + dt.time().minute();
    for (const auto& e : allEvents()) {
        if (e.impact < 3) continue;
        int startM = 0, endM = 0;
        QString timeDesc;
        if (getEventTimeWindow(e, curDate, startM, endM, timeDesc)) {
            if (curM >= startM && curM <= endM)
                return true;
        }
    }
    return false;
}

QString EventCalendar::currentEventWarning(const QDateTime& dt)
{
    const QDate curDate = dt.date();
    const int curM = dt.time().hour() * 60 + dt.time().minute();
    for (const auto& e : allEvents()) {
        if (e.impact < 3) continue;
        int startM = 0, endM = 0;
        QString timeDesc;
        if (getEventTimeWindow(e, curDate, startM, endM, timeDesc)) {
            if (curM >= startM && curM <= endM) {
                return QStringLiteral("⚠️ 重磅数据避险期 (%1 %2公布)：谨防流动性抽离与双向插针，暂停追单！")
                    .arg(e.title, timeDesc);
            }
            // 提前 30 分钟预警
            if (curM >= startM - 30 && curM < startM) {
                const int remain = startM - curM;
                return QStringLiteral("⏳ 重磅数据临近 (%1 %2)：距数据落地约 %3 分钟，注意提前收拢多头防守线。")
                    .arg(e.title, timeDesc).arg(remain);
            }
        }
    }
    return QString();
}
