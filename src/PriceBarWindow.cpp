#include "PriceBarWindow.h"
#include "PriceService.h"
#include "SettingsDialog.h"
#include "ChartWindow.h"
#include "AppSettings.h"
#include "HistoryCache.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QToolButton>
#include <QMouseEvent>
#include <QCloseEvent>
#include <QMenu>
#include <QApplication>
#include <QStyle>
#include <QScreen>

PriceBarWindow::PriceBarWindow(QWidget* parent)
    : QWidget(parent)
{
    setWindowFlags(Qt::FramelessWindowHint
                   | Qt::WindowStaysOnTopHint
                   | Qt::Tool);
    setAttribute(Qt::WA_TranslucentBackground, false);
    setFixedHeight(36);
    setMinimumWidth(360);

    setupUi();
    setupTray();
    applyOpacity();

    m_priceService = new PriceService(this);
    connect(m_priceService, &PriceService::priceUpdated,
            this, &PriceBarWindow::onPriceUpdated);
    connect(m_priceService, &PriceService::fetchFailed,
            this, &PriceBarWindow::onFetchFailed);
    connect(m_priceService, &PriceService::extremesUpdated, this, [this]() {
        // 全日分时写入后刷新词条「高」
        double high = 0.0;
        if (HistoryCache::instance().todayHigh(high)) {
            if (m_priceService->hasValidPrice())
                high = qMax(high, m_priceService->lastPrice());
            m_highLabel->setText(tr("高 %1").arg(high, 0, 'f', 2));
        }
    });

    connect(&AppSettings::instance(), &AppSettings::settingsChanged,
            this, &PriceBarWindow::onSettingsChanged);

    m_priceService->start();

    // 初始位置：屏幕右上角附近
    if (QScreen* screen = QApplication::primaryScreen()) {
        const QRect geo = screen->availableGeometry();
        move(geo.right() - width() - 20, geo.top() + 40);
    }
}

PriceBarWindow::~PriceBarWindow()
{
    AppSettings::instance().save();
}

void PriceBarWindow::setupUi()
{
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(10, 4, 8, 4);
    layout->setSpacing(8);

    m_sourceLabel = new QLabel(tr("浙商积存金"), this);
    m_sourceLabel->setStyleSheet("color: #cccccc; font-size: 12px;");

    m_priceLabel = new QLabel(tr("--.--"), this);
    m_priceLabel->setStyleSheet("color: #ffffff; font-size: 15px; font-weight: bold;");

    m_changeLabel = new QLabel(tr(""), this);
    m_changeLabel->setStyleSheet("font-size: 12px;");

    m_highLabel = new QLabel(tr("高 --.--"), this);
    m_highLabel->setStyleSheet("color: #e74c3c; font-size: 12px;");
    m_highLabel->setToolTip(tr("今日最高价（来自全日分时接口，截至当前）"));

    m_chartButton = new QToolButton(this);
    m_chartButton->setText(QStringLiteral("📈"));
    m_chartButton->setToolTip(tr("查看今日分时曲线"));
    m_chartButton->setAutoRaise(true);
    m_chartButton->setFixedSize(28, 28);
    connect(m_chartButton, &QToolButton::clicked,
            this, &PriceBarWindow::onChartClicked);

    m_settingsButton = new QToolButton(this);
    m_settingsButton->setText(QStringLiteral("⚙"));
    m_settingsButton->setToolTip(tr("设置"));
    m_settingsButton->setAutoRaise(true);
    m_settingsButton->setFixedSize(28, 28);
    connect(m_settingsButton, &QToolButton::clicked,
            this, &PriceBarWindow::onSettingsClicked);

    layout->addWidget(m_sourceLabel);
    layout->addWidget(m_priceLabel);
    layout->addWidget(m_changeLabel);
    layout->addWidget(m_highLabel);
    layout->addStretch();
    layout->addWidget(m_chartButton);
    layout->addWidget(m_settingsButton);

    // 深色背景
    setStyleSheet(
        "PriceBarWindow {"
        "  background-color: rgba(30, 30, 30, 230);"
        "  border: 1px solid #555555;"
        "  border-radius: 6px;"
        "}"
        "QToolButton {"
        "  color: #dddddd;"
        "  border: none;"
        "  font-size: 14px;"
        "}"
        "QToolButton:hover {"
        "  background-color: rgba(80, 80, 80, 180);"
        "  border-radius: 4px;"
        "}"
    );
}

void PriceBarWindow::setupTray()
{
    m_trayIcon = new QSystemTrayIcon(this);
    m_trayIcon->setIcon(style()->standardIcon(QStyle::SP_ComputerIcon));
    m_trayIcon->setToolTip(tr("GoldPriceBarLite"));

    auto* menu = new QMenu(this);
    menu->addAction(tr("显示/隐藏价格条"), this, [this]() {
        setVisible(!isVisible());
    });
    menu->addSeparator();
    menu->addAction(tr("退出"), qApp, &QApplication::quit);

    m_trayIcon->setContextMenu(menu);
    connect(m_trayIcon, &QSystemTrayIcon::activated,
            this, &PriceBarWindow::onTrayActivated);
    m_trayIcon->show();
}

void PriceBarWindow::updatePriceDisplay(double price, double change, const QString& sourceName)
{
    m_sourceLabel->setText(sourceName);
    m_priceLabel->setText(QString::number(price, 'f', 2));

    const QString changeText = (change >= 0 ? "+" : "")
                               + QString::number(change, 'f', 2);
    m_changeLabel->setText(changeText);

    // 红涨绿跌
    if (change > 0) {
        m_changeLabel->setStyleSheet("color: #e74c3c; font-size: 12px;");
    } else if (change < 0) {
        m_changeLabel->setStyleSheet("color: #2ecc71; font-size: 12px;");
    } else {
        m_changeLabel->setStyleSheet("color: #aaaaaa; font-size: 12px;");
    }

    // 今日最高：全日分时截至当前 + 与现价取大（避免遗漏启动前高点或刚创新高）
    double high = 0.0;
    if (HistoryCache::instance().todayHigh(high)) {
        high = qMax(high, price);
        m_highLabel->setText(tr("高 %1").arg(high, 0, 'f', 2));
    } else {
        m_highLabel->setText(tr("高 %1").arg(price, 0, 'f', 2));
    }
}

void PriceBarWindow::applyOpacity()
{
    setWindowOpacity(AppSettings::instance().opacity());
}

void PriceBarWindow::onPriceUpdated(double price, double change, const QString& sourceName)
{
    updatePriceDisplay(price, change, sourceName);
}

void PriceBarWindow::onFetchFailed(const QString& error)
{
    m_priceLabel->setText(tr("--.--"));
    m_changeLabel->setText(error);
    m_changeLabel->setStyleSheet("color: #e67e22; font-size: 11px;");
}

void PriceBarWindow::onSettingsClicked()
{
    if (!m_settingsDialog) {
        m_settingsDialog = new SettingsDialog(this);
    }
    m_settingsDialog->show();
    m_settingsDialog->raise();
    m_settingsDialog->activateWindow();
}

void PriceBarWindow::onChartClicked()
{
    if (!m_chartWindow) {
        m_chartWindow = new ChartWindow(this);
        // 新价格到达时，若曲线窗口已打开则自动刷新
        connect(m_priceService, &PriceService::priceUpdated,
                m_chartWindow, &ChartWindow::onNewPrice);
    }
    m_chartWindow->refreshData();
    m_chartWindow->show();
    m_chartWindow->raise();
    m_chartWindow->activateWindow();
}

void PriceBarWindow::onTrayActivated(QSystemTrayIcon::ActivationReason reason)
{
    if (reason == QSystemTrayIcon::Trigger
        || reason == QSystemTrayIcon::DoubleClick) {
        setVisible(!isVisible());
    }
}

void PriceBarWindow::onSettingsChanged()
{
    applyOpacity();
    m_priceService->setInterval(AppSettings::instance().refreshIntervalMs());
    // 切换数据源后清空当日缓存，避免浙商与伦敦金价格混在同一曲线
    HistoryCache::instance().clear();
    m_priceService->forceRefresh();
}

void PriceBarWindow::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        m_dragging = true;
        m_dragOffset = event->globalPosition().toPoint() - frameGeometry().topLeft();
        event->accept();
    }
}

void PriceBarWindow::mouseMoveEvent(QMouseEvent* event)
{
    if (m_dragging && (event->buttons() & Qt::LeftButton)) {
        move(event->globalPosition().toPoint() - m_dragOffset);
        event->accept();
    }
}

void PriceBarWindow::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        m_dragging = false;
        event->accept();
    }
}

void PriceBarWindow::closeEvent(QCloseEvent* event)
{
    // 关闭时隐藏到托盘，而不是退出
    hide();
    event->ignore();
}
