#ifndef FORECASTTRACKER_H
#define FORECASTTRACKER_H

#include <QDateTime>
#include <QVector>
#include <QPair>
#include <QString>

/**
 * 日高低预测命中统计：
 * - 到期后用「当日已实现最高/最低」与预测高/低比对
 * - 命中容差：max(0.3元, 预测价×0.05%)
 */
class ForecastTracker
{
public:
    static ForecastTracker& instance();

    struct PendingRange {
        QDateTime madeAt;
        QDateTime matureAt;
        double predHigh = 0.0;
        double predLow = 0.0;
        QString mode;
    };

    void recordDayRange(const QDateTime& madeAt, int horizonSec,
                        double predHigh, double predLow, const QString& mode);

    /** 兼容旧调用：当作中点预测（尽量少用） */
    void recordPrediction(const QDateTime& madeAt, int horizonSec,
                          double predictedPrice, double basePrice,
                          const QString& mode);

    void evaluateWithActual(double actualPrice,
                            const QVector<QPair<QDateTime, double>>& recentPoints,
                            const QDateTime& now = QDateTime::currentDateTime());

    /** 用今高/今低结算区间预测 */
    void evaluateDayRange(double actualHigh, double actualLow,
                          const QDateTime& now = QDateTime::currentDateTime());

    int totalEvaluated() const { return m_highHits + m_highMisses + m_lowHits + m_lowMisses; }
    int hits() const { return m_highHits + m_lowHits; }
    int misses() const { return m_highMisses + m_lowMisses; }
    double hitRatePercent() const;

    int highHits() const { return m_highHits; }
    int highMisses() const { return m_highMisses; }
    int lowHits() const { return m_lowHits; }
    int lowMisses() const { return m_lowMisses; }
    double highHitRatePercent() const;
    double lowHitRatePercent() const;
    double meanAbsError() const;
    int pendingCount() const { return m_pendingRanges.size(); }

private:
    ForecastTracker() = default;
    bool isHit(double predicted, double actual) const;

    QVector<PendingRange> m_pendingRanges;
    int m_highHits = 0;
    int m_highMisses = 0;
    int m_lowHits = 0;
    int m_lowMisses = 0;
    double m_absErrorSum = 0.0;
    int m_errCount = 0;
};

#endif // FORECASTTRACKER_H
