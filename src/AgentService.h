#ifndef AGENTSERVICE_H
#define AGENTSERVICE_H

#include <QObject>
#include <QString>
#include <QVariantMap>
#include <QVariantList>
#include <QDateTime>
#include <QVector>
#include <QTimer>
#include <QRecursiveMutex>

struct AgentTask {
    QString id;
    QString name;
    QString type;           // "curriculum", "reminder", "command", "gold_monitor"
    QString scheduleRule;   // "10:00", "daily@10:00", "interval:60" (分钟)
    bool enabled = true;
    QVariantMap content;
    QDateTime lastRun;
    QDateTime nextRun;
    QDateTime createdAt;

    QVariantMap toMap() const;
    static AgentTask fromMap(const QVariantMap &map);
};

class AgentService : public QObject {
    Q_OBJECT

public:
    static AgentService& instance();

    bool init();
    QString databasePath() const { return m_dbPath; }
    QString learningNotesDir() const;

    QVector<AgentTask> allTasks() const;
    QVariantList allTasksVariant() const;
    AgentTask getTask(const QString &id) const;

    bool saveTask(const AgentTask &task);
    bool deleteTask(const QString &id);
    bool toggleTask(const QString &id, bool enabled);

    void runTaskNow(const QString &id);
    void advanceCurriculum(const QString &id);
    void setCurriculumDay(const QString &id, int day);

    QVariantList getCurriculumTopicsForTask(const AgentTask &task) const;
    QVariantList getCurriculumTopicsForAgent(const QString &agentId) const;

    QVariantMap getCurriculumTopic(int day) const;
    QVariantMap getCurriculumTopic(const QString &agentId, int day) const;
    int curriculumTotalDays(const QString &agentId = QString()) const;
    QString getCurriculumWechatSummaryMarkdown(const QString &agentId = QString()) const;
    QString getCurriculumDayWechatMarkdown(int day) const;
    QString getCurriculumDayWechatMarkdown(const QString &agentId, int day) const;

    QStringList getNoteFiles() const;
    QString getNoteContent(const QString &fileName) const;

signals:
    void agentTriggered(const QVariantMap &notification);
    void tasksUpdated();

private slots:
    void onTick();

private:
    explicit AgentService(QObject *parent = nullptr);
    ~AgentService() override;

    bool ensureDatabase();
    void seedDefaultAgents();
    void loadCurriculum();
    QDateTime calculateNextRun(const QString &rule, const QDateTime &from = QDateTime::currentDateTime(), bool skipWeekends = false);
    void executeTask(AgentTask &task, bool forced);
    void logExecution(const QString &agentId, const QString &title, const QString &detail, const QString &status);

    QString m_connectionName;
    QString m_dbPath;
    bool m_dbReady = false;
    QTimer *m_tickTimer = nullptr;
    QMap<QString, QVariantList> m_curriculums;
    QVariantList m_curriculumTopics;
    mutable QRecursiveMutex m_mutex;
};

#endif // AGENTSERVICE_H
