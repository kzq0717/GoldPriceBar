#include "AppSettings.h"

#include <QApplication>
#include <QSettings>
#include <QStandardPaths>
#include <QDir>
#include <QNetworkProxy>
#include <QtGlobal>

AppSettings& AppSettings::instance()
{
    static AppSettings inst;
    return inst;
}

AppSettings::AppSettings(QObject* parent) : QObject(parent) {}

int AppSettings::refreshIntervalMs() const { return m_refreshIntervalMs; }
void AppSettings::setRefreshIntervalMs(int ms)
{ if (m_refreshIntervalMs != ms) { m_refreshIntervalMs = ms; emit settingsChanged(); } }

QString AppSettings::dataSource() const { return m_dataSource; }
void AppSettings::setDataSource(const QString& source)
{ if (m_dataSource != source) { m_dataSource = source; emit settingsChanged(); } }

double AppSettings::opacity() const { return m_opacity; }
void AppSettings::setOpacity(double value)
{
    value = qBound(0.3, value, 1.0);
    if (!qFuzzyCompare(m_opacity, value)) { m_opacity = value; emit settingsChanged(); }
}

bool AppSettings::autoStart() const { return m_autoStart; }
void AppSettings::setAutoStart(bool enable)
{ if (m_autoStart != enable) { m_autoStart = enable; emit settingsChanged(); } }

bool AppSettings::forecastOnline() const { return m_forecastOnline; }
void AppSettings::setForecastOnline(bool online)
{ if (m_forecastOnline != online) { m_forecastOnline = online; emit settingsChanged(); } }

QString AppSettings::xaiApiKey() const { return m_xaiApiKey; }
void AppSettings::setXaiApiKey(const QString& key)
{ if (m_xaiApiKey != key) { m_xaiApiKey = key; emit settingsChanged(); } }

QString AppSettings::xaiModel() const { return m_xaiModel; }

QString AppSettings::llmProvider() const { return m_llmProvider; }
void AppSettings::setLlmProvider(const QString& provider)
{
    QString p = provider.trimmed().toLower();
    if (p != QStringLiteral("gemini"))
        p = QStringLiteral("xai");
    if (m_llmProvider != p) {
        m_llmProvider = p;
        emit settingsChanged();
    }
}

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
{ v = qMax(0.0, v); if (!qFuzzyCompare(m_alertHigh, v)) { m_alertHigh = v; emit settingsChanged(); } }

double AppSettings::alertLow() const { return m_alertLow; }
void AppSettings::setAlertLow(double v)
{ v = qMax(0.0, v); if (!qFuzzyCompare(m_alertLow, v)) { m_alertLow = v; emit settingsChanged(); } }

int AppSettings::alertCooldownSec() const { return m_alertCooldownSec; }
void AppSettings::setAlertCooldownSec(int sec)
{ sec = qBound(30, sec, 3600); if (m_alertCooldownSec != sec) { m_alertCooldownSec = sec; emit settingsChanged(); } }

bool AppSettings::trayNotifyOnAlert() const { return m_trayNotifyOnAlert; }
void AppSettings::setTrayNotifyOnAlert(bool on)
{ if (m_trayNotifyOnAlert != on) { m_trayNotifyOnAlert = on; emit settingsChanged(); } }

bool AppSettings::showSecondaryPrice() const { return m_showSecondaryPrice; }
void AppSettings::setShowSecondaryPrice(bool on)
{ if (m_showSecondaryPrice != on) { m_showSecondaryPrice = on; emit settingsChanged(); } }

bool AppSettings::darkTheme() const { return m_darkTheme; }
void AppSettings::setDarkTheme(bool on)
{ if (m_darkTheme != on) { m_darkTheme = on; emit settingsChanged(); } }

bool AppSettings::showMovingAverage() const { return m_showMovingAverage; }
void AppSettings::setShowMovingAverage(bool on)
{ if (m_showMovingAverage != on) { m_showMovingAverage = on; emit settingsChanged(); } }

bool AppSettings::alertSound() const { return m_alertSound; }
void AppSettings::setAlertSound(bool on)
{ if (m_alertSound != on) { m_alertSound = on; emit settingsChanged(); } }

bool AppSettings::hotkeyEnabled() const { return m_hotkeyEnabled; }
void AppSettings::setHotkeyEnabled(bool on)
{ if (m_hotkeyEnabled != on) { m_hotkeyEnabled = on; emit settingsChanged(); } }

bool AppSettings::quietHoursEnabled() const { return m_quietHoursEnabled; }
void AppSettings::setQuietHoursEnabled(bool on)
{ if (m_quietHoursEnabled != on) { m_quietHoursEnabled = on; emit settingsChanged(); } }

QTime AppSettings::quietStart() const { return m_quietStart; }
void AppSettings::setQuietStart(const QTime& t)
{
    if (t.isValid() && m_quietStart != t) { m_quietStart = t; emit settingsChanged(); }
}

QTime AppSettings::quietEnd() const { return m_quietEnd; }
void AppSettings::setQuietEnd(const QTime& t)
{
    if (t.isValid() && m_quietEnd != t) { m_quietEnd = t; emit settingsChanged(); }
}

bool AppSettings::isInQuietHours(const QTime& now) const
{
    if (!m_quietHoursEnabled || !now.isValid())
        return false;
    // 同一天：start < end → [start, end)
    // 跨天：start > end → [start, 24:00) U [00:00, end)
    if (m_quietStart == m_quietEnd)
        return true; // 全天静默（少见）
    if (m_quietStart < m_quietEnd)
        return now >= m_quietStart && now < m_quietEnd;
    return now >= m_quietStart || now < m_quietEnd;
}

int AppSettings::dcaDayOfMonth() const { return m_dcaDayOfMonth; }
void AppSettings::setDcaDayOfMonth(int day)
{
    day = qBound(0, day, 28);
    if (m_dcaDayOfMonth != day) { m_dcaDayOfMonth = day; emit settingsChanged(); }
}

QString AppSettings::dcaNote() const { return m_dcaNote; }
void AppSettings::setDcaNote(const QString& note)
{
    if (m_dcaNote != note) { m_dcaNote = note; emit settingsChanged(); }
}

QString AppSettings::dcaLastNotifiedDate() const { return m_dcaLastNotifiedDate; }
QString AppSettings::dcaLastExecutedDate() const { return m_dcaLastExecutedDate; }
void AppSettings::setDcaLastExecutedDate(const QString& iso)
{ m_dcaLastExecutedDate = iso; }

void AppSettings::setDcaLastNotifiedDate(const QString& isoDate)
{
    if (m_dcaLastNotifiedDate != isoDate) {
        m_dcaLastNotifiedDate = isoDate;
        // 不发 settingsChanged，避免循环；直接 save 由调用方负责
    }
}

bool AppSettings::proxyEnabled() const { return m_proxyEnabled; }
void AppSettings::setProxyEnabled(bool on)
{
    if (m_proxyEnabled != on) {
        m_proxyEnabled = on;
        applyNetworkProxy();
        emit settingsChanged();
    }
}

QString AppSettings::proxyHost() const { return m_proxyHost; }
void AppSettings::setProxyHost(const QString& host)
{
    const QString h = host.trimmed();
    if (m_proxyHost != h) {
        m_proxyHost = h;
        applyNetworkProxy();
        emit settingsChanged();
    }
}

int AppSettings::proxyPort() const { return m_proxyPort; }
void AppSettings::setProxyPort(int port)
{
    port = qBound(1, port, 65535);
    if (m_proxyPort != port) {
        m_proxyPort = port;
        applyNetworkProxy();
        emit settingsChanged();
    }
}

void AppSettings::applyNetworkProxy() const
{
    if (m_proxyEnabled && !m_proxyHost.isEmpty() && m_proxyPort > 0) {
        // 本地常见：Clash HTTP 7890 / SOCKS5 7891；也可手动指定
        QNetworkProxy::ProxyType type = QNetworkProxy::HttpProxy;
        const QString hostLower = m_proxyHost.toLower();
        if (m_proxyPort == 7891 || hostLower.contains(QStringLiteral("socks")))
            type = QNetworkProxy::Socks5Proxy;
        QNetworkProxy proxy(type, m_proxyHost, static_cast<quint16>(m_proxyPort));
        QNetworkProxy::setApplicationProxy(proxy);
        // Gemini / xAI / 行情请求均走 QNetworkAccessManager，会使用 application proxy
    } else {
        QNetworkProxy::setApplicationProxy(QNetworkProxy(QNetworkProxy::NoProxy));
    }
}


bool AppSettings::smartAlertMa() const { return m_smartAlertMa; }
void AppSettings::setSmartAlertMa(bool on)
{ if (m_smartAlertMa != on) { m_smartAlertMa = on; emit settingsChanged(); } }

bool AppSettings::smartAlertPercentile() const { return m_smartAlertPercentile; }
void AppSettings::setSmartAlertPercentile(bool on)
{ if (m_smartAlertPercentile != on) { m_smartAlertPercentile = on; emit settingsChanged(); } }

int AppSettings::percentileLow() const { return m_percentileLow; }
void AppSettings::setPercentileLow(int v)
{ v = qBound(1, v, 49); if (m_percentileLow != v) { m_percentileLow = v; emit settingsChanged(); } }

int AppSettings::percentileHigh() const { return m_percentileHigh; }
void AppSettings::setPercentileHigh(int v)
{ v = qBound(51, v, 99); if (m_percentileHigh != v) { m_percentileHigh = v; emit settingsChanged(); } }

double AppSettings::positionGrams() const { return m_positionGrams; }
void AppSettings::setPositionGrams(double g)
{ g = qMax(0.0, g); if (!qFuzzyCompare(m_positionGrams, g)) { m_positionGrams = g; emit settingsChanged(); } }

double AppSettings::positionCost() const { return m_positionCost; }
void AppSettings::setPositionCost(double c)
{ c = qMax(0.0, c); if (!qFuzzyCompare(m_positionCost, c)) { m_positionCost = c; emit settingsChanged(); } }

bool AppSettings::premiumAlertEnabled() const { return m_premiumAlertEnabled; }
void AppSettings::setPremiumAlertEnabled(bool on)
{ if (m_premiumAlertEnabled != on) { m_premiumAlertEnabled = on; emit settingsChanged(); } }

double AppSettings::premiumThresholdPct() const { return m_premiumThresholdPct; }
void AppSettings::setPremiumThresholdPct(double pct)
{ pct = qBound(0.1, pct, 50.0); if (!qFuzzyCompare(m_premiumThresholdPct, pct)) { m_premiumThresholdPct = pct; emit settingsChanged(); } }

bool AppSettings::dailyReportEnabled() const { return m_dailyReportEnabled; }
void AppSettings::setDailyReportEnabled(bool on)
{ if (m_dailyReportEnabled != on) { m_dailyReportEnabled = on; emit settingsChanged(); } }

QTime AppSettings::dailyReportTime() const { return m_dailyReportTime; }
void AppSettings::setDailyReportTime(const QTime& tm)
{ if (tm.isValid() && m_dailyReportTime != tm) { m_dailyReportTime = tm; emit settingsChanged(); } }

QString AppSettings::dailyReportLastDate() const { return m_dailyReportLastDate; }
void AppSettings::setDailyReportLastDate(const QString& iso)
{ m_dailyReportLastDate = iso; }

bool AppSettings::eventAlertEnabled() const { return m_eventAlertEnabled; }
void AppSettings::setEventAlertEnabled(bool on)
{ if (m_eventAlertEnabled != on) { m_eventAlertEnabled = on; emit settingsChanged(); } }
QString AppSettings::eventAlertLastKey() const { return m_eventAlertLastKey; }
void AppSettings::setEventAlertLastKey(const QString& k) { m_eventAlertLastKey = k; }



void AppSettings::load()
{
    QSettings s(QSettings::IniFormat, QSettings::UserScope,
                QApplication::organizationName(), QApplication::applicationName());
    m_refreshIntervalMs  = s.value("refreshIntervalMs", 5000).toInt();
    m_dataSource         = s.value("dataSource", "zs").toString();
    m_opacity            = s.value("opacity", 0.95).toDouble();
    m_autoStart          = s.value("autoStart", false).toBool();
    m_forecastOnline     = s.value("forecastOnline", false).toBool();
    m_xaiApiKey          = s.value("xaiApiKey", "").toString();
    m_xaiModel           = s.value("xaiModel", "grok-4.6").toString();
    m_llmProvider        = s.value("llmProvider", "xai").toString();
    if (m_llmProvider != QStringLiteral("gemini"))
        m_llmProvider = QStringLiteral("xai");
    m_databaseDir        = s.value("databaseDir", "").toString().trimmed();
    m_alertHigh          = s.value("alertHigh", 0.0).toDouble();
    m_alertLow           = s.value("alertLow", 0.0).toDouble();
    m_alertCooldownSec     = s.value("alertCooldownSec", 120).toInt();
    m_trayNotifyOnAlert  = s.value("trayNotifyOnAlert", true).toBool();
    m_showSecondaryPrice = s.value("showSecondaryPrice", false).toBool();
    m_darkTheme          = s.value("darkTheme", true).toBool();
    m_showMovingAverage  = s.value("showMovingAverage", true).toBool();
    m_alertSound         = s.value("alertSound", false).toBool();
    m_hotkeyEnabled      = s.value("hotkeyEnabled", true).toBool();
    m_quietHoursEnabled  = s.value("quietHoursEnabled", false).toBool();
    m_quietStart = QTime::fromString(s.value("quietStart", "22:00").toString(), "HH:mm");
    if (!m_quietStart.isValid()) m_quietStart = QTime(22, 0);
    m_quietEnd = QTime::fromString(s.value("quietEnd", "08:00").toString(), "HH:mm");
    if (!m_quietEnd.isValid()) m_quietEnd = QTime(8, 0);
    m_dcaDayOfMonth = s.value("dcaDayOfMonth", 0).toInt();
    m_dcaNote = s.value("dcaNote", "").toString();
    m_dcaLastNotifiedDate = s.value("dcaLastNotifiedDate", "").toString();
    m_dcaLastExecutedDate = s.value("dcaLastExecutedDate", "").toString();
    m_proxyEnabled = s.value("proxyEnabled", false).toBool();
    m_proxyHost = s.value("proxyHost", "").toString();
    m_proxyPort = s.value("proxyPort", 7890).toInt();
    m_smartAlertMa = s.value("smartAlertMa", true).toBool();
    m_smartAlertPercentile = s.value("smartAlertPercentile", true).toBool();
    m_percentileLow = s.value("percentileLow", 20).toInt();
    m_percentileHigh = s.value("percentileHigh", 80).toInt();
    m_positionGrams = s.value("positionGrams", 0.0).toDouble();
    m_positionCost = s.value("positionCost", 0.0).toDouble();
    m_premiumAlertEnabled = s.value("premiumAlertEnabled", true).toBool();
    m_premiumThresholdPct = s.value("premiumThresholdPct", 2.0).toDouble();
    m_dailyReportEnabled = s.value("dailyReportEnabled", true).toBool();
    m_dailyReportTime = QTime::fromString(s.value("dailyReportTime", "15:05").toString(), "HH:mm");
    if (!m_dailyReportTime.isValid()) m_dailyReportTime = QTime(15, 5);
    m_dailyReportLastDate = s.value("dailyReportLastDate", "").toString();
    m_eventAlertEnabled = s.value("eventAlertEnabled", true).toBool();
    m_eventAlertLastKey = s.value("eventAlertLastKey", "").toString();
    applyNetworkProxy();
}

void AppSettings::save()
{
    QSettings s(QSettings::IniFormat, QSettings::UserScope,
                QApplication::organizationName(), QApplication::applicationName());
    s.setValue("refreshIntervalMs", m_refreshIntervalMs);
    s.setValue("dataSource", m_dataSource);
    s.setValue("opacity", m_opacity);
    s.setValue("autoStart", m_autoStart);
    s.setValue("forecastOnline", m_forecastOnline);
    s.setValue("xaiApiKey", m_xaiApiKey);
    s.setValue("xaiModel", m_xaiModel);
    s.setValue("llmProvider", m_llmProvider);
    s.setValue("databaseDir", m_databaseDir);
    s.setValue("alertHigh", m_alertHigh);
    s.setValue("alertLow", m_alertLow);
    s.setValue("alertCooldownSec", m_alertCooldownSec);
    s.setValue("trayNotifyOnAlert", m_trayNotifyOnAlert);
    s.setValue("showSecondaryPrice", m_showSecondaryPrice);
    s.setValue("darkTheme", m_darkTheme);
    s.setValue("showMovingAverage", m_showMovingAverage);
    s.setValue("alertSound", m_alertSound);
    s.setValue("hotkeyEnabled", m_hotkeyEnabled);
    s.setValue("quietHoursEnabled", m_quietHoursEnabled);
    s.setValue("quietStart", m_quietStart.toString("HH:mm"));
    s.setValue("quietEnd", m_quietEnd.toString("HH:mm"));
    s.setValue("dcaDayOfMonth", m_dcaDayOfMonth);
    s.setValue("dcaNote", m_dcaNote);
    s.setValue("dcaLastNotifiedDate", m_dcaLastNotifiedDate);
    s.setValue("dcaLastExecutedDate", m_dcaLastExecutedDate);
    s.setValue("proxyEnabled", m_proxyEnabled);
    s.setValue("proxyHost", m_proxyHost);
    s.setValue("proxyPort", m_proxyPort);
    applyNetworkProxy();
    s.setValue("smartAlertMa", m_smartAlertMa);
    s.setValue("smartAlertPercentile", m_smartAlertPercentile);
    s.setValue("percentileLow", m_percentileLow);
    s.setValue("percentileHigh", m_percentileHigh);
    s.setValue("positionGrams", m_positionGrams);
    s.setValue("positionCost", m_positionCost);
    s.setValue("premiumAlertEnabled", m_premiumAlertEnabled);
    s.setValue("premiumThresholdPct", m_premiumThresholdPct);
    s.setValue("dailyReportEnabled", m_dailyReportEnabled);
    s.setValue("dailyReportTime", m_dailyReportTime.toString("HH:mm"));
    s.setValue("dailyReportLastDate", m_dailyReportLastDate);
    s.setValue("eventAlertEnabled", m_eventAlertEnabled);
    s.setValue("eventAlertLastKey", m_eventAlertLastKey);
    s.sync();
    applyNetworkProxy();
}
