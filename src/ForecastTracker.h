#ifndef FORECASTTRACKER_H
#define FORECASTTRACKER_H

#include <QDateTime>
#include <QVector>
#include <QPair>

/**
 * @brief 记录预测并在到期后与真实价格比对，统计命中率
 * 命中：|实际 − 预测| <= max(0.25元, 预测价×0.04%)
 * 结算时优先使用「到期时刻附近」的历史点，避免用更晚的现价误判
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

    /** 用分时点结算；actualPrice 作兜底 */
    void evaluateWithActual(double actualPrice,
                            const QVector<QPair<QDateTime, double>>& recentPoints,
                            const QDateTime& now = QDateTime::currentDateTime());

    int totalEvaluated() const { return m_hits + m_misses; }
    int hits() const { return m_hits; }
    int misses() const { return m_misses; }
    double hitRatePercent() const;
    double meanAbsError() const; // 平均绝对误差（元）
    int pendingCount() const { return m_pending.size(); }

private:
    ForecastTracker() = default;
    bool isHit(double predicted, double actual) const;
    double priceNear(const QVector<QPair<QDateTime, double>>& pts,
                     const QDateTime& t, double fallback) const;

    QVector<Pending> m_pending;
    int m_hits = 0;
    int m_misses = 0;
    double m_absErrorSum = 0.0;
};

#endif // FORECASTTRACKER_H
