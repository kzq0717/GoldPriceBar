#ifndef AGENTAISERVICE_H
#define AGENTAISERVICE_H

#include <QObject>
#include <QString>
#include <QPointer>
#include <QNetworkAccessManager>
#include <QNetworkReply>

class AgentAiService : public QObject {
    Q_OBJECT

public:
    static AgentAiService& instance();

    void askQuestion(const QString &question,
                     const QString &contextTopic = QString(),
                     const QString &provider = QString(),
                     const QString &model = QString(),
                     const QString &apiKey = QString(),
                     const QString &customEndpoint = QString());

    void cancel();
    bool isBusy() const;

    QString defaultProvider() const;
    QString defaultApiKey() const;
    QString defaultModel() const;

signals:
    void answerStarted(const QString &question);
    void answerFinished(const QString &question, const QString &answer, const QString &provider, const QString &model);
    void answerFailed(const QString &question, const QString &error);

private:
    explicit AgentAiService(QObject *parent = nullptr);
    ~AgentAiService() override;

    void processGeminiResponse(const QString &question, const QString &model, const QByteArray &data);
    void processOpenAiResponse(const QString &question, const QString &provider, const QString &model, const QByteArray &data);

    QNetworkAccessManager *m_nam = nullptr;
    QPointer<QNetworkReply> m_currentReply;
    QString m_activeQuestion;
    QString m_activeProvider;
    QString m_activeModel;
};

#endif // AGENTAISERVICE_H
