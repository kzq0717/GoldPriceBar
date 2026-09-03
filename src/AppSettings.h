#ifndef APPSETTINGS_H
#define APPSETTINGS_H

#include <QObject>
#include <QString>

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

    /** 深色主题（价格条/分时窗口） */
    bool darkTheme() const;
    void setDarkTheme(bool on);

    /** 分时显示 MA5 / MA20 */
    bool showMovingAverage() const;
    void setShowMovingAverage(bool on);

    /** 预警时系统蜂鸣 */
    bool alertSound() const;
    void setAlertSound(bool on);

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
};

#endif // APPSETTINGS_H
