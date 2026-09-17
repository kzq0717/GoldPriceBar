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
}

QString SentimentService::classifyBias(const QString& text)
{
    const QString t = text;
    static const QStringList bull = {
        QStringLiteral("上涨"), QStringLiteral("走强"), QStringLiteral("突破"),
        QStringLiteral("新高"), QStringLiteral("利多"), QStringLiteral("避险买盘"),
        QStringLiteral("降息"), QStringLiteral("购金"), QStringLiteral("增持"),
        QStringLiteral("飙升"), QStringLiteral("大涨"), QStringLiteral("看涨"),
        QStringLiteral("rally"), QStringLiteral("surge"), QStringLiteral("bullish"),
    };
    static const QStringList bear = {
        QStringLiteral("下跌"), QStringLiteral("走弱"), QStringLiteral("回落"),
        QStringLiteral("新低"), QStringLiteral("利空"), QStringLiteral("加息"),
        QStringLiteral("抛售"), QStringLiteral("减持"), QStringLiteral("大跌"),
        QStringLiteral("看跌"), QStringLiteral("承压"), QStringLiteral("跌破"),
        QStringLiteral("slump"), QStringLiteral("drop"), QStringLiteral("bearish"),
    };
    int b = 0, s = 0;
    for (const auto& w : bull)
        if (t.contains(w, Qt::CaseInsensitive))
            ++b;
    for (const auto& w : bear)
        if (t.contains(w, Qt::CaseInsensitive))
            ++s;
    if (b > s)
        return QStringLiteral("bullish");
    if (s > b)
        return QStringLiteral("bearish");
    return QStringLiteral("neutral");
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
                // 过滤明显无关
                const QString blob = cur.title + cur.summary;
                if (!blob.contains(QStringLiteral("金"), Qt::CaseInsensitive)
                    && !blob.contains(QStringLiteral("gold"), Qt::CaseInsensitive)
                    && !blob.contains(QStringLiteral("XAU"), Qt::CaseInsensitive)
                    && !blob.contains(QStringLiteral("贵金属")))
                    continue;
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
