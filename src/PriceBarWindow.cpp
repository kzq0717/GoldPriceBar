#include "PriceBarWindow.h"
#include "PriceService.h"
#include "SettingsDialog.h"
#include "ChartWindow.h"
#include "AppSettings.h"
#include "HistoryCache.h"
#include "UpdateChecker.h"
#include "GlobalHotkey.h"
#include "ExtremeDatabase.h"
#include "EventCalendar.h"

#include <QHBoxLayout>
#include <QSizePolicy>
#include <QLabel>
#include <QToolButton>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QEvent>
#include <QPainter>
#include <QPainterPath>
#include <QIcon>
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
#include <QtGlobal>
#include <QtMath>
#include <algorithm>

PriceBarWindow::PriceBarWindow(QWidget* parent)
    : QWidget(parent)
{
    setWindowFlags(Qt::FramelessWindowHint
                   | Qt::WindowStaysOnTopHint
                   | Qt::Tool);
    setAttribute(Qt::WA_TranslucentBackground, true);
    setAttribute(Qt::WA_NoSystemBackground, true);
    // 高度随内容自适应，不再锁死；宽度可随对照/盈亏展开
    setMinimumWidth(280);
    setMinimumHeight(36);
    setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Minimum);

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

    if (!m_dcaTimer) {
        m_dcaTimer = new QTimer(this);
        m_dcaTimer->setInterval(60 * 1000);
        connect(m_dcaTimer, &QTimer::timeout, this, &PriceBarWindow::checkDcaReminder);
        m_dcaTimer->start();
        QTimer::singleShot(5000, this, &PriceBarWindow::checkDcaReminder);
    }
    if (!m_dailyReportTimer) {
        m_dailyReportTimer = new QTimer(this);
        m_dailyReportTimer->setInterval(30 * 1000);
        connect(m_dailyReportTimer, &QTimer::timeout, this, [this]() {
            checkDailyReport();
            checkEventAlerts();
            updateNetworkHealth();
        });
        m_dailyReportTimer->start();
    }

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
    layout->setSpacing(6);

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

    m_pnlLabel = new QLabel(this);
    m_pnlLabel->setStyleSheet("color: #f39c12; font-size: 11px;");
    m_pnlLabel->setToolTip(tr("本地持仓浮盈亏（设置中填写克数与成本）"));
    m_pnlLabel->hide();

    m_healthLabel = new QLabel(QStringLiteral("●"), this);
    m_healthLabel->setStyleSheet("color:#2ecc71;font-size:10px;");
    m_healthLabel->setToolTip(tr("网络健康：绿=正常，黄=偶发失败，红=连续失败/退避中"));

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
    // 与主行情同一刷新周期（不再写死 15s）
    {
        int secMs = AppSettings::instance().refreshIntervalMs();
        if (secMs < 1000)
            secMs = 1000;
        m_secondaryTimer->setInterval(secMs);
    }
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
    layout->addWidget(m_pnlLabel);
    layout->addWidget(m_healthLabel);
    layout->addWidget(m_highLabel);
    layout->addWidget(m_alertDot);
    // 不用 stretch，避免警示点与分时按钮之间大块留白；宽度随内容收紧
    layout->addSpacing(4);
    layout->addWidget(m_chartButton);
    layout->addWidget(m_settingsButton);

    applyTheme();
    installDragFilter();
    relayoutBar();
    setupHotkey();
}

void PriceBarWindow::setupTray()
{
    m_trayIcon = new QSystemTrayIcon(this);
    {
        const QIcon appIcon(QStringLiteral(":/app.png"));
        if (!appIcon.isNull()) {
            m_trayIcon->setIcon(appIcon);
            setWindowIcon(appIcon);
        } else {
            m_trayIcon->setIcon(style()->standardIcon(QStyle::SP_ComputerIcon));
        }
    }
    m_trayIcon->setToolTip(tr("GoldPriceBarLite %1  |  Ctrl+Shift+G 显隐")
        .arg(QApplication::applicationVersion()));

    auto* menu = new QMenu(this);
    menu->addAction(tr("显示/隐藏价格条"), this, [this]() {
        setVisible(!isVisible());
    });
    menu->addAction(tr("分时曲线"), this, &PriceBarWindow::onChartClicked);
    menu->addAction(tr("今日摘要"), this, [this]() { showDailyReport(true); });
    menu->addAction(tr("宏观日程"), this, [this]() {
        if (m_trayIcon)
            QMessageBox::information(this, tr("宏观日程"), EventCalendar::summaryNear());
    });
    menu->addAction(tr("标记定投已执行"), this, [this]() {
        const QString today = QDate::currentDate().toString(Qt::ISODate);
        AppSettings::instance().setDcaLastExecutedDate(today);
        AppSettings::instance().save();
        if (m_trayIcon)
            m_trayIcon->showMessage(tr("定投"), tr("已记录今日定投执行"),
                                    QSystemTrayIcon::Information, 3000);
    });
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
    checkPlanAlerts(price);
}

void PriceBarWindow::applyOpacity()
{
    setWindowOpacity(AppSettings::instance().opacity());
}

void PriceBarWindow::onPriceUpdated(double price, double change, const QString& sourceName)
{
    updatePriceDisplay(price, change, sourceName);
    updatePnLDisplay(price);
    evaluateSmartAlerts(price);
    evaluatePremium(price);
    updateNetworkHealth();
    {
        const QString src = AppSettings::instance().dataSource();
        ExtremeDatabase::instance().insertQuoteSample(
            QDateTime::currentDateTime(), src, price, change);
        double ah = 0, al = 0;
        HistoryCache::instance().todayHigh(ah);
        HistoryCache::instance().todayLow(al);
        if (ah > 0 && al > 0)
            ExtremeDatabase::instance().settleForecasts(src, ah, al);
    }
}

void PriceBarWindow::onFetchFailed(const QString& error)
{
    updateNetworkHealth();

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
    const int ms = AppSettings::instance().refreshIntervalMs();
    m_priceService->setInterval(ms);
    // 对照价与主行情使用相同刷新周期
    if (m_secondaryTimer) {
        const int secMs = qMax(1000, ms);
        m_secondaryTimer->setInterval(secMs);
        if (AppSettings::instance().showSecondaryPrice() && m_secondaryTimer->isActive()) {
            // 立即按新周期拉一次，避免仍按旧间隔等待
            onSecondaryTimer();
        }
    }
    // 切换数据源后清空当日缓存，避免浙商与伦敦金价格混在同一曲线
    HistoryCache::instance().clear();
    m_priceService->forceRefresh();
    if (m_lastPrice > 0.0)
        updateAlertIndicator(m_lastPrice);
    updateSecondaryVisibility();
    if (m_lastPrice > 0.0)
        updatePnLDisplay(m_lastPrice);
    setupHotkey();
    applyTheme();
}

void PriceBarWindow::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        m_dragging = true;
        m_dragOffset = event->globalPosition().toPoint() - frameGeometry().topLeft();
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

void PriceBarWindow::mouseMoveEvent(QMouseEvent* event)
{
    if (m_dragging && (event->buttons() & Qt::LeftButton)) {
        QPoint pos = event->globalPosition().toPoint() - m_dragOffset;
        QScreen* screen = QApplication::screenAt(event->globalPosition().toPoint());
        if (!screen)
            screen = QApplication::primaryScreen();
        if (screen) {
            const QRect geo = screen->availableGeometry();
            pos.setX(qBound(geo.left(), pos.x(), geo.right() - width() + 1));
            pos.setY(qBound(geo.top(), pos.y(), geo.bottom() - height() + 1));
        }
        move(pos);
        event->accept();
        return;
    }
    QWidget::mouseMoveEvent(event);
}

void PriceBarWindow::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && m_dragging) {
        m_dragging = false;
        event->accept();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

bool PriceBarWindow::eventFilter(QObject* watched, QEvent* event)
{
    // 子控件（标签/按钮）会抢走鼠标，导致只能点空白处拖、纵向几乎拖不动
    if (event->type() == QEvent::MouseButtonPress
        || event->type() == QEvent::MouseMove
        || event->type() == QEvent::MouseButtonRelease) {
        auto* me = static_cast<QMouseEvent*>(event);
        // 工具按钮左键交给自身点击；其余区域一律用于拖动
        const bool isToolBtn = qobject_cast<QToolButton*>(watched) != nullptr;
        if (isToolBtn && event->type() == QEvent::MouseButtonPress
            && me->button() == Qt::LeftButton) {
            return QWidget::eventFilter(watched, event);
        }
        if (event->type() == QEvent::MouseButtonPress && me->button() == Qt::LeftButton) {
            m_dragging = true;
            m_dragOffset = me->globalPosition().toPoint() - frameGeometry().topLeft();
            return true;
        }
        if (event->type() == QEvent::MouseMove && m_dragging
            && (me->buttons() & Qt::LeftButton)) {
            QPoint pos = me->globalPosition().toPoint() - m_dragOffset;
            QScreen* screen = QApplication::screenAt(me->globalPosition().toPoint());
            if (!screen)
                screen = QApplication::primaryScreen();
            if (screen) {
                const QRect geo = screen->availableGeometry();
                pos.setX(qBound(geo.left(), pos.x(), geo.right() - width() + 1));
                pos.setY(qBound(geo.top(), pos.y(), geo.bottom() - height() + 1));
            }
            move(pos);
            return true;
        }
        if (event->type() == QEvent::MouseButtonRelease && me->button() == Qt::LeftButton) {
            if (m_dragging) {
                m_dragging = false;
                return true;
            }
        }
    }
    return QWidget::eventFilter(watched, event);
}

void PriceBarWindow::installDragFilter()
{
    const auto kids = findChildren<QWidget*>();
    for (QWidget* w : kids) {
        if (w == this)
            continue;
        w->installEventFilter(this);
        // 标签不抢焦点，便于拖动
        if (qobject_cast<QLabel*>(w))
            w->setAttribute(Qt::WA_TransparentForMouseEvents, false);
    }
}

void PriceBarWindow::relayoutBar()
{
    if (!layout())
        return;
    layout()->activate();
    layout()->invalidate();
    const QSize sh = layout()->sizeHint().expandedTo(layout()->minimumSize());
    // 紧贴内容，不人为加宽
    const int h = qMax(36, sh.height());
    const int w = qMax(280, sh.width());
    setMinimumSize(w, h);
    setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
    resize(w, h);
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
    ExtremeDatabase::instance().insertAlertEvent(
        QDateTime::currentDateTime(),
        AppSettings::instance().dataSource(),
        kind == AlertKind::High ? QStringLiteral("high") : QStringLiteral("low"),
        price,
        kind == AlertKind::High ? AppSettings::instance().alertHigh()
                                : AppSettings::instance().alertLow(),
        body);
}




void PriceBarWindow::checkPlanAlerts(double price)
{
    if (price <= 0.0 || !AppSettings::instance().planEnabled())
        return;
    if (AppSettings::instance().isInQuietHours())
        return;
    if (!m_trayIcon || !AppSettings::instance().trayNotifyOnAlert())
        return;

    const auto& s = AppSettings::instance();
    QString msg;
    if (s.planInvalidPrice() > 0.0 && price <= s.planInvalidPrice())
        msg = tr("现价 %1 触及计划失效价 %2，计划作废")
                  .arg(price, 0, 'f', 2)
                  .arg(s.planInvalidPrice(), 0, 'f', 2);
    else if (s.planBuyPrice() > 0.0 && price <= s.planBuyPrice())
        msg = tr("现价 %1 进入买入观察区（≤ %2）")
                  .arg(price, 0, 'f', 2)
                  .arg(s.planBuyPrice(), 0, 'f', 2);
    else if (s.planSellPrice() > 0.0 && price >= s.planSellPrice())
        msg = tr("现价 %1 进入卖出观察区（≥ %2）")
                  .arg(price, 0, 'f', 2)
                  .arg(s.planSellPrice(), 0, 'f', 2);
    if (msg.isEmpty())
        return;

    const QDateTime now = QDateTime::currentDateTime();
    const int cool = qMax(60, AppSettings::instance().alertCooldownSec());
    if (m_lastPlanNotify.isValid() && m_lastPlanNotify.secsTo(now) < cool)
        return;
    m_lastPlanNotify = now;
    m_trayIcon->showMessage(tr("交易计划"), msg, QSystemTrayIcon::Information, 6000);
    ExtremeDatabase::instance().insertAlertEvent(
        now, AppSettings::instance().dataSource(), QStringLiteral("plan"),
        price, 0.0, msg);
}


void PriceBarWindow::updateSecondaryVisibility()
{
    const bool on = AppSettings::instance().showSecondaryPrice();
    if (!m_secondaryLabel)
        return;
    if (on) {
        m_secondaryLabel->show();
        if (m_secondaryTimer) {
            int secMs = AppSettings::instance().refreshIntervalMs();
            if (secMs < 1000)
                secMs = 1000;
            m_secondaryTimer->setInterval(secMs);
            if (!m_secondaryTimer->isActive()) {
                onSecondaryTimer();
                m_secondaryTimer->start();
            }
        }
        setMinimumWidth(280);
        relayoutBar();
    } else {
        m_secondaryLabel->hide();
        m_secondaryLabel->clear();
        if (m_secondaryTimer)
            m_secondaryTimer->stop();
        setMinimumWidth(280);
        relayoutBar();
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
    m_lastSecondaryPrice = price;
    if (m_lastPrice > 0.0) {
        evaluatePremium(m_lastPrice);
        ExtremeDatabase::instance().insertSecondaryQuote(
            QDateTime::currentDateTime(),
            AppSettings::instance().dataSource(), m_lastPrice,
            QStringLiteral("gj"), price);
    }
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

void PriceBarWindow::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    const bool dark = AppSettings::instance().darkTheme();
    QPainterPath path;
    const qreal r = 14.0;
    const QRectF rr = QRectF(rect()).adjusted(1.0, 1.0, -1.0, -1.0);
    path.addRoundedRect(rr, r, r);
    if (dark) {
        // Dashboard 卡片：深色底 + 细描边 + 轻微内高光
        QLinearGradient g(0, 0, 0, height());
        g.setColorAt(0.0, QColor(26, 32, 48, 250));
        g.setColorAt(1.0, QColor(18, 22, 34, 250));
        p.fillPath(path, g);
        p.setPen(QPen(QColor(55, 65, 90, 200), 1.0));
        p.drawPath(path);
        // 顶部细高光
        QPainterPath hi;
        hi.addRoundedRect(rr.adjusted(1, 1, -1, -rr.height() * 0.55), r - 1, r - 1);
        p.fillPath(hi, QColor(255, 255, 255, 8));
    } else {
        QLinearGradient g(0, 0, 0, height());
        g.setColorAt(0.0, QColor(255, 255, 255, 250));
        g.setColorAt(1.0, QColor(243, 246, 251, 250));
        p.fillPath(path, g);
        p.setPen(QPen(QColor(210, 218, 232), 1.0));
        p.drawPath(path);
    }
}

void PriceBarWindow::applyTheme()
{
    const bool dark = AppSettings::instance().darkTheme();
    if (dark) {
        setStyleSheet(
            "PriceBarWindow { background: transparent; }"
            "QToolButton {"
            "  color: #c5cbe0;"
            "  border: none;"
            "  font-size: 13px;"
            "  padding: 4px 6px;"
            "  border-radius: 8px;"
            "}"
            "QToolButton:hover {"
            "  background-color: rgba(91,141,239,40);"
            "  color: #ffffff;"
            "}"
            "QLabel { background: transparent; }"
        );
        if (m_sourceLabel)
            m_sourceLabel->setStyleSheet("color:#8b93a7;font-size:11px;letter-spacing:0.3px;");
        if (m_priceLabel)
            m_priceLabel->setStyleSheet(
                "color:#f2f4f8;font-size:17px;font-weight:700;font-family:'Segoe UI','Microsoft YaHei UI';");
        if (m_highLabel)
            m_highLabel->setStyleSheet("color:#ff7b72;font-size:12px;font-weight:600;");
        if (m_secondaryLabel)
            m_secondaryLabel->setStyleSheet("color:#a78bfa;font-size:11px;");
        if (m_pnlLabel)
            m_pnlLabel->setStyleSheet("color:#f0b429;font-size:11px;font-weight:600;");
        if (m_healthLabel)
            m_healthLabel->setStyleSheet("color:#3dd68c;font-size:10px;");
        if (m_changeLabel) {
            const QString c = m_changeLabel->styleSheet();
            Q_UNUSED(c);
        }
    } else {
        setStyleSheet(
            "PriceBarWindow { background: transparent; }"
            "QToolButton {"
            "  color: #3d4a5c;"
            "  border: none;"
            "  font-size: 13px;"
            "  padding: 4px 6px;"
            "  border-radius: 8px;"
            "}"
            "QToolButton:hover {"
            "  background-color: rgba(0,82,217,20);"
            "  color: #0052d9;"
            "}"
            "QLabel { background: transparent; }"
        );
        if (m_sourceLabel)
            m_sourceLabel->setStyleSheet("color:#6b7c8f;font-size:11px;");
        if (m_priceLabel)
            m_priceLabel->setStyleSheet(
                "color:#0f172a;font-size:17px;font-weight:700;font-family:'Segoe UI','Microsoft YaHei UI';");
        if (m_highLabel)
            m_highLabel->setStyleSheet("color:#e11d48;font-size:12px;font-weight:600;");
        if (m_secondaryLabel)
            m_secondaryLabel->setStyleSheet("color:#7c3aed;font-size:11px;");
        if (m_pnlLabel)
            m_pnlLabel->setStyleSheet("color:#d97706;font-size:11px;font-weight:600;");
        if (m_healthLabel)
            m_healthLabel->setStyleSheet("color:#059669;font-size:10px;");
    }
    update();
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
    QString body = note.isEmpty()
        ? tr("今天是定投日（每月 %1 日），记得买入积存金。").arg(day)
        : tr("今天是定投日（每月 %1 日）\n%2").arg(day).arg(note);

    if (AppSettings::instance().dcaLastExecutedDate() == iso)
        body += tr("\n（今日已标记执行）");
    m_trayIcon->showMessage(tr("定投提醒"), body, QSystemTrayIcon::Information, 8000);
    if (AppSettings::instance().alertSound())
        QApplication::beep();

    AppSettings::instance().setDcaLastNotifiedDate(iso);
    AppSettings::instance().save();
}



void PriceBarWindow::updatePnLDisplay(double price)
{
    if (!m_pnlLabel)
        return;
    const double grams = AppSettings::instance().positionGrams();
    const double cost = AppSettings::instance().positionCost();
    if (grams <= 0.0 || cost <= 0.0 || price <= 0.0) {
        m_pnlLabel->hide();
        m_pnlLabel->clear();
        return;
    }
    const double pnl = (price - cost) * grams;
    const double pct = (price - cost) / cost * 100.0;
    m_pnlLabel->show();
    const QString color = pnl >= 0 ? QStringLiteral("#2ecc71") : QStringLiteral("#e74c3c");
    m_pnlLabel->setStyleSheet(QStringLiteral("color:%1;font-size:11px;font-weight:bold;").arg(color));
    m_pnlLabel->setText(tr("盈 %1 (%2%)")
                            .arg(pnl, 0, 'f', 1)
                            .arg(pct, 0, 'f', 2)
                            .replace(QStringLiteral("盈 -"), QStringLiteral("亏 ")));
    if (pnl < 0)
        m_pnlLabel->setText(tr("亏 %1 (%2%)").arg(-pnl, 0, 'f', 1).arg(pct, 0, 'f', 2));
    else
        m_pnlLabel->setText(tr("盈 %1 (+%2%)").arg(pnl, 0, 'f', 1).arg(pct, 0, 'f', 2));
    relayoutBar();
}

double PriceBarWindow::computeMa5() const
{
    auto closes = ExtremeDatabase::instance().loadRecentDailyCloses(10, AppSettings::instance().dataSource() == QStringLiteral("ms") ? QStringLiteral("ms") : (AppSettings::instance().dataSource() == QStringLiteral("gj") ? QStringLiteral("gj") : QStringLiteral("zs")));
    if (closes.size() < 5)
        closes = ExtremeDatabase::instance().loadRecentDailyCloses(10, QStringLiteral("gj"));
    if (closes.size() < 5)
        return 0.0;
    double s = 0.0;
    for (int i = closes.size() - 5; i < closes.size(); ++i)
        s += closes.at(i).second;
    return s / 5.0;
}

double PriceBarWindow::computePercentile(double price) const
{
    QString src = AppSettings::instance().dataSource();
    if (src == QStringLiteral("xau")) src = QStringLiteral("gj");
    auto closes = ExtremeDatabase::instance().loadRecentDailyCloses(20, src);
    if (closes.size() < 5)
        closes = ExtremeDatabase::instance().loadRecentDailyCloses(20, QStringLiteral("gj"));
    if (closes.isEmpty() || price <= 0.0)
        return 50.0;
    int below = 0;
    for (const auto& c : closes) {
        if (c.second < price)
            ++below;
    }
    return 100.0 * static_cast<double>(below) / static_cast<double>(closes.size());
}

void PriceBarWindow::evaluateSmartAlerts(double price)
{
    if (price <= 0.0 || AppSettings::instance().isInQuietHours())
        return;

    QStringList reasons;
    if (AppSettings::instance().smartAlertMa()) {
        const double ma5 = computeMa5();
        if (ma5 > 0.0) {
            // 跌破 MA5 超过 0.15% 视为偏弱；站上超过 0.15% 偏强（用 High 样式提示上破）
            if (price < ma5 * 0.9985)
                reasons << tr("跌破MA5日(%1)").arg(ma5, 0, 'f', 2);
            else if (price > ma5 * 1.0015)
                reasons << tr("站上MA5日(%1)").arg(ma5, 0, 'f', 2);
        }
    }
    if (AppSettings::instance().smartAlertPercentile()) {
        const double pct = computePercentile(price);
        const int lo = AppSettings::instance().percentileLow();
        const int hi = AppSettings::instance().percentileHigh();
        if (pct <= lo)
            reasons << tr("近20日分位偏低(%1%)").arg(pct, 0, 'f', 0);
        else if (pct >= hi)
            reasons << tr("近20日分位偏高(%1%)").arg(pct, 0, 'f', 0);
    }
    if (reasons.isEmpty())
        return;

    // 冷却：与预警共用 cooldown
    const int cool = AppSettings::instance().alertCooldownSec();
    const QDateTime now = QDateTime::currentDateTime();
    if (m_lastSmartNotify.isValid() && m_lastSmartNotify.secsTo(now) < cool)
        return;
    m_lastSmartNotify = now;

    // 闪点：分位高/站上均线偏红，其余偏绿
    const bool bullish = reasons.join(QString()).contains(QStringLiteral("站上"))
                         || reasons.join(QString()).contains(QStringLiteral("偏高"));
    m_alertKind = bullish ? AlertKind::High : AlertKind::Low;
    if (m_alertDot) {
        m_alertDot->show();
        if (!m_alertBlinkTimer->isActive())
            m_alertBlinkTimer->start();
    }
    if (m_trayIcon && AppSettings::instance().trayNotifyOnAlert()) {
        const QString shortMsg = reasons.join(QStringLiteral(" · "));
        m_trayIcon->showMessage(tr("智能预警"), shortMsg.left(80),
                                bullish ? QSystemTrayIcon::Warning : QSystemTrayIcon::Information,
                                3500);
    }
    ExtremeDatabase::instance().insertAlertEvent(
        QDateTime::currentDateTime(),
        AppSettings::instance().dataSource(),
        bullish ? QStringLiteral("smart_high") : QStringLiteral("smart_low"),
        price, 0.0, reasons.join(QStringLiteral(";")));
    if (AppSettings::instance().alertSound())
        QApplication::beep();
}

void PriceBarWindow::evaluatePremium(double primaryPrice)
{
    if (!AppSettings::instance().premiumAlertEnabled())
        return;
    if (!AppSettings::instance().showSecondaryPrice())
        return;
    if (primaryPrice <= 0.0 || m_lastSecondaryPrice <= 0.0)
        return;
    if (AppSettings::instance().isInQuietHours())
        return;

    const double ratio = primaryPrice / m_lastSecondaryPrice;
    m_premiumRatios.append(ratio);
    while (m_premiumRatios.size() > 40)
        m_premiumRatios.removeFirst();
    if (m_premiumRatios.size() < 8)
        return;

    double mean = 0.0;
    for (double r : m_premiumRatios)
        mean += r;
    mean /= m_premiumRatios.size();
    if (mean <= 0.0)
        return;
    const double devPct = qAbs(ratio - mean) / mean * 100.0;
    const double thr = AppSettings::instance().premiumThresholdPct();
    if (devPct < thr)
        return;

    const QDateTime now = QDateTime::currentDateTime();
    const int cool = AppSettings::instance().alertCooldownSec();
    if (m_lastPremiumNotify.isValid() && m_lastPremiumNotify.secsTo(now) < cool)
        return;
    m_lastPremiumNotify = now;

    if (m_trayIcon && AppSettings::instance().trayNotifyOnAlert()) {
        m_trayIcon->showMessage(
            tr("溢价监测"),
            tr("比值偏离 %1%（阈 %2%）").arg(devPct, 0, 'f', 1).arg(thr, 0, 'f', 1),
            QSystemTrayIcon::Warning, 3500);
    }
}

QString PriceBarWindow::buildDailyReportText() const
{
    double high = 0.0, low = 0.0;
    HistoryCache::instance().todayHigh(high);
    HistoryCache::instance().todayLow(low);
    const double price = m_lastPrice;
    QString lines;
    lines += tr("【GoldPriceBar 今日摘要】\n");
    lines += tr("现价：%1\n").arg(price > 0 ? QString::number(price, 'f', 2) : QStringLiteral("--"));
    lines += tr("今高：%1  今低：%2\n")
                 .arg(high > 0 ? QString::number(high, 'f', 2) : QStringLiteral("--"))
                 .arg(low > 0 ? QString::number(low, 'f', 2) : QStringLiteral("--"));
    if (high > 0 && low > 0)
        lines += tr("振幅：%1 (%2%)\n")
                     .arg(high - low, 0, 'f', 2)
                     .arg(low > 0 ? (high - low) / low * 100.0 : 0.0, 0, 'f', 2);
    const double grams = AppSettings::instance().positionGrams();
    const double cost = AppSettings::instance().positionCost();
    if (grams > 0 && cost > 0 && price > 0) {
        const double pnl = (price - cost) * grams;
        lines += tr("持仓浮盈亏：%1 元（%2 克）\n")
                     .arg(pnl, 0, 'f', 1)
                     .arg(grams, 0, 'f', 3);
    }
    if (m_lastSecondaryPrice > 0)
        lines += tr("对照价：%1\n").arg(m_lastSecondaryPrice, 0, 'f', 2);
    lines += tr("时间：%1").arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm")));
    return lines;
}

void PriceBarWindow::showDailyReport(bool force)
{
    // 托盘气泡易被截断/遮挡，强制时用简洁对话框
    const QString text = buildDailyReportText();
    if (force) {
        QMessageBox::information(this, tr("今日摘要"), text);
        return;
    }
    if (m_trayIcon) {
        // 短提示，详情用托盘菜单「今日摘要」
        double high = 0, low = 0;
        HistoryCache::instance().todayHigh(high);
        HistoryCache::instance().todayLow(low);
        const QString brief = tr("现 %1  高 %2  低 %3")
                                  .arg(m_lastPrice > 0 ? QString::number(m_lastPrice, 'f', 2) : QStringLiteral("--"))
                                  .arg(high > 0 ? QString::number(high, 'f', 2) : QStringLiteral("--"))
                                  .arg(low > 0 ? QString::number(low, 'f', 2) : QStringLiteral("--"));
        m_trayIcon->showMessage(tr("今日摘要"), brief, QSystemTrayIcon::Information, 4000);
    }
}

void PriceBarWindow::checkDailyReport()
{
    if (!AppSettings::instance().dailyReportEnabled())
        return;
    if (AppSettings::instance().isInQuietHours())
        return;
    const QTime target = AppSettings::instance().dailyReportTime();
    const QTime now = QTime::currentTime();
    if (now.hour() != target.hour() || now.minute() != target.minute())
        return;
    const QString today = QDate::currentDate().toString(Qt::ISODate);
    if (AppSettings::instance().dailyReportLastDate() == today)
        return;
    AppSettings::instance().setDailyReportLastDate(today);
    AppSettings::instance().save();
    showDailyReport(false);
}


void PriceBarWindow::updateNetworkHealth()
{
    if (!m_healthLabel || !m_priceService)
        return;
    const int fails = m_priceService->consecutiveFail();
    const qint64 lastOk = m_priceService->lastSuccessMs();
    const qint64 age = lastOk > 0 ? (QDateTime::currentMSecsSinceEpoch() - lastOk) : -1;
    const int cfg = m_priceService->configuredIntervalMs();
    const int cur = m_priceService->currentIntervalMs();
    QString tip = tr("配置刷新 %1 ms\n实际间隔 %2 ms\n连续失败 %3")
                      .arg(cfg).arg(cur).arg(fails);
    if (age >= 0)
        tip += tr("\n距上次成功 %1 秒").arg(age / 1000);
    if (EventCalendar::isHighImpactDay())
        tip += tr("\n今日高波动日程：%1").arg(EventCalendar::pendingAlertText());
    m_healthLabel->setToolTip(tip);

    if (fails >= 5 || (age > 0 && age > qMax(30000LL, static_cast<qint64>(cfg) * 8))) {
        m_healthLabel->setStyleSheet("color:#e74c3c;font-size:10px;");
    } else if (fails >= 1 || cur > cfg) {
        m_healthLabel->setStyleSheet("color:#f1c40f;font-size:10px;");
    } else {
        m_healthLabel->setStyleSheet("color:#2ecc71;font-size:10px;");
    }
}

void PriceBarWindow::checkEventAlerts()
{
    if (!AppSettings::instance().eventAlertEnabled())
        return;
    if (AppSettings::instance().isInQuietHours())
        return;
    const QString text = EventCalendar::pendingAlertText();
    if (text.isEmpty())
        return;
    const QString key = QDate::currentDate().toString(Qt::ISODate) + QLatin1Char('|') + text;
    if (AppSettings::instance().eventAlertLastKey() == key)
        return;
    AppSettings::instance().setEventAlertLastKey(key);
    AppSettings::instance().save();
    if (m_trayIcon) {
        m_trayIcon->showMessage(tr("宏观日程"), text.left(60),
                                QSystemTrayIcon::Warning, 4000);
    }
}
