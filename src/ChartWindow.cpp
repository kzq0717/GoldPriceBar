#include "ChartWindow.h"
#include "goldsdk/forecast.hpp"
#include <vector>
#include "AppSettings.h"
#include "ExtremeDatabase.h"
#include "Logger.h"
#include "ForecastTracker.h"
#include "HistoryCache.h"
#include "TradingSession.h"
#include "EventCalendar.h"
#include "ForecastService.h"


#include <QBrush>
#include <QCloseEvent>
#include <QDateTime>
#include <QDate>
#include <QFrame>
#include <QSizePolicy>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QJsonObject>
#include <QLabel>
#include <QComboBox>
#include <QDateEdit>
#include <QSet>
#include <QList>
#include <algorithm>
#include <QToolButton>
#include <QListWidget>
#include <QListWidgetItem>
#include <QAbstractItemView>
#include <QMenu>
#include <QAction>
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
  Logger::info(QStringLiteral("ChartWindow ctor begin"));
  setWindowTitle(tr("今日分时曲线"));
  setMinimumSize(760, 440);
  resize(920, 520);
  setWindowFlags(Qt::Window);

  Logger::info(QStringLiteral("ChartWindow: create NAM"));
  m_network = new QNetworkAccessManager(this);
  Logger::info(QStringLiteral("ChartWindow: setupChart"));
  setupChart();
  Logger::info(QStringLiteral("ChartWindow: setupChart done"));
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
  connect(&ForecastService::instance(), &ForecastService::forecastUpdated, this,
          &ChartWindow::onForecastServiceUpdated);
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
  Logger::info(QStringLiteral("ChartWindow ctor end"));
}

QString ChartWindow::currentTypeCode() const
{
    return AppSettings::instance().dataSource().trimmed().isEmpty()
               ? QStringLiteral("zs")
               : (AppSettings::instance().dataSource() == QStringLiteral("xau")
                      ? QStringLiteral("gj")
                      : AppSettings::instance().dataSource().trimmed().toLower());
}


void ChartWindow::setupChart() {
  m_series = new QLineSeries(this);
  m_series->setName(tr("实际"));
  m_series->setPointsVisible(false);
  QPen pen(QColor(0, 82, 217));
  pen.setWidth(2);
  m_series->setPen(pen);

  m_ma5Series = new QLineSeries(this);
  m_ma5Series->setName(tr("MA5"));
  m_ma5Series->setPointsVisible(false);
  QPen ma5(QColor(61, 214, 140));
  ma5.setWidth(2);
  m_ma5Series->setPen(ma5);
  m_ma5Series->setVisible(false);

  m_ma10Series = new QLineSeries(this);
  m_ma10Series->setName(tr("MA10"));
  m_ma10Series->setPointsVisible(false);
  QPen ma10(QColor(255, 193, 7));
  ma10.setWidth(2);
  m_ma10Series->setPen(ma10);
  m_ma10Series->setVisible(false);

  m_ma20Series = new QLineSeries(this);
  m_ma20Series->setName(tr("MA20"));
  m_ma20Series->setPointsVisible(false);
  QPen ma20(QColor(167, 139, 250));
  ma20.setWidth(2);
  m_ma20Series->setPen(ma20);
  m_ma20Series->setVisible(false);

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

  // 当前点：红色呼吸光点（带高亮白色描边）
  m_currentSeries = new QScatterSeries(this);
  m_currentSeries->setName(tr("当前"));
  m_currentSeries->setMarkerShape(QScatterSeries::MarkerShapeCircle);
  m_currentSeries->setMarkerSize(10);
  m_currentSeries->setColor(QColor(255, 77, 79));
  m_currentSeries->setBorderColor(QColor(255, 255, 255));

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
  m_chart->addSeries(m_ma10Series);
  m_chart->addSeries(m_ma20Series);
  m_chart->addSeries(m_yesterdaySeries);
  m_chart->addSeries(m_forecastSeries);
  m_chart->addSeries(m_forecastLowSeries);
  m_chart->addSeries(m_highSeries);
  m_chart->addSeries(m_lowSeries);
  m_chart->addSeries(m_currentSeries);
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

  const QList<QAbstractSeries*> seriesList = {
      m_series, m_ma5Series, m_ma10Series, m_ma20Series, m_yesterdaySeries,
      m_forecastSeries, m_forecastLowSeries, m_currentSeries, m_highSeries, m_lowSeries};
  for (QAbstractSeries *s : seriesList) {
    if (!s)
      continue;
    if (!s->attachedAxes().contains(m_axisX))
      s->attachAxis(m_axisX);
    if (!s->attachedAxes().contains(m_axisY))
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

  // —— 顶部现代工具栏：左周期 | 中均线胶囊 | 右导出 ——
  m_toolbar = new QFrame(this);
  m_toolbar->setObjectName(QStringLiteral("chartToolbar"));
  m_toolbar->setFixedHeight(48);
  m_toolbar->setStyleSheet(
      "QFrame#chartToolbar{"
      "  background:#161b27;"
      "  border:1px solid #2a3347;"
      "  border-radius:12px;"
      "}"
      "QFrame#chartToolbar QLabel#tbHint{"
      "  color:#6b778c;font-size:11px;font-weight:500;"
      "}"
      "QFrame#chartToolbar QComboBox{"
      "  background:#1c2433;color:#e8eaed;"
      "  border:1px solid #2f3a4f;border-radius:8px;"
      "  padding:4px 8px;min-height:28px;max-width:96px;"
      "  font-size:12px;"
      "}"
      "QFrame#chartToolbar QComboBox:hover{border-color:#5b8def;}"
      "QFrame#chartToolbar QComboBox::drop-down{border:none;width:18px;}"
      "QFrame#chartToolbar QComboBox QAbstractItemView{"
      "  background:#1c2433;color:#e8eaed;border:1px solid #2f3a4f;"
      "  selection-background-color:#2a4a7a;"
      "}"
      "QFrame#chartToolbar QDateEdit{"
      "  background:#1c2433;color:#e8eaed;"
      "  border:1px solid #2f3a4f;border-radius:8px;"
      "  padding:3px 6px;min-height:28px;max-width:118px;"
      "  font-size:12px;"
      "}"
      "QFrame#chartToolbar QDateEdit:disabled{"
      "  color:#5c6b77;background:#141922;border-color:#252c3a;"
      "}"
      "QFrame#chartToolbar QDateEdit:hover:!disabled{border-color:#5b8def;}"
      "QFrame#chartToolbar QDateEdit::drop-down{border:none;width:18px;}"
      "QFrame#chartToolbar QPushButton#periodQuery{"
      "  background:#1a3a5c;color:#7eb6ff;"
      "  border:1px solid #5b8def;border-radius:8px;"
      "  padding:4px 12px;min-height:28px;font-size:12px;font-weight:600;"
      "}"
      "QFrame#chartToolbar QPushButton#periodQuery:hover{background:#234a72;}"
      "QFrame#chartToolbar QPushButton#periodQuery:disabled{"
      "  background:#141922;color:#5c6b77;border-color:#252c3a;"
      "}"
      "QFrame#chartToolbar QToolButton#maPill{"
      "  background:#1c2433;color:#9aa8bc;"
      "  border:1px solid #2f3a4f;border-radius:14px;"
      "  padding:4px 12px;min-height:28px;font-size:12px;font-weight:600;"
      "}"
      "QFrame#chartToolbar QToolButton#maPill:hover{"
      "  border-color:#5b8def;color:#e8eaed;"
      "}"
      "QFrame#chartToolbar QToolButton#maPill:checked{"
      "  background:#1a3a5c;color:#7eb6ff;border-color:#5b8def;"
      "}"
      "QFrame#chartToolbar QPushButton#exportCsv{"
      "  background:transparent;color:#9aa8bc;"
      "  border:1px solid #2f3a4f;border-radius:8px;"
      "  padding:4px 14px;min-height:28px;font-size:12px;"
      "}"
      "QFrame#chartToolbar QPushButton#exportCsv:hover{"
      "  background:#1c2433;color:#e8eaed;border-color:#5b8def;"
      "}");

  auto *tb = new QHBoxLayout(m_toolbar);
  tb->setContentsMargins(12, 6, 12, 6);
  tb->setSpacing(10);

  auto *periodHint = new QLabel(tr("周期"), m_toolbar);
  periodHint->setObjectName(QStringLiteral("tbHint"));
  m_periodCombo = new QComboBox(m_toolbar);
  m_periodCombo->setFixedWidth(92);
  m_periodCombo->setSizeAdjustPolicy(QComboBox::AdjustToContents);
  m_periodCombo->addItem(tr("今日分时"), 0);
  m_periodCombo->addItem(tr("按日期"), 1);
  connect(m_periodCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
          this, &ChartWindow::onPeriodChanged);

  m_dateFromEdit = new QDateEdit(QDate::currentDate(), m_toolbar);
  m_dateToEdit = new QDateEdit(QDate::currentDate(), m_toolbar);
  for (QDateEdit* de : {m_dateFromEdit, m_dateToEdit}) {
    de->setCalendarPopup(true);
    de->setDisplayFormat(QStringLiteral("yyyy-MM-dd"));
    de->setMinimumDate(QDate(2015, 1, 1));
    de->setMaximumDate(QDate::currentDate());
    de->setFixedWidth(116);
    de->setEnabled(false);
  }
  connect(m_dateFromEdit, &QDateEdit::dateChanged, this, [this](QDate d) {
    if (m_dateToEdit && m_dateToEdit->date() < d)
      m_dateToEdit->setDate(d);
  });
  connect(m_dateToEdit, &QDateEdit::dateChanged, this, [this](QDate d) {
    if (m_dateFromEdit && m_dateFromEdit->date() > d)
      m_dateFromEdit->setDate(d);
  });
  m_queryPeriodBtn = new QPushButton(tr("查询"), m_toolbar);
  m_queryPeriodBtn->setObjectName(QStringLiteral("periodQuery"));
  m_queryPeriodBtn->setCursor(Qt::PointingHandCursor);
  m_queryPeriodBtn->setEnabled(false);
  m_queryPeriodBtn->setToolTip(tr("起止至少 1 天：同一天显示分时；跨天显示日线收盘"));
  connect(m_queryPeriodBtn, &QPushButton::clicked, this, &ChartWindow::applyPeriodSelection);

  // 均线：胶囊式可勾选按钮（替代下拉菜单）
  auto makeMaPill = [this](const QString& text, QAction** actionOut) {
    auto* btn = new QToolButton(m_toolbar);
    btn->setObjectName(QStringLiteral("maPill"));
    btn->setText(text);
    btn->setCheckable(true);
    btn->setChecked(false);
    btn->setToolButtonStyle(Qt::ToolButtonTextOnly);
    btn->setCursor(Qt::PointingHandCursor);
    auto* act = new QAction(text, this);
    act->setCheckable(true);
    act->setChecked(false);
    connect(btn, &QToolButton::toggled, act, &QAction::setChecked);
    connect(act, &QAction::toggled, btn, &QToolButton::setChecked);
    connect(act, &QAction::toggled, this, &ChartWindow::onMaOptionChanged);
    *actionOut = act;
    return btn;
  };
  m_ma5Btn = makeMaPill(tr("MA5"), &m_ma5Action);
  m_ma10Btn = makeMaPill(tr("MA10"), &m_ma10Action);
  m_ma20Btn = makeMaPill(tr("MA20"), &m_ma20Action);
  m_ma5Btn->setToolTip(tr("5日均线（日线或分时滚动）"));
  m_ma10Btn->setToolTip(tr("10日均线"));
  m_ma20Btn->setToolTip(tr("20日均线"));
  m_maMenuBtn = nullptr; // 旧下拉不再使用

  auto *maHint = new QLabel(tr("均线"), m_toolbar);
  maHint->setObjectName(QStringLiteral("tbHint"));
  auto *maGroup = new QHBoxLayout();
  maGroup->setSpacing(6);
  maGroup->setContentsMargins(0, 0, 0, 0);
  maGroup->addWidget(m_ma5Btn);
  maGroup->addWidget(m_ma10Btn);
  maGroup->addWidget(m_ma20Btn);

  m_exportCsvBtn = new QPushButton(tr("导出 CSV"), m_toolbar);
  m_exportCsvBtn->setObjectName(QStringLiteral("exportCsv"));
  m_exportCsvBtn->setCursor(Qt::PointingHandCursor);
  m_exportCsvBtn->setToolTip(tr("导出当前曲线点到 CSV 文件"));
  connect(m_exportCsvBtn, &QPushButton::clicked, this, &ChartWindow::onExportCsv);

  tb->addWidget(periodHint);
  tb->addWidget(m_periodCombo);
  tb->addWidget(m_dateFromEdit);
  auto *dash = new QLabel(QStringLiteral("—"), m_toolbar);
  dash->setObjectName(QStringLiteral("tbHint"));
  tb->addWidget(dash);
  tb->addWidget(m_dateToEdit);
  tb->addWidget(m_queryPeriodBtn);
  tb->addSpacing(8);
  auto *vdiv1 = new QFrame(m_toolbar);
  vdiv1->setFrameShape(QFrame::VLine);
  vdiv1->setStyleSheet("color:#2a3347;max-width:1px;background:#2a3347;");
  tb->addWidget(vdiv1);
  tb->addWidget(maHint);
  tb->addLayout(maGroup);
  tb->addStretch(1);
  tb->addWidget(m_exportCsvBtn);

  // 主布局：上工具栏 + 曲线 + 右侧信息栏
  auto *root = new QVBoxLayout(this);
  root->setContentsMargins(8, 8, 8, 8);
  root->setSpacing(8);
  root->addWidget(m_toolbar);

  auto *body = new QHBoxLayout();
  body->setSpacing(8);
  body->setContentsMargins(0, 0, 0, 0);
  // 左右等高：子控件垂直方向默认拉伸至行高
  body->setAlignment(Qt::AlignVCenter);

  m_chartView->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  body->addWidget(m_chartView, 1);

  m_sidePanel = new QFrame(this);
  m_sidePanel->setFixedWidth(270);
  m_sidePanel->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
  m_sidePanel->setMinimumHeight(0);
  m_sidePanel->setStyleSheet(
      "QFrame#sidePanel{"
      "  background:#12161f;"
      "  border:1px solid #2a3347;"
      "  border-radius:12px;"
      "}"
      "QLabel{background:transparent;}");
  m_sidePanel->setObjectName(QStringLiteral("sidePanel"));
  auto *sideLay = new QVBoxLayout(m_sidePanel);
  sideLay->setContentsMargins(10, 10, 10, 10);
  sideLay->setSpacing(6);

  // 顶栏：时间 + 交易时段
  auto *head = new QHBoxLayout();
  head->setSpacing(6);
  head->setContentsMargins(0, 0, 0, 0);
  m_sideClockLabel = new QLabel(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss")), m_sidePanel);
  m_sideClockLabel->setStyleSheet(
      "color:#5b8def;font-size:13px;font-weight:600;font-family:Consolas,monospace;");
  m_sideClockLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
  m_sideSessionLabel = new QLabel(tr("—"), m_sidePanel);
  m_sideSessionLabel->setStyleSheet(
      "color:#a8b3c7;font-size:11px;padding:2px 8px;"
      "background:#1c2433;border-radius:8px;");
  m_sideSessionLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
  head->addWidget(m_sideClockLabel, 1);
  head->addWidget(m_sideSessionLabel, 0);
  sideLay->addLayout(head);

  // 趋势状态 + 命中率（类似 趋势: 状态 | 命中: 85%）
  auto *statRow = new QHBoxLayout();
  statRow->setSpacing(0);
  statRow->setContentsMargins(0, 0, 0, 0);

  auto *trendBox = new QHBoxLayout();
  trendBox->setSpacing(4);
  trendBox->setContentsMargins(0, 0, 0, 0);
  trendBox->setAlignment(Qt::AlignCenter);
  auto *tTitle = new QLabel(tr("趋势:"), m_sidePanel);
  tTitle->setStyleSheet("color:#8592a6;font-size:11px;");
  m_sideTrendLabel = new QLabel(tr("—"), m_sidePanel);
  m_sideTrendLabel->setStyleSheet("color:#e8eaed;font-size:12px;font-weight:600;");
  trendBox->addWidget(tTitle);
  trendBox->addWidget(m_sideTrendLabel);

  auto *statDivider = new QLabel(QStringLiteral("|"), m_sidePanel);
  statDivider->setStyleSheet("color:#3a4659;font-size:12px;font-weight:normal;");
  statDivider->setAlignment(Qt::AlignCenter);
  statDivider->setFixedWidth(16);

  auto *hitBox = new QHBoxLayout();
  hitBox->setSpacing(4);
  hitBox->setContentsMargins(0, 0, 0, 0);
  hitBox->setAlignment(Qt::AlignCenter);
  auto *hitTitle = new QLabel(tr("命中:"), m_sidePanel);
  hitTitle->setStyleSheet("color:#8592a6;font-size:11px;");
  m_sideHitRateLabel = new QLabel(tr("--"), m_sidePanel);
  m_sideHitRateLabel->setStyleSheet("color:#7eb6ff;font-size:12px;font-weight:600;");
  hitBox->addWidget(hitTitle);
  hitBox->addWidget(m_sideHitRateLabel);

  statRow->addLayout(trendBox, 1);
  statRow->addWidget(statDivider, 0);
  statRow->addLayout(hitBox, 1);
  sideLay->addLayout(statRow);

  auto sep = [m_sidePanel = m_sidePanel]() {
    auto *line = new QFrame(m_sidePanel);
    line->setFrameShape(QFrame::HLine);
    line->setStyleSheet("color:#2a3347;max-height:1px;background:#2a3347;");
    return line;
  };
  sideLay->addWidget(sep());

  // 行情实况：现价大字居中，今高 | 今低 居中并列
  auto *curRow = new QHBoxLayout();
  curRow->setSpacing(6);
  curRow->setContentsMargins(0, 2, 0, 2);
  curRow->setAlignment(Qt::AlignCenter);
  auto *curTitle = new QLabel(tr("现价:"), m_sidePanel);
  curTitle->setStyleSheet("color:#8592a6;font-size:12px;font-weight:600;");
  m_sideCurrentLabel = new QLabel(QStringLiteral("--.--"), m_sidePanel);
  m_sideCurrentLabel->setStyleSheet("color:#ffffff;font-size:18px;font-weight:700;font-family:Consolas,monospace;");
  m_sideCurrentLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
  curRow->addWidget(curTitle);
  curRow->addWidget(m_sideCurrentLabel);
  sideLay->addLayout(curRow);

  auto *hlRow = new QHBoxLayout();
  hlRow->setSpacing(0);
  hlRow->setContentsMargins(0, 2, 0, 2);

  auto *highBox = new QHBoxLayout();
  highBox->setSpacing(4);
  highBox->setContentsMargins(0, 0, 0, 0);
  highBox->setAlignment(Qt::AlignCenter);
  auto *hTitle = new QLabel(tr("今高:"), m_sidePanel);
  hTitle->setStyleSheet("color:#8592a6;font-size:11px;font-weight:500;");
  m_sideHighLabel = new QLabel(QStringLiteral("--.--"), m_sidePanel);
  m_sideHighLabel->setStyleSheet("color:#f07178;font-size:13px;font-weight:600;font-family:Consolas,monospace;");
  m_sideHighLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
  highBox->addWidget(hTitle);
  highBox->addWidget(m_sideHighLabel);

  auto *hlDivider = new QLabel(QStringLiteral("|"), m_sidePanel);
  hlDivider->setStyleSheet("color:#3a4659;font-size:12px;font-weight:normal;");
  hlDivider->setAlignment(Qt::AlignCenter);
  hlDivider->setFixedWidth(16);

  auto *lowBox = new QHBoxLayout();
  lowBox->setSpacing(4);
  lowBox->setContentsMargins(0, 0, 0, 0);
  lowBox->setAlignment(Qt::AlignCenter);
  auto *lTitle = new QLabel(tr("今低:"), m_sidePanel);
  lTitle->setStyleSheet("color:#8592a6;font-size:11px;font-weight:500;");
  m_sideLowLabel = new QLabel(QStringLiteral("--.--"), m_sidePanel);
  m_sideLowLabel->setStyleSheet("color:#7fd99a;font-size:13px;font-weight:600;font-family:Consolas,monospace;");
  m_sideLowLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
  lowBox->addWidget(lTitle);
  lowBox->addWidget(m_sideLowLabel);

  hlRow->addLayout(highBox, 1);
  hlRow->addWidget(hlDivider, 0);
  hlRow->addLayout(lowBox, 1);
  sideLay->addLayout(hlRow);

  sideLay->addWidget(sep());

  // AI 极值预测专属深色卡片
  m_sideForecastCard = new QFrame(m_sidePanel);
  m_sideForecastCard->setObjectName(QStringLiteral("forecastCard"));
  m_sideForecastCard->setStyleSheet(
      "QFrame#forecastCard{"
      "  background:#151a26;"
      "  border:1px solid #28374d;"
      "  border-radius:8px;"
      "}");
  auto *cardLay = new QVBoxLayout(m_sideForecastCard);
  cardLay->setContentsMargins(8, 8, 8, 8);
  cardLay->setSpacing(6);

  // Card Header: 标签 + 算法模型标识
  auto *cardHead = new QHBoxLayout();
  cardHead->setSpacing(4);
  cardHead->setContentsMargins(0, 0, 0, 0);
  auto *aiTitle = new QLabel(tr("🤖 AI 极值推演"), m_sideForecastCard);
  aiTitle->setStyleSheet("color:#7eb6ff;font-size:11px;font-weight:600;");
  m_sideModeLabel = new QLabel(tr("本地推演"), m_sideForecastCard);
  m_sideModeLabel->setStyleSheet(
      "color:#8b9bb4;font-size:10px;padding:1px 6px;"
      "background:#1e2638;border-radius:4px;border:1px solid #2f3d54;");
  m_sideModeLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
  cardHead->addWidget(aiTitle, 1);
  cardHead->addWidget(m_sideModeLabel, 0);
  cardLay->addLayout(cardHead);

  // 预高与预低并排居中：🔺预高: 价格 | 🔻预低: 价格
  auto *predHlRow = new QHBoxLayout();
  predHlRow->setSpacing(0);
  predHlRow->setContentsMargins(0, 2, 0, 2);

  auto *predHBox = new QHBoxLayout();
  predHBox->setSpacing(4);
  predHBox->setContentsMargins(0, 0, 0, 0);
  predHBox->setAlignment(Qt::AlignCenter);
  auto *phTitle = new QLabel(tr("🔺预高:"), m_sideForecastCard);
  phTitle->setStyleSheet("color:#ffa08a;font-size:11px;font-weight:600;");
  m_sidePredictHighLabel = new QLabel(QStringLiteral("--.--"), m_sideForecastCard);
  m_sidePredictHighLabel->setStyleSheet("color:#ff8b7a;font-size:13px;font-weight:bold;font-family:Consolas,monospace;");
  m_sidePredictHighLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
  predHBox->addWidget(phTitle);
  predHBox->addWidget(m_sidePredictHighLabel);

  auto *predDivider = new QLabel(QStringLiteral("|"), m_sideForecastCard);
  predDivider->setStyleSheet("color:#3a4659;font-size:12px;font-weight:normal;");
  predDivider->setAlignment(Qt::AlignCenter);
  predDivider->setFixedWidth(16);

  auto *predLBox = new QHBoxLayout();
  predLBox->setSpacing(4);
  predLBox->setContentsMargins(0, 0, 0, 0);
  predLBox->setAlignment(Qt::AlignCenter);
  auto *plTitle = new QLabel(tr("🔻预低:"), m_sideForecastCard);
  plTitle->setStyleSheet("color:#80e0a0;font-size:11px;font-weight:600;");
  m_sidePredictLowLabel = new QLabel(QStringLiteral("--.--"), m_sideForecastCard);
  m_sidePredictLowLabel->setStyleSheet("color:#6bcb8a;font-size:13px;font-weight:bold;font-family:Consolas,monospace;");
  m_sidePredictLowLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
  predLBox->addWidget(plTitle);
  predLBox->addWidget(m_sidePredictLowLabel);

  predHlRow->addLayout(predHBox, 1);
  predHlRow->addWidget(predDivider, 0);
  predHlRow->addLayout(predLBox, 1);
  cardLay->addLayout(predHlRow);

  m_sidePredictLabel = m_sidePredictHighLabel;

  // 极值时段：两行清晰对齐，左紧贴标签，右自然排版
  auto makeWindowRow = [this](const QString &titleText, const QString &colorHex, QLabel **valOut) {
    auto *row = new QHBoxLayout();
    row->setSpacing(6);
    row->setContentsMargins(0, 0, 0, 0);
    auto *t = new QLabel(titleText + QStringLiteral(":"), m_sideForecastCard);
    t->setStyleSheet("color:#8592a6;font-size:11px;font-weight:500;");
    t->setFixedWidth(56);
    auto *v = new QLabel(QStringLiteral("—"), m_sideForecastCard);
    v->setStyleSheet(QStringLiteral(
        "color:%1;font-size:11px;font-weight:600;"
        "font-family:'Segoe UI','Microsoft YaHei UI',sans-serif;").arg(colorHex));
    v->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    v->setWordWrap(true);
    v->setTextInteractionFlags(Qt::TextSelectableByMouse);
    row->addWidget(t, 0);
    row->addWidget(v, 1);
    *valOut = v;
    return row;
  };

  cardLay->addLayout(makeWindowRow(tr("高点时段"), "#ffa08a", &m_sidePredictHighTimeLabel));
  cardLay->addLayout(makeWindowRow(tr("低点时段"), "#80e0a0", &m_sidePredictLowTimeLabel));

  // 关键催化：圆角背景框，支持自动折行与 ToolTip
  auto *catHeader = new QLabel(tr("💡 关键催化"), m_sideForecastCard);
  catHeader->setStyleSheet("color:#faad14;font-size:11px;font-weight:600;margin-top:2px;");
  cardLay->addWidget(catHeader);

  m_sideCatalystLabel = new QLabel(QStringLiteral("—"), m_sideForecastCard);
  m_sideCatalystLabel->setObjectName(QStringLiteral("catalystLabel"));
  m_sideCatalystLabel->setStyleSheet(
      "QLabel#catalystLabel{"
      "  color:#ffe58f;font-size:11px;line-height:1.3;"
      "  background:#1a2233;border:1px solid #2f3d54;border-radius:4px;"
      "  padding:4px 6px;"
      "}");
  m_sideCatalystLabel->setWordWrap(true);
  m_sideCatalystLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
  m_sideCatalystLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
  cardLay->addWidget(m_sideCatalystLabel);

  sideLay->addWidget(m_sideForecastCard);

  // 今日推演记录列表
  auto *histTitle = new QLabel(tr("今日推演记录"), m_sidePanel);
  histTitle->setStyleSheet("color:#6b778c;font-size:11px;font-weight:600;margin-top:2px;");
  sideLay->addWidget(histTitle);

  m_sideForecastList = new QListWidget(m_sidePanel);
  m_sideForecastList->setObjectName(QStringLiteral("forecastList"));
  m_sideForecastList->setStyleSheet(
      "QListWidget#forecastList{"
      "  background:#0e1219;border:1px solid #2a3347;border-radius:8px;"
      "  color:#c5cddb;font-size:11px;outline:none;"
      "}"
      "QListWidget#forecastList::item{"
      "  padding:5px 6px;border-bottom:1px solid #1a2233;"
      "}"
      "QListWidget#forecastList::item:hover{"
      "  background:#141c2b;"
      "}"
      "QListWidget#forecastList::item:selected{"
      "  background:#1c2a40;color:#e8eaed;"
      "}");
  m_sideForecastList->setWordWrap(true);
  m_sideForecastList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  m_sideForecastList->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  m_sideForecastList->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
  m_sideForecastList->setSpacing(2);
  m_sideForecastList->setMinimumHeight(110);
  m_sideForecastList->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  m_sideForecastList->setToolTip(
      tr("滚动查看今日极值推演与催化记录；最新在底部"));
  sideLay->addWidget(m_sideForecastList, 1);

  m_sideAdviceLabel = nullptr;
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
    if (ah > 0.0 && al > 0.0) {
      ForecastTracker::instance().evaluateDayRange(ah, al);
      ExtremeDatabase::instance().settleForecasts(currentTypeCode(), ah, al);
      ForecastTracker::instance().loadFromDatabase(currentTypeCode());
    } else {
      ForecastTracker::instance().evaluateWithActual(price, m_plotPoints);
    }
  }

  updateSidePanelValues(
      price > 0 ? price
                : (m_plotPoints.isEmpty() ? 0.0 : m_plotPoints.last().second),
      m_lastPredictPrice, m_hasPredict, high, low, m_forecastModeTag);

  const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
  {
    qint64 intervalMs =
        qMax(15000LL, static_cast<qint64>(AppSettings::instance().forecastIntervalSec()) * 1000LL);
    // 宏观高影响日：拉长请求间隔，降低噪声
    if (EventCalendar::isHighImpactDay())
        intervalMs = qMax(intervalMs * 2, 120000LL);
    if (m_lastForecastMs == 0 || (nowMs - m_lastForecastMs) >= intervalMs) {
        updateForecast();
    }
  }
  // 交易计划触达（节流：用 tip 文本变化即可，托盘由主窗处理可选）
  if (AppSettings::instance().planEnabled() && price > 0.0) {
    const auto& ps = AppSettings::instance();
    if (ps.planInvalidPrice() > 0.0 && price <= ps.planInvalidPrice()) {
      if (m_sideTrendLabel)
        m_sideTrendLabel->setToolTip(
            m_sideTrendLabel->toolTip() + tr(" | 已触及计划失效价"));
    }
  }

  updateClockAndAdvice();
}


void ChartWindow::setCurrentMarker(qint64 xMs, double y, bool startPulse)
{
  if (!m_currentSeries)
    return;
  m_currentSeries->clear();
  m_currentSeries->append(xMs, y);
  m_currentSeries->setVisible(true);
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
  // 等待下一价时末点呼吸闪烁：由 9px ~ 13px 交替，边缘高亮白光
  m_currentSeries->setMarkerSize(m_pulseOn ? 13.0 : 9.0);
  if (m_pulseOn) {
    m_currentSeries->setColor(QColor(255, 77, 79));
    m_currentSeries->setBorderColor(QColor(255, 255, 255));
  } else {
    m_currentSeries->setColor(QColor(255, 120, 120));
    m_currentSeries->setBorderColor(QColor(255, 220, 220));
  }
}

void ChartWindow::showEvent(QShowEvent *event) {
  reloadForecastHistory();
  ForecastTracker::instance().loadFromDatabase(currentTypeCode());

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

    std::vector<goldsdk::IntradayPoint> pts;
    pts.reserve(static_cast<size_t>(m_plotPoints.size()));
    for (const auto& p : m_plotPoints) {
        goldsdk::IntradayPoint ip;
        ip.epochMs = p.first.toMSecsSinceEpoch();
        ip.price = p.second;
        pts.push_back(ip);
    }
    double actHigh = 0.0, actLow = 0.0;
    HistoryCache::instance().todayHigh(actHigh);
    HistoryCache::instance().todayLow(actLow);
    const QTime nowT = QTime::currentTime();
    // 交易时段进度：积存金 09:00–23:30；伦敦金近似 0–24
    double dayFrac = 0.0;
    {
        const QString code = currentTypeCode();
        if (code == QStringLiteral("gj")) {
            dayFrac = nowT.msecsSinceStartOfDay() / (24.0 * 3600.0 * 1000.0);
        } else {
            const int startM = 9 * 60;
            const int endM = 23 * 60 + 30;
            const int nowM = nowT.hour() * 60 + nowT.minute();
            if (nowM <= startM)
                dayFrac = 0.0;
            else if (nowM >= endM)
                dayFrac = 1.0;
            else
                dayFrac = static_cast<double>(nowM - startM) / static_cast<double>(endM - startM);
        }
    }
    const double atr = ExtremeDatabase::instance().computeAtr(10, currentTypeCode());
    const double prevClose = ExtremeDatabase::instance().previousClose(currentTypeCode());
    const auto fr = goldsdk::ForecastEngine::dayRange(pts, actHigh, actLow, dayFrac, atr, prevClose);
    if (!fr.valid)
        return false;
    outPredHigh = fr.predHigh;
    outPredLow = fr.predLow;
    return true;
}

void ChartWindow::updateForecast() {
  if (!isIntradayMode() || m_plotPoints.isEmpty()) {
    m_hasPredict = false;
    return;
  }
  ForecastService::instance().requestForecast(currentTypeCode());
}

void ChartWindow::applyForecastPoints(
    const QVector<QPair<QDateTime, double>> &forecast, const QString &modeTag) {
  Q_UNUSED(forecast);
  Q_UNUSED(modeTag);
}


void ChartWindow::appendForecastHistoryItem(const QDateTime& when, const QString& mode,
                                            const QString& brief, double ph, double pl,
                                            const QString& highTime, const QString& lowTime,
                                            const QString& catalyst)
{
    if (!m_sideForecastList)
        return;

    const QString timeStr = when.isValid() ? when.toString(QStringLiteral("HH:mm"))
                                           : QDateTime::currentDateTime().toString(QStringLiteral("HH:mm"));
    const QString normHTime = ForecastService::normalizeTo24HourTime(highTime);
    const QString normLTime = ForecastService::normalizeTo24HourTime(lowTime);

    QString text = QStringLiteral("🕒 %1 [%2] 预高: %3 | 预低: %4\n极值时段: 高 %5 | 低 %6")
                       .arg(timeStr)
                       .arg(mode)
                       .arg(ph, 0, 'f', 2)
                       .arg(pl, 0, 'f', 2)
                       .arg(normHTime.isEmpty() ? QStringLiteral("—") : normHTime)
                       .arg(normLTime.isEmpty() ? QStringLiteral("—") : normLTime);

    if (!catalyst.trimmed().isEmpty()) {
        text += QStringLiteral("\n催化: %1").arg(catalyst.trimmed());
    } else if (!brief.trimmed().isEmpty() && brief != mode) {
        text += QStringLiteral("\n简评: %1").arg(brief.trimmed());
    }

    const QString tip = QStringLiteral(
        "【推演时间】%1\n"
        "【模型通道】%2\n"
        "【预期最高】%3 (高点时段: %4)\n"
        "【预期最低】%5 (低点时段: %6)\n"
        "【核心催化】%7\n"
        "【情景简述】%8")
        .arg(when.isValid() ? when.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")) : timeStr)
        .arg(mode)
        .arg(ph, 0, 'f', 2)
        .arg(normHTime.isEmpty() ? QStringLiteral("未指定") : normHTime)
        .arg(pl, 0, 'f', 2)
        .arg(normLTime.isEmpty() ? QStringLiteral("未指定") : normLTime)
        .arg(catalyst.isEmpty() ? QStringLiteral("无") : catalyst)
        .arg(brief.isEmpty() ? QStringLiteral("无") : brief);

    auto* item = new QListWidgetItem(text);
    item->setToolTip(tip);
    m_sideForecastList->addItem(item);
    m_sideForecastList->scrollToItem(item, QAbstractItemView::PositionAtBottom);
    m_sideForecastList->setCurrentItem(item);
}

void ChartWindow::reloadForecastHistory()
{
    if (!m_sideForecastList)
        return;
    m_sideForecastList->clear();
    const auto logs = ExtremeDatabase::instance().loadForecastLogsForDay(
        QDate::currentDate(), currentTypeCode());
    for (const auto& e : logs) {
        QString brief = e.brief;
        if (brief.isEmpty())
            brief = e.mode;
        appendForecastHistoryItem(e.madeAt, e.mode, brief, e.predHigh, e.predLow,
                                  e.predHighTime, e.predLowTime, e.catalyst);
    }
    if (m_sideForecastList->count() > 0) {
        auto* last = m_sideForecastList->item(m_sideForecastList->count() - 1);
        m_sideForecastList->scrollToItem(last, QAbstractItemView::PositionAtBottom);
        m_sideForecastList->setCurrentItem(last);
    }
}

void ChartWindow::updateSidePanelValues(double current, double predict,
                                        bool hasPredict, double high,
                                        double low, const QString &modeTag) {
  auto setNum = [](QLabel *lab, double v) {
    if (!lab)
      return;
    lab->setText(v > 0.0 ? QString::number(v, 'f', 2) : QStringLiteral("--.--"));
  };
  setNum(m_sideCurrentLabel, current);
  setNum(m_sideHighLabel, high);
  setNum(m_sideLowLabel, low);

  if (hasPredict && m_lastPredictHigh > 0.0 && m_lastPredictLow > 0.0) {
    setNum(m_sidePredictHighLabel, m_lastPredictHigh);
    setNum(m_sidePredictLowLabel, m_lastPredictLow);
  } else if (hasPredict && predict > 0.0) {
    setNum(m_sidePredictHighLabel, predict);
    if (m_sidePredictLowLabel)
      m_sidePredictLowLabel->setText(QStringLiteral("--.--"));
  } else {
    setNum(m_sidePredictHighLabel, 0);
    setNum(m_sidePredictLowLabel, 0);
  }

  if (m_sideModeLabel) {
    const QString tag = modeTag.isEmpty() ? tr("本地推演") : modeTag;
    m_sideModeLabel->setText(tag);
    m_sideModeLabel->setToolTip(tag);
  }

  // 预测变化时同步刷新图表标题（今高/今低 + 预测高/低）
  if (isIntradayMode())
    refreshIntradayTitle();
}

void ChartWindow::refreshIntradayTitle()
{
  if (!m_chart || !isIntradayMode())
    return;
  double high = 0.0, low = 0.0;
  HistoryCache::instance().todayHigh(high);
  HistoryCache::instance().todayLow(low);
  const QString typeName =
      currentTypeCode() == QStringLiteral("gj")
          ? tr("伦敦金")
          : (currentTypeCode() == QStringLiteral("ms") ? tr("民生") : tr("浙商"));
  const int n = m_plotPoints.size();
  QString predText;
  if (m_hasPredict && m_lastPredictHigh > 0.0 && m_lastPredictLow > 0.0) {
    predText = tr("  |  预高: %1 | 预低: %2")
                   .arg(m_lastPredictHigh, 0, 'f', 2)
                   .arg(m_lastPredictLow, 0, 'f', 2);
  }
  m_chart->setTitle(tr("%1 · 今日分时（%2点）  今高: %3 | 今低: %4%5")
                        .arg(typeName)
                        .arg(n)
                        .arg(high, 0, 'f', 2)
                        .arg(low, 0, 'f', 2)
                        .arg(predText));
}

void ChartWindow::applyLocalForecastLines(double predHigh, double predLow)
{
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
}

void ChartWindow::onForecastServiceUpdated(const ForecastResult& res)
{
    if (!res.valid)
        return;

    m_lastPredictHigh = res.predHigh;
    m_lastPredictLow = res.predLow;
    m_lastPredictPrice = res.predHigh;
    m_hasPredict = true;
    m_forecastModeTag = res.modeTag;
    m_lastForecastMs = res.timestamp.toMSecsSinceEpoch();

    applyLocalForecastLines(res.predHigh, res.predLow);

    if (m_sidePredictHighLabel)
        m_sidePredictHighLabel->setText(QString::number(res.predHigh, 'f', 2));
    if (m_sidePredictLowLabel)
        m_sidePredictLowLabel->setText(QString::number(res.predLow, 'f', 2));
    if (m_sideModeLabel)
        m_sideModeLabel->setText(res.modeTag.isEmpty() ? tr("AI推演") : res.modeTag);
    if (m_sidePredictHighTimeLabel) {
        const QString ht = ForecastService::normalizeTo24HourTime(res.predHighTimeWindow);
        m_sidePredictHighTimeLabel->setText(ht);
        m_sidePredictHighTimeLabel->setToolTip(ht);
    }
    if (m_sidePredictLowTimeLabel) {
        const QString lt = ForecastService::normalizeTo24HourTime(res.predLowTimeWindow);
        m_sidePredictLowTimeLabel->setText(lt);
        m_sidePredictLowTimeLabel->setToolTip(lt);
    }
    if (m_sideCatalystLabel) {
        const QString cat = res.keyCatalyst.isEmpty() ? res.scenario : res.keyCatalyst;
        m_sideCatalystLabel->setText(cat);
        m_sideCatalystLabel->setToolTip(res.scenario.isEmpty() ? cat : (cat + QStringLiteral("\n演变: ") + res.scenario));
    }
    if (m_sideTrendLabel) {
        QString trend = res.multiDayBias.isEmpty() ? res.bias : res.multiDayBias;
        if (res.highAlreadyInProb >= 0.70) {
            trend += QStringLiteral(" · 高点已现%1%").arg(res.highAlreadyInProb * 100.0, 0, 'f', 0);
        } else if (res.lowAlreadyInProb >= 0.70) {
            trend += QStringLiteral(" · 低点企稳%1%").arg(res.lowAlreadyInProb * 100.0, 0, 'f', 0);
        } else if (res.peakWindowProb > 0.0) {
            trend += QStringLiteral(" · 冲高%1%").arg(res.peakWindowProb * 100.0, 0, 'f', 0);
        }
        m_sideTrendLabel->setText(trend);
        m_sideTrendLabel->setToolTip(
            tr("多日:%1\n高点窗:%2\n已现概率:%3% 剩余上行:%4 置信:%5%")
                .arg(res.multiDayBias.isEmpty() ? res.bias : res.multiDayBias)
                .arg(res.predHighTimeWindow)
                .arg(res.highAlreadyInProb * 100.0, 0, 'f', 0)
                .arg(res.remainingUpside, 0, 'f', 2)
                .arg(res.confidence * 100.0, 0, 'f', 0));
    }

    appendForecastHistoryItem(res.timestamp, res.modeTag, res.brief, res.predHigh, res.predLow,
                              res.predHighTimeWindow, res.predLowTimeWindow, res.keyCatalyst);

    double high = 0.0, low = 0.0;
    HistoryCache::instance().todayHigh(high);
    HistoryCache::instance().todayLow(low);
    const double cur = m_plotPoints.isEmpty() ? 0.0 : m_plotPoints.last().second;
    updateSidePanelValues(cur, res.predHigh, true, high, low, res.modeTag);
}

void ChartWindow::updateHighLowMarkers() {
  m_highSeries->clear();
  m_lowSeries->clear();

  if (m_plotPoints.isEmpty() || !m_chart)
    return;

  // 与 updateSeries 相同：在分时点序列上找极值，保证与折线拐点重合
  int highIdx = 0, lowIdx = 0;
  for (int i = 1; i < m_plotPoints.size(); ++i) {
    const double v = m_plotPoints.at(i).second;
    if (v > m_plotPoints.at(highIdx).second)
      highIdx = i;
    if (v < m_plotPoints.at(lowIdx).second)
      lowIdx = i;
  }

  const auto &hp = m_plotPoints.at(highIdx);
  const auto &lp = m_plotPoints.at(lowIdx);
  const qreal hx = static_cast<qreal>(hp.first.toMSecsSinceEpoch());
  const qreal lx = static_cast<qreal>(lp.first.toMSecsSinceEpoch());
  m_highSeries->append(hx, hp.second);
  m_lowSeries->append(lx, lp.second);

  // 坐标轴在 setupChart 已绑定，勿重复 attach（会刷屏警告）
  m_highSeries->setVisible(true);
  m_lowSeries->setVisible(true);

  // 侧栏高低与标记同源（分时点极值），避免与 HistoryCache 时间轴不一致
  const double cur = m_plotPoints.last().second;
  updateSidePanelValues(cur, m_lastPredictPrice, m_hasPredict, hp.second,
                        lp.second, m_forecastModeTag);
}

void ChartWindow::updateCurrentMarker() {
  if (!m_currentSeries || m_plotPoints.isEmpty() || !m_chart || !isIntradayMode())
    return;

  const auto &cur = m_plotPoints.last();
  const qint64 x = cur.first.toMSecsSinceEpoch();
  const double y = cur.second;

  m_markerXMs = x;
  m_targetY = y;
  m_smoothY = y;
  m_hasMarker = true;
  if (m_series)
    m_seriesLastIndex = m_series->count() - 1;

  setCurrentMarker(x, y, true);
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
      AppSettings::instance().chartUrl()
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
  if (!m_chart || !m_series || !m_axisX || !m_axisY) {
    Logger::warn(QStringLiteral("updateSeries: chart not ready"));
    return;
  }
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
    if (m_ma10Series) m_ma10Series->clear();
    if (m_ma20Series) m_ma20Series->clear();
    if (m_yesterdaySeries) m_yesterdaySeries->clear();
    return;
  }

  const int n = m_plotPoints.size();

  // 以中位价为基准剔除异常点（单位混入 USD/oz 或错误点会导致 605~1238 把分时压成一条线）
  QVector<double> vals;
  vals.reserve(n);
  for (const auto& pt : m_plotPoints) {
    if (pt.second > 0.0)
      vals.append(pt.second);
  }
  std::sort(vals.begin(), vals.end());
  const double median = vals.isEmpty() ? 0.0 : vals.at(vals.size() / 2);
  auto inBand = [median](double v) {
    if (v <= 0.0 || median <= 0.0)
      return false;
    // 积存金日内波幅通常很小；硬限制 ±8%，再加绝对下限
    const double lo = median * 0.92;
    const double hi = median * 1.08;
    return v >= lo && v <= hi;
  };

  // 全量数据上的真实最高/最低（仅统计带内点）
  int highIdx = -1;
  int lowIdx = -1;
  for (int i = 0; i < n; ++i) {
    const double v = m_plotPoints.at(i).second;
    if (!inBand(v))
      continue;
    if (highIdx < 0 || v > m_plotPoints.at(highIdx).second)
      highIdx = i;
    if (lowIdx < 0 || v < m_plotPoints.at(lowIdx).second)
      lowIdx = i;
  }
  if (highIdx < 0) {
    highIdx = 0;
    lowIdx = 0;
    for (int i = 1; i < n; ++i) {
      if (m_plotPoints.at(i).second > m_plotPoints.at(highIdx).second)
        highIdx = i;
      if (m_plotPoints.at(i).second < m_plotPoints.at(lowIdx).second)
        lowIdx = i;
    }
  }

  qreal minPrice = m_plotPoints.at(lowIdx).second;
  qreal maxPrice = m_plotPoints.at(highIdx).second;

  int step = 1;
  if (n > 1000)
    step = n / 500;
  else if (n > 500)
    step = 2;

  // 降采样时强制保留最高/最低点
  QSet<int> keep;
  keep.reserve(n / step + 8);
  for (int i = 0; i < n; i += step) {
    if (inBand(m_plotPoints.at(i).second) || i == 0 || i == n - 1)
      keep.insert(i);
  }
  keep.insert(n - 1);
  keep.insert(highIdx);
  keep.insert(lowIdx);
  QList<int> order = keep.values();
  std::sort(order.begin(), order.end());
  for (int i : order) {
    const auto& pt = m_plotPoints.at(i);
    m_series->append(pt.first.toMSecsSinceEpoch(), pt.second);
  }

  // 预测仅在贴近分时带内时纳入 Y 轴（防止离谱预测撑轴）
  double ph = 0.0, pl = 0.0;
  if (computeDayRangeForecast(ph, pl)) {
    const double span = qMax(0.5, maxPrice - minPrice);
    const double pad = qMax(span * 0.5, median * 0.01);
    if (pl >= minPrice - pad && pl <= maxPrice + pad)
      minPrice = qMin(minPrice, static_cast<qreal>(pl));
    if (ph >= minPrice - pad && ph <= maxPrice + pad)
      maxPrice = qMax(maxPrice, static_cast<qreal>(ph));
  }
  // 若已有侧栏预测值，同样做带内裁剪
  if (m_hasPredict) {
    const double span = qMax(0.5, maxPrice - minPrice);
    const double pad = qMax(span * 0.5, median * 0.01);
    if (m_lastPredictLow > 0 && m_lastPredictLow >= minPrice - pad
        && m_lastPredictLow <= maxPrice + pad)
      minPrice = qMin(minPrice, static_cast<qreal>(m_lastPredictLow));
    if (m_lastPredictHigh > 0 && m_lastPredictHigh >= minPrice - pad
        && m_lastPredictHigh <= maxPrice + pad)
      maxPrice = qMax(maxPrice, static_cast<qreal>(m_lastPredictHigh));
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

  refreshIntradayTitle();
  reloadForecastHistory();

  updateMovingAverages();
  updateYesterdayOverlay();
  updateCurrentMarker();

  QTimer::singleShot(50, this, [this]() {
    if (!isVisible())
      return;
    updateForecast();
    updateHighLowMarkers();
    updateCurrentMarker();
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
    m_pendingChart->disconnect(this);
    m_pendingChart->abort();
    m_pendingChart->deleteLater();
    m_pendingChart.clear();
  }
  m_loading = false;
  QWidget::closeEvent(event);
}

void ChartWindow::fillPeriodCombo()
{
    // 周期下拉在构造时已填充（今日 / 按日期）
}

bool ChartWindow::isIntradayMode() const
{
    if (!m_periodCombo)
        return true;
    // 今日，或按日期且起止为同一天 → 分时
    if (m_periodCombo->currentData().toInt() == 0)
        return true;
    if (m_dateFromEdit && m_dateToEdit
        && m_dateFromEdit->date() == m_dateToEdit->date())
        return true;
    return false;
}

void ChartWindow::setForecastVisible(bool on)
{
    if (m_forecastSeries)
        m_forecastSeries->setVisible(on);
    if (m_forecastLowSeries)
        m_forecastLowSeries->setVisible(on);
    if (m_currentSeries)
        m_currentSeries->setVisible(on);
}

void ChartWindow::onPeriodChanged(int)
{
    const bool custom = m_periodCombo && m_periodCombo->currentData().toInt() == 1;
    if (m_dateFromEdit)
        m_dateFromEdit->setEnabled(custom);
    if (m_dateToEdit)
        m_dateToEdit->setEnabled(custom);
    if (m_queryPeriodBtn)
        m_queryPeriodBtn->setEnabled(custom);

    if (!custom) {
        if (m_dateFromEdit)
            m_dateFromEdit->setDate(QDate::currentDate());
        if (m_dateToEdit)
            m_dateToEdit->setDate(QDate::currentDate());
        setWindowTitle(tr("今日分时曲线"));
        setForecastVisible(true);
        if (m_axisX)
            m_axisX->setFormat(QStringLiteral("HH:mm"));
        fetchChartFromApi();
    }
}

void ChartWindow::applyPeriodSelection()
{
    if (!m_dateFromEdit || !m_dateToEdit)
        return;
    QDate from = m_dateFromEdit->date();
    QDate to = m_dateToEdit->date();
    if (!from.isValid() || !to.isValid())
        return;
    if (to < from)
        qSwap(from, to);
    // 最小 1 天（同一天）
    if (from == to) {
        setForecastVisible(from == QDate::currentDate());
        if (m_axisX)
            m_axisX->setFormat(QStringLiteral("HH:mm"));
        loadIntradayForDate(from);
    } else {
        setForecastVisible(false);
        if (m_axisX)
            m_axisX->setFormat(QStringLiteral("MM-dd"));
        loadDailyRange(from, to);
    }
}

void ChartWindow::loadIntradayForDate(const QDate& day)
{
    if (!day.isValid() || !m_series)
        return;
    setWindowTitle(tr("%1 分时").arg(day.toString(QStringLiteral("yyyy-MM-dd"))));

    m_series->clear();
    if (m_forecastSeries) m_forecastSeries->clear();
    if (m_forecastLowSeries) m_forecastLowSeries->clear();
    if (m_currentSeries) m_currentSeries->clear();
    if (m_highSeries) m_highSeries->clear();
    if (m_lowSeries) m_lowSeries->clear();
    hideCrosshair();
    m_hasPredict = false;

    if (day == QDate::currentDate()) {
        fetchChartFromApi();
        return;
    }

    auto raw = ExtremeDatabase::instance().loadIntradaySamples(day, currentTypeCode());
    if (raw.isEmpty())
        raw = ExtremeDatabase::instance().loadIntradaySamples(day, QStringLiteral("zs"));
    if (raw.isEmpty())
        raw = ExtremeDatabase::instance().loadIntradaySamples(day, QStringLiteral("gj"));

    if (raw.isEmpty()) {
        if (m_chart)
            m_chart->setTitle(
                tr("%1 · 无本地分时样本\n"
                   "（仅软件运行期间写入；公开接口无法补历史分钟线）")
                    .arg(day.toString(QStringLiteral("yyyy-MM-dd"))));
        if (m_axisX)
            m_axisX->setRange(QDateTime(day, QTime(0, 0)), QDateTime(day, QTime(23, 59)));
        updateSidePanelValues(0, 0, false, 0, 0, tr("无数据"));
        return;
    }

    // 先找真实高低，再按分钟桶降采样，避免点过密
    int rawHi = 0, rawLo = 0;
    for (int i = 1; i < raw.size(); ++i) {
        if (raw.at(i).second > raw.at(rawHi).second)
            rawHi = i;
        if (raw.at(i).second < raw.at(rawLo).second)
            rawLo = i;
    }
    const double dayHigh = raw.at(rawHi).second;
    const double dayLow = raw.at(rawLo).second;
    const QDateTime tFirst = raw.first().first;
    const QDateTime tLast = raw.last().first;

    QVector<QPair<QDateTime, double>> sampled;
    sampled.reserve(qMin(raw.size(), 500));
    QString lastMinuteKey;
    for (int i = 0; i < raw.size(); ++i) {
        const auto& pt = raw.at(i);
        const QString mk = pt.first.toString(QStringLiteral("HH:mm"));
        const bool force = (i == rawHi || i == rawLo || i == 0 || i == raw.size() - 1);
        if (force || mk != lastMinuteKey) {
            sampled.append(pt);
            lastMinuteKey = mk;
        } else {
            // 同分钟保留最新价
            sampled.last() = pt;
            if (i == rawHi || i == rawLo)
                sampled.last() = pt;
        }
    }
    // 再二次抽稀到约 400 点
    m_plotPoints.clear();
    if (sampled.size() <= 400) {
        m_plotPoints = sampled;
    } else {
        const int step = sampled.size() / 400 + 1;
        for (int i = 0; i < sampled.size(); i += step)
            m_plotPoints.append(sampled.at(i));
        if (m_plotPoints.isEmpty() || m_plotPoints.last().first != sampled.last().first)
            m_plotPoints.append(sampled.last());
        // 强制保留高低点
        auto hasNear = [&](const QDateTime& ts, double price) {
            for (const auto& p : m_plotPoints) {
                if (p.first == ts || (qAbs(p.second - price) < 1e-6
                                      && qAbs(p.first.secsTo(ts)) < 120))
                    return true;
            }
            return false;
        };
        if (!hasNear(raw.at(rawHi).first, dayHigh))
            m_plotPoints.append(raw.at(rawHi));
        if (!hasNear(raw.at(rawLo).first, dayLow))
            m_plotPoints.append(raw.at(rawLo));
        std::sort(m_plotPoints.begin(), m_plotPoints.end(),
                  [](const auto& a, const auto& b) { return a.first < b.first; });
    }

    int hiIdx = 0, loIdx = 0;
    for (int i = 0; i < m_plotPoints.size(); ++i) {
        m_series->append(m_plotPoints.at(i).first.toMSecsSinceEpoch(),
                         m_plotPoints.at(i).second);
        if (m_plotPoints.at(i).second > m_plotPoints.at(hiIdx).second)
            hiIdx = i;
        if (m_plotPoints.at(i).second < m_plotPoints.at(loIdx).second)
            loIdx = i;
    }
    const double hi = m_plotPoints.at(hiIdx).second;
    const double lo = m_plotPoints.at(loIdx).second;
    const double margin = qMax(0.5, (hi - lo) * 0.12);
    if (m_axisY)
        m_axisY->setRange(lo - margin, hi + margin);

    if (m_highSeries) {
        m_highSeries->append(m_plotPoints.at(hiIdx).first.toMSecsSinceEpoch(), hi);
        m_highSeries->setVisible(true);
    }
    if (m_lowSeries) {
        m_lowSeries->append(m_plotPoints.at(loIdx).first.toMSecsSinceEpoch(), lo);
        m_lowSeries->setVisible(true);
    }

    // 时间轴：按实际采样起止，略留边距（不是强制 0～24 点空白）
    if (m_axisX) {
        const QDateTime x0 = tFirst.addSecs(-300);
        const QDateTime x1 = tLast.addSecs(300);
        m_axisX->setRange(x0, x1);
        m_axisX->setFormat(QStringLiteral("HH:mm"));
    }

    const QString span = tr("%1–%2")
                             .arg(tFirst.toString(QStringLiteral("HH:mm")))
                             .arg(tLast.toString(QStringLiteral("HH:mm")));
    if (m_chart)
        m_chart->setTitle(
            tr("%1 本地分时  %2  （原始%3点→显示%4点）  高 %5  低 %6")
                .arg(day.toString(QStringLiteral("yyyy-MM-dd")))
                .arg(span)
                .arg(raw.size())
                .arg(m_plotPoints.size())
                .arg(dayHigh, 0, 'f', 2)
                .arg(dayLow, 0, 'f', 2));
    updateSidePanelValues(m_plotPoints.last().second, 0, false, dayHigh, dayLow,
                          tr("历史分时·本地采样"));
}

void ChartWindow::loadDailyRange(const QDate& from, const QDate& to)
{
    if (!m_series)
        return;
    setWindowTitle(tr("%1 ~ %2 日线")
                       .arg(from.toString(QStringLiteral("yyyy-MM-dd")))
                       .arg(to.toString(QStringLiteral("yyyy-MM-dd"))));

    m_series->clear();
    if (m_forecastSeries) m_forecastSeries->clear();
    if (m_forecastLowSeries) m_forecastLowSeries->clear();
    if (m_currentSeries) m_currentSeries->clear();
    if (m_highSeries) m_highSeries->clear();
    if (m_lowSeries) m_lowSeries->clear();
    hideCrosshair();
    m_hasPredict = false;

    m_plotPoints = ExtremeDatabase::instance().loadDailyClosesRange(
        from, to, currentTypeCode());
    if (m_plotPoints.isEmpty())
        m_plotPoints = ExtremeDatabase::instance().loadDailyClosesRange(
            from, to, QStringLiteral("gj"));

    if (m_plotPoints.isEmpty()) {
        if (m_chart)
            m_chart->setTitle(tr("%1 ~ %2 · 无本地日线（请运行历史导入脚本或保持程序累积）")
                                  .arg(from.toString(QStringLiteral("yyyy-MM-dd")))
                                  .arg(to.toString(QStringLiteral("yyyy-MM-dd"))));
        if (m_axisX)
            m_axisX->setRange(QDateTime(from, QTime(0, 0)), QDateTime(to, QTime(23, 59)));
        updateSidePanelValues(0, 0, false, 0, 0, tr("无数据"));
        return;
    }

    double minP = m_plotPoints.first().second, maxP = minP;
    int hi = 0, lo = 0;
    for (int i = 0; i < m_plotPoints.size(); ++i) {
        m_series->append(m_plotPoints.at(i).first.toMSecsSinceEpoch(),
                         m_plotPoints.at(i).second);
        if (m_plotPoints.at(i).second > maxP) {
            maxP = m_plotPoints.at(i).second;
            hi = i;
        }
        if (m_plotPoints.at(i).second < minP) {
            minP = m_plotPoints.at(i).second;
            lo = i;
        }
    }
    if (m_highSeries) {
        m_highSeries->append(m_plotPoints.at(hi).first.toMSecsSinceEpoch(), maxP);
        m_highSeries->setVisible(true);
    }
    if (m_lowSeries) {
        m_lowSeries->append(m_plotPoints.at(lo).first.toMSecsSinceEpoch(), minP);
        m_lowSeries->setVisible(true);
    }
    if (m_axisX)
        m_axisX->setRange(QDateTime(from, QTime(0, 0)).addDays(-1),
                          QDateTime(to, QTime(23, 59)).addDays(1));
    const double margin = qMax(0.5, (maxP - minP) * 0.12);
    if (m_axisY)
        m_axisY->setRange(minP - margin, maxP + margin);
    if (m_chart)
        m_chart->setTitle(tr("%1 ~ %2 日线（%3天）  高 %4  低 %5")
                              .arg(from.toString(QStringLiteral("yyyy-MM-dd")))
                              .arg(to.toString(QStringLiteral("yyyy-MM-dd")))
                              .arg(m_plotPoints.size())
                              .arg(maxP, 0, 'f', 2)
                              .arg(minP, 0, 'f', 2));
    updateSidePanelValues(m_plotPoints.last().second, 0, false, maxP, minP, tr("日线区间"));
}

void ChartWindow::updateMonthSeries()
{
    // 兼容旧调用：按当前日期控件查询
    applyPeriodSelection();
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
        m_chart->setBackgroundBrush(QBrush(QColor(15, 18, 28)));
        m_chart->setPlotAreaBackgroundBrush(QBrush(QColor(22, 27, 40)));
        m_chart->setTitleBrush(QBrush(QColor(232, 234, 237)));
        if (m_axisX) {
            m_axisX->setLabelsColor(QColor(139, 147, 167));
            m_axisX->setGridLineColor(QColor(42, 51, 71));
            m_axisX->setTitleBrush(QBrush(QColor(139, 147, 167)));
        }
        if (m_axisY) {
            m_axisY->setLabelsColor(QColor(139, 147, 167));
            m_axisY->setGridLineColor(QColor(42, 51, 71));
            m_axisY->setTitleBrush(QBrush(QColor(139, 147, 167)));
        }
        if (m_sidePanel)
            m_sidePanel->setStyleSheet(
                "QFrame{background:#161b27;border:1px solid #2a3347;border-radius:14px;}"
                "QLabel{color:#8b93a7;}");
        setStyleSheet("background:#1a1d23;");

        setSolid(m_series, QColor(110, 168, 254), 2);           // 实际：亮蓝
        setSolid(m_ma5Series, QColor(61, 214, 140), 2);
        setSolid(m_ma10Series, QColor(255, 193, 7), 2);
        setSolid(m_ma20Series, QColor(167, 139, 250), 2);
        setDot(m_yesterdaySeries, QColor(120, 144, 156), 1);    // 昨日：蓝灰点线（非白）
        setDash(m_forecastSeries, QColor(255, 82, 82), 2);      // 预测高：鲜红虚线
        setDash(m_forecastLowSeries, QColor(0, 230, 118), 2);   // 预测低：鲜绿虚线

        if (m_currentSeries) {
            m_currentSeries->setMarkerShape(QScatterSeries::MarkerShapeCircle);
            m_currentSeries->setColor(QColor(255, 77, 79));
            m_currentSeries->setBorderColor(QColor(255, 255, 255));
        }
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
        setStyleSheet("background:#f5f6f8;");

        setSolid(m_series, QColor(0, 82, 217), 2);
        setSolid(m_ma5Series, QColor(230, 126, 34), 2);
        setSolid(m_ma10Series, QColor(241, 196, 15), 2);
        setSolid(m_ma20Series, QColor(155, 89, 182), 2);
        setDot(m_yesterdaySeries, QColor(120, 120, 120), 1);
        setDash(m_forecastSeries, QColor(231, 76, 60), 2);      // 预测高：红虚线
        setDash(m_forecastLowSeries, QColor(39, 174, 96), 2);   // 预测低：绿虚线

        if (m_currentSeries) {
            m_currentSeries->setMarkerShape(QScatterSeries::MarkerShapeCircle);
            m_currentSeries->setColor(QColor(255, 77, 79));
            m_currentSeries->setBorderColor(QColor(255, 255, 255));
        }
        // 侧栏始终深色卡片风格，避免浅色主题把数字刷成深色导致看不清
        applySidePanelChrome();
    }

    if (m_chart->legend()) {
        m_chart->legend()->setLabelColor(dark ? QColor(220, 220, 220) : QColor(60, 60, 60));
        m_chart->legend()->setBackgroundVisible(false);
    }
    applySidePanelChrome();
}


void ChartWindow::applySidePanelChrome()
{
    auto valStyle = [](const QString& color, int size = 13) {
        return QStringLiteral(
                   "color:%1;font-size:%2px;font-weight:600;"
                   "font-family:Consolas,'Microsoft YaHei UI',monospace;")
            .arg(color).arg(size);
    };
    if (m_sideClockLabel)
        m_sideClockLabel->setStyleSheet(
            "color:#5b8def;font-size:13px;font-weight:600;font-family:Consolas,monospace;");
    if (m_sideSessionLabel)
        m_sideSessionLabel->setStyleSheet(
            "color:#a8b3c7;font-size:11px;padding:2px 8px;background:#1c2433;border-radius:8px;");
    if (m_sideCurrentLabel)
        m_sideCurrentLabel->setStyleSheet(
            "color:#ffffff;font-size:16px;font-weight:700;font-family:Consolas,monospace;");
    if (m_sideHighLabel)
        m_sideHighLabel->setStyleSheet(valStyle("#f07178", 13));
    if (m_sideLowLabel)
        m_sideLowLabel->setStyleSheet(valStyle("#7fd99a", 13));
    if (m_sideForecastCard)
        m_sideForecastCard->setStyleSheet(
            "QFrame#forecastCard{background:#151a26;border:1px solid #28374d;border-radius:8px;}");
    if (m_sidePredictHighLabel)
        m_sidePredictHighLabel->setStyleSheet(valStyle("#ff8b7a", 13));
    if (m_sidePredictLowLabel)
        m_sidePredictLowLabel->setStyleSheet(valStyle("#6bcb8a", 13));
    if (m_sideModeLabel)
        m_sideModeLabel->setStyleSheet(
            "color:#8b9bb4;font-size:10px;padding:1px 6px;"
            "background:#1e2638;border-radius:4px;border:1px solid #2f3d54;");
    if (m_sidePredictHighTimeLabel)
        m_sidePredictHighTimeLabel->setStyleSheet(
            "color:#ffa08a;font-size:11px;font-weight:600;font-family:'Segoe UI','Microsoft YaHei UI',sans-serif;");
    if (m_sidePredictLowTimeLabel)
        m_sidePredictLowTimeLabel->setStyleSheet(
            "color:#80e0a0;font-size:11px;font-weight:600;font-family:'Segoe UI','Microsoft YaHei UI',sans-serif;");
    if (m_sideCatalystLabel)
        m_sideCatalystLabel->setStyleSheet(
            "QLabel#catalystLabel{color:#ffe58f;font-size:11px;line-height:1.3;"
            "background:#1a2233;border:1px solid #2f3d54;border-radius:4px;padding:4px 6px;}");
    if (m_sidePanel)
        m_sidePanel->setStyleSheet(
            "QFrame#sidePanel{background:#12161f;border:1px solid #2a3347;border-radius:12px;}"
            "QLabel{background:transparent;}");
    if (m_sideForecastList)
        m_sideForecastList->setStyleSheet(
            "QListWidget#forecastList{"
            "  background:#0e1219;border:1px solid #2a3347;border-radius:8px;"
            "  color:#c5cddb;font-size:11px;outline:none;}"
            "QListWidget#forecastList::item{padding:5px 6px;border-bottom:1px solid #1a2233;}"
            "QListWidget#forecastList::item:hover{background:#141c2b;}"
            "QListWidget#forecastList::item:selected{background:#1c2a40;color:#e8eaed;}");
}

void ChartWindow::onMaOptionChanged()
{
    updateMovingAverages();
    if (m_chartView)
        m_chartView->viewport()->update();
}

void ChartWindow::updateMovingAverages()
{
    if (!m_ma5Series || !m_ma20Series)
        return;

    m_ma5Series->clear();
    if (m_ma10Series)
        m_ma10Series->clear();
    m_ma20Series->clear();

    const bool want5 = m_ma5Action && m_ma5Action->isChecked();
    const bool want10 = m_ma10Action && m_ma10Action->isChecked();
    const bool want20 = m_ma20Action && m_ma20Action->isChecked();
    const bool any = want5 || want10 || want20;
    const bool intraday = isIntradayMode();

    m_ma5Series->setVisible(want5 && intraday);
    if (m_ma10Series)
        m_ma10Series->setVisible(want10 && intraday);
    m_ma20Series->setVisible(want20 && intraday);

    // 勾选均线时绝不能关掉预测线
    if (intraday)
        setForecastVisible(true);

    if (!any || !intraday) {
        if (m_chart)
            m_chart->update();
        return;
    }

    if (m_plotPoints.isEmpty()) {
        if (m_chart)
            m_chart->update();
        return;
    }

    double lastPx = m_plotPoints.last().second;
    double pxMin = lastPx, pxMax = lastPx;
    for (const auto& pt : m_plotPoints) {
        if (pt.second <= 0)
            continue;
        pxMin = qMin(pxMin, pt.second);
        pxMax = qMax(pxMax, pt.second);
    }

    // 日线收盘：与现价尺度一致才画水平 MA（5/10/20 日）
    auto closes = ExtremeDatabase::instance().loadRecentDailyCloses(40, currentTypeCode());
    if (closes.size() < 5)
        closes = ExtremeDatabase::instance().loadRecentDailyCloses(40, QStringLiteral("zs"));
    if (closes.size() < 5)
        closes = ExtremeDatabase::instance().loadRecentDailyCloses(40, QStringLiteral("gj"));

    // 尺度纠正：日线若是另一单位（如美元盎司），按现价/末日线比值缩放
    if (!closes.isEmpty() && lastPx > 0.0) {
        const double lastClose = closes.last().second;
        if (lastClose > 0.0) {
            const double ratio = lastPx / lastClose;
            // 明显不在同一量级（如 930 vs 2600）
            if (ratio < 0.55 || ratio > 1.8) {
                for (auto& c : closes)
                    c.second *= ratio;
            }
        }
    }

    auto smaDaily = [](const QVector<QPair<QDate, double>>& c, int n) -> double {
        if (c.size() < n)
            return 0.0;
        double s = 0.0;
        for (int i = c.size() - n; i < c.size(); ++i)
            s += c.at(i).second;
        return s / static_cast<double>(n);
    };

    auto nearPrice = [&](double ma) {
        if (ma <= 0.0 || lastPx <= 0.0)
            return false;
        return ma >= lastPx * 0.80 && ma <= lastPx * 1.20;
    };

    const double d5 = smaDaily(closes, 5);
    const double d10 = smaDaily(closes, 10);
    const double d20 = smaDaily(closes, 20);

    const QDateTime t0 = QDateTime(QDate::currentDate(), QTime(0, 0));
    const QDateTime t1 = QDateTime(QDate::currentDate(), QTime(23, 59, 59));
    const qint64 x0 = t0.toMSecsSinceEpoch();
    const qint64 x1 = t1.toMSecsSinceEpoch();

    // 分时滚动均线（保证勾选后一定有曲线，不依赖日线库）
    const int n = m_plotPoints.size();
    QVector<double> pref(n + 1, 0.0);
    for (int i = 0; i < n; ++i)
        pref[i + 1] = pref[i] + m_plotPoints.at(i).second;
    auto winAvg = [&](int i, int win) -> double {
        if (i + 1 < win)
            return 0.0;
        return (pref[i + 1] - pref[i + 1 - win]) / static_cast<double>(win);
    };
    const int step = qMax(1, n / 400);

    auto fillRolling = [&](QLineSeries* series, int win, bool use) {
        if (!series || !use)
            return;
        series->setName(win == 5 ? tr("MA5") : (win == 10 ? tr("MA10") : tr("MA20")));
        for (int i = 0; i < n; i += step) {
            const double a = winAvg(i, win);
            if (a > 0.0)
                series->append(m_plotPoints.at(i).first.toMSecsSinceEpoch(), a);
        }
        const double aLast = winAvg(n - 1, win);
        if (aLast > 0.0)
            series->append(m_plotPoints.last().first.toMSecsSinceEpoch(), aLast);
    };

    // 优先：有合格日线则画水平参考线；否则用分时滚动 MA
    if (want5) {
        if (nearPrice(d5)) {
            m_ma5Series->append(x0, d5);
            m_ma5Series->append(x1, d5);
            m_ma5Series->setName(tr("MA5日"));
            pxMin = qMin(pxMin, d5);
            pxMax = qMax(pxMax, d5);
        } else {
            fillRolling(m_ma5Series, 5, true);
        }
    }
    if (want10 && m_ma10Series) {
        if (nearPrice(d10)) {
            m_ma10Series->append(x0, d10);
            m_ma10Series->append(x1, d10);
            m_ma10Series->setName(tr("MA10日"));
            pxMin = qMin(pxMin, d10);
            pxMax = qMax(pxMax, d10);
        } else {
            fillRolling(m_ma10Series, 10, true);
        }
    }
    if (want20) {
        if (nearPrice(d20)) {
            m_ma20Series->append(x0, d20);
            m_ma20Series->append(x1, d20);
            m_ma20Series->setName(tr("MA20日"));
            pxMin = qMin(pxMin, d20);
            pxMax = qMax(pxMax, d20);
        } else {
            fillRolling(m_ma20Series, 20, true);
        }
    }

    // Y 轴纳入均线与预测高低，避免预测虚线被裁掉
    if (m_hasPredict) {
        if (m_lastPredictHigh > 0)
            pxMax = qMax(pxMax, m_lastPredictHigh);
        if (m_lastPredictLow > 0)
            pxMin = qMin(pxMin, m_lastPredictLow);
    }
    if (m_axisY && pxMax > pxMin) {
        const double pad = qMax(0.3, (pxMax - pxMin) * 0.10);
        const double curMin = m_axisY->min();
        const double curMax = m_axisY->max();
        // 只扩大、不无故压缩（防止把已有分时/预测挤没）
        const double yLo = qMin(curMin, pxMin - pad);
        const double yHi = qMax(curMax, pxMax + pad);
        m_axisY->setRange(yLo, yHi);
    }

    // 确保轴绑定（防止某些路径下系列未挂轴）
    if (m_axisX && m_axisY) {
        for (QLineSeries* s : {m_ma5Series, m_ma10Series, m_ma20Series,
                               m_forecastSeries, m_forecastLowSeries}) {
            if (!s)
                continue;
            const auto axes = s->attachedAxes();
            if (!axes.contains(m_axisX))
                s->attachAxis(m_axisX);
            if (!axes.contains(m_axisY))
                s->attachAxis(m_axisY);
        }
    }

    if (m_forecastSeries)
        m_forecastSeries->setVisible(true);
    if (m_forecastLowSeries)
        m_forecastLowSeries->setVisible(true);

    if (m_chart)
        m_chart->update();
    if (m_chartView)
        m_chartView->viewport()->update();
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
    QStringList tips;
    const QString src = currentTypeCode();

    if (!TradingSession::isTradingNow(src)) {
        tips << tr("非交易时段");
        tips << TradingSession::hoursDescription(src);
        return tips.join(QStringLiteral(" · "));
    }

    // 1) 趋势：现价 vs 分时均价
    QString trend = tr("震荡");
    QString reason;
    if (m_plotPoints.size() >= 8 && price > 0.0) {
        double sum = 0.0;
        for (const auto& p : m_plotPoints)
            sum += p.second;
        const double vwap = sum / m_plotPoints.size();
        const double n = m_plotPoints.size();
        const int take = qMin(12, (int)n);
        double recent = 0.0;
        for (int i = (int)n - take; i < (int)n; ++i)
            recent += m_plotPoints.at(i).second;
        recent /= take;
        const double band = qMax(0.15, vwap * 0.0008);
        if (price > vwap + band && recent >= vwap)
            trend = tr("偏强");
        else if (price < vwap - band && recent <= vwap)
            trend = tr("偏弱");
        reason = tr("均价%1").arg(vwap, 0, 'f', 2);
    }
    tips << tr("趋势%1").arg(trend);
    if (!reason.isEmpty())
        tips << reason;

    // 2) 计划价
    const auto& st = AppSettings::instance();
    if (st.planEnabled() && price > 0.0) {
        if (st.planInvalidPrice() > 0.0) {
            const double inv = st.planInvalidPrice();
            if (qAbs(price - inv) / qMax(price, inv) < 0.0015 || price <= inv * 0.999)
                tips << tr("触及失效价%1").arg(inv, 0, 'f', 2);
        }
        if (st.planBuyPrice() > 0.0 && price <= st.planBuyPrice() * 1.001)
            tips << tr("靠近买入观察%1").arg(st.planBuyPrice(), 0, 'f', 2);
        if (st.planSellPrice() > 0.0 && price >= st.planSellPrice() * 0.999)
            tips << tr("靠近卖出观察%1").arg(st.planSellPrice(), 0, 'f', 2);
    }

    // 3) 命中率
    const auto& ft = ForecastTracker::instance();
    if (ft.totalEvaluated() >= 4)
        tips << tr("预测命中%1%").arg(ft.hitRatePercent(), 0, 'f', 0);

    if (EventCalendar::isHighImpactDay())
        tips << tr("今日宏观高波动");

    return tips.join(QStringLiteral(" · "));
}

void ChartWindow::updateClockAndAdvice()
{
    const QDateTime now = QDateTime::currentDateTime();
    if (m_sideClockLabel)
        m_sideClockLabel->setText(now.toString(QStringLiteral("HH:mm:ss")));

    const QString src = currentTypeCode();
    const bool open = TradingSession::isTradingNow(src, now);
    if (m_sideSessionLabel) {
        const QString st = TradingSession::statusText(src, now);
        m_sideSessionLabel->setText(st);
        m_sideSessionLabel->setStyleSheet(
            open ? QStringLiteral(
                       "color:#7fd99a;font-size:11px;padding:2px 6px;"
                       "background:#14301f;border-radius:8px;")
                 : QStringLiteral(
                       "color:#a8b3c7;font-size:11px;padding:2px 6px;"
                       "background:#1c2433;border-radius:8px;"));
        m_sideSessionLabel->setToolTip(TradingSession::hoursDescription(src));
    }

    double price = 0.0;
    if (!m_plotPoints.isEmpty())
        price = m_plotPoints.last().second;

    // 趋势徽章
    if (m_sideTrendLabel) {
        QString trend = tr("—");
        QString color = QStringLiteral("#e8eaed");
        if (m_plotPoints.size() >= 8 && price > 0.0) {
            double sum = 0.0;
            for (const auto& p : m_plotPoints)
                sum += p.second;
            const double vwap = sum / m_plotPoints.size();
            const double band = qMax(0.15, vwap * 0.0008);
            const int n = m_plotPoints.size();
            const int take = qMin(12, n);
            double recent = 0.0;
            for (int i = n - take; i < n; ++i)
                recent += m_plotPoints.at(i).second;
            recent /= take;
            if (price > vwap + band && recent >= vwap) {
                trend = tr("偏强");
                color = QStringLiteral("#7fd99a");
            } else if (price < vwap - band && recent <= vwap) {
                trend = tr("偏弱");
                color = QStringLiteral("#f07178");
            } else {
                trend = tr("震荡");
                color = QStringLiteral("#e8c547");
            }
            m_sideTrendLabel->setToolTip(
                tr("现价 %1 · 分时均价 %2").arg(price, 0, 'f', 2).arg(vwap, 0, 'f', 2));
        }
        m_sideTrendLabel->setText(trend);
        m_sideTrendLabel->setStyleSheet(
            QStringLiteral("color:%1;font-size:13px;font-weight:600;").arg(color));
    }

    if (m_sideHitRateLabel) {
        const auto& ft = ForecastTracker::instance();
        if (ft.totalEvaluated() > 0) {
            m_sideHitRateLabel->setText(
                tr("%1% (%2组)")
                    .arg(ft.hitRatePercent(), 0, 'f', 0)
                    .arg(ft.totalEvaluated() / 2));
            m_sideHitRateLabel->setToolTip(
                tr("来自已结算预测日志（非日线导入）\n"
                   "高命中 %1% / 低命中 %2% / 待结算 %3")
                    .arg(ft.highHitRatePercent(), 0, 'f', 0)
                    .arg(ft.lowHitRatePercent(), 0, 'f', 0)
                    .arg(ft.pendingCount()));
        } else if (ft.pendingCount() > 0) {
            m_sideHitRateLabel->setText(tr("待结算%1").arg(ft.pendingCount()));
            m_sideHitRateLabel->setToolTip(
                tr("已有预测登记，到期后用今高/今低结算才会计入命中率。"
                   "历史日线脚本不会产生预测命中样本。"));
        } else {
            m_sideHitRateLabel->setText(tr("暂无预测"));
            m_sideHitRateLabel->setToolTip(
                tr("命中率统计的是「预测高/低 vs 实际今高/低」。"
                   "import_historical_gold.py 只导入日线，不生成预测样本。"
                   "开启大模型或本地预测并运行一段时间后才会有命中数据。"));
        }
    }

    if (m_sideAdviceLabel)
        m_sideAdviceLabel->setText(buildAdviceText(price));
}


