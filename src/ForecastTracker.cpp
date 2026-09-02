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
    const double tol = qMax(0.25, predicted * 0.0004);
    return qAbs(actual - predicted) <= tol;
}

double ForecastTracker::priceNear(const QVector<QPair<QDateTime, double>>& pts,
                                  const QDateTime& t, double fallback) const
{
    if (pts.isEmpty())
        return fallback;
    int best = -1;
    qint64 bestDist = 0;
    const qint64 target = t.toSecsSinceEpoch();
    for (int i = 0; i < pts.size(); ++i) {
        const qint64 d = qAbs(pts.at(i).first.toSecsSinceEpoch() - target);
        if (best < 0 || d < bestDist) {
            best = i;
            bestDist = d;
        }
    }
    // 到期点前后 90 秒内才采信历史点，否则用兜底现价
    if (best >= 0 && bestDist <= 90)
        return pts.at(best).second;
    return fallback;
}

void ForecastTracker::recordPrediction(const QDateTime& madeAt, int horizonSec,
                                       double predictedPrice, double basePrice,
                                       const QString& mode)
{
    if (predictedPrice <= 0.0 || horizonSec <= 0)
        return;

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

    while (m_pending.size() > 200)
        m_pending.removeFirst();
}

void ForecastTracker::evaluateWithActual(double actualPrice,
                                         const QVector<QPair<QDateTime, double>>& recentPoints,
                                         const QDateTime& now)
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
        const double actual = priceNear(recentPoints, p.matureAt, actualPrice);
        m_absErrorSum += qAbs(actual - p.predictedPrice);
        if (isHit(p.predictedPrice, actual))
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

double ForecastTracker::meanAbsError() const
{
    const int n = m_hits + m_misses;
    if (n <= 0)
        return 0.0;
    return m_absErrorSum / static_cast<double>(n);
}
