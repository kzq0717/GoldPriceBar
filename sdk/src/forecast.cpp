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

    // 保留合理的长尾趋势扩展空间，避免日内波幅接近 ATR 时预算骤减为 0 导致预测线紧贴现价变成移动靶
    const double tailExpansion = std::max(0.15, (atr > 0.0 ? atr * 0.18 : rangeSoFar * 0.12) * timeScale);
    double remainingBudget = std::max(tailExpansion, expectedFullRange - rangeSoFar);
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

    // 短期微观动量判定（回溯最近15~30分钟数据）
    double shortSlope = 0.0;
    if (points.size() >= 2) {
        const int64_t targetLookbackMs = 20 * 60 * 1000;
        const int64_t curEpoch = points.back().epochMs;
        size_t lookbackIdx = points.size() - 1;
        for (size_t i = points.size(); i > 0; --i) {
            if (curEpoch - points[i - 1].epochMs >= targetLookbackMs) {
                lookbackIdx = i - 1;
                break;
            }
        }
        shortSlope = points.back().price - points[lookbackIdx].price;
    }

    const double pos = (rangeSoFar > 0.001) ? std::clamp((lastPrice - actLow) / rangeSoFar, 0.0, 1.0) : 0.5;
    const bool isOvernightPeak = (peakMin < 3 * 60) && (curMin >= 9 * 60);
    const double effectiveTimeSincePeak = isOvernightPeak ? std::max(0.0, static_cast<double>(curMin - 9 * 60)) : timeSincePeakMins;

    const bool isOvernightTrough = (troughMin < 3 * 60) && (curMin >= 9 * 60);
    const double effectiveTimeSinceTrough = isOvernightTrough ? std::max(0.0, static_cast<double>(curMin - 9 * 60)) : timeSinceTroughMins;

    // 动态评估高点是否已确立（以实战盈利防追高为主）：
    // 若欧/美主盘冲高后较长时间无法突破，且价格从高位回撤显著，高点确立概率急剧上升；
    // 反之若处于日内上行反弹浪中，严禁误报高点已现
    double hip = 0.10 + dayFraction * 0.30;
    if (highInPast && !isOvernightPeak) {
        hip += 0.10;
        if (effectiveTimeSincePeak >= 45.0) hip += 0.06;
        if (effectiveTimeSincePeak >= 90.0) hip += 0.08;
        if (effectiveTimeSincePeak >= 180.0) hip += 0.10;

        if ((pullback / rangeSoFar >= 0.30) || (pullbackAtrRatio >= 0.12)) hip += 0.08;
        if ((pullback / rangeSoFar >= 0.50) || (pullbackAtrRatio >= 0.25)) hip += 0.12;
    } else if (isOvernightPeak) {
        // 隔夜高点在日间交易中被视作上方常规阻力，而非日内已确立封顶；
        // 仅当日间已进入欧美盘且日间走势极度弱势时才考虑可能为全日最高点
        if (curMin >= 15 * 60 && (pullback / rangeSoFar >= 0.60) && shortSlope <= 0.0) {
            hip += 0.15;
        } else {
            hip = std::min(hip, 0.25);
        }
    } else {
        // 当前正贴近或处于日内最高点，正在冲高
        hip = std::min(hip, 0.20);
    }

    // 动量与趋势抑制因子（采用归一化 ATR 动量，杜绝0.02元微小噪点跳变）：
    const double effAtrVal = (atr > 0.0) ? atr : (lastPrice > 1000.0 ? 25.0 : 4.0);
    const double normSlope = shortSlope / effAtrVal;
    if (normSlope > 0.06 || pos >= 0.70 || (rebound > pullback * 1.3 && pos >= 0.55)) {
        // 上行进攻或强反弹途中：高点确立概率绝不能超过 0.30
        hip = std::min(hip, 0.30);
    } else if (normSlope > -0.03 && pos >= 0.50) {
        // 偏强震荡运行中
        hip = std::min(hip, 0.42);
    }

    // 交易时段硬约束（欧美盘主浪未开前，绝不大言不惭判定全日高点已现）
    if (curMin < 15 * 60) {
        // 亚盘交易时段：欧盘与美盘主浪均未开启，高点确立概率最高不超过 0.35
        hip = std::min(hip, 0.35);
    } else if (curMin < 20 * 60 + 30) {
        // 欧盘时段：美盘主力博弈段未开启，除非极其深幅回撤且持续走低，否则高点确立概率不超过 0.55
        if (pullback / rangeSoFar < 0.60 || shortSlope > 0.0) {
            hip = std::min(hip, 0.50);
        } else {
            hip = std::min(hip, 0.60);
        }
    }
    out.highAlreadyInProb = std::clamp(hip, 0.05, 0.95);

    // 动态评估低点是否已确立（顺势防守与支撑验证）：
    double lip = 0.10 + dayFraction * 0.30;
    if (lowInPast && !isOvernightTrough) {
        lip += 0.10;
        if (effectiveTimeSinceTrough >= 45.0) lip += 0.06;
        if (effectiveTimeSinceTrough >= 90.0) lip += 0.08;
        if (effectiveTimeSinceTrough >= 180.0) lip += 0.10;

        if ((rebound / rangeSoFar >= 0.30) || (reboundAtrRatio >= 0.12)) lip += 0.08;
        if ((rebound / rangeSoFar >= 0.50) || (reboundAtrRatio >= 0.25)) lip += 0.12;
    } else if (isOvernightTrough) {
        if (curMin >= 15 * 60 && (rebound / rangeSoFar >= 0.60) && shortSlope >= 0.0) {
            lip += 0.15;
        } else {
            lip = std::min(lip, 0.25);
        }
    } else {
        // 当前正贴近或处于日内最低点，正在探底下杀
        lip = std::min(lip, 0.20);
    }

    // 动量与趋势抑制因子（采用归一化 ATR 动量）：
    if (normSlope < -0.06 || pos <= 0.30 || (pullback > rebound * 1.3 && pos <= 0.45)) {
        // 杀跌寻底途中：低点确立概率绝不能超过 0.30，严防诱多接飞刀
        lip = std::min(lip, 0.30);
    } else if (normSlope < 0.03 && pos <= 0.50) {
        lip = std::min(lip, 0.42);
    }

    // 交易时段硬约束
    if (curMin < 15 * 60) {
        lip = std::min(lip, 0.35);
    } else if (curMin < 20 * 60 + 30) {
        if (rebound / rangeSoFar < 0.60 || shortSlope < 0.0) {
            lip = std::min(lip, 0.50);
        } else {
            lip = std::min(lip, 0.60);
        }
    }
    out.lowAlreadyInProb = std::clamp(lip, 0.05, 0.95);

    // 贝叶斯混合时间概率与归一化
    auto probs = blendPeakProbs(historicalPeakBucketCounts);
    redistributePastBuckets(probs, curBucket, highInPast);

    // 寻找未来的最高概率时间窗口（从当前时间桶往后推演，避免预测命中已过去的时段）
    int bestFutureBucket = -1;
    double bestFutureProb = -1.0;
    for (int i = curBucket; i < 48; ++i) {
        if (probs[static_cast<size_t>(i)] > bestFutureProb) {
            bestFutureProb = probs[static_cast<size_t>(i)];
            bestFutureBucket = i;
        }
    }
    if (bestFutureBucket >= 0) {
        out.peakWindowProb = bestFutureProb;
    }

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

    // 高低点时间窗口输出（严格 24 小时制）
    if (out.highAlreadyInProb >= 0.65) {
        out.predHighTimeWindow = formatWindow((peakMin / 30) * 30,
                                              ((peakMin / 30) + 1) * 30,
                                              "高点大概率已现");
    } else if (bestFutureBucket >= 0) {
        std::string tag = "高点高概率";
        if (bestFutureBucket >= 41 && bestFutureBucket <= 45) {
            tag = "美盘主浪高峰";
        } else if (bestFutureBucket >= 31 && bestFutureBucket <= 38) {
            tag = "欧盘脉冲高峰";
        } else if (bestFutureBucket >= 18 && bestFutureBucket <= 21) {
            tag = "亚盘早段高点";
        }
        out.predHighTimeWindow = formatWindow(bestFutureBucket * 30,
                                              (bestFutureBucket + 1) * 30,
                                              tag.c_str());
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

    // 锚定基准全日高低区间预测（彻底解决随动态价格频繁波动问题）：
    // 全日极值属于宏观日线级别锚点，基于基准价与波动率分布形成坚实锚定，严禁随1分钟微波拉风箱
    const double minGap = std::max(lastPrice * 0.0008, 0.15);
    const double basePrice = (prevClose > 0.0) ? prevClose : points.front().price;

    double upRatio = 0.52;
    double downRatio = 0.48;
    if (prevClose > 0.0) {
        const double gap = (basePrice - prevClose) / prevClose;
        if (gap > 0.003) { upRatio = 0.58; downRatio = 0.42; }
        else if (gap < -0.003) { upRatio = 0.42; downRatio = 0.58; }
    }

    const double fullBudget = std::max(expectedFullRange, rangeSoFar + effAtrVal * 0.25);
    const double anchorHigh = basePrice + fullBudget * upRatio;
    const double anchorLow  = basePrice - fullBudget * downRatio;

    // predHigh 必然 >= actHigh（绝不低于日内已出现最高，亦不低于现价），具有单调性：
    // 未突破 anchorHigh 时平稳锚定于 anchorHigh；日内实际冲高突破后，顺势抬升上移，绝不因短线回调而反向缩水
    double predHigh = std::max(actHigh + minGap * 0.5, anchorHigh);
    if (out.highAlreadyInProb >= 0.85 && dayFraction >= 0.80) {
        // 仅在尾盘且极高确立概率时才理性收敛贴近今高阻力
        predHigh = actHigh + minGap * 0.5;
    } else if (actHigh > anchorHigh) {
        const double extend = std::max(minGap * 2.0, effAtrVal * 0.20 * timeScale);
        predHigh = actHigh + extend;
    }
    if (lastPrice >= predHigh) {
        predHigh = lastPrice + std::max(minGap * 2.0, effAtrVal * 0.15);
    }
    out.remainingUpside = std::max(0.0, predHigh - lastPrice);

    // predLow 必然 <= actLow（绝不高于日内已出现最低，亦不高于现价），具有单调性：
    // 未跌破 anchorLow 时平稳锚定于 anchorLow；日内实际击穿后，顺势下延，绝不因短线反弹而反向推高
    double predLow = std::min(actLow - minGap * 0.5, anchorLow);
    if (out.lowAlreadyInProb >= 0.85 && dayFraction >= 0.80) {
        predLow = actLow - minGap * 0.5;
    } else if (actLow < anchorLow) {
        const double extend = std::max(minGap * 2.0, effAtrVal * 0.20 * timeScale);
        predLow = actLow - extend;
    }
    if (lastPrice <= predLow) {
        predLow = lastPrice - std::max(minGap * 2.0, effAtrVal * 0.15);
    }

    const bool isUptrendSoFar = (normSlope > 0.04) || (pos >= 0.65 && normSlope >= 0.0);
    const bool isDowntrendSoFar = (normSlope < -0.04) || (pos <= 0.35 && normSlope <= 0.0);

    // 情景与催化剂演变（以盈利与防守指引为主）
    if (out.highAlreadyInProb >= 0.65) {
        out.scenario = "日内高点大概率已在前期确立，反弹动能衰退，严禁追高，建议逢高止盈或防守保护多头利润";
        out.keyCatalyst = "脉冲见顶 + 动量衰竭";
        out.confidence = out.highAlreadyInProb;
    } else if (isUptrendSoFar && out.highAlreadyInProb <= 0.35) {
        out.scenario = "短线处于多头反弹与上行推进中，动能充沛，重点关注上方阻力位突破及欧美盘高点窗口";
        out.keyCatalyst = "多头推进 + 阻力突破";
        out.confidence = std::min(0.85, 0.50 + (remain * 0.3));
    } else if (isDowntrendSoFar && out.lowAlreadyInProb <= 0.35) {
        out.scenario = "短线处于弱势下探寻底中，切勿急于盲目抄底接飞刀，耐心等待支撑企稳";
        out.keyCatalyst = "空头下探 + 支撑测试";
        out.confidence = std::min(0.85, 0.50 + (remain * 0.3));
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

    // 动态波幅门槛：确保回踩企稳反弹至前高的利润空间能够有效覆盖卖出手续费摩擦成本（积存金买0卖0.4%）并保有安全边际
    const double minAmpRatio = (cfg.feeRate > 0.0005) ? std::max(cfg.feeRate * 1.5, 0.0055) : 0.0030;
    const double minAmpAbs = (curPx > 1000.0) ? 5.0 : 2.0; // 伦敦金(USD) vs 积存金(CNY)
    const double requiredAmp = std::max(curPx * minAmpRatio, minAmpAbs);

    auto enrichResult = [&](PatternResult& r) {
        if (!r.valid) return;
        const double fee = cfg.feeRate;
        const double zoneTol = std::clamp((cfg.atr > 0.0 ? cfg.atr : 4.0) * 0.12, 0.05, curPx * 0.003);

        if (r.bias == "看多突破" || r.bias == "强烈看多") {
            // 只有当价格触及推演高点阻力且已经显现滞涨遇阻/回落时，才提示防诱多；若动能强劲突破，保留看多突破
            const bool nearPredHigh = (cfg.predHigh > 0.0 && curPx >= cfg.predHigh - zoneTol);
            double recentHigh = curPx;
            const int scanN = std::min(15, (int)points.size());
            for (int i = (int)points.size() - scanN; i < (int)points.size(); ++i) {
                if (i >= 0 && points[i].price > recentHigh) recentHigh = points[i].price;
            }
            const bool stalledAtHigh = (recentHigh - curPx >= std::max(0.08, zoneTol * 0.8));

            if (nearPredHigh && stalledAtHigh && cfg.highAlreadyInProb >= 0.70) {
                r.bias = "高位蓄势";
                r.name += " (触及高点阻力·防诱多⚠️)";
                r.strategy = "现价短线冲高触及推演高点阻力区 (" + fmtPrice(cfg.predHigh > 0.0 ? cfg.predHigh : curPx)
                             + ") 出现遇阻滞涨，上方空间受限，严禁盲目追多！建议分批逢高止盈、落袋为安。";
                return;
            }

            const double target = (r.breakoutTarget > curPx) ? r.breakoutTarget : ((r.resistance > curPx) ? r.resistance : curPx * 1.015);
            const double stopLoss = (r.support > 0.0 && r.support < curPx) ? r.support : curPx * 0.99;
            const double buyCost = curPx; // 浙商积存金当前买入手续费为 0
            const double sellProceeds = (fee > 0.0) ? (target * (1.0 - fee)) : target;
            const double stopProceeds = (fee > 0.0) ? (stopLoss * (1.0 - fee)) : stopLoss;
            r.netReward = std::max(0.0, sellProceeds - buyCost);
            r.netRisk = std::max(0.05, buyCost - stopProceeds);
            r.rrr = (r.netRisk > 0.05) ? std::max(0.0, r.netReward / r.netRisk) : 0.0;
            const double atrDist = (cfg.atr > 0.0) ? (cfg.atr * (curPx > 1000.0 ? 1.8 : 1.5)) : (curPx * 0.005);
            r.atrTrailingStop = std::max(stopLoss, curPx - atrDist);

            // 严格实战正收益过滤：扣除卖出手续费(0.4%)后的净收益必须保有安全边际，盈亏比 >= 1.4:1
            const double minProfitReq = (fee > 0.0) ? std::max(1.2, curPx * fee * 0.45) : 1.0;
            if (r.netReward < minProfitReq || (r.rrr > 0.0 && r.rrr < 1.4)) {
                r.bias = "多头蓄势";
                r.strategy += " 【注意：扣除卖出手续费(0.4%)后净盈亏比仅 " + fmtPrice(r.rrr) + ":1 (预期净利 " + fmtPrice(r.netReward) + "元/g)，空间微薄不足以稳妥覆盖成本，建议观望等待更优回踩。】";
                r.atrTrailingStop = 0.0;
            }
        } else if (r.bias == "看空破位" || r.bias == "强烈看空") {
            const bool nearPredLow = (cfg.predLow > 0.0 && curPx <= cfg.predLow + zoneTol);
            double recentLow = curPx;
            const int scanN = std::min(15, (int)points.size());
            for (int i = (int)points.size() - scanN; i < (int)points.size(); ++i) {
                if (i >= 0 && points[i].price < recentLow) recentLow = points[i].price;
            }
            const bool bouncedAtLow = (curPx - recentLow >= std::max(0.08, zoneTol * 0.8));

            if (nearPredLow && bouncedAtLow && cfg.lowAlreadyInProb >= 0.70) {
                r.bias = "低位企稳";
                r.name += " (触及低点支撑·防杀跌⚠️)";
                r.strategy = "现价下探至当日推演极值低点关键支撑区 (" + fmtPrice(cfg.predLow > 0.0 ? cfg.predLow : curPx)
                             + ") 并显现止跌企稳迹象，杀跌动能衰竭，切勿盲目恐慌杀跌，密切关注企稳低吸机会。";
                return;
            }

            const double target = (r.breakoutTarget > 0.0 && r.breakoutTarget < curPx) ? r.breakoutTarget : ((r.support > 0.0 && r.support < curPx) ? r.support : curPx * 0.985);
            const double stopLoss = (r.resistance > curPx) ? r.resistance : curPx * 1.01;
            r.netReward = (fee > 0.0) ? (curPx * (1.0 - fee) - target) : (curPx - target);
            r.netRisk = (fee > 0.0) ? (stopLoss - curPx * (1.0 - fee)) : (stopLoss - curPx);
            r.rrr = (r.netRisk > 0.05) ? std::max(0.0, r.netReward / r.netRisk) : 0.0;
            const double atrDist = (cfg.atr > 0.0) ? (cfg.atr * (curPx > 1000.0 ? 1.8 : 1.5)) : (curPx * 0.005);
            r.atrTrailingStop = curPx + atrDist;
        } else {
            // 震荡/蓄势状态下不设紧贴现价的单边多头追踪止损，避免误触发
            r.atrTrailingStop = 0.0;
        }

        // 若具体形态未指定拐点，自动根据形态多空偏向在近期窗口提取局部极值拐点
        if (r.pivotEpochMs <= 0 && !points.empty()) {
            const bool isBull = (r.bias == "看多突破" || r.bias == "强烈看多"
                                 || r.bias == "多头蓄势" || r.bias == "震荡向上"
                                 || r.bias == "低位企稳" || r.bias.find("多") != std::string::npos);
            const int lookback = std::min(static_cast<int>(points.size()), 60);
            const int startIdx = static_cast<int>(points.size()) - lookback;
            int bestIdx = static_cast<int>(points.size()) - 1;
            double bestVal = points[static_cast<size_t>(bestIdx)].price;
            for (int k = startIdx; k < static_cast<int>(points.size()); ++k) {
                const double p = points[static_cast<size_t>(k)].price;
                if (isBull) {
                    if (p < bestVal) { bestVal = p; bestIdx = k; }
                } else {
                    if (p > bestVal) { bestVal = p; bestIdx = k; }
                }
            }
            r.pivotEpochMs = points[static_cast<size_t>(bestIdx)].epochMs;
            r.pivotPrice = bestVal;
        }
    };

    auto returnRes = [&](PatternResult r) -> PatternResult {
        enrichResult(r);
        return r;
    };

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
        if (pbMin <= fib50 + breakBuffer && pbMin >= fib618 - breakBuffer * 2.0) {
            // 情况 A：黄金口袋企稳反弹（有效从低点回弹并站稳 0.618 关键位上方，且留足防抖裕量）
            if (curPx >= pbMin + breakBuffer * 0.8 && curPx >= fib618 + breakBuffer * 0.3 && curPx <= fib382 + breakBuffer) {
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
            // 情况 B：正在黄金口袋支撑区回踩测试蓄势（未达到确立突破/反弹前，保持多头蓄势观察，杜绝秒级多空翻转）
            else if (curPx >= fib618 - breakBuffer * 1.5 && curPx <= fib50 + breakBuffer) {
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
        // 情况 C：明确跌破 0.618 黄金口袋最后防守位（必须有充足迟滞裕量，杜绝临界点2-3秒多空反复翻转）
        if (!hasFibCandidate && pbMin < fib618 - breakBuffer * 1.5 && curPx < fib618 - breakBuffer * 1.5 && curPx >= bestBullLowPx - breakBuffer) {
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

    if (hasFibCandidate) {
        enrichResult(fibCandidate);
    }

    if (peaks.size() < 2 || troughs.size() < 2) {
        if (hasFibCandidate)
            return returnRes(fibCandidate);
        res.type = PatternType::None;
        res.name = "震荡整理";
        res.bias = "震荡";
        res.strategy = "日内波幅平缓，暂无典型几何形态，关注日内极值支撑与阻力。";
        res.valid = true;
        return returnRes(res);
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
                return returnRes(res);
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
                return returnRes(res);
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
                return returnRes(res);
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
                return returnRes(res);
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
                return returnRes(res);
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
                return returnRes(res);
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
                return returnRes(res);
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
                return returnRes(res);
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
            return returnRes(res);
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
            return returnRes(res);
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
        return returnRes(res);
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
        return returnRes(res);
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
        return returnRes(res);
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
        return returnRes(res);
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
        return returnRes(res);
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
        return returnRes(res);
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
        return returnRes(res);
    } else if (peakSlope < -slopeThreshold && troughSlope < -slopeThreshold) {
        res.type = PatternType::ChannelDown;
        res.name = "下降通道 (Channel Down)";
        res.bias = "震荡向下";
        res.resistance = curUpper;
        res.support = curLower;
        res.breakoutTarget = curLower - priceRange * 0.5;
        res.strategy = "处于弱势下降通道运行，逢反弹受阻通道上轨可适度防守。";
        res.valid = true;
        return returnRes(res);
    }

    if (hasFibCandidate) {
        return returnRes(fibCandidate);
    }

    res.type = PatternType::None;
    res.name = "箱体震荡整理";
    res.bias = "震荡整理";
    res.resistance = std::max(p2.price, curUpper);
    res.support = std::min(t2.price, curLower);
    res.strategy = "处于箱体区间震荡，可高抛低吸或等待突破方向明朗。";
    res.valid = true;
    return returnRes(res);
}

ConfluenceDecision ForecastEngine::evaluateDecision(
    const std::vector<IntradayPoint>& points,
    const DecisionConfig& cfg)
{
    ConfluenceDecision res;
    if (points.empty())
        return res;

    const double curPx = cfg.currentPrice > 0.0 ? cfg.currentPrice : points.back().price;
    if (curPx <= 0.0)
        return res;

    res.valid = true;
    const double effAtr = cfg.atr > 0.2 ? cfg.atr : 4.0;

    // -------------------------------------------------------------
    // 1) 亚盘基准箱体计算 (Asian Range: 09:00 - 14:30 CST)
    // -------------------------------------------------------------
    double aHigh = 0.0, aLow = 0.0;
    int aCount = 0;
    for (const auto& pt : points) {
        if (pt.price <= 0.0) continue;
        const int m = minuteOfDayFromEpochMs(pt.epochMs);
        if (m >= 540 && m <= 870) { // 09:00 ~ 14:30 CST
            if (aCount == 0) {
                aHigh = pt.price;
                aLow = pt.price;
            } else {
                aHigh = std::max(aHigh, pt.price);
                aLow = std::min(aLow, pt.price);
            }
            aCount++;
        }
    }
    if (aCount >= 10 && aHigh > aLow) {
        res.hasAsianBox = true;
        res.asianHigh = aHigh;
        res.asianLow = aLow;
    }

    const int curMin = minuteOfDayFromEpochMs(points.back().epochMs);
    const bool isPostAsian = (curMin > 870 || curMin < 540); // 14:30 之后或次日凌晨

    if (res.hasAsianBox && isPostAsian) {
        if (curPx >= res.asianHigh + 0.30) {
            res.asianBreakoutUp = true;
        } else if (curPx <= res.asianLow - 0.30) {
            res.asianBreakoutDown = true;
        }
    }

    // -------------------------------------------------------------
    // 2) 因子 1 打分：多日中大趋势 (0 ~ 30 分)
    // -------------------------------------------------------------
    int fTrend = 15;
    if (cfg.multiDayTrend.valid) {
        switch (cfg.multiDayTrend.bias) {
        case TrendBias::StrongBull: fTrend = 30; break;
        case TrendBias::MildBull:   fTrend = 24; break;
        case TrendBias::Range:      fTrend = 15; break;
        case TrendBias::MildBear:   fTrend = 8;  break;
        case TrendBias::StrongBear: fTrend = 2;  break;
        }
        if (cfg.multiDayTrend.rsi14 > 76.0) fTrend = std::max(0, fTrend - 4);
        else if (cfg.multiDayTrend.rsi14 < 28.0) fTrend = std::min(30, fTrend + 4);
    }
    res.trendScore = fTrend;

    // -------------------------------------------------------------
    // 3) 因子 2 打分：时段规律与亚盘突破 (0 ~ 20 分)
    // -------------------------------------------------------------
    int fSession = 10;
    if (res.hasAsianBox) {
        if (!isPostAsian) {
            // 亚盘构筑基准箱体阶段
            fSession = 10;
        } else {
            // 欧盘/美盘时段
            if (res.asianBreakoutUp) {
                fSession = 20; // 欧美盘强势向上突破亚盘高点
            } else if (res.asianBreakoutDown) {
                fSession = 1;  // 欧美盘跌破亚盘低点
            } else {
                // 处于亚盘箱体内震荡测试
                if (curPx >= res.asianHigh - 0.35) fSession = 14;
                else if (curPx <= res.asianLow + 0.35) fSession = 6;
                else fSession = 10;
            }
        }
    }
    // 美盘高潮段 (20:30 - 24:00 CST: 1230 ~ 1440 min)
    if (curMin >= 1230) {
        if (fSession >= 14) fSession = std::min(20, fSession + 2);
        else if (fSession <= 8) fSession = std::max(0, fSession - 2);
    }
    res.sessionScore = fSession;

    // -------------------------------------------------------------
    // 4) 因子 3 打分：均值回归与超买超卖 / 波动带 (0 ~ 25 分)
    // -------------------------------------------------------------
    int fMean = 12;
    double sumP = 0.0;
    for (const auto& p : points) sumP += p.price;
    const double vwap = sumP / static_cast<double>(points.size());

    const double predH = cfg.hasForecast ? cfg.predHigh : 0.0;
    const double predL = cfg.hasForecast ? cfg.predLow : 0.0;

    double dHigh = points.front().price;
    double dLow = points.front().price;
    for (const auto& pt : points) {
        if (pt.price > 0.0) {
            dHigh = std::max(dHigh, pt.price);
            dLow = std::min(dLow, pt.price);
        }
    }
    const double rangeSoFar = std::max(0.01, dHigh - dLow);
    const double zoneTol = std::clamp(effAtr * 0.12, 0.05, std::max(0.08, rangeSoFar * 0.20));
    const double reboundFromLow = curPx - dLow;
    const double pullbackFromHigh = dHigh - curPx;
    const double minTurn = std::max(0.08, std::min(effAtr * 0.15, rangeSoFar * 0.18));

    if (predL > 0.0 && curPx <= predL + zoneTol) {
        if (reboundFromLow >= minTurn) {
            fMean = 24; // 触及低点支撑且企稳反弹转折，杀跌动能衰竭
        } else {
            fMean = 14; // 触及低点但仍在下探寻底中，防守观察
        }
    } else if (predH > 0.0 && curPx >= predH - zoneTol) {
        if (pullbackFromHigh >= minTurn) {
            fMean = 4;  // 触及高点阻力且冲高遇阻回落，抛压显现
        } else {
            fMean = 16; // 正在强势冲高中测试阻力，顺势偏强
        }
    } else {
        const double diff = curPx - vwap;
        if (diff < -1.5 * effAtr) {
            fMean = 21; // 深度回踩超卖，随时企稳反抽
        } else if (diff > 1.8 * effAtr) {
            fMean = 5;  // 短线严重偏离均价超买
        } else if (diff >= 0.0 && diff <= 0.8 * effAtr) {
            fMean = 16; // 依托分时均线健康多头运行
        } else if (diff < 0.0 && diff >= -0.8 * effAtr) {
            fMean = 11; // 均线下方弱势承压
        }
    }
    res.meanReversionScore = fMean;

    // -------------------------------------------------------------
    // 5) 因子 4 打分：形态与斐波那契共振 (0 ~ 25 分)
    // -------------------------------------------------------------
    int fPattern = 12;
    if (cfg.pattern.valid) {
        if (cfg.pattern.type == PatternType::FibonacciGoldenPocket) {
            if (cfg.pattern.bias == "强烈看多" || cfg.pattern.bias == "看多突破")
                fPattern = 25;
            else if (cfg.pattern.bias == "多头蓄势")
                fPattern = 19;
            else if (cfg.pattern.bias == "看空破位")
                fPattern = 4;
            else
                fPattern = 12;
        } else if (cfg.pattern.bias == "强烈看多" || cfg.pattern.bias == "看多突破") {
            fPattern = 24;
        } else if (cfg.pattern.bias == "多头蓄势") {
            fPattern = 17;
        } else if (cfg.pattern.bias == "强烈看空" || cfg.pattern.bias == "看空破位") {
            fPattern = 3;
        } else if (cfg.pattern.bias == "空头蓄势" || cfg.pattern.bias == "震荡向下") {
            fPattern = 8;
        }
    }
    res.patternScore = fPattern;

    // -------------------------------------------------------------
    // 6) 综合共振打分与决策判定 (0 ~ 100)
    // -------------------------------------------------------------
    res.score = std::clamp(fTrend + fSession + fMean + fPattern, 0, 100);

    if (res.score >= 80) {
        res.action = DecisionAction::StrongBuy;
        res.actionLabel = "强力买入/加仓";
        res.actionColor = "#52c41a";
    } else if (res.score >= 65) {
        res.action = DecisionAction::BuyOnDips;
        res.actionLabel = "逢低分批建仓";
        res.actionColor = "#73d13d";
    } else if (res.score <= 25) {
        res.action = DecisionAction::StrongSell;
        res.actionLabel = "果断卖出/防守";
        res.actionColor = "#ff4d4f";
    } else if (res.score <= 40) {
        res.action = DecisionAction::SellOnRallies;
        res.actionLabel = "冲高减仓/止盈";
        res.actionColor = "#ff7875";
    } else {
        res.action = DecisionAction::WaitAndWatch;
        res.actionLabel = "震荡蓄势·观望";
        res.actionColor = "#faad14";
    }

    // -------------------------------------------------------------
    // 7) 关键执行挂单与止损价格计算
    // -------------------------------------------------------------
    double s = 1e9;
    if (cfg.pattern.support > 0.0) s = std::min(s, cfg.pattern.support);
    if (predL > 0.0) s = std::min(s, predL);
    if (res.hasAsianBox) s = std::min(s, res.asianLow);
    if (s > 1e8) s = curPx - 1.2 * effAtr;

    double r = 0.0;
    if (cfg.pattern.resistance > 0.0) r = std::max(r, cfg.pattern.resistance);
    if (predH > 0.0) r = std::max(r, predH);
    if (res.hasAsianBox) r = std::max(r, res.asianHigh);
    if (r <= 0.0) r = curPx + 1.2 * effAtr;

    res.conservativeBuy = std::min(curPx, s);
    res.aggressiveBuy = r + 0.35;
    res.takeProfit1 = r;
    res.takeProfit2 = cfg.pattern.breakoutTarget > r ? cfg.pattern.breakoutTarget : (r + 1.2 * effAtr);
    res.stopLoss = s - 0.6 * effAtr;

    // -------------------------------------------------------------
    // 8) 手续费与安全边际过滤（浙商积存金买入0手续费，卖出0.4%）
    // -------------------------------------------------------------
    res.roundTripFee = curPx * cfg.feeRate;
    const double targetP = std::max(res.takeProfit1, curPx + 1.2 * effAtr);
    const double buyCost = curPx; // 买入手续费为 0
    const double sellProceeds = (cfg.feeRate > 0.0) ? (targetP * (1.0 - cfg.feeRate)) : targetP;
    const double stopProceeds = (cfg.feeRate > 0.0) ? (res.stopLoss * (1.0 - cfg.feeRate)) : res.stopLoss;
    res.netReward = std::max(0.0, sellProceeds - buyCost);
    res.netRisk = std::max(0.6, buyCost - stopProceeds);
    res.rrr = res.netRisk > 0.0 ? (res.netReward / res.netRisk) : 0.0;
    const double minRewardEdge = (cfg.feeRate > 0.0) ? std::max(1.2, curPx * cfg.feeRate * 0.45) : 1.0;
    res.feeEdgeSatisfied = (res.netReward >= minRewardEdge && res.rrr >= 1.4);

    if ((res.action == DecisionAction::StrongBuy || res.action == DecisionAction::BuyOnDips) && !res.feeEdgeSatisfied) {
        res.action = DecisionAction::WaitAndWatch;
        res.actionLabel = "空间受限·观望";
        res.actionColor = "#faad14";
    }

    // -------------------------------------------------------------
    // 9) 摘要与多因子归因阐述
    // -------------------------------------------------------------
    char buf[256];
    std::snprintf(buf, sizeof(buf), "综合共振得分: %d分 (%s) | 趋势%d·时段%d·波动%d·形态%d",
                  res.score, res.actionLabel.c_str(), fTrend, fSession, fMean, fPattern);
    res.summary = std::string(buf);

    if (!res.feeEdgeSatisfied) {
        res.summary += " [⚠️扣费净利空间微薄，防手续费损耗已转观望]";
    }

    std::string rat = "【多因子量化共振评测详情】\n";
    rat += "1. 多日趋势维度 (" + std::to_string(fTrend) + "/30分)：" + (cfg.multiDayTrend.valid ? cfg.multiDayTrend.label() : "震荡中性") + "\n";
    if (res.hasAsianBox) {
        rat += "2. 亚欧盘时段维度 (" + std::to_string(fSession) + "/20分)：亚盘箱体 [" + fmtPrice(res.asianLow) + " ~ " + fmtPrice(res.asianHigh) + "]";
        if (res.asianBreakoutUp) rat += " (欧美盘放量上破🚀)\n";
        else if (res.asianBreakoutDown) rat += " (欧美盘破位下挫⚠️)\n";
        else rat += " (区间内震荡整理)\n";
    } else {
        rat += "2. 亚欧盘时段维度 (" + std::to_string(fSession) + "/20分)：数据积累中\n";
    }
    rat += "3. 波动与极值维度 (" + std::to_string(fMean) + "/25分)：现价 vs 分时均价 " + fmtPrice(vwap);
    if (predH > 0 && predL > 0) rat += " (今日推演区间 " + fmtPrice(predL) + " ~ " + fmtPrice(predH) + ")";
    rat += "\n";
    rat += "4. 形态与黄金口袋 (" + std::to_string(fPattern) + "/25分)：" + (cfg.pattern.valid ? cfg.pattern.name : "箱体震荡整理") + "\n\n";

    rat += "【挂单执行点位参考】\n";
    rat += "• 稳健低吸买点: " + fmtPrice(res.conservativeBuy) + " 元/g\n";
    rat += "• 突破顺势买点: " + fmtPrice(res.aggressiveBuy) + " 元/g\n";
    rat += "• 目标止盈一 (前高/阻力): " + fmtPrice(res.takeProfit1) + " 元/g\n";
    rat += "• 目标止盈二 (拓展目标): " + fmtPrice(res.takeProfit2) + " 元/g\n";
    rat += "• 硬防守止损位: " + fmtPrice(res.stopLoss) + " 元/g\n\n";

    rat += "【成本与风控测算】\n";
    rat += "• 预计卖出手续费(0.4% · 买入0费率): -" + fmtPrice(res.roundTripFee) + " 元/g\n";
    rat += "• 扣费后理论净利润: " + fmtPrice(res.netReward) + " 元/g\n";
    rat += "• 理论盈亏比 (RRR): " + fmtPrice(res.rrr) + ":1\n";
    if (!res.feeEdgeSatisfied) {
        rat += "• 提示: 当前反弹空间扣除0.4%卖出手续费后净利微薄或盈亏比不达标，切忌高频频繁追单！\n";
    }

    res.rationale = rat;
    return res;
}

} // namespace goldsdk
