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
    double lowAlreadyInProb = 0.0;   // 0~1
    double remainingUpside = 0.0;
    double peakWindowProb = 0.0;     // 0~1
    QString multiDayBias;            // 强多/偏多/震荡/...
    // 黄金经典形态策略
    QString patternName;             // 识别形态名称
    QString patternBias;             // 形态多空倾向
    QString patternStrategy;         // 形态操作策略建议
    double patternResistance = 0.0;  // 上轨/关键阻力
    double patternSupport = 0.0;     // 下轨/关键支撑
    double patternBreakoutTarget = 0.0; // 突破理论目标位
    double patternFib50 = 0.0;       // 斐波那契 0.500
    double patternFib618 = 0.0;      // 斐波那契 0.618 黄金口袋
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
