#ifndef EXTREMEDATABASE_H
#define EXTREMEDATABASE_H

#include <QObject>
#include <QDate>
#include <QDateTime>
#include <QString>
#include <QVector>
#include <QPair>

/**
 * @brief SQLite：当日高低点 + 日线行情（供月份曲线）
 * 库文件：{databaseDir}/gold_extremes.db
 */
class ExtremeDatabase : public QObject
{
    Q_OBJECT

public:
    static ExtremeDatabase& instance();

    bool open();
    bool isOpen() const { return m_open; }
    QString databasePath() const { return m_dbPath; }

    bool upsertDayExtremes(const QDate& tradeDate,
                           const QString& source,
                           const QDateTime& highTime, double highPrice,
                           const QDateTime& lowTime, double lowPrice);

    /** 用实时价更新日线 OHLC（开高低收） */
    bool upsertDailyBar(const QDate& tradeDate, const QString& source, double price,
                        const QDateTime& when = QDateTime::currentDateTime());

    /** 用分时点批量刷新当日 OHLC */
    bool refreshDailyBarFromPoints(const QDate& tradeDate, const QString& source,
                                   const QVector<QPair<qint64, double>>& points);

    /**
     * 读取某月日线收盘价序列（用于月份曲线）
     * 返回 (日期中午, close)
     */
    QVector<QPair<QDateTime, double>> loadMonthCloses(int year, int month,
                                                      const QString& source) const;

    /** 读取某月 OHLC，用于标题统计 */
    bool monthRange(int year, int month, const QString& source,
                    double& outHigh, double& outLow, int& outDays) const;

    /**
     * 分时抽样写入（降采样，供「昨日对比」）
     * 同一分钟内同 source 只保留一个点（REPLACE）
     */
    bool insertIntradaySample(const QDateTime& ts, const QString& source, double price);

    /** 读取某日分时抽样点（本地时间） */
    QVector<QPair<QDateTime, double>> loadIntradaySamples(const QDate& day,
                                                          const QString& source) const;

    /** 清理 N 天前的分时抽样，控制库体积 */
    bool purgeIntradayOlderThan(int keepDays = 14);

private:
    explicit ExtremeDatabase(QObject* parent = nullptr);
    Q_DISABLE_COPY(ExtremeDatabase)

    bool ensureSchema();

    bool m_open = false;
    QString m_dbPath;
    QString m_connectionName;
    qint64 m_lastWriteMs = 0;
    static constexpr int kMinWriteIntervalMs = 5000;
};

#endif // EXTREMEDATABASE_H
