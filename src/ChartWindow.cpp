#include "ChartWindow.h"
#include "AppSettings.h"
#include "ExtremeDatabase.h"
#include "Logger.h"
#include "ForecastTracker.h"
#include "HistoryCache.h"
#include "TradingSession.h"
#include "EventCalendar.h"


#include <QBrush>
#include <QCloseEvent>
#include <QDateTime>
#include <QDate>
#include <QFrame>
#include <QSizePolicy>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QComboBox>
#include <QFileDialog>
#include <QFile>
#include <QTextStream>
#include <QPushButton>
#include <QMessageBox>
#include <QStringConverter>
#include <QMouseEvent>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPainter>
#include <QPen>
#include <QResizeEvent>
#include <QShowEvent>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <QtCharts/QAbstractSeries>
#include <QtCharts/QChart>
#include <QtCharts/QChartView>
#include <QtCharts/QDateTimeAxis>
#include <QtCharts/QLineSeries>
#include <QtCharts/QScatterSeries>
#include <QtCharts/QValueAxis>
#include <QtMath>
#include <algorithm>


ChartWindow::ChartWindow(QWidget *parent) : QWidget(parent) {
  setWindowTitle(tr("今日分时曲线"));
  setMinimumSize(640, 360);
  resize(800, 440);
  setWindowFlags(Qt::Window);

  m_network = new QNetworkAccessManager(this);
  setupChart();
  connect(&AppSettings::instance(), &AppSettings::settingsChanged, this,
          [this]() {
            applyChartTheme();
            if (isVisible() && !m_plotPoints.isEmpty()) {
              updateMovingAverages();
  updateYesterdayOverlay();
              if (isIntradayMode())
                updateForecast();
            }
          });
  m_lastRedraw.invalidate();

  m_smoothTimer = new QTimer(this);
  m_smoothTimer->setInterval(33); // ~30fps 平滑插值
  connect(m_smoothTimer, &QTimer::timeout, this, &ChartWindow::onSmoothTick);

  m_clockTimer = new QTimer(this);
  m_clockTimer->setInterval(1000);
  connect(m_clockTimer, &QTimer::timeout, this, &ChartWindow::updateClockAndAdvice);
  m_clockTimer->start();
  QTimer::singleShot(0, this, &ChartWindow::updateClockAndAdvice);

  m_pulseTimer = new QTimer(this);
  m_pulseTimer->setInterval(450);
  connect(m_pulseTimer, &QTimer::timeout, this, &ChartWindow::onPulseTick);

}

QString ChartWindow::currentTypeCode() const {
  const QString source = AppSettings::instance().dataSource();
  if (source == QStringLiteral("ms"))
    return QStringLiteral("ms");
  if (source == QStringLiteral("gj") || source == QStringLiteral("xau"))
    return QStringLiteral("gj");
  return QStringLiteral("zs");
}

void ChartWindow::setupChart() {
  m_series = new QLineSeries(this);
  m_series->setName(tr("实际"));
  m_series->setPointsVisible(false);
  QPen pen(QColor(0, 82, 217));
  pen.setWidth(2);
  m_series->setPen(pen);

  m_ma5Series = new QLineSeries(this);
  m_ma5Series->setName(tr("MA5日"));
  m_ma5Series->setPointsVisible(false);
  QPen ma5(QColor(230, 126, 34));
  ma5.setWidth(2);
  m_ma5Series->setPen(ma5);

  m_ma20Series = new QLineSeries(this);
  m_ma20Series->setName(tr("MA20日"));
  m_ma20Series->setPointsVisible(false);
  QPen ma20(QColor(155, 89, 182));
  ma20.setWidth(2);
  m_ma20Series->setPen(ma20);

  m_yesterdaySeries = new QLineSeries(this);
  m_yesterdaySeries->setName(tr("昨日"));
  m_yesterdaySeries->setPointsVisible(false);
  QPen yPen(QColor(150, 150, 150));
  yPen.setWidth(1);
  yPen.setStyle(Qt::DotLine);
  m_yesterdaySeries->setPen(yPen);

  // 未来 2 分钟预测：虚线
  m_forecastSeries = new QLineSeries(this);
  m_forecastSeries->setName(tr("预测最高"));
  m_forecastSeries->setPointsVisible(false);
  QPen dashPen(QColor(255, 128, 128));
  dashPen.setWidth(2);
  dashPen.setStyle(Qt::DashLine);
  m_forecastSeries->setPen(dashPen);

  m_forecastLowSeries = new QLineSeries(this);
  m_forecastLowSeries->setName(tr("预测最低"));
  m_forecastLowSeries->setPointsVisible(false);
  QPen dashLow(QColor(46, 204, 113));
  dashLow.setStyle(Qt::DashLine);
  dashLow.setWidth(2);
  m_forecastLowSeries->setPen(dashLow);

  // 当前点：浅红色
  m_currentSeries = new QScatterSeries(this);
  m_currentSeries->setName(tr("当前"));
  m_currentSeries->setMarkerSize(7);
  m_currentSeries->setColor(QColor(255, 160, 160));
  m_currentSeries->setBorderColor(QColor(220, 80, 80));

  m_highSeries = new QScatterSeries(this);
  m_highSeries->setMarkerSize(6);
  m_highSeries->setColor(QColor(231, 76, 60));
  m_highSeries->setBorderColor(QColor(192, 57, 43));

  m_lowSeries = new QScatterSeries(this);
  m_lowSeries->setMarkerSize(6);
  m_lowSeries->setColor(QColor(39, 174, 96));
  m_lowSeries->setBorderColor(QColor(30, 132, 73));

  m_chart = new QChart();
  m_chart->addSeries(m_series);
  m_chart->addSeries(m_ma5Series);
  m_chart->addSeries(m_ma20Series);
  m_chart->addSeries(m_yesterdaySeries);
  m_chart->addSeries(m_forecastSeries);
  m_chart->addSeries(m_forecastLowSeries);
  m_chart->addSeries(m_currentSeries);
  m_chart->addSeries(m_highSeries);
  m_chart->addSeries(m_lowSeries);
  m_chart->setTitle(tr("今日分时走势"));
  m_chart->legend()->setVisible(true);
  m_chart->legend()->setAlignment(Qt::AlignBottom);
  m_chart->setAnimationOptions(QChart::NoAnimation);
  m_chart->setBackgroundBrush(QBrush(QColor(255, 255, 255)));
  m_chart->setPlotAreaBackgroundVisible(true);
  m_chart->setPlotAreaBackgroundBrush(QBrush(QColor(248, 249, 250)));

  m_axisX = new QDateTimeAxis(this);
  m_axisX->setFormat("HH:mm");
  m_axisX->setTitleText(tr("时间"));
  m_axisX->setLabelsColor(QColor(92, 107, 119));
  m_chart->addAxis(m_axisX, Qt::AlignBottom);

  m_axisY = new QValueAxis(this);
  m_axisY->setLabelFormat("%.2f");
  m_axisY->setTitleText(tr("价格"));
  m_axisY->setLabelsColor(QColor(92, 107, 119));
  m_chart->addAxis(m_axisY, Qt::AlignLeft);

  for (QAbstractSeries *s : {static_cast<QAbstractSeries *>(m_series),
                       static_cast<QAbstractSeries *>(m_ma5Series),
                       static_cast<QAbstractSeries *>(m_ma20Series),
                       static_cast<QAbstractSeries *>(m_yesterdaySeries),
                             static_cast<QAbstractSeries *>(m_forecastSeries),
                             static_cast<QAbstractSeries *>(m_forecastLowSeries),
                             static_cast<QAbstractSeries *>(m_currentSeries),
                             static_cast<QAbstractSeries *>(m_highSeries),
                             static_cast<QAbstractSeries *>(m_lowSeries)}) {
    s->attachAxis(m_axisX);
    s->attachAxis(m_axisY);
  }

  m_chartView = new QChartView(m_chart, this);
  m_chartView->setRenderHint(QPainter::Antialiasing);
  m_chartView->setMouseTracking(true);
  m_chartView->viewport()->setMouseTracking(true);
  m_chartView->viewport()->installEventFilter(this);
  m_chartView->installEventFilter(this);

  m_tipLabel = new QLabel(m_chartView);
  m_tipLabel->setStyleSheet(
      "QLabel{background-color:rgba(33,37,41,230);color:#fff;border:1px solid "
      "#666;"
      "border-radius:4px;padding:5px 9px;font-size:12px;}");
  m_tipLabel->hide();

  // 顶部：周期选择（今日分时 / 7月～当月）
  m_periodCombo = new QComboBox(this);
  m_periodCombo->setMinimumWidth(160);
  fillPeriodCombo();
  connect(m_periodCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
          this, &ChartWindow::onPeriodChanged);

  auto *exportBtn = new QPushButton(tr("导出CSV"), this);
  exportBtn->setFixedWidth(80);
  exportBtn->setToolTip(tr("导出当前曲线点到 CSV 文件"));
  connect(exportBtn, &QPushButton::clicked, this, &ChartWindow::onExportCsv);

  auto *topBar = new QHBoxLayout();
  topBar->addWidget(new QLabel(tr("周期："), this));
  topBar->addWidget(m_periodCombo);
  topBar->addWidget(exportBtn);
  topBar->addStretch();

  // 主布局：上工具栏 + 曲线 + 右侧信息栏
  auto *root = new QVBoxLayout(this);
  root->setContentsMargins(4, 4, 4, 4);
  root->setSpacing(4);
  root->addLayout(topBar);

  auto *body = new QHBoxLayout();
  body->setSpacing(8);
  body->setContentsMargins(0, 0, 0, 0);
  // 左右等高：子控件垂直方向默认拉伸至行高
  body->setAlignment(Qt::AlignVCenter);

  m_chartView->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  body->addWidget(m_chartView, 1);

  m_sidePanel = new QFrame(this);
  m_sidePanel->setFixedWidth(132);
  // 垂直扩展，高度与左侧 chartView 对齐
  m_sidePanel->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
  m_sidePanel->setMinimumHeight(0);
  m_sidePanel->setStyleSheet(
      "QFrame{"
      "  background: qlineargradient(x1:0,y1:0,x2:0,y2:1,"
      "    stop:0 #ffffff, stop:1 #f0f3f7);"
      "  border: 1px solid #d8dee6;"
      "  border-radius: 8px;"
      "}");
  auto *sideLay = new QVBoxLayout(m_sidePanel);
  sideLay->setContentsMargins(12, 16, 12, 16);
  sideLay->setSpacing(8);

  auto mkTitle = [](const QString &s) {
    auto *l = new QLabel(s);
    l->setStyleSheet("color:#5c6b77;font-size:11px;");
    return l;
  };
  auto mkValue = [](const QString &s, const QString &color) {
    auto *l = new QLabel(s);
    l->setStyleSheet(
        QStringLiteral("color:%1;font-size:16px;font-weight:bold;").arg(color));
    l->setWordWrap(true);
    return l;
  };

  m_sideClockLabel = new QLabel(QDateTime::currentDateTime().toString("HH:mm:ss"), m_sidePanel);
  m_sideClockLabel->setStyleSheet("color:#0052d9;font-size:16px;font-weight:bold;");
  m_sideClockLabel->setAlignment(Qt::AlignCenter);
  sideLay->addWidget(m_sideClockLabel);
  m_sideSessionLabel = new QLabel(tr("—"), m_sidePanel);
  m_sideSessionLabel->setStyleSheet("color:#888;font-size:11px;");
  m_sideSessionLabel->setWordWrap(true);
  m_sideSessionLabel->setAlignment(Qt::AlignCenter);
  sideLay->addWidget(m_sideSessionLabel);

  sideLay->addWidget(mkTitle(tr("当前价")));
  m_sideCurrentLabel = mkValue(tr("--.--"), "#212529");
  sideLay->addWidget(m_sideCurrentLabel);

  sideLay->addWidget(mkTitle(tr("预测高低")));
  m_sidePredictLabel = mkValue(tr("--.--"), "#e74c3c");
  sideLay->addWidget(m_sidePredictLabel);

  m_sideModeLabel = new QLabel(tr("本地"), m_sidePanel);
  m_sideModeLabel->setStyleSheet("color:#888;font-size:10px;");
  m_sideModeLabel->setWordWrap(true);
  sideLay->addWidget(m_sideModeLabel);

  // 命中率等次要信息不再占右侧栏
  m_sideHitRateLabel = nullptr;

  sideLay->addSpacing(6);
  sideLay->addWidget(mkTitle(tr("今高")));
  m_sideHighLabel = mkValue(tr("--.--"), "#e74c3c");
  sideLay->addWidget(m_sideHighLabel);

  sideLay->addWidget(mkTitle(tr("今低")));
  m_sideLowLabel = mkValue(tr("--.--"), "#27ae60");
  sideLay->addWidget(m_sideLowLabel);

  sideLay->addStretch(1);
  body->addWidget(m_sidePanel, 0);
  // 强制同一行内垂直方向填满
  body->setStretch(0, 1);
  body->setStretch(1, 0);
  root->addLayout(body, 1);
  applyChartTheme();
}


void ChartWindow::refreshData() {
  if (isIntradayMode())
    fetchChartFromApi();
  else
    updateMonthSeries();
}

void ChartWindow::onNewPrice(double price, double, const QString &) {
  if (!isVisible())
    return;
  if (!isIntradayMode())
    return;

  m_plotPoints = HistoryCache::instance().todayPoints();
  if (!m_plotPoints.isEmpty() && m_series) {
    const auto &cur = m_plotPoints.last();
    const qint64 x = cur.first.toMSecsSinceEpoch();
    const double y = (price > 0.0) ? price : cur.second;

    // 主曲线：新时间戳才追加，同秒只更新目标价（由平滑定时器插值）
    if (m_series->count() == 0) {
      m_series->append(x, y);
      m_seriesLastIndex = 0;
      m_smoothY = y;
    } else {
      const QPointF last = m_series->at(m_series->count() - 1);
      if (qAbs(last.x() - static_cast<qreal>(x)) < 1.0) {
        m_seriesLastIndex = m_series->count() - 1;
        // 不立刻 replace 到目标价，交给 onSmoothTick
      } else {
        // 先把上一段落到平滑终点，再追加新点（起点用当前平滑价，避免硬跳）
        if (m_seriesLastIndex >= 0 && m_seriesLastIndex < m_series->count()) {
          const QPointF p = m_series->at(m_seriesLastIndex);
          m_series->replace(m_seriesLastIndex,
                            QPointF(p.x(), m_hasMarker ? m_smoothY : p.y()));
        }
        m_series->append(x, m_hasMarker ? m_smoothY : y);
        m_seriesLastIndex = m_series->count() - 1;
      }
    }

    m_targetY = y;
    m_markerXMs = x;
    if (!m_hasMarker) {
      m_smoothY = y;
      m_hasMarker = true;
    }
    if (m_smoothTimer && !m_smoothTimer->isActive())
      m_smoothTimer->start();
    if (m_pulseTimer && !m_pulseTimer->isActive())
      m_pulseTimer->start();
    setCurrentMarker(x, m_smoothY, true);

    if (m_axisX && m_axisY) {
      const QDateTime xt = QDateTime::fromMSecsSinceEpoch(x);
      if (xt > m_axisX->max())
        m_axisX->setMax(xt.addSecs(3600));
      if (y > m_axisY->max())
        m_axisY->setMax(y + (y - m_axisY->min()) * 0.05 + 0.5);
      if (y < m_axisY->min())
        m_axisY->setMin(y - (m_axisY->max() - y) * 0.05 - 0.5);
    }
  }

  double high = 0.0, low = 0.0;
  HistoryCache::instance().todayHigh(high);
  HistoryCache::instance().todayLow(low);
  if (price > 0.0) {
    double ah = 0.0, al = 0.0;
    HistoryCache::instance().todayHigh(ah);
    HistoryCache::instance().todayLow(al);
    if (ah > 0.0 && al > 0.0)
      ForecastTracker::instance().evaluateDayRange(ah, al);
    else
      ForecastTracker::instance().evaluateWithActual(price, m_plotPoints);
  }

  updateSidePanelValues(
      price > 0 ? price
                : (m_plotPoints.isEmpty() ? 0.0 : m_plotPoints.last().second),
      m_lastPredictPrice, m_hasPredict, high, low, m_forecastModeTag);

  const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
  if (m_lastForecastMs == 0 ||
      (nowMs - m_lastForecastMs) >= kForecastIntervalMs) {
    if (AppSettings::instance().forecastOnline())
      requestOnlineForecast();
    else
      updateForecast();
  }
  updateClockAndAdvice();
}

void ChartWindow::setCurrentMarker(qint64 xMs, double y, bool startPulse)
{
  if (!m_currentSeries)
    return;
  m_currentSeries->clear();
  m_currentSeries->append(xMs, y);
  if (startPulse && m_pulseTimer && !m_pulseTimer->isActive())
    m_pulseTimer->start();
}

void ChartWindow::onSmoothTick()
{
  if (!isVisible() || !isIntradayMode() || !m_hasMarker)
    return;

  // 指数逼近目标价，变化大时更快、接近时更细腻
  const double diff = m_targetY - m_smoothY;
  if (qAbs(diff) < 1e-4) {
    m_smoothY = m_targetY;
  } else {
    // 每帧约 18%~28% 靠拢，约 0.3~0.6s 完成大部分过渡
    const double alpha = qBound(0.12, 0.12 + qAbs(diff) / qMax(1.0, qAbs(m_targetY)) * 40.0, 0.35);
    m_smoothY += diff * alpha;
  }

  if (m_series && m_seriesLastIndex >= 0 && m_seriesLastIndex < m_series->count()) {
    const QPointF p = m_series->at(m_seriesLastIndex);
    m_series->replace(m_seriesLastIndex, QPointF(p.x(), m_smoothY));
  }
  setCurrentMarker(m_markerXMs, m_smoothY, false);
}

void ChartWindow::onPulseTick()
{
  if (!m_currentSeries || !m_hasMarker || !isVisible() || !isIntradayMode())
    return;
  m_pulseOn = !m_pulseOn;
  // 等待下一价时末点呼吸闪烁
  m_currentSeries->setMarkerSize(m_pulseOn ? 11.0 : 7.0);
  if (m_pulseOn)
    m_currentSeries->setColor(QColor(255, 120, 120));
  else
    m_currentSeries->setColor(QColor(255, 180, 180));
}

void ChartWindow::showEvent(QShowEvent *event) {
  QWidget::showEvent(event);
  fetchChartFromApi();
}

bool ChartWindow::eventFilter(QObject *watched, QEvent *event) {
  if (watched == m_chartView || watched == m_chartView->viewport()) {
    if (event->type() == QEvent::MouseMove) {
      auto *me = static_cast<QMouseEvent *>(event);
      QPoint pos = me->pos();
      if (watched == m_chartView->viewport())
        pos = m_chartView->viewport()->mapTo(m_chartView, pos);
      updateCrosshair(pos);
    } else if (event->type() == QEvent::Leave) {
      hideCrosshair();
    }
  }
  return QWidget::eventFilter(watched, event);
}

int ChartWindow::nearestPointIndex(qreal xMsecs) const {
  if (m_plotPoints.isEmpty())
    return -1;
  int best = 0;
  qreal bestDist =
      qAbs(static_cast<qreal>(m_plotPoints.first().first.toMSecsSinceEpoch()) -
           xMsecs);
  for (int i = 1; i < m_plotPoints.size(); ++i) {
    const qreal d =
        qAbs(static_cast<qreal>(m_plotPoints.at(i).first.toMSecsSinceEpoch()) -
             xMsecs);
    if (d < bestDist) {
      bestDist = d;
      best = i;
    }
  }
  return best;
}

void ChartWindow::updateCrosshair(const QPoint &viewPos) {
  if (m_plotPoints.isEmpty() || !m_chart)
    return;

  const QPointF scenePt = m_chartView->mapToScene(viewPos);
  const QPointF itemPt = m_chart->mapFromScene(scenePt);
  if (!m_chart->plotArea().contains(itemPt)) {
    hideCrosshair();
    return;
  }

  const QPointF value = m_chart->mapToValue(itemPt, m_series);
  const int idx = nearestPointIndex(value.x());
  if (idx < 0) {
    hideCrosshair();
    return;
  }

  const QDateTime t = m_plotPoints.at(idx).first;
  const double price = m_plotPoints.at(idx).second;
  m_tipLabel->setText(QStringLiteral("%1\n%2")
                          .arg(t.toString(QStringLiteral("HH:mm:ss")))
                          .arg(price, 0, 'f', 2));
  m_tipLabel->adjustSize();

  int tx = viewPos.x() + 14;
  int ty = viewPos.y() + 14;
  if (tx + m_tipLabel->width() > m_chartView->width() - 4)
    tx = viewPos.x() - m_tipLabel->width() - 10;
  if (ty + m_tipLabel->height() > m_chartView->height() - 4)
    ty = viewPos.y() - m_tipLabel->height() - 10;
  m_tipLabel->move(qMax(2, tx), qMax(2, ty));
  m_tipLabel->show();
  m_tipLabel->raise();
}

void ChartWindow::hideCrosshair() {
  if (m_tipLabel)
    m_tipLabel->hide();
}

/**
 * 短时推演模型（非外部大模型）：
 * 1) 取最近约 15～30 个分时点
 * 2) 对 (时间, 价格) 做加权线性回归，得到斜率（元/秒）
 * 3) 叠加短窗动量衰减，生成未来 120 秒、步长 10 秒的路径
 * 仅供参考，不构成投资建议
 */
/**
 * 预测「当日最高 / 最低」：
 * - 不低于已出现的今高、不高于已出现的今低之外的合理扩张
 * - 扩张幅度由近窗波动、已走振幅、剩余时间共同决定
 * - 硬顶：全日预期振幅不超过现价约 1.2%（积存金尺度）
 */
bool ChartWindow::computeDayRangeForecast(double& outPredHigh, double& outPredLow) const
{
    outPredHigh = 0.0;
    outPredLow = 0.0;
    if (m_plotPoints.size() < 3)
        return false;

    double actHigh = 0.0, actLow = 0.0;
    HistoryCache::instance().todayHigh(actHigh);
    HistoryCache::instance().todayLow(actLow);
    const double lastPrice = m_plotPoints.last().second;
    if (lastPrice <= 0.0)
        return false;
    if (actHigh <= 0.0) actHigh = lastPrice;
    if (actLow <= 0.0) actLow = lastPrice;

    const int n = m_plotPoints.size();
    const int w = qMin(30, n);
    double mean = 0.0;
    for (int i = n - w; i < n; ++i)
        mean += m_plotPoints.at(i).second;
    mean /= static_cast<double>(w);
    double var = 0.0;
    for (int i = n - w; i < n; ++i) {
        const double d = m_plotPoints.at(i).second - mean;
        var += d * d;
    }
    const double stdev = qSqrt(var / static_cast<double>(qMax(1, w - 1)));
    const double rangeSoFar = qMax(0.01, actHigh - actLow);

    const QTime nowT = QTime::currentTime();
    const double dayFrac = nowT.msecsSinceStartOfDay() / (24.0 * 3600.0 * 1000.0);
    const double remain = qBound(0.08, 1.0 - dayFrac, 1.0);
    const double timeScale = qSqrt(remain);

    // 保守扩张：紧贴已实现区间与近窗波动，避免预测线大幅偏离分时
    double expand = qMax(1.0 * stdev, 0.12 * rangeSoFar);
    expand *= (0.35 + 0.40 * timeScale);
    const double hard = lastPrice * 0.0045;
    expand = qMin(expand, hard);
    expand = qMin(expand, rangeSoFar * 0.55 + stdev);
    expand = qMax(expand, lastPrice * 0.0003);

    outPredHigh = qMax(actHigh, lastPrice) + expand * 0.55;
    outPredLow = qMin(actLow, lastPrice) - expand * 0.55;
    outPredHigh = qMax(outPredHigh, actHigh);
    outPredLow = qMin(outPredLow, actLow);
    const double maxWidth = qMin(hard * 1.6, rangeSoFar * 1.35 + 2.0 * stdev);
    if (outPredHigh - outPredLow > maxWidth) {
        const double mid = lastPrice;
        outPredHigh = qMin(outPredHigh, mid + maxWidth * 0.55);
        outPredLow = qMax(outPredLow, mid - maxWidth * 0.55);
        outPredHigh = qMax(outPredHigh, actHigh);
        outPredLow = qMin(outPredLow, actLow);
    }
    return outPredHigh > outPredLow && outPredHigh > 0.0;
}

void ChartWindow::updateForecast() {
  if (!isIntradayMode() || m_plotPoints.isEmpty()) {
    m_hasPredict = false;
    return;
  }

  // 大模型模式：走在线请求（带 Key）；本地模式才清线重算
  if (AppSettings::instance().forecastOnline()
      && !AppSettings::instance().xaiApiKey().trimmed().isEmpty()) {
    requestOnlineForecast();
    return;
  }

  m_lastForecastMs = QDateTime::currentMSecsSinceEpoch();
  if (m_forecastSeries)
    m_forecastSeries->clear();
  if (m_forecastLowSeries)
    m_forecastLowSeries->clear();

  double predHigh = 0.0, predLow = 0.0;
  if (!computeDayRangeForecast(predHigh, predLow)) {
    m_hasPredict = false;
    return;
  }

  m_lastPredictHigh = predHigh;
  m_lastPredictLow = predLow;
  m_lastPredictPrice = predHigh;
  m_hasPredict = true;
  {
      double actH = 0, actL = 0;
      HistoryCache::instance().todayHigh(actH);
      HistoryCache::instance().todayLow(actL);
      m_forecastModeTag = tr("本地 · 振幅%1")
                              .arg(qMax(0.0, actH - actL), 0, 'f', 2);
      if (!AppSettings::instance().xaiApiKey().trimmed().isEmpty()
          && !AppSettings::instance().forecastOnline())
        m_forecastModeTag = tr("本地（请开启「大模型」开关）");
  }


  // 水平虚线：从当日 0 点到 23:59
  const QDateTime t0 = QDateTime(QDate::currentDate(), QTime(0, 0));
  const QDateTime t1 = QDateTime(QDate::currentDate(), QTime(23, 59, 59));
  const qint64 x0 = t0.toMSecsSinceEpoch();
  const qint64 x1 = t1.toMSecsSinceEpoch();
  if (m_forecastSeries) {
    m_forecastSeries->append(x0, predHigh);
    m_forecastSeries->append(x1, predHigh);
  }
  if (m_forecastLowSeries) {
    m_forecastLowSeries->append(x0, predLow);
    m_forecastLowSeries->append(x1, predLow);
  }

  // 命中统计：登记「预测高」与「预测低」的均值，到期用今高/今低检验
  ForecastTracker::instance().recordDayRange(
      QDateTime::currentDateTime(), 3600, predHigh, predLow, m_forecastModeTag);

  if (m_axisY) {
    qreal yMin = m_axisY->min();
    qreal yMax = m_axisY->max();
    yMin = qMin(yMin, predLow);
    yMax = qMax(yMax, predHigh);
    const qreal m = (yMax - yMin) * 0.05 + 0.2;
    m_axisY->setRange(yMin - m, yMax + m);
  }

  double high = 0.0, low = 0.0;
  HistoryCache::instance().todayHigh(high);
  HistoryCache::instance().todayLow(low);
  const double cur = m_plotPoints.last().second;
  updateSidePanelValues(cur, predHigh, true, high, low, m_forecastModeTag);
}

void ChartWindow::applyForecastPoints(
    const QVector<QPair<QDateTime, double>> &forecast, const QString &modeTag) {
  Q_UNUSED(forecast);
  Q_UNUSED(modeTag);
}

void ChartWindow::updateSidePanelValues(double current, double predict,
                                        bool hasPredict, double high,
                                        double low, const QString &modeTag) {
  if (m_sideCurrentLabel) {
    if (current > 0.0)
      m_sideCurrentLabel->setText(QString::number(current, 'f', 2));
    else
      m_sideCurrentLabel->setText(tr("--.--"));
  }
  if (m_sidePredictLabel) {
    if (hasPredict && m_lastPredictHigh > 0.0 && m_lastPredictLow > 0.0)
      m_sidePredictLabel->setText(
          tr("高 %1  低 %2")
              .arg(m_lastPredictHigh, 0, 'f', 2)
              .arg(m_lastPredictLow, 0, 'f', 2));
    else if (hasPredict && predict > 0.0)
      m_sidePredictLabel->setText(QString::number(predict, 'f', 2));
    else
      m_sidePredictLabel->setText(tr("--.--"));
  }
  if (m_sideHighLabel) {
    if (high > 0.0)
      m_sideHighLabel->setText(QString::number(high, 'f', 2));
    else
      m_sideHighLabel->setText(tr("--.--"));
  }
  if (m_sideLowLabel) {
    if (low > 0.0)
      m_sideLowLabel->setText(QString::number(low, 'f', 2));
    else
      m_sideLowLabel->setText(tr("--.--"));
  }
  if (m_sideModeLabel) {
    QString tag = modeTag.isEmpty() ? tr("本地") : modeTag;
    if (tag.size() > 28)
      tag = tag.left(28) + QStringLiteral("…");
    m_sideModeLabel->setText(tag);
  }

}

void ChartWindow::requestOnlineForecast() {
  if (!AppSettings::instance().forecastOnline()) {
    return; // 由 updateForecast 本地分支处理
  }
  if (m_pendingForecast)
    return;
  if (m_plotPoints.isEmpty()) {
    updateForecast();
    return;
  }
  if (!m_network)
    m_network = new QNetworkAccessManager(this);

  const QString apiKey = AppSettings::instance().xaiApiKey().trimmed();
  if (apiKey.isEmpty()) {
    Logger::warn(QStringLiteral("Online forecast: empty API key, fallback local"));
    // 避免递归：临时按本地算
    const bool was = AppSettings::instance().forecastOnline();
    Q_UNUSED(was);
    // 直接本地算法
    double ph = 0, pl = 0;
    if (computeDayRangeForecast(ph, pl)) {
      m_lastPredictHigh = ph;
      m_lastPredictLow = pl;
      m_hasPredict = true;
      m_forecastModeTag = tr("无Key·本地");
      m_lastForecastMs = QDateTime::currentMSecsSinceEpoch();
      double high = 0, low = 0;
      HistoryCache::instance().todayHigh(high);
      HistoryCache::instance().todayLow(low);
      updateSidePanelValues(m_plotPoints.last().second, ph, true, high, low, m_forecastModeTag);
    }
    return;
  }

  if (m_sidePredictLabel)
    m_sidePredictLabel->setText(tr("请求中…"));
  if (m_sideModeLabel)
    m_sideModeLabel->setText(tr("大模型…"));
  Logger::info(QStringLiteral("Online forecast request provider=%1 model=%2")
                   .arg(AppSettings::instance().llmProvider(),
                        AppSettings::instance().xaiModel()));

  // 构造摘要
  const int n = m_plotPoints.size();
  const int take = qMin(30, n);
  QString seriesText;
  for (int i = n - take; i < n; ++i) {
    const auto& pt = m_plotPoints.at(i);
    seriesText += QStringLiteral("%1 %2\n")
                      .arg(pt.first.toString(QStringLiteral("HH:mm")))
                      .arg(pt.second, 0, 'f', 2);
  }
  double actH = 0, actL = 0;
  HistoryCache::instance().todayHigh(actH);
  HistoryCache::instance().todayLow(actL);
  const double lastPrice = m_plotPoints.last().second;
  const QString src = currentTypeCode();
  const QString provider = AppSettings::instance().llmProvider();
  QString model = AppSettings::instance().xaiModel().trimmed();
  if (model.isEmpty())
    model = (provider == QStringLiteral("gemini")) ? QStringLiteral("gemini-2.0-flash")
                                                   : QStringLiteral("grok-4.6");

  const QString systemPrompt = QStringLiteral(
      "你是黄金短线分析助手。根据用户提供的今日分时与已出现高低，估计「当日剩余时段」可能达到的最高价与最低价。"
      "只输出一个 JSON 对象，不要 Markdown。格式："
      "{\"pred_high\":0.0,\"pred_low\":0.0,\"brief\":\"一句话理由\"}。"
      "pred_high 不得低于已出现今高，pred_low 不得高于已出现今低；幅度应克制，避免极端跳跃。这不是投资建议。");

  const QString userPrompt =
      QStringLiteral("品种:%1\n现价:%2\n已出现今高:%3 今低:%4\n最近分时:\n%5\n请给出今日预测最高/最低 JSON。")
          .arg(src)
          .arg(lastPrice, 0, 'f', 2)
          .arg(actH > 0 ? actH : lastPrice, 0, 'f', 2)
          .arg(actL > 0 ? actL : lastPrice, 0, 'f', 2)
          .arg(seriesText);

  QNetworkRequest request;
  request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("GoldPriceBarLite/0.7.7"));
  request.setTransferTimeout(30000);
  request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                       QNetworkRequest::NoLessSafeRedirectPolicy);

  QNetworkReply* reply = nullptr;
  if (provider == QStringLiteral("gemini")) {
    if (model.startsWith(QStringLiteral("models/")))
      model = model.mid(7);
    const QUrl url(QStringLiteral(
        "https://generativelanguage.googleapis.com/v1beta/models/%1:generateContent?key=%2")
                       .arg(model, QString::fromUtf8(QUrl::toPercentEncoding(apiKey))));
    request.setUrl(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    QJsonObject body;
    QJsonArray contents;
    QJsonObject userMsg;
    userMsg.insert(QStringLiteral("role"), QStringLiteral("user"));
    QJsonArray parts;
    parts.append(QJsonObject{{QStringLiteral("text"), systemPrompt + QStringLiteral("\n\n") + userPrompt}});
    userMsg.insert(QStringLiteral("parts"), parts);
    contents.append(userMsg);
    body.insert(QStringLiteral("contents"), contents);
    QJsonObject genCfg;
    genCfg.insert(QStringLiteral("temperature"), 0.2);
    genCfg.insert(QStringLiteral("maxOutputTokens"), 400);
    body.insert(QStringLiteral("generationConfig"), genCfg);
    reply = m_network->post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
  } else {
    request.setUrl(QUrl(QStringLiteral("https://api.x.ai/v1/chat/completions")));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setRawHeader("Authorization", QByteArray("Bearer ") + apiKey.toUtf8());
    QJsonObject body;
    body.insert(QStringLiteral("model"), model);
    body.insert(QStringLiteral("temperature"), 0.2);
    body.insert(QStringLiteral("max_tokens"), 400);
    QJsonArray messages;
    messages.append(QJsonObject{{QStringLiteral("role"), QStringLiteral("system")},
                                {QStringLiteral("content"), systemPrompt}});
    messages.append(QJsonObject{{QStringLiteral("role"), QStringLiteral("user")},
                                {QStringLiteral("content"), userPrompt}});
    body.insert(QStringLiteral("messages"), messages);
    reply = m_network->post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
  }

  m_pendingForecast = reply;
  connect(reply, &QNetworkReply::finished, this, [this, reply]() {
    onOnlineForecastFinished(reply);
  });
}

void ChartWindow::onOnlineForecastFinished(QNetworkReply *reply) {
  if (!reply)
    return;
  if (m_pendingForecast.data() == reply)
    m_pendingForecast.clear();

  auto fallback = [this](const QString& tag) {
    Logger::warn(QStringLiteral("Online forecast fallback: %1").arg(tag));
    double ph = 0, pl = 0;
    if (!computeDayRangeForecast(ph, pl)) {
      m_hasPredict = false;
      if (m_sidePredictLabel)
        m_sidePredictLabel->setText(tr("--.--"));
      if (m_sideModeLabel)
        m_sideModeLabel->setText(tag);
      return;
    }
    m_lastPredictHigh = ph;
    m_lastPredictLow = pl;
    m_lastPredictPrice = ph;
    m_hasPredict = true;
    m_forecastModeTag = tag;
    m_lastForecastMs = QDateTime::currentMSecsSinceEpoch();
    if (m_forecastSeries) {
      m_forecastSeries->clear();
      const QDateTime t0 = QDateTime(QDate::currentDate(), QTime(0, 0));
      const QDateTime t1 = QDateTime(QDate::currentDate(), QTime(23, 59, 59));
      m_forecastSeries->append(t0.toMSecsSinceEpoch(), ph);
      m_forecastSeries->append(t1.toMSecsSinceEpoch(), ph);
    }
    if (m_forecastLowSeries) {
      m_forecastLowSeries->clear();
      const QDateTime t0 = QDateTime(QDate::currentDate(), QTime(0, 0));
      const QDateTime t1 = QDateTime(QDate::currentDate(), QTime(23, 59, 59));
      m_forecastLowSeries->append(t0.toMSecsSinceEpoch(), pl);
      m_forecastLowSeries->append(t1.toMSecsSinceEpoch(), pl);
    }
    double high = 0, low = 0;
    HistoryCache::instance().todayHigh(high);
    HistoryCache::instance().todayLow(low);
    updateSidePanelValues(m_plotPoints.isEmpty() ? 0.0 : m_plotPoints.last().second,
                          ph, true, high, low, tag);
  };

  if (reply->error() != QNetworkReply::NoError) {
    const QString err = reply->errorString();
    reply->deleteLater();
    fallback(tr("在线失败·本地"));
    Q_UNUSED(err);
    return;
  }

  const QByteArray raw = reply->readAll();
  reply->deleteLater();

  QJsonParseError pe{};
  const QJsonDocument doc = QJsonDocument::fromJson(raw, &pe);
  if (pe.error != QJsonParseError::NoError || !doc.isObject()) {
    fallback(tr("解析失败·本地"));
    return;
  }

  // 取出模型文本
  QString content;
  const QJsonObject root = doc.object();
  if (root.contains(QStringLiteral("candidates"))) {
    // Gemini
    const QJsonArray cands = root.value(QStringLiteral("candidates")).toArray();
    if (!cands.isEmpty()) {
      const QJsonArray parts = cands.at(0).toObject()
                                   .value(QStringLiteral("content")).toObject()
                                   .value(QStringLiteral("parts")).toArray();
      if (!parts.isEmpty())
        content = parts.at(0).toObject().value(QStringLiteral("text")).toString();
    }
  } else if (root.contains(QStringLiteral("choices"))) {
    content = root.value(QStringLiteral("choices")).toArray().at(0).toObject()
                  .value(QStringLiteral("message")).toObject()
                  .value(QStringLiteral("content")).toString();
  }

  content = content.trimmed();
  // 剥离可能的 ```json 包裹
  if (content.startsWith(QStringLiteral("```"))) {
    const int nl = content.indexOf(QLatin1Char('\n'));
    if (nl > 0)
      content = content.mid(nl + 1);
    if (content.endsWith(QStringLiteral("```")))
      content.chop(3);
    content = content.trimmed();
  }

  QJsonParseError pe2{};
  QJsonDocument jdoc = QJsonDocument::fromJson(content.toUtf8(), &pe2);
  if (pe2.error != QJsonParseError::NoError || !jdoc.isObject()) {
    // 尝试截取第一个 { ... }
    const int a = content.indexOf(QLatin1Char('{'));
    const int b = content.lastIndexOf(QLatin1Char('}'));
    if (a >= 0 && b > a)
      jdoc = QJsonDocument::fromJson(content.mid(a, b - a + 1).toUtf8(), &pe2);
  }
  if (pe2.error != QJsonParseError::NoError || !jdoc.isObject()) {
    fallback(tr("JSON无效·本地"));
    return;
  }

  const QJsonObject jo = jdoc.object();
  double predHigh = jo.value(QStringLiteral("pred_high")).toDouble();
  double predLow = jo.value(QStringLiteral("pred_low")).toDouble();
  if (predHigh <= 0 || predLow <= 0 || predHigh < predLow) {
    fallback(tr("数值无效·本地"));
    return;
  }

  double actH = 0, actL = 0;
  HistoryCache::instance().todayHigh(actH);
  HistoryCache::instance().todayLow(actL);
  if (actH > 0)
    predHigh = qMax(predHigh, actH);
  if (actL > 0)
    predLow = qMin(predLow, actL);
  if (!m_plotPoints.isEmpty()) {
    const double px = m_plotPoints.last().second;
    const double hard = px * 0.006;
    predHigh = qMin(predHigh, qMax(actH > 0 ? actH : px, px) + hard);
    predLow = qMax(predLow, qMin(actL > 0 ? actL : px, px) - hard);
    if (predHigh < predLow) {
      predHigh = qMax(actH > 0 ? actH : px, px);
      predLow = qMin(actL > 0 ? actL : px, px);
    }
  }

  m_lastPredictHigh = predHigh;
  m_lastPredictLow = predLow;
  m_lastPredictPrice = predHigh;
  m_hasPredict = true;
  const QString brief = jo.value(QStringLiteral("brief")).toString();
  const QString prov = AppSettings::instance().llmProvider();
  m_forecastModeTag = (prov == QStringLiteral("gemini") ? tr("Gemini") : tr("Grok"))
                      + (brief.isEmpty() ? QString() : QStringLiteral("·") + brief.left(24));

  if (m_forecastSeries) {
    m_forecastSeries->clear();
    const QDateTime t0 = QDateTime(QDate::currentDate(), QTime(0, 0));
    const QDateTime t1 = QDateTime(QDate::currentDate(), QTime(23, 59, 59));
    m_forecastSeries->append(t0.toMSecsSinceEpoch(), predHigh);
    m_forecastSeries->append(t1.toMSecsSinceEpoch(), predHigh);
  }
  if (m_forecastLowSeries) {
    m_forecastLowSeries->clear();
    const QDateTime t0 = QDateTime(QDate::currentDate(), QTime(0, 0));
    const QDateTime t1 = QDateTime(QDate::currentDate(), QTime(23, 59, 59));
    m_forecastLowSeries->append(t0.toMSecsSinceEpoch(), predLow);
    m_forecastLowSeries->append(t1.toMSecsSinceEpoch(), predLow);
  }

  ForecastTracker::instance().recordDayRange(
      QDateTime::currentDateTime(), 3600, predHigh, predLow, m_forecastModeTag);

  m_lastForecastMs = QDateTime::currentMSecsSinceEpoch();
  double high = 0, low = 0;
  HistoryCache::instance().todayHigh(high);
  HistoryCache::instance().todayLow(low);
  const double cur = m_plotPoints.isEmpty() ? 0.0 : m_plotPoints.last().second;
  updateSidePanelValues(cur, predHigh, true, high, low, m_forecastModeTag);
}

void ChartWindow::updateHighLowMarkers() {
  m_highSeries->clear();
  m_lowSeries->clear();

  if (m_plotPoints.isEmpty() || !m_chart)
    return;

  int highIdx = 0, lowIdx = 0;
  for (int i = 1; i < m_plotPoints.size(); ++i) {
    if (m_plotPoints.at(i).second > m_plotPoints.at(highIdx).second)
      highIdx = i;
    if (m_plotPoints.at(i).second < m_plotPoints.at(lowIdx).second)
      lowIdx = i;
  }

  const auto &hp = m_plotPoints.at(highIdx);
  const auto &lp = m_plotPoints.at(lowIdx);
  m_highSeries->append(static_cast<qreal>(hp.first.toMSecsSinceEpoch()),
                       hp.second);
  m_lowSeries->append(static_cast<qreal>(lp.first.toMSecsSinceEpoch()),
                      lp.second);

  // 数值统一刷到右侧信息栏
  const double cur = m_plotPoints.last().second;
  updateSidePanelValues(cur, m_lastPredictPrice, m_hasPredict, hp.second,
                        lp.second, m_forecastModeTag);
}

void ChartWindow::fetchChartFromApi() {
  if (m_loading)
    return;
  if (m_pendingChart) {
    m_pendingChart->abort();
    m_pendingChart->deleteLater();
    m_pendingChart.clear();
  }

  m_loading = true;
  m_chart->setTitle(tr("正在加载分时数据…"));

  const QUrl url(
      QStringLiteral("https://jin.20021002.xyz/api.php?action=chart&type=%1")
          .arg(currentTypeCode()));
  QNetworkRequest request(url);
  request.setHeader(QNetworkRequest::UserAgentHeader,
                    QStringLiteral("GoldPriceBarLite/0.1.6"));
  request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                       QNetworkRequest::NoLessSafeRedirectPolicy);
  request.setAttribute(QNetworkRequest::CacheLoadControlAttribute,
                       QNetworkRequest::AlwaysNetwork);
  request.setTransferTimeout(12000);

  QNetworkReply *reply = m_network->get(request);
  m_pendingChart = reply;
  connect(reply, &QNetworkReply::finished, this,
          [this, reply]() { onChartReplyFinished(reply); });
}

void ChartWindow::onChartReplyFinished(QNetworkReply *reply) {
  m_loading = false;
  if (m_pendingChart.data() == reply)
    m_pendingChart.clear();

  if (reply->error() != QNetworkReply::NoError) {
    if (reply->error() != QNetworkReply::OperationCanceledError) {
      m_chart->setTitle(tr("分时加载失败：%1").arg(reply->errorString()));
      updateSeries();
    }
    reply->deleteLater();
    return;
  }

  const QByteArray raw = reply->readAll();
  reply->deleteLater();

  QJsonParseError err;
  const QJsonDocument doc = QJsonDocument::fromJson(raw, &err);
  if (err.error != QJsonParseError::NoError || !doc.isObject()) {
    m_chart->setTitle(tr("分时数据解析失败"));
    updateSeries();
    return;
  }

  const QJsonObject root = doc.object();
  if (root.value(QStringLiteral("code")).toInt() != 200) {
    m_chart->setTitle(tr("分时接口返回错误"));
    updateSeries();
    return;
  }

  const QJsonArray arr = root.value(QStringLiteral("data")).toArray();
  QVector<QPair<qint64, double>> chartPoints;
  chartPoints.reserve(arr.size());
  for (const QJsonValue &v : arr) {
    if (!v.isObject())
      continue;
    const QJsonObject o = v.toObject();
    const qint64 t =
        static_cast<qint64>(o.value(QStringLiteral("t")).toDouble());
    const double p = o.value(QStringLiteral("p")).toDouble();
    if (t > 0 && p > 0.0)
      chartPoints.append({t, p});
  }

  if (chartPoints.isEmpty()) {
    m_chart->setTitle(tr("分时数据为空"));
    updateSeries();
    return;
  }

  HistoryCache::instance().replaceFromChart(chartPoints);
    ExtremeDatabase::instance().refreshDailyBarFromPoints(
        QDate::currentDate(), currentTypeCode(), chartPoints);
  HistoryCache::instance().persistExtremesToDb(currentTypeCode());
  updateSeries();
}

void ChartWindow::updateSeries() {
  m_lastRedraw.restart();
  m_series->clear();
  hideCrosshair();

  m_plotPoints = HistoryCache::instance().todayPoints();
  if (m_plotPoints.isEmpty()) {
    m_chart->setTitle(tr("今日分时（暂无数据）"));
    const QDateTime now = QDateTime::currentDateTime();
    m_axisX->setRange(QDateTime(QDate::currentDate(), QTime(0, 0)),
                      now.addSecs(3600));
    m_axisY->setRange(900.0, 1100.0);
    if (m_forecastSeries) m_forecastSeries->clear();
    if (m_forecastLowSeries) m_forecastLowSeries->clear();
    m_currentSeries->clear();
    m_highSeries->clear();
    m_lowSeries->clear();
    if (m_ma5Series) m_ma5Series->clear();
    if (m_ma20Series) m_ma20Series->clear();
    if (m_yesterdaySeries) m_yesterdaySeries->clear();
    return;
  }

  qreal minPrice = m_plotPoints.first().second;
  qreal maxPrice = minPrice;
  const int n = m_plotPoints.size();
  int step = 1;
  if (n > 1000)
    step = n / 500;
  else if (n > 500)
    step = 2;

  for (int i = 0; i < n; i += step) {
    const auto &p = m_plotPoints.at(i);
    m_series->append(p.first.toMSecsSinceEpoch(), p.second);
    minPrice = qMin(minPrice, p.second);
    maxPrice = qMax(maxPrice, p.second);
  }
  if ((n - 1) % step != 0) {
    const auto &p = m_plotPoints.last();
    m_series->append(p.first.toMSecsSinceEpoch(), p.second);
    minPrice = qMin(minPrice, p.second);
    maxPrice = qMax(maxPrice, p.second);
  }
  for (const auto &p : m_plotPoints) {
    minPrice = qMin(minPrice, p.second);
    maxPrice = qMax(maxPrice, p.second);
  }

  // 日高低预测纳入坐标范围
  double ph = 0.0, pl = 0.0;
  if (computeDayRangeForecast(ph, pl)) {
    minPrice = qMin(minPrice, pl);
    maxPrice = qMax(maxPrice, ph);
  }

  // 横轴：左端尽量从当日 00:00 起；右端留足到当前之后 1 小时
  const QDateTime now = QDateTime::currentDateTime();
  const QDateTime dayStart = QDateTime(QDate::currentDate(), QTime(0, 0, 0));
  const QDateTime lastPt = m_plotPoints.last().first;

  QDateTime minTime = dayStart;
  // 若首点晚于 00:00，仍从 00:00 起，便于对照全日；数据极少时略留左边距
  if (m_plotPoints.size() < 5) {
    minTime = lastPt.addSecs(-600);
    if (minTime < dayStart)
      minTime = dayStart;
  }

  // 右端：取「最后数据点 / 当前时刻」较晚者，再加 1 小时，
  // 避免仅多出 2 分钟时预测段被挤在最右侧、几乎看不见。
  QDateTime maxTime = lastPt;
  if (now > maxTime)
    maxTime = now;
  maxTime = maxTime.addSecs(3600); // +1 小时

  // 不超过次日 00:00 + 1 小时
  const QDateTime nextMidnight = dayStart.addDays(1);
  if (maxTime > nextMidnight.addSecs(3600))
    maxTime = nextMidnight.addSecs(3600);

  m_axisX->setRange(minTime, maxTime);

  qreal margin = qMax(0.3, (maxPrice - minPrice) * 0.12);
  if (qFuzzyCompare(minPrice, maxPrice))
    margin = qMax(0.5, minPrice * 0.002);
  margin = qMax(margin, (maxPrice - minPrice) * 0.08 + 0.2);
  m_axisY->setRange(minPrice - margin, maxPrice + margin);

  double high = 0.0, low = 0.0;
  HistoryCache::instance().todayHigh(high);
  HistoryCache::instance().todayLow(low);

  const QString typeName =
      currentTypeCode() == QStringLiteral("gj")
          ? tr("伦敦金")
          : (currentTypeCode() == QStringLiteral("ms") ? tr("民生")
                                                       : tr("浙商"));

  QString predText;
  if (m_hasPredict && m_lastPredictHigh > 0.0) {
    predText = tr("  |  预测高 %1 低 %2")
                   .arg(m_lastPredictHigh, 0, 'f', 2)
                   .arg(m_lastPredictLow, 0, 'f', 2);
  }

  m_chart->setTitle(tr("%1 · 今日分时（%2点）  高 %3  低 %4%5")
                        .arg(typeName)
                        .arg(n)
                        .arg(high, 0, 'f', 2)
                        .arg(low, 0, 'f', 2)
                        .arg(predText));

  updateMovingAverages();
  updateYesterdayOverlay();

  QTimer::singleShot(50, this, [this]() {
    if (!isVisible())
      return;
    updateForecast();
    updateHighLowMarkers();
    updateMovingAverages();
  });
}

void ChartWindow::resizeEvent(QResizeEvent *event) {
  QWidget::resizeEvent(event);
  // 尺寸变化后仅刷新侧栏数值，避免依赖浮动坐标
  if (!m_plotPoints.isEmpty()) {
    double high = 0.0, low = 0.0;
    HistoryCache::instance().todayHigh(high);
    HistoryCache::instance().todayLow(low);
    updateSidePanelValues(m_plotPoints.last().second, m_lastPredictPrice,
                          m_hasPredict, high, low, m_forecastModeTag);
  }
}

void ChartWindow::closeEvent(QCloseEvent *event) {
  if (m_pendingChart) {
    m_pendingChart->abort();
    m_pendingChart->deleteLater();
    m_pendingChart.clear();
  }
  if (m_pendingForecast) {
    m_pendingForecast->abort();
    m_pendingForecast->deleteLater();
    m_pendingForecast.clear();
  }
  m_loading = false;
  QWidget::closeEvent(event);
}

void ChartWindow::fillPeriodCombo()
{
    if (!m_periodCombo)
        return;
    m_periodCombo->blockSignals(true);
    m_periodCombo->clear();
    // data: 0 = 今日分时；否则 YYYYMM
    m_periodCombo->addItem(tr("今日分时"), 0);

    const QDate today = QDate::currentDate();
    int year = today.year();
    int startMonth = 7;
    // 若当前早于 7 月，则从上一年 7 月起
    if (today.month() < 7) {
        year = today.year() - 1;
    }
    QDate d(year, 7, 1);
    const QDate end(today.year(), today.month(), 1);
    while (d <= end) {
        const int key = d.year() * 100 + d.month();
        m_periodCombo->addItem(tr("%1年%2月").arg(d.year()).arg(d.month()), key);
        d = d.addMonths(1);
    }
    m_periodCombo->setCurrentIndex(0);
    m_periodCombo->blockSignals(false);
}

bool ChartWindow::isIntradayMode() const
{
    if (!m_periodCombo)
        return true;
    return m_periodCombo->currentData().toInt() == 0;
}

void ChartWindow::setForecastVisible(bool on)
{
    if (m_forecastSeries)
        m_forecastSeries->setVisible(on);
    if (m_forecastLowSeries)
        m_forecastLowSeries->setVisible(on);
    if (m_currentSeries)
        m_currentSeries->setVisible(on);
    // 月份模式仍可用高低点系列
}

void ChartWindow::onPeriodChanged(int)
{
    if (isIntradayMode()) {
        setWindowTitle(tr("今日分时曲线"));
        setForecastVisible(true);
        m_axisX->setFormat(QStringLiteral("HH:mm"));
        fetchChartFromApi();
    } else {
        setForecastVisible(false);
        m_axisX->setFormat(QStringLiteral("MM-dd"));
        updateMonthSeries();
    }
}

void ChartWindow::updateMonthSeries()
{
    if (!m_periodCombo)
        return;
    const int key = m_periodCombo->currentData().toInt();
    if (key <= 0)
        return;

    const int year = key / 100;
    const int month = key % 100;
    setWindowTitle(tr("%1年%2月走势").arg(year).arg(month));

    m_series->clear();
    if (m_forecastSeries) m_forecastSeries->clear();
    if (m_forecastLowSeries) m_forecastLowSeries->clear();
    m_currentSeries->clear();
    m_highSeries->clear();
    m_lowSeries->clear();
    hideCrosshair();

    m_plotPoints = ExtremeDatabase::instance().loadMonthCloses(
        year, month, currentTypeCode());

    if (m_plotPoints.isEmpty()) {
        m_chart->setTitle(tr("%1年%2月 · 暂无本地日线数据\n"
                             "（公开接口仅提供当日分时；请保持程序运行以累积日线）")
                              .arg(year).arg(month));
        const QDate start(year, month, 1);
        const QDate end = start.addMonths(1).addDays(-1);
        m_axisX->setRange(QDateTime(start, QTime(0, 0)), QDateTime(end, QTime(23, 59)));
        m_axisY->setRange(800.0, 1200.0);
        return;
    }

    double minP = m_plotPoints.first().second;
    double maxP = minP;
    for (const auto& p : m_plotPoints) {
        m_series->append(p.first.toMSecsSinceEpoch(), p.second);
        minP = qMin(minP, p.second);
        maxP = qMax(maxP, p.second);
    }

    // 高低标记
    int hi = 0, lo = 0;
    for (int i = 1; i < m_plotPoints.size(); ++i) {
        if (m_plotPoints.at(i).second > m_plotPoints.at(hi).second)
            hi = i;
        if (m_plotPoints.at(i).second < m_plotPoints.at(lo).second)
            lo = i;
    }
    m_highSeries->append(m_plotPoints.at(hi).first.toMSecsSinceEpoch(),
                         m_plotPoints.at(hi).second);
    m_lowSeries->append(m_plotPoints.at(lo).first.toMSecsSinceEpoch(),
                        m_plotPoints.at(lo).second);

    const QDate start(year, month, 1);
    const QDate endDate = start.addMonths(1).addDays(-1);
    m_axisX->setRange(QDateTime(start, QTime(0, 0)).addDays(-1),
                      QDateTime(endDate, QTime(23, 59)).addDays(1));
    const double margin = qMax(0.5, (maxP - minP) * 0.12);
    m_axisY->setRange(minP - margin, maxP + margin);

    double mh = 0, ml = 0;
    int days = 0;
    ExtremeDatabase::instance().monthRange(year, month, currentTypeCode(), mh, ml, days);
    m_chart->setTitle(tr("%1年%2月日线（%3天）  高 %4  低 %5")
                          .arg(year).arg(month).arg(m_plotPoints.size())
                          .arg(mh > 0 ? mh : maxP, 0, 'f', 2)
                          .arg(ml > 0 ? ml : minP, 0, 'f', 2));

    updateSidePanelValues(
        m_plotPoints.last().second, 0.0, false,
        mh > 0 ? mh : maxP, ml > 0 ? ml : minP, tr("月线"));
}



void ChartWindow::onExportCsv()
{
    if (m_plotPoints.isEmpty()) {
        QMessageBox::information(this, tr("导出"), tr("当前没有可导出的数据点。"));
        return;
    }
    const QString suggest = isIntradayMode()
        ? QStringLiteral("intraday-%1.csv").arg(QDate::currentDate().toString(QStringLiteral("yyyyMMdd")))
        : QStringLiteral("month-%1.csv").arg(m_periodCombo ? m_periodCombo->currentData().toInt() : 0);
    const QString path = QFileDialog::getSaveFileName(
        this, tr("导出 CSV"), suggest, tr("CSV 文件 (*.csv)"));
    if (path.isEmpty())
        return;

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, tr("导出"), tr("无法写入文件：%1").arg(path));
        return;
    }
    QTextStream ts(&f);
    ts.setEncoding(QStringConverter::Utf8);
    ts << "time,price\n";
    for (const auto& p : m_plotPoints) {
        ts << p.first.toString(Qt::ISODateWithMs) << ","
           << QString::number(p.second, 'f', 4) << "\n";
    }
    f.close();
    QMessageBox::information(this, tr("导出"), tr("已导出 %1 个点。").arg(m_plotPoints.size()));
}

void ChartWindow::applyChartTheme()
{
    const bool dark = AppSettings::instance().darkTheme();
    if (!m_chart)
        return;

    // 曲线笔触：深色底用高对比色，避免「发白虚线」与看不清
    auto setDash = [](QLineSeries* s, const QColor& c, int width = 2) {
        if (!s) return;
        QPen pen(c);
        pen.setStyle(Qt::DashLine);
        pen.setWidth(width);
        s->setPen(pen);
    };
    auto setSolid = [](QLineSeries* s, const QColor& c, int width = 2) {
        if (!s) return;
        QPen pen(c);
        pen.setStyle(Qt::SolidLine);
        pen.setWidth(width);
        s->setPen(pen);
    };
    auto setDot = [](QLineSeries* s, const QColor& c, int width = 1) {
        if (!s) return;
        QPen pen(c);
        pen.setStyle(Qt::DotLine);
        pen.setWidth(width);
        s->setPen(pen);
    };

    if (dark) {
        m_chart->setBackgroundBrush(QBrush(QColor(28, 31, 38)));
        m_chart->setPlotAreaBackgroundBrush(QBrush(QColor(34, 38, 46)));
        m_chart->setTitleBrush(QBrush(QColor(230, 230, 230)));
        if (m_axisX) {
            m_axisX->setLabelsColor(QColor(180, 180, 180));
            m_axisX->setGridLineColor(QColor(55, 60, 70));
            m_axisX->setTitleBrush(QBrush(QColor(180, 180, 180)));
        }
        if (m_axisY) {
            m_axisY->setLabelsColor(QColor(180, 180, 180));
            m_axisY->setGridLineColor(QColor(55, 60, 70));
            m_axisY->setTitleBrush(QBrush(QColor(180, 180, 180)));
        }
        if (m_sidePanel)
            m_sidePanel->setStyleSheet(
                "QFrame{background:#252a33;border:1px solid #3d4450;border-radius:8px;}"
                "QLabel{color:#e8eaed;}");
        setStyleSheet("background:#1a1d23;");

        setSolid(m_series, QColor(64, 158, 255), 2);           // 实际：亮蓝
        setSolid(m_ma5Series, QColor(255, 167, 38), 2);         // MA5日：亮橙
        setSolid(m_ma20Series, QColor(186, 104, 200), 2);       // MA20日：亮紫
        setDot(m_yesterdaySeries, QColor(120, 144, 156), 1);    // 昨日：蓝灰点线（非白）
        setDash(m_forecastSeries, QColor(255, 82, 82), 2);      // 预测高：鲜红虚线
        setDash(m_forecastLowSeries, QColor(0, 230, 118), 2);   // 预测低：鲜绿虚线

        if (m_currentSeries) {
            m_currentSeries->setColor(QColor(255, 100, 100));
            m_currentSeries->setBorderColor(QColor(255, 200, 200));
        }
        if (m_sideCurrentLabel)
            m_sideCurrentLabel->setStyleSheet("color:#ffe082;font-size:16px;font-weight:bold;");
        if (m_sidePredictLabel)
            m_sidePredictLabel->setStyleSheet("color:#ff8a80;font-size:16px;font-weight:bold;");
        if (m_sideHighLabel)
            m_sideHighLabel->setStyleSheet("color:#ff8a80;font-size:16px;font-weight:bold;");
        if (m_sideLowLabel)
            m_sideLowLabel->setStyleSheet("color:#69f0ae;font-size:16px;font-weight:bold;");
        if (m_sideModeLabel)
            m_sideModeLabel->setStyleSheet("color:#9aa0a6;font-size:10px;");
        if (m_sideClockLabel)
            m_sideClockLabel->setStyleSheet("color:#82b1ff;font-size:16px;font-weight:bold;");
        if (m_sideAdviceLabel)
            m_sideAdviceLabel->setStyleSheet("color:#b0b8c4;font-size:11px;");
        if (m_sideHitRateLabel)
            m_sideHitRateLabel->setStyleSheet("color:#82b1ff;font-size:14px;font-weight:bold;");
    } else {
        m_chart->setBackgroundBrush(QBrush(QColor(255, 255, 255)));
        m_chart->setPlotAreaBackgroundBrush(QBrush(QColor(248, 249, 250)));
        m_chart->setTitleBrush(QBrush(QColor(33, 37, 41)));
        if (m_axisX) {
            m_axisX->setLabelsColor(QColor(80, 80, 80));
            m_axisX->setGridLineColor(QColor(230, 230, 230));
            m_axisX->setTitleBrush(QBrush(QColor(80, 80, 80)));
        }
        if (m_axisY) {
            m_axisY->setLabelsColor(QColor(80, 80, 80));
            m_axisY->setGridLineColor(QColor(230, 230, 230));
            m_axisY->setTitleBrush(QBrush(QColor(80, 80, 80)));
        }
        if (m_sidePanel)
            m_sidePanel->setStyleSheet(
                "QFrame{background:qlineargradient(x1:0,y1:0,x2:0,y2:1,"
                "stop:0 #ffffff, stop:1 #f0f3f7);border:1px solid #d8dee6;border-radius:8px;}"
                "QLabel{color:#5c6b77;}");
        setStyleSheet("background:#f5f6f8;");

        setSolid(m_series, QColor(0, 82, 217), 2);
        setSolid(m_ma5Series, QColor(230, 126, 34), 2);
        setSolid(m_ma20Series, QColor(155, 89, 182), 2);
        setDot(m_yesterdaySeries, QColor(120, 120, 120), 1);
        setDash(m_forecastSeries, QColor(231, 76, 60), 2);      // 预测高：红虚线
        setDash(m_forecastLowSeries, QColor(39, 174, 96), 2);   // 预测低：绿虚线

        if (m_currentSeries) {
            m_currentSeries->setColor(QColor(255, 160, 160));
            m_currentSeries->setBorderColor(QColor(220, 80, 80));
        }
        if (m_sideCurrentLabel)
            m_sideCurrentLabel->setStyleSheet("color:#212529;font-size:16px;font-weight:bold;");
        if (m_sidePredictLabel)
            m_sidePredictLabel->setStyleSheet("color:#e74c3c;font-size:16px;font-weight:bold;");
        if (m_sideHighLabel)
            m_sideHighLabel->setStyleSheet("color:#e74c3c;font-size:16px;font-weight:bold;");
        if (m_sideLowLabel)
            m_sideLowLabel->setStyleSheet("color:#27ae60;font-size:16px;font-weight:bold;");
        if (m_sideModeLabel)
            m_sideModeLabel->setStyleSheet("color:#888;font-size:10px;");
        if (m_sideClockLabel)
            m_sideClockLabel->setStyleSheet("color:#0052d9;font-size:16px;font-weight:bold;");
        if (m_sideAdviceLabel)
            m_sideAdviceLabel->setStyleSheet("color:#666;font-size:11px;");
        if (m_sideHitRateLabel)
            m_sideHitRateLabel->setStyleSheet("color:#0052d9;font-size:14px;font-weight:bold;");
    }

    if (m_chart->legend()) {
        m_chart->legend()->setLabelColor(dark ? QColor(220, 220, 220) : QColor(60, 60, 60));
        m_chart->legend()->setBackgroundVisible(false);
    }

    const bool showMa = AppSettings::instance().showMovingAverage() && isIntradayMode();
    if (m_ma5Series) m_ma5Series->setVisible(showMa);
    if (m_ma20Series) m_ma20Series->setVisible(showMa);
}

void ChartWindow::updateMovingAverages()
{
    if (!m_ma5Series || !m_ma20Series)
        return;
    m_ma5Series->clear();
    m_ma20Series->clear();
    const bool show = AppSettings::instance().showMovingAverage() && isIntradayMode();
    m_ma5Series->setVisible(show);
    m_ma20Series->setVisible(show);
    if (!show)
        return;

    // 真正的「5日线 / 20日线」：基于本地 daily_bars 收盘价的 SMA
    // 优先当前数据源；不足时回退 gj（freegoldapi 历史写入）
    auto closes = ExtremeDatabase::instance().loadRecentDailyCloses(30, currentTypeCode());
    if (closes.size() < 5)
        closes = ExtremeDatabase::instance().loadRecentDailyCloses(30, QStringLiteral("gj"));
    if (closes.size() < 5)
        closes = ExtremeDatabase::instance().loadRecentDailyCloses(30, QStringLiteral("xau"));

    auto sma = [](const QVector<QPair<QDate, double>>& c, int n) -> double {
        if (c.size() < n)
            return 0.0;
        double s = 0.0;
        for (int i = c.size() - n; i < c.size(); ++i)
            s += c.at(i).second;
        return s / static_cast<double>(n);
    };

    const double ma5 = sma(closes, 5);
    const double ma20 = sma(closes, 20);

    // 在分时图上画水平参考线（当日全天同一日线均价值）
    const QDateTime t0 = QDateTime(QDate::currentDate(), QTime(0, 0));
    const QDateTime t1 = QDateTime(QDate::currentDate(), QTime(23, 59, 59));
    const qint64 x0 = t0.toMSecsSinceEpoch();
    const qint64 x1 = t1.toMSecsSinceEpoch();

    if (ma5 > 0.0) {
        m_ma5Series->append(x0, ma5);
        m_ma5Series->append(x1, ma5);
    }
    if (ma20 > 0.0) {
        m_ma20Series->append(x0, ma20);
        m_ma20Series->append(x1, ma20);
    }

    // 把均线纳入 Y 轴范围：在 updateSeries 已算过 min/max，这里仅补充显示
    if ((ma5 > 0.0 || ma20 > 0.0) && m_axisY) {
        qreal yMin = m_axisY->min();
        qreal yMax = m_axisY->max();
        if (ma5 > 0.0) {
            yMin = qMin(yMin, ma5);
            yMax = qMax(yMax, ma5);
        }
        if (ma20 > 0.0) {
            yMin = qMin(yMin, ma20);
            yMax = qMax(yMax, ma20);
        }
        const qreal m = (yMax - yMin) * 0.05;
        m_axisY->setRange(yMin - m, yMax + m);
    }
}

void ChartWindow::updateYesterdayOverlay()
{
    if (!m_yesterdaySeries)
        return;
    m_yesterdaySeries->clear();
    if (!isIntradayMode()) {
        m_yesterdaySeries->setVisible(false);
        return;
    }
    m_yesterdaySeries->setVisible(true);

    const QDate yday = QDate::currentDate().addDays(-1);
    const auto pts = ExtremeDatabase::instance().loadIntradaySamples(
        yday, currentTypeCode());
    if (pts.isEmpty())
        return;

    // 将昨日点的「时刻」映射到今天的日期，便于与今日分时同轴对比
    const QDate today = QDate::currentDate();
    for (const auto& p : pts) {
        const QTime tm = p.first.time();
        const QDateTime mapped(today, tm);
        m_yesterdaySeries->append(mapped.toMSecsSinceEpoch(), p.second);
    }
}


QString ChartWindow::buildAdviceText(double price) const
{
    const QString src = AppSettings::instance().dataSource();
    QStringList tips;

    if (!TradingSession::isTradingNow(src)) {
        tips << tr("【时段】当前不在示意交易时间内，不宜下单；仅可观望。");
        tips << TradingSession::hoursDescription(src);
    } else {
        tips << tr("【时段】处于示意交易时段。");
    }

    const QString ev = EventCalendar::pendingAlertText();
    if (!ev.isEmpty())
        tips << tr("【宏观】%1，波动可能放大，注意风险。").arg(ev);

    if (price > 0.0) {
        QString code = src;
        if (code == QStringLiteral("xau")) code = QStringLiteral("gj");
        auto closes = ExtremeDatabase::instance().loadRecentDailyCloses(20, code);
        if (closes.size() < 5)
            closes = ExtremeDatabase::instance().loadRecentDailyCloses(20, QStringLiteral("gj"));
        if (closes.size() >= 5) {
            double s5 = 0.0;
            const int k = qMin(5, closes.size());
            for (int i = closes.size() - k; i < closes.size(); ++i)
                s5 += closes.at(i).second;
            const double ma5 = s5 / k;
            int below = 0;
            for (const auto& c : closes)
                if (c.second < price) ++below;
            const double pct = 100.0 * below / closes.size();
            if (price < ma5 * 0.998)
                tips << tr("【技术】现价低于近5日均线，偏谨慎。");
            else if (price > ma5 * 1.002)
                tips << tr("【技术】现价高于近5日均线，追高需谨慎。");
            if (pct <= 20)
                tips << tr("【位置】处近20日偏低分位，仅作区间参考。");
            else if (pct >= 80)
                tips << tr("【位置】处近20日偏高分位，注意回撤风险。");
        }
    }

    tips << tr("以上仅为软件规则提示，不构成投资建议。");
    return tips.join(QStringLiteral("\n"));
}

void ChartWindow::updateClockAndAdvice()
{
    const QDateTime now = QDateTime::currentDateTime();
    if (m_sideClockLabel)
        m_sideClockLabel->setText(now.toString(QStringLiteral("HH:mm:ss")));

    const QString src = AppSettings::instance().dataSource();
    const bool open = TradingSession::isTradingNow(src, now);
    if (m_sideSessionLabel) {
        m_sideSessionLabel->setText(TradingSession::statusText(src, now));
        m_sideSessionLabel->setStyleSheet(
            open ? QStringLiteral("color:#27ae60;font-size:11px;font-weight:bold;")
                 : QStringLiteral("color:#e67e22;font-size:11px;font-weight:bold;"));
        m_sideSessionLabel->setToolTip(TradingSession::hoursDescription(src));
    }

    double price = 0.0;
    if (!m_plotPoints.isEmpty())
        price = m_plotPoints.last().second;
    if (m_sideAdviceLabel)
        m_sideAdviceLabel->setText(buildAdviceText(price));
}
