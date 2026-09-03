#include "SettingsDialog.h"
#include "AppSettings.h"
#include "ExtremeDatabase.h"
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
#include <QSpinBox>
#include <QTimeEdit>
#include <QDesktopServices>
#include <QUrl>
#include <QApplication>
#include <QMessageBox>

SettingsDialog::SettingsDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("设置 - GoldPriceBarLite %1").arg(QApplication::applicationVersion()));
    setMinimumWidth(460);
    setMinimumHeight(700);
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);

    setupUi();
    loadFromSettings();
}

void SettingsDialog::setupUi()
{
    auto* mainLayout = new QVBoxLayout(this);
    auto* form = new QFormLayout;
    form->setSpacing(10);

    m_intervalCombo = new QComboBox(this);
    m_intervalCombo->addItem(tr("1 秒"), 1000);
    m_intervalCombo->addItem(tr("2 秒"), 2000);
    m_intervalCombo->addItem(tr("5 秒"), 5000);
    m_intervalCombo->addItem(tr("10 秒"), 10000);
    m_intervalCombo->addItem(tr("30 秒"), 30000);
    form->addRow(tr("刷新频率："), m_intervalCombo);

    m_sourceCombo = new QComboBox(this);
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
    m_alertHighSpin->setRange(0.0, 99999.0);
    m_alertHighSpin->setDecimals(2);
    m_alertHighSpin->setSingleStep(1.0);
    m_alertHighSpin->setSpecialValueText(tr("关闭"));
    m_alertHighSpin->setToolTip(tr("现价达到或超过该值时，价格条红点闪烁；0=关闭"));
    form->addRow(tr("高价预警："), m_alertHighSpin);

    m_alertLowSpin = new QDoubleSpinBox(this);
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

    m_darkThemeCheck = new QCheckBox(tr("深色主题（价格条 / 分时窗口）"), this);
    form->addRow("", m_darkThemeCheck);

    m_maCheck = new QCheckBox(tr("分时显示均线 MA5分 / MA20分（按时间窗口）"), this);
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
    localLbl->setStyleSheet("color:#666;font-size:11px;");
    m_forecastSlider = new QSlider(Qt::Horizontal, this);
    m_forecastSlider->setRange(0, 1);
    m_forecastSlider->setPageStep(1);
    m_forecastSlider->setSingleStep(1);
    m_forecastSlider->setValue(0);
    m_forecastSlider->setFixedWidth(80);
    auto* onlineLbl = new QLabel(tr("大模型"), this);
    onlineLbl->setStyleSheet("color:#666;font-size:11px;");
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

    m_apiKeyEdit = new QLineEdit(this);
    m_apiKeyEdit->setEchoMode(QLineEdit::Password);
    m_apiKeyEdit->setPlaceholderText(tr("xAI API Key（console.x.ai）"));
    m_apiKeyLabel = new QLabel(tr("API Key："), this);
    form->addRow(m_apiKeyLabel, m_apiKeyEdit);

    m_modelCombo = new QComboBox(this);
    m_modelCombo->setEditable(true);
    m_modelCombo->addItem(QStringLiteral("grok-4.6"));
    m_modelCombo->addItem(QStringLiteral("grok-4.5"));
    m_modelCombo->addItem(QStringLiteral("grok-3-mini"));
    m_modelCombo->addItem(QStringLiteral("grok-3"));
    m_modelLabel = new QLabel(tr("模型："), this);
    form->addRow(m_modelLabel, m_modelCombo);

    m_autoStartCheck = new QCheckBox(tr("开机自动启动"), this);
    form->addRow("", m_autoStartCheck);

    mainLayout->addLayout(form);

    auto* hint = new QLabel(
        tr("高/低预警：0 表示关闭。触发后价格条在「高」与分时按钮之间闪烁色点。"
           "退出可直接点下方「退出软件」。"),
        this);
    hint->setWordWrap(true);
    hint->setStyleSheet("color:#888;font-size:11px;");
    mainLayout->addWidget(hint);
    mainLayout->addStretch();

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &SettingsDialog::onAccept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* exitBtn = new QPushButton(tr("退出软件"), this);
    exitBtn->setStyleSheet(
        "QPushButton{color:#c0392b;font-weight:bold;padding:6px 14px;}"
        "QPushButton:hover{background:#fdecea;}");
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

    m_forecastSlider->setValue(settings.forecastOnline() ? 1 : 0);
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
