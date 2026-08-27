#ifndef EXTREMEDATABASE_H
#define EXTREMEDATABASE_H

#include <QObject>
#include <QDate>
#include <QDateTime>
#include <QString>

/**
 * @brief SQLite 存储当日最高/最低点（时间 + 价格）
 * 数据库文件：%AppData%/GoldPriceBarLite/gold_extremes.db （Windows）
 * 表 daily_extremes：按交易日 + 数据源 唯一
 */
class ExtremeDatabase : public QObject
{
    Q_OBJECT

public:
    static ExtremeDatabase& instance();

    bool open();
    bool isOpen() const { return m_open; }
    QString databasePath() const { return m_dbPath; }

    /**
     * 写入或更新当日极值
     * @param tradeDate 交易日
     * @param source    数据源代码 zs/ms/gj
     * @param highTime  最高点时间
     * @param highPrice 最高价
     * @param lowTime   最低点时间
     * @param lowPrice  最低价
     */
    bool upsertDayExtremes(const QDate& tradeDate,
                           const QString& source,
                           const QDateTime& highTime, double highPrice,
                           const QDateTime& lowTime, double lowPrice);

private:
    explicit ExtremeDatabase(QObject* parent = nullptr);
    Q_DISABLE_COPY(ExtremeDatabase)

    bool ensureSchema();

    bool m_open = false;
    QString m_dbPath;
    QString m_connectionName;
    qint64 m_lastWriteMs = 0;
    static constexpr int kMinWriteIntervalMs = 5000; // 避免高频写库导致卡顿/闪退
};



#endif // EXTREMEDATABASE_H
