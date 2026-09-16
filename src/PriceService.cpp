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

PriceService::PriceService(QObject *parent) : QObject(parent) {
    // 构造函数保持空：所有资源在 start()/ensureXxx 中创建（规避启动期 0xc0000005）
    Logger::info(QStringLiteral("PriceService ctor: empty OK"));
}

void PriceService::ensureTimers() {
    if (m_timer)
        return;
    Logger::info(QStringLiteral("PriceService: create timers"));
    m_timer = new QTimer(this);
    m_timer->setSingleShot(false);
    m_timer->setTimerType(Qt::CoarseTimer);
    connect(m_timer, &QTimer::timeout, this, &PriceService::onTimeout);

    m_watchdog = new QTimer(this);
    m_watchdog->setInterval(15000);
    m_watchdog->setSingleShot(false);
    connect(m_watchdog, &QTimer::timeout, this, &PriceService::onWatchdog);

    m_chartSeedTimer = new QTimer(this);
    m_chartSeedTimer->setInterval(120000);
    m_chartSeedTimer->setSingleShot(false);
    connect(m_chartSeedTimer, &QTimer::timeout, this, &PriceService::onChartSeedTimer);
    Logger::info(QStringLiteral("PriceService: timers OK"));
}

void PriceService::ensureNetwork() {
    if (m_network)
        return;
    Logger::info(QStringLiteral("PriceService: creating QNetworkAccessManager"));
    m_network = new QNetworkAccessManager(this);
    Logger::info(QStringLiteral("PriceService: NAM ready"));
}

PriceService::~PriceService() {
    stop();
    abortPending();
}

QString PriceService::currentTypeCode() const {
    // 与 jin.20021002.xyz / 油猴脚本约定一致的 type 码
    static const QStringList kKnown = {
        QStringLiteral("ms"),
        QStringLiteral("zs"),
        QStringLiteral("cib"),
        QStringLiteral("icbc"),
        QStringLiteral("cmb"),
        QStringLiteral("cgb"),
        QStringLiteral("abc"),
        QStringLiteral("ccb"),
        QStringLiteral("boc"),
        QStringLiteral("jd"),
        QStringLiteral("gj"),
        QStringLiteral("xau"),
    };
    QString source = AppSettings::instance().dataSource().trimmed().toLower();
    if (source == QStringLiteral("xau"))
        source = QStringLiteral("gj");
    if (kKnown.contains(source))
        return source;
    return QStringLiteral("zs");
}

void PriceService::start() {
    Logger::info(QStringLiteral("PriceService::start enter"));
    ensureTimers();
    Logger::info(QStringLiteral("PriceService::start after timers"));
    if (!m_timer)
        return;
    if (m_timer->isActive())
        return;

    m_intervalMs = AppSettings::instance().refreshIntervalMs();
    if (m_intervalMs < 1000)
        m_intervalMs = 1000;

    // 先只启动定时器，网络放到下一事件循环，隔离 NAM 崩溃
    m_timer->start(m_intervalMs);
    m_watchdog->start();
    m_chartSeedTimer->start();
    Logger::info(QStringLiteral("PriceService::start timers running, schedule net"));

    QTimer::singleShot(300, this, [this]() {
        Logger::info(QStringLiteral("PriceService: delayed net begin"));
        ensureNetwork();
        Logger::info(QStringLiteral("PriceService: delayed net after NAM"));
        ExtremeDatabase::instance().purgeIntradayOlderThan(14);
        Logger::info(QStringLiteral("PriceService: after purge"));
        requestHistorySeed();
        Logger::info(QStringLiteral("PriceService: after history seed req"));
        requestChartSeed();
        Logger::info(QStringLiteral("PriceService: after chart seed req"));
        requestPrice();
        Logger::info(QStringLiteral("PriceService: after first price req"));
    });
}

void PriceService::stop() {
    if (m_timer)
        m_timer->stop();
    if (m_watchdog)
        m_watchdog->stop();
    if (m_chartSeedTimer)
        m_chartSeedTimer->stop();
    // 仅断开并清空指针；不在此 deleteLater reply（与 NAM 生命周期绑定）
    abortPending();
    if (m_pendingChart) {
        QNetworkReply *r = m_pendingChart.data();
        m_pendingChart.clear();
        if (r) {
            QObject::disconnect(r, nullptr, this, nullptr);
            r->abort();
            r->deleteLater();
        }
    }
}

void PriceService::setInterval(int intervalMs) {
    if (intervalMs < 1000)
        intervalMs = 1000;
    m_intervalMs = intervalMs;
    if (m_timer && m_timer->isActive()) {
        m_timer->stop();
        m_timer->start(m_intervalMs);
    }
}

void PriceService::forceRefresh() {
    abortPending();
    requestChartSeed();
    requestPrice();
}

void PriceService::onTimeout() {
    requestPrice();
}

void PriceService::onChartSeedTimer() {
    requestChartSeed();
}

void PriceService::onWatchdog() {
    const qint64 now = QDateTime::currentMSecsSinceEpoch();

    if (m_pendingReply && m_requestStartMs > 0 && (now - m_requestStartMs) > 12000) {
        qWarning() << "PriceService: request hung >12s, abort";
        abortPending();
        requestPrice();
        return;
    }

    const qint64 staleMs = qMax(static_cast<qint64>(m_intervalMs) * 5, 30000LL);
    if (m_lastSuccessMs > 0 && (now - m_lastSuccessMs) > staleMs) {
        Logger::warn(
            QStringLiteral("PriceService watchdog: no success for %1 ms (skip recreate)").arg(now - m_lastSuccessMs));
        // 不再 recreateNetworkManager：与 reply 生命周期叠加易 0xc0000005
        abortPending();
        requestPrice();
    }
}

void PriceService::recreateNetworkManager() {
    Logger::info(QStringLiteral("PriceService: recreateNetworkManager"));
    // 崩溃根因：reply 是 NAM 的子对象。对 reply abort/deleteLater 后再
    // deleteLater NAM，会二次销毁，QPointer::clear 前后都可能踩内存。
    // 正确做法：先断开信号并清空 QPointer，再只销毁 NAM（子 reply 随父销毁）。

    Logger::warn(QStringLiteral("PriceService: recreating QNetworkAccessManager"));

    if (m_pendingReply) {
        QNetworkReply *r = m_pendingReply.data();
        m_pendingReply.clear();
        m_requestStartMs = 0;
        if (r)
            QObject::disconnect(r, nullptr, this, nullptr);
    }
    if (m_pendingChart) {
        QNetworkReply *r = m_pendingChart.data();
        m_pendingChart.clear();
        if (r)
            QObject::disconnect(r, nullptr, this, nullptr);
    }

    if (m_network) {
        QNetworkAccessManager *old = m_network;
        m_network = nullptr;
        // 不再对旧 reply 调用 deleteLater
        old->deleteLater();
    }
    m_network = new QNetworkAccessManager(this);
}

void PriceService::abortPending() {
    if (!m_pendingReply)
        return;
    QNetworkReply *r = m_pendingReply.data();
    m_pendingReply.clear();
    m_requestStartMs = 0;
    if (!r)
        return;
    // 先断开，避免 abort 同步触发 finished 时重入
    QObject::disconnect(r, nullptr, this, nullptr);
    r->abort();
    r->deleteLater();
}

void PriceService::requestChartSeed() {
    ensureNetwork();
    if (!m_network)
        return;
    if (m_pendingChart)
        return;
    if (!m_network)
        m_network = new QNetworkAccessManager(this);

    // 全日分时：https://jin.20021002.xyz/api.php?action=chart&type=zs
    const QUrl url(AppSettings::instance().chartUrl().arg(currentTypeCode()));
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("GoldPriceBarLite/0.1.5"));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
    request.setTransferTimeout(15000);

    QNetworkReply *reply = m_network->get(request);
    m_pendingChart = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply]() { onChartSeedFinished(reply); });
}

void PriceService::onChartSeedFinished(QNetworkReply *reply) {
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
    for (const QJsonValue &v : arr) {
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
    ExtremeDatabase::instance().refreshDailyBarFromPoints(QDate::currentDate(), currentTypeCode(), chartPoints);
    HistoryCache::instance().persistExtremesToDb(currentTypeCode());

    // 若已有实时价，合并进缓存，保证最高不低于现价
    if (m_hasValidPrice && m_lastPrice > 0.0) {
        HistoryCache::instance().append(QDateTime::currentDateTime(), m_lastPrice);
        ExtremeDatabase::instance().upsertDailyBar(QDate::currentDate(), currentTypeCode(), m_lastPrice);
    }

    emit extremesUpdated();

    // 有实时价时顺便刷新词条上的「高」
    if (m_hasValidPrice)
        emit priceUpdated(m_lastPrice, m_lastChange, m_lastSourceName);
}

void PriceService::requestPrice() {
    // 卡住的请求超过 8s 则放弃，避免永不更新
    if (m_pendingReply) {
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        if (m_requestStartMs > 0 && (now - m_requestStartMs) > 8000)
            abortPending();
        else
            return;
    }
    m_backupIndex = 0;
    requestPriceFromBackup(0);
}

void PriceService::requestPriceFromBackup(int backupIndex) {
    ensureNetwork();
    if (!m_network) {
        emit fetchFailed(tr("网络组件未初始化"));
        return;
    }
    if (m_pendingReply)
        return;

    if (!m_network)
        m_network = new QNetworkAccessManager(this);

    m_backupIndex = backupIndex;
    QUrl url;
    const QString type = currentTypeCode();
    const bool domestic = !(type == QStringLiteral("gj") || type == QStringLiteral("xau")
                            || type == QStringLiteral("jd"));
    static const QStringList kDomesticFallback = {
        QStringLiteral("zs"), QStringLiteral("ms"), QStringLiteral("cib"),
        QStringLiteral("icbc"), QStringLiteral("cmb"),
    };

    if (backupIndex == 0) {
        url = QUrl(AppSettings::instance().primaryPriceUrl().arg(type));
    } else if (domestic && backupIndex >= 1 && backupIndex <= kDomesticFallback.size()) {
        QString alt = kDomesticFallback.at(backupIndex - 1);
        if (alt == type) {
            requestPriceFromBackup(backupIndex + 1);
            return;
        }
        Logger::info(QStringLiteral("Price fallback domestic type=%1 idx=%2").arg(alt).arg(backupIndex));
        url = QUrl(AppSettings::instance().primaryPriceUrl().arg(alt));
    } else if (type == QStringLiteral("cmb") && backupIndex == kDomesticFallback.size() + 1) {
        url = QUrl(QStringLiteral("https://m.cmbchina.com/api/rate/gold"));
    } else if ((type == QStringLiteral("gj") || type == QStringLiteral("xau")
                || type == QStringLiteral("jd")) && backupIndex == 1) {
        url = QUrl(AppSettings::instance().backupPriceUrl1());
    } else if ((type == QStringLiteral("gj") || type == QStringLiteral("xau")
                || type == QStringLiteral("jd")) && backupIndex == 2) {
        url = QUrl(AppSettings::instance().backupPriceUrl2());
    } else {
        ++m_consecutiveFail;
        Logger::warn(QStringLiteral("Price all sources failed type=%1 fails=%2")
                         .arg(type).arg(m_consecutiveFail));
        emit fetchFailed(tr("全部数据源失败，请检查网络/代理"));
        if (m_timer && m_consecutiveFail >= 3) {
            const int backoff = qMin(m_intervalMs * 2, 15000);
            m_timer->setInterval(qMax(m_intervalMs, backoff));
        }
        return;
    }

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("GoldPriceBarLite/1.3.10"));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
    request.setTransferTimeout(10000);
    request.setRawHeader("Accept", "application/json");

    QNetworkReply *reply = m_network->get(request);
    m_pendingReply = reply;
    m_requestStartMs = QDateTime::currentMSecsSinceEpoch();

    connect(reply, &QNetworkReply::finished, this, [this, reply]() { onNetworkFinished(reply); });
}

bool PriceService::applyPrice(double price, double change, const QString &name, const QString &currency) {
    if (price <= 0.0)
        return false;

    // 与上次有效价相比跳变过大（>25%）则拒绝，防止串源污染缓存
    if (m_hasValidPrice && m_lastPrice > 0.0) {
        const double ratio = price / m_lastPrice;
        if (ratio > 1.25 || ratio < 0.75) {
            Logger::warn(QStringLiteral("Reject price jump %1 -> %2").arg(m_lastPrice).arg(price));
            return false;
        }
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
    m_backupIndex = 0;
    // 恢复用户设定刷新周期
    if (m_timer && m_intervalMs > 0 && m_timer->interval() != m_intervalMs)
        m_timer->setInterval(m_intervalMs);

    HistoryCache::instance().append(QDateTime::currentDateTime(), m_lastPrice);
    ExtremeDatabase::instance().upsertDailyBar(QDate::currentDate(), currentTypeCode(), m_lastPrice);
    ExtremeDatabase::instance().insertIntradaySample(QDateTime::currentDateTime(), currentTypeCode(), m_lastPrice);
    if ((++m_persistCounter % 12) == 0)
        HistoryCache::instance().persistExtremesToDb(currentTypeCode());
    emit priceUpdated(m_lastPrice, m_lastChange, m_lastSourceName);
    return true;
}

void PriceService::onNetworkFinished(QNetworkReply *reply) {
    if (m_pendingReply.data() != reply) {
        reply->deleteLater();
        return;
    }
    m_pendingReply.clear();
    m_requestStartMs = 0;

    const int tried = m_backupIndex;
    auto tryNext = [this, tried]() {
        // 国际金备用仅当主数据源为伦敦金(gj)时启用；
        // 浙商/民生与 XAU/USD 量纲不同，绝不能回退，否则预警/预测全错
        const QString type = currentTypeCode();
        const bool domestic = !(type == QStringLiteral("gj") || type == QStringLiteral("xau")
                                || type == QStringLiteral("jd"));
        const int maxTry = domestic ? 5 : 2;
        if (tried < maxTry) {
            requestPriceFromBackup(tried + 1);
            return;
        }
        ++m_consecutiveFail;
        // 不 recreate NAM（易崩溃）
        emit fetchFailed(tr("数据源失败，将自动重试"));
        if (m_timer && m_consecutiveFail >= 3) {
            const int backoff = qMin(m_intervalMs * 2, 15000);
            m_timer->setInterval(qMax(m_intervalMs, backoff));
        }
    };

    if (reply->error() != QNetworkReply::NoError) {
        if (reply->error() != QNetworkReply::OperationCanceledError) {
            Logger::warn(QStringLiteral("Price fetch error (src %1): %2").arg(tried).arg(reply->errorString()));
            tryNext();
        }
        reply->deleteLater();
        return;
    }

    const QByteArray data = reply->readAll();
    reply->deleteLater();

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        tryNext();
        return;
    }

    if (tried == 0) {
        // jin 格式
        if (!doc.isObject()) {
            tryNext();
            return;
        }
        const QJsonObject root = doc.object();
        if (root.value(QStringLiteral("code")).toInt() != 200) {
            tryNext();
            return;
        }
        const QJsonObject d = root.value(QStringLiteral("data")).toObject();
        double price = d.value(QStringLiteral("price")).toDouble();
        double change = d.value(QStringLiteral("change")).toDouble();
        QString name = d.value(QStringLiteral("name")).toString();
        const QString currency = d.value(QStringLiteral("currency")).toString();
        if (name.isEmpty())
            name = tr("未知");
        if (!(name.contains(QStringLiteral("伦敦")) || name.contains(QStringLiteral("国际")))) {
            if (!name.contains(QStringLiteral("积存金")))
                name += QStringLiteral("积存金");
        }
        if (!applyPrice(price, change, name, currency))
            tryNext();
        return;
    }

    if (tried == 1) {
        // 招行官方 Au99.99 或 gold-api.com
        if (!doc.isObject()) {
            tryNext();
            return;
        }
        if (currentTypeCode() == QStringLiteral("cmb")) {
            const QJsonObject root = doc.object();
            const QJsonArray items =
                root.value(QStringLiteral("body")).toObject().value(QStringLiteral("data")).toArray();
            double price = 0, change = 0;
            for (const QJsonValue &v : items) {
                const QJsonObject it = v.toObject();
                if (it.value(QStringLiteral("variety")).toString() == QStringLiteral("Au99.99")) {
                    price = it.value(QStringLiteral("curPrice")).toString().toDouble();
                    if (price <= 0)
                        price = it.value(QStringLiteral("curPrice")).toDouble();
                    change = it.value(QStringLiteral("upDown")).toString().toDouble();
                    if (qFuzzyIsNull(change))
                        change = it.value(QStringLiteral("upDown")).toDouble();
                    break;
                }
            }
            if (!applyPrice(price, change, tr("招商银行·官方"), QStringLiteral("¥")))
                tryNext();
            return;
        }
        // gold-api.com
        const QJsonObject o = doc.object();
        const double price = o.value(QStringLiteral("price")).toDouble();
        if (!applyPrice(price, 0.0, tr("伦敦金·备用gold-api"), QStringLiteral("USD")))
            tryNext();
        return;
    }

    if (tried == 2) {
        // goldprice.dev
        if (!doc.isObject()) {
            tryNext();
            return;
        }
        const QJsonObject root = doc.object();
        const QJsonArray symbols = root.value(QStringLiteral("symbols")).toArray();
        if (symbols.isEmpty()) {
            tryNext();
            return;
        }
        const QJsonObject s0 = symbols.at(0).toObject();
        const double price = s0.value(QStringLiteral("price")).toString().toDouble();
        if (!applyPrice(price, 0.0, tr("伦敦金·备用goldprice.dev"), QStringLiteral("USD")))
            tryNext();
        return;
    }

    tryNext();
}

void PriceService::requestHistorySeed() {
    ensureNetwork();
    if (!m_network)
        return;
    if (m_historySeeded || m_pendingHistory)
        return;
    if (!m_network)
        m_network = new QNetworkAccessManager(this);

    // freegoldapi：长期日线（含近年 Yahoo 日线），用于填充 MA5日/MA20日
    const QUrl url(QStringLiteral("https://freegoldapi.com/data/latest.json"));
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("GoldPriceBarLite/1.3.10"));
    request.setTransferTimeout(20000);
    QNetworkReply *reply = m_network->get(request);
    m_pendingHistory = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply]() { onHistoryFinished(reply); });
    Logger::info(QStringLiteral("History seed request started (freegoldapi)"));
}

void PriceService::onHistoryFinished(QNetworkReply *reply) {
    if (m_pendingHistory.data() == reply)
        m_pendingHistory.clear();
    if (!reply)
        return;

    if (reply->error() != QNetworkReply::NoError) {
        Logger::warn(QStringLiteral("History seed failed: %1").arg(reply->errorString()));
        reply->deleteLater();
        return;
    }

    const QByteArray raw = reply->readAll();
    reply->deleteLater();

    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(raw, &err);
    if (err.error != QJsonParseError::NoError || !doc.isArray()) {
        Logger::warn(QStringLiteral("History seed JSON invalid"));
        return;
    }

    const QJsonArray arr = doc.array();
    const QDate today = QDate::currentDate();
    const QDate from = today.addDays(-40); // 多取一些，过滤后够 20 交易日
    int written = 0;
    // 数组可能从古到今：只取最近区间
    for (const QJsonValue &v : arr) {
        if (!v.isObject())
            continue;
        const QJsonObject o = v.toObject();
        const QDate d = QDate::fromString(o.value(QStringLiteral("date")).toString(), Qt::ISODate);
        const double price = o.value(QStringLiteral("price")).toDouble();
        if (!d.isValid() || price <= 0.0)
            continue;
        if (d < from || d > today)
            continue;
        // 写入国际金日线源，供 MA5日/MA20日；积存金无历史源时也可参考
        if (ExtremeDatabase::instance().upsertHistoricalClose(d, QStringLiteral("gj"), price))
            ++written;
        ExtremeDatabase::instance().upsertHistoricalClose(d, QStringLiteral("xau"), price);
    }

    m_historySeeded = true;
    Logger::info(QStringLiteral("History seed wrote %1 daily bars").arg(written));
    emit extremesUpdated();
}
