#include "goldsdk/forecast.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <numeric>
#include <string>
#include <vector>

namespace goldsdk {

const char* MultiDayTrend::biasLabel(TrendBias b)
{
    switch (b) {
    case TrendBias::StrongBull: return "强多";
    case TrendBias::MildBull:   return "偏多";
    case TrendBias::MildBear:   return "偏空";
    case TrendBias::StrongBear: return "强空";
    default:                    return "震荡";
    }
}

namespace {

int minuteOfDayFromEpochMs(std::int64_t epochMs)
{
    // epochMs 是 UTC 毫秒时间戳。中国标准时间 (CST) 为 UTC+8。
    const std::int64_t cstOffsetMs = 8LL * 3600 * 1000;
    const std::int64_t dayMs = 24LL * 3600 * 1000;
    std::int64_t mod = (epochMs + cstOffsetMs) % dayMs;
    if (mod < 0)
        mod += dayMs;
    return static_cast<int>(mod / 60000);
}

std::string formatWindow(int startMin, int endMin, const char* tag)
{
    const int sh = (startMin / 60) % 24, sm = startMin % 60;
    int eh = endMin / 60;
    int em = endMin % 60;
    if (eh == 24 && em == 0) {
        // 保持 24:00 明确表示至全日收盘
    } else {
        eh = eh % 24;
    }
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%02d:%02d - %02d:%02d (%s)", sh, sm, eh, em, tag);
    return std::string(buf);
}

inline std::string fmtPrice(double v)
{
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.2f", v);
    return std::string(buf);
}

std::vector<double> priorPeakWeights()
{
    std::vector<double> w(48, 0.5);
    // 48个半小时桶（按北京时间 CST 分布）：
    // 00:00 - 02:30 (桶 0..4): 美盘后半程
    for (int i = 0; i < 5; ++i) w[static_cast<size_t>(i)] = 1.0;
    // 02:30 - 09:00 (桶 5..17): 隔夜清淡与休市
    for (int i = 5; i < 18; ++i) w[static_cast<size_t>(i)] = 0.35;
    // 09:00 - 10:30 (桶 18..20): 国内盘开盘定价期（脉冲极值高发期）
    for (int i = 18; i < 21; ++i) w[static_cast<size_t>(i)] = 1.8;
    // 10:30 - 14:00 (桶 21..27): 亚盘午间收敛
    for (int i = 21; i < 28; ++i) w[static_cast<size_t>(i)] = 0.55;
    // 14:00 - 15:30 (桶 28..30): 亚盘尾盘与欧盘初动
    for (int i = 28; i < 31; ++i) w[static_cast<size_t>(i)] = 0.9;
    // 15:30 - 19:30 (桶 31..38): 欧盘主交易段（国内积存金日间休市）
    for (int i = 31; i < 39; ++i) w[static_cast<size_t>(i)] = 1.2;
    // 19:30 - 20:30 (桶 39..40): 国内夜盘开盘前夕与初开
    for (int i = 39; i < 41; ++i) w[static_cast<size_t>(i)] = 1.4;
    // 20:30 - 23:00 (桶 41..45): 美盘核心博弈与重磅数据发布（全球最大波动）
    for (int i = 41; i < 46; ++i) w[static_cast<size_t>(i)] = 2.2;
    // 23:00 - 24:00 (桶 46..47): 美盘主浪延续
    for (int i = 46; i < 48; ++i) w[static_cast<size_t>(i)] = 1.3;

    double s = std::accumulate(w.begin(), w.end(), 0.0);
    if (s > 0) {
        for (double& v : w) v /= s;
    }
    return w;
}

std::vector<double> blendPeakProbs(const std::vector<int>& histCounts)
{
    auto prior = priorPeakWeights();
    if (histCounts.size() != 48)
        return prior;
    int N = 0;
    for (int c : histCounts) N += std::max(0, c);
    const double alpha = std::min(1.0, N / 60.0);
    std::vector<double> hist(48, 0.0);
    if (N > 0) {
        for (int i = 0; i < 48; ++i)
            hist[static_cast<size_t>(i)] =
                static_cast<double>(std::max(0, histCounts[static_cast<size_t>(i)])) / N;
    } else {
        hist = prior;
    }
    std::vector<double> out(48);
    for (int i = 0; i < 48; ++i)
        out[static_cast<size_t>(i)] =
            alpha * hist[static_cast<size_t>(i)] + (1.0 - alpha) * prior[static_cast<size_t>(i)];
    double s = std::accumulate(out.begin(), out.end(), 0.0);
    if (s > 0) {
        for (double& v : out) v /= s;
    }
    return out;
}

void redistributePastBuckets(std::vector<double>& p, int currentBucket, bool highInPast)
{
    if (currentBucket <= 0 || currentBucket >= 48)
        return;
    double mass = 0.0;
    for (int i = 0; i < currentBucket; ++i) {
        if (highInPast) {
            mass += p[static_cast<size_t>(i)] * 0.85;
            p[static_cast<size_t>(i)] *= 0.15;
        } else {
            mass += p[static_cast<size_t>(i)];
            p[static_cast<size_t>(i)] = 0.0;
        }
    }
    double remain = 0.0;
    for (int i = currentBucket; i < 48; ++i)
        remain += p[static_cast<size_t>(i)];
    if (remain > 1e-12 && mass > 0) {
        for (int i = currentBucket; i < 48; ++i)
            p[static_cast<size_t>(i)] += mass * (p[static_cast<size_t>(i)] / remain);
    } else if (mass > 0) {
        p[47] += mass;
    }
    double s = std::accumulate(p.begin(), p.end(), 0.0);
    if (s > 0) {
        for (double& v : p) v /= s;
    }
}

double rsiWilder(const std::vector<double>& closes, int period)
{
    if (static_cast<int>(closes.size()) < period + 1)
        return 50.0;
    double gain = 0, loss = 0;
    const int n = static_cast<int>(closes.size());
    for (int i = n - period; i < n; ++i) {
        const double d = closes[static_cast<size_t>(i)] - closes[static_cast<size_t>(i - 1)];
        if (d >= 0) gain += d;
        else loss -= d;
    }
    if (loss < 1e-12)
        return 100.0;
    const double rs = gain / loss;
    return 100.0 - (100.0 / (1.0 + rs));
}

} // namespace

DayRangeForecast ForecastEngine::dayRange(const std::vector<IntradayPoint>& points,
                                          double actHigh,
                                          double actLow,
                                          double dayFraction,
                                          double atr,
                                          double prevClose,
                                          const std::vector<int>& historicalPeakBucketCounts)
{
    DayRangeForecast out;
    if (points.size() < 3)
        return out;

    double lastPrice = points.back().price;
    if (lastPrice <= 0.0)
        return out;

    if (actHigh <= 0.0 || actLow <= 0.0) {
        actHigh = actLow = points.front().price;
        for (const auto& p : points) {
            actHigh = std::max(actHigh, p.price);
            actLow = std::min(actLow, p.price);
        }
    }

    const int n = static_cast<int>(points.size());
    const int w = std::min(60, n);

    double mean = 0.0;
    for (int i = n - w; i < n; ++i)
        mean += points[static_cast<size_t>(i)].price;
    mean /= static_cast<double>(w);
    double var = 0.0;
    for (int i = n - w; i < n; ++i) {
        const double d = points[static_cast<size_t>(i)].price - mean;
        var += d * d;
    }
    const double stdev = std::sqrt(var / static_cast<double>(std::max(1, w - 1)));
    const double rangeSoFar = std::max(0.01, actHigh - actLow);

    dayFraction = std::clamp(dayFraction, 0.0, 1.0);
    const double remain = std::clamp(1.0 - dayFraction, 0.05, 1.0);
    const double timeScale = std::sqrt(remain);

    const double pctFloor = lastPrice * 0.005;
    const double pctCeil = lastPrice * 0.025;
    double expectedFullRange = 0.0;
    if (atr > 0.0) {
        expectedFullRange = std::clamp(atr * (0.85 + 0.35 * (1.0 - timeScale)), pctFloor, pctCeil);
    } else {
        expectedFullRange = std::max({rangeSoFar * 1.25, stdev * 4.2, pctFloor});
        expectedFullRange = std::min(expectedFullRange, pctCeil);
    }

    double remainingBudget = std::max(0.0, expectedFullRange - rangeSoFar);
    remainingBudget *= (0.35 + 0.65 * timeScale);

    double up = remainingBudget * 0.5;
    double down = remainingBudget * 0.5;
    if (actHigh > actLow) {
        const double pos = (lastPrice - actLow) / (actHigh - actLow);
        if (pos > 0.72) {
            up *= 0.75;
            down *= 1.15;
        } else if (pos < 0.28) {
            up *= 1.15;
            down *= 0.75;
        }
    }
    if (prevClose > 0.0) {
        const double gap = (lastPrice - prevClose) / prevClose;
        if (gap > 0.004) {
            up *= 1.08;
            down *= 0.95;
        } else if (gap < -0.004) {
            up *= 0.95;
            down *= 1.08;
        }
    }

    // 峰值与谷值特征分析（按 CST 时间桶）
    int peakMin = minuteOfDayFromEpochMs(points.front().epochMs);
    double peakPx = points.front().price;
    int peakIndex = 0;

    int troughMin = minuteOfDayFromEpochMs(points.front().epochMs);
    double troughPx = points.front().price;
    int troughIndex = 0;

    for (size_t i = 0; i < points.size(); ++i) {
        if (points[i].price >= peakPx) {
            peakPx = points[i].price;
            peakMin = minuteOfDayFromEpochMs(points[i].epochMs);
            peakIndex = static_cast<int>(i);
        }
        if (points[i].price <= troughPx) {
            troughPx = points[i].price;
            troughMin = minuteOfDayFromEpochMs(points[i].epochMs);
            troughIndex = static_cast<int>(i);
        }
    }

    const int curMin = minuteOfDayFromEpochMs(points.back().epochMs);
    const int curBucket = std::clamp(curMin / 30, 0, 47);
    const int peakBucket = std::clamp(peakMin / 30, 0, 47);
    const int troughBucket = std::clamp(troughMin / 30, 0, 47);
    const bool highInPast = peakBucket < curBucket;
    const bool lowInPast = troughBucket < curBucket;

    // 峰值衰减特征（多长时间未破高？从今高回落了多少？）
    const int64_t timeSincePeakMs = points.back().epochMs - points[static_cast<size_t>(peakIndex)].epochMs;
    const double timeSincePeakMins = std::max(0.0, static_cast<double>(timeSincePeakMs) / 60000.0);
    const double pullback = std::max(0.0, peakPx - lastPrice);
    const double pullbackAtrRatio = (atr > 0.0) ? (pullback / atr) : (pullback / std::max(0.1, peakPx * 0.005));

    // 谷值反弹特征（多长时间未破低？从今低反弹了多少？）
    const int64_t timeSinceTroughMs = points.back().epochMs - points[static_cast<size_t>(troughIndex)].epochMs;
    const double timeSinceTroughMins = std::max(0.0, static_cast<double>(timeSinceTroughMs) / 60000.0);
    const double rebound = std::max(0.0, lastPrice - troughPx);
    const double reboundAtrRatio = (atr > 0.0) ? (rebound / atr) : (rebound / std::max(0.1, troughPx * 0.005));

    // 动态评估高点是否已确立（以实战盈利防追高为主）：
    // 若早盘冲高后较长时间无法突破，且价格从高位回撤显著，高点确立概率急剧上升
    double hip = 0.15 + dayFraction * 0.35;
    if (highInPast) {
        hip += 0.15;
        if (timeSincePeakMins >= 30.0) hip += 0.08;
        if (timeSincePeakMins >= 60.0) hip += 0.10;
        if (timeSincePeakMins >= 120.0) hip += 0.12;

        if ((pullback / rangeSoFar >= 0.20) || (pullbackAtrRatio >= 0.08)) hip += 0.10;
        if ((pullback / rangeSoFar >= 0.35) || (pullbackAtrRatio >= 0.15)) hip += 0.12;
    } else {
        // 当前正贴近或处于日内最高点，正在冲高
        hip = std::min(hip, 0.30);
    }
    out.highAlreadyInProb = std::clamp(hip, 0.05, 0.95);

    // 动态评估低点是否已确立（顺势防守与支撑验证）：
    double lip = 0.15 + dayFraction * 0.35;
    if (lowInPast) {
        lip += 0.15;
        if (timeSinceTroughMins >= 30.0) lip += 0.08;
        if (timeSinceTroughMins >= 60.0) lip += 0.10;
        if (timeSinceTroughMins >= 120.0) lip += 0.12;
        if ((rebound / rangeSoFar >= 0.20) || (reboundAtrRatio >= 0.08)) lip += 0.10;
        if ((rebound / rangeSoFar >= 0.35) || (reboundAtrRatio >= 0.15)) lip += 0.12;
    } else {
        lip = std::min(lip, 0.30);
    }
    out.lowAlreadyInProb = std::clamp(lip, 0.05, 0.95);

    // 贝叶斯混合时间概率与归一化
    auto probs = blendPeakProbs(historicalPeakBucketCounts);
    redistributePastBuckets(probs, curBucket, highInPast);

    std::vector<PeakBucket> buckets;
    buckets.reserve(48);
    for (int i = 0; i < 48; ++i) {
        PeakBucket b;
        b.bucketIndex = i;
        b.startMinute = i * 30;
        b.endMinute = (i + 1) * 30;
        b.probability = probs[static_cast<size_t>(i)];
        buckets.push_back(b);
    }
    std::sort(buckets.begin(), buckets.end(),
              [](const PeakBucket& a, const PeakBucket& b) { return a.probability > b.probability; });
    out.peakBuckets.assign(buckets.begin(), buckets.begin() + std::min<size_t>(6, buckets.size()));
    if (!out.peakBuckets.empty()) {
        out.peakWindowProb = out.peakBuckets.front().probability;
    }

    // 高低点时间窗口输出（严格 24 小时制）
    if (out.highAlreadyInProb >= 0.65) {
        out.predHighTimeWindow = formatWindow((peakMin / 30) * 30,
                                              ((peakMin / 30) + 1) * 30,
                                              "高点大概率已现");
    } else if (!out.peakBuckets.empty()) {
        out.predHighTimeWindow = formatWindow(out.peakBuckets.front().startMinute,
                                              out.peakBuckets.front().endMinute,
                                              "高点高概率");
    } else {
        out.predHighTimeWindow = "20:30 - 22:30 (美盘主浪)";
    }

    if (out.lowAlreadyInProb >= 0.65) {
        out.predLowTimeWindow = formatWindow((troughMin / 30) * 30,
                                             ((troughMin / 30) + 1) * 30,
                                             "已探底确立");
    } else {
        if (curBucket < 28) {
            out.predLowTimeWindow = "15:00 - 16:30 (欧盘初探底)";
        } else if (curBucket < 40) {
            out.predLowTimeWindow = "20:30 - 21:30 (美盘洗盘探底)";
        } else {
            out.predLowTimeWindow = "22:30 - 24:00 (美盘后程支撑)";
        }
    }

    // 计算预测价格（实战盈利导向：严禁虚高诱导追高）
    const double minGap = std::max(lastPrice * 0.0008, 0.12);
    double predHigh = 0.0;
    double predLow = 0.0;

    if (out.highAlreadyInProb >= 0.65) {
        // 高点大概率已现：预测高锚定在已出现今高，作为关键阻力位
        predHigh = actHigh;
    } else {
        // 仍有可能冲高：根据已现概率评估上行空间
        const double upFactor = std::clamp(1.0 - (out.highAlreadyInProb - 0.15) / 0.50, 0.10, 1.0);
        predHigh = std::max(actHigh, lastPrice + up * upFactor);
        predHigh = std::max(predHigh, actHigh + minGap * 0.25);
    }
    out.remainingUpside = std::max(0.0, predHigh - lastPrice);

    if (out.lowAlreadyInProb >= 0.65) {
        // 低点大概率已现：预测低锚定在已出现今低，作为关键支撑位
        predLow = actLow;
    } else {
        // 仍有可能探底：根据已现概率评估下行支撑
        const double downFactor = std::clamp(1.0 - (out.lowAlreadyInProb - 0.15) / 0.50, 0.10, 1.0);
        predLow = std::min(actLow, lastPrice - down * downFactor);
        predLow = std::min(predLow, actLow - minGap * 0.25);
    }

    // 情景与催化剂演变（以盈利与防守指引为主）
    if (out.highAlreadyInProb >= 0.65) {
        out.scenario = "日内高点大概率已在前期确立，反弹动能衰退，严禁追高，建议逢高止盈或防守保护多头利润";
        out.keyCatalyst = "脉冲见顶 + 动量衰竭";
        out.confidence = out.highAlreadyInProb;
    } else if (out.lowAlreadyInProb >= 0.75 && dayFraction < 0.80) {
        out.scenario = "日内探底企稳，关键支撑有效，可顺多头趋势逢低关注低吸";
        out.keyCatalyst = "探底回升 + 支撑确认";
        out.confidence = out.lowAlreadyInProb;
    } else if (out.peakWindowProb >= 0.10 && dayFraction < 0.85) {
        out.scenario = "仍处潜在冲高窗口，若放量突破今高可顺势关注高概率时段";
        out.keyCatalyst = "欧美盘交叠资金 + 宏观数据";
        out.confidence = std::min(0.85, 0.35 + out.peakWindowProb + remain * 0.25);
    } else if (dayFraction >= 0.85) {
        out.scenario = "尾盘收敛，全日振幅大体确立，避免盲目追单";
        out.keyCatalyst = "隔夜头寸了结";
        out.confidence = 0.50 + out.highAlreadyInProb * 0.25;
    } else {
        out.scenario = "区间震荡蓄势，等待欧/美盘关键时段指引";
        out.keyCatalyst = "关键支撑阻力与时段切换";
        out.confidence = 0.45;
    }

    out.predHigh = predHigh;
    out.predLow = predLow;
    out.valid = predHigh > predLow && predHigh > 0.0 && predLow > 0.0;
    return out;
}

MultiDayTrend ForecastEngine::multiDayTrend(const std::vector<double>& closes)
{
    MultiDayTrend t;
    if (closes.size() < 10)
        return t;

    const int n = static_cast<int>(closes.size());
    auto sma = [&](int period) {
        period = std::min(period, n);
        double s = 0;
        for (int i = n - period; i < n; ++i)
            s += closes[static_cast<size_t>(i)];
        return s / period;
    };
    t.ma5 = sma(5);
    t.ma10 = sma(std::min(10, n));
    t.ma20 = sma(std::min(20, n));
    t.rsi14 = rsiWilder(closes, 14);
    const double last = closes.back();

    double score = 0.0;
    if (t.ma5 > t.ma10) score += 0.25;
    else score -= 0.25;
    if (t.ma10 > t.ma20) score += 0.25;
    else score -= 0.25;
    if (last > t.ma5) score += 0.2;
    else score -= 0.2;
    if (last > t.ma20) score += 0.15;
    else score -= 0.15;
    if (t.rsi14 > 55) score += 0.1;
    else if (t.rsi14 < 45) score -= 0.1;
    if (n >= 6 && closes[static_cast<size_t>(n - 6)] > 0.0) {
        const double r = (last / closes[static_cast<size_t>(n - 6)]) - 1.0;
        score += std::clamp(r * 8.0, -0.2, 0.2);
    }
    t.score = std::clamp(score, -1.0, 1.0);
    if (t.score >= 0.55)
        t.bias = TrendBias::StrongBull;
    else if (t.score >= 0.20)
        t.bias = TrendBias::MildBull;
    else if (t.score <= -0.55)
        t.bias = TrendBias::StrongBear;
    else if (t.score <= -0.20)
        t.bias = TrendBias::MildBear;
    else
        t.bias = TrendBias::Range;
    t.valid = true;
    return t;
}

PatternResult ForecastEngine::identifyPattern(const std::vector<IntradayPoint>& points,
                                             const PatternConfig& cfg)
{
    PatternResult res;
    if (points.size() < 12)
        return res;

    const int n = static_cast<int>(points.size());
    const int span = std::clamp(n / 25, 3, 8);

    struct SwingPoint {
        int index = 0;
        int64_t epochMs = 0;
        double price = 0.0;
    };
    std::vector<SwingPoint> peaks;
    std::vector<SwingPoint> troughs;

    for (int i = span; i < n - span; ++i) {
        bool isPeak = true;
        bool isTrough = true;
        const double p = points[static_cast<size_t>(i)].price;
        for (int j = i - span; j <= i + span; ++j) {
            if (j == i) continue;
            const double pj = points[static_cast<size_t>(j)].price;
            if (pj >= p) isPeak = false;
            if (pj <= p) isTrough = false;
        }
        if (isPeak) {
            peaks.push_back({i, points[static_cast<size_t>(i)].epochMs, p});
        }
        if (isTrough) {
            troughs.push_back({i, points[static_cast<size_t>(i)].epochMs, p});
        }
    }

    const double curPx = points.back().price;
    const double breakBuffer = std::clamp(curPx * 0.0003, 0.08, 0.25); // 施密特迟滞防抖缓冲区 (0.08~0.25元)

    // 动态波幅门槛：确保回踩企稳反弹至前高的利润空间能够有效覆盖手续费摩擦成本（积存金0.4%）并保有安全边际
    const double minAmpRatio = (cfg.feeRate > 0.0005) ? std::max(cfg.feeRate * 1.8, 0.006) : 0.0030;
    const double minAmpAbs = (curPx > 1000.0) ? 5.0 : 2.0; // 伦敦金(USD) vs 积存金(CNY)
    const double requiredAmp = std::max(curPx * minAmpRatio, minAmpAbs);

    // -------------------------------------------------------------------------
    // 斐波那契主升/主跌波段扫描与黄金口袋计算 (Fibonacci Golden Pocket 0.5~0.618)
    // -------------------------------------------------------------------------
    double bestBullAmp = 0.0;
    int bestBullLowIdx = -1, bestBullHighIdx = -1;
    double bestBullLowPx = 0.0, bestBullHighPx = 0.0;

    // A. 从 troughs 与 peaks 中寻找主升浪
    for (const auto& pk : peaks) {
        if (pk.index > n - 2) continue; // 必须留有回踩空间
        for (const auto& tr : troughs) {
            if (tr.index < pk.index && (pk.index - tr.index) >= 3) {
                const double amp = pk.price - tr.price;
                if (amp >= requiredAmp && amp > bestBullAmp) {
                    bestBullAmp = amp;
                    bestBullLowIdx = tr.index;
                    bestBullHighIdx = pk.index;
                    bestBullLowPx = tr.price;
                    bestBullHighPx = pk.price;
                }
            }
        }
    }

    // B. 从近期窗口 [n - fibLookback, n - 1] 扫描全局主升浪
    const int fibLookback = std::min(n, 300);
    int scanMaxIdx = -1;
    double scanMaxPx = -1e9;
    for (int i = n - fibLookback + 4; i <= n - 2; ++i) {
        const double p = points[static_cast<size_t>(i)].price;
        if (p > scanMaxPx) {
            scanMaxPx = p;
            scanMaxIdx = i;
        }
    }
    if (scanMaxIdx >= 0) {
        int scanMinIdx = -1;
        double scanMinPx = 1e9;
        for (int i = std::max(0, n - fibLookback); i < scanMaxIdx; ++i) {
            const double p = points[static_cast<size_t>(i)].price;
            if (p < scanMinPx) {
                scanMinPx = p;
                scanMinIdx = i;
            }
        }
        if (scanMinIdx >= 0) {
            const double amp = scanMaxPx - scanMinPx;
            if (amp >= requiredAmp && amp >= bestBullAmp) {
                bestBullAmp = amp;
                bestBullLowIdx = scanMinIdx;
                bestBullHighIdx = scanMaxIdx;
                bestBullLowPx = scanMinPx;
                bestBullHighPx = scanMaxPx;
            }
        }
    }

    if (bestBullAmp >= requiredAmp) {
        res.fib50 = bestBullHighPx - 0.500 * bestBullAmp;
        res.fib618 = bestBullHighPx - 0.618 * bestBullAmp;
        res.fibSwingHigh = bestBullHighPx;
        res.fibSwingLow = bestBullLowPx;
    }

    PatternResult fibCandidate;
    bool hasFibCandidate = false;

    // 判定多头黄金口袋回踩 (Kirito 经典战法：健康回踩 0.50~0.618 黄金口袋逢低买入)
    if (bestBullAmp >= requiredAmp) {
        double pbMin = bestBullHighPx;
        for (int i = bestBullHighIdx; i < n; ++i) {
            pbMin = std::min(pbMin, points[static_cast<size_t>(i)].price);
        }
        const double fib50 = res.fib50;
        const double fib618 = res.fib618;
        const double fib382 = bestBullHighPx - 0.382 * bestBullAmp;

        // 回踩触及或深入 0.50~0.618 黄金口袋核心区间
        if (pbMin <= fib50 + breakBuffer && pbMin >= fib618 - breakBuffer * 1.5) {
            // 情况 A：黄金口袋企稳反弹（经典顺势逢低买入触发🚀）
            if (curPx >= pbMin + breakBuffer * 0.35 && curPx >= fib618 - breakBuffer && curPx <= fib382 + breakBuffer) {
                fibCandidate.type = PatternType::FibonacciGoldenPocket;
                fibCandidate.fib50 = fib50;
                fibCandidate.fib618 = fib618;
                fibCandidate.fibSwingHigh = bestBullHighPx;
                fibCandidate.fibSwingLow = bestBullLowPx;
                fibCandidate.support = fib618;
                fibCandidate.resistance = bestBullHighPx;
                fibCandidate.breakoutTarget = bestBullHighPx + bestBullAmp * 0.382;

                // 核心趋势与日内高点切合度核验：
                if (cfg.highAlreadyInProb >= 0.65 || cfg.multiDayBias == TrendBias::StrongBear) {
                    fibCandidate.name = "斐波那契黄金口袋 (顺势偏空·弱反抽观察)";
                    fibCandidate.bias = "弱势蓄势";
                    fibCandidate.strategy = "现价回踩斐波那契 0.50~0.618 黄金口袋关键支撑区并微幅反弹，但受多日空头排列/日内高点已确立压制，反抽动能有限，切勿盲目追涨做多，注意逢高防守。";
                } else {
                    fibCandidate.name = "斐波那契黄金口袋 (0.5~0.618回踩企稳🚀)";
                    fibCandidate.bias = "强烈看多";
                    fibCandidate.strategy = "主升浪上涨后健康回踩斐波那契 0.50~0.618 黄金口袋关键支撑区并企稳反弹！顺势逢低布局，防守位设在 0.618 支撑 "
                                           + fmtPrice(fib618) + "，第一止盈目标看前高 " + fmtPrice(bestBullHighPx)
                                           + "，突破后理论扩展目标看 " + fmtPrice(fibCandidate.breakoutTarget) + "。";
                }
                fibCandidate.valid = true;
                hasFibCandidate = true;
            }
            // 情况 B：正在黄金口袋支撑区回踩测试蓄势
            else if (curPx >= fib618 - breakBuffer && curPx <= fib50 + breakBuffer) {
                fibCandidate.type = PatternType::FibonacciGoldenPocket;
                fibCandidate.fib50 = fib50;
                fibCandidate.fib618 = fib618;
                fibCandidate.fibSwingHigh = bestBullHighPx;
                fibCandidate.fibSwingLow = bestBullLowPx;
                fibCandidate.name = "斐波那契黄金口袋 (0.5~0.618回踩测试)";
                fibCandidate.bias = "多头蓄势";
                fibCandidate.support = fib618;
                fibCandidate.resistance = fib50;
                fibCandidate.breakoutTarget = bestBullHighPx;
                fibCandidate.strategy = "现价回踩进入斐波那契 0.50~0.618 黄金口袋核心支撑区间 ("
                                       + fmtPrice(fib618) + " ~ " + fmtPrice(fib50)
                                       + ")，正在测试多头承接力，切忌恐慌杀跌，密切观察止跌企稳低吸机会。";
                fibCandidate.valid = true;
                hasFibCandidate = true;
            }
        }
        // 情况 C：跌破 0.618 黄金口袋最后防守位
        if (!hasFibCandidate && pbMin < fib618 - breakBuffer && curPx < fib618 - breakBuffer && curPx >= bestBullLowPx - breakBuffer) {
            fibCandidate.type = PatternType::FibonacciGoldenPocket;
            fibCandidate.fib50 = fib50;
            fibCandidate.fib618 = fib618;
            fibCandidate.fibSwingHigh = bestBullHighPx;
            fibCandidate.fibSwingLow = bestBullLowPx;
            fibCandidate.name = "斐波那契黄金口袋 (0.618失守破位⚠️)";
            fibCandidate.bias = "看空破位";
            fibCandidate.resistance = fib618;
            fibCandidate.support = bestBullLowPx;
            fibCandidate.breakoutTarget = bestBullLowPx;
            fibCandidate.strategy = "价格有效跌破斐波那契 0.618 黄金口袋关键防守支撑 ("
                                   + fmtPrice(fib618) + ")，回调已演变为深度破位，原支撑转为强阻力，需防范进一步回探起涨点 "
                                   + fmtPrice(bestBullLowPx) + "，注意防守减仓。";
            fibCandidate.valid = true;
            hasFibCandidate = true;
        }
    }

    // 判定空头黄金口袋反弹受阻 (Bearish Golden Pocket)
    if (!hasFibCandidate) {
        double bestBearAmp = 0.0;
        int bestBearHighIdx = -1, bestBearLowIdx = -1;
        double bestBearHighPx = 0.0, bestBearLowPx = 0.0;
        for (const auto& tr : troughs) {
            if (tr.index > n - 2) continue;
            for (const auto& pk : peaks) {
                if (pk.index < tr.index && (tr.index - pk.index) >= 3) {
                    const double amp = pk.price - tr.price;
                    if (amp >= requiredAmp && amp > bestBearAmp) {
                        bestBearAmp = amp;
                        bestBearHighIdx = pk.index;
                        bestBearLowIdx = tr.index;
                        bestBearHighPx = pk.price;
                        bestBearLowPx = tr.price;
                    }
                }
            }
        }
        if (bestBearAmp >= requiredAmp) {
            double pbMax = bestBearLowPx;
            for (int i = bestBearLowIdx; i < n; ++i) {
                pbMax = std::max(pbMax, points[static_cast<size_t>(i)].price);
            }
            const double bearFib50 = bestBearLowPx + 0.500 * bestBearAmp;
            const double bearFib618 = bestBearLowPx + 0.618 * bestBearAmp;
            const double bearFib382 = bestBearLowPx + 0.382 * bestBearAmp;
            if (pbMax >= bearFib50 - breakBuffer && pbMax <= bearFib618 + breakBuffer * 1.5) {
                if (curPx <= pbMax - breakBuffer * 0.35 && curPx <= bearFib618 + breakBuffer && curPx >= bearFib382 - breakBuffer) {
                    fibCandidate.type = PatternType::FibonacciGoldenPocket;
                    fibCandidate.fib50 = bearFib50;
                    fibCandidate.fib618 = bearFib618;
                    fibCandidate.fibSwingHigh = bestBearHighPx;
                    fibCandidate.fibSwingLow = bestBearLowPx;
                    fibCandidate.resistance = bearFib618;
                    fibCandidate.support = bestBearLowPx;
                    fibCandidate.breakoutTarget = bestBearLowPx - bestBearAmp * 0.382;

                    if (cfg.lowAlreadyInProb >= 0.65 || cfg.multiDayBias == TrendBias::StrongBull) {
                        fibCandidate.name = "斐波那契黄金口袋 (顺势偏多·回调休整)";
                        fibCandidate.bias = "中性蓄势";
                        fibCandidate.strategy = "急跌后反弹测试 0.50~0.618 黄金口袋压制，但大周期多头趋势强劲且日内低点已确立，回调属于良性洗盘，切勿盲目杀跌，耐心等待多头重聚。";
                    } else {
                        fibCandidate.name = "斐波那契黄金口袋 (0.5~0.618反弹受阻⚠️)";
                        fibCandidate.bias = "强烈看空";
                        fibCandidate.strategy = "急跌后弱势反弹至斐波那契 0.50~0.618 黄金口袋强压区受阻回落！反抽动能衰竭，建议逢高减仓防守，谨防二次探底 "
                                               + fmtPrice(bestBearLowPx) + "。";
                    }
                    fibCandidate.valid = true;
                    hasFibCandidate = true;
                }
            }
        }
    }

    if (peaks.size() < 2 || troughs.size() < 2) {
        if (hasFibCandidate)
            return fibCandidate;
        res.type = PatternType::None;
        res.name = "震荡整理";
        res.bias = "震荡";
        res.strategy = "日内波幅平缓，暂无典型几何形态，关注日内极值支撑与阻力。";
        res.valid = true;
        return res;
    }

    const auto& p1 = peaks[peaks.size() - 2];
    const auto& p2 = peaks.back();
    const auto& t1 = troughs[troughs.size() - 2];
    const auto& t2 = troughs.back();

    const double priceRange = std::max(0.1, p2.price - t2.price);
    const double slopeThreshold = priceRange * 0.003;

    // =========================================================================
    // 1) 脉冲 V 型反转 / 倒 V 冲高跳水 (黄金数据公布/情绪急杀剧烈洗盘)
    // =========================================================================
    if (n >= 25) {
        const int lookback = std::min(70, n);
        int minIdx = n - lookback, maxIdx = n - lookback;
        double minPx = points[static_cast<size_t>(minIdx)].price;
        double maxPx = points[static_cast<size_t>(maxIdx)].price;
        for (int i = n - lookback; i < n; ++i) {
            const double p = points[static_cast<size_t>(i)].price;
            if (p < minPx) { minPx = p; minIdx = i; }
            if (p > maxPx) { maxPx = p; maxIdx = i; }
        }
        // V型急跌反转：最低点在区间中段，且自最低点强力拉升收复跌幅 >= 85%
        if (minIdx >= n - lookback + 4 && minIdx <= n - 3) {
            double preHigh = points[static_cast<size_t>(n - lookback)].price;
            for (int i = n - lookback; i < minIdx; ++i)
                preHigh = std::max(preHigh, points[static_cast<size_t>(i)].price);
            const double drop = preHigh - minPx;
            const double rally = curPx - minPx;
            if (drop >= curPx * 0.0035 && rally >= drop * 0.85) {
                res.type = PatternType::VReversalBottom;
                res.breakoutTarget = preHigh + drop * 0.5;
                if (curPx >= preHigh + breakBuffer) {
                    res.name = "V型急跌反转 (强势突破🚀)";
                    res.bias = "看多突破";
                    res.support = preHigh;
                    res.resistance = res.breakoutTarget;
                    res.strategy = "短线急跌洗盘被多头强势深V收复并放量突破起跌点！诱空失败，多头全面反攻，回踩原起跌点为买入支撑。";
                } else {
                    res.name = "V型急跌反转 (深V蓄势中)";
                    res.bias = "多头蓄势";
                    res.support = minPx;
                    res.resistance = preHigh;
                    res.strategy = "急速探底后快速强力抽升，多头承接盘强劲，现价正冲击起跌阻力位，等待放量突破确认。";
                }
                res.valid = true;
                return res;
            }
        }
        // 倒V冲高跳水：最高点在区间中段，自最高点快速跳水回吐涨幅 >= 85%
        if (maxIdx >= n - lookback + 4 && maxIdx <= n - 3) {
            double preLow = points[static_cast<size_t>(n - lookback)].price;
            for (int i = n - lookback; i < maxIdx; ++i)
                preLow = std::min(preLow, points[static_cast<size_t>(i)].price);
            const double surge = maxPx - preLow;
            const double plunge = maxPx - curPx;
            if (surge >= curPx * 0.0035 && plunge >= surge * 0.85) {
                res.type = PatternType::VReversalTop;
                res.breakoutTarget = preLow - surge * 0.5;
                if (curPx <= preLow - breakBuffer) {
                    res.name = "倒V冲高跳水 (破位跌破⚠️)";
                    res.bias = "看空破位";
                    res.resistance = preLow;
                    res.support = res.breakoutTarget;
                    res.strategy = "多头脉冲急拉冲高受阻，跳水回落全额吞没涨幅并跌破起涨支撑，形成倒V见顶破位，谨防加速下行，注意防守止盈。";
                } else {
                    res.name = "倒V冲高跳水 (冲高承压中)";
                    res.bias = "空头蓄势";
                    res.resistance = maxPx;
                    res.support = preLow;
                    res.strategy = "脉冲拉升后遭遇沉重抛压大幅回落，上方形成假突破套牢盘，正在测试起涨支撑。";
                }
                res.valid = true;
                return res;
            }
        }
    }

    // =========================================================================
    // 2) 头肩底 (倒头肩) & 头肩顶 (大级别反转之王)
    // =========================================================================
    if (troughs.size() >= 3) {
        const auto& lt1 = troughs[troughs.size() - 3]; // 左肩
        const auto& lt2 = troughs[troughs.size() - 2]; // 头部 (最深谷底)
        const auto& lt3 = troughs.back();             // 右肩
        if (lt1.price > 0 && lt2.price > 0 && lt3.price > 0 &&
            lt2.price < lt1.price - priceRange * 0.20 && lt2.price < lt3.price - priceRange * 0.20 &&
            std::abs(lt1.price - lt3.price) / lt1.price <= 0.0030) {
            double neckline = 0.0;
            for (int idx = lt1.index; idx <= lt3.index; ++idx)
                neckline = std::max(neckline, points[static_cast<size_t>(idx)].price);
            if (neckline > std::max(lt1.price, lt3.price) + priceRange * 0.2) {
                res.type = PatternType::HeadAndShouldersBottom;
                const double target = neckline + (neckline - lt2.price);
                res.breakoutTarget = target;
                if (curPx >= neckline + breakBuffer) {
                    res.name = "头肩底突破 (倒头肩确立🚀)";
                    res.bias = "看多突破";
                    res.support = neckline;
                    res.resistance = target;
                    res.strategy = "现价已强势突破头肩底颈线！经典大级别反转确立，颈线转为强支撑，上看目标位 "
                                   + fmtPrice(target) + "。";
                } else {
                    res.name = "头肩底形态 (倒头肩蓄势)";
                    res.bias = "多头蓄势";
                    res.support = std::min(lt1.price, lt3.price);
                    res.resistance = neckline;
                    res.strategy = "右肩构筑企稳，倒头肩形态蓄势冲击颈线阻力，放量突破将开启大级别主升浪。";
                }
                res.valid = true;
                return res;
            }
        }
    }

    if (peaks.size() >= 3) {
        const auto& lp1 = peaks[peaks.size() - 3]; // 左肩
        const auto& lp2 = peaks[peaks.size() - 2]; // 头部 (最高峰)
        const auto& lp3 = peaks.back();            // 右肩
        if (lp1.price > 0 && lp2.price > 0 && lp3.price > 0 &&
            lp2.price > lp1.price + priceRange * 0.20 && lp2.price > lp3.price + priceRange * 0.20 &&
            std::abs(lp1.price - lp3.price) / lp1.price <= 0.0030) {
            double neckline = 1e9;
            for (int idx = lp1.index; idx <= lp3.index; ++idx)
                neckline = std::min(neckline, points[static_cast<size_t>(idx)].price);
            if (neckline < std::min(lp1.price, lp3.price) - priceRange * 0.2) {
                res.type = PatternType::HeadAndShouldersTop;
                const double target = neckline - (lp2.price - neckline);
                res.breakoutTarget = target;
                if (curPx <= neckline - breakBuffer) {
                    res.name = "头肩顶跌破 (头部确立⚠️)";
                    res.bias = "看空破位";
                    res.resistance = neckline;
                    res.support = target;
                    res.strategy = "现价已跌破头肩顶颈线支撑！大级别顶部确认，原颈线转为强压，防范回落目标 "
                                   + fmtPrice(target) + "。";
                } else {
                    res.name = "头肩顶形态 (高位受阻)";
                    res.bias = "空头蓄势";
                    res.resistance = lp2.price;
                    res.support = neckline;
                    res.strategy = "右肩冲高乏力，头肩顶形成，当前测试颈线支撑，若跌破颈线将引发加速下挫。";
                }
                res.valid = true;
                return res;
            }
        }
    }

    // =========================================================================
    // 3) 三重底 / 三重顶 识别 (三次坚决确认)
    // =========================================================================
    if (troughs.size() >= 3) {
        const auto& tr1 = troughs[troughs.size() - 3];
        const auto& tr2 = troughs[troughs.size() - 2];
        const auto& tr3 = troughs.back();
        const double mnT = std::min({tr1.price, tr2.price, tr3.price});
        const double mxT = std::max({tr1.price, tr2.price, tr3.price});
        if (mnT > 0 && (mxT - mnT) / mnT <= 0.0018) {
            double neckline = 0.0;
            for (int idx = tr1.index; idx <= tr3.index; ++idx)
                neckline = std::max(neckline, points[static_cast<size_t>(idx)].price);
            if (neckline > mxT + priceRange * 0.22) {
                res.type = PatternType::TripleBottom;
                const double target = neckline + (neckline - mnT);
                res.breakoutTarget = target;
                if (curPx >= neckline + breakBuffer) {
                    res.name = "三重底突破 (坚实反转🚀)";
                    res.bias = "看多突破";
                    res.support = neckline;
                    res.resistance = target;
                    res.strategy = "三次回踩底部均获强力支撑，现价强势放量突破颈线，三重筑底确立，后市上看 "
                                   + fmtPrice(target) + "。";
                } else {
                    res.name = "三重底形态 (三重筑底)";
                    res.bias = "多头蓄势";
                    res.support = mnT;
                    res.resistance = neckline;
                    res.strategy = "关键支撑经过三次严苛测试依然有效，多头底部扎实，等待放量突破颈线确认。";
                }
                res.valid = true;
                return res;
            }
        }
    }

    if (peaks.size() >= 3) {
        const auto& pk1 = peaks[peaks.size() - 3];
        const auto& pk2 = peaks[peaks.size() - 2];
        const auto& pk3 = peaks.back();
        const double mnP = std::min({pk1.price, pk2.price, pk3.price});
        const double mxP = std::max({pk1.price, pk2.price, pk3.price});
        if (mnP > 0 && (mxP - mnP) / mnP <= 0.0018) {
            double neckline = 1e9;
            for (int idx = pk1.index; idx <= pk3.index; ++idx)
                neckline = std::min(neckline, points[static_cast<size_t>(idx)].price);
            if (neckline < mnP - priceRange * 0.22) {
                res.type = PatternType::TripleTop;
                const double target = neckline - (mxP - neckline);
                res.breakoutTarget = target;
                if (curPx <= neckline - breakBuffer) {
                    res.name = "三重顶跌破 (强阻力破位⚠️)";
                    res.bias = "看空破位";
                    res.resistance = neckline;
                    res.support = target;
                    res.strategy = "三次冲高同一阻力位均严重受阻，现价跌破颈线支撑，多头放弃抵抗，下方目标 "
                                   + fmtPrice(target) + "。";
                } else {
                    res.name = "三重顶形态 (三重承压)";
                    res.bias = "空头蓄势";
                    res.resistance = mxP;
                    res.support = neckline;
                    res.strategy = "上方水平阻力位三次冲高无法突破，抛压沉重，当前测试颈线，破位需坚决防守。";
                }
                res.valid = true;
                return res;
            }
        }
    }

    // =========================================================================
    // 4) 顺势旗形中继 (Bull Flag / Bear Flag 单边趋势主升浪)
    // =========================================================================
    if (n >= 35) {
        const int flagLen = std::clamp(n / 5, 10, 25);
        const int poleLen = std::clamp((n - flagLen) / 2, 8, 30);
        const double poleStart = points[static_cast<size_t>(n - flagLen - poleLen)].price;
        const double poleEnd = points[static_cast<size_t>(n - flagLen)].price;
        const double poleHeight = poleEnd - poleStart;

        // 看涨旗形：旗杆急拉 >= 0.35%，随后微幅回踩旗面整理，波幅 <= 旗杆 45%
        if (poleHeight >= curPx * 0.0035) {
            double flagHigh = poleEnd, flagLow = poleEnd;
            for (int i = n - flagLen; i < n; ++i) {
                flagHigh = std::max(flagHigh, points[static_cast<size_t>(i)].price);
                flagLow = std::min(flagLow, points[static_cast<size_t>(i)].price);
            }
            if ((flagHigh - flagLow) <= poleHeight * 0.45 && curPx >= flagLow - breakBuffer) {
                res.type = PatternType::BullFlag;
                const double target = flagHigh + poleHeight;
                res.breakoutTarget = target;
                if (curPx >= flagHigh + breakBuffer) {
                    if (hasFibCandidate && (fibCandidate.bias == "强烈看多" || fibCandidate.bias == "多头蓄势")) {
                        res.name = "看涨旗形 + 黄金口袋共振 (顺势主升浪🚀)";
                        res.strategy = "放量向上突破看涨旗面，且旗面回踩完美契合斐波那契 0.50~0.618 黄金口袋关键支撑！顺势双重共振，第一目标位上看 "
                                       + fmtPrice(target) + "。";
                    } else {
                        res.name = "看涨旗形 (顺势主升浪🚀)";
                        res.strategy = "放量向上突破看涨旗面！主升浪中继形态确立，顺势看多，第一目标上看 "
                                       + fmtPrice(target) + "。";
                    }
                    res.bias = "看多突破";
                    res.support = flagHigh;
                    res.resistance = target;
                } else {
                    res.name = "看涨旗形 (蓄势中继)";
                    res.bias = "多头蓄势";
                    res.resistance = flagHigh;
                    res.support = flagLow;
                    res.strategy = "强势冲高后微幅回踩构筑旗面整理，属于健康顺势中继，等待突破旗面上轨。";
                }
                res.valid = true;
                return res;
            }
        }
        // 看跌旗形：旗杆急跌，随后微幅反弹整理
        if (poleHeight <= -curPx * 0.0035) {
            const double dropHeight = -poleHeight;
            double flagHigh = poleEnd, flagLow = poleEnd;
            for (int i = n - flagLen; i < n; ++i) {
                flagHigh = std::max(flagHigh, points[static_cast<size_t>(i)].price);
                flagLow = std::min(flagLow, points[static_cast<size_t>(i)].price);
            }
            if ((flagHigh - flagLow) <= dropHeight * 0.45 && curPx <= flagHigh + breakBuffer) {
                res.type = PatternType::BearFlag;
                const double target = flagLow - dropHeight;
                res.breakoutTarget = target;
                if (curPx <= flagLow - breakBuffer) {
                    res.name = "看跌旗形 (顺势破位下挫⚠️)";
                    res.bias = "看空破位";
                    res.resistance = flagLow;
                    res.support = target;
                    res.strategy = "向下跌破看跌旗面下轨！弱势中继形态确立，谨防顺势加速探底 "
                                   + fmtPrice(target) + "。";
                } else {
                    res.name = "看跌旗形 (偏空中继)";
                    res.bias = "空头蓄势";
                    res.resistance = flagHigh;
                    res.support = flagLow;
                    res.strategy = "急跌后低位弱势反弹构筑看跌旗面，反弹动能有限，随时谨防二次破位。";
                }
                res.valid = true;
                return res;
            }
        }
    }

    // =========================================================================
    // 5) 双底 / W底 识别 (两底接近，中间颈线)
    // =========================================================================
    if (t1.price > 0.0 && std::abs(t2.price - t1.price) / t1.price <= 0.0022 && t2.index > t1.index) {
        double neckline = 0.0;
        for (int idx = t1.index; idx <= t2.index; ++idx) {
            neckline = std::max(neckline, points[static_cast<size_t>(idx)].price);
        }
        const double baseSupport = std::min(t1.price, t2.price);
        if (neckline > baseSupport + priceRange * 0.25) {
            res.type = PatternType::DoubleBottom;
            const double target = neckline + (neckline - baseSupport);
            res.breakoutTarget = target;
            if (curPx >= neckline + breakBuffer) {
                if (hasFibCandidate && (fibCandidate.bias == "强烈看多" || fibCandidate.bias == "多头蓄势")) {
                    res.name = "双底突破 + 黄金口袋共振 (W底强力反转🚀)";
                    res.strategy = "突破W底颈线，且筑底回踩恰好落入斐波那契 0.50~0.618 黄金口袋强支撑！双重多头共振，第一目标位上看 "
                                   + fmtPrice(target) + "。";
                } else {
                    res.name = "双底突破 (W底确立🚀)";
                    res.strategy = "现价已强势突破W底颈线！颈线位已由阻力转为强支撑，双底反转形态确立，第一目标位上看 "
                                   + fmtPrice(target) + "。";
                }
                res.bias = "看多突破";
                res.support = neckline;
                res.resistance = target;
            } else {
                res.name = "双底形态 (W底筑底)";
                res.bias = "多头蓄势";
                res.support = baseSupport;
                res.resistance = neckline;
                res.strategy = "双底支撑有效确立，现价正在向颈线阻力蓄势发起冲击，突破颈线后将打开翻倍反弹空间。";
            }
            res.valid = true;
            return res;
        }
    }

    // =========================================================================
    // 6) 双顶 / M顶 识别
    // =========================================================================
    if (p1.price > 0.0 && std::abs(p2.price - p1.price) / p1.price <= 0.0022 && p2.index > p1.index) {
        double neckline = 1e9;
        for (int idx = p1.index; idx <= p2.index; ++idx) {
            neckline = std::min(neckline, points[static_cast<size_t>(idx)].price);
        }
        const double baseResistance = std::max(p1.price, p2.price);
        if (neckline < baseResistance - priceRange * 0.25) {
            res.type = PatternType::DoubleTop;
            const double target = neckline - (baseResistance - neckline);
            res.breakoutTarget = target;
            if (curPx <= neckline - breakBuffer) {
                res.name = "双顶破位 (M顶确立⚠️)";
                res.bias = "看空破位";
                res.resistance = neckline;
                res.support = target;
                res.strategy = "现价已跌破M顶颈线支撑！颈线位已转为反弹强阻力，双顶下行形态确立，下方第一目标位看至 "
                               + fmtPrice(target) + "。";
            } else {
                res.name = "双顶形态 (M顶受阻)";
                res.bias = "空头蓄势";
                res.resistance = baseResistance;
                res.support = neckline;
                res.strategy = "高位双顶强阻力压制，反弹未能创新高，当前测试颈线支撑，若跌破颈线谨防加速回落。";
            }
            res.valid = true;
            return res;
        }
    }

    const double peakSlope = (p2.price - p1.price) / static_cast<double>(std::max(1, p2.index - p1.index));
    const double troughSlope = (t2.price - t1.price) / static_cast<double>(std::max(1, t2.index - t1.index));
    const double curUpper = p2.price + peakSlope * static_cast<double>(std::max(0, n - 1 - p2.index));
    const double curLower = t2.price + troughSlope * static_cast<double>(std::max(0, n - 1 - t2.index));

    // =========================================================================
    // 7) 扩散喇叭形 (Broadening / Megaphone 情绪极度分歧发散)
    // =========================================================================
    if (peakSlope > slopeThreshold && troughSlope < -slopeThreshold && priceRange >= curPx * 0.0035) {
        res.type = PatternType::Broadening;
        res.name = "扩散喇叭形 (情绪加剧)";
        res.bias = "剧烈变盘";
        res.resistance = curUpper;
        res.support = curLower;
        res.breakoutTarget = curUpper + priceRange * 0.5;
        res.strategy = "高点更高、低点更低，振幅发散放大，市场多空情绪分歧剧烈，避免追单，谨防剧烈单边爆发！";
        res.valid = true;
        return res;
    }

    // =========================================================================
    // 8) 下降楔形 (Falling Wedge)：两轨下移，上轨斜率更陡（下降收敛），看涨向上突破
    // =========================================================================
    if (peakSlope < -slopeThreshold && troughSlope < 0 && peakSlope < troughSlope) {
        res.type = PatternType::FallingWedge;
        res.breakoutTarget = p1.price;
        if (curPx >= curUpper + breakBuffer) {
            if (hasFibCandidate && (fibCandidate.bias == "强烈看多" || fibCandidate.bias == "多头蓄势")) {
                res.name = "下降楔形 + 黄金口袋共振 (向上突破🚀)";
                res.strategy = "下降楔形向上突破上轨，且下沿获得斐波那契 0.50~0.618 黄金口袋坚实承接！空头衰竭反转，顺势看多，第一目标上看 "
                               + fmtPrice(p1.price) + "。";
            } else {
                res.name = "下降楔形 (向上突破🚀)";
                res.strategy = "现价已向上突破下降楔形上轨！空头衰竭确认，反转向上变盘，顺势看多。";
            }
            res.bias = "看多突破";
            res.support = curUpper;
            res.resistance = p1.price;
        } else if (curPx <= curLower - breakBuffer) {
            res.name = "下降楔形 (向下破位⚠️)";
            res.bias = "看空破位";
            res.resistance = curLower;
            res.support = curLower - priceRange * 0.5;
            res.strategy = "跌破下轨支撑破位下行，需防范加速探底风险。";
        } else {
            res.name = "下降楔形 (收敛蓄势)";
            res.bias = "多头蓄势";
            res.resistance = curUpper;
            res.support = curLower;
            res.strategy = "下降楔形波幅持续收敛，空头下探动能衰竭，等待放量突破上轨阻力！";
        }
        res.valid = true;
        return res;
    }

    // =========================================================================
    // 9) 上升楔形 (Rising Wedge)：两轨上移，下轨斜率更陡（上升收敛），看跌向下反转
    // =========================================================================
    if (peakSlope > slopeThreshold && troughSlope > 0 && troughSlope > peakSlope) {
        res.type = PatternType::RisingWedge;
        res.breakoutTarget = t1.price;
        if (curPx <= curLower - breakBuffer) {
            res.name = "上升楔形 (向下跌破⚠️)";
            res.bias = "看空破位";
            res.resistance = curLower;
            res.support = t1.price;
            res.strategy = "现价已跌破上升楔形下轨支撑！多头动能衰竭确认，谨防加速下行，注意防守减仓。";
        } else {
            res.name = "上升楔形 (高位收敛)";
            res.bias = "空头蓄势";
            res.resistance = curUpper;
            res.support = curLower;
            res.strategy = "上升楔形持续收紧，高位上冲动能递减，谨防跌破下轨支撑引发快速回落！";
        }
        res.valid = true;
        return res;
    }

    // =========================================================================
    // 10) 对称三角形 (Symmetrical Triangle)：波峰下移，波谷抬高（对称收敛）
    // =========================================================================
    if (peakSlope < -slopeThreshold && troughSlope > slopeThreshold) {
        res.type = PatternType::SymmetricalTriangle;
        res.breakoutTarget = curPx + (p1.price - t1.price) * 0.7;
        if (curPx >= curUpper + breakBuffer) {
            res.name = "对称三角形 (向上突破🚀)";
            res.bias = "看多突破";
            res.support = curUpper;
            res.resistance = p1.price;
            res.strategy = "对称三角形向上突破上轨压制！多头决战胜出，顺势看多做多。";
        } else if (curPx <= curLower - breakBuffer) {
            res.name = "对称三角形 (向下破位⚠️)";
            res.bias = "看空破位";
            res.resistance = curLower;
            res.support = t1.price;
            res.strategy = "对称三角形跌破下轨支撑！空头胜出，谨防深度调整。";
        } else {
            res.name = "对称三角形收敛 (Triangle)";
            res.bias = "中性蓄势";
            res.resistance = curUpper;
            res.support = curLower;
            res.strategy = "高低点对称收敛，多空决战蓄势，关注突破方向，顺势跟随。";
        }
        res.valid = true;
        return res;
    }

    // =========================================================================
    // 11) 上升三角形：阻力接近水平，低点抬高
    // =========================================================================
    if (std::abs(peakSlope) <= slopeThreshold && troughSlope > slopeThreshold) {
        res.type = PatternType::AscendingTriangle;
        const double resLvl = std::max(p1.price, p2.price);
        res.breakoutTarget = resLvl + (resLvl - t1.price);
        if (curPx >= resLvl + breakBuffer) {
            res.name = "上升三角形 (向上突破🚀)";
            res.bias = "看多突破";
            res.support = resLvl;
            res.resistance = res.breakoutTarget;
            res.strategy = "现价已放量突破上升三角形水平阻力位！打开上行空间，顺势看多。";
        } else {
            res.name = "上升三角形 (Ascending Triangle)";
            res.bias = "多头蓄势";
            res.resistance = resLvl;
            res.support = t2.price;
            res.strategy = "水平阻力位多次测试，低点持续抬高，多头蓄势占优，等待向上突破。";
        }
        res.valid = true;
        return res;
    }

    // =========================================================================
    // 12) 下降三角形：支撑接近水平，高点降低
    // =========================================================================
    if (std::abs(troughSlope) <= slopeThreshold && peakSlope < -slopeThreshold) {
        res.type = PatternType::DescendingTriangle;
        const double supLvl = std::min(t1.price, t2.price);
        res.breakoutTarget = supLvl - (p1.price - supLvl);
        if (curPx <= supLvl - breakBuffer) {
            res.name = "下降三角形 (向下破位⚠️)";
            res.bias = "看空破位";
            res.resistance = supLvl;
            res.support = res.breakoutTarget;
            res.strategy = "现价已跌破下降三角形水平支撑位！空头破位确立，谨防加速下行。";
        } else {
            res.name = "下降三角形 (Descending Triangle)";
            res.bias = "空头蓄势";
            res.support = supLvl;
            res.resistance = p2.price;
            res.strategy = "水平支撑位多次受压，高点逐级压低，空头蓄势占优，谨防破位下行。";
        }
        res.valid = true;
        return res;
    }

    // =========================================================================
    // 13) 倾斜通道 (上升通道 / 下降通道)
    // =========================================================================
    if (peakSlope > slopeThreshold && troughSlope > slopeThreshold) {
        res.type = PatternType::ChannelUp;
        res.name = "上升通道 (Channel Up)";
        res.bias = "震荡向上";
        res.resistance = curUpper;
        res.support = curLower;
        res.breakoutTarget = curUpper + priceRange * 0.5;
        res.strategy = "处于多头上升通道运行，可沿通道下轨顺势做多，破下轨止损。";
        res.valid = true;
        return res;
    } else if (peakSlope < -slopeThreshold && troughSlope < -slopeThreshold) {
        res.type = PatternType::ChannelDown;
        res.name = "下降通道 (Channel Down)";
        res.bias = "震荡向下";
        res.resistance = curUpper;
        res.support = curLower;
        res.breakoutTarget = curLower - priceRange * 0.5;
        res.strategy = "处于弱势下降通道运行，逢反弹受阻通道上轨可适度防守。";
        res.valid = true;
        return res;
    }

    if (hasFibCandidate) {
        return fibCandidate;
    }

    res.type = PatternType::None;
    res.name = "箱体震荡整理";
    res.bias = "震荡整理";
    res.resistance = std::max(p2.price, curUpper);
    res.support = std::min(t2.price, curLower);
    res.strategy = "处于箱体区间震荡，可高抛低吸或等待突破方向明朗。";
    res.valid = true;
    return res;
}

} // namespace goldsdk
