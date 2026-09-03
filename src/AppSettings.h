#ifndef APPSETTINGS_H
#define APPSETTINGS_H

#include <QObject>
#include <QString>
#include <QTime>

class AppSettings : public QObject
{
    Q_OBJECT

public:
    static AppSettings& instance();

    int refreshIntervalMs() const;
    void setRefreshIntervalMs(int ms);

    QString dataSource() const;
    void setDataSource(const QString& source);

    double opacity() const;
    void setOpacity(double value);

    bool autoStart() const;
    void setAutoStart(bool enable);

    bool forecastOnline() const;
    void setForecastOnline(bool online);

    QString xaiApiKey() const;
    void setXaiApiKey(const QString& key);

    QString xaiModel() const;
    void setXaiModel(const QString& model);

    QString databaseDir() const;
    void setDatabaseDir(const QString& dir);
    QString resolvedDatabaseDir() const;

    double alertHigh() const;
    void setAlertHigh(double v);
    double alertLow() const;
    void setAlertLow(double v);

    int alertCooldownSec() const;
    void setAlertCooldownSec(int sec);

    bool trayNotifyOnAlert() const;
    void setTrayNotifyOnAlert(bool on);

    bool showSecondaryPrice() const;
    void setShowSecondaryPrice(bool on);

    bool darkTheme() const;
    void setDarkTheme(bool on);

    bool showMovingAverage() const;
    void setShowMovingAverage(bool on);

    bool alertSound() const;
    void setAlertSound(bool on);

    /** 全局热键 Ctrl+Shift+G 显隐价格条 */
    bool hotkeyEnabled() const;
    void setHotkeyEnabled(bool on);

    /** 免打扰：该时段内不托盘通知、不闪点、不蜂鸣 */
    bool quietHoursEnabled() const;
    void setQuietHoursEnabled(bool on);
    QTime quietStart() const;
    void setQuietStart(const QTime& t);
    QTime quietEnd() const;
    void setQuietEnd(const QTime& t);
    /** 当前是否处于免打扰 */
    bool isInQuietHours(const QTime& now = QTime::currentTime()) const;

    /**
     * 定投提醒：每月几号（1～28），0=关闭
     * 当天首次检查时托盘提醒一次
     */
    int dcaDayOfMonth() const;
    void setDcaDayOfMonth(int day);
    QString dcaNote() const; // 如金额说明
    void setDcaNote(const QString& note);
    QString dcaLastNotifiedDate() const; // yyyy-MM-dd
    void setDcaLastNotifiedDate(const QString& isoDate);

    bool proxyEnabled() const;
    void setProxyEnabled(bool on);
    QString proxyHost() const;
    void setProxyHost(const QString& host);
    int proxyPort() const;
    void setProxyPort(int port);
    /** 应用 QNetworkProxy::setApplicationProxy */
    void applyNetworkProxy() const;

    void load();
    void save();

signals:
    void settingsChanged();

private:
    explicit AppSettings(QObject* parent = nullptr);
    Q_DISABLE_COPY(AppSettings)

    int m_refreshIntervalMs = 5000;
    QString m_dataSource = "zs";
    double m_opacity = 0.95;
    bool m_autoStart = false;
    bool m_forecastOnline = false;
    QString m_xaiApiKey;
    QString m_xaiModel = "grok-4.6";
    QString m_databaseDir;
    double m_alertHigh = 0.0;
    double m_alertLow = 0.0;
    int m_alertCooldownSec = 120;
    bool m_trayNotifyOnAlert = true;
    bool m_showSecondaryPrice = false;
    bool m_darkTheme = true;
    bool m_showMovingAverage = true;
    bool m_alertSound = false;
    bool m_hotkeyEnabled = true;
    bool m_quietHoursEnabled = false;
    QTime m_quietStart = QTime(22, 0);
    QTime m_quietEnd = QTime(8, 0);
    int m_dcaDayOfMonth = 0;
    QString m_dcaNote;
    QString m_dcaLastNotifiedDate;
    bool m_proxyEnabled = false;
    QString m_proxyHost;
    int m_proxyPort = 7890;
};

#endif // APPSETTINGS_H
