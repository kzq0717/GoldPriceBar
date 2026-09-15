#ifndef CHARTWINDOW_H
#define CHARTWINDOW_H

#include <QWidget>
#include <QElapsedTimer>
#include <QNetworkAccessManager>
#include <QPointer>
#include <QVector>
#include <QPair>
#include <QDateTime>

QT_BEGIN_NAMESPACE
class QChartView;
class QLineSeries;
class QScatterSeries;
class QChart;
class QDateTimeAxis;
class QValueAxis;
class QNetworkReply;
class QLabel;
class QFrame;
class QComboBox;
class QDateEdit;
class QListWidget;
class QToolButton;
class QPushButton;
class QMenu;
class QAction;
QT_END_NAMESPACE

/**
 * 分时 / 月份曲线：
 * - 默认「今日分时」
 * - 可选 7 月～当前月：显示本地日线收盘走势（依赖运行期累积的 daily_bars）
 */
class ChartWindow : public QWidget
{
    Q_OBJECT

public:
    explicit ChartWindow(QWidget* parent = nullptr);

    void refreshData();

public slots:
    void onNewPrice(double price, double change, const QString& sourceName);

protected:
    void showEvent(QShowEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void closeEvent(QCloseEvent* event) override;

private slots:
    void onChartReplyFinished(QNetworkReply* reply);
    void onPeriodChanged(int index);
    void onExportCsv();
    void onMaOptionChanged();

private:
    void setupChart();
    void updateSeries();
    void updateMonthSeries();
    void fetchChartFromApi();
    void fillPeriodCombo();
    void applyPeriodSelection();
    void loadIntradayForDate(const QDate& day);
    void loadDailyRange(const QDate& from, const QDate& to);

    bool isIntradayMode() const;
    QString currentTypeCode() const;
    void updateCrosshair(const QPoint& viewPos);
    void hideCrosshair();
    void updateHighLowMarkers();
    void updateForecast();
    void applyLocalForecastLines(double predHigh, double predLow);
    void applyForecastPoints(const QVector<QPair<QDateTime, double>>& forecast, const QString& modeTag);
    void requestOnlineForecast();
    void onOnlineForecastFinished();
    void refreshIntradayTitle();
    void reloadForecastHistory();
    void appendForecastHistoryItem(const QDateTime& when, const QString& mode, const QString& brief, double ph, double pl);
    void applySidePanelChrome();
    void updateSidePanelValues(double current, double predict, bool hasPredict,
                               double high, double low, const QString& modeTag);
    int nearestPointIndex(qreal xMsecs) const;
    /** 预测当日最高/最低（基于已现高低 + 波动 + 剩余时段） */
    bool computeDayRangeForecast(double& outPredHigh, double& outPredLow) const;
    void setForecastVisible(bool on);
    void applyChartTheme();
    void updateMovingAverages();
    void updateClockAndAdvice();
    QString buildAdviceText(double price) const;
    void updateYesterdayOverlay();
    void onSmoothTick();
    void onPulseTick();
    void setCurrentMarker(qint64 xMs, double y, bool startPulse = true);

    QComboBox* m_periodCombo = nullptr;
    QDateEdit* m_dateFromEdit = nullptr;
    QDateEdit* m_dateToEdit = nullptr;
    QPushButton* m_queryPeriodBtn = nullptr;

    QFrame* m_toolbar = nullptr;
    QToolButton* m_maMenuBtn = nullptr;
    QToolButton* m_ma5Btn = nullptr;
    QToolButton* m_ma10Btn = nullptr;
    QToolButton* m_ma20Btn = nullptr;
    QPushButton* m_exportCsvBtn = nullptr;
    QAction* m_ma5Action = nullptr;
    QAction* m_ma10Action = nullptr;
    QAction* m_ma20Action = nullptr;

    QChartView* m_chartView = nullptr;
    QChart* m_chart = nullptr;
    QLineSeries* m_series = nullptr;
    QLineSeries* m_ma5Series = nullptr;
    QLineSeries* m_ma10Series = nullptr;
    QLineSeries* m_ma20Series = nullptr;
    QLineSeries* m_yesterdaySeries = nullptr;
    QLineSeries* m_forecastSeries = nullptr;      // 预测最高（水平虚线）
    QLineSeries* m_forecastLowSeries = nullptr;   // 预测最低（水平虚线）
    QScatterSeries* m_currentSeries = nullptr;
    QScatterSeries* m_highSeries = nullptr;
    QScatterSeries* m_lowSeries = nullptr;
    QDateTimeAxis* m_axisX = nullptr;
    QValueAxis* m_axisY = nullptr;
    QElapsedTimer m_lastRedraw;

    QLabel* m_tipLabel = nullptr;

    QFrame* m_sidePanel = nullptr;
    QLabel* m_sideCurrentLabel = nullptr;
    QLabel* m_sidePredictLabel = nullptr;
    QLabel* m_sidePredictHighLabel = nullptr;
    QLabel* m_sidePredictLowLabel = nullptr;
    QLabel* m_sideHighLabel = nullptr;
    QLabel* m_sideLowLabel = nullptr;
    QLabel* m_sideModeLabel = nullptr;
    QListWidget* m_sideForecastList = nullptr;
    QLabel* m_sideTrendLabel = nullptr;
    QLabel* m_sideHitRateLabel = nullptr;
    QLabel* m_sideClockLabel = nullptr;
    QLabel* m_sideSessionLabel = nullptr;
    QLabel* m_sideAdviceLabel = nullptr;
    QTimer* m_clockTimer = nullptr;

    QNetworkAccessManager* m_network = nullptr;
    QPointer<QNetworkReply> m_pendingChart;
    QPointer<QNetworkReply> m_pendingForecast;
    bool m_loading = false;
    QString m_forecastModeTag;

    QVector<QPair<QDateTime, double>> m_plotPoints;
    double m_lastPredictPrice = 0.0; // 兼容侧栏：展示用（预测高）
    double m_lastPredictHigh = 0.0;
    double m_lastPredictLow = 0.0;
    bool m_hasPredict = false;
    qint64 m_lastForecastMs = 0;
    QTimer* m_smoothTimer = nullptr;
    QTimer* m_pulseTimer = nullptr;
    qint64 m_markerXMs = 0;
    double m_smoothY = 0.0;
    double m_targetY = 0.0;
    bool m_hasMarker = false;
    bool m_pulseOn = false;
    int m_seriesLastIndex = -1;
};

#endif // CHARTWINDOW_H
