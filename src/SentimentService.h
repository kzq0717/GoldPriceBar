#ifndef SENTIMENTSERVICE_H
#define SENTIMENTSERVICE_H

#include <QObject>
#include <QVector>
#include <QString>
#include <QDateTime>
#include <QPointer>
#include <QTimer>
#include <QNetworkAccessManager>
#include <QNetworkReply>

/** 单条黄金相关舆情/热点 */
struct SentimentItem {
    QString title;
    QString source;
    QString link;
    QDateTime published;
    /** bullish / bearish / neutral */
    QString bias;
    QString summary;
    int score = 0;
};

/** 盘面消息面冲击综合研判 */
struct SentimentImpact {
    bool hasImpact = false;
    QString bias; // "bullish" / "bearish" / "neutral"
    int netScore = 0;
    QString topTitle;
    QString topSource;
    QDateTime topTime;
    QString summary;
};

/**
 * 黄金舆情监测与快讯抓取服务：
 * 1. 支持后台自动定时抓取（可配置间隔，默认10分钟，对接新浪财经7x24快讯与公开资讯源）；
 * 2. 多维度关键词加权打分（重大利多/利空与常规多空）；
 * 3. 输出 recentMarketImpact 供行情走势研判与推演引擎融合；
 * 4. 驱动推演记录（高亮利多/利空消息驱动）。
 */
class SentimentService : public QObject
{
    Q_OBJECT

public:
    static SentimentService& instance();

    void refresh();
    bool isLoading() const { return m_loading; }
    QVector<SentimentItem> items() const { return m_items; }
    QString lastError() const { return m_lastError; }
    QDateTime lastSuccess() const { return m_lastSuccess; }

    /** 简易文本偏向：bullish / bearish / neutral */
    static QString classifyBias(const QString& text);
    /** 加权分值：正分利多，负分利空 */
    static int scoreText(const QString& text);

    /** 获取近期(默认6小时内)对盘面的关键消息面冲击 */
    SentimentImpact recentMarketImpact(int maxAgeHours = 6) const;

    void updateTimerConfig();

signals:
    void updated();
    void failed(const QString& reason);

private slots:
    void onAutoRefreshTimeout();
    void onSettingsChanged();

private:
    explicit SentimentService(QObject* parent = nullptr);
    void fetchNext();
    void parseRss(const QByteArray& data, const QString& feedName);
    void parseSinaJson(const QByteArray& data);
    void finishOk();

    QTimer* m_timer = nullptr;
    QNetworkAccessManager* m_nam = nullptr;
    QPointer<QNetworkReply> m_reply;
    QVector<SentimentItem> m_items;
    QVector<SentimentItem> m_pending;
    QStringList m_feedQueue;
    int m_feedIndex = 0;
    bool m_loading = false;
    QString m_lastError;
    QDateTime m_lastSuccess;
};

#endif
