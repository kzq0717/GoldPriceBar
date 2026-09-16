#ifndef SENTIMENTSERVICE_H
#define SENTIMENTSERVICE_H

#include <QObject>
#include <QVector>
#include <QString>
#include <QDateTime>
#include <QPointer>
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
};

/**
 * 黄金舆情监测：拉取公开 RSS（Google 新闻等），关键词偏向标注，供热点列表展示。
 * 说明：第三方站点 tool.moxuangenet.com 无法稳定对接时，用公开 RSS 实现同类「实时热点追踪」。
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

    /** 简易文本偏向：利多 / 利空 / 中性 */
    static QString classifyBias(const QString& text);

signals:
    void updated();
    void failed(const QString& reason);

private:
    explicit SentimentService(QObject* parent = nullptr);
    void fetchNext();
    void parseRss(const QByteArray& data, const QString& feedName);
    void finishOk();

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
