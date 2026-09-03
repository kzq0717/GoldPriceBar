#include "ExtremeDatabase.h"
#include "AppSettings.h"

#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QDir>
#include <QDebug>
#include <QDateTime>
#include <QMutex>
#include <QMutexLocker>
#include <QVariant>

ExtremeDatabase& ExtremeDatabase::instance()
{
    static ExtremeDatabase inst;
    return inst;
}

ExtremeDatabase::ExtremeDatabase(QObject* parent)
    : QObject(parent)
    , m_connectionName(QStringLiteral("gold_extremes_conn"))
{
}

bool ExtremeDatabase::open()
{
    if (m_open) {
        QSqlDatabase::database(m_connectionName).close();
        QSqlDatabase::removeDatabase(m_connectionName);
        m_open = false;
    }

    const QString dataDir = AppSettings::instance().resolvedDatabaseDir();
    if (!QDir().mkpath(dataDir)) {
        qWarning() << "Cannot create database dir:" << dataDir;
        return false;
    }

    m_dbPath = QDir(dataDir).filePath(QStringLiteral("gold_extremes.db"));

    if (QSqlDatabase::contains(m_connectionName))
        QSqlDatabase::removeDatabase(m_connectionName);

    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
    db.setDatabaseName(m_dbPath);
    if (!db.open()) {
        qWarning() << "SQLite open failed:" << db.lastError().text() << m_dbPath;
        return false;
    }
    if (!ensureSchema()) {
        db.close();
        return false;
    }
    m_open = true;
    qInfo() << "SQLite ready:" << m_dbPath;
    return true;
}

bool ExtremeDatabase::ensureSchema()
{
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);

    if (!q.exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS daily_extremes ("
            "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  trade_date TEXT NOT NULL,"
            "  source TEXT NOT NULL,"
            "  high_time TEXT NOT NULL,"
            "  high_price REAL NOT NULL,"
            "  low_time TEXT NOT NULL,"
            "  low_price REAL NOT NULL,"
            "  updated_at TEXT NOT NULL,"
            "  UNIQUE(trade_date, source)"
            ")"))) {
        qWarning() << "CREATE daily_extremes failed:" << q.lastError().text();
        return false;
    }
    q.exec(QStringLiteral(
        "CREATE INDEX IF NOT EXISTS idx_daily_extremes_date ON daily_extremes(trade_date)"));

    // 分时抽样：昨日对比（按分钟去重）
    if (!q.exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS intraday_samples ("
            "  trade_date TEXT NOT NULL,"
            "  minute_key TEXT NOT NULL,"
            "  source TEXT NOT NULL,"
            "  ts TEXT NOT NULL,"
            "  price REAL NOT NULL,"
            "  PRIMARY KEY(trade_date, minute_key, source)"
            ")"))) {
        qWarning() << "CREATE intraday_samples failed:" << q.lastError().text();
        return false;
    }

    // 日线：月份曲线用
    if (!q.exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS daily_bars ("
            "  trade_date TEXT NOT NULL,"
            "  source TEXT NOT NULL,"
            "  open_price REAL NOT NULL,"
            "  high_price REAL NOT NULL,"
            "  low_price REAL NOT NULL,"
            "  close_price REAL NOT NULL,"
            "  updated_at TEXT NOT NULL,"
            "  PRIMARY KEY(trade_date, source)"
            ")"))) {
        qWarning() << "CREATE daily_bars failed:" << q.lastError().text();
        return false;
    }
    return true;
}

bool ExtremeDatabase::upsertDayExtremes(const QDate& tradeDate,
                                        const QString& source,
                                        const QDateTime& highTime, double highPrice,
                                        const QDateTime& lowTime, double lowPrice)
{
    static QMutex s_mutex;
    QMutexLocker locker(&s_mutex);

    if (!m_open && !open())
        return false;
    if (!tradeDate.isValid() || highPrice <= 0.0 || lowPrice <= 0.0)
        return false;

    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    if (m_lastWriteMs > 0 && (nowMs - m_lastWriteMs) < kMinWriteIntervalMs)
        return true;
    m_lastWriteMs = nowMs;

    if (!QSqlDatabase::contains(m_connectionName) ||
        !QSqlDatabase::database(m_connectionName).isOpen()) {
        m_open = false;
        if (!open())
            return false;
    }

    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "INSERT INTO daily_extremes "
        "(trade_date, source, high_time, high_price, low_time, low_price, updated_at) "
        "VALUES (?, ?, ?, ?, ?, ?, ?) "
        "ON CONFLICT(trade_date, source) DO UPDATE SET "
        "  high_time=excluded.high_time, high_price=excluded.high_price, "
        "  low_time=excluded.low_time, low_price=excluded.low_price, "
        "  updated_at=excluded.updated_at"));

    const QString src = source.isEmpty() ? QStringLiteral("zs") : source;
    q.addBindValue(tradeDate.toString(Qt::ISODate));
    q.addBindValue(src);
    q.addBindValue(highTime.toString(Qt::ISODateWithMs));
    q.addBindValue(highPrice);
    q.addBindValue(lowTime.toString(Qt::ISODateWithMs));
    q.addBindValue(lowPrice);
    q.addBindValue(QDateTime::currentDateTime().toString(Qt::ISODateWithMs));
    if (!q.exec()) {
        qWarning() << "upsertDayExtremes failed:" << q.lastError().text();
        return false;
    }
    return true;
}

bool ExtremeDatabase::upsertDailyBar(const QDate& tradeDate, const QString& source, double price,
                                     const QDateTime& when)
{
    Q_UNUSED(when);
    if (!m_open && !open())
        return false;
    if (!tradeDate.isValid() || price <= 0.0)
        return false;

    const QString src = source.isEmpty() ? QStringLiteral("zs") : source;
    const QString dateStr = tradeDate.toString(Qt::ISODate);
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);

    q.prepare(QStringLiteral(
        "SELECT open_price, high_price, low_price FROM daily_bars WHERE trade_date=? AND source=?"));
    q.addBindValue(dateStr);
    q.addBindValue(src);
    double openP = price, highP = price, lowP = price;
    if (q.exec() && q.next()) {
        openP = q.value(0).toDouble();
        highP = qMax(q.value(1).toDouble(), price);
        lowP = qMin(q.value(2).toDouble(), price);
    }

    QSqlQuery u(db);
    u.prepare(QStringLiteral(
        "INSERT INTO daily_bars (trade_date, source, open_price, high_price, low_price, close_price, updated_at) "
        "VALUES (?, ?, ?, ?, ?, ?, ?) "
        "ON CONFLICT(trade_date, source) DO UPDATE SET "
        "  high_price=excluded.high_price, low_price=excluded.low_price, "
        "  close_price=excluded.close_price, updated_at=excluded.updated_at"));
    u.addBindValue(dateStr);
    u.addBindValue(src);
    u.addBindValue(openP);
    u.addBindValue(highP);
    u.addBindValue(lowP);
    u.addBindValue(price);
    u.addBindValue(QDateTime::currentDateTime().toString(Qt::ISODateWithMs));
    if (!u.exec()) {
        qWarning() << "upsertDailyBar failed:" << u.lastError().text();
        return false;
    }
    return true;
}

bool ExtremeDatabase::refreshDailyBarFromPoints(const QDate& tradeDate, const QString& source,
                                                const QVector<QPair<qint64, double>>& points)
{
    if (!m_open && !open())
        return false;
    if (!tradeDate.isValid() || points.isEmpty())
        return false;

    double openP = 0, highP = 0, lowP = 0, closeP = 0;
    bool first = true;
    for (const auto& pt : points) {
        if (pt.second <= 0.0)
            continue;
        if (first) {
            openP = highP = lowP = closeP = pt.second;
            first = false;
        } else {
            highP = qMax(highP, pt.second);
            lowP = qMin(lowP, pt.second);
            closeP = pt.second;
        }
    }
    if (first)
        return false;

    const QString src = source.isEmpty() ? QStringLiteral("zs") : source;
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery u(db);
    u.prepare(QStringLiteral(
        "INSERT INTO daily_bars (trade_date, source, open_price, high_price, low_price, close_price, updated_at) "
        "VALUES (?, ?, ?, ?, ?, ?, ?) "
        "ON CONFLICT(trade_date, source) DO UPDATE SET "
        "  open_price=excluded.open_price, high_price=excluded.high_price, "
        "  low_price=excluded.low_price, close_price=excluded.close_price, "
        "  updated_at=excluded.updated_at"));
    u.addBindValue(tradeDate.toString(Qt::ISODate));
    u.addBindValue(src);
    u.addBindValue(openP);
    u.addBindValue(highP);
    u.addBindValue(lowP);
    u.addBindValue(closeP);
    u.addBindValue(QDateTime::currentDateTime().toString(Qt::ISODateWithMs));
    return u.exec();
}

QVector<QPair<QDateTime, double>> ExtremeDatabase::loadMonthCloses(int year, int month,
                                                                   const QString& source) const
{
    QVector<QPair<QDateTime, double>> out;
    if (!m_open)
        return out;

    const QString src = source.isEmpty() ? QStringLiteral("zs") : source;
    const QString prefix = QStringLiteral("%1-%2")
                               .arg(year, 4, 10, QChar('0'))
                               .arg(month, 2, 10, QChar('0'));

    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "SELECT trade_date, close_price FROM daily_bars "
        "WHERE source=? AND trade_date LIKE ? ORDER BY trade_date ASC"));
    q.addBindValue(src);
    q.addBindValue(prefix + QStringLiteral("-%"));
    if (!q.exec())
        return out;

    while (q.next()) {
        const QDate d = QDate::fromString(q.value(0).toString(), Qt::ISODate);
        const double c = q.value(1).toDouble();
        if (d.isValid() && c > 0.0)
            out.append({QDateTime(d, QTime(12, 0)), c});
    }
    return out;
}

bool ExtremeDatabase::monthRange(int year, int month, const QString& source,
                                 double& outHigh, double& outLow, int& outDays) const
{
    outHigh = outLow = 0;
    outDays = 0;
    if (!m_open)
        return false;

    const QString src = source.isEmpty() ? QStringLiteral("zs") : source;
    const QString prefix = QStringLiteral("%1-%2")
                               .arg(year, 4, 10, QChar('0'))
                               .arg(month, 2, 10, QChar('0'));
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "SELECT MAX(high_price), MIN(low_price), COUNT(*) FROM daily_bars "
        "WHERE source=? AND trade_date LIKE ?"));
    q.addBindValue(src);
    q.addBindValue(prefix + QStringLiteral("-%"));
    if (!q.exec() || !q.next())
        return false;
    outHigh = q.value(0).toDouble();
    outLow = q.value(1).toDouble();
    outDays = q.value(2).toInt();
    return outDays > 0 && outHigh > 0.0;
}

bool ExtremeDatabase::insertIntradaySample(const QDateTime& ts, const QString& source, double price)
{
    if (!m_open && !open())
        return false;
    if (!ts.isValid() || price <= 0.0)
        return false;

    // 节流：全局至少间隔 20s 写一次抽样（与实时刷新解耦）
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    static qint64 s_lastSampleMs = 0;
    if (s_lastSampleMs > 0 && (nowMs - s_lastSampleMs) < 20000)
        return true;
    s_lastSampleMs = nowMs;

    const QString src = source.isEmpty() ? QStringLiteral("zs") : source;
    const QString dateStr = ts.date().toString(Qt::ISODate);
    const QString minuteKey = ts.toString(QStringLiteral("HH:mm"));

    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery u(db);
    u.prepare(QStringLiteral(
        "INSERT INTO intraday_samples (trade_date, minute_key, source, ts, price) "
        "VALUES (?, ?, ?, ?, ?) "
        "ON CONFLICT(trade_date, minute_key, source) DO UPDATE SET "
        "  ts=excluded.ts, price=excluded.price"));
    u.addBindValue(dateStr);
    u.addBindValue(minuteKey);
    u.addBindValue(src);
    u.addBindValue(ts.toString(Qt::ISODateWithMs));
    u.addBindValue(price);
    if (!u.exec()) {
        qWarning() << "insertIntradaySample failed:" << u.lastError().text();
        return false;
    }
    return true;
}

QVector<QPair<QDateTime, double>> ExtremeDatabase::loadIntradaySamples(
    const QDate& day, const QString& source) const
{
    QVector<QPair<QDateTime, double>> out;
    if (!m_open || !day.isValid())
        return out;
    const QString src = source.isEmpty() ? QStringLiteral("zs") : source;
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "SELECT ts, price FROM intraday_samples "
        "WHERE trade_date=? AND source=? ORDER BY minute_key ASC"));
    q.addBindValue(day.toString(Qt::ISODate));
    q.addBindValue(src);
    if (!q.exec())
        return out;
    while (q.next()) {
        const QDateTime dt = QDateTime::fromString(q.value(0).toString(), Qt::ISODateWithMs);
        const double p = q.value(1).toDouble();
        if (dt.isValid() && p > 0.0)
            out.append({dt, p});
    }
    return out;
}

bool ExtremeDatabase::purgeIntradayOlderThan(int keepDays)
{
    if (!m_open && !open())
        return false;
    const QDate cut = QDate::currentDate().addDays(-qMax(1, keepDays));
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);
    q.prepare(QStringLiteral("DELETE FROM intraday_samples WHERE trade_date < ?"));
    q.addBindValue(cut.toString(Qt::ISODate));
    return q.exec();
}
