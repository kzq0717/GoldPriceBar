#include "PriceService.h"
#include "AppSettings.h"
#include "HistoryCache.h"
#include "ExtremeDatabase.h"
#include "Logger.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QUrl>
#include <QNetworkRequest>
#include <QDebug>
#include <QDateTime>
#include <QDate>

PriceService::PriceService(QObject* parent)
    : QObject(parent)
{
    m_timer = new QTimer(this);
    m_timer->setSingleShot(false);
    m_timer->setTimerType(Qt::CoarseTimer);
    connect(m_timer, &QTimer::timeout, this, &PriceService::onTimeout);

    m_watchdog = new QTimer(this);
    m_watchdog->setInterval(15000);
    m_watchdog->setSingleShot(false);
    connect(m_watchdog, &QTimer::timeout, this, &PriceService::onWatchdog);

    // 定期用全日分时接口校正「今日最高/最低」（避免仅统计启动后轮询点）
    m_chartSeedTimer = new QTimer(this);
    m_chartSeedTimer->setInterval(120000); // 2 分钟
    m_chartSeedTimer->setSingleShot(false);
    connect(m_chartSeedTimer, &QTimer::timeout, this, &PriceService::onChartSeedTimer);

    m_network = new QNetworkAccessManager(this);
    m_intervalMs = AppSettings::instance().refreshIntervalMs();
}

PriceService::~PriceService()
{
    stop();
    abortPending();
}

QString PriceService::currentTypeCode() const
{
    const QString source = AppSettings::instance().dataSource();
    if (source == QStringLiteral("ms"))
        return QStringLiteral("ms");
    if (source == QStringLiteral("gj") || source == QStringLiteral("xau"))
        return QStringLiteral("gj");
    return QStringLiteral("zs");
}

void PriceService::start()
{
    if (!m_timer->isActive()) {
        requestChartSeed();   // 启动即拉全日分时，校正最高价
        requestPrice();
        m_timer->start(m_intervalMs);
        m_watchdog->start();
        m_chartSeedTimer->start();
    }
}

void PriceService::stop()
{
    m_timer->stop();
    m_watchdog->stop();
    m_chartSeedTimer->stop();
    // 仅断开并清空指针；不在此 deleteLater reply（与 NAM 生命周期绑定）
    abortPending();
    if (m_pendingChart) {
        QNetworkReply* r = m_pendingChart.data();
        m_pendingChart.clear();
        if (r) {
            QObject::disconnect(r, nullptr, this, nullptr);
            r->abort();
            r->deleteLater();
        }
    }
}

void PriceService::setInterval(int intervalMs)
{
    if (intervalMs < 1000)
        intervalMs = 1000;
    m_intervalMs = intervalMs;
    if (m_timer->isActive()) {
        m_timer->stop();
        m_timer->start(m_intervalMs);
    }
}

void PriceService::forceRefresh()
{
    abortPending();
    requestChartSeed();
    requestPrice();
}

void PriceService::onTimeout()
{
    requestPrice();
}

void PriceService::onChartSeedTimer()
{
    requestChartSeed();
}

void PriceService::onWatchdog()
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();

    if (m_pendingReply && m_requestStartMs > 0 && (now - m_requestStartMs) > 12000) {
        qWarning() << "PriceService: request hung >12s, abort";
        abortPending();
        requestPrice();
        return;
    }

    const qint64 staleMs = qMax(static_cast<qint64>(m_intervalMs) * 5, 30000LL);
    if (m_lastSuccessMs > 0 && (now - m_lastSuccessMs) > staleMs) {
        Logger::warn(QStringLiteral("PriceService watchdog: no success for %1 ms")
                         .arg(now - m_lastSuccessMs));
        qWarning() << "PriceService watchdog: no success for" << (now - m_lastSuccessMs) << "ms";
        // 只 recreate：内部会安全断开 pending，勿先 abortPending 再毁 NAM
        recreateNetworkManager();
        requestPrice();
    }
}

void PriceService::recreateNetworkManager()
{
    // 崩溃根因：reply 是 NAM 的子对象。对 reply abort/deleteLater 后再
    // deleteLater NAM，会二次销毁，QPointer::clear 前后都可能踩内存。
    // 正确做法：先断开信号并清空 QPointer，再只销毁 NAM（子 reply 随父销毁）。

    Logger::warn(QStringLiteral("PriceService: recreating QNetworkAccessManager"));

    if (m_pendingReply) {
        QNetworkReply* r = m_pendingReply.data();
        m_pendingReply.clear();
        m_requestStartMs = 0;
        if (r)
            QObject::disconnect(r, nullptr, this, nullptr);
    }
    if (m_pendingChart) {
        QNetworkReply* r = m_pendingChart.data();
        m_pendingChart.clear();
        if (r)
            QObject::disconnect(r, nullptr, this, nullptr);
    }

    if (m_network) {
        QNetworkAccessManager* old = m_network;
        m_network = nullptr;
        // 不再对旧 reply 调用 deleteLater
        old->deleteLater();
    }
    m_network = new QNetworkAccessManager(this);
}

void PriceService::abortPending()
{
    if (!m_pendingReply)
        return;
    QNetworkReply* r = m_pendingReply.data();
    m_pendingReply.clear();
    m_requestStartMs = 0;
    if (!r)
        return;
    // 先断开，避免 abort 同步触发 finished 时重入
    QObject::disconnect(r, nullptr, this, nullptr);
    r->abort();
    r->deleteLater();
}

void PriceService::requestChartSeed()
{
    if (m_pendingChart)
        return;
    if (!m_network)
        m_network = new QNetworkAccessManager(this);

    // 全日分时：https://jin.20021002.xyz/api.php?action=chart&type=zs
    const QUrl url(QStringLiteral("https://jin.20021002.xyz/api.php?action=chart&type=%1")
                       .arg(currentTypeCode()));
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("GoldPriceBarLite/0.1.5"));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setAttribute(QNetworkRequest::CacheLoadControlAttribute,
                         QNetworkRequest::AlwaysNetwork);
    request.setTransferTimeout(15000);

    QNetworkReply* reply = m_network->get(request);
    m_pendingChart = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        onChartSeedFinished(reply);
    });
}

void PriceService::onChartSeedFinished(QNetworkReply* reply)
{
    if (m_pendingChart.data() == reply)
        m_pendingChart.clear();

    if (reply->error() != QNetworkReply::NoError) {
        if (reply->error() != QNetworkReply::OperationCanceledError)
            qWarning() << "Chart seed failed:" << reply->errorString();
        reply->deleteLater();
        return;
    }

    const QByteArray raw = reply->readAll();
    reply->deleteLater();

    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(raw, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject())
        return;

    const QJsonObject root = doc.object();
    if (root.value(QStringLiteral("code")).toInt() != 200)
        return;

    const QJsonArray arr = root.value(QStringLiteral("data")).toArray();
    QVector<QPair<qint64, double>> chartPoints;
    chartPoints.reserve(arr.size());

    const qint64 nowSec = QDateTime::currentSecsSinceEpoch();
    for (const QJsonValue& v : arr) {
        if (!v.isObject())
            continue;
        const QJsonObject o = v.toObject();
        const qint64 t = static_cast<qint64>(o.value(QStringLiteral("t")).toDouble());
        const double p = o.value(QStringLiteral("p")).toDouble();
        // 只统计截至当前时刻的点（防止异常未来时间戳）
        if (t > 0 && t <= nowSec + 60 && p > 0.0)
            chartPoints.append({t, p});
    }

    if (chartPoints.isEmpty())
        return;

    HistoryCache::instance().replaceFromChart(chartPoints);
    ExtremeDatabase::instance().refreshDailyBarFromPoints(
        QDate::currentDate(), currentTypeCode(), chartPoints);
    HistoryCache::instance().persistExtremesToDb(currentTypeCode());

    // 若已有实时价，合并进缓存，保证最高不低于现价
    if (m_hasValidPrice && m_lastPrice > 0.0) {
        HistoryCache::instance().append(QDateTime::currentDateTime(), m_lastPrice);
        ExtremeDatabase::instance().upsertDailyBar(
            QDate::currentDate(), currentTypeCode(), m_lastPrice);
    }

    emit extremesUpdated();

    // 有实时价时顺便刷新词条上的「高」
    if (m_hasValidPrice)
        emit priceUpdated(m_lastPrice, m_lastChange, m_lastSourceName);
}

void PriceService::requestPrice()
{
    if (m_pendingReply)
        return;

    if (!m_network)
        m_network = new QNetworkAccessManager(this);

    const QUrl url(QStringLiteral("https://jin.20021002.xyz/api.php?type=%1")
                       .arg(currentTypeCode()));

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("GoldPriceBarLite/0.1.5"));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setAttribute(QNetworkRequest::CacheLoadControlAttribute,
                         QNetworkRequest::AlwaysNetwork);
    request.setTransferTimeout(10000);

    QNetworkReply* reply = m_network->get(request);
    m_pendingReply = reply;
    m_requestStartMs = QDateTime::currentMSecsSinceEpoch();

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        onNetworkFinished(reply);
    });
}

void PriceService::onNetworkFinished(QNetworkReply* reply)
{
    if (m_pendingReply.data() != reply) {
        reply->deleteLater();
        return;
    }
    m_pendingReply.clear();
    m_requestStartMs = 0;

    if (reply->error() != QNetworkReply::NoError) {
        if (reply->error() != QNetworkReply::OperationCanceledError) {
            ++m_consecutiveFail;
            Logger::warn(QStringLiteral("Price fetch error: %1").arg(reply->errorString()));
            qWarning() << "Price fetch error:" << reply->errorString();
            if (m_consecutiveFail >= 5) {
                recreateNetworkManager();
                m_consecutiveFail = 0;
            }
            emit fetchFailed(tr("网络错误: %1").arg(reply->errorString()));
        }
        reply->deleteLater();
        return;
    }

    const QByteArray data = reply->readAll();
    reply->deleteLater();

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        ++m_consecutiveFail;
        emit fetchFailed(tr("JSON 解析失败"));
        return;
    }

    const QJsonObject root = doc.object();
    if (root.value(QStringLiteral("code")).toInt() != 200) {
        ++m_consecutiveFail;
        emit fetchFailed(root.value(QStringLiteral("msg")).toString(tr("接口返回错误")));
        return;
    }

    const QJsonObject d = root.value(QStringLiteral("data")).toObject();
    if (d.isEmpty()) {
        ++m_consecutiveFail;
        emit fetchFailed(tr("数据为空"));
        return;
    }

    const double price  = d.value(QStringLiteral("price")).toDouble();
    const double change = d.value(QStringLiteral("change")).toDouble();
    QString name        = d.value(QStringLiteral("name")).toString();
    const QString currency = d.value(QStringLiteral("currency")).toString();

    if (name.isEmpty())
        name = tr("未知");

    if (!(name.contains(QStringLiteral("伦敦")) || name.contains(QStringLiteral("国际")))) {
        if (!name.contains(QStringLiteral("积存金")))
            name += QStringLiteral("积存金");
    }

    if (price <= 0.0) {
        ++m_consecutiveFail;
        emit fetchFailed(tr("价格无效"));
        return;
    }

    m_lastPrice = price;
    m_lastChange = change;
    m_lastSourceName = name;
    if (!currency.isEmpty() && currency != QStringLiteral("¥") && currency != QStringLiteral("￥")) {
        if (!m_lastSourceName.contains(currency))
            m_lastSourceName += QStringLiteral("(%1)").arg(currency);
    }
    m_hasValidPrice = true;
    m_consecutiveFail = 0;
    m_lastSuccessMs = QDateTime::currentMSecsSinceEpoch();

    HistoryCache::instance().append(QDateTime::currentDateTime(), m_lastPrice);
    ExtremeDatabase::instance().upsertDailyBar(
        QDate::currentDate(), currentTypeCode(), m_lastPrice);
    // 降低写库频率：约每 12 次成功刷新写一次
    if ((++m_persistCounter % 12) == 0)
        HistoryCache::instance().persistExtremesToDb(currentTypeCode());
    emit priceUpdated(m_lastPrice, m_lastChange, m_lastSourceName);
}
