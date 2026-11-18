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
#include "AgentService.h"
#include "AgentAiService.h"

#include <QDesktopServices>
#include <QClipboard>
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
#include <QElapsedTimer>
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

    // 初始化自动化 Agent 引擎
    AgentService::instance().init();
    connect(&AgentService::instance(), &AgentService::agentTriggered, this, [this](const QVariantMap &notification) {
        emit agentNotificationTriggered(notification);
        openAgentCardWindow();
        if (m_tray && m_tray->isVisible()) {
            m_tray->showMessage(notification.value("title").toString(),
                                notification.value("summary").toString(),
                                QSystemTrayIcon::Information, 6000);
        }
    });
    connect(&AgentService::instance(), &AgentService::tasksUpdated, this, &QmlBridge::agentTasksUpdated);

    // AI 知识问答与延伸服务信号中继（确保 QML 聊天窗口能实时接收问答进度与模型响应）
    connect(&AgentAiService::instance(), &AgentAiService::answerStarted, this, &QmlBridge::aiAnswerStarted);
    connect(&AgentAiService::instance(), &AgentAiService::answerFinished, this, &QmlBridge::aiAnswerReceived);
    connect(&AgentAiService::instance(), &AgentAiService::answerFailed, this, &QmlBridge::aiAnswerFailed);
}

QmlBridge::~QmlBridge() {
    if (m_checkTimer) {
        m_checkTimer->stop();
    }
    if (m_secondaryReply) {
        QNetworkReply *old = m_secondaryReply.data();
        m_secondaryReply.clear();
        if (old) {
            QObject::disconnect(old, nullptr, nullptr, nullptr);
            old->abort();
            old->deleteLater();
        }
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

    auto *actSimTrade = menu->addAction(tr("📊 模拟交易明细与记录"));
    connect(actSimTrade, &QAction::triggered, this, &QmlBridge::openSimulatedTradesWindow);
        auto *actDaily = menu->addAction(tr("今日收盘摘要"));
    connect(actDaily, &QAction::triggered, this, &QmlBridge::showDailyReportDialog);

    auto *actDca = menu->addAction(tr("今日已定投（打卡）"));
    connect(actDca, &QAction::triggered, this, &QmlBridge::markDcaExecutedToday);

    auto *actAgent = menu->addAction(tr("🤖 自动化 Agent 任务中心"));
    connect(actAgent, &QAction::triggered, this, &QmlBridge::openAgentWindow);

    auto *actAiChat = menu->addAction(tr("🤖 AI 知识问答与延伸"));
    connect(actAiChat, &QAction::triggered, this, [this]() {
        openAgentChatWindow();
    });

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

    // 保持伦敦金（对照价）与积存金刷新时间严格同步
    if (AppSettings::instance().showSecondaryPrice() || (m_chartWindow && m_chartWindow->isVisible())) {
        onSecondaryTimer();
    }
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

void QmlBridge::closeSentimentWindow() {
    Logger::info(QStringLiteral("QmlBridge::closeSentimentWindow called"));
    if (m_sentimentWindow) {
        m_sentimentWindow->setVisible(false);
    }
    emit requestCloseSentiment();
}

void QmlBridge::toggleSentimentWindow() {
    Logger::info(QStringLiteral("QmlBridge::toggleSentimentWindow called"));
    if (m_sentimentWindow && m_sentimentWindow->isVisible()) {
        closeSentimentWindow();
        return;
    }
    openSentimentWindow();
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

void QmlBridge::closeSettingsWindow() {
    Logger::info(QStringLiteral("QmlBridge::closeSettingsWindow called"));
    if (m_settingsWindow) {
        m_settingsWindow->setVisible(false);
    }
    emit requestCloseSettings();
}

void QmlBridge::toggleSettingsWindow() {
    Logger::info(QStringLiteral("QmlBridge::toggleSettingsWindow called"));
    if (m_settingsWindow && m_settingsWindow->isVisible()) {
        closeSettingsWindow();
        return;
    }
    openSettingsWindow();
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
    if (m_secondaryTimer && !m_secondaryTimer->isActive()) {
        m_secondaryTimer->start();
        onSecondaryTimer();
    }
    if (m_secondaryPrice > 0.0) {
        m_chartWindow->onSecondaryPriceUpdated(m_secondaryPrice);
    }
    m_chartWindow->refreshData();
    if (m_priceService && m_priceService->hasValidPrice()) {
        m_chartWindow->onNewPrice(m_priceService->lastPrice(), m_priceService->lastChange(), m_priceService->lastSourceName());
    }
}

void QmlBridge::closeChartWindow() {
    Logger::info(QStringLiteral("QmlBridge::closeChartWindow called"));
    if (m_chartWindow) {
        m_chartWindow->hide();
    }
}

void QmlBridge::toggleChartWindow() {
    Logger::info(QStringLiteral("QmlBridge::toggleChartWindow called"));
    if (m_chartWindow && m_chartWindow->isVisible()) {
        closeChartWindow();
        return;
    }
    openChartWindow();
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
    m["autoSimulateTrading"] = s.autoSimulateTrading();
    m["simulateTradeBatchGrams"] = s.simulateTradeBatchGrams();
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
    if (m.contains("autoSimulateTrading")) s.setAutoSimulateTrading(m.value("autoSimulateTrading").toBool());
    if (m.contains("simulateTradeBatchGrams")) s.setSimulateTradeBatchGrams(m.value("simulateTradeBatchGrams").toDouble());
    if (m.contains("positionGrams") || m.contains("positionCost")) {
        updatePnL(m_price);
        emit positionChanged();
    }
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
    if (closes.size() < 3) {
        auto gjCloses = ExtremeDatabase::instance().loadRecentDailyCloses(10, QStringLiteral("gj"));
        if (!gjCloses.isEmpty() && m_price > 0.0) {
            const double lastGj = gjCloses.last().second;
            const double ratio = (lastGj > 0.0) ? (m_price / lastGj) : 1.0;
            for (auto &c : gjCloses)
                c.second *= ratio;
            closes = gjCloses;
        }
    }
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
        QNetworkReply *old = m_modelsReply.data();
        m_modelsReply.clear();
        if (old) {
            QObject::disconnect(old, nullptr, nullptr, nullptr);
            old->abort();
            old->deleteLater();
        }
    }
    QNetworkRequest req;
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setTransferTimeout(20000);
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("GoldPriceBarLite/1.3.10"));

    const QString provLow = provider.trimmed().toLower();
    if (provLow == QStringLiteral("gemini")) {
        const QUrl url(QStringLiteral("https://generativelanguage.googleapis.com/v1beta/models?key=%1&pageSize=100")
                           .arg(QString::fromUtf8(QUrl::toPercentEncoding(key))));
        req.setUrl(url);
    } else if (provLow == QStringLiteral("deepseek")) {
        req.setUrl(QUrl(QStringLiteral("https://api.deepseek.com/models")));
        req.setRawHeader("Authorization", QByteArray("Bearer ") + key.toUtf8());
        req.setRawHeader("Accept", "application/json");
    } else if (provLow == QStringLiteral("glm") || provLow == QStringLiteral("zhipu")) {
        req.setUrl(QUrl(QStringLiteral("https://open.bigmodel.cn/api/paas/v4/models")));
        req.setRawHeader("Authorization", QByteArray("Bearer ") + key.toUtf8());
        req.setRawHeader("Accept", "application/json");
    } else if (provLow == QStringLiteral("qwen")) {
        req.setUrl(QUrl(QStringLiteral("https://dashscope.aliyuncs.com/compatible-mode/v1/models")));
        req.setRawHeader("Authorization", QByteArray("Bearer ") + key.toUtf8());
        req.setRawHeader("Accept", "application/json");
    } else if (provLow == QStringLiteral("moonshot")) {
        req.setUrl(QUrl(QStringLiteral("https://api.moonshot.cn/v1/models")));
        req.setRawHeader("Authorization", QByteArray("Bearer ") + key.toUtf8());
        req.setRawHeader("Accept", "application/json");
    } else if (provLow == QStringLiteral("openai")) {
        req.setUrl(QUrl(QStringLiteral("https://api.openai.com/v1/models")));
        req.setRawHeader("Authorization", QByteArray("Bearer ") + key.toUtf8());
        req.setRawHeader("Accept", "application/json");
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

        auto getFallbackModels = [](const QString &p) -> QStringList {
            QString pl = p.trimmed().toLower();
            if (pl == QStringLiteral("zhipu")) pl = QStringLiteral("glm");
            if (pl == QStringLiteral("deepseek")) return {QStringLiteral("deepseek-chat"), QStringLiteral("deepseek-reasoner")};
            if (pl == QStringLiteral("glm")) return {QStringLiteral("glm-4-plus"), QStringLiteral("glm-4-flash"), QStringLiteral("glm-4-long"), QStringLiteral("glm-4v-plus"), QStringLiteral("glm-3-turbo")};
            if (pl == QStringLiteral("qwen")) return {QStringLiteral("qwen-max"), QStringLiteral("qwen-plus"), QStringLiteral("qwen-turbo"), QStringLiteral("qwen-long")};
            if (pl == QStringLiteral("moonshot")) return {QStringLiteral("moonshot-v1-8k"), QStringLiteral("moonshot-v1-32k"), QStringLiteral("moonshot-v1-128k")};
            if (pl == QStringLiteral("gemini")) return {QStringLiteral("gemini-flash-latest"), QStringLiteral("gemini-2.5-flash-lite"), QStringLiteral("gemini-2.0-flash"), QStringLiteral("gemini-pro-latest"), QStringLiteral("gemini-1.5-flash")};
            if (pl == QStringLiteral("openai")) return {QStringLiteral("gpt-4o"), QStringLiteral("gpt-4o-mini"), QStringLiteral("o1"), QStringLiteral("o1-mini"), QStringLiteral("gpt-4-turbo")};
            if (pl == QStringLiteral("xai")) return {QStringLiteral("grok-2-latest"), QStringLiteral("grok-2"), QStringLiteral("grok-beta"), QStringLiteral("grok-vision-beta")};
            return {QStringLiteral("deepseek-chat"), QStringLiteral("glm-4-flash"), QStringLiteral("gpt-4o-mini")};
        };

        if (err != QNetworkReply::NoError) {
            QStringList fallbacks = getFallbackModels(provider);
            QVariantList vl;
            for (const QString &m : fallbacks) vl.append(m);
            emit modelsLoaded(vl);

            QString friendlyErr;
            if (err == QNetworkReply::ProxyConnectionRefusedError || errStr.contains(QStringLiteral("Proxy"), Qt::CaseInsensitive)) {
                friendlyErr = tr("无法连接本地代理 (%1:%2)！请检查代理客户端是否已开启。已载入内置备选模型。").arg(AppSettings::instance().proxyHost(), QString::number(AppSettings::instance().proxyPort()));
            } else if (errStr.contains(QStringLiteral("401"))) {
                friendlyErr = tr("API Key 鉴权失败 (HTTP 401)！请核对服务商与 Key 是否匹配。已载入内置备选模型。");
            } else {
                friendlyErr = tr("在线拉取失败：%1（已载入 %2 官方预设模型，可点击「测试连通性」排查）").arg(errStr.isEmpty() ? tr("网络异常") : errStr, provider.toUpper());
            }
            emit modelsLoadFailed(friendlyErr);
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
            QStringList fallbacks = getFallbackModels(provider);
            QVariantList vl;
            for (const QString &m : fallbacks) vl.append(m);
            emit modelsLoaded(vl);
            emit modelsLoadFailed(tr("接口未返回可用模型列表，已载入 %1 官方推荐模型").arg(provider.toUpper()));
            return;
        }
        QVariantList vl;
        for (const QString &m : models) vl.append(m);
        emit modelsLoaded(vl);
    });
}

void QmlBridge::testAiConnection(const QString &provider, const QString &apiKey, const QString &model, const QString &customEndpoint) {
    if (!m_modelsNam) {
        m_modelsNam = new QNetworkAccessManager(this);
    }
    const QString key = apiKey.trimmed();
    if (key.isEmpty()) {
        emit aiConnectionTested(false, tr("请先填写 API Key"), 0);
        return;
    }
    const QString provLow = provider.trimmed().toLower();
    QString mdl = model.trimmed();
    if (mdl.isEmpty()) {
        if (provLow == QStringLiteral("gemini")) mdl = QStringLiteral("gemini-flash-latest");
        else if (provLow == QStringLiteral("deepseek")) mdl = QStringLiteral("deepseek-chat");
        else if (provLow == QStringLiteral("glm") || provLow == QStringLiteral("zhipu")) mdl = QStringLiteral("glm-4-flash");
        else if (provLow == QStringLiteral("qwen")) mdl = QStringLiteral("qwen-plus");
        else if (provLow == QStringLiteral("moonshot")) mdl = QStringLiteral("moonshot-v1-8k");
        else if (provLow == QStringLiteral("openai")) mdl = QStringLiteral("gpt-4o-mini");
        else mdl = QStringLiteral("grok-2-latest");
    }

    QNetworkRequest req;
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setTransferTimeout(15000); // 15秒测试超时
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("GoldPriceBarLite/1.4.0 (AI Connection Tester)"));

    QByteArray postData;
    if (provLow == QStringLiteral("gemini")) {
        QString m = mdl;
        if (m.startsWith(QStringLiteral("models/"))) m = m.mid(7);
        const QUrl url(QStringLiteral("https://generativelanguage.googleapis.com/v1beta/models/%1:generateContent?key=%2")
                           .arg(m, QString::fromUtf8(QUrl::toPercentEncoding(key))));
        req.setUrl(url);
        req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
        QJsonObject body;
        QJsonArray contents;
        QJsonObject userMsg;
        userMsg.insert(QStringLiteral("role"), QStringLiteral("user"));
        QJsonArray parts;
        parts.append(QJsonObject{{QStringLiteral("text"), QStringLiteral("ping")}});
        userMsg.insert(QStringLiteral("parts"), parts);
        contents.append(userMsg);
        body.insert(QStringLiteral("contents"), contents);
        QJsonObject genCfg;
        genCfg.insert(QStringLiteral("maxOutputTokens"), 5);
        body.insert(QStringLiteral("generationConfig"), genCfg);
        postData = QJsonDocument(body).toJson(QJsonDocument::Compact);
    } else {
        QUrl url;
        if (!customEndpoint.trimmed().isEmpty()) {
            url = QUrl(customEndpoint.trimmed());
        } else if (provLow == QStringLiteral("deepseek")) {
            url = QUrl(QStringLiteral("https://api.deepseek.com/chat/completions"));
        } else if (provLow == QStringLiteral("glm") || provLow == QStringLiteral("zhipu")) {
            url = QUrl(QStringLiteral("https://open.bigmodel.cn/api/paas/v4/chat/completions"));
        } else if (provLow == QStringLiteral("qwen")) {
            url = QUrl(QStringLiteral("https://dashscope.aliyuncs.com/compatible-mode/v1/chat/completions"));
        } else if (provLow == QStringLiteral("moonshot")) {
            url = QUrl(QStringLiteral("https://api.moonshot.cn/v1/chat/completions"));
        } else if (provLow == QStringLiteral("openai")) {
            url = QUrl(QStringLiteral("https://api.openai.com/v1/chat/completions"));
        } else {
            url = QUrl(QStringLiteral("https://api.x.ai/v1/chat/completions"));
        }
        req.setUrl(url);
        req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
        req.setRawHeader("Authorization", QByteArray("Bearer ") + key.toUtf8());
        QJsonObject body;
        body.insert(QStringLiteral("model"), mdl);
        body.insert(QStringLiteral("max_tokens"), 5);
        QJsonArray messages;
        messages.append(QJsonObject{{QStringLiteral("role"), QStringLiteral("user")}, {QStringLiteral("content"), QStringLiteral("ping")}});
        body.insert(QStringLiteral("messages"), messages);
        postData = QJsonDocument(body).toJson(QJsonDocument::Compact);
    }

    auto timer = std::make_shared<QElapsedTimer>();
    timer->start();

    QNetworkReply *reply = m_modelsNam->post(req, postData);
    connect(reply, &QNetworkReply::finished, this, [this, reply, timer, provider, mdl]() {
        const qint64 latency = timer->elapsed();
        const auto err = reply->error();
        const QString errStr = reply->errorString();
        const int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray resp = reply->readAll();
        reply->deleteLater();

        if (err == QNetworkReply::NoError && statusCode >= 200 && statusCode < 300) {
            const QString msg = tr("连接成功！模型 [%1] 响应正常，延迟: %2 ms").arg(mdl, QString::number(latency));
            Logger::info(QStringLiteral("testAiConnection success: %1 (model=%2 latency=%3ms)").arg(provider, mdl, QString::number(latency)));
            emit aiConnectionTested(true, msg, static_cast<int>(latency));
            return;
        }

        QString apiErrMsg;
        if (!resp.isEmpty()) {
            const QJsonDocument doc = QJsonDocument::fromJson(resp);
            if (doc.isObject()) {
                if (doc.object().contains(QStringLiteral("error"))) {
                    const QJsonValue ev = doc.object().value(QStringLiteral("error"));
                    if (ev.isObject()) {
                        apiErrMsg = ev.toObject().value(QStringLiteral("message")).toString();
                    } else if (ev.isString()) {
                        apiErrMsg = ev.toString();
                    }
                }
            }
        }

        QString friendlyMsg;
        if (err == QNetworkReply::ProxyConnectionRefusedError || errStr.contains(QStringLiteral("Proxy"), Qt::CaseInsensitive)) {
            friendlyMsg = tr("无法连接本地代理 (%1:%2)！请检查 Clash/V2Ray 是否开启，或在「网络代理」设置中关闭代理或修改端口。")
                              .arg(AppSettings::instance().proxyHost(), QString::number(AppSettings::instance().proxyPort()));
        } else if (statusCode == 401) {
            friendlyMsg = tr("鉴权失败 (HTTP 401)！API Key 无效或未激活，请核对 Key 与服务商是否匹配。");
        } else if (statusCode == 404) {
            friendlyMsg = tr("模型不存在或已下线 (HTTP 404)！该模型 [%1] 无法调用，请在下拉菜单选择其他模型。").arg(mdl);
        } else if (statusCode == 429) {
            friendlyMsg = tr("调用超限或额度耗尽 (HTTP 429)！免费配额已用尽或请求过于频繁，请稍后再试或充值。");
        } else if (err == QNetworkReply::TimeoutError) {
            friendlyMsg = tr("连接超时 (15s)！若使用海外模型 (如 Gemini/OpenAI)，需在高级设置中配置有效网络代理。");
        } else if (err == QNetworkReply::HostNotFoundError) {
            friendlyMsg = tr("无法解析服务器域名！请检查网络连接及 DNS 配置。");
        } else if (!apiErrMsg.isEmpty()) {
            friendlyMsg = tr("服务商报错 (HTTP %1): %2").arg(QString::number(statusCode), apiErrMsg);
        } else {
            friendlyMsg = tr("连接失败: %1 (HTTP %2)").arg(errStr, QString::number(statusCode));
        }

        Logger::warn(QStringLiteral("testAiConnection failed: %1 (model=%2 status=%3 err=%4 apiMsg=%5)")
                         .arg(provider, mdl, QString::number(statusCode), errStr, apiErrMsg));
        emit aiConnectionTested(false, friendlyMsg, static_cast<int>(latency));
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

    const bool chartActive = (m_chartWindow && m_chartWindow->isVisible());
    if (!AppSettings::instance().showSecondaryPrice() && !chartActive) {
        if (!m_secondaryPriceText.isEmpty()) {
            m_secondaryPriceText.clear();
            emit secondaryPriceChanged();
        }
        return;
    }
    if (!m_secondaryNam)
        return;

    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    if (m_secondaryReply) {
        if (m_secondaryRequestStartMs > 0 && (nowMs - m_secondaryRequestStartMs < 5000) && m_secondaryReply->isRunning()) {
            return;
        }
        // 超过 5 秒未返回的卡死请求，安全断开后终止并释放，避免信号重入
        QNetworkReply *old = m_secondaryReply.data();
        m_secondaryReply.clear();
        m_secondaryRequestStartMs = 0;
        if (old) {
            QObject::disconnect(old, nullptr, this, nullptr);
            old->abort();
            old->deleteLater();
        }
    }

    const QString primary = AppSettings::instance().dataSource();
    const QString sec = (primary == QStringLiteral("gj") || primary == QStringLiteral("xau"))
                            ? QStringLiteral("zs")
                            : QStringLiteral("gj");
    const QUrl url(QStringLiteral("https://jin.20021002.xyz/api.php?type=%1&_t=%2").arg(sec).arg(nowMs));
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("GoldPriceBarLite/1.3.10"));
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
    req.setTransferTimeout(3500);
    req.setRawHeader("Accept", "*/*");

    QNetworkReply *reply = m_secondaryNam->get(req);
    m_secondaryReply = reply;
    m_secondaryRequestStartMs = nowMs;
    connect(reply, &QNetworkReply::finished, this, [this, reply, sec]() { onSecondaryFinished(reply, sec); });
}

void QmlBridge::onSecondaryFinished(QNetworkReply *reply, const QString &sec) {
    if (m_secondaryReply.data() == reply) {
        m_secondaryReply.clear();
        m_secondaryRequestStartMs = 0;
    }
    if (!reply)
        return;
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
        // Logger::warn(QStringLiteral("Secondary price fetch error: %1 (%2)").arg(reply->errorString(), reply->url().toString()));
        if (m_secondaryNam && m_secondaryNam->proxy().type() != QNetworkProxy::NoProxy) {
            m_secondaryNam->setProxy(QNetworkProxy(QNetworkProxy::NoProxy));
        }
        // 如果是国际金且主源失败，自动请求备用源
        if (sec == QStringLiteral("gj") && !m_secondaryReply) {
            const QUrl backupUrl(AppSettings::instance().backupPriceUrl1());
            if (backupUrl.isValid()) {
                // Logger::info(QStringLiteral("Secondary price fallback to backup: %1").arg(backupUrl.toString()));
                QNetworkRequest bReq(backupUrl);
                bReq.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("GoldPriceBarLite/1.3.10"));
                bReq.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
                bReq.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
                bReq.setTransferTimeout(8000);
                QNetworkReply *bReply = m_secondaryNam->get(bReq);
                m_secondaryReply = bReply;
                m_secondaryRequestStartMs = QDateTime::currentMSecsSinceEpoch();
                connect(bReply, &QNetworkReply::finished, this, [this, bReply]() {
                    onSecondaryFinished(bReply, QStringLiteral("backup"));
                });
            }
        }
        return;
    }
    const QByteArray raw = reply->readAll();
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
    // Logger::info(QStringLiteral("Secondary price updated: %1 (price: %2)").arg(m_secondaryPriceText).arg(price, 0, 'f', 2));
    emit secondaryPriceChanged();
    if (m_chartWindow) {
        m_chartWindow->onSecondaryPriceUpdated(price);
    }

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
    if (closes.size() < 5) {
        auto gjCloses = ExtremeDatabase::instance().loadRecentDailyCloses(10, QStringLiteral("gj"));
        if (!gjCloses.isEmpty() && m_price > 0.0) {
            const double lastGj = gjCloses.last().second;
            const double ratio = (lastGj > 0.0) ? (m_price / lastGj) : 1.0;
            for (auto &c : gjCloses)
                c.second *= ratio;
            closes = gjCloses;
        }
    }
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
    if (closes.size() < 5) {
        auto gjCloses = ExtremeDatabase::instance().loadRecentDailyCloses(20, QStringLiteral("gj"));
        if (!gjCloses.isEmpty() && price > 0.0) {
            const double lastGj = gjCloses.last().second;
            const double ratio = (lastGj > 0.0) ? (price / lastGj) : 1.0;
            for (auto &c : gjCloses)
                c.second *= ratio;
            closes = gjCloses;
        }
    }
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
        auto& db = ExtremeDatabase::instance();
        goldsdk::ForecastContext fctx;
        fctx.actHigh = actHigh;
        fctx.actLow = actLow;
        fctx.dayFraction = dayFrac;
        fctx.atr = db.computeAtr(10, src);
        fctx.prevClose = db.previousClose(src);
        fctx.dailyBars = db.loadRecentDailyBars(45, src);
        if (fctx.dailyBars.size() >= 10) {
            fctx.multiDayTrend = goldsdk::ForecastEngine::multiDayTrendFromBars(fctx.dailyBars);
        }
        fctx.historicalPeakBucketCounts = db.loadHistoricalPeakBucketCounts(60, src);
        fctx.historicalBucketVarWeights = db.loadHistoricalBucketVarWeights(20, src);
        fctx.crossMarketLeadSignal = db.computeCrossMarketLeadSignal(src);
        fctx.sentimentScore = SentimentService::instance().recentMarketImpact(6).decayedScore;
        fctx.conformalScale = db.computeAdaptiveConformalScale(src, 30);
        const auto fr = goldsdk::ForecastEngine::dayRangeWithContext(pts, fctx);
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
            if (fctx.multiDayTrend.valid) {
                m_multiDayBiasText = QString::fromUtf8(fctx.multiDayTrend.label());
            }
            m_forecastConfidenceText = fr.confidence > 0
                ? tr("%1%").arg(fr.confidence * 100.0, 0, 'f', 0) : tr("—");
        } else {
            const double basePrice = price > 0.0 ? price : (actHigh > 0.0 ? actHigh : 750.0);
            predHigh = qMax(actHigh, basePrice * 1.005);
            predLow = (actLow > 0.0) ? qMin(actLow, basePrice * 0.995) : basePrice * 0.995;
        }
    }

    m_predHigh = predHigh;
    m_predLow = predLow;

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

    // 多因子共振决策评估（趋势 + 时段突破 + TWAP均值回归 + 形态 + 0.4%费率安全边际）
    goldsdk::ConfluenceDecision confluence;
    // 预测高低区间与经典形态决策融合
    if (EventCalendar::isEventBlackoutWindow()) {
        level = 4;
        tag = tr("⏳ 数据避险·防双向插针");
        color = QStringLiteral("#FAAD14");
        bg = QStringLiteral("#352814");
        const QString w = EventCalendar::currentEventWarning();
        advice = !w.isEmpty() ? w : tr("当前正处于重磅宏观数据公布窗口（非农/CPI/FOMC等），价差急剧拉大且双向插针频繁，坚决暂停追单！");
    } else if (level == 1) {
        // 实时就地识别最新分时形态，传入推演极值阻力与支撑，保证与图表窗口毫秒级同频
        goldsdk::PatternResult realPat;
        if (pts.size() >= 12) {
            goldsdk::PatternConfig pcfg;
            pcfg.feeRate = (src == QStringLiteral("gj") || src == QStringLiteral("xau")) ? 0.0 : AppSettings::instance().feeRate();
            pcfg.atr = ExtremeDatabase::instance().computeAtr(10, src);
            pcfg.predHigh = predHigh;
            pcfg.predLow = predLow;
            const auto lastFc = ForecastService::instance().lastForecast();
            if (lastFc.valid) {
                pcfg.highAlreadyInProb = lastFc.highAlreadyInProb;
                pcfg.lowAlreadyInProb = lastFc.lowAlreadyInProb;
                pcfg.multiDayBias = lastFc.multiDayBias == QStringLiteral("强多") ? goldsdk::TrendBias::StrongBull :
                                    lastFc.multiDayBias == QStringLiteral("偏多") ? goldsdk::TrendBias::MildBull :
                                    lastFc.multiDayBias == QStringLiteral("偏空") ? goldsdk::TrendBias::MildBear :
                                    lastFc.multiDayBias == QStringLiteral("强空") ? goldsdk::TrendBias::StrongBear : goldsdk::TrendBias::Range;
            }
            realPat = goldsdk::ForecastEngine::identifyPattern(pts, pcfg);
        }

        const auto fcRes = ForecastService::instance().lastForecast();
        const bool hasPattern = realPat.valid || (fcRes.valid && !fcRes.patternName.isEmpty());
        const QString patName = realPat.valid ? QString::fromStdString(realPat.name) : fcRes.patternName;
        const QString patBias = realPat.valid ? QString::fromStdString(realPat.bias) : fcRes.patternBias;
        const QString patStrategy = realPat.valid ? QString::fromStdString(realPat.strategy) : fcRes.patternStrategy;
        const double patTrailingStop = realPat.valid ? realPat.atrTrailingStop : fcRes.patternTrailingStop;
        const double patRrr = realPat.valid ? realPat.rrr : fcRes.patternRrr;

        const bool patternIsBuy = hasPattern && (patBias == QStringLiteral("看多突破") || patBias == QStringLiteral("强烈看多"));
        const bool patternIsSell = hasPattern && (patBias == QStringLiteral("看空破位") || patBias == QStringLiteral("强烈看空"));

        const double posGrams = AppSettings::instance().positionGrams();
        const double posCost = AppSettings::instance().positionCost();
        const double feeRate = (src == QStringLiteral("gj") || src == QStringLiteral("xau")) ? 0.0 : AppSettings::instance().feeRate();
        const double breakEvenPrice = (feeRate > 0.0 && feeRate < 0.1) ? (posCost / (1.0 - feeRate)) : posCost;
        const bool isInProfit = (posGrams > 0.0 && posCost > 0.0 && price > breakEvenPrice + 0.15);

        // 0. ATR 移动追踪止盈检测（必须真正处于正收益盈利状态，严禁浮亏状态下误判为“止盈”）
        if (patTrailingStop > 0.0 && posGrams > 0.0 && isInProfit && price <= qMax(breakEvenPrice + 0.10, patTrailingStop)) {
            level = 5;
            tag = tr("🎯 触及移动止盈·锁定利润");
            color = QStringLiteral("#FF7875");
            bg = QStringLiteral("#3E1C1F");
            const double curProfit = (price - posCost) * posGrams - (price * posGrams * feeRate);
            advice = tr("现价 %1 触及动态移动止盈/保盈线 %2（持仓成本 %3，保本线 %4），当前实现净浮盈 +%5 元，建议果断落袋为安锁定正收益！")
                         .arg(price, 0, 'f', 2)
                         .arg(patTrailingStop, 0, 'f', 2)
                         .arg(posCost, 0, 'f', 2)
                         .arg(breakEvenPrice, 0, 'f', 2)
                         .arg(curProfit, 0, 'f', 1);
        } else {
            const double rangeSoFar = qMax(0.05, actHigh - actLow);
            const double curAtr = ExtremeDatabase::instance().computeAtr(10, src);
            const double effAtr = curAtr > 0.1 ? curAtr : (price > 1000.0 ? 25.0 : 4.0);

            // 自适应容差：在窄幅震荡时收缩，在宽幅时适应 ATR，避免微涨/微跌误触发
            const double zoneTol = qBound(0.05, effAtr * 0.12, qMax(0.08, rangeSoFar * 0.20));

            // 短线动量与转折点判定（回溯最近 15 根采样点）
            double recentMin = price;
            double recentMax = price;
            const int nPts = static_cast<int>(pts.size());
            const int lookback = qMin(15, nPts);
            for (int i = nPts - lookback; i < nPts; ++i) {
                if (i >= 0) {
                    recentMin = qMin(recentMin, pts[i].price);
                    recentMax = qMax(recentMax, pts[i].price);
                }
            }
            const double prevP = (nPts >= 6) ? pts[nPts - 6].price : pts.front().price;
            const double shortSlope = price - prevP; // 5个点前的微观斜率动量
            const double reboundFromLow = price - actLow;
            const double pullbackFromHigh = actHigh - price;

            // 企稳转折确认与冲高受阻确认阈值
            const double pos = (rangeSoFar > 0.001) ? qBound(0.0, (price - actLow) / rangeSoFar, 1.0) : 0.5;
            const double minReboundConfirm = qMax(0.08, qMin(effAtr * 0.15, rangeSoFar * 0.18));
            const double minPullbackConfirm = qMax(0.08, qMin(effAtr * 0.15, rangeSoFar * 0.18));

            const bool isBottomStabilized = (reboundFromLow >= minReboundConfirm) && (shortSlope >= -0.02);
            const bool isTopStalled = (pullbackFromHigh >= minPullbackConfirm) && (shortSlope <= 0.01);
            const bool isNearLowZone = (predLow > 0.0 && price <= predLow + zoneTol);
            const bool isNearHighZone = (predHigh > 0.0 && price >= predHigh - zoneTol);
            const bool isBreakingNewHigh = (price >= actHigh - zoneTol * 0.25) || (shortSlope > zoneTol * 0.4);
            const bool isBreakingNewLow = (price <= actLow + zoneTol * 0.25) || (shortSlope < -zoneTol * 0.4);

            // 多头推进 / 弱势下探动能判定（杜绝在上行推进中误判为见顶衰竭）
            const bool isUptrend = (shortSlope > 0.01 && (reboundFromLow >= pullbackFromHigh || pos >= 0.45))
                                || (pos >= 0.60 && shortSlope >= -0.01 && !isTopStalled);
            const bool isDowntrend = (shortSlope < -0.01 && (pullbackFromHigh >= reboundFromLow || pos <= 0.55))
                                  || (pos <= 0.40 && shortSlope <= 0.01 && !isBottomStabilized);

            // 1. 形态破位或关键支撑向下击穿（坚决阻断地板抄底接飞刀）
            if (patternIsSell || (predLow > 0.0 && price < predLow - zoneTol && isBreakingNewLow)) {
                if (posGrams > 0.0) {
                    const double hardStopLossPrice = posCost - qMax(3.8, effAtr * 0.75);
                    const double supportBreakTolerance = qBound(1.8, effAtr * 0.35, 3.5);
                    const bool breakDailySupport = (predLow > 0.0 && price < predLow - supportBreakTolerance && isBreakingNewLow);
                    if (price <= hardStopLossPrice || breakDailySupport) {
                        level = 5;
                        tag = tr("⚠️ 破位止损·防守减仓");
                        color = QStringLiteral("#FF7875");
                        bg = QStringLiteral("#3E1C1F");
                        advice = tr("现价 %1 已击穿关键防守底线（持仓成本 %2，硬止损线 %3，支撑参考 %4），破位下行风险加剧，建议分批减仓防守！")
                                     .arg(price, 0, 'f', 2)
                                     .arg(posCost, 0, 'f', 2)
                                     .arg(hardStopLossPrice, 0, 'f', 2)
                                     .arg(predLow, 0, 'f', 2);
                    } else {
                        // 尚在成本波动缓冲区内，仅做持仓防守预警，杜绝因 15 分钟级微观杂波频繁割肉损耗手续费
                        level = 4;
                        tag = tr("⚠️ 短线承压·防守观察");
                        color = QStringLiteral("#FAAD14");
                        bg = QStringLiteral("#352814");
                        advice = patternIsSell
                                     ? tr("分时形态显现【%1】短线承压，当前运行于持仓成本防守区（成本 %2，硬止损线 %3），耐心等待企稳，避免微观杂波频繁割肉损耗手续费。")
                                           .arg(patName)
                                           .arg(posCost, 0, 'f', 2)
                                           .arg(hardStopLossPrice, 0, 'f', 2)
                                     : tr("现价下探测试推演支撑（%1），关注企稳力度，严格守好成本防守线 %2。")
                                           .arg(predLow, 0, 'f', 2)
                                           .arg(hardStopLossPrice, 0, 'f', 2);
                    }
                } else {
                    level = 2;
                    tag = tr("📉 形态走弱·空仓观望");
                    color = QStringLiteral("#FF7875");
                    bg = QStringLiteral("#3E1C1F");
                    advice = patternIsSell
                                 ? tr("短线形态显现【%1】！%2，当前无仓位，建议保持观望切勿盲目抄底。").arg(patName, patStrategy)
                                 : tr("现价已跌破日内推演关键支撑（%1），杀跌动能释放中，切勿逆势抄底接飞刀！").arg(predLow, 0, 'f', 2);
                }
            }
            // 2. 形态突破或阻力放量向上突破（顺势多头主升，坚决避免过早卖飞）
            else if (patternIsBuy || (predHigh > 0.0 && price > predHigh + zoneTol * 0.5 && isBreakingNewHigh)) {
                const auto lastFc = ForecastService::instance().lastForecast();
                const auto sentiment = SentimentService::instance().recentMarketImpact();
                const bool isMacroBear = m_multiDayBiasText.contains(QStringLiteral("空")) ||
                                         lastFc.multiDayBias.contains(QStringLiteral("空")) ||
                                         sentiment.bias == QStringLiteral("bearish");
                if (isMacroBear) {
                    level = 2;
                    tag = tr("⏳ 弱反抽·谨防诱多");
                    color = QStringLiteral("#FAAD14");
                    bg = QStringLiteral("#352814");
                    advice = tr("虽然分时呈现【%1】，但日线大周期处于空头承压或消息面利空驱动中，分时脉冲极易沦为诱多反抽，切勿在逆势环境下追多接盘！")
                                 .arg(patternIsBuy ? patName : tr("向上脉冲"));
                } else {
                    level = 3;
                    tag = tr("🚀 形态突破·顺势买入");
                    color = QStringLiteral("#52C41A");
                    bg = QStringLiteral("#163219");
                    if (patternIsBuy) {
                        advice = tr("触发【%1】！%2").arg(patName, patStrategy);
                        if (patRrr > 0.0) {
                            advice += tr(" [扣费后净盈亏比 RRR: %1:1 | 移动止盈位: %2]")
                                          .arg(patRrr, 0, 'f', 2)
                                          .arg(patTrailingStop, 0, 'f', 2);
                        }
                    } else {
                        advice = tr("现价放量突破日内推演阻力位（%1），多头主升浪顺势展开，建议顺势持有，防守线抬升至突破位。").arg(predHigh, 0, 'f', 2);
                    }
                }
            }
            // 3. 极值低位区处理：必须区分「探底寻底中（严禁接飞刀）」vs「企稳反弹转折（确认分批买入）」
            else if (isNearLowZone) {
                if (isBottomStabilized) {
                    // 转折确认：已从低点止跌并有效反弹，动能衰竭
                    const auto lastFc = ForecastService::instance().lastForecast();
                    const auto sentiment = SentimentService::instance().recentMarketImpact();
                    const bool isMacroBear = m_multiDayBiasText.contains(QStringLiteral("空")) ||
                                             lastFc.multiDayBias.contains(QStringLiteral("空")) ||
                                             sentiment.bias == QStringLiteral("bearish");
                    if (isMacroBear) {
                        level = 2;
                        tag = tr("📉 探底微弱反抽·等待筑底");
                        color = QStringLiteral("#73D13D");
                        bg = QStringLiteral("#1B301D");
                        advice = tr("现价下探至推演支撑区（%1）虽有微幅反抽（+%2元），但大周期空头趋势或利空消息压制沉重，底部尚未稳固，建议耐心等待右侧真正放量筑底。")
                                     .arg(predLow, 0, 'f', 2)
                                     .arg(reboundFromLow, 0, 'f', 2);
                    } else {
                        level = 3;
                        tag = tr("🟢 企稳反弹·分批建仓");
                        color = QStringLiteral("#52C41A");
                        bg = QStringLiteral("#163219");
                        advice = tr("现价下探至推演低点支撑区（%1）后出现企稳反弹迹象（自低点回升+%2元），杀跌衰竭，适合企稳分批建仓或定投。预计低点窗口: %3。")
                                     .arg(predLow, 0, 'f', 2)
                                     .arg(reboundFromLow, 0, 'f', 2)
                                     .arg(m_predLowTimeWindow.isEmpty() ? tr("时段末") : m_predLowTimeWindow);
                    }
                } else {
                    // 仍在下探跌落中：未见止跌，严禁接飞刀
                    level = 2;
                    tag = tr("📉 探底寻底中·等待企稳");
                    color = QStringLiteral("#73D13D");
                    bg = QStringLiteral("#1B301D");
                    advice = tr("现价正在下探测试推演支撑区（%1），短线下行动能仍在释放，切勿盲目抢反弹接飞刀！耐心等待止跌企稳与转折信号。")
                                 .arg(predLow, 0, 'f', 2);
                }
            }
            // 4. 极值高位区处理：必须区分「冲高中顺势（严禁卖飞）」vs「冲高受阻回落（确认分批止盈）」
            else if (isNearHighZone) {
                if (isTopStalled) {
                    // 转折确认：冲高受阻，已从最高位回撤确认滞涨
                    level = 5;
                    tag = tr("🎯 冲高受阻·分批止盈");
                    color = QStringLiteral("#FF7875");
                    bg = QStringLiteral("#3E1C1F");
                    advice = tr("现价已触及推演高点阻力区（%1）并显现遇阻回落（自今高回落-%2元），短线上方抛压沉重且盈亏比偏低，建议逢高止盈锁定利润。预计高点窗口: %3。")
                                 .arg(predHigh, 0, 'f', 2)
                                 .arg(pullbackFromHigh, 0, 'f', 2)
                                 .arg(m_predHighTimeWindow.isEmpty() ? tr("时段末") : m_predHighTimeWindow);
                } else {
                    // 正在冲高破高途中：多头强势，顺势持有
                    level = 4;
                    tag = tr("🚀 冲高中·多头顺势");
                    color = QStringLiteral("#FAAD14");
                    bg = QStringLiteral("#352814");
                    advice = tr("现价正上攻测试推演高点阻力区（%1），多头动能充沛，顺势持有并跟踪抬升防守位，切勿过早盲目猜顶卖飞！预计高点窗口: %2。")
                                 .arg(predHigh, 0, 'f', 2)
                                 .arg(m_predHighTimeWindow.isEmpty() ? tr("时段末") : m_predHighTimeWindow);
                }
            }
            // 5. 高点已现防守（必须经过遇阻滞涨、动能衰竭与回撤确认，严禁在上行推进阶段误报）
            else if (isTopStalled && (m_predHighTimeWindow.contains(QStringLiteral("高点大概率已现")) || m_predHighTimeWindow.contains(QStringLiteral("已确立"))) && shortSlope <= 0.0 && !isUptrend) {
                level = 4;
                tag = tr("⚠️ 高点已现·防守保护");
                color = QStringLiteral("#FAAD14");
                bg = QStringLiteral("#352814");
                advice = tr("当日高点阻力大概率已于前期确立（%1），现价自高位回撤-%2元显现滞涨，短线上行动能衰竭，建议防守保护多头利润，切勿盲目追高！")
                             .arg(m_predHighTimeWindow)
                             .arg(pullbackFromHigh, 0, 'f', 2);
            }
            // 6. 顺势多头反弹与推进（上行阶段明确指示多头反弹、顺势持有，杜绝猜顶）
            else if (isUptrend) {
                if (shortSlope > zoneTol * 0.25 || pos >= 0.68) {
                    level = 4;
                    tag = tr("📈 多头反弹·顺势推进");
                    color = QStringLiteral("#52C41A");
                    bg = QStringLiteral("#163219");
                    advice = tr("短线处于多头反弹/上行推进通道中（动能向上 +%1），建议顺势持有并跟踪抬升防守位，关注上方推演阻力（%2）。预计高点窗口: %3。")
                                 .arg(shortSlope, 0, 'f', 2)
                                 .arg(predHigh > 0.0 ? QString::number(predHigh, 'f', 2) : tr("前期高位"))
                                 .arg(m_predHighTimeWindow.isEmpty() ? tr("欧美盘高峰") : m_predHighTimeWindow);
                } else {
                    level = 3;
                    tag = tr("📈 偏多运行·顺势持有");
                    color = QStringLiteral("#69C0FF");
                    bg = QStringLiteral("#142738");
                    advice = tr("现价站稳日内中轴上方偏多运行（反弹 +%1元），反弹形态良好，建议持有现有头寸，动态关注上方目标 %2。预计高点窗口: %3。")
                                 .arg(reboundFromLow, 0, 'f', 2)
                                 .arg(predHigh > 0.0 ? QString::number(predHigh, 'f', 2) : tr("--"))
                                 .arg(m_predHighTimeWindow.isEmpty() ? tr("时段后程") : m_predHighTimeWindow);
                }
            }
            // 7. 弱势下探调整阶段
            else if (isDowntrend) {
                level = 2;
                tag = tr("📉 下探调整·空仓等待");
                color = QStringLiteral("#FF7875");
                bg = QStringLiteral("#3E1C1F");
                advice = tr("短线处于下探调整走势中（微观动能 -%1），下方推演支撑参考 %2，未现止跌企稳前切勿盲目抢反弹。预计低点窗口: %3。")
                             .arg(-shortSlope, 0, 'f', 2)
                             .arg(predLow > 0.0 ? QString::number(predLow, 'f', 2) : tr("前期低位"))
                             .arg(m_predLowTimeWindow.isEmpty() ? tr("时段后程") : m_predLowTimeWindow);
            }
            // 8. 均线与分位常规状态
            else if (pct20 >= 80 && ma5 > 0.0 && price > ma5) {
                level = 4;
                tag = tr("📈 偏多·谨防追高");
                color = QStringLiteral("#FAAD14");
                bg = QStringLiteral("#352814");
                advice = tr("价格站上MA5(%1)且处于近20日高分位(%2%)，短期多头强势，谨防追高回落。").arg(ma5, 0, 'f', 2).arg(pct20, 0, 'f', 0);
            } else if (pct20 <= 20 && ma5 > 0.0 && price < ma5) {
                const auto lastFc = ForecastService::instance().lastForecast();
                const auto sentiment = SentimentService::instance().recentMarketImpact();
                const bool isMacroBear = m_multiDayBiasText.contains(QStringLiteral("空")) ||
                                         lastFc.multiDayBias.contains(QStringLiteral("空")) ||
                                         sentiment.bias == QStringLiteral("bearish");
                level = 2;
                if (isMacroBear) {
                    tag = tr("📉 弱势超跌·空仓观望");
                    color = QStringLiteral("#FF7875");
                    bg = QStringLiteral("#3E1C1F");
                    advice = tr("处于近20日低分位(%1%)且压制在MA5(%2)下方，大周期处于空头通道中，超跌绝不盲目左侧接飞刀，建议保持空仓等待右侧企稳。")
                                 .arg(pct20, 0, 'f', 0).arg(ma5, 0, 'f', 2);
                } else {
                    tag = tr("📉 超跌寻底·等待企稳");
                    color = QStringLiteral("#FAAD14");
                    bg = QStringLiteral("#352814");
                    advice = tr("处于近20日低分位(%1%)且运行于MA5(%2)下方，短线超跌寻底，建议耐心等待止跌企稳信号，切勿急于盲目抄底。")
                                 .arg(pct20, 0, 'f', 0).arg(ma5, 0, 'f', 2);
                }
            }
        }

        // 多因子共振与 0.4% 费率安全边际校验
        {
            auto& db = ExtremeDatabase::instance();
            goldsdk::DecisionConfig dcfg;
            dcfg.feeRate = (src == QStringLiteral("gj") || src == QStringLiteral("xau")) ? 0.0 : AppSettings::instance().feeRate();
            dcfg.currentPrice = price;
            dcfg.atr = db.computeAtr(10, src);
            dcfg.predHigh = predHigh;
            dcfg.predLow = predLow;
            dcfg.hasForecast = (predHigh > predLow);
            if (fcRes.valid) {
                dcfg.highAlreadyInProb = fcRes.highAlreadyInProb;
                dcfg.lowAlreadyInProb = fcRes.lowAlreadyInProb;
            }
            const auto dailyBars = db.loadRecentDailyBars(35, src);
            if (dailyBars.size() >= 10) {
                dcfg.multiDayTrend = goldsdk::ForecastEngine::multiDayTrendFromBars(dailyBars);
            }
            dcfg.pattern = realPat;
            dcfg.crossMarketLeadSignal = db.computeCrossMarketLeadSignal(src);
            dcfg.sentimentScore = SentimentService::instance().recentMarketImpact(6).decayedScore;
            confluence = goldsdk::ForecastEngine::evaluateDecision(pts, dcfg);

            if (confluence.valid) {
                // 费损安全边际拦截：有卖出手续费且共振引擎判定净空间/盈亏比未达门槛时，拦截低赔率买单
                if (level == 3 && dcfg.feeRate > 0.0 && !confluence.feeEdgeSatisfied && confluence.score < 68) {
                    level = 2;
                    tag = tr("⏳ 盈亏比不足·观望过滤");
                    color = QStringLiteral("#FAAD14");
                    bg = QStringLiteral("#352814");
                    advice = tr("多因子共振 %1/100 [%2]，当前扣费(0.4%)后净空间 %3 元 / RRR %4:1 未达安全边际（要求净利≥1.2且RRR≥1.4），避免微幅震荡反复损耗手续费。")
                                 .arg(confluence.score)
                                 .arg(QString::fromUtf8(goldsdk::MultiDayTrend::regimeLabel(confluence.regime)))
                                 .arg(confluence.netReward, 0, 'f', 2)
                                 .arg(confluence.rrr, 0, 'f', 2);
                } else if (level == 1 &&
                           (confluence.action == goldsdk::DecisionAction::StrongBuy ||
                            confluence.action == goldsdk::DecisionAction::BuyOnDips) &&
                           confluence.feeEdgeSatisfied && confluence.score >= 70) {
                    level = 3;
                    tag = tr("🟢 多因子共振布局 (%1分)").arg(confluence.score);
                    color = QStringLiteral("#52C41A");
                    bg = QStringLiteral("#163219");
                    advice = tr("%1 | 稳健买点 %2 | 止盈目标 %3 | 防守止损 %4 (扣费RRR %5:1)")
                                 .arg(QString::fromStdString(confluence.summary))
                                 .arg(confluence.conservativeBuy, 0, 'f', 2)
                                 .arg(confluence.takeProfit1, 0, 'f', 2)
                                 .arg(confluence.stopLoss, 0, 'f', 2)
                                 .arg(confluence.rrr, 0, 'f', 2);
                } else if (level == 1 &&
                           confluence.action == goldsdk::DecisionAction::StrongSell &&
                           confluence.score <= 25) {
                    level = 4;
                    tag = tr("⚠️ 多因子共振防守 (%1分)").arg(confluence.score);
                    color = QStringLiteral("#FF7875");
                    bg = QStringLiteral("#3E1C1F");
                    advice = QString::fromStdString(confluence.summary);
                }
            }
        }
    }

    const QString upcomingWarn = EventCalendar::currentEventWarning();
    if (!upcomingWarn.isEmpty() && !advice.contains(upcomingWarn)) {
        advice += QStringLiteral("\n") + upcomingWarn;
    }

    // 卖出后冷静期与多Tick企稳确认机制，彻底解决“卖出后2-3秒立即提示买入”的翻转抽搐
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    const qint64 kSellAdviceCooldownMs = 8 * 60 * 1000; // 卖出后 8 分钟内禁止立即反手提示买入
    const bool inSellCooldown = (m_lastSellAdviceMs > 0 && (nowMs - m_lastSellAdviceMs < kSellAdviceCooldownMs));

    if (level == 5 || (level == 4 && tag.contains(QStringLiteral("高点已现·防守保护")))) {
        m_lastSellAdviceMs = nowMs;
        m_consecutiveBuyTicks = 0;
    } else if (level == 3) {
        if (inSellCooldown) {
            level = 2;
            tag = tr("⏳ 卖出观察·等待企稳确认");
            color = QStringLiteral("#73D13D");
            bg = QStringLiteral("#1B301D");
            const int minsAgo = qMax(1, static_cast<int>((nowMs - m_lastSellAdviceMs) / 60000));
            advice = tr("短线此前已触发卖出防守/止盈（距今仅 %1 分钟），当前处于卖出后冷静观察期，坚决阻断立即反手追多接飞刀，耐心等待右侧支撑与形态稳固。")
                         .arg(minsAgo);
        } else {
            m_consecutiveBuyTicks++;
            if (m_consecutiveBuyTicks < 2) {
                level = 2;
                tag = tr("👀 观察企稳·等待信号确认");
                color = QStringLiteral("#73D13D");
                bg = QStringLiteral("#1B301D");
            }
        }
    } else {
        m_consecutiveBuyTicks = 0;
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
    if (confluence.valid) {
        m_decisionMetricsText += tr(" · 共振 %1分[%2]")
                                     .arg(confluence.score)
                                     .arg(QString::fromUtf8(goldsdk::MultiDayTrend::regimeLabel(confluence.regime)));
    }
    m_decisionSessionText = TradingSession::statusText(src, QDateTime::currentDateTime());

    QString feeLine;
    if (src == QStringLiteral("zs") && price > 0.0) {
        const double feeRate = AppSettings::instance().feeRate();
        const double fee = price * feeRate;
        const double net = price - fee;
        const double breakEven = price / (1.0 - feeRate);
        if (level == 3) {
            feeLine = tr("\n• 浙商费率(买0/卖0.4%): 买入保本卖出价 %1 元/g (抗卖出费损 +%2 元/g)")
                          .arg(breakEven, 0, 'f', 2)
                          .arg(breakEven - price, 0, 'f', 2);
        } else {
            feeLine = tr("\n• 浙商费率(买0/卖0.4%): 若当前卖出扣费 %1 元/g · 净到手: %2 元/g")
                          .arg(fee, 0, 'f', 2)
                          .arg(net, 0, 'f', 2);
        }
    }
    if (confluence.valid && confluence.conservativeBuy > 0.0 && confluence.takeProfit1 > 0.0) {
        feeLine += tr("\n• 共振参考: 买 %1 / 盈 %2 / 损 %3 (RRR %4:1)")
                       .arg(confluence.conservativeBuy, 0, 'f', 2)
                       .arg(confluence.takeProfit1, 0, 'f', 2)
                       .arg(confluence.stopLoss, 0, 'f', 2)
                       .arg(confluence.rrr, 0, 'f', 2);
    }

    m_decisionTooltip = tr("【💡 实时趋势与交易决策参考】\n"
                           "• 建议：%1\n"
                           "• 当日预测区间：%2\n"
                           "• 预期时间窗口：%3\n"
                           "• 核心推演催化：%4\n"
                           "• 均线与分位：%5\n"
                           "• 时段状态：%6%7\n"
                           "（点击可直接打开分时走势图）")
                            .arg(m_decisionAdvice)
                            .arg(m_decisionRangeText)
                            .arg(m_decisionTimeWindowText)
                            .arg(m_predCatalyst.isEmpty() ? (m_predDailyPath.isEmpty() ? tr("待定") : m_predDailyPath) : m_predCatalyst)
                            .arg(m_decisionMetricsText)
                            .arg(m_decisionSessionText)
                            .arg(feeLine);
    checkAutoSimulateTrading(price, level, tag);
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
    emit positionChanged();
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

// ============================================================
// 自动化 Agent 相关接口实现
// ============================================================
void QmlBridge::openAgentWindow() {
    Logger::info(QStringLiteral("QmlBridge::openAgentWindow called"));
    if (m_agentWindow) {
        m_agentWindow->setVisible(true);
        m_agentWindow->show();
        m_agentWindow->raise();
        m_agentWindow->requestActivate();
    }
    emit requestShowAgentWindow();
}

void QmlBridge::closeAgentWindow() {
    Logger::info(QStringLiteral("QmlBridge::closeAgentWindow called"));
    if (m_agentWindow) {
        m_agentWindow->setVisible(false);
    }
    emit requestCloseAgentWindow();
}

void QmlBridge::toggleAgentWindow() {
    Logger::info(QStringLiteral("QmlBridge::toggleAgentWindow called"));
    if (m_agentWindow && m_agentWindow->isVisible()) {
        closeAgentWindow();
        return;
    }
    openAgentWindow();
}

void QmlBridge::openAgentCardWindow() {
    if (m_agentCardWindow) {
        m_agentCardWindow->setVisible(true);
        m_agentCardWindow->show();
        m_agentCardWindow->raise();
    }
}

void QmlBridge::closeAgentCardWindow() {
    if (m_agentCardWindow) {
        m_agentCardWindow->setVisible(false);
    }
}

QVariantList QmlBridge::getAgentTasks() const {
    return AgentService::instance().allTasksVariant();
}

bool QmlBridge::addOrUpdateAgentTask(const QVariantMap &taskData) {
    return AgentService::instance().saveTask(AgentTask::fromMap(taskData));
}

bool QmlBridge::deleteAgentTask(const QString &taskId) {
    return AgentService::instance().deleteTask(taskId);
}

bool QmlBridge::toggleAgentTask(const QString &taskId, bool enabled) {
    return AgentService::instance().toggleTask(taskId, enabled);
}

void QmlBridge::runAgentTaskNow(const QString &taskId) {
    AgentService::instance().runTaskNow(taskId);
}

void QmlBridge::advanceCurriculumDay(const QString &taskId) {
    AgentService::instance().advanceCurriculum(taskId);
}

void QmlBridge::setCurriculumDay(const QString &taskId, int day) {
    AgentService::instance().setCurriculumDay(taskId, day);
}

QVariantMap QmlBridge::getCurriculumTopic(int day) const {
    return AgentService::instance().getCurriculumTopic(day);
}

QVariantMap QmlBridge::getCurriculumTopicForAgent(const QString &agentId, int day) const {
    return AgentService::instance().getCurriculumTopic(agentId, day);
}

int QmlBridge::getCurriculumTotalDaysForAgent(const QString &agentId) const {
    return AgentService::instance().curriculumTotalDays(agentId);
}

QString QmlBridge::getCurriculumWechatSummaryMarkdown() const {
    return AgentService::instance().getCurriculumWechatSummaryMarkdown();
}

QString QmlBridge::getCurriculumWechatSummaryMarkdownForAgent(const QString &agentId) const {
    return AgentService::instance().getCurriculumWechatSummaryMarkdown(agentId);
}

QString QmlBridge::getCurriculumDayWechatMarkdown(int day) const {
    return AgentService::instance().getCurriculumDayWechatMarkdown(day);
}

QString QmlBridge::getCurriculumDayWechatMarkdownForAgent(const QString &agentId, int day) const {
    return AgentService::instance().getCurriculumDayWechatMarkdown(agentId, day);
}

void QmlBridge::copyToClipboard(const QString &text) {
    if (QClipboard *clipboard = QGuiApplication::clipboard()) {
        clipboard->setText(text);
    }
}

QString QmlBridge::getAgentDatabasePath() const {
    return AgentService::instance().databasePath();
}

void QmlBridge::openLearningNotesDir() {
    const QString dir = AgentService::instance().learningNotesDir();
    QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
}

QStringList QmlBridge::getNoteFiles() const {
    return AgentService::instance().getNoteFiles();
}

QString QmlBridge::getNoteContent(const QString &fileName) const {
    return AgentService::instance().getNoteContent(fileName);
}


void QmlBridge::openAgentWindowWithNote(const QString &fileName) {
    openAgentWindow();
    emit requestOpenNoteInAgentWindow(fileName);
}

void QmlBridge::openAgentChatWindow(const QString &initialPrompt, const QString &contextTopic) {
    if (m_agentChatWindow) {
        m_agentChatWindow->setVisible(true);
        m_agentChatWindow->show();
        m_agentChatWindow->raise();
        m_agentChatWindow->requestActivate();
    }
    emit requestOpenAgentChatWindow(initialPrompt, contextTopic);
}

void QmlBridge::closeAgentChatWindow() {
    if (m_agentChatWindow) {
        m_agentChatWindow->setVisible(false);
    }
}

void QmlBridge::askAiQuestion(const QString &question,
                               const QString &contextTopic,
                               const QString &provider,
                               const QString &model,
                               const QString &apiKey,
                               const QString &customEndpoint)
{
    AgentAiService::instance().askQuestion(question, contextTopic, provider, model, apiKey, customEndpoint);
}

void QmlBridge::cancelAiQuestion() {
    AgentAiService::instance().cancel();
}

bool QmlBridge::isAiBusy() const {
    return AgentAiService::instance().isBusy();
}

QString QmlBridge::defaultAiProvider() const {
    return AgentAiService::instance().defaultProvider();
}

QString QmlBridge::defaultAiApiKey() const {
    return AgentAiService::instance().defaultApiKey();
}

QString QmlBridge::defaultAiModel() const {
    return AgentAiService::instance().defaultModel();
}

void QmlBridge::saveAiSettings(const QString &provider, const QString &apiKey, const QString &model) {
    if (!provider.isEmpty()) AppSettings::instance().setLlmProvider(provider);
    if (!apiKey.isEmpty()) AppSettings::instance().setXaiApiKey(apiKey);
    if (!model.isEmpty()) AppSettings::instance().setXaiModel(model);
    AppSettings::instance().save();
}


bool QmlBridge::autoSimulateTrading() const {
    return AppSettings::instance().autoSimulateTrading();
}

void QmlBridge::setAutoSimulateTrading(bool on) {
    if (AppSettings::instance().autoSimulateTrading() != on) {
        AppSettings::instance().setAutoSimulateTrading(on);
        AppSettings::instance().save();
        emit autoSimulateTradingChanged();
    }
}

double QmlBridge::simulateTradeBatchGrams() const {
    return AppSettings::instance().simulateTradeBatchGrams();
}

void QmlBridge::setSimulateTradeBatchGrams(double g) {
    if (!qFuzzyCompare(AppSettings::instance().simulateTradeBatchGrams(), g)) {
        AppSettings::instance().setSimulateTradeBatchGrams(g);
        AppSettings::instance().save();
        emit simulateTradeBatchGramsChanged();
    }
}

double QmlBridge::totalRealizedPnl() const {
    const QString src = AppSettings::instance().dataSource();
    return ExtremeDatabase::instance().totalRealizedPnl(src);
}

double QmlBridge::positionGrams() const {
    return AppSettings::instance().positionGrams();
}

double QmlBridge::positionCost() const {
    return AppSettings::instance().positionCost();
}

QString QmlBridge::positionSummaryText() const {
    const double g = positionGrams();
    const double c = positionCost();
    if (g <= 0.0) return tr("当前空仓 (0g)");
    return tr("持仓 %1g · 成本 %2元/g").arg(g, 0, 'f', 2).arg(c, 0, 'f', 2);
}

void QmlBridge::openSimulatedTradesWindow() {
    Logger::info(QStringLiteral("QmlBridge::openSimulatedTradesWindow called"));
    if (m_simulatedTradesWindow) {
        m_simulatedTradesWindow->setVisible(true);
        m_simulatedTradesWindow->show();
        m_simulatedTradesWindow->raise();
        m_simulatedTradesWindow->requestActivate();
    }
    emit requestShowSimulatedTrades();
}

void QmlBridge::closeSimulatedTradesWindow() {
    Logger::info(QStringLiteral("QmlBridge::closeSimulatedTradesWindow called"));
    if (m_simulatedTradesWindow) {
        m_simulatedTradesWindow->setVisible(false);
    }
    emit requestCloseSimulatedTrades();
}

void QmlBridge::toggleSimulatedTradesWindow() {
    Logger::info(QStringLiteral("QmlBridge::toggleSimulatedTradesWindow called"));
    if (m_simulatedTradesWindow && m_simulatedTradesWindow->isVisible()) {
        closeSimulatedTradesWindow();
        return;
    }
    openSimulatedTradesWindow();
}

bool QmlBridge::executeSimulatedTrade(const QString &action, double grams, const QString &customNote) {
    if (m_price <= 0.0) return false;
    const double price = m_price;
    const QString src = AppSettings::instance().dataSource();
    const double sellFeeRate = (src == QStringLiteral("gj") || src == QStringLiteral("xau")) ? 0.0 : AppSettings::instance().feeRate();
    const double buyFeeRate = 0.0; // 浙商积存金等当前买入手续费为 0，仅卖出收取 0.4%
    const QString decisionTag = m_decisionText.isEmpty() ? tr("手工决策") : m_decisionText;

    double tradeGrams = grams > 0.0 ? grams : AppSettings::instance().simulateTradeBatchGrams();
    if (tradeGrams <= 0.0) tradeGrams = 10.0;

    double oldGrams = AppSettings::instance().positionGrams();
    double oldCost = AppSettings::instance().positionCost();
    double newGrams = oldGrams;
    double newCost = oldCost;
    double realizedPnl = 0.0;
    double grossAmount = 0.0;
    double fee = 0.0;

    if (action.toUpper() == QStringLiteral("BUY")) {
        grossAmount = price * tradeGrams;
        fee = grossAmount * buyFeeRate;
        newGrams = oldGrams + tradeGrams;
        if (oldGrams <= 0.0 || oldCost <= 0.0) {
            newCost = (grossAmount + fee) / tradeGrams;
        } else {
            newCost = (oldGrams * oldCost + grossAmount + fee) / newGrams;
        }
        realizedPnl = 0.0;
    } else if (action.toUpper() == QStringLiteral("SELL")) {
        if (oldGrams <= 0.0) {
            if (m_tray) {
                m_tray->showMessage(tr("模拟交易提醒"), tr("当前无持仓可卖出！"), QSystemTrayIcon::Warning, 3000);
            }
            return false;
        }
        m_lastSellAdviceMs = QDateTime::currentMSecsSinceEpoch();
        m_consecutiveBuyTicks = 0;
        tradeGrams = qMin(tradeGrams, oldGrams);
        grossAmount = price * tradeGrams;
        fee = grossAmount * sellFeeRate;
        realizedPnl = (price - oldCost) * tradeGrams - fee;
        newGrams = oldGrams - tradeGrams;
        if (newGrams <= 0.0001) {
            newGrams = 0.0;
            newCost = 0.0;
        } else {
            newCost = oldCost;
        }
    } else {
        return false;
    }

    AppSettings::instance().setPositionGrams(newGrams);
    AppSettings::instance().setPositionCost(newCost);
    AppSettings::instance().save();

    SimulatedTradeEntry entry;
    entry.tradeTime = QDateTime::currentDateTime();
    entry.source = src;
    entry.action = action.toUpper();
    entry.decisionTag = decisionTag;
    entry.price = price;
    entry.grams = tradeGrams;
    entry.amount = grossAmount;
    entry.fee = fee;
    entry.realizedPnl = realizedPnl;
    entry.remainingGrams = newGrams;
    entry.remainingCost = newCost;
    entry.note = customNote.isEmpty() ? QStringLiteral("根据决策【%1】模拟%2").arg(decisionTag, action.toUpper() == "BUY" ? "买入" : "卖出") : customNote;
    ExtremeDatabase::instance().insertSimulatedTrade(entry);

    updatePnL(price);
    emit positionChanged();
    emit simulatedTradesUpdated();
    emit simulatedTradeExecuted(action.toUpper(), price, tradeGrams, realizedPnl, decisionTag);

    if (m_tray && AppSettings::instance().trayNotifyOnAlert()) {
        QString msg;
        if (action.toUpper() == QStringLiteral("BUY")) {
            msg = tr("【模拟买入记账完成】\n决策: %1\n成交: %2 元/g · %3 克 (金额: %4 元, 买入免手续费: %5 元)\n现持仓: %6 克 · 成本: %7 元/g")
                      .arg(decisionTag)
                      .arg(price, 0, 'f', 2)
                      .arg(tradeGrams, 0, 'f', 2)
                      .arg(grossAmount, 0, 'f', 2)
                      .arg(fee, 0, 'f', 2)
                      .arg(newGrams, 0, 'f', 2)
                      .arg(newCost, 0, 'f', 2);
        } else {
            msg = tr("【模拟卖出记账完成】\n决策: %1\n成交: %2 元/g · %3 克 (金额: %4 元, 扣费: %5 元)\n本次实现盈亏: %6%7 元\n剩余持仓: %8 克")
                      .arg(decisionTag)
                      .arg(price, 0, 'f', 2)
                      .arg(tradeGrams, 0, 'f', 2)
                      .arg(grossAmount, 0, 'f', 2)
                      .arg(fee, 0, 'f', 2)
                      .arg(realizedPnl >= 0 ? "+" : "")
                      .arg(realizedPnl, 0, 'f', 2)
                      .arg(newGrams, 0, 'f', 2);
        }
        m_tray->showMessage(tr("模拟交易完成"), msg, QSystemTrayIcon::Information, 5000);
    }
    return true;
}

QVariantList QmlBridge::getSimulatedTrades(int limit) const {
    const QString src = AppSettings::instance().dataSource();
    const auto list = ExtremeDatabase::instance().loadSimulatedTrades(limit, src);
    QVariantList res;
    res.reserve(list.size());
    for (const auto &t : list) {
        QVariantMap m;
        m[QStringLiteral("id")] = t.id;
        m[QStringLiteral("tradeTime")] = t.tradeTime.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
        m[QStringLiteral("source")] = t.source;
        m[QStringLiteral("action")] = t.action;
        m[QStringLiteral("decisionTag")] = t.decisionTag;
        m[QStringLiteral("price")] = t.price;
        m[QStringLiteral("grams")] = t.grams;
        m[QStringLiteral("amount")] = t.amount;
        m[QStringLiteral("fee")] = t.fee;
        m[QStringLiteral("realizedPnl")] = t.realizedPnl;
        m[QStringLiteral("remainingGrams")] = t.remainingGrams;
        m[QStringLiteral("remainingCost")] = t.remainingCost;
        m[QStringLiteral("note")] = t.note;
        res.append(m);
    }
    return res;
}

double QmlBridge::getTotalRealizedPnl() const {
    return totalRealizedPnl();
}

void QmlBridge::clearSimulatedTrades() {
    ExtremeDatabase::instance().clearSimulatedTrades();
    emit simulatedTradesUpdated();
}

void QmlBridge::checkAutoSimulateTrading(double price, int level, const QString &tag) {
    if (!AppSettings::instance().autoSimulateTrading() || price <= 0.0) return;
    if (AppSettings::instance().isInQuietHours()) return;

    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    const qint64 kMinAutoTradeIntervalMs = 20 * 60 * 1000;
    if (m_lastAutoTradeMs > 0 && (nowMs - m_lastAutoTradeMs < kMinAutoTradeIntervalMs)) {
        return; // 严格执行20分钟交易冷却间隔，杜绝因微小价差穿透冷却期
    }

    const double currentGrams = AppSettings::instance().positionGrams();
    const double batchGrams = AppSettings::instance().simulateTradeBatchGrams();
    const double posCost = AppSettings::instance().positionCost();
    const QString src = AppSettings::instance().dataSource();
    const double feeRate = (src == QStringLiteral("gj") || src == QStringLiteral("xau")) ? 0.0 : AppSettings::instance().feeRate();
    const double fullCycleFee = price * feeRate; // 买入0费率，仅卖出扣除 0.4%
    const double breakEvenPrice = (feeRate > 0.0 && feeRate < 0.1) ? (posCost / (1.0 - feeRate)) : posCost;

    // 卖出后冷静期：至少30分钟内禁止反手追多买入，彻底避免“卖出后立即反手买入”
    const qint64 kPostSellBuyCooldownMs = 30 * 60 * 1000;
    if (m_lastAutoTradeAction == QStringLiteral("SELL") && (nowMs - m_lastAutoTradeMs < kPostSellBuyCooldownMs)) {
        return;
    }

    const bool isBuySignal = (level == 3) && (tag.contains(QStringLiteral("买入")) || tag.contains(QStringLiteral("建仓")));
    const bool isSellSignal = (level == 5 && currentGrams > 0.0) ||
                             (level == 4 && tag.contains(QStringLiteral("高点已现·防守保护")) && currentGrams > 0.0);

    const auto lastFc = ForecastService::instance().lastForecast();
    const auto sentiment = SentimentService::instance().recentMarketImpact();

    // 买入前进行宏观大势、消息面与空间费损核验
    if (isBuySignal && m_lastAutoTradeDecisionTag != tag) {
        // 1. 宏观日线趋势与消息面严控：强空/偏空或利空消息下严禁逆势接飞刀
        const bool isMacroBear = m_multiDayBiasText.contains(QStringLiteral("空")) ||
                                 lastFc.multiDayBias.contains(QStringLiteral("空")) ||
                                 sentiment.bias == QStringLiteral("bearish");
        if (isMacroBear) {
            Logger::info(QStringLiteral("AutoTrade skipped BUY: macro trend (%1) or news sentiment (%2) is bearish.")
                             .arg(m_multiDayBiasText).arg(sentiment.bias));
            return;
        }

        // 2. 高点已大概率出现：禁止追高
        if (lastFc.valid && lastFc.highAlreadyInProb >= 0.70) {
            Logger::info(QStringLiteral("AutoTrade skipped BUY: highAlreadyInProb (%1) >= 0.70.")
                             .arg(lastFc.highAlreadyInProb));
            return;
        }

        // 3. 空间与摩擦成本核验：浙商积存金卖出扣 0.4%（约3.3~3.5元/g）。
        // 预期上方空间必须足以覆盖至少 2.5 倍卖出手续费（保留至少 8.5 元/g 的安全空间），否则期望为负，拒绝开仓
        const double minUpsideRequired = (feeRate > 0.0) ? qMax(8.0, fullCycleFee * 2.5) : 3.0;
        if (m_predHigh > 0.0 && (m_predHigh - price < minUpsideRequired)) {
            Logger::info(QStringLiteral("AutoTrade skipped BUY: expectedUpside (%1) < minUpsideRequired (%2)")
                             .arg(m_predHigh - price).arg(minUpsideRequired));
            return;
        }

        m_lastAutoTradeMs = nowMs;
        m_lastAutoTradePrice = price;
        m_lastAutoTradeDecisionTag = tag;
        m_lastAutoTradeAction = QStringLiteral("BUY");
        executeSimulatedTrade(QStringLiteral("BUY"), batchGrams,
                              tr("决策算法【%1】触发自动模拟买入").arg(tag));
    } else if (isSellSignal && m_lastAutoTradeDecisionTag != tag) {
        // 卖出过滤：
        // 1. 止盈类操作：必须确保扣费后处于真实净收益盈利状态
        const bool isProfitTaking = tag.contains(QStringLiteral("止盈")) || tag.contains(QStringLiteral("冲高受阻"));
        if (isProfitTaking && currentGrams > 0.0 && posCost > 0.0) {
            if (price <= breakEvenPrice + 0.10) {
                Logger::info(QStringLiteral("AutoTrade skipped SELL on take-profit: price (%1) <= breakEvenPrice (%2)")
                                 .arg(price).arg(breakEvenPrice));
                return;
            }
        }

        // 2. 防守减仓 / 止损类操作：必须经过硬止损线或日线支撑跌破核验，杜绝 15 分钟级微观杂波频繁割肉
        const bool isDefensiveSell = tag.contains(QStringLiteral("破位")) || tag.contains(QStringLiteral("防守")) || tag.contains(QStringLiteral("失效"));
        if (isDefensiveSell && currentGrams > 0.0 && posCost > 0.0) {
            const double curAtr = ExtremeDatabase::instance().computeAtr(10, src);
            const double effAtr = curAtr > 0.1 ? curAtr : 4.0;
            const double hardStopLossPrice = posCost - qMax(3.8, effAtr * 0.75);
            const double supportBreakTolerance = qBound(1.8, effAtr * 0.35, 3.5);
            const bool breakDailySupport = (m_predLow > 0.0 && price < m_predLow - supportBreakTolerance);
            if (price > hardStopLossPrice && !breakDailySupport) {
                Logger::info(QStringLiteral("AutoTrade skipped SELL on noise: price (%1) > hardStopLoss (%2) and above daily support (%3)")
                                 .arg(price).arg(hardStopLossPrice).arg(m_predLow - supportBreakTolerance));
                return;
            }
        }

        m_lastAutoTradeMs = nowMs;
        m_lastAutoTradePrice = price;
        m_lastAutoTradeDecisionTag = tag;
        m_lastAutoTradeAction = QStringLiteral("SELL");
        m_lastSellAdviceMs = nowMs;

        // 梯级减仓机制：彻底废弃“一旦破位就全仓100%市价割肉”的极端恶劣逻辑！
        // 区分常规防守（减仓 50% 或 1个 batch，保留底仓抗洗盘，节省一半手续费） vs 灾难性深跌（亏损 > max(5.0, ATR*1.0) 时全额清仓止损）
        double sellG = batchGrams;
        const double curAtr = ExtremeDatabase::instance().computeAtr(10, src);
        const double effAtr = curAtr > 0.1 ? curAtr : 4.0;
        const double catastrophicStopPrice = posCost - qMax(5.0, effAtr * 1.0);
        const bool isCatastrophic = (posCost > 0.0 && price <= catastrophicStopPrice);

        if (tag.contains(QStringLiteral("移动止盈"))) {
            // 移动止盈锁定正收益：全仓获利了结
            sellG = currentGrams;
        } else if (isCatastrophic) {
            // 灾难性深跌：全仓硬止损，阻断深套破产风险
            sellG = currentGrams;
            Logger::warn(QStringLiteral("AutoTrade triggered CATASTROPHIC stop-loss: price (%1) <= catastrophicStopPrice (%2), sell all %3g")
                             .arg(price).arg(catastrophicStopPrice).arg(currentGrams));
        } else {
            // 常规防守减仓：梯级减仓 50%（或至少 1 个 batchGrams，至多留下一半底仓）
            // 既有效降低风险敞口、减少一半手续费，又避免因假摔或下影线插针被“一次性全额洗劫在最低点”
            sellG = qMin(currentGrams, qMax(batchGrams, qRound(currentGrams * 0.5 * 100.0) / 100.0));
            Logger::info(QStringLiteral("AutoTrade triggered GRADUATED defensive reduction: sell %1g out of %2g (50% risk-reduction)")
                             .arg(sellG).arg(currentGrams));
        }

        executeSimulatedTrade(QStringLiteral("SELL"), sellG,
                              tr("决策算法【%1】触发自动模拟卖出").arg(tag));
    }
}
