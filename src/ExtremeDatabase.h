#ifndef EXTREMEDATABASE_H
#define EXTREMEDATABASE_H

#include <QObject>
#include <QDate>
#include <QDateTime>
#include <QString>
#include <QVector>
#include <QPair>
#include <vector>
#include "goldsdk/types.hpp"

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
struct ForecastLogEntry {
    qint64 id = 0;
    QDateTime madeAt;
    QString source;
    QString mode;
    QString brief;
    double predHigh = 0;
    double predLow = 0;
    double basePrice = 0;
    double actualHigh = 0;
    double actualLow = 0;
    QString predHighTime;
    QString predLowTime;
    QString catalyst;
};

struct SimulatedTradeEntry {
    qint64 id = 0;
    QDateTime tradeTime;
    QString source;
    QString action;       // "BUY" 或 "SELL"
    QString decisionTag;  // e.g. "🟢 企稳反弹·分批建仓", "🎯 冲高受阻·分批止盈"
    double price = 0.0;
    double grams = 0.0;
    double amount = 0.0;
    double fee = 0.0;
    double realizedPnl = 0.0;
    double remainingGrams = 0.0;
    double remainingCost = 0.0;
    QString note;
};

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

    bool backfillIntradaySamplesFromChart(const QDate& tradeDate, const QString& source,
                                          const QVector<QPair<qint64, double>>& points);

    QVector<QPair<QDateTime, double>> loadIntradaySamples(const QDate& day,
                                                          const QString& source) const;

    bool purgeIntradayOlderThan(int keepDays = 14);

    QVector<QPair<QDate, double>> loadRecentDailyCloses(int maxDays,
                                                        const QString& source) const;

    QVector<QPair<QDateTime, double>> loadDailyClosesRange(const QDate& from,
                                                           const QDate& to,
                                                           const QString& source) const;

    bool upsertHistoricalClose(const QDate& tradeDate, const QString& source, double close,
                               const QString& unit = QString());

    /** 单位：CNY/g、USD/oz 等；空则按 source 推断 */
    static QString defaultUnitForSource(const QString& source);

    /** USD/oz → 约元/克：cny_per_g = usd_oz * usdCny / 31.1034768 */
    static double usdOzToCnyG(double usdPerOz, double usdCny);

    /** 计算近 N 日真实波幅均值（Wilder ATR），不含当日 */
    double computeAtr(int days = 10, const QString& source = QString()) const;

    /** 加载最近 N 日完整 OHLC 日线（旧→新，不含当日），供 HAR-RV / Garman-Klass 使用 */
    std::vector<goldsdk::DailyBar> loadRecentDailyBars(int maxDays = 40,
                                                       const QString& source = QString()) const;

    /** 统计近 N 日历史日高点落入 48 个半小时 CST 时间桶的频次 */
    std::vector<int> loadHistoricalPeakBucketCounts(int lookbackDays = 60,
                                                    const QString& source = QString()) const;

    /** 统计近 N 日 48 个半小时 CST 时间桶的已实现方差分布占比 */
    std::vector<double> loadHistoricalBucketVarWeights(int lookbackDays = 20,
                                                       const QString& source = QString()) const;

    /** 基于 secondary_quotes 计算跨市场内外盘领先信号 Z-Score（正值表示外盘强于内盘） */
    double computeCrossMarketLeadSignal(const QString& primarySource = QString()) const;

    /** 基于历史已结算预测残差计算自适应共形区间校准因子 */
    double computeAdaptiveConformalScale(const QString& source = QString(), int limit = 30) const;

    /** 查询前一交易日收盘价 */
    double previousClose(const QString& source = QString()) const;

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
                             double basePrice, const QString& brief = QString(),
                             const QString& predHighTime = QString(),
                             const QString& predLowTime = QString(),
                             const QString& catalyst = QString(),
                             int horizonSec = 3600);

    /** 当日预测记录（按时间升序，最新在末尾） */
    QVector<ForecastLogEntry> loadForecastLogsForDay(const QDate& day,
                                                     const QString& source = QString()) const;

    /** 已结算预测样本（用于命中率）；limit 最近条数 */
    QVector<ForecastLogEntry> loadSettledForecasts(int limit = 200,
                                                   const QString& source = QString()) const;


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


    /** 模拟交易：记录买入/卖出明细 */
    bool insertSimulatedTrade(const SimulatedTradeEntry& entry);

    /** 模拟交易：查询记录（按时间倒序） */
    QVector<SimulatedTradeEntry> loadSimulatedTrades(int limit = 100,
                                                     const QString& source = QString()) const;

    /** 模拟交易：累计已实现平仓盈亏 */
    double totalRealizedPnl(const QString& source = QString()) const;

    /** 模拟交易：清空历史记录 */
    bool clearSimulatedTrades();

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
