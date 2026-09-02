#include "ChartWindow.h"
#include "AppSettings.h"
#include "ExtremeDatabase.h"
#include "Logger.h"
#include "ForecastTracker.h"
#include "HistoryCache.h"


#include <QBrush>
#include <QCloseEvent>
#include <QDateTime>
#include <QDate>
#include <QFrame>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QComboBox>
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
            if (isVisible() && !m_plotPoints.isEmpty())
              updateForecast();
          });
  m_lastRedraw.invalidate();
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

  // 未来 2 分钟预测：虚线
  m_forecastSeries = new QLineSeries(this);
  m_forecastSeries->setName(tr("预测2分钟"));
  m_forecastSeries->setPointsVisible(false);
  QPen dashPen(QColor(255, 128, 128));
  dashPen.setWidth(2);
  dashPen.setStyle(Qt::DashLine);
  m_forecastSeries->setPen(dashPen);

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
  m_chart->addSeries(m_forecastSeries);
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
                             static_cast<QAbstractSeries *>(m_forecastSeries),
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

  auto *topBar = new QHBoxLayout();
  topBar->addWidget(new QLabel(tr("周期："), this));
  topBar->addWidget(m_periodCombo);
  topBar->addStretch();

  // 主布局：上工具栏 + 曲线 + 右侧信息栏
  auto *root = new QVBoxLayout(this);
  root->setContentsMargins(4, 4, 4, 4);
  root->setSpacing(4);
  root->addLayout(topBar);

  auto *body = new QHBoxLayout();
  body->setSpacing(6);
  body->addWidget(m_chartView, 1);

  m_sidePanel = new QFrame(this);
  m_sidePanel->setFixedWidth(128);
  m_sidePanel->setStyleSheet(
      "QFrame{background:#f8f9fa;border:1px solid #e3e6eb;border-radius:6px;}");
  auto *sideLay = new QVBoxLayout(m_sidePanel);
  sideLay->setContentsMargins(10, 12, 10, 12);
  sideLay->setSpacing(10);

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

  sideLay->addWidget(mkTitle(tr("当前价")));
  m_sideCurrentLabel = mkValue(tr("--.--"), "#212529");
  sideLay->addWidget(m_sideCurrentLabel);

  sideLay->addWidget(mkTitle(tr("2分钟预测")));
  m_sidePredictLabel = mkValue(tr("--.--"), "#e74c3c");
  sideLay->addWidget(m_sidePredictLabel);

  m_sideModeLabel = new QLabel(tr("模式: 本地"), m_sidePanel);
  m_sideModeLabel->setStyleSheet("color:#888;font-size:10px;");
  m_sideModeLabel->setWordWrap(true);
  sideLay->addWidget(m_sideModeLabel);

  sideLay->addWidget(mkTitle(tr("预测命中率")));
  m_sideHitRateLabel = mkValue(tr("--%"), "#0052d9");
  m_sideHitRateLabel->setStyleSheet("color:#0052d9;font-size:14px;font-weight:bold;");
  sideLay->addWidget(m_sideHitRateLabel);

  sideLay->addSpacing(6);
  sideLay->addWidget(mkTitle(tr("今日最高")));
  m_sideHighLabel = mkValue(tr("--.--"), "#e74c3c");
  sideLay->addWidget(m_sideHighLabel);

  sideLay->addWidget(mkTitle(tr("今日最低")));
  m_sideLowLabel = mkValue(tr("--.--"), "#27ae60");
  sideLay->addWidget(m_sideLowLabel);

  sideLay->addStretch();
  body->addWidget(m_sidePanel, 0);
  root->addLayout(body, 1);
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

  // 实时只刷新「当前点」和侧栏当前/高低；预测值不跟每一跳实时价乱变
  m_plotPoints = HistoryCache::instance().todayPoints();
  if (!m_plotPoints.isEmpty()) {
    m_currentSeries->clear();
    const auto &cur = m_plotPoints.last();
    m_currentSeries->append(cur.first.toMSecsSinceEpoch(), cur.second);
  }

  double high = 0.0, low = 0.0;
  HistoryCache::instance().todayHigh(high);
  HistoryCache::instance().todayLow(low);
  if (price > 0.0)
    ForecastTracker::instance().evaluateWithActual(price, m_plotPoints);

  updateSidePanelValues(
      price > 0 ? price
                : (m_plotPoints.isEmpty() ? 0.0 : m_plotPoints.last().second),
      m_lastPredictPrice, m_hasPredict, high, low, m_forecastModeTag);

  // 预测：至少间隔 30 秒才重算，避免侧栏预测值持续跳动
  const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
  if (m_lastForecastMs == 0 ||
      (nowMs - m_lastForecastMs) >= kForecastIntervalMs) {
    updateForecast();
  }
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
 * 本地 2 分钟预测（务实版）：
 * 短时金价近似随机游走，最优基线往往是「维持现价」。
 * 在此基础上叠加：多尺度稳健动量 + 向 EMA 的弱回归，并用已实现波动率限制幅度。
 * 仅供参考，不构成投资建议。
 */
QVector<QPair<QDateTime, double>>
ChartWindow::computeForecastLocal(int horizonSec) const
{
    QVector<QPair<QDateTime, double>> out;
    if (m_plotPoints.size() < 4)
        return out;

    const int n = m_plotPoints.size();
    const double lastPrice = m_plotPoints.last().second;
    const QDateTime lastT = m_plotPoints.last().first;
    if (lastPrice <= 0.0)
        return out;

    auto priceAtOffset = [&](int back) -> double {
        const int idx = qMax(0, n - 1 - back);
        return m_plotPoints.at(idx).second;
    };
    auto secsBack = [&](int back) -> double {
        const int idx = qMax(0, n - 1 - back);
        return static_cast<double>(m_plotPoints.at(idx).first.secsTo(lastT));
    };

    // 1) EMA（近期中枢）
    const int wEma = qMin(30, n);
    double ema = m_plotPoints.at(n - wEma).second;
    const double alpha = 0.25;
    for (int i = n - wEma + 1; i < n; ++i)
        ema = alpha * m_plotPoints.at(i).second + (1.0 - alpha) * ema;

    // 2) 多尺度动量（元/秒），取中位数更抗噪
    QVector<double> moms;
    for (int back : {3, 5, 8, 12}) {
        if (back >= n)
            continue;
        const double dt = secsBack(back);
        if (dt < 2.0)
            continue;
        moms.append((lastPrice - priceAtOffset(back)) / dt);
    }
    double momMed = 0.0;
    if (!moms.isEmpty()) {
        std::sort(moms.begin(), moms.end());
        momMed = moms.at(moms.size() / 2);
    }

    // 3) 已实现波动：近窗价格标准差 / 时间尺度 → 限制 2 分钟合理波动
    const int wVol = qMin(25, n);
    double mean = 0.0;
    for (int i = n - wVol; i < n; ++i)
        mean += m_plotPoints.at(i).second;
    mean /= static_cast<double>(wVol);
    double var = 0.0;
    for (int i = n - wVol; i < n; ++i) {
        const double d = m_plotPoints.at(i).second - mean;
        var += d * d;
    }
    const double stdev = qSqrt(var / static_cast<double>(qMax(1, wVol - 1)));
    const double spanSec = qMax(30.0, secsBack(wVol - 1));
    // 将窗口波动缩放到 horizon
    const double volHorizon = stdev * qSqrt(static_cast<double>(horizonSec) / spanSec);

    // 4) 合成：现价基线 + 弱动量(40%) + 弱回归(60% 的一部分)
    // 回归：2 分钟内只消化与 EMA 差距的一小部分
    const double gap = ema - lastPrice;
    const double reversionMove = gap * 0.20; // 最多向中枢靠 20%

    double driftMove = momMed * horizonSec * 0.40;
    // 动量幅度不超过 0.6 * volHorizon
    const double momCap = qMax(0.08, 0.6 * volHorizon);
    driftMove = qBound(-momCap, driftMove, momCap);

    double totalMove = driftMove + reversionMove;

    // 总位移硬顶：max(0.12元, 0.05% 现价, 0.85*volHorizon)
    const double hardCap = qMax(0.12, qMax(lastPrice * 0.0005, 0.85 * volHorizon));
    totalMove = qBound(-hardCap, totalMove, hardCap);

    // 极低波动时：预测几乎等于现价（随机游走）
    if (stdev < 0.08 && qAbs(momMed) * 120.0 < 0.05)
        totalMove *= 0.15;

    out.append({lastT, lastPrice});
    const int step = 10;
    for (int s = step; s <= horizonSec; s += step) {
        const double frac = static_cast<double>(s) / static_cast<double>(horizonSec);
        // 前半段更贴近现价，后半段逐渐体现漂移（减轻早期误差）
        const double ease = frac * frac; // 二次缓入
        const double y = lastPrice + totalMove * ease;
        out.append({lastT.addSecs(s), y});
    }
    return out;
}

void ChartWindow::updateForecast() {
  m_lastForecastMs = QDateTime::currentMSecsSinceEpoch();
  m_forecastSeries->clear();
  m_currentSeries->clear();

  if (m_plotPoints.isEmpty())
    return;

  // 当前点（浅红色）
  const auto &cur = m_plotPoints.last();
  m_currentSeries->append(cur.first.toMSecsSinceEpoch(), cur.second);

  if (AppSettings::instance().forecastOnline()) {
    // 在线模式：异步请求，失败则回退本地
    requestOnlineForecast();
    // 先用本地占位，避免空白
    const auto local = computeForecastLocal(120);
    if (local.size() >= 2)
      applyForecastPoints(local, tr("本地(请求中)"));
  } else {
    const auto local = computeForecastLocal(120);
    applyForecastPoints(local, tr("本地"));
  }
}

void ChartWindow::applyForecastPoints(
    const QVector<QPair<QDateTime, double>> &forecast, const QString &modeTag) {
  m_forecastModeTag = modeTag;
  m_forecastSeries->clear();
  if (forecast.size() < 2) {
    m_hasPredict = false;
    m_lastPredictPrice = 0.0;
    if (m_sidePredictLabel)
      m_sidePredictLabel->setText(tr("--.--"));
    if (m_sideModeLabel)
      m_sideModeLabel->setText(tr("模式: %1").arg(modeTag));
    return;
  }

  for (const auto &p : forecast)
    m_forecastSeries->append(p.first.toMSecsSinceEpoch(), p.second);

  m_hasPredict = true;
  m_lastPredictPrice = forecast.last().second;

  // 登记预测，供 2 分钟后统计命中率
  ForecastTracker::instance().recordPrediction(
      forecast.first().first, 120, m_lastPredictPrice,
      forecast.first().second, modeTag);

  // 确保横轴右端覆盖预测终点（当前时间 + 3 分钟）
  if (m_axisX) {
    const QDateTime now = QDateTime::currentDateTime();
    const QDateTime needMax =
        ((now > forecast.last().first) ? now : forecast.last().first)
            .addSecs(3600);
    QDateTime curMax = m_axisX->max();
    QDateTime curMin = m_axisX->min();
    if (needMax > curMax)
      m_axisX->setRange(curMin, needMax);
  }

  double high = 0.0, low = 0.0;
  HistoryCache::instance().todayHigh(high);
  HistoryCache::instance().todayLow(low);
  const double cur = m_plotPoints.isEmpty() ? 0.0 : m_plotPoints.last().second;
  updateSidePanelValues(cur, m_lastPredictPrice, true, high, low, modeTag);
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
    if (hasPredict && predict > 0.0)
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
  if (m_sideModeLabel)
    m_sideModeLabel->setText(
        tr("模式: %1").arg(modeTag.isEmpty() ? tr("本地") : modeTag));

  if (m_sideHitRateLabel) {
    auto& ft = ForecastTracker::instance();
    const int n = ft.totalEvaluated();
    if (n <= 0) {
      m_sideHitRateLabel->setText(tr("--%"));
    } else {
      m_sideHitRateLabel->setText(
          tr("%1% (%2/%3)\nMAE %4")
              .arg(ft.hitRatePercent(), 0, 'f', 1)
              .arg(ft.hits())
              .arg(n)
              .arg(ft.meanAbsError(), 0, 'f', 2));
    }
  }

}

void ChartWindow::requestOnlineForecast() {
  if (m_pendingForecast)
    return;
  if (m_plotPoints.isEmpty())
    return;

  const QString apiKey = AppSettings::instance().xaiApiKey().trimmed();
  if (apiKey.isEmpty()) {
    const auto local = computeForecastLocal(120);
    applyForecastPoints(local, tr("无Key·本地"));
    return;
  }

  // 构造近期行情摘要给大模型
  const int n = m_plotPoints.size();
  const int take = qMin(20, n);
  QString seriesText;
  for (int i = n - take; i < n; ++i) {
    const auto &pt = m_plotPoints.at(i);
    seriesText += QStringLiteral("%1 %2\n")
                      .arg(pt.first.toString(QStringLiteral("HH:mm:ss")))
                      .arg(pt.second, 0, 'f', 2);
  }
  const double lastPrice = m_plotPoints.last().second;
  const QString src = currentTypeCode();

  const QString systemPrompt = QStringLiteral(
      "你是黄金短线分析助手。根据用户提供的最近分时点，推断未来约120秒可能的价"
      "格路径。"
      "只输出一个 JSON 对象，不要 Markdown，不要解释。格式："
      "{\"points\":[{\"offset_sec\":10,\"price\":0.0}],\"brief\":"
      "\"一句话理由\"}。"
      "points 从 offset_sec=10 起，步长约10秒，直到120；price 为合理小数。"
      "这不是投资建议，路径应平滑、贴近最近走势，避免极端跳跃。");

  const QString userPrompt =
      QStringLiteral("品种代码:%1\n当前价:%2\n最近分时(时间 "
                     "价格):\n%3\n请给出未来120秒预测路径 JSON。")
          .arg(src)
          .arg(lastPrice, 0, 'f', 2)
          .arg(seriesText);

  QJsonObject body;
  body.insert(QStringLiteral("model"), AppSettings::instance().xaiModel());
  body.insert(QStringLiteral("temperature"), 0.2);
  body.insert(QStringLiteral("max_tokens"), 500);

  QJsonArray messages;
  messages.append(
      QJsonObject{{QStringLiteral("role"), QStringLiteral("system")},
                  {QStringLiteral("content"), systemPrompt}});
  messages.append(QJsonObject{{QStringLiteral("role"), QStringLiteral("user")},
                              {QStringLiteral("content"), userPrompt}});
  body.insert(QStringLiteral("messages"), messages);

  const QUrl url(QStringLiteral("https://api.x.ai/v1/chat/completions"));
  QNetworkRequest request(url);
  request.setHeader(QNetworkRequest::ContentTypeHeader,
                    QStringLiteral("application/json"));
  request.setRawHeader("Authorization",
                       QByteArray("Bearer ") + apiKey.toUtf8());
  request.setHeader(QNetworkRequest::UserAgentHeader,
                    QStringLiteral("GoldPriceBarLite/0.2.0"));
  request.setTransferTimeout(25000);

  QNetworkReply *reply = m_network->post(
      request, QJsonDocument(body).toJson(QJsonDocument::Compact));
  m_pendingForecast = reply;
  connect(reply, &QNetworkReply::finished, this,
          [this, reply]() { onOnlineForecastFinished(reply); });
}

void ChartWindow::onOnlineForecastFinished(QNetworkReply *reply) {
  if (m_pendingForecast.data() == reply)
    m_pendingForecast.clear();

  auto fallbackLocal = [this](const QString &tag) {
    const auto local = computeForecastLocal(120);
    applyForecastPoints(local, tag);
  };

  if (reply->error() != QNetworkReply::NoError) {
    const QString err = reply->errorString();
    reply->deleteLater();
    fallbackLocal(tr("Grok失败·本地"));
    Q_UNUSED(err);
    return;
  }

  const QByteArray raw = reply->readAll();
  reply->deleteLater();

  QJsonParseError err;
  const QJsonDocument doc = QJsonDocument::fromJson(raw, &err);
  if (err.error != QJsonParseError::NoError || !doc.isObject()) {
    fallbackLocal(tr("解析失败·本地"));
    return;
  }

  const QJsonObject root = doc.object();
  // OpenAI 兼容：choices[0].message.content
  const QJsonArray choices = root.value(QStringLiteral("choices")).toArray();
  if (choices.isEmpty()) {
    fallbackLocal(tr("空响应·本地"));
    return;
  }

  QString content = choices.at(0)
                        .toObject()
                        .value(QStringLiteral("message"))
                        .toObject()
                        .value(QStringLiteral("content"))
                        .toString()
                        .trimmed();

  // 去掉可能的 ```json 包裹
  if (content.startsWith(QStringLiteral("```"))) {
    const int firstNl = content.indexOf(QLatin1Char('\n'));
    const int lastFence = content.lastIndexOf(QStringLiteral("```"));
    if (firstNl >= 0 && lastFence > firstNl)
      content = content.mid(firstNl + 1, lastFence - firstNl - 1).trimmed();
  }

  QJsonParseError perr;
  const QJsonDocument predDoc =
      QJsonDocument::fromJson(content.toUtf8(), &perr);
  if (perr.error != QJsonParseError::NoError || !predDoc.isObject()) {
    fallbackLocal(tr("格式失败·本地"));
    return;
  }

  const QJsonObject predObj = predDoc.object();
  const QJsonArray points = predObj.value(QStringLiteral("points")).toArray();
  if (points.isEmpty()) {
    fallbackLocal(tr("无点·本地"));
    return;
  }

  QVector<QPair<QDateTime, double>> forecast;
  if (!m_plotPoints.isEmpty())
    forecast.append(m_plotPoints.last());

  const QDateTime base = m_plotPoints.isEmpty() ? QDateTime::currentDateTime()
                                                : m_plotPoints.last().first;

  for (const QJsonValue &v : points) {
    if (!v.isObject())
      continue;
    const QJsonObject o = v.toObject();
    int offset = o.value(QStringLiteral("offset_sec")).toInt();
    if (offset <= 0)
      offset = o.value(QStringLiteral("t")).toInt();
    double price = o.value(QStringLiteral("price")).toDouble();
    if (price <= 0.0)
      price = o.value(QStringLiteral("p")).toDouble();
    if (offset > 0 && price > 0.0)
      forecast.append({base.addSecs(offset), price});
  }

  if (forecast.size() < 2) {
    fallbackLocal(tr("点不足·本地"));
    return;
  }

  applyForecastPoints(forecast, tr("Grok"));
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
    m_forecastSeries->clear();
    m_currentSeries->clear();
    m_highSeries->clear();
    m_lowSeries->clear();

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

  // 先算预测，把预测价也纳入坐标范围
  const auto forecast = computeForecastLocal(120);
  for (const auto &p : forecast) {
    minPrice = qMin(minPrice, p.second);
    maxPrice = qMax(maxPrice, p.second);
  }

  // 横轴：左端尽量从当日 00:00 起；右端必须超过「当前时间」至少 3 分钟，
  // 否则未来 2 分钟预测虚线会被裁切、无法完整显示。
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
  if (forecast.size() >= 2) {
    predText = tr("  |  2分钟预测 %1").arg(forecast.last().second, 0, 'f', 2);
  }

  m_chart->setTitle(tr("%1 · 今日分时（%2点）  高 %3  低 %4%5")
                        .arg(typeName)
                        .arg(n)
                        .arg(high, 0, 'f', 2)
                        .arg(low, 0, 'f', 2)
                        .arg(predText));

  QTimer::singleShot(50, this, [this]() {
    if (!isVisible())
      return;
    updateForecast();
    updateHighLowMarkers();
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
    m_forecastSeries->clear();
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

