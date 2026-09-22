#ifndef FORECASTSERVICE_H
#define FORECASTSERVICE_H

#include <QObject>
#include <QString>
#include <QDateTime>
#include <QPointer>
#include <QNetworkAccessManager>
#include <QNetworkReply>

struct ForecastResult {
    bool valid = false;
    double predHigh = 0.0;
    double predLow = 0.0;
    QString predHighTimeWindow;
    QString predLowTimeWindow;
    QString scenario;
    QString keyCatalyst;
    QString bias;
    QString brief;
    double confidence = 0.0;
    QString modeTag;
    QDateTime timestamp;
    // 1.4 决策卡片增强
    double highAlreadyInProb = 0.0;  // 0~1
    double remainingUpside = 0.0;
    double peakWindowProb = 0.0;     // 0~1
    QString multiDayBias;            // 强多/偏多/震荡/...
};

class ForecastService : public QObject {
    Q_OBJECT

public:
    static ForecastService& instance();
    static QString normalizeTo24HourTime(const QString& raw);

    void requestForecast(const QString& source = QString(), bool forceOnline = false);
    void cancelPending();

    ForecastResult lastForecast() const { return m_lastResult; }
    bool hasValidForecast() const { return m_lastResult.valid; }

signals:
    void forecastUpdated(const ForecastResult& result);
    void forecastFailed(const QString& reason);

private slots:
    void onReplyFinished();

private:
    explicit ForecastService(QObject* parent = nullptr);
    ~ForecastService() override;
    Q_DISABLE_COPY(ForecastService)

    void fallbackLocal(const QString& source, const QString& tag);
    void parseLlmResponse(const QByteArray& raw);

    QNetworkAccessManager* m_nam = nullptr;
    QPointer<QNetworkReply> m_pendingReply;
    ForecastResult m_lastResult;
    qint64 m_lastRequestMs = 0;
    QString m_currentSource;
    QString m_modelName;
    QString m_provider;
};

#endif // FORECASTSERVICE_H
