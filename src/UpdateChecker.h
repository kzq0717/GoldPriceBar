#ifndef UPDATECHECKER_H
#define UPDATECHECKER_H

#include <QObject>
#include <QPointer>

class QNetworkAccessManager;
class QNetworkReply;
class QWidget;

/**
 * @brief 从 GitHub Releases 检查是否有新版本
 * API: https://api.github.com/repos/kzq0717/GoldPriceBar/releases/latest
 */
class UpdateChecker : public QObject
{
    Q_OBJECT

public:
    explicit UpdateChecker(QObject* parent = nullptr);

    /** 静默=仅有更新时提示；否则无论结果都提示 */
    void check(QWidget* parentDialog, bool silent = false);

    static int compareVersion(const QString& a, const QString& b);

private slots:
    void onFinished(QNetworkReply* reply);

private:
    QNetworkAccessManager* m_nam = nullptr;
    QPointer<QWidget> m_parent;
    bool m_silent = false;
    bool m_busy = false;
};

#endif // UPDATECHECKER_H
