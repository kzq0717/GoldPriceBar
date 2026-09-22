#include "QmlBridge.h"
#include "PriceService.h"
#include "SentimentService.h"
#include "AppSettings.h"
#include "ExtremeDatabase.h"
#include "HistoryCache.h"
#include "ChartWindow.h"
#include "SettingsDialog.h"
#include "EventCalendar.h"
#include "TradingSession.h"
#include "UpdateChecker.h"
#include "Logger.h"
#include "goldsdk/forecast.hpp"
#include "ForecastService.h"

#include <QDesktopServices>
#include <QMessageBox>
#include <QUrl>
#include <QMenu>
#include <QAction>
#include <QApplication>
#include <QIcon>
#include <QDate>
#include <QDir>
#include <QFileDialog>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QNetworkProxy>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QQuickWindow>

QmlBridge::QmlBridge(PriceService *priceService, QObject *parent)
    : QObject(parent), m_priceService(priceService)
{
    if (m_priceService) {
        connect(m_priceService, &PriceService::priceUpdated, this, &QmlBridge::onPriceUpdated);
        connect(m_priceService, &PriceService::fetchFailed, this, &QmlBridge::onFetchFailed);
        connect(m_priceService, &PriceService::extremesUpdated, this, &QmlBridge::onExtremesUpdated);

        if (m_priceService->hasValidPrice()) {
            m_price = m_priceService->lastPrice();
            m_priceChange = m_priceService->lastChange();
            m_sourceName = m_priceService->lastSourceName();
            m_hasValidPrice = true;
            m_statusText = tr("正常");
            m_statusLevel = 0;
            m_updateTime = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"));
            const double prev = m_price - m_priceChange;
            m_changePct = (prev > 0.0) ? (m_priceChange / prev * 100.0) : 0.0;
            updateExtremes();
            updateAlertIndicator(m_price);
            evaluateSmartAlerts(m_price);
            evaluateDecision(m_price);
            updatePnL(m_price);
        } else {
            m_sourceName = PriceService::canonicalSourceName(AppSettings::instance().dataSource());
        }
    } else {
        m_sourceName = PriceService::canonicalSourceName(AppSettings::instance().dataSource());
    }

    connect(&SentimentService::instance(), &SentimentService::updated, this, &QmlBridge::onSentimentUpdated);
    connect(&SentimentService::instance(), &SentimentService::failed, this, &QmlBridge::onSentimentFailed);
    connect(&ForecastService::instance(), &ForecastService::forecastUpdated, this, &QmlBridge::onForecastUpdated);

    setupTray();

    m_secondaryTimer = new QTimer(this);
    const int secMs = qMax(1000, AppSettings::instance().refreshIntervalMs());
    m_secondaryTimer->setInterval(secMs);
    connect(m_secondaryTimer, &QTimer::timeout, this, &QmlBridge::onSecondaryTimer);
    if (AppSettings::instance().showSecondaryPrice()) {
        m_secondaryTimer->start();
        QTimer::singleShot(500, this, &QmlBridge::onSecondaryTimer);
    }

    m_checkTimer = new QTimer(this);
    m_checkTimer->setInterval(60000); // 60秒巡检
    connect(m_checkTimer, &QTimer::timeout, this, [this]() {
        checkDcaReminder();
        checkDailyReport();
        checkEventAlerts();
        // 每 20 分钟定期在线刷新一次预测（若开启大模型）
        static int checkCount = 0;
        if (++checkCount % 20 == 0) {
            if (AppSettings::instance().forecastOnline()) {
                ForecastService::instance().requestForecast();
            }
        }
    });
    m_checkTimer->start();

    // 延迟 2.5 秒预检定投、宏观日程并启动初始预测
    QTimer::singleShot(2500, this, [this]() {
        checkDcaReminder();
        checkEventAlerts();
        ForecastService::instance().requestForecast();
    });
}

QmlBridge::~QmlBridge() {
    if (m_checkTimer) {
        m_checkTimer->stop();
    }
    if (m_secondaryReply) {
        m_secondaryReply->abort();
        m_secondaryReply->deleteLater();
    }
    if (m_chartWindow) {
        delete m_chartWindow;
        m_chartWindow = nullptr;
    }
    if (m_settingsDialog) {
        delete m_settingsDialog;
        m_settingsDialog = nullptr;
    }
    if (m_tray) {
        m_tray->hide();
        delete m_tray;
        m_tray = nullptr;
    }
}

void QmlBridge::setupTray() {
    if (m_tray)
        return;

    m_tray = new QSystemTrayIcon(this);
    m_tray->setIcon(QIcon(QStringLiteral(":/app.png")));
    m_tray->setToolTip(QStringLiteral("GoldPriceBarLite"));

    auto *menu = new QMenu();
    auto *actToggle = menu->addAction(tr("显示/隐藏价格条"));
    connect(actToggle, &QAction::triggered, this, &QmlBridge::requestTogglePriceBar);

    auto *actSentiment = menu->addAction(tr("黄金实时舆情"));
    connect(actSentiment, &QAction::triggered, this, &QmlBridge::openSentimentWindow);

    auto *actChart = menu->addAction(tr("分时走势图"));
    connect(actChart, &QAction::triggered, this, &QmlBridge::openChartWindow);

    auto *actDaily = menu->addAction(tr("今日收盘摘要"));
    connect(actDaily, &QAction::triggered, this, &QmlBridge::showDailyReportDialog);

    auto *actDca = menu->addAction(tr("今日已定投（打卡）"));
    connect(actDca, &QAction::triggered, this, &QmlBridge::markDcaExecutedToday);

    auto *actSettings = menu->addAction(tr("设置"));
    connect(actSettings, &QAction::triggered, this, &QmlBridge::openSettingsWindow);

    menu->addSeparator();

    auto *actQuit = menu->addAction(tr("退出"));
    connect(actQuit, &QAction::triggered, this, &QmlBridge::quitApp);

    m_tray->setContextMenu(menu);
    connect(m_tray, &QSystemTrayIcon::activated, this, &QmlBridge::onTrayActivated);
    m_tray->show();
}

void QmlBridge::onTrayActivated(QSystemTrayIcon::ActivationReason reason) {
    if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick) {
        emit requestTogglePriceBar();
    }
}

void QmlBridge::updateExtremes() {
    double h = 0.0;
    if (HistoryCache::instance().todayHigh(h) && h > 0.0) {
        m_highPrice = h;
    } else {
        m_highPrice = m_price;
    }

    double l = 0.0;
    if (HistoryCache::instance().todayLow(l) && l > 0.0) {
        m_lowPrice = l;
    } else {
        m_lowPrice = m_price;
    }
}

void QmlBridge::onPriceUpdated(double price, double change, const QString &sourceName) {
    m_price = price;
    m_priceChange = change;
    m_sourceName = sourceName;
    m_hasValidPrice = true;
    m_statusText = tr("正常");
    m_statusLevel = 0;
    m_updateTime = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"));

    const double prev = price - change;
    m_changePct = (prev > 0.0) ? (change / prev * 100.0) : 0.0;

    updateExtremes();
    updateAlertIndicator(price);
    evaluateSmartAlerts(price);
    evaluatePremium(price);
    checkPlanAlerts(price);
    evaluateDecision(price);
    updatePnL(price);

    if (m_tray) {
        m_tray->setToolTip(QStringLiteral("GoldPriceBar - %1: %2 (%3%4)")
                               .arg(sourceName)
                               .arg(price, 0, 'f', 2)
                               .arg(change >= 0.0 ? "+" : "")
                               .arg(change, 0, 'f', 2));
    }

    emit priceChanged();
}

void QmlBridge::onFetchFailed(const QString &error) {
    m_statusText = error;
    m_statusLevel = 2;
    emit priceChanged();
}

void QmlBridge::onExtremesUpdated() {
    updateExtremes();
    emit priceChanged();
}

void QmlBridge::onSentimentUpdated() {
    const auto list = SentimentService::instance().items();
    m_sentimentItems.clear();
    int bull = 0, bear = 0, neutral = 0;

    for (const auto &it : list) {
        QVariantMap m;
        m[QStringLiteral("title")] = it.title;
        m[QStringLiteral("source")] = it.source;
        m[QStringLiteral("link")] = it.link;
        m[QStringLiteral("published")] = it.published.toString(QStringLiteral("MM-dd HH:mm"));
        m[QStringLiteral("bias")] = it.bias;
        m[QStringLiteral("summary")] = it.summary;
        m_sentimentItems.append(m);

        if (it.bias == QStringLiteral("bullish")) {
            ++bull;
        } else if (it.bias == QStringLiteral("bearish")) {
            ++bear;
        } else {
            ++neutral;
        }
    }

    m_sentimentBullCount = bull;
    m_sentimentBearCount = bear;
    m_sentimentNeutralCount = neutral;
    const int total = bull + bear + neutral;
    m_sentimentBullRatio = (total > 0) ? (static_cast<double>(bull) / static_cast<double>(total)) : 0.5;

    m_sentimentLoading = false;
    m_sentimentNetworkError = false;
    m_sentimentError.clear();

    emit sentimentChanged();
}

void QmlBridge::onSentimentFailed(const QString &reason) {
    m_sentimentLoading = false;
    m_sentimentNetworkError = true;
    m_sentimentError = reason;
    emit sentimentChanged();
}

bool QmlBridge::sentimentEnabled() const {
    return AppSettings::instance().sentimentEnabled();
}

void QmlBridge::setSentimentEnabled(bool on) {
    AppSettings::instance().setSentimentEnabled(on);
    emit settingsChanged();
    if (on) {
        // 用户在设置勾选开启黄金舆情时直接弹出窗口
        openSentimentWindow();
    }
}

void QmlBridge::refreshPrice() {
    if (m_priceService) {
        m_statusText = tr("正在刷新...");
        m_statusLevel = 1;
        emit priceChanged();
        m_priceService->forceRefresh();
    }
    if (AppSettings::instance().showSecondaryPrice()) {
        if (m_secondaryReply) {
            m_secondaryReply->abort();
            m_secondaryReply.clear();
        }
        onSecondaryTimer();
    }
}

void QmlBridge::refreshSentiment() {
    m_sentimentLoading = true;
    m_sentimentNetworkError = false;
    emit sentimentChanged();
    SentimentService::instance().refresh();
}

void QmlBridge::openSentimentWindow() {
    Logger::info(QStringLiteral("QmlBridge::openSentimentWindow called"));
    if (m_sentimentWindow) {
        m_sentimentWindow->setVisible(true);
        m_sentimentWindow->show();
        m_sentimentWindow->raise();
        m_sentimentWindow->requestActivate();
    }
    emit requestShowSentiment();
    if (m_sentimentItems.isEmpty() && !m_sentimentLoading) {
        refreshSentiment();
    }
}

void QmlBridge::openSettingsWindow() {
    Logger::info(QStringLiteral("QmlBridge::openSettingsWindow called (QML SettingsWindow)"));
    if (m_settingsWindow) {
        m_settingsWindow->setVisible(true);
        m_settingsWindow->show();
        m_settingsWindow->raise();
        m_settingsWindow->requestActivate();
    }
    emit requestShowSettings();
}

void QmlBridge::onSettingsDialogAccepted() {
    const auto &settings = AppSettings::instance();
    m_sourceName = PriceService::canonicalSourceName(settings.dataSource());
    if (m_priceService) {
        m_priceService->setInterval(settings.refreshIntervalMs());
        m_priceService->forceRefresh();
    }
    if (m_chartWindow && m_chartWindow->isVisible()) {
        m_chartWindow->refreshData();
    }
    if (settings.sentimentEnabled()) {
        openSentimentWindow();
    }
    emit priceChanged();
    emit settingsChanged();
}

void QmlBridge::openChartWindow() {
    if (!m_chartWindow) {
        m_chartWindow = new ChartWindow(nullptr);
        m_chartWindow->setAttribute(Qt::WA_DeleteOnClose, false);
        if (m_priceService) {
            connect(m_priceService, &PriceService::priceUpdated, m_chartWindow, &ChartWindow::onNewPrice,
                    Qt::QueuedConnection);
        }
    }
    m_chartWindow->show();
    m_chartWindow->raise();
    m_chartWindow->activateWindow();
    m_chartWindow->refreshData();
    if (m_priceService && m_priceService->hasValidPrice()) {
        m_chartWindow->onNewPrice(m_priceService->lastPrice(), m_priceService->lastChange(), m_priceService->lastSourceName());
    }
}

void QmlBridge::openUrl(const QString &url) {
    if (!url.isEmpty()) {
        QDesktopServices::openUrl(QUrl(url));
    }
}

void QmlBridge::quitApp() {
    QApplication::quit();
}

double QmlBridge::windowOpacity() const {
    return AppSettings::instance().opacity();
}

void QmlBridge::setWindowOpacity(double op) {
    AppSettings::instance().setOpacity(op);
    emit settingsChanged();
}

int QmlBridge::refreshIntervalSec() const {
    return AppSettings::instance().refreshIntervalMs() / 1000;
}

void QmlBridge::setRefreshIntervalSec(int sec) {
    if (sec < 1) sec = 1;
    AppSettings::instance().setRefreshIntervalMs(sec * 1000);
    if (m_priceService) {
        m_priceService->setInterval(sec * 1000);
    }
    emit settingsChanged();
}

QString QmlBridge::dataSource() const {
    return AppSettings::instance().dataSource();
}

void QmlBridge::setDataSource(const QString &src) {
    AppSettings::instance().setDataSource(src);
    m_sourceName = PriceService::canonicalSourceName(src);
    emit priceChanged();
    if (m_priceService) {
        m_priceService->forceRefresh();
    }
    if (m_chartWindow && m_chartWindow->isVisible()) {
        m_chartWindow->refreshData();
    }
    emit settingsChanged();
}

bool QmlBridge::proxyEnabled() const {
    return AppSettings::instance().proxyEnabled();
}

void QmlBridge::setProxyEnabled(bool en) {
    AppSettings::instance().setProxyEnabled(en);
    AppSettings::instance().applyNetworkProxy();
    emit settingsChanged();
}

QString QmlBridge::proxyHost() const {
    return AppSettings::instance().proxyHost();
}

void QmlBridge::setProxyHost(const QString &host) {
    AppSettings::instance().setProxyHost(host);
    AppSettings::instance().applyNetworkProxy();
    emit settingsChanged();
}

int QmlBridge::proxyPort() const {
    return AppSettings::instance().proxyPort();
}

void QmlBridge::setProxyPort(int port) {
    AppSettings::instance().setProxyPort(port);
    AppSettings::instance().applyNetworkProxy();
    emit settingsChanged();
}

void QmlBridge::saveSettings(const QString &dataSource, int refreshIntervalSec, double opacity,
                             bool proxyEnabled, const QString &proxyHost, int proxyPort,
                             bool sentimentEnabled) {
    setDataSource(dataSource);
    setRefreshIntervalSec(refreshIntervalSec);
    setWindowOpacity(opacity);
    setProxyHost(proxyHost);
    setProxyPort(proxyPort);
    setProxyEnabled(proxyEnabled);
    setSentimentEnabled(sentimentEnabled);
    AppSettings::instance().save();

    const int secMs = qMax(1000, refreshIntervalSec * 1000);
    if (m_secondaryTimer) {
        m_secondaryTimer->setInterval(secMs);
        if (AppSettings::instance().showSecondaryPrice()) {
            if (!m_secondaryTimer->isActive())
                m_secondaryTimer->start();
            onSecondaryTimer();
        } else {
            m_secondaryTimer->stop();
            m_secondaryPriceText.clear();
            emit secondaryPriceChanged();
        }
    }
    if (m_price > 0.0) {
        updateAlertIndicator(m_price);
    }
}

QVariantMap QmlBridge::getAllSettings() const {
    const auto &s = AppSettings::instance();
    QVariantMap m;
    // 行情与显示
    m["refreshIntervalSec"] = s.refreshIntervalMs() / 1000;
    m["dataSource"] = s.dataSource();
    m["sentimentEnabled"] = s.sentimentEnabled();
    m["windowOpacity"] = s.opacity();
    m["showSecondaryPrice"] = s.showSecondaryPrice();
    m["darkTheme"] = s.darkTheme();
    m["showMovingAverage"] = s.showMovingAverage();
    m["hotkeyEnabled"] = s.hotkeyEnabled();
    m["autoStart"] = s.autoStart();

    // 预警与计划
    m["alertHigh"] = s.alertHigh();
    m["alertLow"] = s.alertLow();
    m["alertCooldownSec"] = s.alertCooldownSec();
    m["trayNotifyOnAlert"] = s.trayNotifyOnAlert();
    m["alertSound"] = s.alertSound();
    m["quietHoursEnabled"] = s.quietHoursEnabled();
    m["quietStart"] = s.quietStart().toString(QStringLiteral("HH:mm"));
    m["quietEnd"] = s.quietEnd().toString(QStringLiteral("HH:mm"));
    m["smartAlertMa"] = s.smartAlertMa();
    m["smartAlertPercentile"] = s.smartAlertPercentile();
    m["percentileLow"] = s.percentileLow();
    m["percentileHigh"] = s.percentileHigh();
    m["premiumAlertEnabled"] = s.premiumAlertEnabled();
    m["premiumThresholdPct"] = s.premiumThresholdPct();
    m["planEnabled"] = s.planEnabled();
    m["planBuyPrice"] = s.planBuyPrice();
    m["planSellPrice"] = s.planSellPrice();
    m["planInvalidPrice"] = s.planInvalidPrice();

    // 价格预测 (AI)
    m["forecastOnline"] = s.forecastOnline();
    m["forecastIntervalSec"] = s.forecastIntervalSec();
    m["llmProvider"] = s.llmProvider();
    m["xaiApiKey"] = s.xaiApiKey();
    m["xaiModel"] = s.xaiModel();

    // 持仓与报告
    m["positionGrams"] = s.positionGrams();
    m["positionCost"] = s.positionCost();
    m["dailyReportEnabled"] = s.dailyReportEnabled();
    m["dailyReportTime"] = s.dailyReportTime().toString(QStringLiteral("HH:mm"));
    m["dcaDayOfMonth"] = s.dcaDayOfMonth();
    m["dcaNote"] = s.dcaNote();
    m["eventAlertEnabled"] = s.eventAlertEnabled();

    // 高级与数据
    m["primaryPriceUrl"] = s.primaryPriceUrl();
    m["chartUrl"] = s.chartUrl();
    m["databaseDir"] = s.databaseDir();
    m["proxyEnabled"] = s.proxyEnabled();
    m["proxyHost"] = s.proxyHost();
    m["proxyPort"] = s.proxyPort();

    return m;
}

void QmlBridge::saveAllSettings(const QVariantMap &m) {
    auto &s = AppSettings::instance();
    const QString oldDbDir = s.databaseDir();

    if (m.contains("refreshIntervalSec")) s.setRefreshIntervalMs(m.value("refreshIntervalSec").toInt() * 1000);
    if (m.contains("dataSource")) s.setDataSource(m.value("dataSource").toString());
    if (m.contains("sentimentEnabled")) s.setSentimentEnabled(m.value("sentimentEnabled").toBool());
    if (m.contains("windowOpacity")) s.setOpacity(m.value("windowOpacity").toDouble());
    if (m.contains("showSecondaryPrice")) s.setShowSecondaryPrice(m.value("showSecondaryPrice").toBool());
    if (m.contains("darkTheme")) s.setDarkTheme(m.value("darkTheme").toBool());
    if (m.contains("showMovingAverage")) s.setShowMovingAverage(m.value("showMovingAverage").toBool());
    if (m.contains("hotkeyEnabled")) s.setHotkeyEnabled(m.value("hotkeyEnabled").toBool());
    if (m.contains("autoStart")) s.setAutoStart(m.value("autoStart").toBool());

    if (m.contains("alertHigh")) s.setAlertHigh(m.value("alertHigh").toDouble());
    if (m.contains("alertLow")) s.setAlertLow(m.value("alertLow").toDouble());
    if (m.contains("alertCooldownSec")) s.setAlertCooldownSec(m.value("alertCooldownSec").toInt());
    if (m.contains("trayNotifyOnAlert")) s.setTrayNotifyOnAlert(m.value("trayNotifyOnAlert").toBool());
    if (m.contains("alertSound")) s.setAlertSound(m.value("alertSound").toBool());
    if (m.contains("quietHoursEnabled")) s.setQuietHoursEnabled(m.value("quietHoursEnabled").toBool());
    if (m.contains("quietStart")) s.setQuietStart(QTime::fromString(m.value("quietStart").toString(), QStringLiteral("HH:mm")));
    if (m.contains("quietEnd")) s.setQuietEnd(QTime::fromString(m.value("quietEnd").toString(), QStringLiteral("HH:mm")));
    if (m.contains("smartAlertMa")) s.setSmartAlertMa(m.value("smartAlertMa").toBool());
    if (m.contains("smartAlertPercentile")) s.setSmartAlertPercentile(m.value("smartAlertPercentile").toBool());
    if (m.contains("percentileLow")) s.setPercentileLow(m.value("percentileLow").toInt());
    if (m.contains("percentileHigh")) s.setPercentileHigh(m.value("percentileHigh").toInt());
    if (m.contains("premiumAlertEnabled")) s.setPremiumAlertEnabled(m.value("premiumAlertEnabled").toBool());
    if (m.contains("premiumThresholdPct")) s.setPremiumThresholdPct(m.value("premiumThresholdPct").toDouble());
    if (m.contains("planEnabled")) s.setPlanEnabled(m.value("planEnabled").toBool());
    if (m.contains("planBuyPrice")) s.setPlanBuyPrice(m.value("planBuyPrice").toDouble());
    if (m.contains("planSellPrice")) s.setPlanSellPrice(m.value("planSellPrice").toDouble());
    if (m.contains("planInvalidPrice")) s.setPlanInvalidPrice(m.value("planInvalidPrice").toDouble());

    if (m.contains("forecastOnline")) s.setForecastOnline(m.value("forecastOnline").toBool());
    if (m.contains("forecastIntervalSec")) s.setForecastIntervalSec(m.value("forecastIntervalSec").toInt());
    if (m.contains("llmProvider")) s.setLlmProvider(m.value("llmProvider").toString());
    if (m.contains("xaiApiKey")) s.setXaiApiKey(m.value("xaiApiKey").toString().trimmed());
    if (m.contains("xaiModel")) s.setXaiModel(m.value("xaiModel").toString().trimmed());

    if (m.contains("positionGrams")) s.setPositionGrams(m.value("positionGrams").toDouble());
    if (m.contains("positionCost")) s.setPositionCost(m.value("positionCost").toDouble());
    if (m.contains("dailyReportEnabled")) s.setDailyReportEnabled(m.value("dailyReportEnabled").toBool());
    if (m.contains("dailyReportTime")) s.setDailyReportTime(QTime::fromString(m.value("dailyReportTime").toString(), QStringLiteral("HH:mm")));
    if (m.contains("dcaDayOfMonth")) s.setDcaDayOfMonth(m.value("dcaDayOfMonth").toInt());
    if (m.contains("dcaNote")) s.setDcaNote(m.value("dcaNote").toString().trimmed());
    if (m.contains("eventAlertEnabled")) s.setEventAlertEnabled(m.value("eventAlertEnabled").toBool());

    if (m.contains("primaryPriceUrl")) s.setPrimaryPriceUrl(m.value("primaryPriceUrl").toString().trimmed());
    if (m.contains("chartUrl")) s.setChartUrl(m.value("chartUrl").toString().trimmed());
    if (m.contains("databaseDir")) s.setDatabaseDir(m.value("databaseDir").toString().trimmed());
    if (m.contains("proxyEnabled")) s.setProxyEnabled(m.value("proxyEnabled").toBool());
    if (m.contains("proxyHost")) s.setProxyHost(m.value("proxyHost").toString().trimmed());
    if (m.contains("proxyPort")) s.setProxyPort(m.value("proxyPort").toInt());

    s.save();
    s.applyNetworkProxy();

    if (oldDbDir != s.databaseDir() || !ExtremeDatabase::instance().isOpen())
        ExtremeDatabase::instance().open();

    m_sourceName = PriceService::canonicalSourceName(s.dataSource());
    if (m_priceService) {
        m_priceService->setInterval(s.refreshIntervalMs());
        m_priceService->forceRefresh();
    }
    if (m_chartWindow && m_chartWindow->isVisible()) {
        m_chartWindow->refreshData();
    }

    const int secMs = qMax(1000, s.refreshIntervalMs());
    if (m_secondaryTimer) {
        m_secondaryTimer->setInterval(secMs);
        if (s.showSecondaryPrice()) {
            if (!m_secondaryTimer->isActive())
                m_secondaryTimer->start();
            onSecondaryTimer();
        } else {
            m_secondaryTimer->stop();
            m_secondaryPriceText.clear();
            emit secondaryPriceChanged();
        }
    }
    if (m_price > 0.0) {
        updateAlertIndicator(m_price);
        evaluateSmartAlerts(m_price);
        evaluateDecision(m_price);
        updatePnL(m_price);
    }

    emit priceChanged();
    emit settingsChanged();
}

QVariantMap QmlBridge::get10DayAmplitude() {
    auto closes = ExtremeDatabase::instance().loadRecentDailyCloses(
        10, AppSettings::instance().dataSource());
    if (closes.size() < 3)
        closes = ExtremeDatabase::instance().loadRecentDailyCloses(10, QStringLiteral("gj"));
    if (closes.size() < 3) {
        QVariantMap res;
        res["text"] = tr("日线样本不足（需导入历史或运行累积）");
        res["min"] = 0.0;
        res["max"] = 0.0;
        res["avg"] = 0.0;
        res["amp"] = 0.0;
        return res;
    }
    double mn = closes.first().second, mx = mn;
    double sum = 0.0;
    for (const auto &c : closes) {
        mn = qMin(mn, c.second);
        mx = qMax(mx, c.second);
        sum += c.second;
    }
    const double avg = sum / closes.size();
    const double amp = mx - mn;
    const double pct = (avg > 0.0) ? (amp / avg * 100.0) : 0.0;
    const QString text = tr("近 %1 日 收盘均 %2 | 最低 %3 最高 %4 | 振幅 %5 (%6%)")
                             .arg(closes.size())
                             .arg(avg, 0, 'f', 2)
                             .arg(mn, 0, 'f', 2)
                             .arg(mx, 0, 'f', 2)
                             .arg(amp, 0, 'f', 2)
                             .arg(pct, 0, 'f', 2);
    QVariantMap res;
    res["text"] = text;
    res["min"] = mn;
    res["max"] = mx;
    res["avg"] = avg;
    res["amp"] = amp;
    res["count"] = closes.size();
    return res;
}

QString QmlBridge::getEventCalendarSummary() const {
    return EventCalendar::summaryNear();
}

QString QmlBridge::selectDatabaseDir() {
    const QString cur = AppSettings::instance().resolvedDatabaseDir();
    const QString dir = QFileDialog::getExistingDirectory(nullptr, tr("选择数据库目录"), cur);
    return dir;
}

void QmlBridge::openLogDir() {
    const QString dir = Logger::logDir();
    QDir().mkpath(dir);
    QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
}

void QmlBridge::checkUpdate() {
    auto *checker = new UpdateChecker(this);
    checker->check(nullptr, false);
}

void QmlBridge::fetchModelList(const QString &provider, const QString &apiKey) {
    if (!m_modelsNam) {
        m_modelsNam = new QNetworkAccessManager(this);
    }
    const QString key = apiKey.trimmed();
    if (key.isEmpty()) {
        emit modelsLoadFailed(tr("请先填写 API Key"));
        return;
    }
    if (m_modelsReply) {
        m_modelsReply->abort();
        m_modelsReply->deleteLater();
        m_modelsReply.clear();
    }
    QNetworkRequest req;
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setTransferTimeout(20000);
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("GoldPriceBarLite/1.3.10"));

    if (provider == QStringLiteral("gemini")) {
        const QUrl url(QStringLiteral("https://generativelanguage.googleapis.com/v1beta/models?key=%1&pageSize=100")
                           .arg(QString::fromUtf8(QUrl::toPercentEncoding(key))));
        req.setUrl(url);
    } else {
        req.setUrl(QUrl(QStringLiteral("https://api.x.ai/v1/models")));
        req.setRawHeader("Authorization", QByteArray("Bearer ") + key.toUtf8());
        req.setRawHeader("Accept", "application/json");
    }
    m_modelsReply = m_modelsNam->get(req);
    connect(m_modelsReply, &QNetworkReply::finished, this, [this, provider]() {
        QNetworkReply *reply = m_modelsReply.data();
        m_modelsReply.clear();
        if (!reply) return;
        const auto err = reply->error();
        const QString errStr = reply->errorString();
        const QByteArray raw = reply->readAll();
        reply->deleteLater();

        if (err != QNetworkReply::NoError) {
            emit modelsLoadFailed(tr("拉取模型失败：%1").arg(errStr));
            return;
        }
        QJsonParseError pe{};
        const QJsonDocument doc = QJsonDocument::fromJson(raw, &pe);
        if (pe.error != QJsonParseError::NoError) {
            emit modelsLoadFailed(tr("解析模型数据失败 (JSON 无效)"));
            return;
        }
        QStringList models;
        if (provider == QStringLiteral("gemini")) {
            const QJsonArray arr = doc.object().value(QStringLiteral("models")).toArray();
            for (const QJsonValue &v : arr) {
                const QJsonObject o = v.toObject();
                const QJsonArray methods = o.value(QStringLiteral("supportedGenerationMethods")).toArray();
                bool canGen = false;
                for (const QJsonValue &m : methods) {
                    if (m.toString() == QStringLiteral("generateContent")) { canGen = true; break; }
                }
                if (!canGen) continue;
                QString name = o.value(QStringLiteral("name")).toString();
                if (name.startsWith(QStringLiteral("models/"))) name = name.mid(7);
                if (!name.isEmpty()) models.append(name);
            }
        } else {
            QJsonArray arr = doc.object().value(QStringLiteral("data")).toArray();
            if (arr.isEmpty() && doc.isArray()) arr = doc.array();
            for (const QJsonValue &v : arr) {
                const QString id = v.toObject().value(QStringLiteral("id")).toString();
                if (!id.isEmpty()) models.append(id);
            }
        }
        models.removeDuplicates();
        models.sort();
        if (models.isEmpty()) {
            emit modelsLoadFailed(tr("未解析到可用模型列表"));
            return;
        }
        QVariantList vl;
        for (const QString &m : models) vl.append(m);
        emit modelsLoaded(vl);
    });
}

bool QmlBridge::showSecondaryPrice() const {
    return AppSettings::instance().showSecondaryPrice();
}

void QmlBridge::ensureSecondaryNam() {
    if (!m_secondaryNam) {
        m_secondaryNam = new QNetworkAccessManager(this);
    }
    if (m_secondaryNam->proxy().type() != QNetworkProxy::NoProxy) {
        m_secondaryNam->setProxy(QNetworkProxy(QNetworkProxy::NoProxy));
    }
}

void QmlBridge::onSecondaryTimer() {
    ensureSecondaryNam();

    if (!AppSettings::instance().showSecondaryPrice()) {
        if (!m_secondaryPriceText.isEmpty()) {
            m_secondaryPriceText.clear();
            emit secondaryPriceChanged();
        }
        return;
    }
    if (m_secondaryReply)
        return;
    if (!m_secondaryNam)
        return;

    const QString primary = AppSettings::instance().dataSource();
    const QString sec = (primary == QStringLiteral("gj") || primary == QStringLiteral("xau"))
                            ? QStringLiteral("zs")
                            : QStringLiteral("gj");
    const QUrl url(QStringLiteral("https://jin.20021002.xyz/api.php?type=%1").arg(sec));
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("GoldPriceBarLite/1.3.10"));
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
    req.setTransferTimeout(8000);
    req.setRawHeader("Accept", "*/*");
    Logger::info(QStringLiteral("Secondary price: requesting %1 (sec: %2)").arg(url.toString(), sec));
    QNetworkReply *reply = m_secondaryNam->get(req);
    m_secondaryReply = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply, sec]() { onSecondaryFinished(reply, sec); });
}

void QmlBridge::onSecondaryFinished(QNetworkReply *reply, const QString &sec) {
    if (m_secondaryReply.data() == reply)
        m_secondaryReply.clear();
    if (!reply)
        return;
    if (reply->error() != QNetworkReply::NoError) {
        Logger::warn(QStringLiteral("Secondary price fetch error: %1 (%2)").arg(reply->errorString(), reply->url().toString()));
        if (m_secondaryNam && m_secondaryNam->proxy().type() != QNetworkProxy::NoProxy) {
            m_secondaryNam->setProxy(QNetworkProxy(QNetworkProxy::NoProxy));
        }
        reply->deleteLater();
        // 如果是国际金且主源失败，自动请求备用源
        if (sec == QStringLiteral("gj") && !m_secondaryReply) {
            const QUrl backupUrl(AppSettings::instance().backupPriceUrl1());
            if (backupUrl.isValid()) {
                Logger::info(QStringLiteral("Secondary price fallback to backup: %1").arg(backupUrl.toString()));
                QNetworkRequest bReq(backupUrl);
                bReq.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("GoldPriceBarLite/1.3.10"));
                bReq.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
                bReq.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
                bReq.setTransferTimeout(8000);
                QNetworkReply *bReply = m_secondaryNam->get(bReq);
                m_secondaryReply = bReply;
                connect(bReply, &QNetworkReply::finished, this, [this, bReply]() {
                    onSecondaryFinished(bReply, QStringLiteral("backup"));
                });
            }
        }
        return;
    }
    const QByteArray raw = reply->readAll();
    reply->deleteLater();
    const QJsonDocument doc = QJsonDocument::fromJson(raw);
    if (!doc.isObject()) {
        Logger::warn(QStringLiteral("Secondary price JSON parse failed: %1").arg(QString::fromUtf8(raw.left(120))));
        return;
    }

    double price = 0.0;
    QString name;
    const QJsonObject rootObj = doc.object();
    if (rootObj.contains(QStringLiteral("data")) && rootObj.value(QStringLiteral("data")).isObject()) {
        const QJsonObject data = rootObj.value(QStringLiteral("data")).toObject();
        price = data.value(QStringLiteral("price")).toDouble();
        if (price <= 0.0) price = data.value(QStringLiteral("price")).toString().toDouble();
        name = data.value(QStringLiteral("name")).toString();
    } else if (rootObj.contains(QStringLiteral("price"))) {
        price = rootObj.value(QStringLiteral("price")).toDouble();
        if (price <= 0.0) price = rootObj.value(QStringLiteral("price")).toString().toDouble();
        name = rootObj.value(QStringLiteral("name")).toString();
    }

    if (price <= 0.0) {
        Logger::warn(QStringLiteral("Secondary price invalid or missing in data: %1").arg(QString::fromUtf8(raw.left(120))));
        return;
    }

    const QString shortName = (name.contains(QStringLiteral("伦敦")) || name.toLower().contains(QStringLiteral("xau")) || sec == QStringLiteral("gj") || sec == QStringLiteral("backup"))
                                  ? tr("伦")
                                  : (name.contains(QStringLiteral("浙商")) ? tr("浙") : tr("对照"));
    m_secondaryPrice = price;
    m_lastSecondaryPrice = price;
    m_secondaryPriceText = tr("%1 %2").arg(shortName).arg(price, 0, 'f', 2);
    Logger::info(QStringLiteral("Secondary price updated: %1 (price: %2)").arg(m_secondaryPriceText).arg(price, 0, 'f', 2));
    emit secondaryPriceChanged();

    if (m_price > 0.0) {
        ExtremeDatabase::instance().insertSecondaryQuote(QDateTime::currentDateTime(),
            AppSettings::instance().dataSource(), m_price, QStringLiteral("gj"), price);
        evaluatePremium(m_price);
    }
}

void QmlBridge::updateAlertIndicator(double price) {
    const auto &s = AppSettings::instance();
    const double hi = s.alertHigh();
    const double lo = s.alertLow();
    const bool planOn = s.planEnabled() && (s.planBuyPrice() > 0 || s.planSellPrice() > 0);
    const bool smartOn = s.smartAlertMa() || s.smartAlertPercentile();
    m_alertArmed = (hi > 0.0 || lo > 0.0 || planOn || smartOn);

    int kind = 0; // 0=None, 1=High, 2=Low
    if (!s.isInQuietHours()) {
        if (hi > 0.0 && price >= hi)
            kind = 1;
        else if (lo > 0.0 && price <= lo)
            kind = 2;
    }

    m_alertKind = kind;
    if (m_alertKind == 1) {
        m_alertTooltip = tr("【高价预警触发】\n现价 %1 ≥ 上限阈值 %2\n请注意防范冲高回落或锁定利润").arg(price, 0, 'f', 2).arg(hi, 0, 'f', 2);
    } else if (m_alertKind == 2) {
        m_alertTooltip = tr("【低价预警触发】\n现价 %1 ≤ 下限阈值 %2\n价格触及支撑防守区").arg(price, 0, 'f', 2).arg(lo, 0, 'f', 2);
    } else if (m_alertArmed) {
        m_alertTooltip = tr("【预警布防中·待触发】\n现价: %1\n上限预警: %2\n下限预警: %3\n智能均线/分位: %4")
                             .arg(price, 0, 'f', 2)
                             .arg(hi > 0 ? QString::number(hi, 'f', 2) : tr("未设"))
                             .arg(lo > 0 ? QString::number(lo, 'f', 2) : tr("未设"))
                             .arg(smartOn ? tr("已开启") : tr("未开启"));
    } else {
        m_alertTooltip = tr("未配置价格预警（点击齿轮打开设置可配置高低价预警）");
    }
    emit alertStateChanged();

    if (kind != 0) {
        maybeTrayNotify(kind, price);
    }
}

void QmlBridge::maybeTrayNotify(int kind, double price) {
    if (!m_tray || !AppSettings::instance().trayNotifyOnAlert())
        return;
    if (kind == 0)
        return;
    if (AppSettings::instance().isInQuietHours())
        return;

    const int cool = AppSettings::instance().alertCooldownSec();
    const QDateTime now = QDateTime::currentDateTime();
    QDateTime *last = (kind == 1) ? &m_lastHighNotify : &m_lastLowNotify;
    if (last->isValid() && last->secsTo(now) < cool)
        return;
    *last = now;

    const QString title = tr("金价预警");
    QString body;
    if (kind == 1) {
        body = tr("现价 %1 ≥ 高预警 %2").arg(price, 0, 'f', 2).arg(AppSettings::instance().alertHigh(), 0, 'f', 2);
    } else {
        body = tr("现价 %1 ≤ 低预警 %2").arg(price, 0, 'f', 2).arg(AppSettings::instance().alertLow(), 0, 'f', 2);
    }
    m_tray->showMessage(title, body, QSystemTrayIcon::Warning, 5000);
    if (AppSettings::instance().alertSound())
        QApplication::beep();
    ExtremeDatabase::instance().insertAlertEvent(QDateTime::currentDateTime(), AppSettings::instance().dataSource(),
        kind == 1 ? QStringLiteral("high") : QStringLiteral("low"), price,
        kind == 1 ? AppSettings::instance().alertHigh() : AppSettings::instance().alertLow(), body);
}

void QmlBridge::checkPlanAlerts(double price) {
    if (price <= 0.0 || !AppSettings::instance().planEnabled())
        return;
    if (AppSettings::instance().isInQuietHours())
        return;
    if (!m_tray || !AppSettings::instance().trayNotifyOnAlert())
        return;

    const auto &s = AppSettings::instance();
    QString msg;
    if (s.planInvalidPrice() > 0.0 && price <= s.planInvalidPrice())
        msg = tr("现价 %1 触及计划失效价 %2，计划作废").arg(price, 0, 'f', 2).arg(s.planInvalidPrice(), 0, 'f', 2);
    else if (s.planBuyPrice() > 0.0 && price <= s.planBuyPrice())
        msg = tr("现价 %1 进入买入观察区（≤ %2）").arg(price, 0, 'f', 2).arg(s.planBuyPrice(), 0, 'f', 2);
    else if (s.planSellPrice() > 0.0 && price >= s.planSellPrice())
        msg = tr("现价 %1 进入卖出观察区（≥ %2）").arg(price, 0, 'f', 2).arg(s.planSellPrice(), 0, 'f', 2);
    if (msg.isEmpty())
        return;

    const QDateTime now = QDateTime::currentDateTime();
    const int cool = qMax(60, AppSettings::instance().alertCooldownSec());
    if (m_lastPlanNotify.isValid() && m_lastPlanNotify.secsTo(now) < cool)
        return;
    m_lastPlanNotify = now;
    m_tray->showMessage(tr("交易计划"), msg, QSystemTrayIcon::Information, 6000);
    ExtremeDatabase::instance().insertAlertEvent(
        now, AppSettings::instance().dataSource(), QStringLiteral("plan"), price, 0.0, msg);
}

double QmlBridge::computeMa5() const {
    const QString src = AppSettings::instance().dataSource();
    auto closes = ExtremeDatabase::instance().loadRecentDailyCloses(10, src);
    if (closes.size() < 5)
        closes = ExtremeDatabase::instance().loadRecentDailyCloses(10, QStringLiteral("gj"));
    if (closes.size() < 5)
        return 0.0;
    double s = 0.0;
    for (int i = closes.size() - 5; i < closes.size(); ++i)
        s += closes.at(i).second;
    return s / 5.0;
}

double QmlBridge::computePercentile(double price) const {
    QString src = AppSettings::instance().dataSource();
    if (src == QStringLiteral("xau"))
        src = QStringLiteral("gj");
    auto closes = ExtremeDatabase::instance().loadRecentDailyCloses(20, src);
    if (closes.size() < 5)
        closes = ExtremeDatabase::instance().loadRecentDailyCloses(20, QStringLiteral("gj"));
    if (closes.isEmpty() || price <= 0.0)
        return 50.0;
    int below = 0;
    for (const auto &c : closes) {
        if (c.second < price)
            ++below;
    }
    return 100.0 * static_cast<double>(below) / static_cast<double>(closes.size());
}

void QmlBridge::evaluateSmartAlerts(double price) {
    if (price <= 0.0 || AppSettings::instance().isInQuietHours())
        return;

    QStringList reasons;
    if (AppSettings::instance().smartAlertMa()) {
        const double ma5 = computeMa5();
        if (ma5 > 0.0) {
            if (price < ma5 * 0.9985)
                reasons << tr("跌破MA5日(%1)").arg(ma5, 0, 'f', 2);
            else if (price > ma5 * 1.0015)
                reasons << tr("站上MA5日(%1)").arg(ma5, 0, 'f', 2);
        }
    }
    if (AppSettings::instance().smartAlertPercentile()) {
        const double pct = computePercentile(price);
        const int lo = AppSettings::instance().percentileLow();
        const int hi = AppSettings::instance().percentileHigh();
        if (pct <= lo)
            reasons << tr("近20日分位偏低(%1%)").arg(pct, 0, 'f', 0);
        else if (pct >= hi)
            reasons << tr("近20日分位偏高(%1%)").arg(pct, 0, 'f', 0);
    }
    if (reasons.isEmpty())
        return;

    const int cool = AppSettings::instance().alertCooldownSec();
    const QDateTime now = QDateTime::currentDateTime();
    if (m_lastSmartNotify.isValid() && m_lastSmartNotify.secsTo(now) < cool)
        return;
    m_lastSmartNotify = now;

    const bool bullish = reasons.join(QString()).contains(QStringLiteral("站上")) ||
                         reasons.join(QString()).contains(QStringLiteral("偏高"));
    if (m_alertKind == 0) {
        m_alertKind = bullish ? 1 : 2;
        m_alertTooltip = tr("【智能均线/分位预警】\n%1").arg(reasons.join(QStringLiteral("\n")));
        emit alertStateChanged();
    }
    if (m_tray && AppSettings::instance().trayNotifyOnAlert()) {
        const QString shortMsg = reasons.join(QStringLiteral(" · "));
        m_tray->showMessage(
            tr("智能预警"), shortMsg.left(80), bullish ? QSystemTrayIcon::Warning : QSystemTrayIcon::Information, 4000);
    }
    ExtremeDatabase::instance().insertAlertEvent(
        QDateTime::currentDateTime(), AppSettings::instance().dataSource(),
        bullish ? QStringLiteral("smart_high") : QStringLiteral("smart_low"), price, 0.0,
        reasons.join(QStringLiteral(";")));
    if (AppSettings::instance().alertSound())
        QApplication::beep();
}

void QmlBridge::evaluatePremium(double primaryPrice) {
    if (!AppSettings::instance().premiumAlertEnabled())
        return;
    if (!AppSettings::instance().showSecondaryPrice())
        return;
    if (primaryPrice <= 0.0 || m_lastSecondaryPrice <= 0.0)
        return;
    if (AppSettings::instance().isInQuietHours())
        return;

    const double ratio = primaryPrice / m_lastSecondaryPrice;
    m_premiumRatios.append(ratio);
    while (m_premiumRatios.size() > 40)
        m_premiumRatios.removeFirst();
    if (m_premiumRatios.size() < 8)
        return;

    double mean = 0.0;
    for (double r : m_premiumRatios)
        mean += r;
    mean /= m_premiumRatios.size();
    if (mean <= 0.0)
        return;
    const double devPct = qAbs(ratio - mean) / mean * 100.0;
    const double thr = AppSettings::instance().premiumThresholdPct();
    if (devPct < thr)
        return;

    const QDateTime now = QDateTime::currentDateTime();
    const int cool = AppSettings::instance().alertCooldownSec();
    if (m_lastPremiumNotify.isValid() && m_lastPremiumNotify.secsTo(now) < cool)
        return;
    m_lastPremiumNotify = now;

    if (m_tray && AppSettings::instance().trayNotifyOnAlert()) {
        m_tray->showMessage(tr("溢价监测"), tr("内外盘比值偏离 %1%（阈值 %2%）").arg(devPct, 0, 'f', 1).arg(thr, 0, 'f', 1),
            QSystemTrayIcon::Warning, 4000);
    }
}

void QmlBridge::onForecastUpdated(const ForecastResult &res) {
    if (!res.valid) return;
    m_predHighTimeWindow = res.predHighTimeWindow;
    m_predLowTimeWindow = res.predLowTimeWindow;
    m_predCatalyst = res.keyCatalyst;
    m_predDailyPath = res.scenario;
    m_peakProbText = res.peakWindowProb > 0
        ? tr("%1%").arg(res.peakWindowProb * 100.0, 0, 'f', 0)
        : tr("—");
    m_highInProbText = res.highAlreadyInProb > 0
        ? tr("%1%").arg(res.highAlreadyInProb * 100.0, 0, 'f', 0)
        : tr("—");
    m_remainingUpsideText = res.remainingUpside > 0
        ? QString::number(res.remainingUpside, 'f', 2)
        : tr("—");
    m_multiDayBiasText = res.multiDayBias.isEmpty() ? tr("—") : res.multiDayBias;
    m_forecastConfidenceText = res.confidence > 0
        ? tr("%1%").arg(res.confidence * 100.0, 0, 'f', 0)
        : tr("—");
    m_decisionTimeWindowText = tr("高点: %1 | 低点: %2").arg(
        m_predHighTimeWindow.isEmpty() ? tr("—") : m_predHighTimeWindow,
        m_predLowTimeWindow.isEmpty() ? tr("—") : m_predLowTimeWindow);
    if (m_price > 0.0) {
        evaluateDecision(m_price);
    }
}

void QmlBridge::evaluateDecision(double price) {
    if (price <= 0.0) return;

    const QString src = AppSettings::instance().dataSource();
    double actHigh = 0.0, actLow = 0.0;
    HistoryCache::instance().todayHigh(actHigh);
    HistoryCache::instance().todayLow(actLow);
    if (actHigh <= 0.0) actHigh = m_price;
    if (actLow <= 0.0) actLow = m_price;

    const auto ptsSamples = ExtremeDatabase::instance().loadIntradaySamples(QDate::currentDate(), src);
    std::vector<goldsdk::IntradayPoint> pts;
    pts.reserve(ptsSamples.size() + 1);
    for (const auto &p : ptsSamples) {
        goldsdk::IntradayPoint ip;
        ip.epochMs = p.first.toMSecsSinceEpoch();
        ip.price = p.second;
        pts.push_back(ip);
    }
    if (pts.empty() || pts.back().price != price) {
        goldsdk::IntradayPoint cur;
        cur.epochMs = QDateTime::currentMSecsSinceEpoch();
        cur.price = price;
        pts.push_back(cur);
    }

    const QTime nowT = QTime::currentTime();
    double dayFrac = 0.5;
    if (src == QStringLiteral("gj") || src == QStringLiteral("xau")) {
        dayFrac = nowT.msecsSinceStartOfDay() / (24.0 * 3600.0 * 1000.0);
    } else {
        const int startM = 9 * 60;
        const int endM = 23 * 60 + 30;
        const int nowM = nowT.hour() * 60 + nowT.minute();
        if (nowM <= startM) dayFrac = 0.05;
        else if (nowM >= endM) dayFrac = 0.95;
        else dayFrac = static_cast<double>(nowM - startM) / static_cast<double>(endM - startM);
    }

    double predHigh = 0.0, predLow = 0.0;
    if (ForecastService::instance().hasValidForecast()) {
        const auto res = ForecastService::instance().lastForecast();
        predHigh = res.predHigh;
        predLow = res.predLow;
        m_predHighTimeWindow = res.predHighTimeWindow;
        m_predLowTimeWindow = res.predLowTimeWindow;
        m_predCatalyst = res.keyCatalyst;
        m_predDailyPath = res.scenario;
    m_peakProbText = res.peakWindowProb > 0
        ? tr("%1%").arg(res.peakWindowProb * 100.0, 0, 'f', 0)
        : tr("—");
    m_highInProbText = res.highAlreadyInProb > 0
        ? tr("%1%").arg(res.highAlreadyInProb * 100.0, 0, 'f', 0)
        : tr("—");
    m_remainingUpsideText = res.remainingUpside > 0
        ? QString::number(res.remainingUpside, 'f', 2)
        : tr("—");
    m_multiDayBiasText = res.multiDayBias.isEmpty() ? tr("—") : res.multiDayBias;
    m_forecastConfidenceText = res.confidence > 0
        ? tr("%1%").arg(res.confidence * 100.0, 0, 'f', 0)
        : tr("—");
    } else {
        const double atr = ExtremeDatabase::instance().computeAtr(10, src);
        const double prevClose = ExtremeDatabase::instance().previousClose(src);
        const auto fr = goldsdk::ForecastEngine::dayRange(pts, actHigh, actLow, dayFrac, atr, prevClose);
        if (fr.valid && fr.predHigh > fr.predLow) {
            predHigh = fr.predHigh;
            predLow = fr.predLow;
            m_predHighTimeWindow = QString::fromStdString(fr.predHighTimeWindow);
            m_predLowTimeWindow = QString::fromStdString(fr.predLowTimeWindow);
            m_predCatalyst = QString::fromStdString(fr.keyCatalyst);
            m_predDailyPath = QString::fromStdString(fr.scenario);
            m_peakProbText = fr.peakWindowProb > 0
                ? tr("%1%").arg(fr.peakWindowProb * 100.0, 0, 'f', 0) : tr("—");
            m_highInProbText = fr.highAlreadyInProb > 0
                ? tr("%1%").arg(fr.highAlreadyInProb * 100.0, 0, 'f', 0) : tr("—");
            m_remainingUpsideText = fr.remainingUpside > 0
                ? QString::number(fr.remainingUpside, 'f', 2) : tr("—");
            m_forecastConfidenceText = fr.confidence > 0
                ? tr("%1%").arg(fr.confidence * 100.0, 0, 'f', 0) : tr("—");
        } else {
            predHigh = actHigh * 1.003;
            predLow = actLow * 0.997;
        }
    }

    m_decisionTimeWindowText = tr("高点: %1 | 低点: %2").arg(
        m_predHighTimeWindow.isEmpty() ? tr("—") : m_predHighTimeWindow,
        m_predLowTimeWindow.isEmpty() ? tr("—") : m_predLowTimeWindow);

    const double ma5 = computeMa5();
    const double pct20 = computePercentile(price);
    const auto &s = AppSettings::instance();

    int level = 1;
    QString tag = tr("🎯 震荡观望");
    QString color = QStringLiteral("#B0B0C0");
    QString bg = QStringLiteral("#22222E");
    QString advice = tr("现价在日内推演区间内运行。预计低点时段: %1，高点时段: %2。维持现有仓位观望。")
                         .arg(m_predLowTimeWindow.isEmpty() ? tr("待定") : m_predLowTimeWindow)
                         .arg(m_predHighTimeWindow.isEmpty() ? tr("待定") : m_predHighTimeWindow);

    // 检查计划优先
    if (s.planEnabled()) {
        if (s.planInvalidPrice() > 0.0 && price <= s.planInvalidPrice()) {
            level = 5;
            tag = tr("🛑 计划失效");
            color = QStringLiteral("#FF4D4F");
            bg = QStringLiteral("#3E181B");
            advice = tr("现价 %1 跌破计划防守失效价 %2，请按预定纪律执行止损！").arg(price, 0, 'f', 2).arg(s.planInvalidPrice(), 0, 'f', 2);
        } else if (s.planSellPrice() > 0.0 && price >= s.planSellPrice()) {
            level = 5;
            tag = tr("🎯 达目标卖出");
            color = QStringLiteral("#FF7875");
            bg = QStringLiteral("#3E1C1F");
            advice = tr("现价 %1 已达计划卖出目标价 %2，建议获利了结。").arg(price, 0, 'f', 2).arg(s.planSellPrice(), 0, 'f', 2);
        } else if (s.planBuyPrice() > 0.0 && price <= s.planBuyPrice()) {
            level = 3;
            tag = tr("🎯 达计划建仓");
            color = QStringLiteral("#52C41A");
            bg = QStringLiteral("#163219");
            advice = tr("现价 %1 进入计划买入观察区（≤ %2），适合分批建仓。").arg(price, 0, 'f', 2).arg(s.planBuyPrice(), 0, 'f', 2);
        }
    }

    // 预测高低区间决策
    if (level == 1) {
        if (predHigh > 0.0 && price >= predHigh * 0.9985) {
            level = 5;
            tag = tr("🎯 触及高点·分批卖出");
            color = QStringLiteral("#FF7875");
            bg = QStringLiteral("#3E1C1F");
            advice = tr("现价已触及当日推演高点阻力区（%1），盈亏比偏低，建议逢高止盈锁定收益。预计高点窗口: %2。")
                         .arg(predHigh, 0, 'f', 2)
                         .arg(m_predHighTimeWindow.isEmpty() ? tr("时段末") : m_predHighTimeWindow);
        } else if (predLow > 0.0 && price <= predLow * 1.0015) {
            level = 3;
            tag = tr("🎯 触及低点·分批买入");
            color = QStringLiteral("#52C41A");
            bg = QStringLiteral("#163219");
            advice = tr("现价已下探至当日推演低点支撑区（%1），风险收益比占优，适合分批买入或定投。预计低点窗口: %2。")
                         .arg(predLow, 0, 'f', 2)
                         .arg(m_predLowTimeWindow.isEmpty() ? tr("时段末") : m_predLowTimeWindow);
        } else if (pct20 >= 80 && ma5 > 0.0 && price > ma5) {
            level = 4;
            tag = tr("📈 偏多·谨防追高");
            color = QStringLiteral("#FAAD14");
            bg = QStringLiteral("#352814");
            advice = tr("价格站上MA5(%1)且处于近20日高分位(%2%)，短期多头强势，谨防追高回落。").arg(ma5, 0, 'f', 2).arg(pct20, 0, 'f', 0);
        } else if (pct20 <= 20 && ma5 > 0.0 && price < ma5) {
            level = 2;
            tag = tr("📉 超跌·关注低吸");
            color = QStringLiteral("#73D13D");
            bg = QStringLiteral("#1B301D");
            advice = tr("处于近20日极低分位(%1%)且运行于MA5(%2)下方，短线超跌，建议关注企稳低吸机会。").arg(pct20, 0, 'f', 0).arg(ma5, 0, 'f', 2);
        }
    }

    m_decisionLevel = level;
    m_decisionText = tag;
    m_decisionColor = color;
    m_decisionBg = bg;
    m_decisionAdvice = advice;
    m_decisionRangeText = tr("%1 ~ %2 (现价: %3)")
                              .arg(predLow > 0 ? QString::number(predLow, 'f', 2) : tr("--"))
                              .arg(predHigh > 0 ? QString::number(predHigh, 'f', 2) : tr("--"))
                              .arg(price > 0 ? QString::number(price, 'f', 2) : tr("--"));
    m_decisionMetricsText = tr("MA5日 %1 · 近20日分位 %2%")
                                .arg(ma5 > 0 ? QString::number(ma5, 'f', 2) : tr("--"))
                                .arg(pct20, 0, 'f', 0);
    m_decisionSessionText = TradingSession::statusText(src, QDateTime::currentDateTime());

    m_decisionTooltip = tr("【💡 实时趋势与交易决策参考】\n"
                           "• 建议：%1\n"
                           "• 当日预测区间：%2\n"
                           "• 预期时间窗口：%3\n"
                           "• 核心推演催化：%4\n"
                           "• 均线与分位：%5\n"
                           "• 时段状态：%6\n"
                           "（点击可直接打开分时走势图）")
                            .arg(m_decisionAdvice)
                            .arg(m_decisionRangeText)
                            .arg(m_decisionTimeWindowText)
                            .arg(m_predCatalyst.isEmpty() ? (m_predDailyPath.isEmpty() ? tr("待定") : m_predDailyPath) : m_predCatalyst)
                            .arg(m_decisionMetricsText)
                            .arg(m_decisionSessionText);
    emit decisionChanged();

    // 当出现重要决策级别 (触及高点/低点/计划价) 时推送托盘通知
    if (level == 3 || level == 5) {
        const QDateTime now = QDateTime::currentDateTime();
        if (!m_lastDecisionNotify.isValid() || m_lastDecisionNotify.secsTo(now) >= qMax(180, s.alertCooldownSec())) {
            m_lastDecisionNotify = now;
            if (m_tray && s.trayNotifyOnAlert() && !s.isInQuietHours()) {
                m_tray->showMessage(tr("交易决策提醒"), advice.left(70),
                                    level == 5 ? QSystemTrayIcon::Warning : QSystemTrayIcon::Information, 4500);
            }
        }
    }
}

void QmlBridge::updatePnL(double price) {
    const double grams = AppSettings::instance().positionGrams();
    const double cost = AppSettings::instance().positionCost();
    if (grams > 0.0 && cost > 0.0 && price > 0.0) {
        m_hasPosition = true;
        const double pnl = (price - cost) * grams;
        const double pct = (price - cost) / cost * 100.0;
        if (pnl >= 0.0) {
            m_pnlText = tr("盈 +%1 (+%2%)").arg(pnl, 0, 'f', 1).arg(pct, 0, 'f', 2);
            m_pnlColor = QStringLiteral("#FF4D4F");
        } else {
            m_pnlText = tr("亏 %1 (%2%)").arg(pnl, 0, 'f', 1).arg(pct, 0, 'f', 2);
            m_pnlColor = QStringLiteral("#52C41A");
        }
    } else {
        m_hasPosition = false;
        m_pnlText.clear();
    }
    emit pnlChanged();
}

void QmlBridge::checkDcaReminder() {
    const int day = AppSettings::instance().dcaDayOfMonth();
    if (day <= 0 || !m_tray)
        return;
    if (AppSettings::instance().isInQuietHours())
        return;

    const QDate today = QDate::currentDate();
    if (today.day() != day)
        return;

    const QString iso = today.toString(Qt::ISODate);
    if (AppSettings::instance().dcaLastNotifiedDate() == iso)
        return;

    const QString note = AppSettings::instance().dcaNote().trimmed();
    QString body = note.isEmpty() ? tr("今天是定投日（每月 %1 日），记得买入积存金。").arg(day)
                                  : tr("今天是定投日（每月 %1 日）\n%2").arg(day).arg(note);

    if (AppSettings::instance().dcaLastExecutedDate() == iso)
        body += tr("\n（今日已标记执行）");
    m_tray->showMessage(tr("定投提醒"), body, QSystemTrayIcon::Information, 8000);
    if (AppSettings::instance().alertSound())
        QApplication::beep();

    AppSettings::instance().setDcaLastNotifiedDate(iso);
    AppSettings::instance().save();
}

void QmlBridge::markDcaExecutedToday() {
    const QString today = QDate::currentDate().toString(Qt::ISODate);
    AppSettings::instance().setDcaLastExecutedDate(today);
    AppSettings::instance().save();
    if (m_tray) {
        m_tray->showMessage(tr("定投打卡"), tr("已记录今日定投已执行！"), QSystemTrayIcon::Information, 3000);
    }
}

QString QmlBridge::buildDailyReportText() const {
    double high = 0.0, low = 0.0;
    HistoryCache::instance().todayHigh(high);
    HistoryCache::instance().todayLow(low);
    const double price = m_price;
    QString lines;
    lines += tr("【GoldPriceBar 今日行情摘要】\n");
    lines += tr("品种：%1\n").arg(m_sourceName);
    lines += tr("现价：%1\n").arg(price > 0 ? QString::number(price, 'f', 2) : QStringLiteral("--"));
    lines += tr("今高：%1  今低：%2\n")
                 .arg(high > 0 ? QString::number(high, 'f', 2) : QStringLiteral("--"))
                 .arg(low > 0 ? QString::number(low, 'f', 2) : QStringLiteral("--"));
    if (high > 0 && low > 0)
        lines += tr("振幅：%1 (%2%)\n")
                     .arg(high - low, 0, 'f', 2)
                     .arg(low > 0 ? (high - low) / low * 100.0 : 0.0, 0, 'f', 2);
    const double grams = AppSettings::instance().positionGrams();
    const double cost = AppSettings::instance().positionCost();
    if (grams > 0 && cost > 0 && price > 0) {
        const double pnl = (price - cost) * grams;
        lines += tr("持仓浮盈亏：%1 元（%2 克）\n").arg(pnl, 0, 'f', 1).arg(grams, 0, 'f', 3);
    }
    if (m_secondaryPrice > 0)
        lines += tr("对照价：%1\n").arg(m_secondaryPrice, 0, 'f', 2);
    lines += tr("时间：%1").arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm")));
    return lines;
}

void QmlBridge::showDailyReport(bool force) {
    const QString text = buildDailyReportText();
    if (force) {
        QMessageBox::information(nullptr, tr("今日摘要"), text);
        return;
    }
    if (m_tray) {
        double high = 0, low = 0;
        HistoryCache::instance().todayHigh(high);
        HistoryCache::instance().todayLow(low);
        const QString brief = tr("现 %1  高 %2  低 %3")
                                  .arg(m_price > 0 ? QString::number(m_price, 'f', 2) : QStringLiteral("--"))
                                  .arg(high > 0 ? QString::number(high, 'f', 2) : QStringLiteral("--"))
                                  .arg(low > 0 ? QString::number(low, 'f', 2) : QStringLiteral("--"));
        m_tray->showMessage(tr("今日收盘摘要"), brief, QSystemTrayIcon::Information, 5000);
    }
}

void QmlBridge::showDailyReportDialog() {
    showDailyReport(true);
}

void QmlBridge::checkDailyReport(bool force) {
    if (force) {
        showDailyReport(true);
        return;
    }
    if (!AppSettings::instance().dailyReportEnabled())
        return;
    if (AppSettings::instance().isInQuietHours())
        return;
    const QTime target = AppSettings::instance().dailyReportTime();
    const QTime now = QTime::currentTime();
    if (now.hour() != target.hour() || now.minute() != target.minute())
        return;
    const QString today = QDate::currentDate().toString(Qt::ISODate);
    if (AppSettings::instance().dailyReportLastDate() == today)
        return;
    AppSettings::instance().setDailyReportLastDate(today);
    AppSettings::instance().save();
    showDailyReport(false);
}

void QmlBridge::checkEventAlerts() {
    if (!AppSettings::instance().eventAlertEnabled())
        return;
    if (AppSettings::instance().isInQuietHours())
        return;
    const QString text = EventCalendar::pendingAlertText();
    if (text.isEmpty())
        return;
    const QString key = QDate::currentDate().toString(Qt::ISODate) + QLatin1Char('|') + text;
    if (AppSettings::instance().eventAlertLastKey() == key)
        return;
    AppSettings::instance().setEventAlertLastKey(key);
    AppSettings::instance().save();
    if (m_tray) {
        m_tray->showMessage(tr("宏观日程"), text.left(60), QSystemTrayIcon::Warning, 4500);
    }
}
