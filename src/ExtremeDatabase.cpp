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
    // 允许重新 open（例如修改了数据库目录后）
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

    if (QSqlDatabase::contains(m_connectionName)) {
        QSqlDatabase::removeDatabase(m_connectionName);
    }

    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
    db.setDatabaseName(m_dbPath);

    if (!db.open()) {
        qWarning() << "SQLite open failed:" << db.lastError().text() << m_dbPath;
        return false;
    }

    if (!ensureSchema()) {
        qWarning() << "SQLite schema failed";
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

    const QString sql = QStringLiteral(
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
        ")");

    if (!q.exec(sql)) {
        qWarning() << "CREATE TABLE failed:" << q.lastError().text();
        return false;
    }

    q.exec(QStringLiteral(
        "CREATE INDEX IF NOT EXISTS idx_daily_extremes_date ON daily_extremes(trade_date)"));
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

    // 节流：最短 5 秒写一次，降低长时间运行时 SQLite 压力
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    if (m_lastWriteMs > 0 && (nowMs - m_lastWriteMs) < kMinWriteIntervalMs)
        return true;
    m_lastWriteMs = nowMs;

    if (!QSqlDatabase::contains(m_connectionName)) {
        m_open = false;
        if (!open())
            return false;
    }

    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    if (!db.isOpen()) {
        m_open = false;
        if (!open())
            return false;
        db = QSqlDatabase::database(m_connectionName);
    }
    QSqlQuery q(db);

    q.prepare(QStringLiteral(
        "INSERT INTO daily_extremes "
        "(trade_date, source, high_time, high_price, low_time, low_price, updated_at) "
        "VALUES (?, ?, ?, ?, ?, ?, ?) "
        "ON CONFLICT(trade_date, source) DO UPDATE SET "
        "  high_time=excluded.high_time, "
        "  high_price=excluded.high_price, "
        "  low_time=excluded.low_time, "
        "  low_price=excluded.low_price, "
        "  updated_at=excluded.updated_at"));

    const QString dateStr = tradeDate.toString(Qt::ISODate);
    const QString highStr = highTime.toString(Qt::ISODateWithMs);
    const QString lowStr = lowTime.toString(Qt::ISODateWithMs);
    const QString nowStr = QDateTime::currentDateTime().toString(Qt::ISODateWithMs);
    const QString src = source.isEmpty() ? QStringLiteral("zs") : source;

    q.addBindValue(dateStr);
    q.addBindValue(src);
    q.addBindValue(highStr);
    q.addBindValue(highPrice);
    q.addBindValue(lowStr);
    q.addBindValue(lowPrice);
    q.addBindValue(nowStr);

    if (!q.exec()) {
        qWarning() << "upsertDayExtremes failed:" << q.lastError().text();
        return false;
    }
    return true;
}
