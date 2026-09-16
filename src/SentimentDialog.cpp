#include "SentimentDialog.h"
#include "SentimentService.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QListWidget>
#include <QLabel>
#include <QPushButton>
#include <QDesktopServices>
#include <QUrl>
#include <QFrame>
#include <QColor>

SentimentDialog::SentimentDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("黄金舆情 · 实时热点"));
    setMinimumSize(480, 520);
    resize(520, 560);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(12, 12, 12, 12);
    root->setSpacing(8);

    auto* head = new QHBoxLayout();
    m_status = new QLabel(tr("点击刷新拉取公开新闻 RSS"), this);
    m_status->setStyleSheet("color:#8b93a7;font-size:12px;");
    m_refreshBtn = new QPushButton(tr("刷新热点"), this);
    m_refreshBtn->setCursor(Qt::PointingHandCursor);
    connect(m_refreshBtn, &QPushButton::clicked, this, &SentimentDialog::refresh);
    head->addWidget(m_status, 1);
    head->addWidget(m_refreshBtn);
    root->addLayout(head);

    m_summary = new QLabel(tr("—"), this);
    m_summary->setWordWrap(true);
    m_summary->setStyleSheet(
        "QLabel{background:#161b27;border:1px solid #2a3347;border-radius:10px;"
        "padding:10px;color:#e8eaed;font-size:13px;}");
    root->addWidget(m_summary);

    auto* hint = new QLabel(
        tr("数据来自公开新闻 RSS（非墨轩阁站点接口）。双击条目可打开原文。偏向为关键词粗分，仅供参考。"),
        this);
    hint->setWordWrap(true);
    hint->setStyleSheet("color:#6b778c;font-size:11px;");
    root->addWidget(hint);

    m_list = new QListWidget(this);
    m_list->setStyleSheet(
        "QListWidget{background:#12161f;border:1px solid #2a3347;border-radius:10px;"
        "color:#c5cddb;font-size:12px;}"
        "QListWidget::item{padding:8px;border-bottom:1px solid #1c2433;}"
        "QListWidget::item:selected{background:#1c2a40;}");
    m_list->setWordWrap(true);
    connect(m_list, &QListWidget::itemDoubleClicked, this, &SentimentDialog::onItemActivated);
    root->addWidget(m_list, 1);

    connect(&SentimentService::instance(), &SentimentService::updated,
            this, &SentimentDialog::onUpdated);
    connect(&SentimentService::instance(), &SentimentService::failed,
            this, &SentimentDialog::onFailed);

    if (!SentimentService::instance().items().isEmpty())
        onUpdated();
}

void SentimentDialog::refresh()
{
    m_refreshBtn->setEnabled(false);
    m_status->setText(tr("正在拉取舆情…"));
    SentimentService::instance().refresh();
}

void SentimentDialog::onUpdated()
{
    m_refreshBtn->setEnabled(true);
    rebuildList();
    const auto& items = SentimentService::instance().items();
    int bull = 0, bear = 0, neu = 0;
    for (const auto& it : items) {
        if (it.bias == QLatin1String("bullish"))
            ++bull;
        else if (it.bias == QLatin1String("bearish"))
            ++bear;
        else
            ++neu;
    }
    const QString tone = (bull > bear + 2)   ? tr("偏多")
                         : (bear > bull + 2) ? tr("偏空")
                                             : tr("多空交织");
    m_summary->setText(
        tr("热点概览：共 %1 条 · 利多倾向 %2 · 利空倾向 %3 · 中性 %4 → 整体 %5\n"
           "更新：%6")
            .arg(items.size())
            .arg(bull)
            .arg(bear)
            .arg(neu)
            .arg(tone)
            .arg(SentimentService::instance().lastSuccess().toString(
                QStringLiteral("yyyy-MM-dd HH:mm:ss"))));
    m_status->setText(tr("已更新 · 双击打开链接"));
}

void SentimentDialog::onFailed(const QString& reason)
{
    m_refreshBtn->setEnabled(true);
    m_status->setText(tr("失败：%1").arg(reason));
}

void SentimentDialog::rebuildList()
{
    m_list->clear();
    for (const auto& it : SentimentService::instance().items()) {
        QString tag = tr("[中]");
        QString colorHint;
        if (it.bias == QLatin1String("bullish")) {
            tag = tr("[多]");
            colorHint = QStringLiteral("#7fd99a");
        } else if (it.bias == QLatin1String("bearish")) {
            tag = tr("[空]");
            colorHint = QStringLiteral("#f07178");
        }
        const QString time = it.published.isValid()
                                 ? it.published.toString(QStringLiteral("MM-dd HH:mm"))
                                 : QStringLiteral("--");
        auto* item = new QListWidgetItem(
            QStringLiteral("%1 %2  %3\n%4 · %5")
                .arg(tag, time, it.title, it.source,
                     it.summary.isEmpty() ? tr("（无摘要）") : it.summary.left(80)));
        item->setData(Qt::UserRole, it.link);
        item->setToolTip(it.link);
        if (!colorHint.isEmpty())
            item->setForeground(QColor(colorHint));
        m_list->addItem(item);
    }
}

void SentimentDialog::onItemActivated()
{
    auto* item = m_list->currentItem();
    if (!item)
        return;
    const QString link = item->data(Qt::UserRole).toString();
    if (!link.isEmpty())
        QDesktopServices::openUrl(QUrl(link));
}
