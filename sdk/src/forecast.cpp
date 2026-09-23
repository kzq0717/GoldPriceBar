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
    const int eh = (endMin / 60) % 24, em = endMin % 60;
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%02d:%02d - %02d:%02d (%s)", sh, sm, eh, em, tag);
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
        if (timeSincePeakMins >= 30.0) hip += 0.10;
        if (timeSincePeakMins >= 60.0) hip += 0.12;
        if (timeSincePeakMins >= 120.0) hip += 0.10;

        if (pullbackAtrRatio >= 0.15) hip += 0.12;
        if (pullbackAtrRatio >= 0.28) hip += 0.15;
    } else {
        // 当前正贴近或处于日内最高点，正在冲高
        hip = std::min(hip, 0.30);
    }
    out.highAlreadyInProb = std::clamp(hip, 0.05, 0.95);

    // 动态评估低点是否已确立（顺势防守与支撑验证）：
    double lip = 0.15 + dayFraction * 0.35;
    if (lowInPast) {
        lip += 0.15;
        if (timeSinceTroughMins >= 30.0) lip += 0.10;
        if (timeSinceTroughMins >= 60.0) lip += 0.12;
        if (reboundAtrRatio >= 0.15) lip += 0.12;
        if (reboundAtrRatio >= 0.28) lip += 0.15;
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
    if (out.highAlreadyInProb >= 0.70) {
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

    if (out.lowAlreadyInProb >= 0.70) {
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

    if (out.highAlreadyInProb >= 0.70) {
        // 高点大概率已现：预测高锁定在已出现今高微幅扰动处
        predHigh = std::max(actHigh, lastPrice) + minGap * 0.20;
    } else {
        // 仍有可能冲高：根据已现概率削减盲目上行预算
        const double upFactor = std::clamp(1.0 - (out.highAlreadyInProb - 0.15) / 0.55, 0.20, 1.0);
        predHigh = std::max(actHigh, lastPrice) + up * upFactor;
        predHigh = std::max(predHigh, actHigh + minGap * 0.25);
    }
    out.remainingUpside = std::max(0.0, predHigh - lastPrice);

    if (out.lowAlreadyInProb >= 0.70) {
        predLow = std::min(actLow, lastPrice) - minGap * 0.20;
    } else {
        const double downFactor = std::clamp(1.0 - (out.lowAlreadyInProb - 0.15) / 0.55, 0.20, 1.0);
        predLow = std::min(actLow, lastPrice) - down * downFactor;
        predLow = std::min(predLow, actLow - minGap * 0.25);
    }

    // 情景与催化剂演变（以盈利与防守指引为主）
    if (out.highAlreadyInProb >= 0.75) {
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
    if (n >= 6) {
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

} // namespace goldsdk
