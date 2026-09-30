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

    /** 当前时间是否处于重磅数据公布前后的双向插针避险静默期（如非农/CPI 20:15~20:50，FOMC 01:50~02:45） */
    static bool isEventBlackoutWindow(const QDateTime& dt = QDateTime::currentDateTime());

    /** 若处于避险窗口或临近30分钟内，返回避险预警提示文本，否则返回空字符串 */
    static QString currentEventWarning(const QDateTime& dt = QDateTime::currentDateTime());
};

#endif
