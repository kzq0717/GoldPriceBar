#ifndef PRICEBARWINDOW_H
#define PRICEBARWINDOW_H

#include <QWidget>
#include <QPoint>
#include <QSystemTrayIcon>

class QLabel;
class QToolButton;
class PriceService;
class SettingsDialog;
class ChartWindow;

/**
 * @brief 主价格条窗口
 * 无边框、始终置顶、可拖动，右侧包含曲线与设置按钮
 * 显示：数据源 | 现价 | 涨跌 | 今日最高
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

private:
    void setupUi();
    void setupTray();
    void updatePriceDisplay(double price, double change, const QString& sourceName);
    void applyOpacity();

    QLabel* m_sourceLabel = nullptr;
    QLabel* m_priceLabel = nullptr;
    QLabel* m_changeLabel = nullptr;
    QLabel* m_highLabel = nullptr;   // 今日最高（当前时间之前最大值）
    QToolButton* m_chartButton = nullptr;
    QToolButton* m_settingsButton = nullptr;

    PriceService* m_priceService = nullptr;
    SettingsDialog* m_settingsDialog = nullptr;
    ChartWindow* m_chartWindow = nullptr;
    QSystemTrayIcon* m_trayIcon = nullptr;

    bool m_dragging = false;
    QPoint m_dragOffset;
};

#endif // PRICEBARWINDOW_H
