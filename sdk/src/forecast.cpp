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
    const std::int64_t dayMs = 24LL * 3600 * 1000;
    std::int64_t mod = epochMs % dayMs;
    if (mod < 0)
        mod += dayMs;
    return static_cast<int>(mod / 60000);
}

std::string formatWindow(int startMin, int endMin, const char* tag)
{
    const int sh = startMin / 60, sm = startMin % 60;
    const int eh = endMin / 60, em = endMin % 60;
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%02d:%02d - %02d:%02d (%s)", sh, sm, eh, em, tag);
    return std::string(buf);
}

std::vector<double> priorPeakWeights()
{
    std::vector<double> w(48, 0.5);
    for (int i = 18; i < 30; ++i) w[static_cast<size_t>(i)] = 0.6;  // 09-15 低
    for (int i = 30; i < 40; ++i) w[static_cast<size_t>(i)] = 1.2;  // 15-20 中
    for (int i = 40; i < 48; ++i) w[static_cast<size_t>(i)] = 2.0;  // 20-24 高
    for (int i = 0; i < 18; ++i)  w[static_cast<size_t>(i)] = 0.8;  // 00-09
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

    double predHigh = std::max(actHigh, lastPrice) + up;
    double predLow = std::min(actLow, lastPrice) - down;
    const double minGap = std::max(lastPrice * 0.0008, 0.12);
    predHigh = std::max(predHigh, actHigh + minGap * 0.25);
    predLow = std::min(predLow, actLow - minGap * 0.25);

    // 峰值时间概率
    int peakMin = minuteOfDayFromEpochMs(points.front().epochMs);
    double peakPx = points.front().price;
    for (const auto& p : points) {
        if (p.price >= peakPx) {
            peakPx = p.price;
            peakMin = minuteOfDayFromEpochMs(p.epochMs);
        }
    }
    const int curMin = minuteOfDayFromEpochMs(points.back().epochMs);
    const int curBucket = std::clamp(curMin / 30, 0, 47);
    const int peakBucket = std::clamp(peakMin / 30, 0, 47);
    const bool highInPast = peakBucket < curBucket;

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
        out.predHighTimeWindow = formatWindow(out.peakBuckets.front().startMinute,
                                              out.peakBuckets.front().endMinute,
                                              "高点高概率");
    }

    // 低点窗口：用对称启发式（先验中偏低活跃时段）
    if (dayFraction < 0.45) {
        out.predLowTimeWindow = "10:30 - 14:00 (亚盘/午间)";
    } else if (dayFraction < 0.70) {
        out.predLowTimeWindow = "16:00 - 17:30 (欧盘二次下探)";
    } else {
        out.predLowTimeWindow = "20:30 - 21:30 (数据发布下影)";
    }

    double hip = dayFraction * 0.50;
    if (highInPast)
        hip += 0.25;
    if (peakBucket < curBucket - 1)
        hip += 0.10;
    out.highAlreadyInProb = std::clamp(hip, 0.05, 0.95);
    out.remainingUpside = std::max(0.0, predHigh - std::max(actHigh, lastPrice));

    if (out.highAlreadyInProb >= 0.75) {
        out.scenario = "高点或已在盘中出现，剩余时段以防守与锁定利润为主";
        out.keyCatalyst = "已实现高点 + 时间衰减";
        out.confidence = out.highAlreadyInProb;
        // 高点已现：预测高贴近今高
        predHigh = std::max(actHigh, lastPrice) + minGap * 0.15;
    } else if (out.peakWindowProb >= 0.10 && dayFraction < 0.85) {
        out.scenario = "仍处冲高窗口，关注高概率时段放量突破";
        out.keyCatalyst = "欧美盘交叠资金 + 宏观数据";
        out.confidence = std::min(0.85, 0.35 + out.peakWindowProb + remain * 0.25);
    } else if (dayFraction >= 0.85) {
        out.scenario = "尾盘收敛，波动收窄，避免追高";
        out.keyCatalyst = "隔夜头寸了结";
        out.confidence = 0.45 + out.highAlreadyInProb * 0.25;
    } else {
        out.scenario = "区间震荡蓄势，欧/美盘或给出方向";
        out.keyCatalyst = "关键支撑阻力与时段切换";
        out.confidence = 0.42;
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
