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
QT_END_NAMESPACE

/**
 * @brief 分时曲线窗口
 * 右侧固定信息栏显示：当前价、预测值、高、低
 * 曲线：当前点浅红、预测虚线、高低散点
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

private:
    void setupChart();
    void setupSidePanel(QWidget* parentLayoutHost);
    void updateSeries();
    void fetchChartFromApi();
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

    QChartView* m_chartView = nullptr;
    QChart* m_chart = nullptr;
    QLineSeries* m_series = nullptr;
    QLineSeries* m_forecastSeries = nullptr;
    QScatterSeries* m_currentSeries = nullptr;
    QScatterSeries* m_highSeries = nullptr;
    QScatterSeries* m_lowSeries = nullptr;
    QDateTimeAxis* m_axisX = nullptr;
    QValueAxis* m_axisY = nullptr;
    QElapsedTimer m_lastRedraw;

    QLabel* m_tipLabel = nullptr;

    // 右侧固定信息栏（可靠显示，不依赖 mapToPosition）
    QFrame* m_sidePanel = nullptr;
    QLabel* m_sideCurrentLabel = nullptr;
    QLabel* m_sidePredictLabel = nullptr;
    QLabel* m_sideHighLabel = nullptr;
    QLabel* m_sideLowLabel = nullptr;
    QLabel* m_sideModeLabel = nullptr;

    QNetworkAccessManager* m_network = nullptr;
    QPointer<QNetworkReply> m_pendingChart;
    QPointer<QNetworkReply> m_pendingForecast;
    bool m_loading = false;
    QString m_forecastModeTag;

    QVector<QPair<QDateTime, double>> m_plotPoints;
    double m_lastPredictPrice = 0.0;
    bool m_hasPredict = false;
    qint64 m_lastForecastMs = 0;  // 限制预测刷新频率，避免侧栏乱跳
    static constexpr int kForecastIntervalMs = 30000; // 至少 30 秒重算一次
};



#endif // CHARTWINDOW_H
