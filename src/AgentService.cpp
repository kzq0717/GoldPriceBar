#include "AgentService.h"
#include "AppSettings.h"
#include "Logger.h"

#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QProcess>
#include <QCoreApplication>
#include <QRegularExpression>
#include <QMutexLocker>
#include <QTextStream>

QVariantMap AgentTask::toMap() const {
    QVariantMap m;
    m["id"] = id;
    m["name"] = name;
    m["type"] = type;
    m["scheduleRule"] = scheduleRule;
    m["enabled"] = enabled;
    m["content"] = content;
    m["lastRun"] = lastRun.isValid() ? lastRun.toString("yyyy-MM-dd HH:mm:ss") : QString();
    m["nextRun"] = nextRun.isValid() ? nextRun.toString("yyyy-MM-dd HH:mm:ss") : QString();
    m["createdAt"] = createdAt.isValid() ? createdAt.toString("yyyy-MM-dd HH:mm:ss") : QString();
    return m;
}

AgentTask AgentTask::fromMap(const QVariantMap &map) {
    AgentTask t;
    t.id = map.value("id").toString();
    t.name = map.value("name").toString();
    t.type = map.value("type", "reminder").toString();
    t.scheduleRule = map.value("scheduleRule", "10:00").toString();
    t.enabled = map.value("enabled", true).toBool();
    t.content = map.value("content").toMap();
    if (map.contains("lastRun") && !map.value("lastRun").toString().isEmpty()) {
        t.lastRun = QDateTime::fromString(map.value("lastRun").toString(), "yyyy-MM-dd HH:mm:ss");
    }
    if (map.contains("nextRun") && !map.value("nextRun").toString().isEmpty()) {
        t.nextRun = QDateTime::fromString(map.value("nextRun").toString(), "yyyy-MM-dd HH:mm:ss");
    }
    if (map.contains("createdAt") && !map.value("createdAt").toString().isEmpty()) {
        t.createdAt = QDateTime::fromString(map.value("createdAt").toString(), "yyyy-MM-dd HH:mm:ss");
    } else {
        t.createdAt = QDateTime::currentDateTime();
    }
    return t;
}

AgentService& AgentService::instance() {
    static AgentService inst;
    return inst;
}

AgentService::AgentService(QObject *parent)
    : QObject(parent)
    , m_connectionName(QStringLiteral("gold_agents_conn"))
{
}

AgentService::~AgentService() {
    if (m_tickTimer) {
        m_tickTimer->stop();
    }
}

bool AgentService::init() {
    QMutexLocker locker(&m_mutex);
    if (!ensureDatabase()) {
        Logger::error(QStringLiteral("AgentService database initialization failed"));
        return false;
    }

    loadCurriculum();
    seedDefaultAgents();

    if (!m_tickTimer) {
        m_tickTimer = new QTimer(this);
        connect(m_tickTimer, &QTimer::timeout, this, &AgentService::onTick);
        m_tickTimer->start(10000); // 每 10 秒轮询一次
    }

    Logger::info(QStringLiteral("AgentService initialized successfully, DB: %1").arg(m_dbPath));
    return true;
}

QString AgentService::learningNotesDir() const {
    const QString base = AppSettings::instance().resolvedDatabaseDir();
    const QString dir = QDir(base).filePath(QStringLiteral("learning_notes"));
    QDir().mkpath(dir);
    return dir;
}

bool AgentService::ensureDatabase() {
    const QString dataDir = AppSettings::instance().resolvedDatabaseDir();
    QDir().mkpath(dataDir);
    m_dbPath = QDir(dataDir).filePath(QStringLiteral("automation_agents.db"));

    if (QSqlDatabase::contains(m_connectionName)) {
        QSqlDatabase::removeDatabase(m_connectionName);
    }

    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
    db.setDatabaseName(m_dbPath);
    if (!db.open()) {
        Logger::error(QStringLiteral("Failed to open SQLite database: %1, path: %2")
            .arg(db.lastError().text(), m_dbPath));
        return false;
    }

    QSqlQuery q(db);
    // Agent 主表
    const QString createAgentsTable = QStringLiteral(
        "CREATE TABLE IF NOT EXISTS automation_agents ("
        "  id TEXT PRIMARY KEY,"
        "  name TEXT NOT NULL,"
        "  agent_type TEXT NOT NULL,"
        "  schedule_rule TEXT NOT NULL,"
        "  enabled INTEGER NOT NULL DEFAULT 1,"
        "  content_json TEXT NOT NULL,"
        "  last_run TEXT,"
        "  next_run TEXT,"
        "  created_at TEXT NOT NULL"
        ");"
    );
    if (!q.exec(createAgentsTable)) {
        Logger::error(QStringLiteral("Failed to create automation_agents table: %1").arg(q.lastError().text()));
        return false;
    }

    // Agent 执行日志表
    const QString createLogsTable = QStringLiteral(
        "CREATE TABLE IF NOT EXISTS agent_execution_logs ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  agent_id TEXT NOT NULL,"
        "  executed_at TEXT NOT NULL,"
        "  title TEXT NOT NULL,"
        "  detail TEXT,"
        "  status TEXT NOT NULL"
        ");"
    );
    if (!q.exec(createLogsTable)) {
        Logger::error(QStringLiteral("Failed to create agent_execution_logs table: %1").arg(q.lastError().text()));
        return false;
    }

    m_dbReady = true;
    return true;
}

void AgentService::loadCurriculum() {
    m_curriculums.clear();
    m_curriculumTopics.clear();

    auto loadJson = [](const QString &resPath, const QString &fileName) -> QVariantList {
        QVariantList list;
        QString jsonContent;
        QFile resFile(resPath);
        if (resFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
            jsonContent = QString::fromUtf8(resFile.readAll());
            resFile.close();
        } else {
            QString localPath = QDir(QCoreApplication::applicationDirPath()).filePath(fileName);
            QFile localFile(localPath);
            if (!localFile.exists()) {
                localPath = QDir::current().filePath(fileName);
            }
            if (localFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
                jsonContent = QString::fromUtf8(localFile.readAll());
                localFile.close();
            }
        }
        if (!jsonContent.isEmpty()) {
            QJsonDocument doc = QJsonDocument::fromJson(jsonContent.toUtf8());
            if (doc.isObject()) {
                QJsonArray topics = doc.object().value(QStringLiteral("topics")).toArray();
                for (const auto &val : topics) {
                    list.append(val.toObject().toVariantMap());
                }
            }
        }
        return list;
    };

    QVariantList cppList = loadJson(QStringLiteral(":/data/cpp_30days_curriculum.json"), QStringLiteral("data/cpp_30days_curriculum.json"));
    m_curriculumTopics = cppList;
    m_curriculums[QStringLiteral("cpp")] = cppList;
    m_curriculums[QStringLiteral("cpp_daily_master")] = cppList;

    QVariantList rustList = loadJson(QStringLiteral(":/data/rust_30days_curriculum.json"), QStringLiteral("data/rust_30days_curriculum.json"));
    m_curriculums[QStringLiteral("rust")] = rustList;
    m_curriculums[QStringLiteral("rust_master_30days")] = rustList;

    Logger::info(QStringLiteral("Loaded %1 C++ and %2 Rust curriculum topics").arg(cppList.size()).arg(rustList.size()));
}

QVariantList AgentService::getCurriculumTopicsForTask(const AgentTask &task) const {
    if (task.content.contains(QStringLiteral("curriculum_topics"))) {
        QVariantList vl = task.content.value(QStringLiteral("curriculum_topics")).toList();
        if (!vl.isEmpty()) return vl;
    }

    const QString idLower = task.id.toLower();
    const QString nameLower = task.name.toLower();
    const QString msgLower = task.content.value(QStringLiteral("message")).toString().toLower();
    const QString promptLower = task.content.value(QStringLiteral("prompt")).toString().toLower();
    const QString curId = task.content.value(QStringLiteral("curriculum_id")).toString().toLower();

    if (idLower.contains(QStringLiteral("rust")) ||
        nameLower.contains(QStringLiteral("rust")) ||
        msgLower.contains(QStringLiteral("rust")) ||
        promptLower.contains(QStringLiteral("rust")) ||
        curId.contains(QStringLiteral("rust"))) {
        return m_curriculums.value(QStringLiteral("rust"));
    }

    return m_curriculums.value(QStringLiteral("cpp"), m_curriculumTopics);
}

QVariantList AgentService::getCurriculumTopicsForAgent(const QString &agentId) const {
    if (agentId.isEmpty() || agentId == QStringLiteral("cpp_daily_master") || agentId == QStringLiteral("cpp")) {
        return m_curriculums.value(QStringLiteral("cpp"), m_curriculumTopics);
    }
    if (agentId.contains(QStringLiteral("rust"), Qt::CaseInsensitive)) {
        return m_curriculums.value(QStringLiteral("rust"));
    }
    AgentTask t = getTask(agentId);
    if (!t.id.isEmpty()) {
        return getCurriculumTopicsForTask(t);
    }
    return m_curriculumTopics;
}

void AgentService::seedDefaultAgents() {
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);
    q.exec(QStringLiteral("SELECT COUNT(*) FROM automation_agents;"));
    if (q.next() && q.value(0).toInt() == 0) {
        // 创建内置 30天 C++ 知识点推送 Agent (每天 10:00)
        AgentTask defaultCpp;
        defaultCpp.id = QStringLiteral("cpp_daily_master");
        defaultCpp.name = QStringLiteral("C++ 30天核心知识阶梯通关");
        defaultCpp.type = QStringLiteral("curriculum");
        defaultCpp.scheduleRule = QStringLiteral("workday@10:00");
        defaultCpp.enabled = true;
        defaultCpp.createdAt = QDateTime::currentDateTime();
        
        QVariantMap content;
        content["current_day"] = 1;
        content["total_days"] = 30;
        content["repeat_when_finished"] = false;
        content["skip_weekends"] = true;
        defaultCpp.content = content;
        defaultCpp.nextRun = calculateNextRun(defaultCpp.scheduleRule, QDateTime::currentDateTime(), true);

        saveTask(defaultCpp);
        Logger::info(QStringLiteral("Seeded default Agent: %1 scheduled at %2")
            .arg(defaultCpp.name, defaultCpp.scheduleRule));
    }
}

QDateTime AgentService::calculateNextRun(const QString &rule, const QDateTime &from, bool skipWeekends) {
    const QDateTime now = from.isValid() ? from : QDateTime::currentDateTime();
    const QString r = rule.trimmed();
    const bool workdayOnly = skipWeekends || r.startsWith(QStringLiteral("workday@"), Qt::CaseInsensitive);

    // 格式 1: "10:00" 或 "daily@10:00" 或 "workday@10:00"
    static QRegularExpression reTime(QStringLiteral("^(?:daily@|workday@)?(\\d{1,2}):(\\d{2})$"), QRegularExpression::CaseInsensitiveOption);
    auto match = reTime.match(r);
    if (match.hasMatch()) {
        int hour = match.captured(1).toInt();
        int minute = match.captured(2).toInt();
        QDateTime target(now.date(), QTime(hour, minute, 0));
        if (target <= now) {
            target = target.addDays(1);
        }
        if (workdayOnly) {
            while (target.date().dayOfWeek() == Qt::Saturday || target.date().dayOfWeek() == Qt::Sunday) {
                target = target.addDays(1);
            }
        }
        return target;
    }

    // 格式 2: "interval:30" (分钟) 或 "every_30m"
    static QRegularExpression reInterval(QStringLiteral("^(?:interval:|every_)(\\d+)([smhd])?$"));
    auto matchInt = reInterval.match(r);
    if (matchInt.hasMatch()) {
        int val = matchInt.captured(1).toInt();
        QString unit = matchInt.captured(2);
        QDateTime target = now;
        if (unit == QStringLiteral("s")) target = now.addSecs(val);
        else if (unit == QStringLiteral("h")) target = now.addSecs(val * 3600);
        else if (unit == QStringLiteral("d")) target = now.addDays(val);
        else target = now.addSecs(val * 60);

        if (workdayOnly) {
            while (target.date().dayOfWeek() == Qt::Saturday || target.date().dayOfWeek() == Qt::Sunday) {
                target = target.addDays(1);
            }
        }
        return target;
    }

    QDateTime fallback = now.addDays(1);
    if (workdayOnly) {
        while (fallback.date().dayOfWeek() == Qt::Saturday || fallback.date().dayOfWeek() == Qt::Sunday) {
            fallback = fallback.addDays(1);
        }
    }
    return fallback;
}

QVector<AgentTask> AgentService::allTasks() const {
    QMutexLocker locker(&m_mutex);
    QVector<AgentTask> list;
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);
    q.exec(QStringLiteral("SELECT id, name, agent_type, schedule_rule, enabled, content_json, last_run, next_run, created_at FROM automation_agents ORDER BY created_at ASC;"));
    while (q.next()) {
        AgentTask t;
        t.id = q.value(0).toString();
        t.name = q.value(1).toString();
        t.type = q.value(2).toString();
        t.scheduleRule = q.value(3).toString();
        t.enabled = q.value(4).toInt() != 0;
        
        QJsonDocument doc = QJsonDocument::fromJson(q.value(5).toString().toUtf8());
        if (doc.isObject()) {
            t.content = doc.object().toVariantMap();
        }
        
        QString lr = q.value(6).toString();
        if (!lr.isEmpty()) t.lastRun = QDateTime::fromString(lr, "yyyy-MM-dd HH:mm:ss");
        
        QString nr = q.value(7).toString();
        if (!nr.isEmpty()) t.nextRun = QDateTime::fromString(nr, "yyyy-MM-dd HH:mm:ss");
        
        QString ca = q.value(8).toString();
        if (!ca.isEmpty()) t.createdAt = QDateTime::fromString(ca, "yyyy-MM-dd HH:mm:ss");
        
        list.append(t);
    }
    return list;
}

QVariantList AgentService::allTasksVariant() const {
    QVector<AgentTask> list = allTasks();
    QVariantList vl;
    for (const auto &t : list) {
        vl.append(t.toMap());
    }
    return vl;
}

AgentTask AgentService::getTask(const QString &id) const {
    QMutexLocker locker(&m_mutex);
    AgentTask t;
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);
    q.prepare(QStringLiteral("SELECT id, name, agent_type, schedule_rule, enabled, content_json, last_run, next_run, created_at FROM automation_agents WHERE id = :id;"));
    q.bindValue(QStringLiteral(":id"), id);
    if (q.exec() && q.next()) {
        t.id = q.value(0).toString();
        t.name = q.value(1).toString();
        t.type = q.value(2).toString();
        t.scheduleRule = q.value(3).toString();
        t.enabled = q.value(4).toInt() != 0;
        
        QJsonDocument doc = QJsonDocument::fromJson(q.value(5).toString().toUtf8());
        if (doc.isObject()) {
            t.content = doc.object().toVariantMap();
        }
        
        QString lr = q.value(6).toString();
        if (!lr.isEmpty()) t.lastRun = QDateTime::fromString(lr, "yyyy-MM-dd HH:mm:ss");
        
        QString nr = q.value(7).toString();
        if (!nr.isEmpty()) t.nextRun = QDateTime::fromString(nr, "yyyy-MM-dd HH:mm:ss");
        
        QString ca = q.value(8).toString();
        if (!ca.isEmpty()) t.createdAt = QDateTime::fromString(ca, "yyyy-MM-dd HH:mm:ss");
    }
    return t;
}

bool AgentService::saveTask(const AgentTask &task) {
    QMutexLocker locker(&m_mutex);
    AgentTask tCopy = task;
    const bool skipWk = tCopy.content.value(QStringLiteral("skip_weekends"), false).toBool()
                     || tCopy.scheduleRule.startsWith(QStringLiteral("workday@"), Qt::CaseInsensitive);
    if (tCopy.enabled && (!tCopy.nextRun.isValid() || tCopy.nextRun < QDateTime::currentDateTime())) {
        tCopy.nextRun = calculateNextRun(tCopy.scheduleRule, QDateTime::currentDateTime(), skipWk);
    }

    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "INSERT INTO automation_agents (id, name, agent_type, schedule_rule, enabled, content_json, last_run, next_run, created_at) "
        "VALUES (:id, :name, :type, :rule, :en, :content, :last, :next, :create) "
        "ON CONFLICT(id) DO UPDATE SET "
        "name=excluded.name, agent_type=excluded.agent_type, schedule_rule=excluded.schedule_rule, "
        "enabled=excluded.enabled, content_json=excluded.content_json, last_run=excluded.last_run, "
        "next_run=excluded.next_run;"
    ));
    q.bindValue(QStringLiteral(":id"), tCopy.id);
    q.bindValue(QStringLiteral(":name"), tCopy.name);
    q.bindValue(QStringLiteral(":type"), tCopy.type);
    q.bindValue(QStringLiteral(":rule"), tCopy.scheduleRule);
    q.bindValue(QStringLiteral(":en"), tCopy.enabled ? 1 : 0);
    q.bindValue(QStringLiteral(":content"), QString::fromUtf8(QJsonDocument(QJsonObject::fromVariantMap(tCopy.content)).toJson(QJsonDocument::Compact)));
    q.bindValue(QStringLiteral(":last"), tCopy.lastRun.isValid() ? tCopy.lastRun.toString("yyyy-MM-dd HH:mm:ss") : QVariant());
    q.bindValue(QStringLiteral(":next"), tCopy.nextRun.isValid() ? tCopy.nextRun.toString("yyyy-MM-dd HH:mm:ss") : QVariant());
    q.bindValue(QStringLiteral(":create"), tCopy.createdAt.isValid() ? tCopy.createdAt.toString("yyyy-MM-dd HH:mm:ss") : QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss"));

    bool ok = q.exec();
    if (ok) {
        emit tasksUpdated();
    } else {
        Logger::error(QStringLiteral("Failed to save AgentTask %1: %2").arg(tCopy.id, q.lastError().text()));
    }
    return ok;
}

bool AgentService::deleteTask(const QString &id) {
    QMutexLocker locker(&m_mutex);
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);
    q.prepare(QStringLiteral("DELETE FROM automation_agents WHERE id = :id;"));
    q.bindValue(QStringLiteral(":id"), id);
    bool ok = q.exec();
    if (ok) {
        emit tasksUpdated();
    }
    return ok;
}

bool AgentService::toggleTask(const QString &id, bool enabled) {
    AgentTask t = getTask(id);
    if (t.id.isEmpty()) return false;
    t.enabled = enabled;
    const bool skipWk = t.content.value(QStringLiteral("skip_weekends"), false).toBool()
                     || t.scheduleRule.startsWith(QStringLiteral("workday@"), Qt::CaseInsensitive);
    if (enabled && (!t.nextRun.isValid() || t.nextRun < QDateTime::currentDateTime())) {
        t.nextRun = calculateNextRun(t.scheduleRule, QDateTime::currentDateTime(), skipWk);
    }
    return saveTask(t);
}

void AgentService::runTaskNow(const QString &id) {
    AgentTask t = getTask(id);
    if (!t.id.isEmpty()) {
        executeTask(t, true);
    }
}

void AgentService::advanceCurriculum(const QString &id) {
    AgentTask t = getTask(id);
    if (!t.id.isEmpty() && t.type == QStringLiteral("curriculum")) {
        int cur = t.content.value("current_day", 1).toInt();
        t.content["current_day"] = cur + 1;
        saveTask(t);
    }
}

void AgentService::setCurriculumDay(const QString &id, int day) {
    AgentTask t = getTask(id);
    if (!t.id.isEmpty() && t.type == QStringLiteral("curriculum")) {
        t.content["current_day"] = qBound(1, day, 30);
        saveTask(t);
    }
}

QVariantMap AgentService::getCurriculumTopic(int day) const {
    return getCurriculumTopic(QStringLiteral("cpp"), day);
}

QVariantMap AgentService::getCurriculumTopic(const QString &agentId, int day) const {
    QVariantList topics = getCurriculumTopicsForAgent(agentId);
    if (day >= 1 && day <= topics.size()) {
        return topics.at(day - 1).toMap();
    }
    return QVariantMap();
}

int AgentService::curriculumTotalDays(const QString &agentId) const {
    QVariantList topics = getCurriculumTopicsForAgent(agentId);
    return topics.size() > 0 ? topics.size() : 30;
}

QString AgentService::getCurriculumWechatSummaryMarkdown(const QString &agentId) const {
    QMutexLocker locker(&m_mutex);
    QVariantList topics = getCurriculumTopicsForAgent(agentId);
    if (topics.isEmpty()) {
        return QStringLiteral("⚠️ 未找到 30 天课程大纲数据");
    }

    bool isRust = agentId.contains(QStringLiteral("rust"), Qt::CaseInsensitive);
    if (!isRust) {
        AgentTask t = getTask(agentId);
        if (!t.id.isEmpty() && (t.name.contains("Rust", Qt::CaseInsensitive) || t.id.contains("rust", Qt::CaseInsensitive))) {
            isRust = true;
        }
    }

    QString md;
    QTextStream out(&md);

    if (isRust) {
        out << QStringLiteral("# 🚀【收藏级】Rust 30天从入门到学通 (含两大实战项目)·全景知识大纲\n\n");
        out << QStringLiteral("> 📌 **导语**：\n");
        out << QStringLiteral("> 本技术路线专为攻坚 Rust 内存安全、零成本抽象与高并发系统开发的工程师量身定制。涵盖**所有权与借用检查、复合类型与模式匹配、Trait特征多态、生命周期标注、智能指针、无畏并发与 Tokio 异步编程**，并最终完整落地**Mini-Ripgrep 多线程搜索工具**与**Mini-Redis 异步高并发内存服务**两大工业级实战项目！建议**星标收藏**，开启每日技术打卡！\n\n");
        out << QStringLiteral("---\n\n");
        out << QStringLiteral("## 🗺️ 30天五大阶梯成长路线总览\n\n");
        out << QStringLiteral("| 阶梯阶段 | 周期规划 | 核心攻坚方向 | 典型代表主题 |\n");
        out << QStringLiteral("| :--- | :--- | :--- | :--- |\n");
        out << QStringLiteral("| **第一阶段** | Day 01 - Day 07 | 语言基石与所有权内存模型 | Cargo工程规范、不可变性、所有权转移、借用规则、切片内存布局 |\n");
        out << QStringLiteral("| **第二阶段** | Day 08 - Day 14 | 复合类型与健壮错误处理 | HashMap底细、Option/Result、泛型单态化、Trait特征、动态分发、生命周期标注 |\n");
        out << QStringLiteral("| **第三阶段** | Day 15 - Day 21 | 高级特征与智能指针基石 | Fn闭包三剑客、惰性迭代器、Box堆分配、Deref/Drop、Rc/Arc、RefCell内部可变性 |\n");
        out << QStringLiteral("| **第四阶段** | Day 22 - Day 26 | 无畏并发与异步编程 | 原生线程与move闭包、mpsc通道通信、Arc<Mutex>并发锁、Send/Sync契约、Tokio异步栈 |\n");
        out << QStringLiteral("| **第五阶段** | Day 27 - Day 30 | 宏系统与两大实战项目 | 声明式宏、Unsafe与FFI、实战一:Mini-Ripgrep搜索器、实战二:Mini-Redis缓存服务 |\n\n");
        out << QStringLiteral("---\n\n");
    } else {
        out << QStringLiteral("# 🚀【收藏级】C++ 30天核心知识阶梯通关·全景知识大纲\n\n");
        out << QStringLiteral("> 📌 **导语**：\n");
        out << QStringLiteral("> 本技术路线专为攻坚现代 C++ 底层内核与高性能架构的开发者量身定制。涵盖**虚拟内存分区、面向对象本质与虚表机制、C++11~20现代核心特性、泛型模板与STL底层剖析，以及高并发多线程与极致性能调优**。每日专注一个深度知识点，30天系统构筑不可动摇的底层技术壁垒！建议**星标收藏**，开启每日技术打卡！\n\n");
        out << QStringLiteral("---\n\n");
        out << QStringLiteral("## 🗺️ 30天五大阶梯成长路线总览\n\n");
        out << QStringLiteral("| 阶梯阶段 | 周期规划 | 核心攻坚方向 | 典型代表主题 |\n");
        out << QStringLiteral("| :--- | :--- | :--- | :--- |\n");
        out << QStringLiteral("| **第一阶段** | Day 01 - Day 07 | 底层内存与指针引用基石 | 虚拟内存分区、引用底层、const不变性、内存对齐 |\n");
        out << QStringLiteral("| **第二阶段** | Day 08 - Day 14 | 面向对象本质与资源管理 (RAII) | 虚函数表底层、菱形继承、RAII哲学、四种类型转换 |\n");
        out << QStringLiteral("| **第三阶段** | Day 15 - Day 21 | 现代 C++ 核心：移动语义与智能指针 | 右值引用与转发、智能指针底层、Lambda与类型推导 |\n");
        out << QStringLiteral("| **第四阶段** | Day 22 - Day 26 | 泛型编程、模板与 STL 底层机制 | 模板元编程、特化偏特化、SFINAE、STL容器底层与失效 |\n");
        out << QStringLiteral("| **第五阶段** | Day 27 - Day 30 | 并发编程、C++20 新特性与极致调优 | 多线程原子操作、无锁队列、C++20协程与性能剖析 |\n\n");
        out << QStringLiteral("---\n\n");
    }

    out << QStringLiteral("## 📅 30天逐日通关知识大纲详解\n\n");

    QString currentStage;
    for (int i = 0; i < topics.size(); ++i) {
        QVariantMap topic = topics.at(i).toMap();
        int day = topic.value(QStringLiteral("day"), i + 1).toInt();
        QString stage = topic.value(QStringLiteral("stage")).toString();
        QString title = topic.value(QStringLiteral("title")).toString();
        int diff = topic.value(QStringLiteral("difficulty"), 1).toInt();
        QString summary = topic.value(QStringLiteral("summary")).toString();
        QVariantList kps = topic.value(QStringLiteral("key_points")).toList();
        QString pitfall = topic.value(QStringLiteral("pitfall_or_interview")).toString();

        if (stage != currentStage) {
            currentStage = stage;
            out << QStringLiteral("### 🌟 %1\n\n").arg(currentStage);
        }

        QString dayStr = QStringLiteral("%1").arg(day, 2, 10, QLatin1Char('0'));
        QString stars = QString(diff, QChar(0x2B50));
        if (stars.isEmpty()) stars = QStringLiteral("⭐");

        out << QStringLiteral("#### 🔹 Day %1 · %2\n").arg(dayStr, title);
        out << QStringLiteral("- **难度星级**：%1\n").arg(stars);
        out << QStringLiteral("- **核心概述**：%1\n").arg(summary);
        if (!kps.isEmpty()) {
            out << QStringLiteral("- **深度要点**：\n");
            for (const QVariant &kp : kps) {
                out << QStringLiteral("  - %1\n").arg(kp.toString());
            }
        }
        if (!pitfall.isEmpty()) {
            out << QStringLiteral("- **💡 避坑与面试重点**：%1\n").arg(pitfall);
        }
        out << QStringLiteral("\n");
    }

    out << QStringLiteral("---\n\n");
    out << QStringLiteral("## 💬 学习打卡与公众号互动\n\n");
    out << QStringLiteral("1. **每日目标**：建议每天投入 15~30 分钟进行理论推敲与代码实战，不求快但求透彻；\n");
    out << QStringLiteral("2. **交流打卡**：欢迎在文末**留言打卡**或提出疑问，与广大技术极客共同交流探讨；\n");
    out << QStringLiteral("3. **关注星标**：敬请关注本公众号，第一时间接收后续每日深度精讲推文更新！\n\n");
    out << QStringLiteral("> ⚙️ *本文档由 GoldPriceBar 自动化 Agent 任务中心课程系统自动导出生成*");

    return md;
}

QString AgentService::getCurriculumDayWechatMarkdown(int day) const {
    return getCurriculumDayWechatMarkdown(QStringLiteral("cpp"), day);
}

QString AgentService::getCurriculumDayWechatMarkdown(const QString &agentId, int day) const {
    QMutexLocker locker(&m_mutex);
    QVariantList topics = getCurriculumTopicsForAgent(agentId);
    if (day < 1 || day > topics.size()) {
        return QStringLiteral("⚠️ 无效的天数编号: %1").arg(day);
    }

    QVariantMap topic = topics.at(day - 1).toMap();
    QString stage = topic.value(QStringLiteral("stage")).toString();
    QString title = topic.value(QStringLiteral("title")).toString();
    int diff = topic.value(QStringLiteral("difficulty"), 1).toInt();
    QString summary = topic.value(QStringLiteral("summary")).toString();
    QVariantList kps = topic.value(QStringLiteral("key_points")).toList();
    QString code = topic.value(QStringLiteral("code_example")).toString();
    QString pitfall = topic.value(QStringLiteral("pitfall_or_interview")).toString();

    bool isRust = agentId.contains(QStringLiteral("rust"), Qt::CaseInsensitive);
    if (!isRust) {
        AgentTask t = getTask(agentId);
        if (!t.id.isEmpty() && (t.name.contains("Rust", Qt::CaseInsensitive) || t.id.contains("rust", Qt::CaseInsensitive))) {
            isRust = true;
        }
    }

    QString coursePrefix = isRust ? QStringLiteral("Rust 30天通关") : QStringLiteral("C++ 30天通关");
    QString codeLang = isRust ? QStringLiteral("rust") : QStringLiteral("cpp");

    QString dayStr = QStringLiteral("%1").arg(day, 2, 10, QLatin1Char('0'));
    QString stars = QString(diff, QChar(0x2B50));
    if (stars.isEmpty()) stars = QStringLiteral("⭐");

    QString md;
    QTextStream out(&md);

    out << QStringLiteral("# 📚 %1 | Day %2 · %3\n\n").arg(coursePrefix, dayStr, title);
    out << QStringLiteral("> 🏷️ **阶段归属**：%1  \n").arg(stage);
    out << QStringLiteral("> 🌟 **难度星级**：%1  \n").arg(stars);
    out << QStringLiteral("> ⏱️ **建议研读时长**：15 分钟  \n\n");
    out << QStringLiteral("---\n\n");

    out << QStringLiteral("## 01 / 核心概念与背景\n\n");
    out << QStringLiteral("%1\n\n").arg(summary);
    out << QStringLiteral("---\n\n");

    if (!kps.isEmpty()) {
        out << QStringLiteral("## 02 / 核心要点深入精析\n\n");
        for (const QVariant &kp : kps) {
            out << QStringLiteral("- **%1**\n").arg(kp.toString());
        }
        out << QStringLiteral("\n---\n\n");
    }

    if (!code.isEmpty()) {
        out << QStringLiteral("## 03 / 经典精辟代码示例\n\n");
        out << QStringLiteral("```%1\n").arg(codeLang);
        out << code;
        out << QStringLiteral("\n```\n\n");
        out << QStringLiteral("---\n\n");
    }

    if (!pitfall.isEmpty()) {
        out << QStringLiteral("## 04 / ⚠️ 常见避坑与面试重点\n\n");
        out << QStringLiteral("> %1\n\n").arg(pitfall);
        out << QStringLiteral("---\n\n");
    }

    out << QStringLiteral("## 05 / 每日自测与思考\n\n");
    out << QStringLiteral("1. 你能否向同伴清晰阐述该核心考点的底层运作机制？\n");
    out << QStringLiteral("2. 在工程实践中，遇到类似场景你通常会如何权衡？\n\n");
    out << QStringLiteral("欢迎在**留言区**写下你的思考或实战体会，与大家一起打卡交流！\n\n");
    out << QStringLiteral("> 💡 *下一期预告：Day %1 即将推送，点击关注不错过！*").arg(day + 1 <= topics.size() ? QStringLiteral("%1").arg(day + 1, 2, 10, QLatin1Char('0')) : QStringLiteral("总结篇"));

    return md;
}

QStringList AgentService::getNoteFiles() const {
    QDir dir(learningNotesDir());
    if (!dir.exists()) return {};
    return dir.entryList(QStringList() << QStringLiteral("*.md"), QDir::Files, QDir::Name);
}

QString AgentService::getNoteContent(const QString &fileName) const {
    QDir dir(learningNotesDir());
    QString cleanName = QFileInfo(fileName).fileName();
    QFile file(dir.filePath(cleanName));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QStringLiteral("无法读取笔记文件: %1").arg(cleanName);
    }
    QTextStream in(&file);
    in.setEncoding(QStringConverter::Utf8);
    return in.readAll();
}

void AgentService::onTick() {
    const QDateTime now = QDateTime::currentDateTime();
    QVector<AgentTask> tasks = allTasks();
    for (auto &t : tasks) {
        if (!t.enabled) continue;
        if (t.nextRun.isValid() && now >= t.nextRun) {
            executeTask(t, false);
        }
    }
}

void AgentService::executeTask(AgentTask &task, bool forced) {
    const QDateTime now = QDateTime::currentDateTime();
    task.lastRun = now;
    const bool skipWk = task.content.value(QStringLiteral("skip_weekends"), false).toBool()
                     || task.scheduleRule.startsWith(QStringLiteral("workday@"), Qt::CaseInsensitive);
    task.nextRun = calculateNextRun(task.scheduleRule, now, skipWk);

    QVariantMap notification;
    notification["agentId"] = task.id;
    notification["agentName"] = task.name;
    notification["agentType"] = task.type;
    notification["executedAt"] = now.toString("yyyy-MM-dd HH:mm:ss");

    if (task.type == QStringLiteral("curriculum")) {
        QVariantList topics = getCurriculumTopicsForTask(task);
        int totalDays = topics.size() > 0 ? topics.size() : task.content.value(QStringLiteral("total_days"), 30).toInt();
        int curDay = task.content.value(QStringLiteral("current_day"), 1).toInt();

        if (curDay > totalDays) {
            notification["title"] = QStringLiteral("🎉 恭喜！【%1】30天知识与实战项目已全部学完！").arg(task.name);
            notification["summary"] = QStringLiteral("您已达成整整 %1 天阶梯通关，已掌握从入门到工业级实战项目落地的完整能力。").arg(totalDays);
            notification["day"] = curDay;
            notification["totalDays"] = totalDays;
            notification["isCompleted"] = true;
            logExecution(task.id, notification["title"].toString(), notification["summary"].toString(), QStringLiteral("COMPLETED"));
        } else if (topics.size() >= curDay) {
            QVariantMap topic = topics.at(curDay - 1).toMap();
            QString title = topic.value("title").toString();
            int diff = topic.value("difficulty", 1).toInt();
            QString summary = topic.value("summary").toString();
            QVariantList kps = topic.value("key_points").toList();
            QString codeSnippet = topic.value("code_example").toString();
            QString pitfall = topic.value("pitfall_or_interview").toString();
            QString stage = topic.value("stage").toString();

            notification["day"] = curDay;
            notification["totalDays"] = totalDays;
            notification["stage"] = stage;
            notification["title"] = QStringLiteral("Day %1: %2").arg(QString::number(curDay).rightJustified(2, '0'), title);
            notification["topicTitle"] = title;
            notification["difficulty"] = diff;
            notification["stars"] = QStringLiteral("⭐").repeated(diff);
            notification["summary"] = summary;
            notification["keyPoints"] = kps;
            notification["codeSnippet"] = codeSnippet;
            notification["pitfall"] = pitfall;

            // 写入本地 Markdown 学习笔记
            const QString notesDir = learningNotesDir();
            bool isRust = (task.name.contains("Rust", Qt::CaseInsensitive) || task.id.contains("rust", Qt::CaseInsensitive));
            QString prefix = isRust ? QStringLiteral("Rust") : QStringLiteral("C++");
            QString safeName = QStringLiteral("%1_%2").arg(prefix, title);
            safeName.replace(QRegularExpression(QStringLiteral(R"([\/*?:"<>| ])")), QStringLiteral("_"));
            const QString notePath = QDir(notesDir).filePath(QStringLiteral("Day_%1_%2.md").arg(QString::number(curDay).rightJustified(2, '0'), safeName));

            QFile nf(notePath);
            if (nf.open(QIODevice::WriteOnly | QIODevice::Text)) {
                QTextStream out(&nf);
                out.setEncoding(QStringConverter::Utf8);
                out << "# " << notification["title"].toString() << "\n\n";
                out << "- **所属任务**：" << task.name << "\n";
                out << "- **所属阶段**：" << stage << "\n";
                out << "- **难度等级**：" << notification["stars"].toString() << " (" << diff << "/5)\n";
                out << "- **推送时间**：" << now.toString("yyyy-MM-dd HH:mm:ss") << "\n";
                out << "- **进度归档**：Day " << curDay << " / " << totalDays << " 天\n\n";
                out << "---\n\n## 📌 核心概述\n> " << summary << "\n\n";
                out << "---\n\n## 💡 深度要点\n";
                for (const auto &p : kps) {
                    out << "- **" << p.toString() << "**\n";
                }
                QString lang = isRust ? "rust" : "cpp";
                out << "\n---\n\n## 💻 经典精辟代码示例\n```" << lang << "\n" << codeSnippet << "\n```\n\n";
                out << "---\n\n## ⚠️ 常见避坑与面试重点\n> " << pitfall << "\n\n";
                out << "---\n*💡 GoldPriceBar 自动化学习 Agent 定时推送 · 掌握系统级内功*\n";
                nf.close();
            }
            notification["notePath"] = notePath;

            // 自动推算推进到下一天
            task.content["current_day"] = curDay + 1;
            logExecution(task.id, notification["title"].toString(), QStringLiteral("笔记已保存至: %1").arg(notePath), QStringLiteral("SUCCESS"));
        } else {
            // 没有大纲数据的自定义任务兜底推进
            QString msg = task.content.value("message").toString().trimmed();
            if (msg.isEmpty()) msg = task.content.value("prompt").toString().trimmed();
            if (msg.isEmpty()) msg = task.name;

            notification["day"] = curDay;
            notification["totalDays"] = totalDays;
            notification["stage"] = QStringLiteral("阶梯推进阶段 (Day %1/%2)").arg(curDay).arg(totalDays);
            notification["summary"] = msg;
            notification["prompt"] = msg;
            notification["title"] = QStringLiteral("Day %1: %2").arg(QString::number(curDay).rightJustified(2, '0'), task.name);
            task.content["current_day"] = curDay + 1;
            logExecution(task.id, notification["title"].toString(), msg, QStringLiteral("SUCCESS"));
        }
        } else if (task.type == QStringLiteral("command")) {
        QString cmd = task.content.value("command").toString();
        notification["title"] = QStringLiteral("[%1] 系统脚本已执行").arg(task.name);
        if (cmd.isEmpty()) {
            notification["summary"] = QStringLiteral("未配置任何执行命令");
        } else {
            QProcess proc;
            proc.startCommand(cmd);
            proc.waitForFinished(10000);
            QString out = QString::fromUtf8(proc.readAllStandardOutput()).trimmed();
            QString err = QString::fromUtf8(proc.readAllStandardError()).trimmed();
            notification["summary"] = out.isEmpty() ? (err.isEmpty() ? QStringLiteral("执行完成 (无控制台输出)") : err) : out;
        }
        logExecution(task.id, notification["title"].toString(), notification["summary"].toString(), QStringLiteral("SUCCESS"));
    } else {
        // reminder 模式 (包括普通提醒与 AI 智能任务)
        QString msg = task.content.value("message").toString().trimmed();
        if (msg.isEmpty()) {
            msg = task.content.value("prompt").toString().trimmed();
        }
        if (msg.isEmpty()) {
            msg = QStringLiteral("自动化任务定时提醒");
        }
        notification["title"] = QStringLiteral("⏰ 任务提醒：%1").arg(task.name);
        notification["summary"] = msg;
        notification["prompt"] = msg;
        logExecution(task.id, notification["title"].toString(), msg, QStringLiteral("SUCCESS"));
    }

    saveTask(task);
    emit agentTriggered(notification);
}

void AgentService::logExecution(const QString &agentId, const QString &title, const QString &detail, const QString &status) {
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);
    q.prepare(QStringLiteral("INSERT INTO agent_execution_logs (agent_id, executed_at, title, detail, status) VALUES (:id, :ts, :title, :detail, :status);"));
    q.bindValue(QStringLiteral(":id"), agentId);
    q.bindValue(QStringLiteral(":ts"), QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss"));
    q.bindValue(QStringLiteral(":title"), title);
    q.bindValue(QStringLiteral(":detail"), detail);
    q.bindValue(QStringLiteral(":status"), status);
    q.exec();
}
