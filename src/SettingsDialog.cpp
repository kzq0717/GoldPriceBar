#include "SettingsDialog.h"
#include "AppSettings.h"
#include "EventCalendar.h"
#include "ExtremeDatabase.h"
#include <QtMath>

#include "ExtremeDatabase.h"
#include <QtMath>
#include "Logger.h"
#include "UpdateChecker.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QComboBox>
#include <QSlider>
#include <QLabel>
#include <QCheckBox>
#include <QLineEdit>
#include <QPushButton>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QStandardPaths>
#include <QDoubleSpinBox>
#include <QAbstractSpinBox>
#include <QSpinBox>
#include <QTimeEdit>
#include <QDesktopServices>
#include <QUrl>
#include <QApplication>
#include <QIcon>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QUrl>
#include <QMessageBox>
#include <QScrollArea>
#include <QFrame>
#include <QFormLayout>
#include <QMessageBox>

SettingsDialog::SettingsDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("设置 - GoldPriceBarLite %1").arg(QApplication::applicationVersion()));
    setMinimumWidth(520);
    setMinimumHeight(640);
    resize(560, 720);
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
    setWindowIcon(QIcon(QStringLiteral(":/app.png")));
    setupUi();
    loadFromSettings();
    applyDialogTheme();
}

void SettingsDialog::setupUi()
{
    auto* mainLayout = new QVBoxLayout(this);
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto* formHost = new QWidget(scroll);
    formHost->setObjectName(QStringLiteral("settingsFormHost"));
    auto* form = new QFormLayout(formHost);
    form->setContentsMargins(12, 12, 16, 12);
    form->setSpacing(12);
    form->setHorizontalSpacing(16);
    form->setFieldGrowthPolicy(QFormLayout::FieldsStayAtSizeHint);
    form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);


    m_intervalCombo = new QComboBox(this);
    m_intervalCombo->setMinimumWidth(200);

    m_intervalCombo->addItem(tr("1 秒"), 1000);
    m_intervalCombo->addItem(tr("2 秒"), 2000);
    m_intervalCombo->addItem(tr("5 秒"), 5000);
    m_intervalCombo->addItem(tr("10 秒"), 10000);
    m_intervalCombo->addItem(tr("30 秒"), 30000);
    form->addRow(tr("刷新频率："), m_intervalCombo);

    m_sourceCombo = new QComboBox(this);
    m_sourceCombo->setMinimumWidth(200);

    m_sourceCombo->addItem(tr("浙商积存金"), "zs");
    m_sourceCombo->addItem(tr("民生积存金"), "ms");
    m_sourceCombo->addItem(tr("伦敦金 (XAU/USD)"), "gj");
    form->addRow(tr("数据源："), m_sourceCombo);

    auto* opacityLayout = new QHBoxLayout;
    m_opacitySlider = new QSlider(Qt::Horizontal, this);
    m_opacitySlider->setRange(30, 100);
    m_opacitySlider->setValue(95);
    m_opacityValueLabel = new QLabel("95%", this);
    m_opacityValueLabel->setFixedWidth(40);
    opacityLayout->addWidget(m_opacitySlider);
    opacityLayout->addWidget(m_opacityValueLabel);
    form->addRow(tr("窗口透明度："), opacityLayout);
    connect(m_opacitySlider, &QSlider::valueChanged, this, &SettingsDialog::onOpacityChanged);

    // 预警阈值
    m_alertHighSpin = new QDoubleSpinBox(this);
    m_alertHighSpin->setFixedWidth(140);
    m_alertHighSpin->setButtonSymbols(QAbstractSpinBox::UpDownArrows);
    m_alertHighSpin->setRange(0.0, 99999.0);
    m_alertHighSpin->setDecimals(2);
    m_alertHighSpin->setSingleStep(1.0);
    m_alertHighSpin->setSpecialValueText(tr("关闭"));
    m_alertHighSpin->setToolTip(tr("现价达到或超过该值时，价格条红点闪烁；0=关闭"));
    form->addRow(tr("高价预警："), m_alertHighSpin);

    m_alertLowSpin = new QDoubleSpinBox(this);
    m_alertLowSpin->setFixedWidth(140);
    m_alertLowSpin->setButtonSymbols(QAbstractSpinBox::UpDownArrows);
    m_alertLowSpin->setRange(0.0, 99999.0);
    m_alertLowSpin->setDecimals(2);
    m_alertLowSpin->setSingleStep(1.0);
    m_alertLowSpin->setSpecialValueText(tr("关闭"));
    m_alertLowSpin->setToolTip(tr("现价达到或低于该值时，价格条绿点闪烁；0=关闭"));
    form->addRow(tr("低价预警："), m_alertLowSpin);

    m_alertCooldownSpin = new QSpinBox(this);
    m_alertCooldownSpin->setRange(30, 3600);
    m_alertCooldownSpin->setSuffix(tr(" 秒"));
    m_alertCooldownSpin->setToolTip(tr("同一方向预警最短通知间隔，避免频繁弹窗"));
    form->addRow(tr("预警冷却："), m_alertCooldownSpin);

    m_trayNotifyCheck = new QCheckBox(tr("触发预警时弹出系统托盘通知"), this);
    form->addRow("", m_trayNotifyCheck);

    m_secondaryPriceCheck = new QCheckBox(tr("词条显示对照价（主源非伦敦金时显示伦敦金）"), this);
    form->addRow("", m_secondaryPriceCheck);

    m_darkThemeCheck = new QCheckBox(tr("深色主题（价格条 / 分时 / 设置）"), this);
    form->addRow("", m_darkThemeCheck);

    m_maCheck = new QCheckBox(tr("分时显示均线 MA5日 / MA20日（近5/20个交易日收盘）"), this);
    form->addRow("", m_maCheck);

    m_alertSoundCheck = new QCheckBox(tr("预警时系统提示音"), this);
    form->addRow("", m_alertSoundCheck);

    m_hotkeyCheck = new QCheckBox(tr("全局热键 Ctrl+Shift+G 显示/隐藏价格条"), this);
    form->addRow("", m_hotkeyCheck);

    m_quietCheck = new QCheckBox(tr("启用免打扰时段（不通知、不闪点、不蜂鸣）"), this);
    form->addRow("", m_quietCheck);
    auto* quietLay = new QHBoxLayout;
    m_quietStartEdit = new QTimeEdit(this);
    m_quietStartEdit->setDisplayFormat("HH:mm");
    m_quietEndEdit = new QTimeEdit(this);
    m_quietEndEdit->setDisplayFormat("HH:mm");
    quietLay->addWidget(new QLabel(tr("从"), this));
    quietLay->addWidget(m_quietStartEdit);
    quietLay->addWidget(new QLabel(tr("到"), this));
    quietLay->addWidget(m_quietEndEdit);
    quietLay->addWidget(new QLabel(tr("（可跨午夜）"), this));
    quietLay->addStretch();
    form->addRow(tr("免打扰："), quietLay);

    m_dcaDaySpin = new QSpinBox(this);
    m_dcaDaySpin->setRange(0, 28);
    m_dcaDaySpin->setSpecialValueText(tr("关闭"));
    m_dcaDaySpin->setToolTip(tr("每月几号提醒定投，0=关闭"));
    form->addRow(tr("定投日："), m_dcaDaySpin);
    m_dcaNoteEdit = new QLineEdit(this);
    m_dcaNoteEdit->setPlaceholderText(tr("可选备注，如：每月定投 500 元"));
    form->addRow(tr("定投备注："), m_dcaNoteEdit);

    m_smartMaCheck = new QCheckBox(tr("智能预警：跌破 MA5日 或 显著偏离均线"), this);
    form->addRow("", m_smartMaCheck);
    m_smartPctCheck = new QCheckBox(tr("智能预警：近20日价格分位过高/过低"), this);
    form->addRow("", m_smartPctCheck);
    auto* pctLay = new QHBoxLayout;
    m_pctLowSpin = new QSpinBox(this);
    m_pctLowSpin->setRange(1, 49);
    m_pctHighSpin = new QSpinBox(this);
    m_pctHighSpin->setRange(51, 99);
    pctLay->addWidget(new QLabel(tr("低分位≤"), this));
    pctLay->addWidget(m_pctLowSpin);
    pctLay->addWidget(new QLabel(tr("%  高分位≥"), this));
    pctLay->addWidget(m_pctHighSpin);
    pctLay->addWidget(new QLabel(tr("%"), this));
    pctLay->addStretch();
    form->addRow(tr("分位阈值："), pctLay);

    m_posGramsSpin = new QDoubleSpinBox(this);
    m_posGramsSpin->setFixedWidth(140);
    m_posGramsSpin->setButtonSymbols(QAbstractSpinBox::UpDownArrows);
    m_posGramsSpin->setRange(0, 99999);
    m_posGramsSpin->setDecimals(3);
    m_posGramsSpin->setSuffix(tr(" 克"));
    form->addRow(tr("持仓克数："), m_posGramsSpin);
    m_posCostSpin = new QDoubleSpinBox(this);
    m_posCostSpin->setFixedWidth(140);
    m_posCostSpin->setButtonSymbols(QAbstractSpinBox::UpDownArrows);
    m_posCostSpin->setRange(0, 99999);
    m_posCostSpin->setDecimals(2);
    m_posCostSpin->setSuffix(tr(" 元/克"));
    form->addRow(tr("持仓成本："), m_posCostSpin);

    m_premiumCheck = new QCheckBox(tr("溢价监测：主价/对照价比值异常偏离"), this);
    form->addRow("", m_premiumCheck);
    m_premiumPctSpin = new QDoubleSpinBox(this);
    m_premiumPctSpin->setRange(0.1, 50);
    m_premiumPctSpin->setDecimals(1);
    m_premiumPctSpin->setSuffix(tr(" %"));
    form->addRow(tr("溢价偏离阈值："), m_premiumPctSpin);

    m_dailyReportCheck = new QCheckBox(tr("每日收盘摘要（托盘）"), this);
    form->addRow("", m_dailyReportCheck);
    m_dailyReportTimeEdit = new QTimeEdit(this);
    m_dailyReportTimeEdit->setDisplayFormat("HH:mm");
    form->addRow(tr("摘要时刻："), m_dailyReportTimeEdit);

    m_eventAlertCheck = new QCheckBox(tr("宏观日程提醒（非农/FOMC/CPI 等，本地表）"), this);
    form->addRow("", m_eventAlertCheck);
    m_eventSummaryLabel = new QLabel(EventCalendar::summaryNear(), this);
    m_eventSummaryLabel->setWordWrap(true);
    m_eventSummaryLabel->setStyleSheet("font-size:11px;");
    form->addRow(tr("近10日日程："), m_eventSummaryLabel);

    m_suggestAlertBtn = new QPushButton(tr("按近20日波动建议高低预警"), this);
    m_suggestAlertBtn->setObjectName(QStringLiteral("wideAction"));
    form->addRow("", m_suggestAlertBtn);
    QObject::connect(m_suggestAlertBtn, &QPushButton::clicked, this, [this]() {
        QString src = AppSettings::instance().dataSource();
        if (src == QStringLiteral("xau")) src = QStringLiteral("gj");
        auto closes = ExtremeDatabase::instance().loadRecentDailyCloses(21, src);
        if (closes.size() < 5)
            closes = ExtremeDatabase::instance().loadRecentDailyCloses(21, QStringLiteral("gj"));
        if (closes.size() < 5) {
            m_eventSummaryLabel->setText(tr("日线样本不足，无法建议（请先运行积累或切换伦敦金）"));
            return;
        }
        double sumRange = 0.0;
        int n = 0;
        for (int i = 1; i < closes.size(); ++i) {
            sumRange += qAbs(closes.at(i).second - closes.at(i-1).second);
            ++n;
        }
        const double avgMove = n > 0 ? sumRange / n : 0.0;
        const double last = closes.last().second;
        const double hi = last + avgMove * 0.8;
        const double lo = last - avgMove * 0.8;
        m_alertHighSpin->setValue(hi);
        m_alertLowSpin->setValue(qMax(0.0, lo));
        m_eventSummaryLabel->setText(
            tr("建议高 %1 / 低 %2（近均日变动 %3）")
                .arg(hi, 0, 'f', 2).arg(lo, 0, 'f', 2).arg(avgMove, 0, 'f', 2));
    });

    m_proxyCheck = new QCheckBox(tr("启用 HTTP 代理（公司网络/科学上网）"), this);
    form->addRow("", m_proxyCheck);
    auto* proxyLay = new QHBoxLayout;
    m_proxyHostEdit = new QLineEdit(this);
    m_proxyHostEdit->setPlaceholderText(tr("主机，如 127.0.0.1"));
    m_proxyPortSpin = new QSpinBox(this);
    m_proxyPortSpin->setRange(1, 65535);
    m_proxyPortSpin->setValue(7890);
    proxyLay->addWidget(m_proxyHostEdit, 1);
    proxyLay->addWidget(new QLabel(tr(":"), this));
    proxyLay->addWidget(m_proxyPortSpin);
    form->addRow(tr("代理地址："), proxyLay);

    auto* dbLayout = new QHBoxLayout;
    m_dbDirEdit = new QLineEdit(this);
    m_dbDirEdit->setPlaceholderText(tr("留空 = 默认路径（系统 AppData）"));
    m_dbDirBrowseBtn = new QPushButton(tr("浏览…"), this);
    m_dbDirBrowseBtn->setFixedWidth(64);
    dbLayout->addWidget(m_dbDirEdit);
    dbLayout->addWidget(m_dbDirBrowseBtn);
    form->addRow(tr("数据库目录："), dbLayout);
    connect(m_dbDirBrowseBtn, &QPushButton::clicked, this, [this]() {
        const QString start = m_dbDirEdit->text().isEmpty()
                                  ? QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                                  : m_dbDirEdit->text();
        const QString dir = QFileDialog::getExistingDirectory(
            this, tr("选择数据库目录"), start);
        if (!dir.isEmpty())
            m_dbDirEdit->setText(dir);
    });

    auto* forecastLayout = new QHBoxLayout;
    auto* localLbl = new QLabel(tr("本地"), this);
    localLbl->setStyleSheet("font-size:11px;");
    m_forecastSlider = new QSlider(Qt::Horizontal, this);
    m_forecastSlider->setRange(0, 1);
    m_forecastSlider->setPageStep(1);
    m_forecastSlider->setSingleStep(1);
    m_forecastSlider->setValue(0);
    m_forecastSlider->setFixedWidth(80);
    auto* onlineLbl = new QLabel(tr("大模型"), this);
    onlineLbl->setStyleSheet("font-size:11px;");
    m_forecastModeLabel = new QLabel(tr("本地推演"), this);
    m_forecastModeLabel->setStyleSheet("color:#0052d9;font-size:12px;font-weight:bold;");
    forecastLayout->addWidget(localLbl);
    forecastLayout->addWidget(m_forecastSlider);
    forecastLayout->addWidget(onlineLbl);
    forecastLayout->addSpacing(8);
    forecastLayout->addWidget(m_forecastModeLabel);
    forecastLayout->addStretch();
    form->addRow(tr("价格预测："), forecastLayout);
    connect(m_forecastSlider, &QSlider::valueChanged, this, &SettingsDialog::onForecastSliderChanged);

    m_providerLabel = new QLabel(tr("大模型提供方："), this);
    m_providerCombo = new QComboBox(this);
    m_providerCombo->setMinimumWidth(200);
    m_providerCombo->addItem(tr("xAI (Grok)"), QStringLiteral("xai"));
    m_providerCombo->addItem(tr("Google Gemini"), QStringLiteral("gemini"));
    form->addRow(m_providerLabel, m_providerCombo);
    connect(m_providerCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &SettingsDialog::onProviderChanged);

    m_apiKeyEdit = new QLineEdit(this);
    m_apiKeyEdit->setEchoMode(QLineEdit::Password);
    m_apiKeyEdit->setPlaceholderText(tr("API Key"));
    m_apiKeyLabel = new QLabel(tr("API Key："), this);
    form->addRow(m_apiKeyLabel, m_apiKeyEdit);
    connect(m_apiKeyEdit, &QLineEdit::editingFinished, this, [this]() {
        if (!m_apiKeyEdit->text().trimmed().isEmpty()
            && m_forecastSlider && m_forecastSlider->value() == 1)
            onRefreshModels();
    });

    m_modelCombo = new QComboBox(this);
    m_modelCombo->setEditable(true);
    m_modelCombo->setMinimumWidth(200);
    m_modelLabel = new QLabel(tr("模型："), this);
    auto* modelLay = new QHBoxLayout;
    modelLay->addWidget(m_modelCombo, 1);
    m_refreshModelsBtn = new QPushButton(tr("拉取模型"), this);
    m_refreshModelsBtn->setObjectName(QStringLiteral("wideAction"));
    m_refreshModelsBtn->setToolTip(tr("使用当前 API Key 从服务商拉取可用模型列表"));
    modelLay->addWidget(m_refreshModelsBtn);
    form->addRow(m_modelLabel, modelLay);
    connect(m_refreshModelsBtn, &QPushButton::clicked, this, &SettingsDialog::onRefreshModels);

    m_modelsNam = new QNetworkAccessManager(this);
    fillDefaultModels();

    m_autoStartCheck = new QCheckBox(tr("开机自动启动"), this);
    form->addRow("", m_autoStartCheck);

    scroll->setWidget(formHost);
    mainLayout->addWidget(scroll, 1);

    auto* hint = new QLabel(
        tr("高/低预警：0 表示关闭。触发后价格条在「高」与分时按钮之间闪烁色点。"
           "退出可直接点下方「退出软件」。"),
        this);
    hint->setWordWrap(true);
    hint->setStyleSheet("font-size:11px;");
    mainLayout->addWidget(hint);
    mainLayout->addStretch();

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &SettingsDialog::onAccept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* exitBtn = new QPushButton(tr("退出软件"), this);
    exitBtn->setObjectName(QStringLiteral("exitButton"));
    connect(exitBtn, &QPushButton::clicked, this, &SettingsDialog::onExitApp);

    auto* logBtn = new QPushButton(tr("打开日志目录"), this);
    connect(logBtn, &QPushButton::clicked, this, []() {
        const QString dir = Logger::logDir();
        QDir().mkpath(dir);
        QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
    });

    auto* updateBtn = new QPushButton(tr("检查更新"), this);
    connect(updateBtn, &QPushButton::clicked, this, &SettingsDialog::onCheckUpdate);

    auto* bottom = new QHBoxLayout;
    bottom->addWidget(exitBtn);
    bottom->addWidget(logBtn);
    bottom->addWidget(updateBtn);
    bottom->addStretch();
    bottom->addWidget(buttons);
    mainLayout->addLayout(bottom);
}

void SettingsDialog::updateForecastUiState()
{
    const bool online = m_forecastSlider->value() >= 1;
    m_apiKeyEdit->setEnabled(online);
    m_modelCombo->setEnabled(online);
    m_apiKeyLabel->setEnabled(online);
    m_modelLabel->setEnabled(online);
    if (online) {
        m_forecastModeLabel->setText(tr("大模型(Grok)"));
        m_forecastModeLabel->setStyleSheet("color:#e67e22;font-size:12px;font-weight:bold;");
    } else {
        m_forecastModeLabel->setText(tr("本地推演"));
        m_forecastModeLabel->setStyleSheet("color:#0052d9;font-size:12px;font-weight:bold;");
    }
}

void SettingsDialog::loadFromSettings()
{
    const auto& settings = AppSettings::instance();

    int index = m_intervalCombo->findData(settings.refreshIntervalMs());
    if (index < 0) index = 2;
    m_intervalCombo->setCurrentIndex(index);

    index = m_sourceCombo->findData(settings.dataSource());
    if (index < 0) index = 0;
    m_sourceCombo->setCurrentIndex(index);

    const int opacityPercent = static_cast<int>(settings.opacity() * 100);
    m_opacitySlider->setValue(opacityPercent);
    m_opacityValueLabel->setText(QString("%1%").arg(opacityPercent));

    m_autoStartCheck->setChecked(settings.autoStart());
    m_dbDirEdit->setText(settings.databaseDir());

    m_alertHighSpin->setValue(settings.alertHigh());
    m_alertLowSpin->setValue(settings.alertLow());
    m_alertCooldownSpin->setValue(settings.alertCooldownSec());
    m_trayNotifyCheck->setChecked(settings.trayNotifyOnAlert());
    m_secondaryPriceCheck->setChecked(settings.showSecondaryPrice());
    m_darkThemeCheck->setChecked(settings.darkTheme());
    m_maCheck->setChecked(settings.showMovingAverage());
    m_alertSoundCheck->setChecked(settings.alertSound());
    m_hotkeyCheck->setChecked(settings.hotkeyEnabled());
    m_quietCheck->setChecked(settings.quietHoursEnabled());
    m_quietStartEdit->setTime(settings.quietStart());
    m_quietEndEdit->setTime(settings.quietEnd());
    m_dcaDaySpin->setValue(settings.dcaDayOfMonth());
    m_dcaNoteEdit->setText(settings.dcaNote());
    m_proxyCheck->setChecked(settings.proxyEnabled());
    m_proxyHostEdit->setText(settings.proxyHost());
    m_proxyPortSpin->setValue(settings.proxyPort());
    m_smartMaCheck->setChecked(settings.smartAlertMa());
    m_smartPctCheck->setChecked(settings.smartAlertPercentile());
    m_pctLowSpin->setValue(settings.percentileLow());
    m_pctHighSpin->setValue(settings.percentileHigh());
    m_posGramsSpin->setValue(settings.positionGrams());
    m_posCostSpin->setValue(settings.positionCost());
    m_premiumCheck->setChecked(settings.premiumAlertEnabled());
    m_premiumPctSpin->setValue(settings.premiumThresholdPct());
    m_dailyReportCheck->setChecked(settings.dailyReportEnabled());
    m_dailyReportTimeEdit->setTime(settings.dailyReportTime());
    m_eventAlertCheck->setChecked(settings.eventAlertEnabled());


    m_forecastSlider->setValue(settings.forecastOnline() ? 1 : 0);
    {
        const int pi = m_providerCombo->findData(settings.llmProvider());
        m_providerCombo->setCurrentIndex(pi >= 0 ? pi : 0);
    }
    updateApiKeyPlaceholder();
    fillDefaultModels();
    m_apiKeyEdit->setText(settings.xaiApiKey());

    const QString model = settings.xaiModel();
    int mi = m_modelCombo->findText(model);
    if (mi >= 0)
        m_modelCombo->setCurrentIndex(mi);
    else
        m_modelCombo->setEditText(model);

    updateForecastUiState();
}

void SettingsDialog::onOpacityChanged(int value)
{
    m_opacityValueLabel->setText(QString("%1%").arg(value));
}

void SettingsDialog::onForecastSliderChanged(int)
{
    updateForecastUiState();
}

void SettingsDialog::onAccept()
{
    auto& settings = AppSettings::instance();
    const QString oldDbDir = settings.databaseDir();

    settings.setRefreshIntervalMs(m_intervalCombo->currentData().toInt());
    settings.setDataSource(m_sourceCombo->currentData().toString());
    settings.setOpacity(m_opacitySlider->value() / 100.0);
    settings.setAutoStart(m_autoStartCheck->isChecked());
    settings.setForecastOnline(m_forecastSlider->value() >= 1);
    settings.setXaiApiKey(m_apiKeyEdit->text().trimmed());
    settings.setXaiModel(m_modelCombo->currentText().trimmed());
    settings.setDatabaseDir(m_dbDirEdit->text().trimmed());
    settings.setAlertHigh(m_alertHighSpin->value());
    settings.setAlertLow(m_alertLowSpin->value());
    settings.setAlertCooldownSec(m_alertCooldownSpin->value());
    settings.setTrayNotifyOnAlert(m_trayNotifyCheck->isChecked());
    settings.setShowSecondaryPrice(m_secondaryPriceCheck->isChecked());
    settings.setDarkTheme(m_darkThemeCheck->isChecked());
    settings.setShowMovingAverage(m_maCheck->isChecked());
    settings.setAlertSound(m_alertSoundCheck->isChecked());
    settings.setHotkeyEnabled(m_hotkeyCheck->isChecked());
    settings.setQuietHoursEnabled(m_quietCheck->isChecked());
    settings.setQuietStart(m_quietStartEdit->time());
    settings.setQuietEnd(m_quietEndEdit->time());
    settings.setDcaDayOfMonth(m_dcaDaySpin->value());
    settings.setDcaNote(m_dcaNoteEdit->text().trimmed());
    settings.setProxyEnabled(m_proxyCheck->isChecked());
    settings.setProxyHost(m_proxyHostEdit->text().trimmed());
    settings.setProxyPort(m_proxyPortSpin->value());
    settings.setSmartAlertMa(m_smartMaCheck->isChecked());
    settings.setSmartAlertPercentile(m_smartPctCheck->isChecked());
    settings.setPercentileLow(m_pctLowSpin->value());
    settings.setPercentileHigh(m_pctHighSpin->value());
    settings.setPositionGrams(m_posGramsSpin->value());
    settings.setPositionCost(m_posCostSpin->value());
    settings.setPremiumAlertEnabled(m_premiumCheck->isChecked());
    settings.setPremiumThresholdPct(m_premiumPctSpin->value());
    settings.setDailyReportEnabled(m_dailyReportCheck->isChecked());
    settings.setDailyReportTime(m_dailyReportTimeEdit->time());
    settings.setEventAlertEnabled(m_eventAlertCheck->isChecked());

    settings.save();

    if (oldDbDir != settings.databaseDir() || !ExtremeDatabase::instance().isOpen())
        ExtremeDatabase::instance().open();

    accept();
}

void SettingsDialog::onExitApp()
{
    const auto ret = QMessageBox::question(
        this, tr("退出确认"),
        tr("确定要退出 GoldPriceBarLite 吗？"),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (ret != QMessageBox::Yes)
        return;

    // 先保存当前表单中的设置
    onAccept();
    QApplication::quit();
}

void SettingsDialog::onIntervalChanged(int) {}

void SettingsDialog::onCheckUpdate()
{
    auto* checker = new UpdateChecker(this);
    checker->check(this, false);
}


void SettingsDialog::fillDefaultModels()
{
    if (!m_modelCombo || !m_providerCombo)
        return;
    const QString cur = m_modelCombo->currentText();
    m_modelCombo->clear();
    const QString prov = m_providerCombo->currentData().toString();
    if (prov == QStringLiteral("gemini")) {
        m_modelCombo->addItem(QStringLiteral("gemini-2.0-flash"));
        m_modelCombo->addItem(QStringLiteral("gemini-2.0-flash-lite"));
        m_modelCombo->addItem(QStringLiteral("gemini-1.5-flash"));
        m_modelCombo->addItem(QStringLiteral("gemini-1.5-pro"));
        m_modelCombo->addItem(QStringLiteral("gemini-2.5-flash-preview-05-20"));
    } else {
        m_modelCombo->addItem(QStringLiteral("grok-4.6"));
        m_modelCombo->addItem(QStringLiteral("grok-4.5"));
        m_modelCombo->addItem(QStringLiteral("grok-3-mini"));
        m_modelCombo->addItem(QStringLiteral("grok-3"));
    }
    if (!cur.isEmpty()) {
        int i = m_modelCombo->findText(cur);
        if (i >= 0)
            m_modelCombo->setCurrentIndex(i);
        else
            m_modelCombo->setEditText(cur);
    }
}

void SettingsDialog::updateApiKeyPlaceholder()
{
    if (!m_apiKeyEdit || !m_providerCombo)
        return;
    if (m_providerCombo->currentData().toString() == QStringLiteral("gemini"))
        m_apiKeyEdit->setPlaceholderText(tr("Gemini API Key（aistudio.google.com）"));
    else
        m_apiKeyEdit->setPlaceholderText(tr("xAI API Key（console.x.ai）"));
}

void SettingsDialog::onProviderChanged(int)
{
    updateApiKeyPlaceholder();
    fillDefaultModels();
    if (!m_apiKeyEdit->text().trimmed().isEmpty() && m_forecastSlider
        && m_forecastSlider->value() == 1)
        onRefreshModels();
}

void SettingsDialog::onRefreshModels()
{
    if (!m_modelsNam || !m_apiKeyEdit)
        return;
    const QString key = m_apiKeyEdit->text().trimmed();
    if (key.isEmpty()) {
        QMessageBox::information(this, tr("拉取模型"), tr("请先填写 API Key"));
        return;
    }
    if (m_modelsReply) {
        m_modelsReply->abort();
        m_modelsReply->deleteLater();
        m_modelsReply.clear();
    }

    const QString prov = m_providerCombo->currentData().toString();
    QNetworkRequest req;
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setTransferTimeout(20000);
    req.setHeader(QNetworkRequest::UserAgentHeader,
                  QStringLiteral("GoldPriceBarLite/0.7.7"));

    if (prov == QStringLiteral("gemini")) {
        const QUrl url(QStringLiteral(
            "https://generativelanguage.googleapis.com/v1beta/models?key=%1&pageSize=100")
                           .arg(QString::fromUtf8(QUrl::toPercentEncoding(key))));
        req.setUrl(url);
        m_modelsReply = m_modelsNam->get(req);
    } else {
        req.setUrl(QUrl(QStringLiteral("https://api.x.ai/v1/models")));
        req.setRawHeader("Authorization",
                         QByteArray("Bearer ") + key.toUtf8());
        req.setRawHeader("Accept", "application/json");
        m_modelsReply = m_modelsNam->get(req);
    }
    if (m_refreshModelsBtn)
        m_refreshModelsBtn->setEnabled(false);
    connect(m_modelsReply, &QNetworkReply::finished, this,
            &SettingsDialog::onModelsListFinished);
}

void SettingsDialog::onModelsListFinished()
{
    if (m_refreshModelsBtn)
        m_refreshModelsBtn->setEnabled(true);
    QNetworkReply* reply = m_modelsReply;
    if (!reply)
        reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply)
        return;
    if (m_modelsReply.data() == reply)
        m_modelsReply.clear();

    const QByteArray raw = reply->readAll();
    const auto err = reply->error();
    const QString errStr = reply->errorString();
    reply->deleteLater();

    if (err != QNetworkReply::NoError) {
        QMessageBox::warning(this, tr("拉取模型"),
                             tr("请求失败：%1").arg(errStr));
        return;
    }

    QJsonParseError pe{};
    const QJsonDocument doc = QJsonDocument::fromJson(raw, &pe);
    if (pe.error != QJsonParseError::NoError) {
        QMessageBox::warning(this, tr("拉取模型"), tr("JSON 解析失败"));
        return;
    }

    QStringList models;
    const QString prov = m_providerCombo->currentData().toString();
    if (prov == QStringLiteral("gemini")) {
        const QJsonArray arr = doc.object().value(QStringLiteral("models")).toArray();
        for (const QJsonValue& v : arr) {
            const QJsonObject o = v.toObject();
            const QJsonArray methods = o.value(QStringLiteral("supportedGenerationMethods")).toArray();
            bool canGen = false;
            for (const QJsonValue& m : methods) {
                if (m.toString() == QStringLiteral("generateContent")) {
                    canGen = true;
                    break;
                }
            }
            if (!canGen)
                continue;
            QString name = o.value(QStringLiteral("name")).toString();
            if (name.startsWith(QStringLiteral("models/")))
                name = name.mid(7);
            if (!name.isEmpty())
                models.append(name);
        }
    } else {
        // OpenAI-style: { data: [ { id: "grok-..." } ] }
        QJsonArray arr = doc.object().value(QStringLiteral("data")).toArray();
        if (arr.isEmpty() && doc.isArray())
            arr = doc.array();
        for (const QJsonValue& v : arr) {
            const QString id = v.toObject().value(QStringLiteral("id")).toString();
            if (!id.isEmpty())
                models.append(id);
        }
    }

    models.removeDuplicates();
    models.sort();
    if (models.isEmpty()) {
        QMessageBox::information(this, tr("拉取模型"),
                                 tr("未解析到可用模型，已保留默认列表"));
        fillDefaultModels();
        return;
    }

    const QString keep = m_modelCombo->currentText();
    m_modelCombo->clear();
    for (const QString& m : models)
        m_modelCombo->addItem(m);
    int i = m_modelCombo->findText(keep);
    if (i >= 0)
        m_modelCombo->setCurrentIndex(i);
    else if (!keep.isEmpty())
        m_modelCombo->setEditText(keep);

    QMessageBox::information(this, tr("拉取模型"),
                             tr("已加载 %1 个模型").arg(models.size()));
}

void SettingsDialog::applyDialogTheme()
{
    const bool dark = AppSettings::instance().darkTheme();
    const QString up = dark ? QStringLiteral(":/arrow_up_dark.png")
                            : QStringLiteral(":/arrow_up.png");
    const QString down = dark ? QStringLiteral(":/arrow_down_dark.png")
                              : QStringLiteral(":/arrow_down.png");

    // 显式 image，避免 stylesheet 把系统箭头“吃掉”
    const QString spinArrows = QStringLiteral(
        "QSpinBox::up-button, QDoubleSpinBox::up-button, QTimeEdit::up-button,"
        "QDateTimeEdit::up-button {"
        "  subcontrol-origin: border;"
        "  subcontrol-position: top right;"
        "  width: 24px;"
        "  height: 16px;"
        "  border: none;"
        "  background: transparent;"
        "}"
        "QSpinBox::down-button, QDoubleSpinBox::down-button, QTimeEdit::down-button,"
        "QDateTimeEdit::down-button {"
        "  subcontrol-origin: border;"
        "  subcontrol-position: bottom right;"
        "  width: 24px;"
        "  height: 16px;"
        "  border: none;"
        "  background: transparent;"
        "}"
        "QSpinBox::up-arrow, QDoubleSpinBox::up-arrow, QTimeEdit::up-arrow,"
        "QDateTimeEdit::up-arrow {"
        "  image: url(%1);"
        "  width: 12px; height: 12px;"
        "}"
        "QSpinBox::down-arrow, QDoubleSpinBox::down-arrow, QTimeEdit::down-arrow,"
        "QDateTimeEdit::down-arrow {"
        "  image: url(%2);"
        "  width: 12px; height: 12px;"
        "}"
        "QComboBox::drop-down {"
        "  subcontrol-origin: padding;"
        "  subcontrol-position: top right;"
        "  width: 28px;"
        "  border: none;"
        "  background: transparent;"
        "}"
        "QComboBox::down-arrow {"
        "  image: url(%2);"
        "  width: 12px; height: 12px;"
        "}")
                                         .arg(up, down);

    for (auto* sp : findChildren<QAbstractSpinBox*>()) {
        sp->setButtonSymbols(QAbstractSpinBox::UpDownArrows);
        if (sp->minimumHeight() < 34)
            sp->setMinimumHeight(34);
    }
    for (auto* cb : findChildren<QComboBox*>()) {
        cb->setMinimumHeight(34);
    }

    if (dark) {
        setStyleSheet(
            QStringLiteral(
                "QDialog, QScrollArea, QWidget#qt_scrollarea_viewport, QWidget#settingsFormHost {"
                "  background: #1e222a; color: #e8eaed; }"
                "QLabel { font-size: 13px; color: #cfd3dc; }"
                "QComboBox, QLineEdit, QSpinBox, QDoubleSpinBox, QTimeEdit, QDateTimeEdit {"
                "  min-height: 34px; max-height: 38px;"
                "  min-width: 110px; max-width: 280px;"
                "  padding: 2px 28px 2px 10px;"
                "  border: 1px solid #3d4450; border-radius: 6px;"
                "  background: #2a303a; color: #e8eaed; font-size: 13px;"
                "  selection-background-color: #0052d9; selection-color: #ffffff; }"
                "QLineEdit { padding-right: 10px; max-width: 360px; }"
                "QComboBox QAbstractItemView {"
                "  min-width: 160px; padding: 4px; background: #2a303a; color: #e8eaed;"
                "  selection-background-color: #0052d9; border: 1px solid #3d4450; }"
                "QCheckBox { spacing: 8px; min-height: 26px; font-size: 13px; color: #e8eaed; }"
                "QPushButton {"
                "  min-height: 30px; max-height: 34px;"
                "  min-width: 72px; max-width: 200px;"
                "  padding: 4px 12px; border-radius: 6px;"
                "  border: 1px solid #4a5160; background: #2f3642; color: #e8eaed; font-size: 13px; }"
                "QPushButton:hover { background: #3a4250; border-color: #5b8def; }"
                "QPushButton:default { background: #0052d9; color: white; border: none; }"
                "QPushButton#exitButton { color: #ff8a80; font-weight: bold; max-width: 120px; }"
                "QPushButton#exitButton:hover { background: #4a2222; }"
                "QPushButton#wideAction { max-width: 280px; }"
                "QSlider::groove:horizontal { height: 6px; border-radius: 3px; background: #3d4450; }"
                "QSlider::handle:horizontal { width: 16px; margin: -6px 0; border-radius: 8px; background: #5b8def; }"
                "QDialogButtonBox QPushButton { min-width: 72px; max-width: 100px; }")
            + spinArrows);
    } else {
        setStyleSheet(
            QStringLiteral(
                "QDialog, QScrollArea, QWidget#settingsFormHost { background: #f7f8fa; color: #333; }"
                "QLabel { font-size: 13px; color: #333; }"
                "QComboBox, QLineEdit, QSpinBox, QDoubleSpinBox, QTimeEdit, QDateTimeEdit {"
                "  min-height: 34px; max-height: 38px;"
                "  min-width: 110px; max-width: 280px;"
                "  padding: 2px 28px 2px 10px;"
                "  border: 1px solid #c5cdd8; border-radius: 6px;"
                "  background: #ffffff; color: #1a1d23; font-size: 13px; }"
                "QLineEdit { padding-right: 10px; max-width: 360px; }"
                "QComboBox QAbstractItemView {"
                "  min-width: 160px; padding: 4px; background: #ffffff; color: #1a1d23;"
                "  selection-background-color: #0052d9; selection-color: #ffffff; }"
                "QCheckBox { spacing: 8px; min-height: 26px; font-size: 13px; color: #333; }"
                "QPushButton {"
                "  min-height: 30px; max-height: 34px;"
                "  min-width: 72px; max-width: 200px;"
                "  padding: 4px 12px; border-radius: 6px;"
                "  border: 1px solid #c5cdd8; background: #ffffff; color: #1a1d23; font-size: 13px; }"
                "QPushButton:hover { background: #eef3ff; border-color: #0052d9; }"
                "QPushButton:default { background: #0052d9; color: white; border: none; }"
                "QPushButton#exitButton { color: #c0392b; font-weight: bold; max-width: 120px; }"
                "QPushButton#exitButton:hover { background: #fdecea; }"
                "QPushButton#wideAction { max-width: 280px; }"
                "QSlider::groove:horizontal { height: 6px; border-radius: 3px; background: #dde3ea; }"
                "QSlider::handle:horizontal { width: 16px; margin: -6px 0; border-radius: 8px; background: #0052d9; }"
                "QDialogButtonBox QPushButton { min-width: 72px; max-width: 100px; }")
            + spinArrows);
    }
}
