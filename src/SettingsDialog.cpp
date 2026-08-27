#include "SettingsDialog.h"
#include "AppSettings.h"
#include "ExtremeDatabase.h"

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

SettingsDialog::SettingsDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("设置 - GoldPriceBarLite"));
    setMinimumWidth(440);
    setFixedHeight(420);
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

    // 数据库目录
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

    // 预测模式
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
        tr("数据库文件名为 gold_extremes.db。目录留空则使用系统默认："
           "%AppData%/GoldPriceBarLite/（Windows）。修改目录后将重新打开数据库。"),
        this);
    hint->setWordWrap(true);
    hint->setStyleSheet("color:#888;font-size:11px;");
    mainLayout->addWidget(hint);
    mainLayout->addStretch();

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &SettingsDialog::onAccept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    mainLayout->addWidget(buttons);
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
    settings.save();

    // 目录变更或首次配置后重新打开数据库
    if (oldDbDir != settings.databaseDir() || !ExtremeDatabase::instance().isOpen()) {
        ExtremeDatabase::instance().open();
    }

    accept();
}

void SettingsDialog::onIntervalChanged(int) {}
