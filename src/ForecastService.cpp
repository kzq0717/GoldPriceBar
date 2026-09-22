#include "ForecastService.h"
#include "AppSettings.h"
#include "ExtremeDatabase.h"
#include "HistoryCache.h"
#include "EventCalendar.h"
#include "SentimentService.h"
#include "TradingSession.h"
#include "ForecastTracker.h"
#include "Logger.h"
#include "goldsdk/forecast.hpp"
#include <vector>

#include <QUrl>
#include <QUrlQuery>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>

ForecastService& ForecastService::instance() {
    static ForecastService s_instance;
    return s_instance;
}

ForecastService::ForecastService(QObject* parent) : QObject(parent) {
    m_nam = new QNetworkAccessManager(this);
}

ForecastService::~ForecastService() {
    cancelPending();
}

void ForecastService::cancelPending() {
    if (m_pendingReply) {
        m_pendingReply->disconnect(this);
        m_pendingReply->abort();
        m_pendingReply->deleteLater();
        m_pendingReply.clear();
    }
}

void ForecastService::requestForecast(const QString& source, bool forceOnline) {
    const QString src = source.trimmed().isEmpty() ? AppSettings::instance().dataSource() : source.trimmed();
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();

    // 节流：同品种 8 秒内不重复请求，除非显式强制
    if (!forceOnline && m_lastRequestMs > 0 && (nowMs - m_lastRequestMs < 8000) && (src == m_currentSource)) {
        return;
    }
    m_lastRequestMs = nowMs;
    m_currentSource = src;

    const bool online = AppSettings::instance().forecastOnline();
    const QString apiKey = AppSettings::instance().xaiApiKey().trimmed();

    if (!online || apiKey.isEmpty()) {
        const QString tag = apiKey.isEmpty() ? tr("无Key·本地算法") : tr("本地算法");
        fallbackLocal(src, tag);
        return;
    }

    cancelPending();

    m_provider = AppSettings::instance().llmProvider();
    m_modelName = AppSettings::instance().xaiModel().trimmed();
    if (m_modelName.isEmpty()) {
        m_modelName = (m_provider == QStringLiteral("gemini"))
                          ? QStringLiteral("gemini-3.6-flash")
                          : QStringLiteral("grok-2-latest");
    }

    // 收集多维度上下文数据
    double actH = 0.0, actL = 0.0;
    QDateTime actHTime, actLTime;
    HistoryCache::instance().todayHighPoint(actHTime, actH);
    HistoryCache::instance().todayLowPoint(actLTime, actL);

    const auto ptsSamples = ExtremeDatabase::instance().loadIntradaySamples(QDate::currentDate(), src);
    double lastPrice = 0.0;
    if (!ptsSamples.isEmpty()) {
        lastPrice = ptsSamples.last().second;
    } else if (actH > 0.0) {
        lastPrice = (actH + actL) * 0.5;
    }

    if (actH <= 0.0) actH = lastPrice;
    if (actL <= 0.0) actL = lastPrice;

    // 最近 25 根样本序列
    QString seriesText;
    const int n = ptsSamples.size();
    const int take = qMin(25, n);
    for (int i = n - take; i < n; ++i) {
        const auto& pt = ptsSamples.at(i);
        seriesText += QStringLiteral("%1 %2\n")
                          .arg(pt.first.toString(QStringLiteral("HH:mm")))
                          .arg(pt.second, 0, 'f', 2);
    }

    const double atr10 = ExtremeDatabase::instance().computeAtr(10, src);
    const double prevClose = ExtremeDatabase::instance().previousClose(src);
    const QString sessionDesc = TradingSession::statusText(src, QDateTime::currentDateTime());
    const QString macroEvents = EventCalendar::summaryNear();
    const QString pendingAlert = EventCalendar::pendingAlertText();

    QString sentimentSummary;
    const auto sItems = SentimentService::instance().items();
    const int sTake = qMin(3, sItems.size());
    for (int i = 0; i < sTake; ++i) {
        sentimentSummary += QStringLiteral("• [%1] %2 (%3)\n")
                                .arg(sItems.at(i).published.toString(QStringLiteral("HH:mm")),
                                     sItems.at(i).title,
                                     sItems.at(i).bias);
    }
    if (sentimentSummary.isEmpty()) {
        sentimentSummary = QStringLiteral("暂无异动");
    }

    const QString nowStr = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm"));

    const QString systemPrompt = QStringLiteral(
        "你是资深黄金/贵金属量化与宏观分析师。\n"
        "核心原则：用户会提供「本地引擎已算好的结构化事实」（预测高低、高点时段概率、多日趋势）。\n"
        "本地趋势为默认立场；仅当宏观/舆情证据充分时方可提出不同偏向，并降低 confidence。\n"
        "禁止编造未给出的价格。多日偏空/强空时 action 不得鼓励追高。\n"
        "请综合分析以下多维度行情与基本面数据：\n"
        "1. 日内分时走势及已实现的今日最高价、最低价及其精准发生时间；\n"
        "2. 近10日真实波幅(ATR)及前日收盘价（全日波幅预算基准，严禁无根据极端漂移）；\n"
        "3. 宏观财经日历事件与预期发布时间（如初请失业金、非农、CPI、美联储决议、央行购金）；\n"
        "4. 当前全球交易时段（亚盘/欧盘/美盘）及最新市场资讯舆情偏向。\n\n"
        "任务目标：\n"
        "推测「今日剩余交易时段」全日的最高价与最低价，并精准预估最高点与最低点最可能出现的【时间窗口/时间节点】，以及核心催化事件与全日演变路径。\n\n"
        "输出规范：\n"
        "必须且仅输出单行合法 JSON 对象，严禁 Markdown 代码块、严禁包含思考过程与列表。字段定义：\n"
        "{\n"
        "  \"pred_high\": number, // 预期全日最高价。必须 >= 已出现今高；若判断高点在前期已成立，则等于今高\n"
        "  \"pred_low\": number,  // 预期全日最低价。必须 <= 已出现今低；若判断低点在前期已成立，则等于今低\n"
        "  \"pred_high_time_window\": string, // 预期高点时间窗口，例如 \"20:30-22:30(美盘数据期)\" 或 \"已于02:00确立\"\n"
        "  \"pred_low_time_window\": string,  // 预期低点时间窗口，例如 \"15:00-16:30(欧盘探底)\" 或 \"已于03:11确立\"\n"
        "  \"daily_path_scenario\": string,   // 20-35字全日路径预演，例如 \"亚盘窄幅盘整 -> 欧盘探底回升 -> 美盘借助初请数据冲高\"\n"
        "  \"key_catalyst\": string,          // 核心驱动催化剂，例如 \"美初请失业金数据与美债收益率走势\"\n"
        "  \"bias\": string,                  // \"偏多\" | \"偏空\" | \"震荡\"\n"
        "  \"brief\": string,                 // 35字内核心简评\n"
        "  \"confidence\": number             // 0.0 到 1.0 置信度\n"
        "}\n"
        "【时间规范 - 极高优先级】：\n"
        "1. 所有时间点与时间窗口必须且只能采用严格的 24 小时制（HH:mm 格式，例如 \"20:30-22:30(美盘数据期)\"、\"15:00-16:30(欧盘探底)\"、\"已于02:15确立\"）；\n"
        "2. 严禁使用 12 小时制、严禁出现 AM / PM、严禁出现「下午 2:00」「晚上 8:30」等非 24 小时制字眼；\n"
        "3. 小时数必须为两位数（如 09:00 严禁写为 9:00）。\n"
        "约束：预期全日总振幅（pred_high - pred_low）建议紧密锚定 10日ATR（约0.7~1.5倍 ATR）。非投资建议。");

    // 本地引擎事实（硬约束，注入 LLM）
    QString localEngineBlock = QStringLiteral("（本地引擎暂无有效结果）");
    {
        std::vector<goldsdk::IntradayPoint> pts;
        pts.reserve(static_cast<size_t>(ptsSamples.size()));
        for (const auto& pt : ptsSamples) {
            goldsdk::IntradayPoint ip;
            ip.epochMs = pt.first.toMSecsSinceEpoch();
            ip.price = pt.second;
            pts.push_back(ip);
        }
        const QTime nowT = QTime::currentTime();
        double dayFrac = 0.5;
        if (src == QStringLiteral("gj") || src == QStringLiteral("xau"))
            dayFrac = nowT.msecsSinceStartOfDay() / (24.0 * 3600.0 * 1000.0);
        else {
            const int startM = 9 * 60, endM = 23 * 60 + 30;
            const int nowM = nowT.hour() * 60 + nowT.minute();
            if (nowM <= startM) dayFrac = 0.05;
            else if (nowM >= endM) dayFrac = 0.95;
            else dayFrac = static_cast<double>(nowM - startM) / static_cast<double>(endM - startM);
        }
        const auto fr = goldsdk::ForecastEngine::dayRange(pts, actH, actL, dayFrac, atr10, prevClose);
        QString multi = QStringLiteral("样本不足");
        {
            std::vector<double> closes;
            const QDate to = QDate::currentDate();
            const auto rows = ExtremeDatabase::instance().loadDailyClosesRange(to.addDays(-90), to, src);
            for (const auto& r : rows)
                if (r.second > 0.0) closes.push_back(r.second);
            const auto trn = goldsdk::ForecastEngine::multiDayTrend(closes);
            if (trn.valid)
                multi = QStringLiteral("%1(score=%2,RSI=%3)")
                            .arg(QString::fromUtf8(trn.label()))
                            .arg(trn.score, 0, 'f', 2)
                            .arg(trn.rsi14, 0, 'f', 1);
        }
        if (fr.valid) {
            localEngineBlock = QStringLiteral(
                "本地预测高=%1 本地预测低=%2\n"
                "高点时段=%3 (概率约%4%)\n"
                "低点时段=%5\n"
                "高点已现概率约%6% 剩余上行约%7\n"
                "情景=%8\n"
                "多日趋势=%9\n"
                "请在本地预测基础上微调，勿大幅偏离。")
                .arg(fr.predHigh, 0, 'f', 2)
                .arg(fr.predLow, 0, 'f', 2)
                .arg(QString::fromStdString(fr.predHighTimeWindow))
                .arg(fr.peakWindowProb * 100.0, 0, 'f', 0)
                .arg(QString::fromStdString(fr.predLowTimeWindow))
                .arg(fr.highAlreadyInProb * 100.0, 0, 'f', 0)
                .arg(fr.remainingUpside, 0, 'f', 2)
                .arg(QString::fromStdString(fr.scenario), multi);
        }
    }

    const QString userPrompt = QStringLiteral(
        "时间(本地): %1\n"
        "品种代码: %2（zs/ms=积存金元/克，gj=伦敦金美元/盎司）\n"
        "现价: %3\n"
        "已出现今高: %4 (发生时间: %5)\n"
        "已出现今低: %6 (发生时间: %7)\n"
        "前日收盘价: %8\n"
        "近10日ATR(真实波幅均值): %9\n"
        "当前交易时段: %10\n"
        "今日宏观事件: %11%12\n"
        "最新要闻舆情:\n%13\n"
        "【本地引擎结构化事实】\n%14\n"
        "最近分时采样:\n%15\n"
        "请按要求输出 JSON。")
        .arg(nowStr, src)
        .arg(lastPrice, 0, 'f', 2)
        .arg(actH, 0, 'f', 2)
        .arg(actHTime.isValid() ? actHTime.toString(QStringLiteral("HH:mm:ss")) : QStringLiteral("开盘"))
        .arg(actL, 0, 'f', 2)
        .arg(actLTime.isValid() ? actLTime.toString(QStringLiteral("HH:mm:ss")) : QStringLiteral("开盘"))
        .arg(prevClose > 0 ? QString::number(prevClose, 'f', 2) : QStringLiteral("未知"))
        .arg(atr10 > 0 ? QString::number(atr10, 'f', 2) : QStringLiteral("未知"))
        .arg(sessionDesc)
        .arg(macroEvents, pendingAlert.isEmpty() ? QString() : (QStringLiteral(" [预警: ") + pendingAlert + QStringLiteral("]")))
        .arg(sentimentSummary)
        .arg(localEngineBlock)
        .arg(seriesText);

    QNetworkRequest request;
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("GoldPriceBarLite/1.0"));
    request.setTransferTimeout(50000);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);

    if (m_provider == QStringLiteral("gemini")) {
        QString model = m_modelName;
        if (model.startsWith(QStringLiteral("models/"))) {
            model = model.mid(7);
        }
        const QUrl url(QStringLiteral("https://generativelanguage.googleapis.com/v1beta/models/%1:generateContent?key=%2")
                           .arg(model, QString::fromUtf8(QUrl::toPercentEncoding(apiKey))));
        request.setUrl(url);
        request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));

        QJsonObject body;
        QJsonArray contents;
        QJsonObject userMsg;
        userMsg.insert(QStringLiteral("role"), QStringLiteral("user"));
        QJsonArray parts;
        parts.append(QJsonObject{{QStringLiteral("text"), systemPrompt + QStringLiteral("\n\n") + userPrompt}});
        userMsg.insert(QStringLiteral("parts"), parts);
        contents.append(userMsg);
        body.insert(QStringLiteral("contents"), contents);

        QJsonObject genCfg;
        genCfg.insert(QStringLiteral("maxOutputTokens"), 4096);
        genCfg.insert(QStringLiteral("responseMimeType"), QStringLiteral("application/json"));

        QJsonObject schema;
        schema.insert(QStringLiteral("type"), QStringLiteral("object"));
        QJsonObject props;
        props.insert(QStringLiteral("pred_high"), QJsonObject{{QStringLiteral("type"), QStringLiteral("number")}});
        props.insert(QStringLiteral("pred_low"), QJsonObject{{QStringLiteral("type"), QStringLiteral("number")}});
        props.insert(QStringLiteral("pred_high_time_window"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}});
        props.insert(QStringLiteral("pred_low_time_window"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}});
        props.insert(QStringLiteral("daily_path_scenario"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}});
        props.insert(QStringLiteral("key_catalyst"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}});
        props.insert(QStringLiteral("bias"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}});
        props.insert(QStringLiteral("brief"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}});
        props.insert(QStringLiteral("confidence"), QJsonObject{{QStringLiteral("type"), QStringLiteral("number")}});
        schema.insert(QStringLiteral("properties"), props);
        QJsonArray req;
        req.append(QStringLiteral("pred_high"));
        req.append(QStringLiteral("pred_low"));
        req.append(QStringLiteral("pred_high_time_window"));
        req.append(QStringLiteral("pred_low_time_window"));
        schema.insert(QStringLiteral("required"), req);
        genCfg.insert(QStringLiteral("responseSchema"), schema);

        QJsonObject thinking;
        thinking.insert(QStringLiteral("thinkingLevel"), QStringLiteral("MINIMAL"));
        genCfg.insert(QStringLiteral("thinkingConfig"), thinking);

        body.insert(QStringLiteral("generationConfig"), genCfg);

        Logger::info(QStringLiteral("ForecastService: sending Gemini request model=%1").arg(model));
        m_pendingReply = m_nam->post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    } else {
        request.setUrl(QUrl(QStringLiteral("https://api.x.ai/v1/chat/completions")));
        request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
        request.setRawHeader("Authorization", QByteArray("Bearer ") + apiKey.toUtf8());

        QJsonObject body;
        body.insert(QStringLiteral("model"), m_modelName);
        body.insert(QStringLiteral("temperature"), 0.3);
        body.insert(QStringLiteral("max_tokens"), 1024);
        QJsonArray messages;
        messages.append(QJsonObject{{QStringLiteral("role"), QStringLiteral("system")}, {QStringLiteral("content"), systemPrompt}});
        messages.append(QJsonObject{{QStringLiteral("role"), QStringLiteral("user")}, {QStringLiteral("content"), userPrompt}});
        body.insert(QStringLiteral("messages"), messages);

        Logger::info(QStringLiteral("ForecastService: sending Grok request model=%1").arg(m_modelName));
        m_pendingReply = m_nam->post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    }

    if (m_pendingReply) {
        connect(m_pendingReply, &QNetworkReply::finished, this, &ForecastService::onReplyFinished);
    } else {
        Logger::warn(QStringLiteral("ForecastService: post returned null"));
        fallbackLocal(src, tr("在线失败·本地算法"));
    }
}

QString ForecastService::normalizeTo24HourTime(const QString& raw) {
    if (raw.trimmed().isEmpty()) return raw;
    QString s = raw.trimmed();

    auto replaceMatch = [](const QString& str, const QRegularExpression& re, auto func) -> QString {
        QString out;
        int lastPos = 0;
        auto it = re.globalMatch(str);
        while (it.hasNext()) {
            auto m = it.next();
            out += str.mid(lastPos, m.capturedStart() - lastPos);
            out += func(m);
            lastPos = m.capturedEnd();
        }
        out += str.mid(lastPos);
        return out;
    };

    // 1. 中文带上下午、晚上、夜间等前缀的清洗
    static const QRegularExpression rePmZh(QStringLiteral("(?:下午|晚上|夜间)\\s*(\\d{1,2})(?:[:点](\\d{1,2}))?(?:分)?"));
    s = replaceMatch(s, rePmZh, [](const QRegularExpressionMatch& m) -> QString {
        int h = m.captured(1).toInt();
        int min = m.captured(2).isEmpty() ? 0 : m.captured(2).toInt();
        if (h < 12) h += 12;
        return QString::asprintf("%02d:%02d", h, min);
    });

    static const QRegularExpression reAmZh(QStringLiteral("(?:上午|早晨|凌晨)\\s*(\\d{1,2})(?:[:点](\\d{1,2}))?(?:分)?"));
    s = replaceMatch(s, reAmZh, [](const QRegularExpressionMatch& m) -> QString {
        int h = m.captured(1).toInt();
        int min = m.captured(2).isEmpty() ? 0 : m.captured(2).toInt();
        if (h == 12) h = 0;
        return QString::asprintf("%02d:%02d", h, min);
    });

    // 2. 英文 AM / PM 后缀清洗
    static const QRegularExpression reEnPm(QStringLiteral("(\\d{1,2})(?::(\\d{2}))?\\s*(?:PM|pm)\\b"));
    s = replaceMatch(s, reEnPm, [](const QRegularExpressionMatch& m) -> QString {
        int h = m.captured(1).toInt();
        int min = m.captured(2).isEmpty() ? 0 : m.captured(2).toInt();
        if (h < 12) h += 12;
        return QString::asprintf("%02d:%02d", h, min);
    });

    static const QRegularExpression reEnAm(QStringLiteral("(\\d{1,2})(?::(\\d{2}))?\\s*(?:AM|am)\\b"));
    s = replaceMatch(s, reEnAm, [](const QRegularExpressionMatch& m) -> QString {
        int h = m.captured(1).toInt();
        int min = m.captured(2).isEmpty() ? 0 : m.captured(2).toInt();
        if (h == 12) h = 0;
        return QString::asprintf("%02d:%02d", h, min);
    });

    // 3. 转换中文 "20点30分" 或 "20点" 为 "20:30" / "20:00"
    static const QRegularExpression rePointMin(QStringLiteral("(\\d{1,2})点(\\d{1,2})分?"));
    s = replaceMatch(s, rePointMin, [](const QRegularExpressionMatch& m) -> QString {
        int h = m.captured(1).toInt();
        int min = m.captured(2).toInt();
        return QString::asprintf("%02d:%02d", h, min);
    });
    static const QRegularExpression rePointOnly(QStringLiteral("(\\d{1,2})点\\b"));
    s = replaceMatch(s, rePointOnly, [](const QRegularExpressionMatch& m) -> QString {
        int h = m.captured(1).toInt();
        return QString::asprintf("%02d:00", h);
    });

    // 4. 补齐单数小时：如 "9:30" 转换为 "09:30"
    static const QRegularExpression reSingleHour(QStringLiteral("(?<!\\d)(\\d):([0-5]\\d)"));
    s.replace(reSingleHour, QStringLiteral("0\\1:\\2"));

    return s;
}

void ForecastService::onReplyFinished() {
    QNetworkReply* reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;

    if (m_pendingReply.data() == reply) {
        m_pendingReply.clear();
    }

    const auto err = reply->error();
    QByteArray raw;
    if (reply->isOpen()) {
        raw = reply->readAll();
    }

    reply->disconnect(this);
    reply->deleteLater();

    if (err == QNetworkReply::OperationCanceledError) {
        Logger::info(QStringLiteral("ForecastService: request canceled"));
        return;
    }

    if (err != QNetworkReply::NoError) {
        Logger::warn(QStringLiteral("ForecastService HTTP error: %1 body=%2")
                         .arg(reply->errorString(), QString::fromUtf8(raw.left(400))));
        fallbackLocal(m_currentSource, tr("在线失败·本地算法"));
        return;
    }

    parseLlmResponse(raw);
}

void ForecastService::parseLlmResponse(const QByteArray& raw) {
    QJsonParseError pe{};
    const QJsonDocument doc = QJsonDocument::fromJson(raw, &pe);
    if (pe.error != QJsonParseError::NoError || !doc.isObject()) {
        Logger::warn(QStringLiteral("ForecastService: failed to parse HTTP JSON response"));
        fallbackLocal(m_currentSource, tr("解析失败·本地算法"));
        return;
    }

    const QJsonObject root = doc.object();
    QString content;

    if (root.contains(QStringLiteral("candidates"))) {
        const QJsonArray cands = root.value(QStringLiteral("candidates")).toArray();
        if (!cands.isEmpty()) {
            const QJsonObject c0 = cands.at(0).toObject();
            const QJsonObject contentObj = c0.value(QStringLiteral("content")).toObject();
            const QJsonArray parts = contentObj.value(QStringLiteral("parts")).toArray();
            for (const QJsonValue& pv : parts) {
                const QString tx = pv.toObject().value(QStringLiteral("text")).toString();
                if (!tx.isEmpty()) {
                    if (!content.isEmpty()) content += QLatin1Char('\n');
                    content += tx;
                }
            }
        }
    } else if (root.contains(QStringLiteral("choices"))) {
        content = root.value(QStringLiteral("choices")).toArray().at(0).toObject()
                      .value(QStringLiteral("message")).toObject()
                      .value(QStringLiteral("content")).toString();
    }

    content = content.trimmed();
    if (content.startsWith(QStringLiteral("```"))) {
        const int nl = content.indexOf(QLatin1Char('\n'));
        if (nl > 0) content = content.mid(nl + 1);
        if (content.endsWith(QStringLiteral("```"))) content.chop(3);
        content = content.trimmed();
    }
    {
        const int a = content.indexOf(QLatin1Char('{'));
        const int b = content.lastIndexOf(QLatin1Char('}'));
        if (a >= 0 && b > a) {
            content = content.mid(a, b - a + 1).trimmed();
        }
    }

    QJsonParseError pe2{};
    const QJsonDocument jdoc = QJsonDocument::fromJson(content.toUtf8(), &pe2);
    if (pe2.error != QJsonParseError::NoError || !jdoc.isObject()) {
        Logger::warn(QStringLiteral("ForecastService: LLM text not valid JSON: %1").arg(content.left(300)));
        fallbackLocal(m_currentSource, tr("JSON无效·本地算法"));
        return;
    }

    const QJsonObject jo = jdoc.object();
    auto num = [&](std::initializer_list<const char*> keys) -> double {
        for (const char* k : keys) {
            if (jo.contains(QLatin1String(k))) {
                const QJsonValue v = jo.value(QLatin1String(k));
                if (v.isDouble() || v.isString()) {
                    bool ok = false;
                    const double d = v.toVariant().toDouble(&ok);
                    if (ok && d > 0.0) return d;
                }
            }
        }
        return 0.0;
    };

    double predHigh = num({"pred_high", "predHigh", "high", "max"});
    double predLow  = num({"pred_low", "predLow", "low", "min"});
    QString highTime = normalizeTo24HourTime(jo.value(QStringLiteral("pred_high_time_window")).toString());
    if (highTime.isEmpty()) highTime = normalizeTo24HourTime(jo.value(QStringLiteral("high_time")).toString());
    QString lowTime = normalizeTo24HourTime(jo.value(QStringLiteral("pred_low_time_window")).toString());
    if (lowTime.isEmpty()) lowTime = normalizeTo24HourTime(jo.value(QStringLiteral("low_time")).toString());
    QString scenario = jo.value(QStringLiteral("daily_path_scenario")).toString();
    if (scenario.isEmpty()) scenario = jo.value(QStringLiteral("scenario")).toString();
    QString catalyst = jo.value(QStringLiteral("key_catalyst")).toString();
    if (catalyst.isEmpty()) catalyst = jo.value(QStringLiteral("catalyst")).toString();
    QString bias = jo.value(QStringLiteral("bias")).toString();
    QString brief = jo.value(QStringLiteral("brief")).toString();
    if (brief.isEmpty()) brief = jo.value(QStringLiteral("reason")).toString();
    double conf = jo.value(QStringLiteral("confidence")).toDouble(0.8);

    if (predHigh <= 0.0 || predLow <= 0.0 || predHigh < predLow) {
        Logger::warn(QStringLiteral("ForecastService: invalid high/low values high=%1 low=%2").arg(predHigh).arg(predLow));
        fallbackLocal(m_currentSource, tr("数值异常·本地算法"));
        return;
    }

    // 严谨校验与锚定：全日预期最高价不得低于已出现今高，最低价不得高于已出现今低
    double actH = 0.0, actL = 0.0;
    HistoryCache::instance().todayHigh(actH);
    HistoryCache::instance().todayLow(actL);
    if (actH > 0.0) predHigh = qMax(predHigh, actH);
    if (actL > 0.0) predLow  = qMin(predLow, actL);

    m_lastResult.valid = true;
    m_lastResult.predHigh = predHigh;
    m_lastResult.predLow = predLow;
    m_lastResult.predHighTimeWindow = highTime.isEmpty() ? QStringLiteral("20:30 - 22:30 (美盘主浪)") : highTime;
    m_lastResult.predLowTimeWindow  = lowTime.isEmpty()  ? QStringLiteral("15:00 - 16:30 (欧盘探底)") : lowTime;
    m_lastResult.scenario = scenario;
    m_lastResult.keyCatalyst = catalyst;
    m_lastResult.bias = bias.isEmpty() ? tr("震荡") : bias;
    m_lastResult.brief = brief.isEmpty() ? scenario : brief;
    m_lastResult.confidence = conf;
    // LLM 路径补充本地概率/多日（若可算）
    {
        std::vector<double> closes;
        const QDate to = QDate::currentDate();
        const auto rows = ExtremeDatabase::instance().loadDailyClosesRange(
            to.addDays(-90), to, m_currentSource);
        for (const auto& r : rows)
            if (r.second > 0.0) closes.push_back(r.second);
        const auto trend = goldsdk::ForecastEngine::multiDayTrend(closes);
        if (trend.valid)
            m_lastResult.multiDayBias = QString::fromUtf8(trend.label());
        else
            m_lastResult.multiDayBias.clear();
        // 概率类仍以本地引擎为准（若有上次本地结果则保留；否则保持 0）
        if (m_lastResult.peakWindowProb <= 0.0) {
            // 轻量再算一次本地概率供卡片展示
            const auto ptsSamples = ExtremeDatabase::instance().loadIntradaySamples(
                QDate::currentDate(), m_currentSource);
            std::vector<goldsdk::IntradayPoint> pts;
            for (const auto& pt : ptsSamples) {
                goldsdk::IntradayPoint ip;
                ip.epochMs = pt.first.toMSecsSinceEpoch();
                ip.price = pt.second;
                pts.push_back(ip);
            }
            const QTime nowT = QTime::currentTime();
            double dayFrac = nowT.msecsSinceStartOfDay() / (24.0 * 3600.0 * 1000.0);
            const double atr = ExtremeDatabase::instance().computeAtr(10, m_currentSource);
            const double prev = ExtremeDatabase::instance().previousClose(m_currentSource);
            const auto fr = goldsdk::ForecastEngine::dayRange(pts, actH, actL, dayFrac, atr, prev);
            if (fr.valid) {
                m_lastResult.highAlreadyInProb = fr.highAlreadyInProb;
                m_lastResult.remainingUpside = fr.remainingUpside;
                m_lastResult.peakWindowProb = fr.peakWindowProb;
            }
        }
    }
    m_lastResult.modeTag = (m_provider == QStringLiteral("gemini") ? tr("Gemini大模型") : tr("Grok大模型"));
    m_lastResult.timestamp = QDateTime::currentDateTime();

    // 存库与统计
    ExtremeDatabase::instance().insertForecastLog(
        m_lastResult.timestamp, m_currentSource, m_lastResult.modeTag,
        m_lastResult.predHigh, m_lastResult.predLow,
        actH > 0 ? (actH + actL) * 0.5 : predHigh,
        m_lastResult.brief,
        m_lastResult.predHighTimeWindow,
        m_lastResult.predLowTimeWindow,
        m_lastResult.keyCatalyst);

    ForecastTracker::instance().recordDayRange(
        m_lastResult.timestamp, 3600, m_lastResult.predHigh, m_lastResult.predLow, m_lastResult.modeTag);

    Logger::info(QStringLiteral("ForecastService: LLM forecast success! high=%1 (%2) low=%3 (%4) catalyst=%5")
                     .arg(predHigh, 0, 'f', 2).arg(m_lastResult.predHighTimeWindow)
                     .arg(predLow, 0, 'f', 2).arg(m_lastResult.predLowTimeWindow)
                     .arg(catalyst));

    emit forecastUpdated(m_lastResult);
}

void ForecastService::fallbackLocal(const QString& source, const QString& tag) {
    const QString src = source.isEmpty() ? AppSettings::instance().dataSource() : source;
    double actH = 0.0, actL = 0.0;
    QDateTime hTime, lTime;
    HistoryCache::instance().todayHighPoint(hTime, actH);
    HistoryCache::instance().todayLowPoint(lTime, actL);

    const auto ptsSamples = ExtremeDatabase::instance().loadIntradaySamples(QDate::currentDate(), src);
    std::vector<goldsdk::IntradayPoint> pts;
    pts.reserve(ptsSamples.size());
    for (const auto& p : ptsSamples) {
        pts.push_back({p.first.toMSecsSinceEpoch(), p.second});
    }

    double curPrice = 0.0;
    if (!pts.empty()) {
        curPrice = pts.back().price;
    } else if (actH > 0.0) {
        curPrice = (actH + actL) * 0.5;
    }
    if (actH <= 0.0) actH = curPrice;
    if (actL <= 0.0) actL = curPrice;

    const QTime nowT = QTime::currentTime();
    double dayFrac = 0.5;
    if (src == QStringLiteral("gj") || src == QStringLiteral("xau")) {
        dayFrac = nowT.msecsSinceStartOfDay() / (24.0 * 3600.0 * 1000.0);
    } else {
        const int startM = 9 * 60;
        const int endM = 23 * 60 + 30;
        const int nowM = nowT.hour() * 60 + nowT.minute();
        if (nowM <= startM) dayFrac = 0.05;
        else if (nowM >= endM) dayFrac = 0.95;
        else dayFrac = static_cast<double>(nowM - startM) / static_cast<double>(endM - startM);
    }

    const double atr = ExtremeDatabase::instance().computeAtr(10, src);
    const double prevClose = ExtremeDatabase::instance().previousClose(src);

    const auto fr = goldsdk::ForecastEngine::dayRange(pts, actH, actL, dayFrac, atr, prevClose);
    if (!fr.valid) {
        Logger::warn(QStringLiteral("ForecastService: local forecastEngine returned invalid"));
        emit forecastFailed(tr("分时数据不足"));
        return;
    }

    m_lastResult.valid = true;
    m_lastResult.predHigh = fr.predHigh;
    m_lastResult.predLow = fr.predLow;
    m_lastResult.predHighTimeWindow = normalizeTo24HourTime(QString::fromStdString(fr.predHighTimeWindow));
    m_lastResult.predLowTimeWindow = normalizeTo24HourTime(QString::fromStdString(fr.predLowTimeWindow));
    m_lastResult.scenario = QString::fromStdString(fr.scenario);
    m_lastResult.keyCatalyst = QString::fromStdString(fr.keyCatalyst);

    // 多日趋势过滤（利于正收益：非多头时不强调冲高）
    QString multiBias = tr("震荡");
    bool allowLong = true;
    {
        std::vector<double> closes;
        const QDate to = QDate::currentDate();
        const auto rows = ExtremeDatabase::instance().loadDailyClosesRange(
            to.addDays(-90), to, src);
        closes.reserve(static_cast<size_t>(rows.size()));
        for (const auto& r : rows) {
            if (r.second > 0.0)
                closes.push_back(r.second);
        }
        const auto trend = goldsdk::ForecastEngine::multiDayTrend(closes);
        if (trend.valid) {
            multiBias = QString::fromUtf8(trend.label());
            allowLong = trend.allowLongBias();
            if (!allowLong) {
                m_lastResult.scenario = tr("多日%1·不追高，以观望或保护利润为主；%2")
                                            .arg(multiBias, m_lastResult.scenario);
            } else {
                m_lastResult.scenario = tr("多日%1·可顺势关注高点窗口；%2")
                                            .arg(multiBias, m_lastResult.scenario);
            }
        }
    }

    m_lastResult.bias = allowLong
                            ? ((fr.predHigh - actH > actL - fr.predLow) ? tr("偏多") : tr("震荡"))
                            : tr("偏空");
    m_lastResult.brief = m_lastResult.scenario;
    m_lastResult.confidence = fr.confidence > 0.0 ? fr.confidence : 0.65;
    m_lastResult.highAlreadyInProb = fr.highAlreadyInProb;
    m_lastResult.remainingUpside = fr.remainingUpside;
    m_lastResult.peakWindowProb = fr.peakWindowProb;
    m_lastResult.multiDayBias = multiBias;
    m_lastResult.modeTag = tag;
    m_lastResult.timestamp = QDateTime::currentDateTime();

    ExtremeDatabase::instance().insertForecastLog(
        m_lastResult.timestamp, src, m_lastResult.modeTag,
        m_lastResult.predHigh, m_lastResult.predLow,
        curPrice,
        m_lastResult.brief,
        m_lastResult.predHighTimeWindow,
        m_lastResult.predLowTimeWindow,
        m_lastResult.keyCatalyst);

    ForecastTracker::instance().recordDayRange(
        m_lastResult.timestamp, 3600, m_lastResult.predHigh, m_lastResult.predLow, m_lastResult.modeTag);

    Logger::info(QStringLiteral("ForecastService: local forecast ready high=%1 (%2) low=%3 (%4)")
                     .arg(fr.predHigh, 0, 'f', 2).arg(m_lastResult.predHighTimeWindow)
                     .arg(fr.predLow, 0, 'f', 2).arg(m_lastResult.predLowTimeWindow));

    emit forecastUpdated(m_lastResult);
}
