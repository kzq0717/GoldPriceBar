#ifndef PRICEBARWINDOW_H
#define PRICEBARWINDOW_H

#include <QWidget>
#include <QPoint>
#include <QSystemTrayIcon>
#include <QDateTime>
#include <QPointer>

class QLabel;
class QToolButton;
class QTimer;
class QNetworkAccessManager;
class QNetworkReply;
class PriceService;
class SettingsDialog;
class ChartWindow;

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
    void onSecondaryTimer();
    void onSecondaryFinished(QNetworkReply* reply);

private:
    void setupUi();
    void setupTray();
    void updatePriceDisplay(double price, double change, const QString& sourceName);
    enum class AlertKind { None, High, Low };

    void applyOpacity();
    void updateAlertIndicator(double price);
    void maybeTrayNotify(AlertKind kind, double price);
    void updateSecondaryVisibility();

    QLabel* m_sourceLabel = nullptr;
    QLabel* m_priceLabel = nullptr;
    QLabel* m_changeLabel = nullptr;
    QLabel* m_highLabel = nullptr;
    QLabel* m_secondaryLabel = nullptr; // 对照价（如伦敦金）
    QLabel* m_alertDot = nullptr;
    QToolButton* m_chartButton = nullptr;
    QToolButton* m_settingsButton = nullptr;
    QTimer* m_alertBlinkTimer = nullptr;
    QTimer* m_secondaryTimer = nullptr;

    PriceService* m_priceService = nullptr;
    SettingsDialog* m_settingsDialog = nullptr;
    ChartWindow* m_chartWindow = nullptr;
    QSystemTrayIcon* m_trayIcon = nullptr;
    QNetworkAccessManager* m_secondaryNam = nullptr;
    QPointer<QNetworkReply> m_secondaryReply;

    bool m_dragging = false;
    QPoint m_dragOffset;
    double m_lastPrice = 0.0;
    AlertKind m_alertKind = AlertKind::None;
    bool m_alertLit = false;
    QDateTime m_lastHighNotify;
    QDateTime m_lastLowNotify;
};

#endif // PRICEBARWINDOW_H
