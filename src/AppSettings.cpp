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
    if (m_refreshIntervalMs != ms) { m_refreshIntervalMs = ms; emit settingsChanged(); }
}

QString AppSettings::dataSource() const { return m_dataSource; }
void AppSettings::setDataSource(const QString& source)
{
    if (m_dataSource != source) { m_dataSource = source; emit settingsChanged(); }
}

double AppSettings::opacity() const { return m_opacity; }
void AppSettings::setOpacity(double value)
{
    value = qBound(0.3, value, 1.0);
    if (!qFuzzyCompare(m_opacity, value)) { m_opacity = value; emit settingsChanged(); }
}

bool AppSettings::autoStart() const { return m_autoStart; }
void AppSettings::setAutoStart(bool enable)
{
    if (m_autoStart != enable) { m_autoStart = enable; emit settingsChanged(); }
}

bool AppSettings::forecastOnline() const { return m_forecastOnline; }
void AppSettings::setForecastOnline(bool online)
{
    if (m_forecastOnline != online) { m_forecastOnline = online; emit settingsChanged(); }
}

QString AppSettings::xaiApiKey() const { return m_xaiApiKey; }
void AppSettings::setXaiApiKey(const QString& key)
{
    if (m_xaiApiKey != key) { m_xaiApiKey = key; emit settingsChanged(); }
}

QString AppSettings::xaiModel() const { return m_xaiModel; }
void AppSettings::setXaiModel(const QString& model)
{
    const QString m = model.isEmpty() ? QStringLiteral("grok-4.6") : model;
    if (m_xaiModel != m) { m_xaiModel = m; emit settingsChanged(); }
}

QString AppSettings::databaseDir() const { return m_databaseDir; }
void AppSettings::setDatabaseDir(const QString& dir)
{
    const QString d = dir.trimmed();
    if (m_databaseDir != d) { m_databaseDir = d; emit settingsChanged(); }
}

QString AppSettings::resolvedDatabaseDir() const
{
    if (!m_databaseDir.isEmpty())
        return QDir::cleanPath(m_databaseDir);
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
}

double AppSettings::alertHigh() const { return m_alertHigh; }
void AppSettings::setAlertHigh(double v)
{
    v = qMax(0.0, v);
    if (!qFuzzyCompare(m_alertHigh, v)) { m_alertHigh = v; emit settingsChanged(); }
}

double AppSettings::alertLow() const { return m_alertLow; }
void AppSettings::setAlertLow(double v)
{
    v = qMax(0.0, v);
    if (!qFuzzyCompare(m_alertLow, v)) { m_alertLow = v; emit settingsChanged(); }
}

int AppSettings::alertCooldownSec() const { return m_alertCooldownSec; }
void AppSettings::setAlertCooldownSec(int sec)
{
    sec = qBound(30, sec, 3600);
    if (m_alertCooldownSec != sec) { m_alertCooldownSec = sec; emit settingsChanged(); }
}

bool AppSettings::trayNotifyOnAlert() const { return m_trayNotifyOnAlert; }
void AppSettings::setTrayNotifyOnAlert(bool on)
{
    if (m_trayNotifyOnAlert != on) { m_trayNotifyOnAlert = on; emit settingsChanged(); }
}

bool AppSettings::showSecondaryPrice() const { return m_showSecondaryPrice; }
void AppSettings::setShowSecondaryPrice(bool on)
{
    if (m_showSecondaryPrice != on) { m_showSecondaryPrice = on; emit settingsChanged(); }
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
    m_databaseDir       = settings.value("databaseDir", "").toString().trimmed();
    m_alertHigh         = settings.value("alertHigh", 0.0).toDouble();
    m_alertLow          = settings.value("alertLow", 0.0).toDouble();
    m_alertCooldownSec    = settings.value("alertCooldownSec", 120).toInt();
    m_trayNotifyOnAlert = settings.value("trayNotifyOnAlert", true).toBool();
    m_showSecondaryPrice = settings.value("showSecondaryPrice", false).toBool();
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
    settings.setValue("alertHigh", m_alertHigh);
    settings.setValue("alertLow", m_alertLow);
    settings.setValue("alertCooldownSec", m_alertCooldownSec);
    settings.setValue("trayNotifyOnAlert", m_trayNotifyOnAlert);
    settings.setValue("showSecondaryPrice", m_showSecondaryPrice);
    settings.sync();
}
