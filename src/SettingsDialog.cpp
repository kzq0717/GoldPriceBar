#include "SettingsDialog.h"
#include "AppSettings.h"
#include "EventCalendar.h"
#include "ExtremeDatabase.h"
#include "Logger.h"
#include "UpdateChecker.h"

#include <QtMath>
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
#include <QMessageBox>
#include <QScrollArea>
#include <QListWidget>
#include <QStackedWidget>
#include <QDir>
#include <QFrame>

SettingsDialog::SettingsDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("设置 - GoldPriceBarLite %1").arg(QApplication::applicationVersion()));
    setupUi();
    loadFromSettings();
    applyDialogTheme();
}

void SettingsDialog::setupUi()
{
    setMinimumSize(720, 520);
    resize(780, 560);

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(12, 12, 12, 12);
    mainLayout->setSpacing(10);

    auto* body = new QHBoxLayout;
    body->setSpacing(12);

    m_categoryList = new QListWidget(this);
    m_categoryList->setObjectName(QStringLiteral("settingsCategoryList"));
    m_categoryList->setFixedWidth(148);
    m_categoryList->setSpacing(2);
    m_categoryList->setMovement(QListWidget::Static);
    m_categoryList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    const QStringList cats = {
        tr("行情显示"),
        tr("预警提醒"),
        tr("价格预测"),
        tr("持仓报告"),
        tr("高级")
    };
    for (const QString& c : cats)
        m_categoryList->addItem(c);
    m_categoryList->setCurrentRow(0);
    body->addWidget(m_categoryList);

    m_stack = new QStackedWidget(this);
    m_stack->setObjectName(QStringLiteral("settingsStack"));

    auto makePage = [this](const QString& title) -> QFormLayout* {
        auto* page = new QWidget(m_stack);
        page->setObjectName(QStringLiteral("settingsFormHost"));
        auto* outer = new QVBoxLayout(page);
        outer->setContentsMargins(0, 0, 0, 0);
        auto* scroll = new QScrollArea(page);
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        auto* host = new QWidget;
        host->setObjectName(QStringLiteral("settingsFormHost"));
        auto* form = new QFormLayout(host);
        form->setContentsMargins(8, 4, 12, 12);
        form->setSpacing(12);
        form->setHorizontalSpacing(16);
        form->setFieldGrowthPolicy(QFormLayout::FieldsStayAtSizeHint);
        form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
        auto* head = new QLabel(title, host);
        head->setObjectName(QStringLiteral("settingsPageTitle"));
        head->setStyleSheet("font-size:16px;font-weight:700;padding:4px 0 8px 0;");
        form->addRow(head);
        scroll->setWidget(host);
        outer->addWidget(scroll);
        m_stack->addWidget(page);
        return form;
    };

    // ---- 0 行情显示 ----
    {
        auto* form = makePage(tr("行情与显示"));
        m_intervalCombo = new QComboBox(this);
        m_intervalCombo->setMinimumWidth(200);
        m_intervalCombo->addItem(tr("1 秒"), 1000);
        m_intervalCombo->addItem(tr("2 秒"), 2000);
        m_intervalCombo->addItem(tr("5 秒"), 5000);
        m_intervalCombo->addItem(tr("10 秒"), 10000);
        m_intervalCombo->addItem(tr("30 秒"), 30000);
        form->addRow(tr("刷新频率："), m_intervalCombo);

        m_sourceCombo = new QComboBox(this);
        m_sourceCombo->setMinimumWidth(220);
        // 与 jin.20021002.xyz / GoldAccumulationRealTimeMonitor 一致
        m_sourceCombo->addItem(tr("浙商积存金"), "zs");
        m_sourceCombo->addItem(tr("民生积存金"), "ms");
        m_sourceCombo->addItem(tr("兴业积存金"), "cib");
        m_sourceCombo->addItem(tr("工商积存金"), "icbc");
        m_sourceCombo->addItem(tr("招商积存金"), "cmb");
        m_sourceCombo->addItem(tr("广发积存金"), "cgb");
        m_sourceCombo->addItem(tr("农业积存金"), "abc");
        m_sourceCombo->addItem(tr("建设积存金"), "ccb");
        m_sourceCombo->addItem(tr("中国银行积存金"), "boc");
        m_sourceCombo->addItem(tr("京东24h金"), "jd");
        m_sourceCombo->addItem(tr("伦敦金 (XAU/USD)"), "gj");
        form->addRow(tr("数据源："), m_sourceCombo);
        m_sentimentCheck = new QCheckBox(tr("启用黄金舆情监测（独立窗口）"), this);
        form->addRow(tr("黄金舆情："), m_sentimentCheck);
        auto* srcHint = new QLabel(
            tr("主源同公开聚合接口 type=码；招商失败时可走招行官方 Au99.99。"
               "历史分时仍依赖本地采样/chart，脚本不提供历史查询。"), this);
        srcHint->setWordWrap(true);
        srcHint->setStyleSheet("color:#8b93a7;font-size:11px;");
        form->addRow("", srcHint);

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

        m_secondaryPriceCheck = new QCheckBox(tr("词条显示对照价（主源非伦敦金时显示伦敦金）"), this);
        form->addRow("", m_secondaryPriceCheck);
        m_darkThemeCheck = new QCheckBox(tr("深色主题（价格条 / 分时 / 设置）"), this);
        form->addRow("", m_darkThemeCheck);
        m_maCheck = new QCheckBox(tr("分时显示均线（优先日线 MA；否则分时滚动均线）"), this);
        form->addRow("", m_maCheck);
        m_hotkeyCheck = new QCheckBox(tr("全局热键显示/隐藏价格条（Ctrl+Shift+G）"), this);
        form->addRow("", m_hotkeyCheck);
        m_autoStartCheck = new QCheckBox(tr("开机自动启动"), this);
        form->addRow("", m_autoStartCheck);
    }

    // ---- 1 预警 ----
    {
        auto* form = makePage(tr("预警与通知"));
        m_alertHighSpin = new QDoubleSpinBox(this);
        m_alertHighSpin->setFixedWidth(140);
        m_alertHighSpin->setButtonSymbols(QAbstractSpinBox::UpDownArrows);
        m_alertHighSpin->setRange(0.0, 99999.0);
        m_alertHighSpin->setDecimals(2);
        m_alertHighSpin->setSingleStep(1.0);
        m_alertHighSpin->setSpecialValueText(tr("关闭"));
        form->addRow(tr("高价预警："), m_alertHighSpin);

        m_alertLowSpin = new QDoubleSpinBox(this);
        m_alertLowSpin->setFixedWidth(140);
        m_alertLowSpin->setButtonSymbols(QAbstractSpinBox::UpDownArrows);
        m_alertLowSpin->setRange(0.0, 99999.0);
        m_alertLowSpin->setDecimals(2);
        m_alertLowSpin->setSingleStep(1.0);
        m_alertLowSpin->setSpecialValueText(tr("关闭"));
        form->addRow(tr("低价预警："), m_alertLowSpin);

        m_alertCooldownSpin = new QSpinBox(this);
        m_alertCooldownSpin->setRange(30, 3600);
        m_alertCooldownSpin->setSuffix(tr(" 秒"));
        form->addRow(tr("预警冷却："), m_alertCooldownSpin);

        m_trayNotifyCheck = new QCheckBox(tr("触发预警时弹出系统托盘通知"), this);
        form->addRow("", m_trayNotifyCheck);
        m_alertSoundCheck = new QCheckBox(tr("预警时播放系统提示音"), this);
        form->addRow("", m_alertSoundCheck);

        m_quietCheck = new QCheckBox(tr("启用静默时段（期间不弹托盘）"), this);
        form->addRow("", m_quietCheck);
        auto* quietLay = new QHBoxLayout;
        m_quietStartEdit = new QTimeEdit(this);
        m_quietEndEdit = new QTimeEdit(this);
        m_quietStartEdit->setDisplayFormat(QStringLiteral("HH:mm"));
        m_quietEndEdit->setDisplayFormat(QStringLiteral("HH:mm"));
        quietLay->addWidget(m_quietStartEdit);
        quietLay->addWidget(new QLabel(tr("至"), this));
        quietLay->addWidget(m_quietEndEdit);
        quietLay->addStretch();
        form->addRow(tr("静默时段："), quietLay);

        m_smartMaCheck = new QCheckBox(tr("智能：突破近5日均线提示"), this);
        form->addRow("", m_smartMaCheck);
        m_smartPctCheck = new QCheckBox(tr("智能：触及近20日分位提示"), this);
        form->addRow("", m_smartPctCheck);
        auto* pctLay = new QHBoxLayout;
        m_pctLowSpin = new QSpinBox(this);
        m_pctHighSpin = new QSpinBox(this);
        m_pctLowSpin->setRange(1, 49);
        m_pctHighSpin->setRange(51, 99);
        m_pctLowSpin->setSuffix(QStringLiteral("%"));
        m_pctHighSpin->setSuffix(QStringLiteral("%"));
        pctLay->addWidget(new QLabel(tr("低分位"), this));
        pctLay->addWidget(m_pctLowSpin);
        pctLay->addWidget(new QLabel(tr("高分位"), this));
        pctLay->addWidget(m_pctHighSpin);
        pctLay->addStretch();
        form->addRow(tr("分位阈值："), pctLay);

        m_premiumCheck = new QCheckBox(tr("监控主源相对伦敦金溢价"), this);
        form->addRow("", m_premiumCheck);
        m_premiumPctSpin = new QDoubleSpinBox(this);
        m_premiumPctSpin->setRange(0.1, 50.0);
        m_premiumPctSpin->setDecimals(2);
        m_premiumPctSpin->setSuffix(QStringLiteral(" %"));
        form->addRow(tr("溢价阈值："), m_premiumPctSpin);

        m_amplitudeHintLabel = new QLabel(tr("（打开本页时自动计算近10日振幅建议）"), this);
        m_amplitudeHintLabel->setWordWrap(true);
        m_amplitudeHintLabel->setStyleSheet(QStringLiteral("color:#8b93a7;font-size:12px;"));
        form->addRow(tr("近10日振幅："), m_amplitudeHintLabel);

        m_suggestAlertBtn = new QPushButton(tr("采用近10日振幅为高低预警"), this);
        m_suggestAlertBtn->setObjectName(QStringLiteral("wideAction"));
        m_suggestAlertBtn->setToolTip(tr("与「宏观日程」无关：用本地日线最高/最低建议预警阈值"));
        form->addRow("", m_suggestAlertBtn);
        connect(m_suggestAlertBtn, &QPushButton::clicked, this, [this]() {
            refreshAmplitudeHint(true);
        });

        m_planEnabledCheck = new QCheckBox(tr("启用交易计划提醒"), this);
        form->addRow("", m_planEnabledCheck);
        m_planBuySpin = new QDoubleSpinBox(this);
        m_planBuySpin->setRange(0, 999999);
        m_planBuySpin->setDecimals(2);
        form->addRow(tr("计划买入观察价："), m_planBuySpin);
        m_planSellSpin = new QDoubleSpinBox(this);
        m_planSellSpin->setRange(0, 999999);
        m_planSellSpin->setDecimals(2);
        form->addRow(tr("计划卖出观察价："), m_planSellSpin);
        m_planInvalidSpin = new QDoubleSpinBox(this);
        m_planInvalidSpin->setRange(0, 999999);
        m_planInvalidSpin->setDecimals(2);
        m_planInvalidSpin->setToolTip(tr("价格触及则计划作废（失效条件）"));
        form->addRow(tr("计划失效价："), m_planInvalidSpin);

    }

    // ---- 2 预测 ----
    {
        auto* form = makePage(tr("价格预测"));
        auto* forecastLayout = new QHBoxLayout;
        auto* localLbl = new QLabel(tr("本地"), this);
        auto* onlineLbl = new QLabel(tr("大模型"), this);
        m_forecastSlider = new QSlider(Qt::Horizontal, this);
        m_forecastSlider->setRange(0, 1);
        m_forecastSlider->setPageStep(1);
        m_forecastSlider->setSingleStep(1);
        m_forecastSlider->setValue(0);
        m_forecastSlider->setFixedWidth(80);
        m_forecastModeLabel = new QLabel(tr("本地推演"), this);
        m_forecastModeLabel->setStyleSheet("color:#5b8def;font-size:12px;font-weight:bold;");
        forecastLayout->addWidget(localLbl);
        forecastLayout->addWidget(m_forecastSlider);
        forecastLayout->addWidget(onlineLbl);
        forecastLayout->addSpacing(8);
        forecastLayout->addWidget(m_forecastModeLabel);
        forecastLayout->addStretch();
        form->addRow(tr("预测模式："), forecastLayout);
        connect(m_forecastSlider, &QSlider::valueChanged, this, &SettingsDialog::onForecastSliderChanged);

        m_forecastIntervalSpin = new QSpinBox(this);
        m_forecastIntervalSpin->setRange(15, 3600);
        m_forecastIntervalSpin->setValue(60);
        m_forecastIntervalSpin->setSuffix(tr(" 秒"));
        m_forecastIntervalSpin->setToolTip(tr("大模型请求最小间隔，默认 60 秒；修改保存后立即生效"));
        form->addRow(tr("大模型请求间隔："), m_forecastIntervalSpin);


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
        modelLay->addWidget(m_refreshModelsBtn);
        form->addRow(m_modelLabel, modelLay);
        connect(m_refreshModelsBtn, &QPushButton::clicked, this, &SettingsDialog::onRefreshModels);
        m_modelsNam = new QNetworkAccessManager(this);
        fillDefaultModels();
    }

    // ---- 3 持仓报告 ----
    {
        auto* form = makePage(tr("持仓与报告"));
        m_posGramsSpin = new QDoubleSpinBox(this);
        m_posGramsSpin->setRange(0, 1e6);
        m_posGramsSpin->setDecimals(3);
        m_posGramsSpin->setSuffix(tr(" 克"));
        form->addRow(tr("持仓克数："), m_posGramsSpin);
        m_posCostSpin = new QDoubleSpinBox(this);
        m_posCostSpin->setRange(0, 1e7);
        m_posCostSpin->setDecimals(2);
        form->addRow(tr("成本均价："), m_posCostSpin);

        m_dailyReportCheck = new QCheckBox(tr("每日汇总报告"), this);
        form->addRow("", m_dailyReportCheck);
        m_dailyReportTimeEdit = new QTimeEdit(this);
        m_dailyReportTimeEdit->setDisplayFormat(QStringLiteral("HH:mm"));
        form->addRow(tr("报告时间："), m_dailyReportTimeEdit);

        m_dcaDaySpin = new QSpinBox(this);
        m_dcaDaySpin->setRange(0, 31);
        m_dcaDaySpin->setSpecialValueText(tr("关闭"));
        form->addRow(tr("定投提醒日："), m_dcaDaySpin);
        m_dcaNoteEdit = new QLineEdit(this);
        m_dcaNoteEdit->setPlaceholderText(tr("定投备注"));
        form->addRow(tr("定投备注："), m_dcaNoteEdit);

        m_eventAlertCheck = new QCheckBox(tr("宏观日程提醒（非农/FOMC/CPI 等）"), this);
        form->addRow("", m_eventAlertCheck);
        m_eventSummaryLabel = new QLabel(EventCalendar::summaryNear(), this);
        m_eventSummaryLabel->setWordWrap(true);
        m_eventSummaryLabel->setAlignment(Qt::AlignTop | Qt::AlignLeft);
        m_eventSummaryLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
        m_eventSummaryLabel->setStyleSheet("font-size:12px; padding: 6px;");
        m_eventSummaryLabel->setMinimumWidth(280);
        auto* eventScroll = new QScrollArea(this);
        eventScroll->setWidgetResizable(true);
        eventScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        eventScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        eventScroll->setMinimumHeight(140);
        eventScroll->setMaximumHeight(220);
        eventScroll->setFrameShape(QFrame::StyledPanel);
        eventScroll->setWidget(m_eventSummaryLabel);
        form->addRow(tr("宏观日程（本地表，非振幅）："), eventScroll);
        m_eventSummaryLabel->setToolTip(tr("非农/FOMC/CPI 等本地日程，与「近10日振幅预警」无关"));

    }

    // ---- 4 高级 ----
    {
        auto* form = makePage(tr("高级与数据"));
        m_primaryUrlEdit = new QLineEdit(this);
        m_primaryUrlEdit->setPlaceholderText(tr("主行情 URL，%1=品种"));
        form->addRow(tr("主行情URL："), m_primaryUrlEdit);
        m_chartUrlEdit = new QLineEdit(this);
        m_chartUrlEdit->setPlaceholderText(tr("分时 chart URL，%1=品种"));
        form->addRow(tr("分时URL："), m_chartUrlEdit);
        auto* urlHint = new QLabel(tr("也可直接编辑配置文件中的 primaryPriceUrl / chartUrl（无需重编译）"), this);
        urlHint->setWordWrap(true);
        urlHint->setStyleSheet("color:#8b93a7;font-size:11px;");
        form->addRow("", urlHint);

        m_dbDirEdit = new QLineEdit(this);
        m_dbDirBrowseBtn = new QPushButton(tr("浏览…"), this);
        auto* dbLay = new QHBoxLayout;
        dbLay->addWidget(m_dbDirEdit, 1);
        dbLay->addWidget(m_dbDirBrowseBtn);
        form->addRow(tr("数据库目录："), dbLay);
        connect(m_dbDirBrowseBtn, &QPushButton::clicked, this, [this]() {
            const QString dir = QFileDialog::getExistingDirectory(this, tr("选择数据库目录"));
            if (!dir.isEmpty())
                m_dbDirEdit->setText(dir);
        });

        m_proxyCheck = new QCheckBox(tr("启用 HTTP 代理"), this);
        form->addRow("", m_proxyCheck);
        m_proxyHostEdit = new QLineEdit(this);
        m_proxyHostEdit->setPlaceholderText(QStringLiteral("127.0.0.1"));
        form->addRow(tr("代理主机："), m_proxyHostEdit);
        m_proxyPortSpin = new QSpinBox(this);
        m_proxyPortSpin->setRange(1, 65535);
        m_proxyPortSpin->setValue(7890);
        form->addRow(tr("代理端口："), m_proxyPortSpin);

        auto* tip = new QLabel(
            tr("历史日线可用 scripts/import_historical_gold.py 导入；单位见 scripts/README.md。"),
            this);
        tip->setWordWrap(true);
        tip->setStyleSheet("font-size:11px;color:#8b93a7;");
        form->addRow(tip);
    }

    body->addWidget(m_stack, 1);
    mainLayout->addLayout(body, 1);

    connect(m_categoryList, &QListWidget::currentRowChanged, this, &SettingsDialog::onCategoryChanged);

    auto* hint = new QLabel(
        tr("分类切换无需滚动整页。高/低预警为 0 表示关闭。"),
        this);
    hint->setWordWrap(true);
    hint->setStyleSheet("font-size:11px;");
    mainLayout->addWidget(hint);

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

    updateForecastUiState();
}

void SettingsDialog::onCategoryChanged(int row)
{
    if (m_stack && row >= 0 && row < m_stack->count())
        m_stack->setCurrentIndex(row);
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
    if (m_sentimentCheck)
        m_sentimentCheck->setChecked(settings.sentimentEnabled());

    const int opacityPercent = static_cast<int>(settings.opacity() * 100);
    m_opacitySlider->setValue(opacityPercent);
    m_opacityValueLabel->setText(QString("%1%").arg(opacityPercent));

    m_autoStartCheck->setChecked(settings.autoStart());
    m_dbDirEdit->setText(settings.databaseDir());
    if (m_primaryUrlEdit)
        m_primaryUrlEdit->setText(settings.primaryPriceUrl());
    if (m_chartUrlEdit)
        m_chartUrlEdit->setText(settings.chartUrl());

    m_alertHighSpin->setValue(settings.alertHigh());
    if (m_planEnabledCheck)
        m_planEnabledCheck->setChecked(settings.planEnabled());
    if (m_planBuySpin)
        m_planBuySpin->setValue(settings.planBuyPrice());
    if (m_planSellSpin)
        m_planSellSpin->setValue(settings.planSellPrice());
    if (m_planInvalidSpin)
        m_planInvalidSpin->setValue(settings.planInvalidPrice());
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
    refreshAmplitudeHint(false);


    m_forecastSlider->setValue(settings.forecastOnline() ? 1 : 0);
    if (m_forecastIntervalSpin)
        m_forecastIntervalSpin->setValue(settings.forecastIntervalSec());
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
    if (m_sentimentCheck)
        settings.setSentimentEnabled(m_sentimentCheck->isChecked());
    settings.setOpacity(m_opacitySlider->value() / 100.0);
    settings.setAutoStart(m_autoStartCheck->isChecked());
    settings.setForecastOnline(m_forecastSlider->value() >= 1);
    if (m_forecastIntervalSpin)
        settings.setForecastIntervalSec(m_forecastIntervalSpin->value());
    if (m_providerCombo)
        settings.setLlmProvider(m_providerCombo->currentData().toString());
    settings.setXaiApiKey(m_apiKeyEdit->text().trimmed());
    settings.setXaiModel(m_modelCombo->currentText().trimmed());
    settings.setDatabaseDir(m_dbDirEdit->text().trimmed());
    if (m_primaryUrlEdit)
        settings.setPrimaryPriceUrl(m_primaryUrlEdit->text().trimmed());
    if (m_chartUrlEdit)
        settings.setChartUrl(m_chartUrlEdit->text().trimmed());
    settings.setAlertHigh(m_alertHighSpin->value());
    if (m_planEnabledCheck)
        settings.setPlanEnabled(m_planEnabledCheck->isChecked());
    if (m_planBuySpin)
        settings.setPlanBuyPrice(m_planBuySpin->value());
    if (m_planSellSpin)
        settings.setPlanSellPrice(m_planSellSpin->value());
    if (m_planInvalidSpin)
        settings.setPlanInvalidPrice(m_planInvalidSpin->value());
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
        m_modelCombo->addItem(QStringLiteral("gemini-3.6-flash"));
        m_modelCombo->addItem(QStringLiteral("gemini-3.5-flash"));
        m_modelCombo->addItem(QStringLiteral("gemini-3.5-flash-lite"));
        m_modelCombo->addItem(QStringLiteral("gemini-3.8-flash"));
        m_modelCombo->addItem(QStringLiteral("gemini-2.5-flash"));
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
                "  background: #0f1219; color: #e8eaed; }"
                "QLabel { font-size: 13px; color: #cfd3dc; }"
                "QComboBox, QLineEdit, QSpinBox, QDoubleSpinBox, QTimeEdit, QDateTimeEdit {"
                "  min-height: 36px; max-height: 40px;"
                "  min-width: 110px; max-width: 280px;"
                "  padding: 2px 28px 2px 12px;"
                "  border: 1px solid #2a3347; border-radius: 10px;"
                "  background: #161b27; color: #e8eaed; font-size: 13px;"
                "  selection-background-color: #0052d9; selection-color: #ffffff; }"
                "QLineEdit { padding-right: 10px; max-width: 360px; }"
                "QComboBox QAbstractItemView {"
                "  min-width: 160px; padding: 4px; background: #2a303a; color: #e8eaed;"
                "  selection-background-color: #0052d9; border: 1px solid #3d4450; }"
                "QCheckBox { spacing: 8px; min-height: 26px; font-size: 13px; color: #e8eaed; }"
                "QPushButton {"
                "  min-height: 32px; max-height: 36px;"
                "  min-width: 72px; max-width: 200px;"
                "  padding: 6px 14px; border-radius: 10px;"
                "  border: 1px solid #2a3347; background: #1a2233; color: #e8eaed; font-size: 13px; }"
                "QPushButton:hover { background: #243049; border-color: #5b8def; }"
                "QPushButton:default { background: #5b8def; color: #0f1219; border: none; font-weight: 600; }"
                "QPushButton#exitButton { color: #ff8a80; font-weight: bold; max-width: 120px; }"
                "QPushButton#exitButton:hover { background: #4a2222; }"
                "QPushButton#wideAction { max-width: 280px; }"
                "QSlider::groove:horizontal { height: 6px; border-radius: 3px; background: #3d4450; }"
                "QSlider::handle:horizontal { width: 16px; margin: -6px 0; border-radius: 8px; background: #5b8def; }"
                "QDialogButtonBox QPushButton { min-width: 72px; max-width: 100px; }"
                "QListWidget#settingsCategoryList {"
                "  background:#161b27;border:1px solid #2a3347;border-radius:12px;"
                "  padding:8px 6px;outline:none;}"
                "QListWidget#settingsCategoryList::item {"
                "  color:#8b93a7;padding:10px 12px;border-radius:8px;margin:2px 0;}"
                "QListWidget#settingsCategoryList::item:selected {"
                "  background:#243049;color:#e8eaed;font-weight:600;}"
                "QListWidget#settingsCategoryList::item:hover {"
                "  background:#1a2233;color:#c5cbe0;}"
                "QLabel#settingsPageTitle{color:#e8eaed;}")
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
                "QDialogButtonBox QPushButton { min-width: 72px; max-width: 100px; }"
                "QListWidget#settingsCategoryList {"
                "  background:#f3f6fb;border:1px solid #d8dee6;border-radius:12px;"
                "  padding:8px 6px;outline:none;}"
                "QListWidget#settingsCategoryList::item {"
                "  color:#5c6b77;padding:10px 12px;border-radius:8px;margin:2px 0;}"
                "QListWidget#settingsCategoryList::item:selected {"
                "  background:#e8eefc;color:#0f172a;font-weight:600;}"
                "QListWidget#settingsCategoryList::item:hover {"
                "  background:#eef2f7;color:#1a1d23;}"
                "QLabel#settingsPageTitle{color:#0f172a;}")
            + spinArrows);
    }
}


void SettingsDialog::refreshAmplitudeHint(bool applyToSpins)
{
    if (!m_amplitudeHintLabel && !applyToSpins)
        return;
    auto closes = ExtremeDatabase::instance().loadRecentDailyCloses(
        10, AppSettings::instance().dataSource());
    if (closes.size() < 3)
        closes = ExtremeDatabase::instance().loadRecentDailyCloses(10, QStringLiteral("gj"));
    if (closes.size() < 3) {
        if (m_amplitudeHintLabel)
            m_amplitudeHintLabel->setText(tr("日线样本不足（需导入历史或运行累积）"));
        return;
    }
    double mn = closes.first().second, mx = mn;
    double sum = 0.0;
    for (const auto& c : closes) {
        mn = qMin(mn, c.second);
        mx = qMax(mx, c.second);
        sum += c.second;
    }
    const double avg = sum / closes.size();
    const double amp = mx - mn;
    const QString text = tr("近%1日 收盘均 %2  |  最低 %3  最高 %4  |  振幅 %5 (%6%)")
                             .arg(closes.size())
                             .arg(avg, 0, 'f', 2)
                             .arg(mn, 0, 'f', 2)
                             .arg(mx, 0, 'f', 2)
                             .arg(amp, 0, 'f', 2)
                             .arg(avg > 0 ? amp / avg * 100.0 : 0.0, 0, 'f', 2);
    if (m_amplitudeHintLabel)
        m_amplitudeHintLabel->setText(text);
    if (applyToSpins) {
        if (m_alertLowSpin)
            m_alertLowSpin->setValue(mn);
        if (m_alertHighSpin)
            m_alertHighSpin->setValue(mx);
    }
}
