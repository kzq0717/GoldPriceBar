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
    const double tol = qMax(0.30, predicted * 0.0005);
    return qAbs(actual - predicted) <= tol;
}

void ForecastTracker::recordDayRange(const QDateTime& madeAt, int horizonSec,
                                     double predHigh, double predLow,
                                     const QString& mode)
{
    if (predHigh <= 0.0 || predLow <= 0.0 || predHigh < predLow || horizonSec <= 0)
        return;
    if (!m_pendingRanges.isEmpty()) {
        const auto& last = m_pendingRanges.last();
        if (last.madeAt.isValid() && last.madeAt.secsTo(madeAt) < 60)
            return;
    }
    PendingRange p;
    p.madeAt = madeAt;
    p.matureAt = madeAt.addSecs(horizonSec);
    p.predHigh = predHigh;
    p.predLow = predLow;
    p.mode = mode;
    m_pendingRanges.append(p);
    while (m_pendingRanges.size() > 100)
        m_pendingRanges.removeFirst();
}

void ForecastTracker::recordPrediction(const QDateTime& madeAt, int horizonSec,
                                       double predictedPrice, double basePrice,
                                       const QString& mode)
{
    Q_UNUSED(basePrice);
    // 旧接口：无法拆高低，记为对称窄区间
    if (predictedPrice <= 0.0)
        return;
    recordDayRange(madeAt, horizonSec, predictedPrice, predictedPrice * 0.999, mode);
}

void ForecastTracker::evaluateWithActual(double actualPrice,
                                         const QVector<QPair<QDateTime, double>>& recentPoints,
                                         const QDateTime& now)
{
    Q_UNUSED(recentPoints);
    if (actualPrice <= 0.0)
        return;
    // 单点路径：用现价近似今高/低不准确，优先等 evaluateDayRange
    double h = actualPrice, l = actualPrice;
    for (const auto& pt : recentPoints) {
        h = qMax(h, pt.second);
        l = qMin(l, pt.second);
    }
    evaluateDayRange(h, l, now);
}

void ForecastTracker::evaluateDayRange(double actualHigh, double actualLow,
                                       const QDateTime& now)
{
    if (actualHigh <= 0.0 || actualLow <= 0.0 || m_pendingRanges.isEmpty())
        return;

    QVector<PendingRange> remain;
    for (const auto& p : m_pendingRanges) {
        if (now < p.matureAt) {
            remain.append(p);
            continue;
        }
        m_absErrorSum += qAbs(actualHigh - p.predHigh) + qAbs(actualLow - p.predLow);
        m_errCount += 2;
        if (isHit(p.predHigh, actualHigh))
            ++m_highHits;
        else
            ++m_highMisses;
        if (isHit(p.predLow, actualLow))
            ++m_lowHits;
        else
            ++m_lowMisses;
    }
    m_pendingRanges.swap(remain);
}

double ForecastTracker::hitRatePercent() const
{
    const int n = m_highHits + m_highMisses + m_lowHits + m_lowMisses;
    if (n <= 0)
        return 0.0;
    return 100.0 * static_cast<double>(m_highHits + m_lowHits) / static_cast<double>(n);
}

double ForecastTracker::highHitRatePercent() const
{
    const int n = m_highHits + m_highMisses;
    if (n <= 0) return 0.0;
    return 100.0 * m_highHits / static_cast<double>(n);
}

double ForecastTracker::lowHitRatePercent() const
{
    const int n = m_lowHits + m_lowMisses;
    if (n <= 0) return 0.0;
    return 100.0 * m_lowHits / static_cast<double>(n);
}

double ForecastTracker::meanAbsError() const
{
    if (m_errCount <= 0)
        return 0.0;
    return m_absErrorSum / static_cast<double>(m_errCount);
}
