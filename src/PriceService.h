#ifndef PRICESERVICE_H
#define PRICESERVICE_H

#include <QObject>
#include <QTimer>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPointer>

class PriceService : public QObject
{
    Q_OBJECT

public:
    explicit PriceService(QObject* parent = nullptr);
    ~PriceService() override;

    void start();
    void stop();
    void setInterval(int intervalMs);
    void forceRefresh();

    double lastPrice() const { return m_lastPrice; }
    double lastChange() const { return m_lastChange; }
    QString lastSourceName() const { return m_lastSourceName; }
    bool hasValidPrice() const { return m_hasValidPrice; }

signals:
    void priceUpdated(double price, double change, const QString& sourceName);
    void fetchFailed(const QString& error);
    /** 全日分时写入缓存后，高低价已更新 */
    void extremesUpdated();

private slots:
    void onTimeout();
    void onNetworkFinished(QNetworkReply* reply);
    void onWatchdog();
    void onChartSeedFinished(QNetworkReply* reply);
    void onChartSeedTimer();

private:
    void requestPrice();
    void abortPending();
    void recreateNetworkManager();
    void requestChartSeed();
    QString currentTypeCode() const;

    QTimer* m_timer = nullptr;
    QTimer* m_watchdog = nullptr;
    QTimer* m_chartSeedTimer = nullptr;
    QNetworkAccessManager* m_network = nullptr;
    QPointer<QNetworkReply> m_pendingReply;
    QPointer<QNetworkReply> m_pendingChart;

    double m_lastPrice = 0.0;
    double m_lastChange = 0.0;
    QString m_lastSourceName;
    bool m_hasValidPrice = false;

    int m_intervalMs = 5000;
    int m_consecutiveFail = 0;
    qint64 m_lastSuccessMs = 0;
    qint64 m_requestStartMs = 0;
    int m_persistCounter = 0;
};

#endif // PRICESERVICE_H
