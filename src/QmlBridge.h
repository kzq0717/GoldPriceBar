#ifndef QMLBRIDGE_H
#define QMLBRIDGE_H

#include <QObject>
#include <QVariantList>
#include <QVariantMap>
#include <QString>
#include <QDateTime>
#include <QSystemTrayIcon>
#include <QPointer>

class PriceService;
class ChartWindow;
class SettingsDialog;
class QQuickWindow;
class QNetworkAccessManager;
class QNetworkReply;

class QmlBridge : public QObject {
    Q_OBJECT

    // Real-time Price properties
    Q_PROPERTY(double price READ price NOTIFY priceChanged)
    Q_PROPERTY(double priceChange READ priceChange NOTIFY priceChanged)
    Q_PROPERTY(double changePct READ changePct NOTIFY priceChanged)
    Q_PROPERTY(double highPrice READ highPrice NOTIFY priceChanged)
    Q_PROPERTY(double lowPrice READ lowPrice NOTIFY priceChanged)
    Q_PROPERTY(QString sourceName READ sourceName NOTIFY priceChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY priceChanged)
    Q_PROPERTY(int statusLevel READ statusLevel NOTIFY priceChanged) // 0=normal, 1=updating, 2=error
    Q_PROPERTY(bool hasValidPrice READ hasValidPrice NOTIFY priceChanged)
    Q_PROPERTY(QString updateTime READ updateTime NOTIFY priceChanged)

    // Gold Sentiment properties
    Q_PROPERTY(QVariantList sentimentItems READ sentimentItems NOTIFY sentimentChanged)
    Q_PROPERTY(bool sentimentLoading READ sentimentLoading NOTIFY sentimentChanged)
    Q_PROPERTY(bool sentimentNetworkError READ sentimentNetworkError NOTIFY sentimentChanged)
    Q_PROPERTY(QString sentimentError READ sentimentError NOTIFY sentimentChanged)
    Q_PROPERTY(int sentimentBullCount READ sentimentBullCount NOTIFY sentimentChanged)
    Q_PROPERTY(int sentimentBearCount READ sentimentBearCount NOTIFY sentimentChanged)
    Q_PROPERTY(int sentimentNeutralCount READ sentimentNeutralCount NOTIFY sentimentChanged)
    Q_PROPERTY(double sentimentBullRatio READ sentimentBullRatio NOTIFY sentimentChanged)
    Q_PROPERTY(bool sentimentEnabled READ sentimentEnabled WRITE setSentimentEnabled NOTIFY settingsChanged)

    // Settings properties
    Q_PROPERTY(double windowOpacity READ windowOpacity WRITE setWindowOpacity NOTIFY settingsChanged)
    Q_PROPERTY(int refreshIntervalSec READ refreshIntervalSec WRITE setRefreshIntervalSec NOTIFY settingsChanged)
    Q_PROPERTY(QString dataSource READ dataSource WRITE setDataSource NOTIFY settingsChanged)
    Q_PROPERTY(bool proxyEnabled READ proxyEnabled WRITE setProxyEnabled NOTIFY settingsChanged)
    Q_PROPERTY(QString proxyHost READ proxyHost WRITE setProxyHost NOTIFY settingsChanged)
    Q_PROPERTY(int proxyPort READ proxyPort WRITE setProxyPort NOTIFY settingsChanged)
    Q_PROPERTY(bool showSecondaryPrice READ showSecondaryPrice NOTIFY secondaryPriceChanged)
    Q_PROPERTY(QString secondaryPriceText READ secondaryPriceText NOTIFY secondaryPriceChanged)
    Q_PROPERTY(double secondaryPrice READ secondaryPrice NOTIFY secondaryPriceChanged)
    Q_PROPERTY(int alertKind READ alertKind NOTIFY alertStateChanged)
    Q_PROPERTY(bool alertArmed READ alertArmed NOTIFY alertStateChanged)
    Q_PROPERTY(QString alertTooltip READ alertTooltip NOTIFY alertStateChanged)

    // Position PnL
    Q_PROPERTY(bool hasPosition READ hasPosition NOTIFY pnlChanged)
    Q_PROPERTY(QString pnlText READ pnlText NOTIFY pnlChanged)
    Q_PROPERTY(QString pnlColor READ pnlColor NOTIFY pnlChanged)

    // Trend & Decision Engine
    Q_PROPERTY(QString decisionText READ decisionText NOTIFY decisionChanged)
    Q_PROPERTY(QString decisionColor READ decisionColor NOTIFY decisionChanged)
    Q_PROPERTY(QString decisionBg READ decisionBg NOTIFY decisionChanged)
    Q_PROPERTY(QString decisionTooltip READ decisionTooltip NOTIFY decisionChanged)
    Q_PROPERTY(int decisionLevel READ decisionLevel NOTIFY decisionChanged)
    Q_PROPERTY(QString decisionAdvice READ decisionAdvice NOTIFY decisionChanged)
    Q_PROPERTY(QString decisionRangeText READ decisionRangeText NOTIFY decisionChanged)
    Q_PROPERTY(QString decisionMetricsText READ decisionMetricsText NOTIFY decisionChanged)
    Q_PROPERTY(QString decisionSessionText READ decisionSessionText NOTIFY decisionChanged)
    Q_PROPERTY(QString predHighTimeWindow READ predHighTimeWindow NOTIFY decisionChanged)
    Q_PROPERTY(QString predLowTimeWindow READ predLowTimeWindow NOTIFY decisionChanged)
    Q_PROPERTY(QString predCatalyst READ predCatalyst NOTIFY decisionChanged)
    Q_PROPERTY(QString predDailyPath READ predDailyPath NOTIFY decisionChanged)
    Q_PROPERTY(QString peakProbText READ peakProbText NOTIFY decisionChanged)
    Q_PROPERTY(QString highInProbText READ highInProbText NOTIFY decisionChanged)
    Q_PROPERTY(QString remainingUpsideText READ remainingUpsideText NOTIFY decisionChanged)
    Q_PROPERTY(QString multiDayBiasText READ multiDayBiasText NOTIFY decisionChanged)
    Q_PROPERTY(QString forecastConfidenceText READ forecastConfidenceText NOTIFY decisionChanged)
    Q_PROPERTY(QString decisionTimeWindowText READ decisionTimeWindowText NOTIFY decisionChanged)
    Q_PROPERTY(bool autoSimulateTrading READ autoSimulateTrading WRITE setAutoSimulateTrading NOTIFY autoSimulateTradingChanged)
    Q_PROPERTY(double simulateTradeBatchGrams READ simulateTradeBatchGrams WRITE setSimulateTradeBatchGrams NOTIFY simulateTradeBatchGramsChanged)
    Q_PROPERTY(double totalRealizedPnl READ totalRealizedPnl NOTIFY simulatedTradesUpdated)
    Q_PROPERTY(double positionGrams READ positionGrams NOTIFY positionChanged)
    Q_PROPERTY(double positionCost READ positionCost NOTIFY positionChanged)
    Q_PROPERTY(QString positionSummaryText READ positionSummaryText NOTIFY positionChanged)

public:
    explicit QmlBridge(PriceService *priceService, QObject *parent = nullptr);
    ~QmlBridge() override;

    void setupTray();
    void setPriceBarWindow(QQuickWindow *w) { m_priceBarWindow = w; }
    void setSentimentWindow(QQuickWindow *w) { m_sentimentWindow = w; }
    void setSettingsWindow(QQuickWindow *w) { m_settingsWindow = w; }
    void setAgentWindow(QQuickWindow *w) { m_agentWindow = w; }
    void setAgentCardWindow(QQuickWindow *w) { m_agentCardWindow = w; }
    void setAgentChatWindow(QQuickWindow *w) { m_agentChatWindow = w; }
    void setSimulatedTradesWindow(QQuickWindow *w) { m_simulatedTradesWindow = w; }

    // Getters
    double price() const { return m_price; }
    double priceChange() const { return m_priceChange; }
    double changePct() const { return m_changePct; }
    double highPrice() const { return m_highPrice; }
    double lowPrice() const { return m_lowPrice; }
    QString sourceName() const { return m_sourceName; }
    QString statusText() const { return m_statusText; }
    int statusLevel() const { return m_statusLevel; }
    bool hasValidPrice() const { return m_hasValidPrice; }
    QString updateTime() const { return m_updateTime; }

    bool showSecondaryPrice() const;
    QString secondaryPriceText() const { return m_secondaryPriceText; }
    double secondaryPrice() const { return m_secondaryPrice; }
    int alertKind() const { return m_alertKind; }
    bool alertArmed() const { return m_alertArmed; }
    QString alertTooltip() const { return m_alertTooltip; }

    bool hasPosition() const { return m_hasPosition; }
    QString pnlText() const { return m_pnlText; }
    QString pnlColor() const { return m_pnlColor; }

    QString decisionText() const { return m_decisionText; }
    QString decisionColor() const { return m_decisionColor; }
    QString decisionBg() const { return m_decisionBg; }
    QString decisionTooltip() const { return m_decisionTooltip; }
    int decisionLevel() const { return m_decisionLevel; }
    QString decisionAdvice() const { return m_decisionAdvice; }
    QString decisionRangeText() const { return m_decisionRangeText; }
    QString decisionMetricsText() const { return m_decisionMetricsText; }
    QString decisionSessionText() const { return m_decisionSessionText; }
    QString predHighTimeWindow() const { return m_predHighTimeWindow; }
    QString predLowTimeWindow() const { return m_predLowTimeWindow; }
    QString predCatalyst() const { return m_predCatalyst; }
    QString predDailyPath() const { return m_predDailyPath; }
    QString peakProbText() const { return m_peakProbText; }
    QString highInProbText() const { return m_highInProbText; }
    QString remainingUpsideText() const { return m_remainingUpsideText; }
    QString multiDayBiasText() const { return m_multiDayBiasText; }
    QString forecastConfidenceText() const { return m_forecastConfidenceText; }
    QString decisionTimeWindowText() const { return m_decisionTimeWindowText; }

    QVariantList sentimentItems() const { return m_sentimentItems; }
    bool sentimentLoading() const { return m_sentimentLoading; }
    bool sentimentNetworkError() const { return m_sentimentNetworkError; }
    QString sentimentError() const { return m_sentimentError; }
    int sentimentBullCount() const { return m_sentimentBullCount; }
    int sentimentBearCount() const { return m_sentimentBearCount; }
    int sentimentNeutralCount() const { return m_sentimentNeutralCount; }
    double sentimentBullRatio() const { return m_sentimentBullRatio; }
    bool sentimentEnabled() const;

    double windowOpacity() const;
    void setWindowOpacity(double op);

    int refreshIntervalSec() const;
    void setRefreshIntervalSec(int sec);

    QString dataSource() const;
    void setDataSource(const QString &src);

    bool proxyEnabled() const;
    void setProxyEnabled(bool en);

    QString proxyHost() const;
    void setProxyHost(const QString &host);

    int proxyPort() const;
    void setProxyPort(int port);

public slots:
    void setSentimentEnabled(bool on);
    void refreshPrice();
    void refreshSentiment();
    void openSentimentWindow();
    void closeSentimentWindow();
    void toggleSentimentWindow();
    void openSettingsWindow();
    void closeSettingsWindow();
    void toggleSettingsWindow();
    void openChartWindow();
    void closeChartWindow();
    void toggleChartWindow();
    void openUrl(const QString &url);
    void quitApp();
    void saveSettings(const QString &dataSource, int refreshIntervalSec, double opacity,
                      bool proxyEnabled, const QString &proxyHost, int proxyPort,
                      bool sentimentEnabled);

    Q_INVOKABLE QVariantMap getAllSettings() const;
    Q_INVOKABLE void saveAllSettings(const QVariantMap &settings);
    Q_INVOKABLE QVariantMap get10DayAmplitude();
    Q_INVOKABLE QString getEventCalendarSummary() const;
    Q_INVOKABLE QString selectDatabaseDir();
    Q_INVOKABLE void openLogDir();
    Q_INVOKABLE void checkUpdate();
    Q_INVOKABLE void fetchModelList(const QString &provider, const QString &apiKey);
    Q_INVOKABLE void testAiConnection(const QString &provider, const QString &apiKey, const QString &model, const QString &customEndpoint = QString());
    Q_INVOKABLE void showDailyReportDialog();
    Q_INVOKABLE void markDcaExecutedToday();

    // 自动化 Agent 相关接口
    Q_INVOKABLE void openAgentWindow();
    Q_INVOKABLE void closeAgentWindow();
    Q_INVOKABLE void toggleAgentWindow();
    Q_INVOKABLE void openAgentCardWindow();
    Q_INVOKABLE void closeAgentCardWindow();
    Q_INVOKABLE QVariantList getAgentTasks() const;
    Q_INVOKABLE bool addOrUpdateAgentTask(const QVariantMap &taskData);
    Q_INVOKABLE bool deleteAgentTask(const QString &taskId);
    Q_INVOKABLE bool toggleAgentTask(const QString &taskId, bool enabled);
    Q_INVOKABLE void runAgentTaskNow(const QString &taskId);
    Q_INVOKABLE void advanceCurriculumDay(const QString &taskId);
    Q_INVOKABLE void setCurriculumDay(const QString &taskId, int day);
    Q_INVOKABLE QVariantMap getCurriculumTopic(int day) const;
    Q_INVOKABLE QVariantMap getCurriculumTopicForAgent(const QString &agentId, int day) const;
    Q_INVOKABLE int getCurriculumTotalDaysForAgent(const QString &agentId) const;
    Q_INVOKABLE QString getCurriculumWechatSummaryMarkdown() const;
    Q_INVOKABLE QString getCurriculumWechatSummaryMarkdownForAgent(const QString &agentId) const;
    Q_INVOKABLE QString getCurriculumDayWechatMarkdown(int day) const;
    Q_INVOKABLE QString getCurriculumDayWechatMarkdownForAgent(const QString &agentId, int day) const;
    Q_INVOKABLE void copyToClipboard(const QString &text);
    Q_INVOKABLE QString getAgentDatabasePath() const;
    Q_INVOKABLE void openLearningNotesDir();
    Q_INVOKABLE QStringList getNoteFiles() const;
    Q_INVOKABLE QString getNoteContent(const QString &fileName) const;
    // 内部笔记跳转与 AI 问答接口
    Q_INVOKABLE void openAgentWindowWithNote(const QString &fileName);
    Q_INVOKABLE void openAgentChatWindow(const QString &initialPrompt = QString(), const QString &contextTopic = QString());
    Q_INVOKABLE void closeAgentChatWindow();
    Q_INVOKABLE void askAiQuestion(const QString &question,
                                   const QString &contextTopic = QString(),
                                   const QString &provider = QString(),
                                   const QString &model = QString(),
                                   const QString &apiKey = QString(),
                                   const QString &customEndpoint = QString());
    Q_INVOKABLE void cancelAiQuestion();
    Q_INVOKABLE bool isAiBusy() const;
    Q_INVOKABLE QString defaultAiProvider() const;
    Q_INVOKABLE QString defaultAiApiKey() const;
    Q_INVOKABLE QString defaultAiModel() const;
    Q_INVOKABLE void saveAiSettings(const QString &provider, const QString &apiKey, const QString &model);

    bool autoSimulateTrading() const;
    void setAutoSimulateTrading(bool on);
    double simulateTradeBatchGrams() const;
    void setSimulateTradeBatchGrams(double g);
    double totalRealizedPnl() const;
    double positionGrams() const;
    double positionCost() const;
    QString positionSummaryText() const;

    Q_INVOKABLE bool executeSimulatedTrade(const QString &action, double grams = 0.0, const QString &customNote = QString());
    Q_INVOKABLE QVariantList getSimulatedTrades(int limit = 100) const;
    Q_INVOKABLE double getTotalRealizedPnl() const;
    Q_INVOKABLE void clearSimulatedTrades();
    Q_INVOKABLE void openSimulatedTradesWindow();
    Q_INVOKABLE void closeSimulatedTradesWindow();
    Q_INVOKABLE void toggleSimulatedTradesWindow();


signals:
    void priceChanged();
    void sentimentChanged();
    void settingsChanged();
    void secondaryPriceChanged();
    void alertStateChanged();
    void pnlChanged();
    void decisionChanged();
    void requestShowSentiment();
    void requestCloseSentiment();
    void requestShowSettings();
    void requestCloseSettings();
    void requestShowPriceBar();
    void requestTogglePriceBar();
    void requestShowAgentWindow();
    void requestCloseAgentWindow();
    void requestCloseSimulatedTrades();
    void agentNotificationTriggered(const QVariantMap &notificationData);
    void agentTasksUpdated();
    void modelsLoaded(const QVariantList &models);
    void modelsLoadFailed(const QString &error);
    void aiConnectionTested(bool success, const QString &message, int latencyMs);
    void requestOpenNoteInAgentWindow(const QString &fileName);
    void requestOpenAgentChatWindow(const QString &initialPrompt, const QString &contextTopic);
    void aiAnswerStarted(const QString &question);
    void aiAnswerReceived(const QString &question, const QString &answer, const QString &provider, const QString &model);
    void aiAnswerFailed(const QString &question, const QString &error);
    void autoSimulateTradingChanged();
    void simulateTradeBatchGramsChanged();
    void simulatedTradesUpdated();
    void simulatedTradeExecuted(const QString &action, double price, double grams, double pnl, const QString &decisionTag);
    void positionChanged();
    void requestShowSimulatedTrades();


private slots:
    void onPriceUpdated(double price, double change, const QString &sourceName);
    void onFetchFailed(const QString &error);
    void onExtremesUpdated();
    void onSentimentUpdated();
    void onSentimentFailed(const QString &reason);
    void onTrayActivated(QSystemTrayIcon::ActivationReason reason);
    void onSettingsDialogAccepted();
    void onSecondaryTimer();
    void onSecondaryFinished(QNetworkReply *reply, const QString &sec = QString());
    void onForecastUpdated(const struct ForecastResult &res);

private:
    void updateExtremes();
    void updateAlertIndicator(double price);
    void maybeTrayNotify(int kind, double price);
    void checkPlanAlerts(double price);
    void evaluateSmartAlerts(double price);
    void evaluatePremium(double primaryPrice);
    void evaluateDecision(double price);
    void updatePnL(double price);
    void checkAutoSimulateTrading(double price, int level, const QString &tag);
    void checkDcaReminder();
    void showDailyReport(bool force = false);
    void checkDailyReport(bool force = false);
    void checkEventAlerts();
    double computeMa5() const;
    double computePercentile(double price) const;
    QString buildDailyReportText() const;
    void ensureSecondaryNam();

    PriceService *m_priceService = nullptr;
    ChartWindow *m_chartWindow = nullptr;
    SettingsDialog *m_settingsDialog = nullptr;
    QSystemTrayIcon *m_tray = nullptr;
    QQuickWindow *m_priceBarWindow = nullptr;
    QQuickWindow *m_sentimentWindow = nullptr;
    QQuickWindow *m_settingsWindow = nullptr;
    QQuickWindow *m_agentWindow = nullptr;
    QQuickWindow *m_agentCardWindow = nullptr;
    QQuickWindow *m_agentChatWindow = nullptr;
    QQuickWindow *m_simulatedTradesWindow = nullptr;
    qint64 m_lastAutoTradeMs = 0;
    double m_lastAutoTradePrice = 0.0;
    QString m_lastAutoTradeDecisionTag;
    QString m_lastAutoTradeAction;
    qint64 m_lastSellAdviceMs = 0;
    int m_consecutiveBuyTicks = 0;
    QNetworkAccessManager *m_modelsNam = nullptr;
    QPointer<QNetworkReply> m_modelsReply;

    double m_price = 0.0;
    double m_priceChange = 0.0;
    double m_changePct = 0.0;
    double m_highPrice = 0.0;
    double m_lowPrice = 0.0;
    QString m_sourceName;
    QString m_statusText;
    int m_statusLevel = 0; // 0=ok, 1=updating, 2=error
    bool m_hasValidPrice = false;
    QString m_updateTime;

    QVariantList m_sentimentItems;
    bool m_sentimentLoading = false;
    bool m_sentimentNetworkError = false;
    QString m_sentimentError;
    int m_sentimentBullCount = 0;
    int m_sentimentBearCount = 0;
    int m_sentimentNeutralCount = 0;
    double m_sentimentBullRatio = 0.5;

    // 对照行情
    QTimer *m_secondaryTimer = nullptr;
    QNetworkAccessManager *m_secondaryNam = nullptr;
    QPointer<QNetworkReply> m_secondaryReply;
    qint64 m_secondaryRequestStartMs = 0;
    double m_secondaryPrice = 0.0;
    QString m_secondaryPriceText;

    // 价格预警指示与定时巡检
    int m_alertKind = 0; // 0=None, 1=High, 2=Low
    bool m_alertArmed = false;
    QString m_alertTooltip;
    QDateTime m_lastHighNotify;
    QDateTime m_lastLowNotify;
    QDateTime m_lastPlanNotify;
    QDateTime m_lastSmartNotify;
    QDateTime m_lastPremiumNotify;
    QDateTime m_lastDecisionNotify;
    QTimer *m_checkTimer = nullptr;
    QVector<double> m_premiumRatios;
    double m_lastSecondaryPrice = 0.0;

    // 持仓盈亏
    bool m_hasPosition = false;
    QString m_pnlText;
    QString m_pnlColor = QStringLiteral("#2ecc71");

    // 决策引擎
    QString m_decisionText;
    QString m_decisionColor = QStringLiteral("#D0D0D0");
    QString m_decisionBg = QStringLiteral("#252530");
    QString m_decisionTooltip;
    int m_decisionLevel = 0;
    QString m_decisionAdvice;
    QString m_decisionRangeText;
    QString m_decisionMetricsText;
    QString m_decisionSessionText;
    QString m_predHighTimeWindow;
    QString m_predLowTimeWindow;
    QString m_predCatalyst;
    QString m_predDailyPath;
    QString m_peakProbText;
    QString m_highInProbText;
    QString m_remainingUpsideText;
    QString m_multiDayBiasText;
    QString m_forecastConfidenceText;
    QString m_decisionTimeWindowText;
    double m_predHigh = 0.0;
    double m_predLow = 0.0;
};

#endif // QMLBRIDGE_H
