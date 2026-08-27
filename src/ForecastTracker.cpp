#include "ForecastTracker.h"
#include <QtGlobal>
#include <QtMath>

ForecastTracker& ForecastTracker::instance()
{
    static ForecastTracker inst;
    return inst;
}

bool ForecastTracker::isHit(double predicted, double actual) const
{
    if (predicted <= 0.0 || actual <= 0.0)
        return false;
    const double tol = qMax(0.30, predicted * 0.0005); // 0.3 元或 0.05%
    return qAbs(actual - predicted) <= tol;
}

void ForecastTracker::recordPrediction(const QDateTime& madeAt, int horizonSec,
                                       double predictedPrice, double basePrice,
                                       const QString& mode)
{
    if (predictedPrice <= 0.0 || horizonSec <= 0)
        return;

    // 避免短时间重复登记（30 秒内只记一次）
    if (!m_pending.isEmpty()) {
        const auto& last = m_pending.last();
        if (last.madeAt.isValid() && last.madeAt.secsTo(madeAt) < 30)
            return;
    }

    Pending p;
    p.madeAt = madeAt;
    p.matureAt = madeAt.addSecs(horizonSec);
    p.predictedPrice = predictedPrice;
    p.basePrice = basePrice;
    p.mode = mode;
    m_pending.append(p);

    // 防止堆积
    while (m_pending.size() > 200)
        m_pending.removeFirst();
}

void ForecastTracker::evaluateWithActual(double actualPrice, const QDateTime& now)
{
    if (actualPrice <= 0.0 || m_pending.isEmpty())
        return;

    QVector<Pending> remain;
    remain.reserve(m_pending.size());
    for (const auto& p : m_pending) {
        if (now < p.matureAt) {
            remain.append(p);
            continue;
        }
        if (isHit(p.predictedPrice, actualPrice))
            ++m_hits;
        else
            ++m_misses;
    }
    m_pending.swap(remain);
}

double ForecastTracker::hitRatePercent() const
{
    const int n = m_hits + m_misses;
    if (n <= 0)
        return 0.0;
    return 100.0 * static_cast<double>(m_hits) / static_cast<double>(n);
}
