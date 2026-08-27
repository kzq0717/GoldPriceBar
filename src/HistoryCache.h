#ifndef HISTORYCACHE_H
#define HISTORYCACHE_H

#include <QObject>
#include <QDateTime>
#include <QVector>
#include <QPair>

class HistoryCache : public QObject
{
    Q_OBJECT

public:
    static HistoryCache& instance();

    void append(const QDateTime& time, double price);
    void replaceFromChart(const QVector<QPair<qint64, double>>& chartPoints);

    QVector<QPair<QDateTime, double>> todayPoints() const;

    bool todayHigh(double& outHigh) const;
    bool todayLow(double& outLow) const;
    bool todayHighPoint(QDateTime& outTime, double& outPrice) const;
    bool todayLowPoint(QDateTime& outTime, double& outPrice) const;

    int count() const { return m_points.size(); }

    void clear();
    void ensureToday();

    /** 将当前最高/最低写入 SQLite（需已 open 数据库） */
    void persistExtremesToDb(const QString& sourceCode);

signals:
    void extremesChanged();

private:
    explicit HistoryCache(QObject* parent = nullptr);
    Q_DISABLE_COPY(HistoryCache)

    void recomputeExtremes();
    void trimIfNeeded();

    QDate m_currentDate;
    QVector<QPair<QDateTime, double>> m_points;
    double m_high = 0.0;
    double m_low = 0.0;
    QDateTime m_highTime;
    QDateTime m_lowTime;
    bool m_hasExtreme = false;

    static constexpr int kMaxPoints = 4000;
};

#endif // HISTORYCACHE_H
