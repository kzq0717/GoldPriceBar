#ifndef TRADINGSESSION_H
#define TRADINGSESSION_H

#include <QString>
#include <QDateTime>
#include <QTime>

/**
 * 交易时段（按北京时间近似，仅作软件提示，非交易所官方时刻）
 * - zs/ms 积存金：工作日 09:00–23:30
 * - gj/xau 伦敦金：周一 07:00 ～ 周六 04:00（周末休市）
 */
class TradingSession
{
public:
    static QString normalizeSource(const QString& src);
    static bool isTradingNow(const QString& source,
                             const QDateTime& now = QDateTime::currentDateTime());
    /** 简短状态：交易中 / 已休市 / 周末… */
    static QString statusText(const QString& source,
                              const QDateTime& now = QDateTime::currentDateTime());
    /** 时段说明 */
    static QString hoursDescription(const QString& source);
};

#endif
