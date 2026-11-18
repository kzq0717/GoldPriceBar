#include "AgentAiService.h"
#include "AppSettings.h"
#include "Logger.h"

#include <QUrl>
#include <QUrlQuery>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QNetworkRequest>

AgentAiService& AgentAiService::instance() {
    static AgentAiService inst;
    return inst;
}

AgentAiService::AgentAiService(QObject *parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this))
{
}

AgentAiService::~AgentAiService() {
    cancel();
}

bool AgentAiService::isBusy() const {
    return (m_currentReply != nullptr && m_currentReply->isRunning());
}

QString AgentAiService::defaultProvider() const {
    QString p = AppSettings::instance().llmProvider().trimmed().toLower();
    return p.isEmpty() ? QStringLiteral("gemini") : p;
}

QString AgentAiService::defaultApiKey() const {
    return AppSettings::instance().xaiApiKey().trimmed();
}

QString AgentAiService::defaultModel() const {
    QString p = defaultProvider();
    QString m = AppSettings::instance().xaiModel().trimmed();

    if (p == QStringLiteral("gemini")) {
        if (m.isEmpty() || m.contains(QStringLiteral("grok"), Qt::CaseInsensitive)) {
            return QStringLiteral("gemini-2.5-flash");
        }
        return m;
    } else if (p == QStringLiteral("xai")) {
        if (m.isEmpty() || m.contains(QStringLiteral("gemini"), Qt::CaseInsensitive)) {
            return QStringLiteral("grok-2-latest");
        }
        return m;
    } else if (p == QStringLiteral("deepseek")) {
        if (m.isEmpty() || !m.contains(QStringLiteral("deepseek"), Qt::CaseInsensitive)) {
            return QStringLiteral("deepseek-chat");
        }
        return m;
    } else if (p == QStringLiteral("glm") || p == QStringLiteral("zhipu")) {
        if (m.isEmpty() || !m.contains(QStringLiteral("glm"), Qt::CaseInsensitive)) {
            return QStringLiteral("glm-4-flash");
        }
        return m;
    } else if (p == QStringLiteral("qwen")) {
        if (m.isEmpty() || !m.contains(QStringLiteral("qwen"), Qt::CaseInsensitive)) {
            return QStringLiteral("qwen-plus");
        }
        return m;
    } else if (p == QStringLiteral("moonshot")) {
        if (m.isEmpty() || !m.contains(QStringLiteral("moonshot"), Qt::CaseInsensitive)) {
            return QStringLiteral("moonshot-v1-8k");
        }
        return m;
    } else if (p == QStringLiteral("openai")) {
        if (m.isEmpty() || (!m.contains(QStringLiteral("gpt"), Qt::CaseInsensitive) && !m.contains(QStringLiteral("o1"), Qt::CaseInsensitive))) {
            return QStringLiteral("gpt-4o-mini");
        }
        return m;
    }
    return m.isEmpty() ? QStringLiteral("deepseek-chat") : m;
}

void AgentAiService::cancel() {
    if (m_currentReply) {
        if (m_currentReply->isRunning()) {
            m_currentReply->abort();
        }
        m_currentReply->deleteLater();
        m_currentReply.clear();
    }
}

void AgentAiService::askQuestion(const QString &question,
                                 const QString &contextTopic,
                                 const QString &provider,
                                 const QString &model,
                                 const QString &apiKey,
                                 const QString &customEndpoint)
{
    cancel();

    const QString q = question.trimmed();
    if (q.isEmpty()) {
        emit answerFailed(QString(), QStringLiteral("提问内容不能为空"));
        return;
    }

    QString prov = provider.trimmed().toLower();
    if (prov.isEmpty()) prov = defaultProvider();

    QString key = apiKey.trimmed();
    if (key.isEmpty()) key = defaultApiKey();

    if (key.isEmpty()) {
        emit answerFailed(q, QStringLiteral("未检测到 API Key。请在下方输入或在「系统设置 - AI研判」中配置 Gemini / Grok 的 API Key。"));
        return;
    }

    QString mdl = model.trimmed();
    if (mdl.isEmpty()) {
        if (prov == QStringLiteral("gemini")) mdl = QStringLiteral("gemini-flash-latest");
        else if (prov == QStringLiteral("xai")) mdl = QStringLiteral("grok-2-latest");
        else if (prov == QStringLiteral("deepseek")) mdl = QStringLiteral("deepseek-chat");
        else mdl = QStringLiteral("gpt-4o-mini");
    } else {
        // 模型平台防呆纠偏：若用户之前配置了 Grok 但切换到了 Gemini，自动纠偏为 Gemini 模型
        if (prov == QStringLiteral("gemini") && mdl.contains(QStringLiteral("grok"), Qt::CaseInsensitive)) {
            mdl = QStringLiteral("gemini-flash-latest");
        } else if (prov == QStringLiteral("xai") && mdl.contains(QStringLiteral("gemini"), Qt::CaseInsensitive)) {
            mdl = QStringLiteral("grok-2-latest");
        }
    }

    m_activeQuestion = q;
    m_activeProvider = prov;
    m_activeModel = mdl;

    emit answerStarted(q);

    // 构建 System Prompt 强化专业性
    const QString systemPrompt = QStringLiteral(
        "你是一位精通现代 C++ (C++11/14/17/20/23)、系统底层机理、高性能并发编程与软件架构的资深专家兼耐心导师。\n"
        "你的任务是为学员解答 C++ 与计算机底层核心疑难，针对学员所学主题进行深度延伸与面试重点剖析。\n"
        "请遵循以下回答准则：\n"
        "1. 结论先行，直击底层运行本质（如内存布局、对象生命周期、编译器行为或汇编细节）；\n"
        "2. 提供高质量、可编译的现代 C++ 精辟代码范例，附带关键行注释；\n"
        "3. 明确指出生产环境中的常见踩坑点、未定义行为 (UB) 或大厂面试高频追问方向；\n"
        "4. 输出排版使用清晰优雅的 Markdown 格式，代码块注明 cpp 语法高亮。"
    );

    QString userContent;
    if (!contextTopic.trimmed().isEmpty()) {
        userContent = QStringLiteral("【当前所学课题/背景】\n%1\n\n【学员提问/延伸追问】\n%2")
                          .arg(contextTopic.trimmed(), q);
    } else {
        userContent = q;
    }

    QNetworkRequest request;
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("GoldPriceBarLite/1.4.0 (AI Agent Module)"));
    request.setTransferTimeout(45000); // 45 秒超时
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);

    if (prov == QStringLiteral("gemini")) {
        QString geminiModel = mdl;
        if (geminiModel.startsWith(QStringLiteral("models/"))) {
            geminiModel = geminiModel.mid(7);
        }

        const QUrl url(QStringLiteral("https://generativelanguage.googleapis.com/v1beta/models/%1:generateContent?key=%2")
                           .arg(geminiModel, QString::fromUtf8(QUrl::toPercentEncoding(key))));
        request.setUrl(url);
        request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));

        QJsonObject body;
        QJsonArray contents;
        QJsonObject userMsg;
        userMsg.insert(QStringLiteral("role"), QStringLiteral("user"));

        QJsonArray parts;
        parts.append(QJsonObject{{QStringLiteral("text"), systemPrompt + QStringLiteral("\n\n---\n\n") + userContent}});
        userMsg.insert(QStringLiteral("parts"), parts);
        contents.append(userMsg);
        body.insert(QStringLiteral("contents"), contents);

        QJsonObject genCfg;
        genCfg.insert(QStringLiteral("maxOutputTokens"), 4096);
        genCfg.insert(QStringLiteral("temperature"), 0.5);
        body.insert(QStringLiteral("generationConfig"), genCfg);

        Logger::info(QStringLiteral("AgentAiService: sending request to Gemini model=%1").arg(geminiModel));
        m_currentReply = m_nam->post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    } else {
        // OpenAI 兼容协议 (xAI Grok / DeepSeek / 自定义 Endpoint)
        QUrl url;
        if (!customEndpoint.trimmed().isEmpty()) {
            url = QUrl(customEndpoint.trimmed());
        } else if (prov == QStringLiteral("xai")) {
            url = QUrl(QStringLiteral("https://api.x.ai/v1/chat/completions"));
        } else if (prov == QStringLiteral("deepseek")) {
            url = QUrl(QStringLiteral("https://api.deepseek.com/chat/completions"));
        } else if (prov == QStringLiteral("glm") || prov == QStringLiteral("zhipu")) {
            url = QUrl(QStringLiteral("https://open.bigmodel.cn/api/paas/v4/chat/completions"));
        } else if (prov == QStringLiteral("qwen")) {
            url = QUrl(QStringLiteral("https://dashscope.aliyuncs.com/compatible-mode/v1/chat/completions"));
        } else if (prov == QStringLiteral("moonshot")) {
            url = QUrl(QStringLiteral("https://api.moonshot.cn/v1/chat/completions"));
        } else {
            url = QUrl(QStringLiteral("https://api.openai.com/v1/chat/completions"));
        }

        request.setUrl(url);
        request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
        request.setRawHeader("Authorization", QByteArray("Bearer ") + key.toUtf8());

        QJsonObject body;
        body.insert(QStringLiteral("model"), mdl);
        body.insert(QStringLiteral("temperature"), 0.5);
        body.insert(QStringLiteral("max_tokens"), 4096);

        QJsonArray messages;
        messages.append(QJsonObject{{QStringLiteral("role"), QStringLiteral("system")}, {QStringLiteral("content"), systemPrompt}});
        messages.append(QJsonObject{{QStringLiteral("role"), QStringLiteral("user")}, {QStringLiteral("content"), userContent}});
        body.insert(QStringLiteral("messages"), messages);

        Logger::info(QStringLiteral("AgentAiService: sending request to %1 (%2) model=%3").arg(prov, url.toString(), mdl));
        m_currentReply = m_nam->post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    }

    QNetworkReply *reply = m_currentReply.data();
    if (!reply) {
        emit answerFailed(q, QStringLiteral("创建网络请求失败"));
        return;
    }

    connect(reply, &QNetworkReply::finished, this, [this, reply, q, prov, mdl]() {
        if (m_currentReply == reply) {
            m_currentReply.clear();
        }

        const auto err = reply->error();
        const QString errStr = reply->errorString();
        const int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray responseData = reply->readAll();
        reply->deleteLater();

        if (err != QNetworkReply::NoError && responseData.isEmpty()) {
            QString hint;
            if (err == QNetworkReply::ProxyConnectionRefusedError || errStr.contains(QStringLiteral("Proxy"), Qt::CaseInsensitive)) {
                hint = QStringLiteral("无法连接本地代理服务 (127.0.0.1:%1)，请检查代理软件是否开启或在设置中关闭代理").arg(AppSettings::instance().proxyPort());
            } else {
                hint = QStringLiteral("网络请求失败: %1 (HTTP %2，若为海外模型请检查系统代理)").arg(errStr, QString::number(statusCode));
            }
            Logger::error(QStringLiteral("AgentAiService network error: %1 (HTTP %2)").arg(errStr, QString::number(statusCode)));
            emit answerFailed(q, hint);
            return;
        }

        if (prov == QStringLiteral("gemini")) {
            processGeminiResponse(q, mdl, responseData);
        } else {
            processOpenAiResponse(q, prov, mdl, responseData);
        }
    });
}

void AgentAiService::processGeminiResponse(const QString &question, const QString &model, const QByteArray &data) {
    QJsonParseError pe{};
    const QJsonDocument doc = QJsonDocument::fromJson(data, &pe);
    if (pe.error != QJsonParseError::NoError || !doc.isObject()) {
        Logger::error(QStringLiteral("AgentAiService: Gemini invalid JSON response: %1").arg(QString::fromUtf8(data.left(200))));
        emit answerFailed(question, QStringLiteral("模型响应格式解析失败: %1").arg(QString::fromUtf8(data.left(120)).trimmed()));
        return;
    }

    QJsonObject root = doc.object();
    if (root.contains(QStringLiteral("error"))) {
        QJsonObject errObj = root.value(QStringLiteral("error")).toObject();
        QString msg = errObj.value(QStringLiteral("message")).toString();
        emit answerFailed(question, QStringLiteral("Gemini 接口报错: %1").arg(msg));
        return;
    }

    QJsonArray candidates = root.value(QStringLiteral("candidates")).toArray();
    if (!candidates.isEmpty()) {
        QJsonObject first = candidates.at(0).toObject();
        QJsonObject content = first.value(QStringLiteral("content")).toObject();
        QJsonArray parts = content.value(QStringLiteral("parts")).toArray();
        QString answer;
        for (const auto &p : parts) {
            answer += p.toObject().value(QStringLiteral("text")).toString();
        }
        if (!answer.isEmpty()) {
            Logger::info(QStringLiteral("AgentAiService: Gemini answered successfully (%1 chars)").arg(answer.length()));
            emit answerFinished(question, answer.trimmed(), QStringLiteral("gemini"), model);
            return;
        }

        const QString finishReason = first.value(QStringLiteral("finishReason")).toString();
        if (!finishReason.isEmpty() && finishReason != QStringLiteral("STOP")) {
            emit answerFailed(question, QStringLiteral("Gemini 终止输出 (原因: %1)").arg(finishReason));
            return;
        }
    }

    emit answerFailed(question, QStringLiteral("未能获取 Gemini 模型有效回复内容"));
}

void AgentAiService::processOpenAiResponse(const QString &question, const QString &provider, const QString &model, const QByteArray &data) {
    QJsonParseError pe{};
    const QJsonDocument doc = QJsonDocument::fromJson(data, &pe);
    if (pe.error != QJsonParseError::NoError || !doc.isObject()) {
        Logger::error(QStringLiteral("AgentAiService: OpenAI invalid JSON response: %1").arg(QString::fromUtf8(data.left(200))));
        emit answerFailed(question, QStringLiteral("模型响应格式解析失败: %1").arg(QString::fromUtf8(data.left(120)).trimmed()));
        return;
    }

    QJsonObject root = doc.object();
    if (root.contains(QStringLiteral("error"))) {
        QJsonObject errObj = root.value(QStringLiteral("error")).toObject();
        QString msg = errObj.value(QStringLiteral("message")).toString();
        emit answerFailed(question, QStringLiteral("%1 接口报错: %2").arg(provider.toUpper(), msg));
        return;
    }

    QJsonArray choices = root.value(QStringLiteral("choices")).toArray();
    if (!choices.isEmpty()) {
        QJsonObject first = choices.at(0).toObject();
        QJsonObject message = first.value(QStringLiteral("message")).toObject();
        QString answer = message.value(QStringLiteral("content")).toString();
        if (!answer.isEmpty()) {
            Logger::info(QStringLiteral("AgentAiService: %1 answered successfully (%2 chars)").arg(provider, QString::number(answer.length())));
            emit answerFinished(question, answer.trimmed(), provider, model);
            return;
        }
    }

    emit answerFailed(question, QStringLiteral("未能获取 %1 模型有效回复内容").arg(provider.toUpper()));
}
