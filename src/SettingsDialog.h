#ifndef SETTINGSDIALOG_H
#define SETTINGSDIALOG_H

#include <QDialog>
#include <QPointer>
#include <QPointer>

class QComboBox;
class QSlider;
class QLabel;
class QCheckBox;
class QLineEdit;
class QPushButton;
class QDoubleSpinBox;
class QSpinBox;
class QTimeEdit;

class SettingsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SettingsDialog(QWidget* parent = nullptr);

signals:
    void requestOpenSentiment();
    void requestChangeOpacity(double op);

protected:
    void showEvent(QShowEvent* event) override;

private slots:
    void onAccept();
    void onIntervalChanged(int index);
    void onOpacityChanged(int value);
    void onForecastSliderChanged(int value);
    void refreshAmplitudeHint(bool applyToSpins);
    void onExitApp();
    void onCheckUpdate();
    void onProviderChanged(int index);
    void onRefreshModels();
    void onModelsListFinished();

private:
    void setupUi();
    void loadFromSettings();
    void updateForecastUiState();
    void applyDialogTheme();
    void onCategoryChanged(int row);
    void fillDefaultModels();
    void updateApiKeyPlaceholder();

    QComboBox* m_intervalCombo = nullptr;
    QComboBox* m_sourceCombo;
    QCheckBox* m_sentimentCheck = nullptr;
    QSlider* m_opacitySlider = nullptr;
    QLabel* m_opacityValueLabel = nullptr;
    QCheckBox* m_autoStartCheck = nullptr;

    QSlider* m_forecastSlider = nullptr;
    QSpinBox* m_forecastIntervalSpin = nullptr;
    QLabel* m_amplitudeHintLabel = nullptr;
    QLabel* m_forecastModeLabel = nullptr;
    QComboBox* m_providerCombo = nullptr;
    QLabel* m_providerLabel = nullptr;
    QLineEdit* m_apiKeyEdit = nullptr;
    QComboBox* m_modelCombo = nullptr;
    QLabel* m_apiKeyLabel = nullptr;
    QLabel* m_modelLabel = nullptr;
    QPushButton* m_refreshModelsBtn = nullptr;
    class QNetworkAccessManager* m_modelsNam = nullptr;
    QPointer<class QNetworkReply> m_modelsReply;

    QLineEdit* m_dbDirEdit = nullptr;
    QLineEdit* m_primaryUrlEdit = nullptr;
    QLineEdit* m_chartUrlEdit = nullptr;
    QPushButton* m_dbDirBrowseBtn = nullptr;

    QDoubleSpinBox* m_alertHighSpin = nullptr;
    QDoubleSpinBox* m_alertLowSpin = nullptr;
    QSpinBox* m_alertCooldownSpin = nullptr;
    QCheckBox* m_trayNotifyCheck = nullptr;
    QCheckBox* m_secondaryPriceCheck = nullptr;
    QCheckBox* m_darkThemeCheck = nullptr;
    QCheckBox* m_maCheck = nullptr;
    QCheckBox* m_alertSoundCheck = nullptr;
    QCheckBox* m_hotkeyCheck = nullptr;
    QCheckBox* m_quietCheck = nullptr;
    QTimeEdit* m_quietStartEdit = nullptr;
    QTimeEdit* m_quietEndEdit = nullptr;
    QSpinBox* m_dcaDaySpin = nullptr;
    QLineEdit* m_dcaNoteEdit = nullptr;
    QCheckBox* m_proxyCheck = nullptr;
    QLineEdit* m_proxyHostEdit = nullptr;
    QSpinBox* m_proxyPortSpin = nullptr;
    QCheckBox* m_smartMaCheck = nullptr;
    QCheckBox* m_smartPctCheck = nullptr;
    QSpinBox* m_pctLowSpin = nullptr;
    QSpinBox* m_pctHighSpin = nullptr;
    QDoubleSpinBox* m_posGramsSpin = nullptr;
    QDoubleSpinBox* m_posCostSpin = nullptr;
    QCheckBox* m_premiumCheck = nullptr;
    QDoubleSpinBox* m_premiumPctSpin = nullptr;
    QCheckBox* m_dailyReportCheck = nullptr;
    QTimeEdit* m_dailyReportTimeEdit = nullptr;
    QCheckBox* m_eventAlertCheck = nullptr;
    QPushButton* m_suggestAlertBtn = nullptr;
    QCheckBox* m_planEnabledCheck = nullptr;
    QDoubleSpinBox* m_planBuySpin = nullptr;
    QDoubleSpinBox* m_planSellSpin = nullptr;
    QDoubleSpinBox* m_planInvalidSpin = nullptr;
    QLabel* m_eventSummaryLabel = nullptr;

    class QListWidget* m_categoryList = nullptr;
    class QStackedWidget* m_stack = nullptr;
};






#endif // SETTINGSDIALOG_H
