#ifndef PRICEBARWINDOW_H
#define PRICEBARWINDOW_H

#include <QWidget>
#include <QPoint>
#include <QSystemTrayIcon>

class QLabel;
class QToolButton;
class QTimer;
class PriceService;
class SettingsDialog;
class ChartWindow;

/**
 * @brief 主价格条窗口
 * 无边框、始终置顶、可拖动；「高」与分时按钮之间有预警闪烁点
 */
class PriceBarWindow : public QWidget
{
    Q_OBJECT

public:
    explicit PriceBarWindow(QWidget* parent = nullptr);
    ~PriceBarWindow() override;

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void closeEvent(QCloseEvent* event) override;

private slots:
    void onPriceUpdated(double price, double change, const QString& sourceName);
    void onFetchFailed(const QString& error);
    void onSettingsClicked();
    void onChartClicked();
    void onTrayActivated(QSystemTrayIcon::ActivationReason reason);
    void onSettingsChanged();
    void onAlertBlinkTick();

private:
    void setupUi();
    void setupTray();
    void updatePriceDisplay(double price, double change, const QString& sourceName);
    void applyOpacity();
    void updateAlertIndicator(double price);

    QLabel* m_sourceLabel = nullptr;
    QLabel* m_priceLabel = nullptr;
    QLabel* m_changeLabel = nullptr;
    QLabel* m_highLabel = nullptr;
    QLabel* m_alertDot = nullptr;   // 高价红闪 / 低价绿闪
    QToolButton* m_chartButton = nullptr;
    QToolButton* m_settingsButton = nullptr;
    QTimer* m_alertBlinkTimer = nullptr;

    PriceService* m_priceService = nullptr;
    SettingsDialog* m_settingsDialog = nullptr;
    ChartWindow* m_chartWindow = nullptr;
    QSystemTrayIcon* m_trayIcon = nullptr;

    bool m_dragging = false;
    QPoint m_dragOffset;
    double m_lastPrice = 0.0;
    enum class AlertKind { None, High, Low };
    AlertKind m_alertKind = AlertKind::None;
    bool m_alertLit = false;
};

#endif // PRICEBARWINDOW_H
