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
#include <QTimer>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QUrl>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDesktopServices>
#include <QDateTime>

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

    m_secondaryLabel = new QLabel(this);
    m_secondaryLabel->setStyleSheet("color: #9b59b6; font-size: 11px;");
    m_secondaryLabel->setToolTip(tr("对照行情（伦敦金）"));
    m_secondaryLabel->hide();

    m_highLabel = new QLabel(tr("高 --.--"), this);
    m_highLabel->setStyleSheet("color: #e74c3c; font-size: 12px;");
    m_highLabel->setToolTip(tr("今日最高价（来自全日分时接口，截至当前）"));

    // 预警闪烁点：位于「高」与分时图标之间
    m_alertDot = new QLabel(this);
    m_alertDot->setFixedSize(12, 12);
    m_alertDot->setAlignment(Qt::AlignCenter);
    m_alertDot->setText(QStringLiteral("●"));
    m_alertDot->setStyleSheet("color: transparent; font-size: 14px;");
    m_alertDot->setToolTip(tr("价格预警指示：红=触及高预警，绿=触及低预警"));
    m_alertDot->hide();

    m_alertBlinkTimer = new QTimer(this);
    m_alertBlinkTimer->setInterval(450);
    connect(m_alertBlinkTimer, &QTimer::timeout, this, &PriceBarWindow::onAlertBlinkTick);

    m_secondaryNam = new QNetworkAccessManager(this);
    m_secondaryTimer = new QTimer(this);
    m_secondaryTimer->setInterval(15000);
    connect(m_secondaryTimer, &QTimer::timeout, this, &PriceBarWindow::onSecondaryTimer);

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
    layout->addWidget(m_secondaryLabel);
    layout->addWidget(m_highLabel);
    layout->addWidget(m_alertDot);
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
    m_trayIcon->setToolTip(tr("GoldPriceBarLite %1").arg(QApplication::applicationVersion()));

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

    m_lastPrice = price;
    updateAlertIndicator(price);
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
    if (m_lastPrice > 0.0)
        updateAlertIndicator(m_lastPrice);
    updateSecondaryVisibility();
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

void PriceBarWindow::updateAlertIndicator(double price)
{
    if (!m_alertDot)
        return;

    const double hi = AppSettings::instance().alertHigh();
    const double lo = AppSettings::instance().alertLow();

    AlertKind kind = AlertKind::None;
    if (hi > 0.0 && price >= hi)
        kind = AlertKind::High;
    else if (lo > 0.0 && price <= lo)
        kind = AlertKind::Low;

    const bool kindChanged = (kind != m_alertKind);
    m_alertKind = kind;
    if (kind == AlertKind::None) {
        m_alertBlinkTimer->stop();
        m_alertDot->hide();
        m_alertDot->setStyleSheet("color: transparent; font-size: 14px;");
        m_alertLit = false;
        return;
    }

    m_alertDot->show();
    if (!m_alertBlinkTimer->isActive()) {
        m_alertLit = true;
        onAlertBlinkTick();
        m_alertBlinkTimer->start();
    }
    // 托盘通知（带冷却）
    if (kindChanged || true)
        maybeTrayNotify(kind, price);
}

void PriceBarWindow::maybeTrayNotify(AlertKind kind, double price)
{
    if (!m_trayIcon || !AppSettings::instance().trayNotifyOnAlert())
        return;
    if (kind == AlertKind::None)
        return;

    const int cool = AppSettings::instance().alertCooldownSec();
    const QDateTime now = QDateTime::currentDateTime();
    QDateTime* last = (kind == AlertKind::High) ? &m_lastHighNotify : &m_lastLowNotify;
    if (last->isValid() && last->secsTo(now) < cool)
        return;
    *last = now;

    const QString title = tr("金价预警");
    QString body;
    if (kind == AlertKind::High) {
        body = tr("现价 %1 ≥ 高预警 %2")
                   .arg(price, 0, 'f', 2)
                   .arg(AppSettings::instance().alertHigh(), 0, 'f', 2);
    } else {
        body = tr("现价 %1 ≤ 低预警 %2")
                   .arg(price, 0, 'f', 2)
                   .arg(AppSettings::instance().alertLow(), 0, 'f', 2);
    }
    m_trayIcon->showMessage(title, body, QSystemTrayIcon::Warning, 5000);
}

void PriceBarWindow::updateSecondaryVisibility()
{
    const bool on = AppSettings::instance().showSecondaryPrice();
    if (!m_secondaryLabel)
        return;
    if (on) {
        m_secondaryLabel->show();
        if (m_secondaryTimer && !m_secondaryTimer->isActive()) {
            onSecondaryTimer();
            m_secondaryTimer->start();
        }
        setMinimumWidth(420);
    } else {
        m_secondaryLabel->hide();
        m_secondaryLabel->clear();
        if (m_secondaryTimer)
            m_secondaryTimer->stop();
        setMinimumWidth(360);
    }
}

void PriceBarWindow::onSecondaryTimer()
{
    if (!AppSettings::instance().showSecondaryPrice())
        return;
    if (m_secondaryReply)
        return;
    if (!m_secondaryNam)
        return;

    // 主源已是伦敦金时，对照改拉浙商
    const QString primary = AppSettings::instance().dataSource();
    const QString sec = (primary == QStringLiteral("gj") || primary == QStringLiteral("xau"))
                            ? QStringLiteral("zs")
                            : QStringLiteral("gj");
    const QUrl url(QStringLiteral("https://jin.20021002.xyz/api.php?type=%1").arg(sec));
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("GoldPriceBarLite/0.3.0"));
    req.setTransferTimeout(8000);
    QNetworkReply* reply = m_secondaryNam->get(req);
    m_secondaryReply = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        onSecondaryFinished(reply);
    });
}

void PriceBarWindow::onSecondaryFinished(QNetworkReply* reply)
{
    if (m_secondaryReply.data() == reply)
        m_secondaryReply.clear();
    if (!reply)
        return;
    if (reply->error() != QNetworkReply::NoError) {
        reply->deleteLater();
        return;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
    reply->deleteLater();
    if (!doc.isObject())
        return;
    const QJsonObject data = doc.object().value(QStringLiteral("data")).toObject();
    const double price = data.value(QStringLiteral("price")).toDouble();
    if (price <= 0.0)
        return;
    const QString name = data.value(QStringLiteral("name")).toString();
    const QString shortName = name.contains(QStringLiteral("伦敦")) || name.toLower().contains(QStringLiteral("xau"))
                                  ? tr("伦")
                                  : (name.contains(QStringLiteral("浙商")) ? tr("浙") : tr("对照"));
    if (m_secondaryLabel)
        m_secondaryLabel->setText(tr("%1 %2").arg(shortName).arg(price, 0, 'f', 2));
}

void PriceBarWindow::onAlertBlinkTick()
{
    if (!m_alertDot || m_alertKind == AlertKind::None)
        return;

    m_alertLit = !m_alertLit;
    if (!m_alertLit) {
        m_alertDot->setStyleSheet("color: transparent; font-size: 14px;");
        return;
    }
    if (m_alertKind == AlertKind::High) {
        m_alertDot->setStyleSheet(
            "color: #ff3333; font-size: 14px; font-weight: bold;");
        m_alertDot->setToolTip(tr("高价预警：现价 ≥ %1")
                                   .arg(AppSettings::instance().alertHigh(), 0, 'f', 2));
    } else {
        m_alertDot->setStyleSheet(
            "color: #2ecc71; font-size: 14px; font-weight: bold;");
        m_alertDot->setToolTip(tr("低价预警：现价 ≤ %1")
                                   .arg(AppSettings::instance().alertLow(), 0, 'f', 2));
    }
}

