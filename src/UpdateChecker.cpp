#include "UpdateChecker.h"
#include "Logger.h"

#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QApplication>
#include <QMessageBox>
#include <QDesktopServices>
#include <QUrl>
#include <QRegularExpression>

UpdateChecker::UpdateChecker(QObject* parent)
    : QObject(parent)
{
    m_nam = new QNetworkAccessManager(this);
}

int UpdateChecker::compareVersion(const QString& a, const QString& b)
{
    auto parts = [](QString v) {
        v = v.trimmed();
        if (v.startsWith(QLatin1Char('v')) || v.startsWith(QLatin1Char('V')))
            v = v.mid(1);
        const QStringList segs = v.split(QLatin1Char('.'));
        QList<int> out;
        for (const QString& s : segs) {
            int n = 0;
            for (const QChar c : s) {
                if (!c.isDigit())
                    break;
                n = n * 10 + c.digitValue();
            }
            out.append(n);
        }
        while (out.size() < 3)
            out.append(0);
        return out;
    };
    const auto pa = parts(a);
    const auto pb = parts(b);
    const int n = qMax(pa.size(), pb.size());
    for (int i = 0; i < n; ++i) {
        const int xa = i < pa.size() ? pa[i] : 0;
        const int xb = i < pb.size() ? pb[i] : 0;
        if (xa < xb) return -1;
        if (xa > xb) return 1;
    }
    return 0;
}

void UpdateChecker::check(QWidget* parentDialog, bool silent)
{
    if (m_busy)
        return;
    m_busy = true;
    m_parent = parentDialog;
    m_silent = silent;

    const QUrl url(QStringLiteral(
        "https://api.github.com/repos/kzq0717/GoldPriceBar/releases/latest"));
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader,
                  QStringLiteral("GoldPriceBarLite/%1")
                      .arg(QApplication::applicationVersion()));
    req.setRawHeader("Accept", "application/vnd.github+json");
    req.setTransferTimeout(12000);

    QNetworkReply* reply = m_nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        onFinished(reply);
    });
    Logger::info(QStringLiteral("Update check started"));
}

void UpdateChecker::onFinished(QNetworkReply* reply)
{
    m_busy = false;
    if (!reply)
        return;

    auto showInfo = [this](const QString& title, const QString& text) {
        if (m_silent)
            return;
        QMessageBox::information(m_parent.data(), title, text);
    };

    if (reply->error() != QNetworkReply::NoError) {
        Logger::warn(QStringLiteral("Update check failed: %1").arg(reply->errorString()));
        showInfo(tr("检查更新"),
                 tr("无法连接 GitHub：%1\n请稍后重试或手动访问仓库 Releases。")
                     .arg(reply->errorString()));
        reply->deleteLater();
        return;
    }

    const QByteArray body = reply->readAll();
    reply->deleteLater();

    const QJsonDocument doc = QJsonDocument::fromJson(body);
    if (!doc.isObject()) {
        showInfo(tr("检查更新"), tr("返回数据无效。"));
        return;
    }

    const QJsonObject obj = doc.object();
    QString tag = obj.value(QStringLiteral("tag_name")).toString();
    const QString htmlUrl = obj.value(QStringLiteral("html_url")).toString();
    const QString name = obj.value(QStringLiteral("name")).toString();
    const bool draft = obj.value(QStringLiteral("draft")).toBool();
    const bool prerelease = obj.value(QStringLiteral("prerelease")).toBool();

    if (tag.isEmpty() || draft) {
        showInfo(tr("检查更新"), tr("暂无正式发布版本。"));
        return;
    }

    const QString current = QApplication::applicationVersion();
    const int cmp = compareVersion(current, tag);

    Logger::info(QStringLiteral("Update check: current=%1 latest=%2 cmp=%3")
                     .arg(current, tag).arg(cmp));

    if (cmp >= 0) {
        showInfo(tr("检查更新"),
                 tr("当前已是最新版本。\n当前：%1\n线上：%2%3")
                     .arg(current, tag, prerelease ? tr("（预发布）") : QString()));
        return;
    }

    // 有新版本
    QMessageBox box(m_parent.data());
    box.setWindowTitle(tr("发现新版本"));
    box.setIcon(QMessageBox::Information);
    box.setText(tr("发现新版本 <b>%1</b>（当前 %2）")
                    .arg(tag, current));
    box.setInformativeText(
        tr("%1\n\n是否打开下载页面？")
            .arg(name.isEmpty() ? tag : name));
    box.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
    box.setDefaultButton(QMessageBox::Yes);
    if (box.exec() == QMessageBox::Yes) {
        const QUrl openUrl = htmlUrl.isEmpty()
            ? QUrl(QStringLiteral("https://github.com/kzq0717/GoldPriceBar/releases"))
            : QUrl(htmlUrl);
        QDesktopServices::openUrl(openUrl);
    }
}
