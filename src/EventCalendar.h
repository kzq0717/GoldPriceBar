#ifndef EVENTCALENDAR_H
#define EVENTCALENDAR_H

#include <QString>
#include <QDate>
#include <QVector>

/** 轻量宏观/贵金属相关日程（本地表，非实时联网） */
class EventCalendar
{
public:
    struct Event {
        QDate date;
        QString title;
        int impact = 2; // 1低 2中 3高
    };

    static QVector<Event> eventsNear(const QDate& around, int daysBefore = 1, int daysAfter = 7);
    static QString summaryNear(const QDate& around = QDate::currentDate());
    static bool isHighImpactDay(const QDate& d = QDate::currentDate());
    /** 今日或明日有高影响事件则返回提醒文案，否则空 */
    static QString pendingAlertText();
};

#endif
