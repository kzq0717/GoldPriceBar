#ifndef SENTIMENTDIALOG_H
#define SENTIMENTDIALOG_H

#include <QDialog>

class QListWidget;
class QLabel;
class QPushButton;

/** 黄金舆情 · 实时热点追踪窗口 */
class SentimentDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SentimentDialog(QWidget* parent = nullptr);

public slots:
    void refresh();

private slots:
    void onUpdated();
    void onFailed(const QString& reason);
    void onItemActivated();

private:
    void rebuildList();

    QListWidget* m_list = nullptr;
    QLabel* m_status = nullptr;
    QLabel* m_summary = nullptr;
    QPushButton* m_refreshBtn = nullptr;
};

#endif
