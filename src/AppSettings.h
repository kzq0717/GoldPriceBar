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

    /** 预警：现价 >= 高阈值值 → 红点闪烁；现价 <= 低预警值 → 绿点闪烁；0 表示关闭 */
    double alertHigh() const;
    void setAlertHigh(double v);
    double alertLow() const;
    void setAlertLow(double v);

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
};

#endif // APPSETTINGS_H
