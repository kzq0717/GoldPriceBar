#include "SentimentService.h"
#include "Logger.h"

#include <QNetworkRequest>
#include <QUrl>
#include <QXmlStreamReader>
#include <QRegularExpression>
#include <QSet>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QNetworkProxy>
#include "AppSettings.h"
#include <algorithm>

SentimentService& SentimentService::instance()
{
    static SentimentService s;
    return s;
}

SentimentService::SentimentService(QObject* parent)
    : QObject(parent)
{
    m_nam = new QNetworkAccessManager(this);
    m_timer = new QTimer(this);
    connect(m_timer, &QTimer::timeout, this, &SentimentService::onAutoRefreshTimeout);
    connect(&AppSettings::instance(), &AppSettings::settingsChanged, this, &SentimentService::onSettingsChanged);
    updateTimerConfig();
    QTimer::singleShot(2500, this, [this]() {
        if (AppSettings::instance().sentimentAutoRefresh()) {
            refresh();
        }
    });
}

void SentimentService::updateTimerConfig()
{
    if (!m_timer)
        return;
    if (AppSettings::instance().sentimentAutoRefresh()) {
        const int intervalMs = AppSettings::instance().sentimentIntervalMinutes() * 60 * 1000;
        m_timer->start(qMax(60000, intervalMs));
    } else {
        m_timer->stop();
    }
}

void SentimentService::onAutoRefreshTimeout()
{
    if (AppSettings::instance().sentimentAutoRefresh() && !m_loading) {
        refresh();
    }
}

void SentimentService::onSettingsChanged()
{
    updateTimerConfig();
}

int SentimentService::scoreText(const QString& text)
{
    const QString t = text;
    // 重大利多关键词 (+3)
    static const QStringList heavyBull = {
        QStringLiteral("暴涨"), QStringLiteral("大幅降息"), QStringLiteral("超预期降息"),
        QStringLiteral("央行购金"), QStringLiteral("避险飙升"), QStringLiteral("地缘局势升级"),
        QStringLiteral("地缘冲突"), QStringLiteral("中东局势紧张"), QStringLiteral("创历史新高"),
        QStringLiteral("多头大爆发"), QStringLiteral("避险情绪升温"), QStringLiteral("避险买盘激增")
    };
    // 常规利多关键词 (+1)
    static const QStringList bull = {
        QStringLiteral("上涨"), QStringLiteral("走强"), QStringLiteral("突破"),
        QStringLiteral("新高"), QStringLiteral("利多"), QStringLiteral("避险买盘"),
        QStringLiteral("降息"), QStringLiteral("购金"), QStringLiteral("增持"),
        QStringLiteral("飙升"), QStringLiteral("大涨"), QStringLiteral("看涨"),
        QStringLiteral("反弹"), QStringLiteral("多头"),
        QStringLiteral("rally"), QStringLiteral("surge"), QStringLiteral("bullish"),
    };
    // 重大利空关键词 (-3)
    static const QStringList heavyBear = {
        QStringLiteral("暴跌"), QStringLiteral("大幅加息"), QStringLiteral("非农强劲"),
        QStringLiteral("非农大超预期"), QStringLiteral("央行抛售"), QStringLiteral("地缘和解"),
        QStringLiteral("停火"), QStringLiteral("美元指数暴涨"), QStringLiteral("重挫"),
        QStringLiteral("高位跳水"), QStringLiteral("闪崩"), QStringLiteral("创一年新低"),
        QStringLiteral("创历史新低"), QStringLiteral("连续下调"), QStringLiteral("突遭抛售"),
        QStringLiteral("跳水重挫"), QStringLiteral("单边下挫"), QStringLiteral("大幅收跌"),
        QStringLiteral("断崖式下跌"), QStringLiteral("大跌创")
    };
    // 常规利空关键词 (-1)
    static const QStringList bear = {
        QStringLiteral("下跌"), QStringLiteral("走弱"), QStringLiteral("回落"),
        QStringLiteral("新低"), QStringLiteral("利空"), QStringLiteral("加息"),
        QStringLiteral("抛售"), QStringLiteral("减持"), QStringLiteral("大跌"),
        QStringLiteral("看跌"), QStringLiteral("承压"), QStringLiteral("跌破"),
        QStringLiteral("空头"), QStringLiteral("跳水"), QStringLiteral("回调"),
        QStringLiteral("slump"), QStringLiteral("drop"), QStringLiteral("bearish"),
    };

    int score = 0;
    for (const auto& w : heavyBull) {
        if (t.contains(w, Qt::CaseInsensitive)) score += 3;
    }
    for (const auto& w : bull) {
        if (t.contains(w, Qt::CaseInsensitive)) score += 1;
    }
    for (const auto& w : heavyBear) {
        if (t.contains(w, Qt::CaseInsensitive)) score -= 3;
    }
    for (const auto& w : bear) {
        if (t.contains(w, Qt::CaseInsensitive)) score -= 1;
    }
    return score;
}

QString SentimentService::classifyBias(const QString& text)
{
    const int s = scoreText(text);
    if (s >= 1)
        return QStringLiteral("bullish");
    if (s <= -1)
        return QStringLiteral("bearish");
    return QStringLiteral("neutral");
}

SentimentImpact SentimentService::recentMarketImpact(int maxAgeHours) const
{
    SentimentImpact res;
    if (m_items.isEmpty())
        return res;

    const QDateTime now = QDateTime::currentDateTime();
    int netScore = 0;
    int topAbsScore = 0;
    SentimentItem topItem;

    for (const auto& it : m_items) {
        if (it.published.isValid() && it.published.secsTo(now) > maxAgeHours * 3600)
            continue;
        netScore += it.score;
        if (qAbs(it.score) > topAbsScore) {
            topAbsScore = qAbs(it.score);
            topItem = it;
        }
    }

    // 若按时间过滤为空，兜底取前3条
    if (topAbsScore == 0 && !m_items.isEmpty()) {
        const int checkCount = qMin(3, m_items.size());
        for (int i = 0; i < checkCount; ++i) {
            const auto& it = m_items.at(i);
            netScore += it.score;
            if (qAbs(it.score) > topAbsScore) {
                topAbsScore = qAbs(it.score);
                topItem = it;
            }
        }
    }

    res.netScore = netScore;
    // 关键修正：必须根据净得分与最强单项的真实正负符号（而非绝对值）判断利多还是利空！
    // 严禁使用 topAbsScore >= 2 判定利多，否则大跌/暴跌（score=-3）会被绝对值(3>=2)严重误判为“利多驱动”！
    const int effectiveDir = (qAbs(netScore) >= 2) ? netScore : topItem.score;
    if (effectiveDir >= 2 || netScore >= 2) {
        res.hasImpact = true;
        res.bias = QStringLiteral("bullish");
        res.topTitle = topItem.title.isEmpty() ? tr("多项消息面偏向利多") : topItem.title;
        res.topSource = topItem.source;
        res.topTime = topItem.published;
        res.summary = QStringLiteral("【利多消息驱动】%1").arg(res.topTitle);
    } else if (effectiveDir <= -2 || netScore <= -2) {
        res.hasImpact = true;
        res.bias = QStringLiteral("bearish");
        res.topTitle = topItem.title.isEmpty() ? tr("多项消息面偏向利空") : topItem.title;
        res.topSource = topItem.source;
        res.topTime = topItem.published;
        res.summary = QStringLiteral("【利空消息驱动】%1").arg(res.topTitle);
    } else {
        res.hasImpact = false;
        res.bias = QStringLiteral("neutral");
        res.summary = QStringLiteral("【消息面中性整理】");
    }
    return res;
}

void SentimentService::refresh()
{
    if (m_loading)
        return;
    m_loading = true;
    m_lastError.clear();
    m_pending.clear();
    m_feedIndex = 0;
    // 新浪财经国内黄金滚动资讯（免翻墙、国内极速）+ 谷歌新闻公开 RSS
    m_feedQueue = {
        QStringLiteral(
            "https://feed.mix.sina.com.cn/api/roll/get?pageid=153&lid=2516&k=%E9%BB%84%E9%87%91&num=20&page=1"),
        QStringLiteral(
            "https://news.google.com/rss/search?q=%E9%BB%84%E9%87%91%20OR%20%E9%87%91%E4%BB%B7%20OR%20XAU&hl=zh-CN&gl=CN&ceid=CN:zh-Hans"),
        QStringLiteral(
            "https://news.google.com/rss/search?q=gold%20price%20OR%20XAUUSD&hl=en-US&gl=US&ceid=US:en"),
    };
    fetchNext();
}

void SentimentService::fetchNext()
{
    if (m_feedIndex >= m_feedQueue.size()) {
        finishOk();
        return;
    }
    if (m_reply) {
        m_reply->abort();
        m_reply.clear();
    }
    const QUrl url(m_feedQueue.at(m_feedIndex));
    // 新浪国内资讯强制直连不走代理；境外资讯按用户设置决定是否走代理
    if (m_feedIndex == 0) {
        m_nam->setProxy(QNetworkProxy(QNetworkProxy::NoProxy));
    } else {
        if (AppSettings::instance().proxyEnabled()) {
            QNetworkProxy p(QNetworkProxy::HttpProxy,
                            AppSettings::instance().proxyHost(),
                            static_cast<quint16>(AppSettings::instance().proxyPort()));
            m_nam->setProxy(p);
        } else {
            m_nam->setProxy(QNetworkProxy(QNetworkProxy::NoProxy));
        }
    }
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader,
                  QStringLiteral("Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36"));
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setTransferTimeout(10000);
    m_reply = m_nam->get(req);
    connect(m_reply, &QNetworkReply::finished, this, [this]() {
        QNetworkReply* reply = m_reply.data();
        m_reply.clear();
        if (!reply) {
            ++m_feedIndex;
            fetchNext();
            return;
        }
        QString feedName = QStringLiteral("新浪财经·黄金");
        if (m_feedIndex == 1)
            feedName = QStringLiteral("谷歌新闻·中文");
        else if (m_feedIndex == 2)
            feedName = QStringLiteral("谷歌新闻·英文");

        if (reply->error() != QNetworkReply::NoError
            && reply->error() != QNetworkReply::OperationCanceledError) {
            Logger::warn(QStringLiteral("Sentiment feed fail: %1 %2")
                             .arg(feedName, reply->errorString()));
            m_lastError = reply->errorString();
        } else {
            const QByteArray body = reply->readAll();
            if (body.trimmed().startsWith('{')) {
                parseSinaJson(body);
            } else {
                parseRss(body, feedName);
            }
        }
        reply->deleteLater();
        ++m_feedIndex;
        fetchNext();
    });
}

void SentimentService::parseSinaJson(const QByteArray& data)
{
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(data, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject())
        return;
    const QJsonObject root = doc.object();
    const QJsonObject result = root.value(QStringLiteral("result")).toObject();
    const QJsonArray arr = result.value(QStringLiteral("data")).toArray();
    for (const QJsonValue& v : arr) {
        if (!v.isObject())
            continue;
        const QJsonObject o = v.toObject();
        SentimentItem item;
        item.title = o.value(QStringLiteral("title")).toString().trimmed();
        if (item.title.isEmpty())
            continue;
        item.summary = o.value(QStringLiteral("intro")).toString().trimmed();
        item.link = o.value(QStringLiteral("url")).toString().trimmed();
        item.source = o.value(QStringLiteral("media_name")).toString().trimmed();
        if (item.source.isEmpty())
            item.source = QStringLiteral("新浪财经");

        qint64 ctime = o.value(QStringLiteral("ctime")).toString().toLongLong();
        if (ctime <= 0)
            ctime = static_cast<qint64>(o.value(QStringLiteral("ctime")).toDouble());
        if (ctime > 0)
            item.published = QDateTime::fromSecsSinceEpoch(ctime);
        else
            item.published = QDateTime::currentDateTime();

        const QString blob = item.title + QStringLiteral(" ") + item.summary;
        // 严格资讯相关性过滤：坚决排除农产品、生猪、白糖等无关期货晨报，确保聚焦黄金与贵金属
        const bool isGoldRelated = blob.contains(QStringLiteral("黄金"), Qt::CaseInsensitive)
                                || blob.contains(QStringLiteral("金价"), Qt::CaseInsensitive)
                                || blob.contains(QStringLiteral("贵金属"), Qt::CaseInsensitive)
                                || blob.contains(QStringLiteral("伦敦金"), Qt::CaseInsensitive)
                                || blob.contains(QStringLiteral("积存金"), Qt::CaseInsensitive)
                                || blob.contains(QStringLiteral("金饰"), Qt::CaseInsensitive)
                                || blob.contains(QStringLiteral("金条"), Qt::CaseInsensitive)
                                || blob.contains(QStringLiteral("现货黄金"), Qt::CaseInsensitive)
                                || blob.contains(QStringLiteral("XAU"), Qt::CaseInsensitive)
                                || (blob.contains(QStringLiteral("央行"), Qt::CaseInsensitive) && blob.contains(QStringLiteral("购金"), Qt::CaseInsensitive));
        const bool isIrrelevant = item.title.contains(QStringLiteral("农产品"))
                               || item.title.contains(QStringLiteral("生猪"))
                               || item.title.contains(QStringLiteral("白糖"))
                               || item.title.contains(QStringLiteral("玉米"))
                               || item.title.contains(QStringLiteral("棉花"))
                               || item.title.contains(QStringLiteral("大豆"));
        if (!isGoldRelated || isIrrelevant)
            continue;

        item.score = scoreText(blob);
        item.bias = classifyBias(blob);
        m_pending.append(item);
    }
}

void SentimentService::parseRss(const QByteArray& data, const QString& feedName)
{
    if (data.isEmpty())
        return;
    QXmlStreamReader xml(data);
    SentimentItem cur;
    bool inItem = false;
    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isStartElement()) {
            const QString n = xml.name().toString();
            if (n == QLatin1String("item") || n == QLatin1String("entry")) {
                inItem = true;
                cur = SentimentItem{};
                cur.source = feedName;
            } else if (inItem) {
                if (n == QLatin1String("title"))
                    cur.title = xml.readElementText().trimmed();
                else if (n == QLatin1String("link")) {
                    QString href = xml.attributes().value(QStringLiteral("href")).toString();
                    if (href.isEmpty())
                        href = xml.readElementText().trimmed();
                    cur.link = href;
                } else if (n == QLatin1String("pubDate") || n == QLatin1String("published")
                           || n == QLatin1String("updated")) {
                    const QString raw = xml.readElementText().trimmed();
                    QDateTime dt = QDateTime::fromString(raw, Qt::RFC2822Date);
                    if (!dt.isValid())
                        dt = QDateTime::fromString(raw, Qt::ISODate);
                    cur.published = dt.toLocalTime();
                } else if (n == QLatin1String("description") || n == QLatin1String("summary")) {
                    QString desc = xml.readElementText();
                    desc.remove(QRegularExpression(QStringLiteral("<[^>]+>")));
                    cur.summary = desc.trimmed().left(200);
                }
            }
        } else if (xml.isEndElement()) {
            const QString n = xml.name().toString();
            if ((n == QLatin1String("item") || n == QLatin1String("entry")) && inItem) {
                inItem = false;
                if (cur.title.isEmpty())
                    continue;
                // 过滤明显无关资讯
                const QString blob = cur.title + cur.summary;
                const bool isGoldRelated = blob.contains(QStringLiteral("黄金"), Qt::CaseInsensitive)
                                        || blob.contains(QStringLiteral("金价"), Qt::CaseInsensitive)
                                        || blob.contains(QStringLiteral("贵金属"), Qt::CaseInsensitive)
                                        || blob.contains(QStringLiteral("伦敦金"), Qt::CaseInsensitive)
                                        || blob.contains(QStringLiteral("积存金"), Qt::CaseInsensitive)
                                        || blob.contains(QStringLiteral("金饰"), Qt::CaseInsensitive)
                                        || blob.contains(QStringLiteral("gold"), Qt::CaseInsensitive)
                                        || blob.contains(QStringLiteral("XAU"), Qt::CaseInsensitive);
                const bool isIrrelevant = cur.title.contains(QStringLiteral("农产品"))
                                       || cur.title.contains(QStringLiteral("生猪"))
                                       || cur.title.contains(QStringLiteral("大豆"));
                if (!isGoldRelated || isIrrelevant)
                    continue;
                cur.score = scoreText(blob);
                cur.bias = classifyBias(blob);
                m_pending.append(cur);
            }
        }
    }
}

void SentimentService::finishOk()
{
    // 去重标题
    QSet<QString> seen;
    QVector<SentimentItem> uniq;
    for (const auto& it : m_pending) {
        const QString key = it.title.left(40);
        if (seen.contains(key))
            continue;
        seen.insert(key);
        uniq.append(it);
    }
    std::sort(uniq.begin(), uniq.end(), [](const SentimentItem& a, const SentimentItem& b) {
        return a.published > b.published;
    });
    if (uniq.size() > 40)
        uniq.resize(40);
    m_items = uniq;
    m_loading = false;
    if (!m_items.isEmpty()) {
        m_lastSuccess = QDateTime::currentDateTime();
        m_lastError.clear();
        Logger::info(QStringLiteral("Sentiment loaded %1 items").arg(m_items.size()));
        emit updated();
    } else {
        if (m_lastError.isEmpty())
            m_lastError = QStringLiteral("网络异常：未能获取黄金舆情资讯（请检查网络连接或代理配置）");
        Logger::warn(QStringLiteral("Sentiment failed: %1").arg(m_lastError));
        emit failed(m_lastError);
    }
}
