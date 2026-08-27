#ifndef FORECASTTRACKER_H
#define FORECASTTRACKER_H

#include <QDateTime>
#include <QVector>

/**
 * @brief 记录预测并在到期后与真实价格比对，统计命中率
 * 命中条件：|实际价 - 预测价| <= max(0.30, 预测价 * 0.0005)
 */
class ForecastTracker
{
public:
    static ForecastTracker& instance();

    struct Pending {
        QDateTime madeAt;
        QDateTime matureAt;
        double predictedPrice = 0.0;
        double basePrice = 0.0;
        QString mode;
    };

    void recordPrediction(const QDateTime& madeAt, int horizonSec,
                          double predictedPrice, double basePrice,
                          const QString& mode);

    /** 用当前价结算已到期的预测 */
    void evaluateWithActual(double actualPrice, const QDateTime& now = QDateTime::currentDateTime());

    int totalEvaluated() const { return m_hits + m_misses; }
    int hits() const { return m_hits; }
    int misses() const { return m_misses; }
    double hitRatePercent() const;

    int pendingCount() const { return m_pending.size(); }

private:
    ForecastTracker() = default;
    bool isHit(double predicted, double actual) const;

    QVector<Pending> m_pending;
    int m_hits = 0;
    int m_misses = 0;
};

#endif // FORECASTTRACKER_H
