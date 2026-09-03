#ifndef SETTINGSDIALOG_H
#define SETTINGSDIALOG_H

#include <QDialog>

class QComboBox;
class QSlider;
class QLabel;
class QCheckBox;
class QLineEdit;
class QPushButton;
class QDoubleSpinBox;

class SettingsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SettingsDialog(QWidget* parent = nullptr);

private slots:
    void onAccept();
    void onIntervalChanged(int index);
    void onOpacityChanged(int value);
    void onForecastSliderChanged(int value);
    void onExitApp();

private:
    void setupUi();
    void loadFromSettings();
    void updateForecastUiState();

    QComboBox* m_intervalCombo = nullptr;
    QComboBox* m_sourceCombo = nullptr;
    QSlider* m_opacitySlider = nullptr;
    QLabel* m_opacityValueLabel = nullptr;
    QCheckBox* m_autoStartCheck = nullptr;

    QSlider* m_forecastSlider = nullptr;
    QLabel* m_forecastModeLabel = nullptr;
    QLineEdit* m_apiKeyEdit = nullptr;
    QComboBox* m_modelCombo = nullptr;
    QLabel* m_apiKeyLabel = nullptr;
    QLabel* m_modelLabel = nullptr;

    QLineEdit* m_dbDirEdit = nullptr;
    QPushButton* m_dbDirBrowseBtn = nullptr;

    QDoubleSpinBox* m_alertHighSpin = nullptr;
    QDoubleSpinBox* m_alertLowSpin = nullptr;
};

#endif // SETTINGSDIALOG_H
