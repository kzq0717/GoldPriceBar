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

private:
    void setupChart();
    void updateSeries();
    void updateMonthSeries();
    void fetchChartFromApi();
    void fillPeriodCombo();
    bool isIntradayMode() const;
    QString currentTypeCode() const;
    void updateCrosshair(const QPoint& viewPos);
    void hideCrosshair();
    void updateHighLowMarkers();
    void updateForecast();
    void applyForecastPoints(const QVector<QPair<QDateTime, double>>& forecast, const QString& modeTag);
    void requestOnlineForecast();
    void onOnlineForecastFinished(QNetworkReply* reply);
    void updateSidePanelValues(double current, double predict, bool hasPredict,
                               double high, double low, const QString& modeTag);
    int nearestPointIndex(qreal xMsecs) const;
    QVector<QPair<QDateTime, double>> computeForecastLocal(int horizonSec) const;
    void setForecastVisible(bool on);
    void applyChartTheme();
    void updateMovingAverages();
    void updateYesterdayOverlay();

    QComboBox* m_periodCombo = nullptr;

    QChartView* m_chartView = nullptr;
    QChart* m_chart = nullptr;
    QLineSeries* m_series = nullptr;
    QLineSeries* m_ma5Series = nullptr;
    QLineSeries* m_ma20Series = nullptr;
    QLineSeries* m_yesterdaySeries = nullptr;
    QLineSeries* m_forecastSeries = nullptr;
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
    QLabel* m_sideHighLabel = nullptr;
    QLabel* m_sideLowLabel = nullptr;
    QLabel* m_sideModeLabel = nullptr;
    QLabel* m_sideHitRateLabel = nullptr;

    QNetworkAccessManager* m_network = nullptr;
    QPointer<QNetworkReply> m_pendingChart;
    QPointer<QNetworkReply> m_pendingForecast;
    bool m_loading = false;
    QString m_forecastModeTag;

    QVector<QPair<QDateTime, double>> m_plotPoints;
    double m_lastPredictPrice = 0.0;
    bool m_hasPredict = false;
    qint64 m_lastForecastMs = 0;
    static constexpr int kForecastIntervalMs = 30000;
};

#endif // CHARTWINDOW_H
