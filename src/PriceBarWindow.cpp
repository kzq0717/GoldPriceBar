#include "PriceBarWindow.h"
#include "PriceService.h"
#include "SettingsDialog.h"
#include "ChartWindow.h"
#include "AppSettings.h"
#include "HistoryCache.h"
#include "UpdateChecker.h"
#include "GlobalHotkey.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QToolButton>
#include <QMouseEvent>
#include <QCloseEvent>
#include <QMenu>
#include <QApplication>
#include <QStyle>
#include <QScreen>
#include <QMessageBox>
#include <QTimer>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QUrl>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDesktopServices>
#include <QDateTime>
#include <QDate>

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

    applyTheme();
    setupHotkey();
}

void PriceBarWindow::setupTray()
{
    m_trayIcon = new QSystemTrayIcon(this);
    m_trayIcon->setIcon(style()->standardIcon(QStyle::SP_ComputerIcon));
    m_trayIcon->setToolTip(tr("GoldPriceBarLite %1  |  Ctrl+Shift+G 显隐")
        .arg(QApplication::applicationVersion()));

    auto* menu = new QMenu(this);
    menu->addAction(tr("显示/隐藏价格条"), this, [this]() {
        setVisible(!isVisible());
    });
    menu->addAction(tr("分时曲线"), this, &PriceBarWindow::onChartClicked);
    menu->addAction(tr("关于"), this, &PriceBarWindow::showAbout);
    menu->addAction(tr("检查更新"), this, [this]() {
        auto* c = new UpdateChecker(this);
        c->check(this, false);
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
    setupHotkey();

    m_dcaTimer = new QTimer(this);
    m_dcaTimer->setInterval(60 * 1000); // 每分钟检查定投日
    connect(m_dcaTimer, &QTimer::timeout, this, &PriceBarWindow::checkDcaReminder);
    m_dcaTimer->start();
    QTimer::singleShot(5000, this, &PriceBarWindow::checkDcaReminder);
    applyTheme();
    setupHotkey();
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
    if (!AppSettings::instance().isInQuietHours()) {
        if (hi > 0.0 && price >= hi)
            kind = AlertKind::High;
        else if (lo > 0.0 && price <= lo)
            kind = AlertKind::Low;
    }

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
    if (AppSettings::instance().isInQuietHours())
        return; // 免打扰

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
    if (AppSettings::instance().alertSound())
        QApplication::beep();
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

void PriceBarWindow::applyTheme()
{
    const bool dark = AppSettings::instance().darkTheme();
    if (dark) {
        setStyleSheet(
            "PriceBarWindow {"
            "  background-color: qlineargradient(x1:0,y1:0,x2:1,y2:0,"
            "    stop:0 #1a1d23, stop:1 #252a33);"
            "  border: 1px solid #3d4450;"
            "  border-radius: 8px;"
            "}"
            "QToolButton {"
            "  color: #e8eaed;"
            "  border: none;"
            "  font-size: 14px;"
            "  padding: 2px;"
            "}"
            "QToolButton:hover {"
            "  background-color: rgba(255,255,255,28);"
            "  border-radius: 5px;"
            "}"
        );
        if (m_sourceLabel)
            m_sourceLabel->setStyleSheet("color:#9aa0a6;font-size:12px;");
        if (m_priceLabel)
            m_priceLabel->setStyleSheet("color:#ffffff;font-size:16px;font-weight:bold;");
        if (m_highLabel)
            m_highLabel->setStyleSheet("color:#ff6b6b;font-size:12px;");
        if (m_secondaryLabel)
            m_secondaryLabel->setStyleSheet("color:#c792ea;font-size:11px;");
    } else {
        setStyleSheet(
            "PriceBarWindow {"
            "  background-color: qlineargradient(x1:0,y1:0,x2:1,y2:0,"
            "    stop:0 #f7f8fa, stop:1 #eef1f6);"
            "  border: 1px solid #c5cdd8;"
            "  border-radius: 8px;"
            "}"
            "QToolButton {"
            "  color: #333;"
            "  border: none;"
            "  font-size: 14px;"
            "}"
            "QToolButton:hover {"
            "  background-color: rgba(0,0,0,18);"
            "  border-radius: 5px;"
            "}"
        );
        if (m_sourceLabel)
            m_sourceLabel->setStyleSheet("color:#5c6b77;font-size:12px;");
        if (m_priceLabel)
            m_priceLabel->setStyleSheet("color:#1a1d23;font-size:16px;font-weight:bold;");
        if (m_highLabel)
            m_highLabel->setStyleSheet("color:#c0392b;font-size:12px;");
        if (m_secondaryLabel)
            m_secondaryLabel->setStyleSheet("color:#8e44ad;font-size:11px;");
    }
    applyOpacity();
}

void PriceBarWindow::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton)
        onChartClicked();
    QWidget::mouseDoubleClickEvent(event);
}

void PriceBarWindow::showAbout()
{
    QMessageBox::about(
        this,
        tr("关于 GoldPriceBarLite"),
        tr("<b>GoldPriceBarLite %1</b><br/>"
           "轻量级积存金/金价浮窗<br/><br/>"
           "数据来源：jin.20021002.xyz 公开接口<br/>"
           "预测仅供参考，不构成投资建议。<br/><br/>"
           "仓库：github.com/kzq0717/GoldPriceBar")
            .arg(QApplication::applicationVersion()));
}

void PriceBarWindow::toggleVisible()
{
    setVisible(!isVisible());
    if (isVisible()) {
        raise();
        activateWindow();
    }
}

void PriceBarWindow::setupHotkey()
{
    if (!m_hotkey) {
        m_hotkey = new GlobalHotkey(this);
        connect(m_hotkey, &GlobalHotkey::activated, this, &PriceBarWindow::toggleVisible);
    }
    if (AppSettings::instance().hotkeyEnabled()) {
        if (!m_hotkey->registerHotkey()) {
            if (m_trayIcon)
                m_trayIcon->showMessage(
                    tr("热键"),
                    tr("Ctrl+Shift+G 注册失败（可能被占用）"),
                    QSystemTrayIcon::Warning, 3000);
        }
    } else {
        m_hotkey->unregisterHotkey();
    }
}

void PriceBarWindow::checkDcaReminder()
{
    const int day = AppSettings::instance().dcaDayOfMonth();
    if (day <= 0 || !m_trayIcon)
        return;
    if (AppSettings::instance().isInQuietHours())
        return;

    const QDate today = QDate::currentDate();
    if (today.day() != day)
        return;

    const QString iso = today.toString(Qt::ISODate);
    if (AppSettings::instance().dcaLastNotifiedDate() == iso)
        return;

    const QString note = AppSettings::instance().dcaNote().trimmed();
    const QString body = note.isEmpty()
        ? tr("今天是定投日（每月 %1 日），记得买入积存金。").arg(day)
        : tr("今天是定投日（每月 %1 日）\n%2").arg(day).arg(note);

    m_trayIcon->showMessage(tr("定投提醒"), body, QSystemTrayIcon::Information, 8000);
    if (AppSettings::instance().alertSound())
        QApplication::beep();

    AppSettings::instance().setDcaLastNotifiedDate(iso);
    AppSettings::instance().save();
}

