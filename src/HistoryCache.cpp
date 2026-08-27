#include "HistoryCache.h"
#include "ExtremeDatabase.h"

#include <QDate>
#include <QtGlobal>
#include <algorithm>

HistoryCache& HistoryCache::instance()
{
    static HistoryCache inst;
    return inst;
}

HistoryCache::HistoryCache(QObject* parent)
    : QObject(parent)
{
    m_currentDate = QDate::currentDate();
    m_points.reserve(1024);
}

void HistoryCache::append(const QDateTime& time, double price)
{
    ensureToday();

    if (!m_points.isEmpty()) {
        const auto& last = m_points.last();
        if (last.first.secsTo(time) == 0) {
            m_points.last().second = price;
            recomputeExtremes();
            return;
        }
        if (last.first.secsTo(time) < 1 && qAbs(last.second - price) < 1e-6)
            return;
    }

    m_points.append({time, price});

    if (!m_hasExtreme) {
        m_high = price;
        m_low = price;
        m_highTime = time;
        m_lowTime = time;
        m_hasExtreme = true;
        emit extremesChanged();
    } else {
        bool changed = false;
        if (price > m_high) {
            m_high = price;
            m_highTime = time;
            changed = true;
        }
        if (price < m_low) {
            m_low = price;
            m_lowTime = time;
            changed = true;
        }
        if (changed)
            emit extremesChanged();
    }

    trimIfNeeded();
}

void HistoryCache::replaceFromChart(const QVector<QPair<qint64, double>>& chartPoints)
{
    ensureToday();
    m_points.clear();
    m_points.reserve(chartPoints.size());

    for (const auto& pt : chartPoints) {
        if (pt.second <= 0.0)
            continue;
        const QDateTime dt = QDateTime::fromSecsSinceEpoch(pt.first);
        m_points.append({dt, pt.second});
    }

    std::sort(m_points.begin(), m_points.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });

    recomputeExtremes();
}

QVector<QPair<QDateTime, double>> HistoryCache::todayPoints() const
{
    return m_points;
}

bool HistoryCache::todayHigh(double& outHigh) const
{
    if (!m_hasExtreme) return false;
    outHigh = m_high;
    return true;
}

bool HistoryCache::todayLow(double& outLow) const
{
    if (!m_hasExtreme) return false;
    outLow = m_low;
    return true;
}

bool HistoryCache::todayHighPoint(QDateTime& outTime, double& outPrice) const
{
    if (!m_hasExtreme) return false;
    outTime = m_highTime;
    outPrice = m_high;
    return true;
}

bool HistoryCache::todayLowPoint(QDateTime& outTime, double& outPrice) const
{
    if (!m_hasExtreme) return false;
    outTime = m_lowTime;
    outPrice = m_low;
    return true;
}

void HistoryCache::clear()
{
    m_points.clear();
    m_hasExtreme = false;
    m_high = 0.0;
    m_low = 0.0;
    m_highTime = QDateTime();
    m_lowTime = QDateTime();
}

void HistoryCache::ensureToday()
{
    const QDate today = QDate::currentDate();
    if (m_currentDate != today) {
        clear();
        m_currentDate = today;
    }
}

void HistoryCache::trimIfNeeded()
{
    if (m_points.size() <= kMaxPoints)
        return;

    QVector<QPair<QDateTime, double>> kept;
    kept.reserve(kMaxPoints / 2 + 2);
    const int n = m_points.size();
    kept.append(m_points.first());
    for (int i = 2; i < n - 1; i += 2)
        kept.append(m_points.at(i));
    kept.append(m_points.last());
    m_points.swap(kept);
    recomputeExtremes();
}

void HistoryCache::recomputeExtremes()
{
    if (m_points.isEmpty()) {
        m_hasExtreme = false;
        return;
    }

    const double oldHigh = m_high;
    const double oldLow = m_low;

    m_high = m_points.first().second;
    m_low = m_high;
    m_highTime = m_points.first().first;
    m_lowTime = m_highTime;

    for (const auto& p : m_points) {
        if (p.second > m_high) {
            m_high = p.second;
            m_highTime = p.first;
        }
        if (p.second < m_low) {
            m_low = p.second;
            m_lowTime = p.first;
        }
    }
    m_hasExtreme = true;

    if (!qFuzzyCompare(oldHigh, m_high) || !qFuzzyCompare(oldLow, m_low))
        emit extremesChanged();
}

void HistoryCache::persistExtremesToDb(const QString& sourceCode)
{
    if (!m_hasExtreme)
        return;

    ExtremeDatabase::instance().upsertDayExtremes(
        m_currentDate.isValid() ? m_currentDate : QDate::currentDate(),
        sourceCode,
        m_highTime, m_high,
        m_lowTime, m_low);
}
