#include "AppSettings.h"

#include <QApplication>
#include <QSettings>
#include <QStandardPaths>
#include <QDir>
#include <QtGlobal>

AppSettings& AppSettings::instance()
{
    static AppSettings inst;
    return inst;
}

AppSettings::AppSettings(QObject* parent)
    : QObject(parent)
{
}

int AppSettings::refreshIntervalMs() const { return m_refreshIntervalMs; }

void AppSettings::setRefreshIntervalMs(int ms)
{
    if (m_refreshIntervalMs != ms) {
        m_refreshIntervalMs = ms;
        emit settingsChanged();
    }
}

QString AppSettings::dataSource() const { return m_dataSource; }

void AppSettings::setDataSource(const QString& source)
{
    if (m_dataSource != source) {
        m_dataSource = source;
        emit settingsChanged();
    }
}

double AppSettings::opacity() const { return m_opacity; }

void AppSettings::setOpacity(double value)
{
    value = qBound(0.3, value, 1.0);
    if (!qFuzzyCompare(m_opacity, value)) {
        m_opacity = value;
        emit settingsChanged();
    }
}

bool AppSettings::autoStart() const { return m_autoStart; }

void AppSettings::setAutoStart(bool enable)
{
    if (m_autoStart != enable) {
        m_autoStart = enable;
        emit settingsChanged();
    }
}

bool AppSettings::forecastOnline() const { return m_forecastOnline; }

void AppSettings::setForecastOnline(bool online)
{
    if (m_forecastOnline != online) {
        m_forecastOnline = online;
        emit settingsChanged();
    }
}

QString AppSettings::xaiApiKey() const { return m_xaiApiKey; }

void AppSettings::setXaiApiKey(const QString& key)
{
    if (m_xaiApiKey != key) {
        m_xaiApiKey = key;
        emit settingsChanged();
    }
}

QString AppSettings::xaiModel() const { return m_xaiModel; }

void AppSettings::setXaiModel(const QString& model)
{
    const QString m = model.isEmpty() ? QStringLiteral("grok-4.6") : model;
    if (m_xaiModel != m) {
        m_xaiModel = m;
        emit settingsChanged();
    }
}

QString AppSettings::databaseDir() const { return m_databaseDir; }

void AppSettings::setDatabaseDir(const QString& dir)
{
    const QString d = dir.trimmed();
    if (m_databaseDir != d) {
        m_databaseDir = d;
        emit settingsChanged();
    }
}

QString AppSettings::resolvedDatabaseDir() const
{
    if (!m_databaseDir.isEmpty()) {
        return QDir::cleanPath(m_databaseDir);
    }
    // 默认：系统应用数据目录
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
}

void AppSettings::load()
{
    QSettings settings(QSettings::IniFormat, QSettings::UserScope,
                       QApplication::organizationName(),
                       QApplication::applicationName());

    m_refreshIntervalMs = settings.value("refreshIntervalMs", 5000).toInt();
    m_dataSource        = settings.value("dataSource", "zs").toString();
    m_opacity           = settings.value("opacity", 0.95).toDouble();
    m_autoStart         = settings.value("autoStart", false).toBool();
    m_forecastOnline    = settings.value("forecastOnline", false).toBool();
    m_xaiApiKey         = settings.value("xaiApiKey", "").toString();
    m_xaiModel          = settings.value("xaiModel", "grok-4.6").toString();
    // 未配置或空字符串 → 使用默认路径
    m_databaseDir       = settings.value("databaseDir", "").toString().trimmed();
}

void AppSettings::save()
{
    QSettings settings(QSettings::IniFormat, QSettings::UserScope,
                       QApplication::organizationName(),
                       QApplication::applicationName());

    settings.setValue("refreshIntervalMs", m_refreshIntervalMs);
    settings.setValue("dataSource", m_dataSource);
    settings.setValue("opacity", m_opacity);
    settings.setValue("autoStart", m_autoStart);
    settings.setValue("forecastOnline", m_forecastOnline);
    settings.setValue("xaiApiKey", m_xaiApiKey);
    settings.setValue("xaiModel", m_xaiModel);
    settings.setValue("databaseDir", m_databaseDir);
    settings.sync();
}
