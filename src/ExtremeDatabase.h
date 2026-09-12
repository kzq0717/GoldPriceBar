#ifndef EXTREMEDATABASE_H
#define EXTREMEDATABASE_H

#include <QObject>
#include <QDate>
#include <QDateTime>
#include <QString>
#include <QVector>
#include <QPair>

/**
 * SQLite 行情与建模数据
 * 库文件：{databaseDir}/gold_extremes.db
 *
 * 表：
 *  - daily_extremes / daily_bars / intraday_samples（原有）
 *  - quote_samples   降采样报价（主源）
 *  - secondary_quotes 对照价与比值
 *  - forecast_logs   预测登记与结算
 *  - alert_events    预警事件
 *  - session_marks   交易时段标记
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

    bool upsertDailyBar(const QDate& tradeDate, const QString& source, double price,
                        const QDateTime& when = QDateTime::currentDateTime());

    bool refreshDailyBarFromPoints(const QDate& tradeDate, const QString& source,
                                   const QVector<QPair<qint64, double>>& points);

    QVector<QPair<QDateTime, double>> loadMonthCloses(int year, int month,
                                                      const QString& source) const;

    bool monthRange(int year, int month, const QString& source,
                    double& outHigh, double& outLow, int& outDays) const;

    bool insertIntradaySample(const QDateTime& ts, const QString& source, double price);

    QVector<QPair<QDateTime, double>> loadIntradaySamples(const QDate& day,
                                                          const QString& source) const;

    bool purgeIntradayOlderThan(int keepDays = 14);

    QVector<QPair<QDate, double>> loadRecentDailyCloses(int maxDays,
                                                        const QString& source) const;

    bool upsertHistoricalClose(const QDate& tradeDate, const QString& source, double close);

    /** 建模：主源报价降采样（默认 ≥30s 一条） */
    bool insertQuoteSample(const QDateTime& ts, const QString& source,
                           double price, double change = 0.0);

    /** 建模：主/对照价与比值 */
    bool insertSecondaryQuote(const QDateTime& ts, const QString& primarySource,
                              double primaryPrice, const QString& secondarySource,
                              double secondaryPrice);

    /** 建模：登记预测；返回 row id（失败 0） */
    qint64 insertForecastLog(const QDateTime& madeAt, const QString& source,
                             const QString& mode, double predHigh, double predLow,
                             double basePrice);

    /** 结算未到期预测（用今高/今低） */
    int settleForecasts(const QString& source, double actualHigh, double actualLow,
                        const QDateTime& now = QDateTime::currentDateTime());

    /** 建模：预警事件 */
    bool insertAlertEvent(const QDateTime& ts, const QString& source,
                          const QString& kind, double price, double threshold,
                          const QString& note = QString());

    /** 建模：时段状态变化（可选节流） */
    bool insertSessionMark(const QDateTime& ts, const QString& source,
                           bool isOpen, const QString& status);

private:
    explicit ExtremeDatabase(QObject* parent = nullptr);
    Q_DISABLE_COPY(ExtremeDatabase)

    bool ensureSchema();

    bool m_open = false;
    QString m_dbPath;
    QString m_connectionName;
    qint64 m_lastWriteMs = 0;
    qint64 m_lastQuoteSampleMs = 0;
    qint64 m_lastSecondaryMs = 0;
    QString m_lastSessionKey;
    static constexpr int kMinWriteIntervalMs = 5000;
    static constexpr int kQuoteSampleIntervalMs = 30000;
    static constexpr int kSecondarySampleIntervalMs = 30000;
};

#endif // EXTREMEDATABASE_H
