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
    QString dcaLastExecutedDate() const;
    void setDcaLastExecutedDate(const QString& isoDate);

    bool proxyEnabled() const;
    void setProxyEnabled(bool on);
    QString proxyHost() const;
    void setProxyHost(const QString& host);
    int proxyPort() const;
    void setProxyPort(int port);
    /** 应用 QNetworkProxy::setApplicationProxy */
    void applyNetworkProxy() const;

    /** 智能预警：相对 MA5日 / 近20日分位 */
    bool smartAlertMa() const;
    void setSmartAlertMa(bool on);
    bool smartAlertPercentile() const;
    void setSmartAlertPercentile(bool on);
    int percentileLow() const;   // 0-50，默认 20
    void setPercentileLow(int v);
    int percentileHigh() const;  // 50-100，默认 80
    void setPercentileHigh(int v);

    /** 本地持仓：克数 + 成本价（元/克） */
    double positionGrams() const;
    void setPositionGrams(double g);
    double positionCost() const;
    void setPositionCost(double c);

    /** 溢价监测：主/对照价比值偏离近窗均值超过阈值则提醒（百分比） */
    bool premiumAlertEnabled() const;
    void setPremiumAlertEnabled(bool on);
    double premiumThresholdPct() const;
    void setPremiumThresholdPct(double pct);

    /** 收盘日报：每天指定时刻托盘摘要一次 */
    bool dailyReportEnabled() const;
    void setDailyReportEnabled(bool on);
    QTime dailyReportTime() const;
    void setDailyReportTime(const QTime& t);
    QString dailyReportLastDate() const;
    void setDailyReportLastDate(const QString& iso);

    bool eventAlertEnabled() const;
    void setEventAlertEnabled(bool on);
    QString eventAlertLastKey() const;
    void setEventAlertLastKey(const QString& k);


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
    QString m_dcaLastExecutedDate;
    bool m_proxyEnabled = false;
    QString m_proxyHost;
    int m_proxyPort = 7890;
    bool m_smartAlertMa = true;
    bool m_smartAlertPercentile = true;
    int m_percentileLow = 20;
    int m_percentileHigh = 80;
    double m_positionGrams = 0.0;
    double m_positionCost = 0.0;
    bool m_premiumAlertEnabled = true;
    double m_premiumThresholdPct = 2.0;
    bool m_dailyReportEnabled = true;
    QTime m_dailyReportTime = QTime(15, 5);
    QString m_dailyReportLastDate;
    bool m_eventAlertEnabled = true;
    QString m_eventAlertLastKey;


};

#endif // APPSETTINGS_H
