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

    /**
     * 数据库目录（存放 gold_extremes.db）
     * 为空表示使用默认路径：AppDataLocation
     */
    QString databaseDir() const;
    void setDatabaseDir(const QString& dir);

    /** 解析后的实际目录：配置非空则用配置，否则默认 AppData */
    QString resolvedDatabaseDir() const;

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
    QString m_databaseDir; // 空 = 默认
};

#endif // APPSETTINGS_H
