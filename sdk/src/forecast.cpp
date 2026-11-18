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

const char* MultiDayTrend::regimeLabel(MarketRegime r)
{
    switch (r) {
    case MarketRegime::Trending:      return "趋势推进";
    case MarketRegime::HighVolCrisis: return "高波冲击";
    default:                          return "低波震荡";
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
    for (int i = 0; i < 5; ++i) w[static_cast<size_t>(i)] = 1.0;   // 00:00 - 02:30 美盘后半程
    for (int i = 5; i < 18; ++i) w[static_cast<size_t>(i)] = 0.35; // 02:30 - 09:00 隔夜清淡与休市
    for (int i = 18; i < 21; ++i) w[static_cast<size_t>(i)] = 1.8; // 09:00 - 10:30 国内早盘定价脉冲
    for (int i = 21; i < 28; ++i) w[static_cast<size_t>(i)] = 0.55;// 10:30 - 14:00 亚盘午间收敛
    for (int i = 28; i < 31; ++i) w[static_cast<size_t>(i)] = 0.9; // 14:00 - 15:30 亚尾/欧盘初动
    for (int i = 31; i < 39; ++i) w[static_cast<size_t>(i)] = 1.2; // 15:30 - 19:30 欧盘主交易段
    for (int i = 39; i < 41; ++i) w[static_cast<size_t>(i)] = 1.4; // 19:30 - 20:30 国内夜盘开盘
    for (int i = 41; i < 46; ++i) w[static_cast<size_t>(i)] = 2.2; // 20:30 - 23:00 美盘核心博弈
    for (int i = 46; i < 48; ++i) w[static_cast<size_t>(i)] = 1.3; // 23:00 - 24:00 美盘主浪延续

    double s = std::accumulate(w.begin(), w.end(), 0.0);
    if (s > 0.0) {
        for (double& v : w) v /= s;
    }
    return w;
}

std::vector<double> priorVarianceWeights()
{
    // 分时段方差先验分布（按黄金日内波动率聚类规律：09:00-10:30 与 20:30-23:00 占显著更高方差）
    std::vector<double> w(48, 0.5);
    for (int i = 0; i < 5; ++i)   w[static_cast<size_t>(i)] = 1.1;  // 00:00 - 02:30
    for (int i = 5; i < 18; ++i)  w[static_cast<size_t>(i)] = 0.25; // 02:30 - 09:00
    for (int i = 18; i < 21; ++i) w[static_cast<size_t>(i)] = 1.9;  // 09:00 - 10:30
    for (int i = 21; i < 28; ++i) w[static_cast<size_t>(i)] = 0.45; // 10:30 - 14:00
    for (int i = 28; i < 31; ++i) w[static_cast<size_t>(i)] = 0.95; // 14:00 - 15:30
    for (int i = 31; i < 39; ++i) w[static_cast<size_t>(i)] = 1.25; // 15:30 - 19:30
    for (int i = 39; i < 41; ++i) w[static_cast<size_t>(i)] = 1.5;  // 19:30 - 20:30
    for (int i = 41; i < 46; ++i) w[static_cast<size_t>(i)] = 2.6;  // 20:30 - 23:00
    for (int i = 46; i < 48; ++i) w[static_cast<size_t>(i)] = 1.4;  // 23:00 - 24:00

    double s = std::accumulate(w.begin(), w.end(), 0.0);
    if (s > 0.0) {
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
    if (N <= 0)
        return prior;
    const double alpha = std::min(0.85, static_cast<double>(N) / 60.0);
    std::vector<double> hist(48, 0.0);
    for (int i = 0; i < 48; ++i) {
        hist[static_cast<size_t>(i)] =
            static_cast<double>(std::max(0, histCounts[static_cast<size_t>(i)])) / static_cast<double>(N);
    }
    std::vector<double> out(48);
    for (int i = 0; i < 48; ++i) {
        out[static_cast<size_t>(i)] =
            alpha * hist[static_cast<size_t>(i)] + (1.0 - alpha) * prior[static_cast<size_t>(i)];
    }
    double s = std::accumulate(out.begin(), out.end(), 0.0);
    if (s > 0.0) {
        for (double& v : out) v /= s;
    }
    return out;
}

std::vector<double> blendVarianceWeights(const std::vector<double>& histVarWeights)
{
    auto prior = priorVarianceWeights();
    if (histVarWeights.size() != 48)
        return prior;
    double sumW = 0.0;
    for (double v : histVarWeights) {
        if (std::isfinite(v) && v > 0.0)
            sumW += v;
    }
    if (sumW <= 1e-12)
        return prior;

    std::vector<double> out(48, 0.0);
    for (int i = 0; i < 48; ++i) {
        const double hv = (std::isfinite(histVarWeights[static_cast<size_t>(i)]) && histVarWeights[static_cast<size_t>(i)] > 0.0)
                              ? (histVarWeights[static_cast<size_t>(i)] / sumW)
                              : 0.0;
        out[static_cast<size_t>(i)] = 0.65 * hv + 0.35 * prior[static_cast<size_t>(i)];
    }
    const double s = std::accumulate(out.begin(), out.end(), 0.0);
    if (s > 0.0) {
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
    if (remain > 1e-12 && mass > 0.0) {
        for (int i = currentBucket; i < 48; ++i)
            p[static_cast<size_t>(i)] += mass * (p[static_cast<size_t>(i)] / remain);
    } else if (mass > 0.0) {
        p[47] += mass;
    }
    double s = std::accumulate(p.begin(), p.end(), 0.0);
    if (s > 0.0) {
        for (double& v : p) v /= s;
    }
}

// 标准 Wilder 平滑 RSI(14)：首段使用前 period 个涨跌均值，后续按 (prev*(P-1)+cur)/P 递推
double rsiWilder(const std::vector<double>& closes, int period)
{
    const int n = static_cast<int>(closes.size());
    if (n < 3 || period <= 0)
        return 50.0;

    const int effPeriod = std::min(period, n - 1);
    double avgGain = 0.0;
    double avgLoss = 0.0;
    for (int i = 1; i <= effPeriod; ++i) {
        const double d = closes[static_cast<size_t>(i)] - closes[static_cast<size_t>(i - 1)];
        if (d >= 0.0) avgGain += d;
        else avgLoss -= d;
    }
    avgGain /= static_cast<double>(effPeriod);
    avgLoss /= static_cast<double>(effPeriod);

    for (int i = effPeriod + 1; i < n; ++i) {
        const double d = closes[static_cast<size_t>(i)] - closes[static_cast<size_t>(i - 1)];
        const double g = (d >= 0.0) ? d : 0.0;
        const double l = (d < 0.0) ? -d : 0.0;
        avgGain = (avgGain * static_cast<double>(effPeriod - 1) + g) / static_cast<double>(effPeriod);
        avgLoss = (avgLoss * static_cast<double>(effPeriod - 1) + l) / static_cast<double>(effPeriod);
    }

    if (avgLoss < 1e-12) {
        return (avgGain < 1e-12) ? 50.0 : 100.0;
    }
    const double rs = avgGain / avgLoss;
    return 100.0 - (100.0 / (1.0 + rs));
}

double computeEma(const std::vector<double>& values, int period)
{
    if (values.empty() || period <= 0)
        return 0.0;
    const double k = 2.0 / (static_cast<double>(period) + 1.0);
    double ema = values.front();
    for (size_t i = 1; i < values.size(); ++i) {
        ema = values[i] * k + ema * (1.0 - k);
    }
    return ema;
}

double computeWilderAtrFromBars(const std::vector<DailyBar>& bars, int period)
{
    const int n = static_cast<int>(bars.size());
    if (n < 2 || period <= 0)
        return 0.0;

    std::vector<double> trs;
    trs.reserve(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) {
        const auto& b = bars[static_cast<size_t>(i)];
        if (b.high <= 0.0 || b.low <= 0.0)
            continue;
        double tr = std::max(0.0, b.high - b.low);
        if (i > 0 && bars[static_cast<size_t>(i - 1)].close > 0.0) {
            const double prevC = bars[static_cast<size_t>(i - 1)].close;
            tr = std::max({tr, std::abs(b.high - prevC), std::abs(b.low - prevC)});
        }
        trs.push_back(tr);
    }
    if (trs.empty())
        return 0.0;

    const int m = static_cast<int>(trs.size());
    const int initLen = std::min(period, m);
    double atr = 0.0;
    for (int i = 0; i < initLen; ++i) atr += trs[static_cast<size_t>(i)];
    atr /= static_cast<double>(initLen);

    for (int i = initLen; i < m; ++i) {
        atr = (atr * static_cast<double>(initLen - 1) + trs[static_cast<size_t>(i)]) / static_cast<double>(initLen);
    }
    return atr;
}

double computeTwap(const std::vector<IntradayPoint>& points)
{
    if (points.empty())
        return 0.0;
    if (points.size() == 1)
        return points.front().price;

    double weightedSum = 0.0;
    double totalWeight = 0.0;
    for (size_t i = 0; i < points.size(); ++i) {
        if (points[i].price <= 0.0)
            continue;
        double dtMin = 1.0;
        if (i + 1 < points.size() && points[i + 1].epochMs > points[i].epochMs) {
            dtMin = static_cast<double>(points[i + 1].epochMs - points[i].epochMs) / 60000.0;
        } else if (i > 0 && points[i].epochMs > points[i - 1].epochMs) {
            dtMin = static_cast<double>(points[i].epochMs - points[i - 1].epochMs) / 60000.0;
        }
        // 截断跨休市断层权重（最多计 10 分钟），避免停盘时段垄断均价
        const double w = std::clamp(dtMin, 0.1, 10.0);
        weightedSum += points[i].price * w;
        totalWeight += w;
    }
    if (totalWeight <= 1e-9) {
        double s = 0.0;
        int c = 0;
        for (const auto& p : points) {
            if (p.price > 0.0) { s += p.price; ++c; }
        }
        return c > 0 ? (s / c) : 0.0;
    }
    return weightedSum / totalWeight;
}

struct RegressionLine {
    double slope = 0.0;
    double intercept = 0.0;
    double r2 = 0.0;
    bool valid = false;
};

template <typename SwingT>
RegressionLine fitSwingRegression(const std::vector<SwingT>& swings, size_t maxCount)
{
    RegressionLine out;
    if (swings.size() < 2)
        return out;
    const size_t cnt = std::min(maxCount, swings.size());
    const size_t start = swings.size() - cnt;

    double sx = 0.0, sy = 0.0;
    for (size_t i = start; i < swings.size(); ++i) {
        sx += static_cast<double>(swings[i].index);
        sy += swings[i].price;
    }
    const double mx = sx / static_cast<double>(cnt);
    const double my = sy / static_cast<double>(cnt);

    double sxx = 0.0, sxy = 0.0, syy = 0.0;
    for (size_t i = start; i < swings.size(); ++i) {
        const double dx = static_cast<double>(swings[i].index) - mx;
        const double dy = swings[i].price - my;
        sxx += dx * dx;
        sxy += dx * dy;
        syy += dy * dy;
    }
    if (sxx <= 1e-12)
        return out;

    out.slope = sxy / sxx;
    out.intercept = my - out.slope * mx;
    if (syy > 1e-12) {
        out.r2 = std::clamp((sxy * sxy) / (sxx * syy), 0.0, 1.0);
    } else {
        out.r2 = 1.0;
    }
    out.valid = true;
    return out;
}

} // namespace

KalmanTrendState ForecastEngine::kalmanTrend(const std::vector<IntradayPoint>& points, double atr)
{
    KalmanTrendState st;
    if (points.size() < 2) {
        if (!points.empty() && points.back().price > 0.0) {
            st.filteredPrice = points.back().price;
            st.valid = true;
        }
        return st;
    }

    // 寻找首有效价格
    size_t firstValid = 0;
    while (firstValid < points.size() && points[firstValid].price <= 0.0)
        ++firstValid;
    if (firstValid >= points.size())
        return st;

    const double refPx = points.back().price > 0.0 ? points.back().price : points[firstValid].price;
    const double effAtr = (atr > 0.05) ? atr : (refPx > 1000.0 ? refPx * 0.011 : refPx * 0.008);

    // 测量噪声 R 与过程噪声基准 Q（按 ATR 自适应缩放，兼顾人民币克价与美元盎司）
    const double sigmaMeas = std::max(0.03, effAtr * 0.030);
    const double rMeas = sigmaMeas * sigmaMeas;
    const double qPosBase = std::pow(std::max(0.008, effAtr * 0.012), 2);
    const double qVelBase = std::pow(std::max(0.001, effAtr * 0.0035), 2);

    double x0 = points[firstValid].price;
    double x1 = 0.0; // velocity (price per minute)
    double p00 = rMeas * 2.0, p01 = 0.0;
    double p10 = 0.0, p11 = std::pow(std::max(0.01, effAtr * 0.02), 2);
    int64_t prevEpoch = points[firstValid].epochMs;

    for (size_t i = firstValid + 1; i < points.size(); ++i) {
        const double z = points[i].price;
        if (z <= 0.0)
            continue;

        double rawDtMin = 1.0;
        if (points[i].epochMs > prevEpoch && prevEpoch > 0) {
            rawDtMin = static_cast<double>(points[i].epochMs - prevEpoch) / 60000.0;
        }
        prevEpoch = points[i].epochMs;

        // 遇休市/断线长间隔 (>25分钟) 时，阻尼历史速度并放大状态不确定度，防止跳空制造虚假斜率
        if (rawDtMin > 25.0) {
            x1 *= 0.20;
            p00 += rMeas * 4.0;
            p11 += qVelBase * 12.0;
        }
        const double dt = std::clamp(rawDtMin, 0.05, 15.0);

        // Predict: F = [[1, dt], [0, 0.985^dt]]
        const double decay = std::pow(0.985, dt);
        const double px0 = x0 + x1 * dt;
        const double px1 = x1 * decay;

        const double fp00 = p00 + dt * (p10 + p01) + dt * dt * p11 + qPosBase * dt;
        const double fp01 = (p01 + dt * p11) * decay;
        const double fp10 = (p10 + dt * p11) * decay;
        const double fp11 = p11 * decay * decay + qVelBase * dt;

        // Update with measurement z = [1, 0] * x
        const double innov = z - px0;
        const double s = fp00 + rMeas;
        if (s <= 1e-12)
            continue;
        const double k0 = fp00 / s;
        const double k1 = fp10 / s;

        x0 = px0 + k0 * innov;
        x1 = px1 + k1 * innov;

        p00 = std::max(1e-9, (1.0 - k0) * fp00);
        p01 = (1.0 - k0) * fp01;
        p10 = fp10 - k1 * fp00;
        p11 = std::max(1e-10, fp11 - k1 * fp01);
    }

    st.filteredPrice = x0;
    st.velocityPerMin = x1;
    st.stateStd = std::sqrt(std::max(0.0, p00));
    const double velScale = std::max(1e-4, std::sqrt(std::max(0.0, p11)) + effAtr * 0.0025);
    st.velocityZ = std::clamp(x1 / velScale, -5.0, 5.0);
    st.valid = true;
    return st;
}

VolatilityEstimate ForecastEngine::estimateVolatility(
    const std::vector<DailyBar>& bars,
    const std::vector<IntradayPoint>& intradayPoints,
    double fallbackAtr)
{
    VolatilityEstimate est;

    // 1) 基于日线 OHLC 计算 Garman-Klass 波动率与真实波幅序列，用于 HAR-RV 建模
    std::vector<double> dailyRanges;
    std::vector<double> gkVols;
    dailyRanges.reserve(bars.size());
    gkVols.reserve(bars.size());

    double lastPrice = 0.0;
    for (size_t i = 0; i < bars.size(); ++i) {
        const auto& b = bars[i];
        if (b.close > 0.0)
            lastPrice = b.close;
        if (b.high <= 0.0 || b.low <= 0.0 || b.high < b.low)
            continue;

        const double o = (b.open > 0.0) ? b.open : b.close;
        const double c = (b.close > 0.0) ? b.close : (0.5 * (b.high + b.low));
        double tr = b.high - b.low;
        double overnightJumpVar = 0.0;
        if (i > 0 && bars[i - 1].close > 0.0) {
            const double prevC = bars[i - 1].close;
            tr = std::max({tr, std::abs(b.high - prevC), std::abs(b.low - prevC)});
            if (o > 0.0) {
                const double logGap = std::log(o / prevC);
                overnightJumpVar = logGap * logGap;
            }
        }

        double gkVar = 0.0;
        if (b.high > b.low && o > 0.0 && c > 0.0) {
            const double hl = std::log(b.high / b.low);
            const double co = std::log(c / o);
            // Garman-Klass (1980) 极值方差估计量
            gkVar = std::max(0.0, 0.5 * hl * hl - (2.0 * std::log(2.0) - 1.0) * co * co);
        }
        const double totalLogVar = gkVar + 0.5 * overnightJumpVar;
        // 几何布朗运动 E[(H-L)/P] ≈ sqrt(8/pi) * sigma ≈ 1.5958 * sigma
        const double gkPriceRange = (c > 0.0) ? (1.5958 * c * std::sqrt(totalLogVar)) : tr;
        const double blendedRange = (tr > 0.0 && gkPriceRange > 0.0)
                                        ? (0.55 * tr + 0.45 * gkPriceRange)
                                        : std::max(tr, gkPriceRange);
        dailyRanges.push_back(blendedRange);
        gkVols.push_back(gkPriceRange);
    }

    if (!intradayPoints.empty() && intradayPoints.back().price > 0.0) {
        lastPrice = intradayPoints.back().price;
    }

    auto meanTail = [](const std::vector<double>& v, size_t k) -> double {
        if (v.empty()) return 0.0;
        const size_t n = std::min(k, v.size());
        double s = 0.0;
        for (size_t i = v.size() - n; i < v.size(); ++i) s += v[i];
        return s / static_cast<double>(n);
    };

    if (!dailyRanges.empty()) {
        const double rv1 = dailyRanges.back();
        const double rv5 = meanTail(dailyRanges, 5);
        const double rv22 = meanTail(dailyRanges, 22);
        // Corsi (2009) HAR-RV 凸组合：同时刻画短期脉冲、周度聚集与月度长记忆
        est.harRvRange = 0.25 * rv1 + 0.45 * rv5 + 0.30 * rv22;
        est.garmanKlassVol = gkVols.empty() ? est.harRvRange : meanTail(gkVols, 10);
        est.regimeVolRatio = (rv22 > 1e-9) ? std::clamp(rv5 / rv22, 0.5, 2.5) : 1.0;
        est.effectiveAtr = (fallbackAtr > 0.0) ? (0.35 * fallbackAtr + 0.65 * est.harRvRange) : est.harRvRange;
        est.valid = est.harRvRange > 0.0;
    } else if (fallbackAtr > 0.0) {
        est.harRvRange = fallbackAtr;
        est.garmanKlassVol = fallbackAtr;
        est.effectiveAtr = fallbackAtr;
        est.valid = true;
    }

    // 2) 日内已实现波动率微调：若当日已有 >= 20 个有效分钟级变化，用已实现方差率补充实时波动感知
    if (intradayPoints.size() >= 20 && lastPrice > 0.0) {
        double sumSqDiff = 0.0;
        double spanMins = 0.0;
        for (size_t i = 1; i < intradayPoints.size(); ++i) {
            const double p0 = intradayPoints[i - 1].price;
            const double p1 = intradayPoints[i].price;
            if (p0 <= 0.0 || p1 <= 0.0) continue;
            const double dtMin = (intradayPoints[i].epochMs > intradayPoints[i - 1].epochMs)
                                     ? static_cast<double>(intradayPoints[i].epochMs - intradayPoints[i - 1].epochMs) / 60000.0
                                     : 1.0;
            if (dtMin > 25.0) continue; // 忽略停盘跨段跳变
            const double d = p1 - p0;
            sumSqDiff += d * d;
            spanMins += std::clamp(dtMin, 0.05, 10.0);
        }
        if (spanMins >= 20.0 && sumSqDiff > 0.0) {
            // 国内积存金活跃交易约 870 分钟 (09:00-23:30)，按扩散缩放推算全日波动率
            const double fullDayMins = (lastPrice > 1000.0) ? 1380.0 : 870.0;
            const double sigmaIntradayDay = std::sqrt(sumSqDiff * (fullDayMins / spanMins));
            // 日内极大极小差约 1.60 * 日内标准差（高频率下比值略高于日线，保守取 1.45）
            est.intradayRv = sigmaIntradayDay * 1.45;
            if (est.harRvRange > 0.0) {
                const double wIntraday = std::clamp(spanMins / 360.0, 0.05, 0.25);
                est.harRvRange = (1.0 - wIntraday) * est.harRvRange + wIntraday * std::clamp(est.intradayRv, est.harRvRange * 0.45, est.harRvRange * 2.0);
            } else {
                est.harRvRange = est.intradayRv;
                est.effectiveAtr = est.intradayRv;
                est.valid = est.harRvRange > 0.0;
            }
        }
    }

    return est;
}

double ForecastEngine::computeConformalScale(
    const std::vector<double>& recentPredHighs,
    const std::vector<double>& recentPredLows,
    const std::vector<double>& recentActHighs,
    const std::vector<double>& recentActLows,
    double targetCoverage)
{
    const size_t n = std::min({recentPredHighs.size(), recentPredLows.size(),
                               recentActHighs.size(), recentActLows.size()});
    if (n < 5)
        return 1.0;

    std::vector<double> ratios;
    ratios.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        const double ph = recentPredHighs[i];
        const double pl = recentPredLows[i];
        const double ah = recentActHighs[i];
        const double al = recentActLows[i];
        if (ph <= pl || ah < al || ph <= 0.0 || pl <= 0.0 || ah <= 0.0 || al <= 0.0)
            continue;
        const double predHalf = std::max(0.10, 0.5 * (ph - pl));
        const double mid = 0.5 * (ph + pl);
        const double reqHalf = std::max(ah - mid, mid - al);
        ratios.push_back(std::clamp(reqHalf / predHalf, 0.5, 2.2));
    }
    if (ratios.size() < 5)
        return 1.0;

    std::sort(ratios.begin(), ratios.end());
    const double q = std::clamp(targetCoverage, 0.50, 0.95);
    const size_t idx = std::min(ratios.size() - 1,
                                static_cast<size_t>(std::ceil(q * static_cast<double>(ratios.size()))) - 1);
    const double empiricalQuantile = ratios[idx];
    // 样本量小时向 1.0 收缩，避免少数异常样本过度放大区间
    const double alpha = std::min(1.0, static_cast<double>(ratios.size()) / 20.0);
    return std::clamp(1.0 + alpha * (empiricalQuantile - 1.0), 0.78, 1.45);
}

std::vector<IntradayPoint> ForecastEngine::resamplePoints(const std::vector<IntradayPoint>& points, int intervalMinutes)
{
    if (points.size() < 4 || intervalMinutes <= 0)
        return points;

    const int64_t bucketMs = static_cast<int64_t>(intervalMinutes) * 60000LL;
    std::vector<IntradayPoint> out;
    out.reserve(points.size());

    int64_t curBucket = -1;
    for (const auto& pt : points) {
        if (pt.price <= 0.0)
            continue;
        const int64_t b = (pt.epochMs > 0) ? (pt.epochMs / bucketMs) : static_cast<int64_t>(out.size());
        if (b != curBucket || out.empty()) {
            out.push_back(pt);
            curBucket = b;
        } else {
            // 保留同一时间桶内的最新采样价，同时若出现剧烈极值尖峰则保留桶内代表极值，避免失真
            out.back().epochMs = pt.epochMs;
            out.back().price = pt.price;
        }
    }
    return (out.size() >= 12) ? out : points;
}

DayRangeForecast ForecastEngine::dayRange(const std::vector<IntradayPoint>& points,
                                          double actHigh,
                                          double actLow,
                                          double dayFraction,
                                          double atr,
                                          double prevClose,
                                          const std::vector<int>& historicalPeakBucketCounts)
{
    ForecastContext ctx;
    ctx.actHigh = actHigh;
    ctx.actLow = actLow;
    ctx.dayFraction = dayFraction;
    ctx.atr = atr;
    ctx.prevClose = prevClose;
    ctx.historicalPeakBucketCounts = historicalPeakBucketCounts;
    return dayRangeWithContext(points, ctx);
}

DayRangeForecast ForecastEngine::dayRangeWithContext(const std::vector<IntradayPoint>& points,
                                                     const ForecastContext& ctx)
{
    DayRangeForecast out;
    if (points.size() < 3)
        return out;

    const double lastPrice = points.back().price;
    if (lastPrice <= 0.0)
        return out;

    double actHigh = ctx.actHigh;
    double actLow = ctx.actLow;
    if (actHigh <= 0.0 || actLow <= 0.0) {
        actHigh = actLow = points.front().price;
        for (const auto& p : points) {
            if (p.price > 0.0) {
                actHigh = std::max(actHigh, p.price);
                actLow = std::min(actLow, p.price);
            }
        }
    }
    if (actHigh < actLow)
        std::swap(actHigh, actLow);

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

    // 1) 波动率估计（HAR-RV + Garman-Klass + 日内 RV）与分时段方差曲线
    const auto volEst = estimateVolatility(ctx.dailyBars, points, ctx.atr);
    const double effAtrVal = (volEst.valid && volEst.effectiveAtr > 0.0)
                                 ? volEst.effectiveAtr
                                 : ((ctx.atr > 0.0) ? ctx.atr : (lastPrice > 1000.0 ? 25.0 : 4.0));

    // 市场状态判定
    MarketRegime regime = ctx.multiDayTrend.valid ? ctx.multiDayTrend.regime : MarketRegime::LowVolRange;
    if (volEst.regimeVolRatio >= 1.35) {
        regime = MarketRegime::HighVolCrisis;
    } else if (ctx.multiDayTrend.valid && ctx.multiDayTrend.efficiencyRatio >= 0.42 && regime == MarketRegime::LowVolRange) {
        regime = MarketRegime::Trending;
    }
    out.regime = regime;

    const double dayFraction = std::clamp(ctx.dayFraction, 0.0, 1.0);
    const int curMin = minuteOfDayFromEpochMs(points.back().epochMs);
    const int curBucket = std::clamp(curMin / 30, 0, 47);

    // 使用 48 桶时段方差曲线计算剩余未释放波动率比例（替代粗糙的 sqrt(1 - dayFraction)）
    const auto varWeights = blendVarianceWeights(ctx.historicalBucketVarWeights);
    double remainVarFrac = 0.0;
    {
        const double minIntoBucket = static_cast<double>(curMin % 30) / 30.0;
        remainVarFrac += varWeights[static_cast<size_t>(curBucket)] * std::clamp(1.0 - minIntoBucket, 0.05, 1.0);
        for (int b = curBucket + 1; b < 48; ++b) {
            remainVarFrac += varWeights[static_cast<size_t>(b)];
        }
    }
    // 针对 09:00-23:30 积存金交易时钟若出现次日 00:00-02:30 样本的边界保护
    if (dayFraction >= 0.90) {
        remainVarFrac = std::min(remainVarFrac, std::max(0.03, 1.0 - dayFraction));
    }
    const double remain = std::clamp(remainVarFrac, 0.03, 1.0);
    const double timeScale = std::sqrt(remain);
    out.remainVolRatio = timeScale;

    // 2) 全日预期振幅（状态自适应上限 + 共形区间校准）
    const double pctFloor = lastPrice * (regime == MarketRegime::LowVolRange ? 0.0040 : 0.0055);
    const double pctCeil = lastPrice * (regime == MarketRegime::HighVolCrisis ? 0.0420 : (regime == MarketRegime::Trending ? 0.0300 : 0.0220));

    double expectedFullRange = 0.0;
    if (volEst.valid && volEst.harRvRange > 0.0) {
        expectedFullRange = std::clamp(volEst.harRvRange * (0.90 + 0.25 * (1.0 - timeScale)), pctFloor, pctCeil);
    } else if (ctx.atr > 0.0) {
        expectedFullRange = std::clamp(ctx.atr * (0.85 + 0.35 * (1.0 - timeScale)), pctFloor, pctCeil);
    } else {
        expectedFullRange = std::max({rangeSoFar * 1.25, stdev * 4.2, pctFloor});
        expectedFullRange = std::min(expectedFullRange, pctCeil);
    }
    const double conformalMult = std::clamp(ctx.conformalScale, 0.78, 1.45);
    expectedFullRange = std::clamp(expectedFullRange * conformalMult, pctFloor, pctCeil);
    out.harExpectedRange = expectedFullRange;

    // 3) 2D 卡尔曼滤波：提取无毛刺瞬时价格与速度
    const auto kf = kalmanTrend(points, effAtrVal);
    out.kalmanSlopePerMin = kf.valid ? kf.velocityPerMin : 0.0;
    out.kalmanVelocityZ = kf.valid ? kf.velocityZ : 0.0;

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

    const int peakBucket = std::clamp(peakMin / 30, 0, 47);
    const int troughBucket = std::clamp(troughMin / 30, 0, 47);
    const bool highInPast = peakBucket < curBucket;
    const bool lowInPast = troughBucket < curBucket;

    const int64_t timeSincePeakMs = points.back().epochMs - points[static_cast<size_t>(peakIndex)].epochMs;
    const double timeSincePeakMins = std::max(0.0, static_cast<double>(timeSincePeakMs) / 60000.0);
    const double pullback = std::max(0.0, peakPx - lastPrice);
    const double pullbackAtrRatio = pullback / std::max(0.1, effAtrVal);

    const int64_t timeSinceTroughMs = points.back().epochMs - points[static_cast<size_t>(troughIndex)].epochMs;
    const double timeSinceTroughMins = std::max(0.0, static_cast<double>(timeSinceTroughMs) / 60000.0);
    const double rebound = std::max(0.0, lastPrice - troughPx);
    const double reboundAtrRatio = rebound / std::max(0.1, effAtrVal);

    // 短线微观动量融合：卡尔曼速度 Z-score + 20分钟窗口净位移
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
    const double rawNormSlope = shortSlope / std::max(0.1, effAtrVal);
    // 卡尔曼瞬时速度与 20 分钟位移融合动量，既抗单 Tick 跳变又不滞后
    const double normSlope = kf.valid
                                 ? (0.60 * std::clamp(kf.velocityZ * 0.08, -0.4, 0.4) + 0.40 * rawNormSlope)
                                 : rawNormSlope;

    const double pos = (rangeSoFar > 0.001) ? std::clamp((lastPrice - actLow) / rangeSoFar, 0.0, 1.0) : 0.5;
    const bool isOvernightPeak = (peakMin < 3 * 60) && (curMin >= 9 * 60);
    const double effectiveTimeSincePeak = isOvernightPeak ? std::max(0.0, static_cast<double>(curMin - 9 * 60)) : timeSincePeakMins;

    const bool isOvernightTrough = (troughMin < 3 * 60) && (curMin >= 9 * 60);
    const double effectiveTimeSinceTrough = isOvernightTrough ? std::max(0.0, static_cast<double>(curMin - 9 * 60)) : timeSinceTroughMins;

    // 4) 高点已确立概率（结合剩余时段方差占比 1 - remain，越接近尾盘/主力方差已释放完，确立概率越高）
    const double varianceSpent = 1.0 - remain;
    double hip = 0.08 + varianceSpent * 0.34;
    if (highInPast && !isOvernightPeak) {
        hip += 0.10;
        if (effectiveTimeSincePeak >= 45.0) hip += 0.06;
        if (effectiveTimeSincePeak >= 90.0) hip += 0.08;
        if (effectiveTimeSincePeak >= 180.0) hip += 0.10;

        if ((pullback / rangeSoFar >= 0.30) || (pullbackAtrRatio >= 0.15)) hip += 0.08;
        if ((pullback / rangeSoFar >= 0.50) || (pullbackAtrRatio >= 0.30)) hip += 0.12;
    } else if (isOvernightPeak) {
        if (curMin >= 15 * 60 && (pullback / rangeSoFar >= 0.60) && normSlope <= 0.0) {
            hip += 0.15;
        } else {
            hip = std::min(hip, 0.25);
        }
    } else {
        hip = std::min(hip, 0.20);
    }

    if (normSlope > 0.06 || pos >= 0.70 || (rebound > pullback * 1.3 && pos >= 0.55)) {
        hip = std::min(hip, 0.30);
    } else if (normSlope > -0.03 && pos >= 0.50) {
        hip = std::min(hip, 0.42);
    }

    if (curMin < 15 * 60) {
        hip = std::min(hip, 0.35);
    } else if (curMin < 20 * 60 + 30) {
        if (pullback / rangeSoFar < 0.60 || normSlope > 0.0)
            hip = std::min(hip, 0.50);
        else
            hip = std::min(hip, 0.60);
    }
    out.highAlreadyInProb = std::clamp(hip, 0.05, 0.95);

    // 低点已确立概率
    double lip = 0.08 + varianceSpent * 0.34;
    if (lowInPast && !isOvernightTrough) {
        lip += 0.10;
        if (effectiveTimeSinceTrough >= 45.0) lip += 0.06;
        if (effectiveTimeSinceTrough >= 90.0) lip += 0.08;
        if (effectiveTimeSinceTrough >= 180.0) lip += 0.10;

        if ((rebound / rangeSoFar >= 0.30) || (reboundAtrRatio >= 0.15)) lip += 0.08;
        if ((rebound / rangeSoFar >= 0.50) || (reboundAtrRatio >= 0.30)) lip += 0.12;
    } else if (isOvernightTrough) {
        if (curMin >= 15 * 60 && (rebound / rangeSoFar >= 0.60) && normSlope >= 0.0) {
            lip += 0.15;
        } else {
            lip = std::min(lip, 0.25);
        }
    } else {
        lip = std::min(lip, 0.20);
    }

    if (normSlope < -0.06 || pos <= 0.30 || (pullback > rebound * 1.3 && pos <= 0.45)) {
        lip = std::min(lip, 0.30);
    } else if (normSlope < 0.03 && pos <= 0.50) {
        lip = std::min(lip, 0.42);
    }

    if (curMin < 15 * 60) {
        lip = std::min(lip, 0.35);
    } else if (curMin < 20 * 60 + 30) {
        if (rebound / rangeSoFar < 0.60 || normSlope < 0.0)
            lip = std::min(lip, 0.50);
        else
            lip = std::min(lip, 0.60);
    }
    out.lowAlreadyInProb = std::clamp(lip, 0.05, 0.95);

    // 5) 贝叶斯混合时间概率与归一化
    auto probs = blendPeakProbs(ctx.historicalPeakBucketCounts);
    redistributePastBuckets(probs, curBucket, highInPast);

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

    if (out.highAlreadyInProb >= 0.65) {
        out.predHighTimeWindow = formatWindow((peakMin / 30) * 30,
                                              ((peakMin / 30) + 1) * 30,
                                              "高点大概率已现");
    } else if (bestFutureBucket >= 0) {
        std::string tag = "高点高概率";
        if (bestFutureBucket >= 41 && bestFutureBucket <= 45)      tag = "美盘主浪高峰";
        else if (bestFutureBucket >= 31 && bestFutureBucket <= 38) tag = "欧盘脉冲高峰";
        else if (bestFutureBucket >= 18 && bestFutureBucket <= 21) tag = "亚盘早段高点";
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
        if (curBucket < 28)      out.predLowTimeWindow = "15:00 - 16:30 (欧盘初探底)";
        else if (curBucket < 40) out.predLowTimeWindow = "20:30 - 21:30 (美盘洗盘探底)";
        else                     out.predLowTimeWindow = "22:30 - 24:00 (美盘后程支撑)";
    }

    // 6) 多因子非对称漂移 + 剩余预算融合区间预测（彻底修复原 up/down 死代码）
    const double minGap = std::max(lastPrice * 0.0008, 0.15);
    const double basePrice = (ctx.prevClose > 0.0) ? ctx.prevClose : points.front().price;

    double driftBias = 0.0;
    if (ctx.multiDayTrend.valid) {
        driftBias += ctx.multiDayTrend.score * 0.10; // ±0.10
    }
    if (ctx.prevClose > 0.0) {
        const double gap = (lastPrice - ctx.prevClose) / ctx.prevClose;
        driftBias += std::clamp(gap * 8.0, -0.06, 0.06);
    }
    if (kf.valid) {
        driftBias += std::clamp(kf.velocityZ * 0.02, -0.05, 0.05);
    }
    if (std::abs(ctx.crossMarketLeadSignal) > 0.1) {
        driftBias += std::clamp(ctx.crossMarketLeadSignal * 0.025, -0.05, 0.05);
    }
    if (std::abs(ctx.sentimentScore) > 0.1) {
        driftBias += std::clamp(ctx.sentimentScore * 0.012, -0.05, 0.05);
    }

    const double upRatio = std::clamp(0.50 + driftBias, 0.30, 0.70);
    const double downRatio = 1.0 - upRatio;

    const double tailExpansion = std::max(minGap * 1.5, effAtrVal * 0.16 * timeScale);
    const double fullBudget = std::max(expectedFullRange, rangeSoFar + tailExpansion);
    const double anchorHigh = basePrice + fullBudget * upRatio;
    const double anchorLow  = basePrice - fullBudget * downRatio;

    // 布朗运动反射原理分位数投影：由当前现价出发，结合剩余方差比例与共形校准因子构建 80% 联合覆盖极值带
    const double dailySigmaEst = std::max(minGap * 2.0, (expectedFullRange / std::max(0.75, conformalMult)) / 1.52);
    const double sigmaRem = dailySigmaEst * timeScale;
    const double qZ = 1.48 * conformalMult;
    const double upMult = std::clamp(1.0 + driftBias * 1.35, 0.62, 1.45);
    const double downMult = std::clamp(1.0 - driftBias * 1.35, 0.62, 1.45);

    const double reflHigh = lastPrice + qZ * sigmaRem * upMult;
    const double reflLow  = lastPrice - qZ * sigmaRem * downMult;

    // 日初适当参考前收中轴锚点，随着日内进展由现价反射分位数主导
    const double wAnchor = std::clamp(0.28 * (1.0 - dayFraction), 0.05, 0.28);
    const double projHigh = (1.0 - wAnchor) * reflHigh + wAnchor * anchorHigh;
    const double projLow  = (1.0 - wAnchor) * reflLow  + wAnchor * anchorLow;

    double predHigh = std::max(actHigh + minGap * 0.5, projHigh);
    if (out.highAlreadyInProb >= 0.85 && dayFraction >= 0.80) {
        predHigh = actHigh + minGap * 0.5;
    } else if (lastPrice >= predHigh) {
        predHigh = lastPrice + std::max(minGap * 2.0, sigmaRem * 0.35);
    }
    out.remainingUpside = std::max(0.0, predHigh - lastPrice);

    double predLow = std::min(actLow - minGap * 0.5, projLow);
    if (out.lowAlreadyInProb >= 0.85 && dayFraction >= 0.80) {
        predLow = actLow - minGap * 0.5;
    } else if (lastPrice <= predLow) {
        predLow = lastPrice - std::max(minGap * 2.0, sigmaRem * 0.35);
    }

    const bool isUptrendSoFar = (normSlope > 0.04) || (pos >= 0.65 && normSlope >= 0.0);
    const bool isDowntrendSoFar = (normSlope < -0.04) || (pos <= 0.35 && normSlope <= 0.0);

    if (out.highAlreadyInProb >= 0.65) {
        out.scenario = "日内高点大概率已在前期确立，反弹动能衰退，严禁追高，建议逢高止盈或防守保护多头利润";
        out.keyCatalyst = "脉冲见顶 + 动量衰竭";
        out.confidence = out.highAlreadyInProb;
    } else if (isUptrendSoFar && out.highAlreadyInProb <= 0.35) {
        out.scenario = "短线处于多头推进通道（卡尔曼斜率向上），动能充沛，关注上方阻力突破与欧美盘窗口";
        out.keyCatalyst = "卡尔曼多头推进 + 阻力测试";
        out.confidence = std::min(0.88, 0.50 + remain * 0.25 + (kf.valid ? std::min(0.10, std::abs(kf.velocityZ) * 0.03) : 0.0));
    } else if (isDowntrendSoFar && out.lowAlreadyInProb <= 0.35) {
        out.scenario = "短线处于弱势下探寻底中（卡尔曼斜率向下），切勿急于盲目抄底接飞刀，耐心等待支撑企稳";
        out.keyCatalyst = "空头下探 + 支撑测试";
        out.confidence = std::min(0.88, 0.50 + remain * 0.25 + (kf.valid ? std::min(0.10, std::abs(kf.velocityZ) * 0.03) : 0.0));
    } else if (out.lowAlreadyInProb >= 0.75 && dayFraction < 0.80) {
        out.scenario = "日内探底企稳，关键支撑有效，可顺多头趋势逢低关注低吸";
        out.keyCatalyst = "探底回升 + 支撑确认";
        out.confidence = out.lowAlreadyInProb;
    } else if (out.peakWindowProb >= 0.10 && dayFraction < 0.85) {
        out.scenario = "仍处潜在冲高窗口，若突破今高可顺势关注高概率时段";
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
        double s = 0.0;
        for (int i = n - period; i < n; ++i)
            s += closes[static_cast<size_t>(i)];
        return s / static_cast<double>(period);
    };
    t.ma5 = sma(5);
    t.ma10 = sma(std::min(10, n));
    t.ma20 = sma(std::min(20, n));
    t.ema5 = computeEma(closes, 5);
    t.ema10 = computeEma(closes, std::min(10, n));
    t.ema20 = computeEma(closes, std::min(20, n));
    t.rsi14 = rsiWilder(closes, 14);
    const double last = closes.back();

    // Kaufman 效率比 (ER)：衡量价格净位移 vs 总路径长度，用于区分“有效趋势”与“宽幅震荡”
    const int erWin = std::min(14, n - 1);
    double pathSum = 0.0;
    for (int i = n - erWin; i < n; ++i) {
        pathSum += std::abs(closes[static_cast<size_t>(i)] - closes[static_cast<size_t>(i - 1)]);
    }
    const double netDisp = std::abs(last - closes[static_cast<size_t>(n - 1 - erWin)]);
    t.efficiencyRatio = (pathSum > 1e-12) ? std::clamp(netDisp / pathSum, 0.0, 1.0) : 0.0;

    // 均线 + EMA + Wilder RSI + 动量打分
    double score = 0.0;
    score += (t.ema5 > t.ema10) ? 0.22 : -0.22;
    score += (t.ema10 > t.ema20) ? 0.22 : -0.22;
    score += (t.ma5 > t.ma20) ? 0.12 : -0.12;
    score += (last > t.ema5) ? 0.16 : -0.16;
    score += (last > t.ema20) ? 0.14 : -0.14;
    if (t.rsi14 > 55.0) score += 0.10;
    else if (t.rsi14 < 45.0) score -= 0.10;
    if (n >= 6 && closes[static_cast<size_t>(n - 6)] > 0.0) {
        const double r = (last / closes[static_cast<size_t>(n - 6)]) - 1.0;
        score += std::clamp(r * 8.0, -0.20, 0.20);
    }

    // 在低效率比（震荡市）下按 ER 连续衰减趋势分，避免无趋势横盘被均线微差误判为单边趋势
    const double erScale = (t.efficiencyRatio < 0.35)
                               ? std::clamp(t.efficiencyRatio / 0.35, 0.30, 1.0)
                               : 1.0;
    t.score = std::clamp(score * erScale, -1.0, 1.0);

    // 估测收盘波动率用于判断高波状态
    if (n >= 22) {
        double var5 = 0.0, var22 = 0.0;
        for (int i = n - 5; i < n; ++i) {
            if (closes[static_cast<size_t>(i - 1)] > 0.0) {
                const double r = (closes[static_cast<size_t>(i)] / closes[static_cast<size_t>(i - 1)]) - 1.0;
                var5 += r * r;
            }
        }
        for (int i = n - 21; i < n; ++i) {
            if (closes[static_cast<size_t>(i - 1)] > 0.0) {
                const double r = (closes[static_cast<size_t>(i)] / closes[static_cast<size_t>(i - 1)]) - 1.0;
                var22 += r * r;
            }
        }
        const double vol5 = std::sqrt(var5 / 5.0);
        const double vol22 = std::sqrt(var22 / 21.0);
        if (vol22 > 1e-9 && (vol5 / vol22) >= 1.38) {
            t.regime = MarketRegime::HighVolCrisis;
        } else if (t.efficiencyRatio >= 0.38 || (t.efficiencyRatio >= 0.25 && std::abs(t.score) >= 0.55)) {
            t.regime = MarketRegime::Trending;
        } else {
            t.regime = MarketRegime::LowVolRange;
        }
    } else {
        t.regime = (t.efficiencyRatio >= 0.40 || (t.efficiencyRatio >= 0.25 && std::abs(t.score) >= 0.55))
                       ? MarketRegime::Trending
                       : MarketRegime::LowVolRange;
    }

    if (t.score >= 0.55)       t.bias = TrendBias::StrongBull;
    else if (t.score >= 0.20)  t.bias = TrendBias::MildBull;
    else if (t.score <= -0.55) t.bias = TrendBias::StrongBear;
    else if (t.score <= -0.20) t.bias = TrendBias::MildBear;
    else                       t.bias = TrendBias::Range;
    t.valid = true;
    return t;
}

MultiDayTrend ForecastEngine::multiDayTrendFromBars(const std::vector<DailyBar>& bars)
{
    std::vector<double> closes;
    closes.reserve(bars.size());
    for (const auto& b : bars) {
        if (b.close > 0.0)
            closes.push_back(b.close);
    }
    auto t = multiDayTrend(closes);
    if (!t.valid)
        return t;

    t.atr14 = computeWilderAtrFromBars(bars, 14);
    const auto vol = estimateVolatility(bars, {}, t.atr14);
    if (vol.valid && vol.regimeVolRatio >= 1.35) {
        t.regime = MarketRegime::HighVolCrisis;
    }
    return t;
}

PatternResult ForecastEngine::identifyPattern(const std::vector<IntradayPoint>& rawPoints,
                                             const PatternConfig& cfg)
{
    PatternResult res;
    if (rawPoints.size() < 12)
        return res;

    // 1) 时间桶重采样（1分钟基准）：消除因轮询速率/断线重连造成的采样点密度差异，使几何形态具备时间尺度不变性
    std::vector<IntradayPoint> points = resamplePoints(rawPoints, 1);
    if (points.size() < 12)
        points = rawPoints;

    const int n = static_cast<int>(points.size());
    const double curPx = points.back().price;
    if (curPx <= 0.0)
        return res;

    const double effAtr = (cfg.atr > 0.05) ? cfg.atr : (curPx > 1000.0 ? curPx * 0.011 : curPx * 0.008);
    const double breakBuffer = std::clamp(curPx * 0.0003, 0.08, 0.25);

    struct SwingPoint {
        int index = 0;
        int64_t epochMs = 0;
        double price = 0.0;
    };
    std::vector<SwingPoint> peaks;
    std::vector<SwingPoint> troughs;

    // 2) ATR 自适应 ZigZag 拐点识别 + 局部窗口极值补充
    const double zigzagThresh = std::max(curPx * 0.0011, effAtr * 0.16);
    {
        int dir = 0; // +1=向上找峰, -1=向下找谷
        int extremeIdx = 0;
        double extremePx = points.front().price;
        int initMinIdx = 0, initMaxIdx = 0;
        double initMinPx = points.front().price, initMaxPx = points.front().price;
        for (int i = 1; i < n; ++i) {
            const double p = points[static_cast<size_t>(i)].price;
            if (p <= 0.0) continue;
            if (dir == 0) {
                if (p < initMinPx) {
                    initMinPx = p;
                    initMinIdx = i;
                }
                if (p > initMaxPx) {
                    initMaxPx = p;
                    initMaxIdx = i;
                }
                if (p - initMinPx >= zigzagThresh) {
                    troughs.push_back({initMinIdx, points[static_cast<size_t>(initMinIdx)].epochMs, initMinPx});
                    dir = 1;
                    extremeIdx = i;
                    extremePx = p;
                } else if (initMaxPx - p >= zigzagThresh) {
                    peaks.push_back({initMaxIdx, points[static_cast<size_t>(initMaxIdx)].epochMs, initMaxPx});
                    dir = -1;
                    extremeIdx = i;
                    extremePx = p;
                }
            } else if (dir == 1) {
                if (p >= extremePx) {
                    extremeIdx = i;
                    extremePx = p;
                } else if (extremePx - p >= zigzagThresh) {
                    peaks.push_back({extremeIdx, points[static_cast<size_t>(extremeIdx)].epochMs, extremePx});
                    dir = -1;
                    extremeIdx = i;
                    extremePx = p;
                }
            } else {
                if (p <= extremePx) {
                    extremeIdx = i;
                    extremePx = p;
                } else if (p - extremePx >= zigzagThresh) {
                    troughs.push_back({extremeIdx, points[static_cast<size_t>(extremeIdx)].epochMs, extremePx});
                    dir = 1;
                    extremeIdx = i;
                    extremePx = p;
                }
            }
        }
    }

    // 若 ZigZag 拐点太少（例如极窄幅震荡日），退回时间自适应窗口极值提取
    if (peaks.size() < 2 || troughs.size() < 2) {
        peaks.clear();
        troughs.clear();
        const int span = std::clamp(n / 25, 3, 8);
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
            if (isPeak) peaks.push_back({i, points[static_cast<size_t>(i)].epochMs, p});
            if (isTrough) troughs.push_back({i, points[static_cast<size_t>(i)].epochMs, p});
        }
    }

    const double minAmpRatio = (cfg.feeRate > 0.0005) ? std::max(cfg.feeRate * 1.5, 0.0055) : 0.0030;
    const double minAmpAbs = (curPx > 1000.0) ? 5.0 : 2.0;
    const double requiredAmp = std::max(curPx * minAmpRatio, minAmpAbs);

    auto enrichResult = [&](PatternResult& r) {
        if (!r.valid) return;
        const double fee = cfg.feeRate;
        const double zoneTol = std::clamp(effAtr * 0.12, 0.05, curPx * 0.003);

        if (r.bias == "看多突破" || r.bias == "强烈看多") {
            const bool nearPredHigh = (cfg.predHigh > 0.0 && curPx >= cfg.predHigh - zoneTol);
            double recentHigh = curPx;
            const int scanN = std::min(15, n);
            for (int i = n - scanN; i < n; ++i) {
                if (i >= 0 && points[static_cast<size_t>(i)].price > recentHigh)
                    recentHigh = points[static_cast<size_t>(i)].price;
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
            const double buyCost = curPx;
            const double sellProceeds = (fee > 0.0) ? (target * (1.0 - fee)) : target;
            const double stopProceeds = (fee > 0.0) ? (stopLoss * (1.0 - fee)) : stopLoss;
            r.netReward = std::max(0.0, sellProceeds - buyCost);
            r.netRisk = std::max(0.05, buyCost - stopProceeds);
            r.rrr = (r.netRisk > 0.05) ? std::max(0.0, r.netReward / r.netRisk) : 0.0;
            const double atrDist = effAtr * (curPx > 1000.0 ? 1.8 : 1.5);
            r.atrTrailingStop = std::max(stopLoss, curPx - atrDist);

            const double minProfitReq = (fee > 0.0) ? std::max(1.2, curPx * fee * 0.45) : 1.0;
            if (r.netReward < minProfitReq || (r.rrr > 0.0 && r.rrr < 1.4)) {
                r.bias = "多头蓄势";
                r.strategy += " 【注意：扣除卖出手续费后净盈亏比仅 " + fmtPrice(r.rrr) + ":1 (预期净利 " + fmtPrice(r.netReward) + ")，空间不足以稳妥覆盖成本，建议观望等待更优回踩。】";
                r.atrTrailingStop = 0.0;
            }
        } else if (r.bias == "看空破位" || r.bias == "强烈看空") {
            const bool nearPredLow = (cfg.predLow > 0.0 && curPx <= cfg.predLow + zoneTol);
            double recentLow = curPx;
            const int scanN = std::min(15, n);
            for (int i = n - scanN; i < n; ++i) {
                if (i >= 0 && points[static_cast<size_t>(i)].price < recentLow)
                    recentLow = points[static_cast<size_t>(i)].price;
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
            const double atrDist = effAtr * (curPx > 1000.0 ? 1.8 : 1.5);
            r.atrTrailingStop = curPx + atrDist;
        } else {
            r.atrTrailingStop = 0.0;
        }

        if (r.pivotEpochMs <= 0 && !points.empty()) {
            const bool isBull = (r.bias == "看多突破" || r.bias == "强烈看多"
                                 || r.bias == "多头蓄势" || r.bias == "震荡向上"
                                 || r.bias == "低位企稳" || r.bias.find("多") != std::string::npos);
            const int lookback = std::min(n, 60);
            const int startIdx = n - lookback;
            int bestIdx = n - 1;
            double bestVal = points[static_cast<size_t>(bestIdx)].price;
            for (int k = startIdx; k < n; ++k) {
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

    for (const auto& pk : peaks) {
        if (pk.index > n - 2) continue;
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

    const int fibLookback = std::min(n, 300);
    int scanMaxIdx = -1;
    double scanMaxPx = -1e9;
    for (int i = n - fibLookback + 4; i <= n - 2; ++i) {
        if (i < 0) continue;
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
    (void)bestBullLowIdx;

    if (bestBullAmp >= requiredAmp) {
        res.fib50 = bestBullHighPx - 0.500 * bestBullAmp;
        res.fib618 = bestBullHighPx - 0.618 * bestBullAmp;
        res.fibSwingHigh = bestBullHighPx;
        res.fibSwingLow = bestBullLowPx;
    }

    PatternResult fibCandidate;
    bool hasFibCandidate = false;

    if (bestBullAmp >= requiredAmp) {
        double pbMin = bestBullHighPx;
        for (int i = bestBullHighIdx; i < n; ++i) {
            pbMin = std::min(pbMin, points[static_cast<size_t>(i)].price);
        }
        const double fib50 = res.fib50;
        const double fib618 = res.fib618;
        const double fib382 = bestBullHighPx - 0.382 * bestBullAmp;

        if (pbMin <= fib50 + breakBuffer && pbMin >= fib618 - breakBuffer * 2.0) {
            if (curPx >= pbMin + breakBuffer * 0.8 && curPx >= fib618 + breakBuffer * 0.3 && curPx <= fib382 + breakBuffer) {
                fibCandidate.type = PatternType::FibonacciGoldenPocket;
                fibCandidate.fib50 = fib50;
                fibCandidate.fib618 = fib618;
                fibCandidate.fibSwingHigh = bestBullHighPx;
                fibCandidate.fibSwingLow = bestBullLowPx;
                fibCandidate.support = fib618;
                fibCandidate.resistance = bestBullHighPx;
                fibCandidate.breakoutTarget = bestBullHighPx + bestBullAmp * 0.382;

                if (cfg.highAlreadyInProb >= 0.65 || cfg.multiDayBias == TrendBias::StrongBear) {
                    fibCandidate.name = "斐波那契黄金口袋 (顺势偏空·弱反抽观察)";
                    fibCandidate.bias = "弱势蓄势";
                    fibCandidate.strategy = "现价回踩斐波那契 0.50~0.618 黄金口袋关键支撑区并微幅反弹，但受多日空头排列/日内高点已确立压制，反抽动能有限，切勿盲目追涨做多。";
                } else {
                    fibCandidate.name = "斐波那契黄金口袋 (0.5~0.618回踩企稳🚀)";
                    fibCandidate.bias = "强烈看多";
                    fibCandidate.strategy = "主升浪上涨后健康回踩斐波那契 0.50~0.618 黄金口袋支撑区并企稳反弹！防守位设在 0.618 支撑 "
                                           + fmtPrice(fib618) + "，第一目标看前高 " + fmtPrice(bestBullHighPx)
                                           + "，扩展目标 " + fmtPrice(fibCandidate.breakoutTarget) + "。";
                }
                fibCandidate.valid = true;
                hasFibCandidate = true;
            } else if (curPx >= fib618 - breakBuffer * 1.5 && curPx <= fib50 + breakBuffer) {
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
                fibCandidate.strategy = "现价回踩进入斐波那契 0.50~0.618 黄金口袋支撑区间 ("
                                       + fmtPrice(fib618) + " ~ " + fmtPrice(fib50)
                                       + ")，正在测试多头承接力，密切观察止跌企稳机会。";
                fibCandidate.valid = true;
                hasFibCandidate = true;
            }
        }
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
            fibCandidate.strategy = "价格有效跌破斐波那契 0.618 黄金口袋防守支撑 ("
                                   + fmtPrice(fib618) + ")，原支撑转为阻力，防范回探起涨点 "
                                   + fmtPrice(bestBullLowPx) + "。";
            fibCandidate.valid = true;
            hasFibCandidate = true;
        }
    }

    if (!hasFibCandidate) {
        double bestBearAmp = 0.0;
        int bestBearLowIdx = -1;
        double bestBearHighPx = 0.0, bestBearLowPx = 0.0;
        for (const auto& tr : troughs) {
            if (tr.index > n - 2) continue;
            for (const auto& pk : peaks) {
                if (pk.index < tr.index && (tr.index - pk.index) >= 3) {
                    const double amp = pk.price - tr.price;
                    if (amp >= requiredAmp && amp > bestBearAmp) {
                        bestBearAmp = amp;
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
                        fibCandidate.strategy = "急跌后反弹测试 0.50~0.618 压制，但大周期偏多且日内低点趋稳，切勿盲目杀跌。";
                    } else {
                        fibCandidate.name = "斐波那契黄金口袋 (0.5~0.618反弹受阻⚠️)";
                        fibCandidate.bias = "强烈看空";
                        fibCandidate.strategy = "急跌后弱势反弹至斐波那契 0.50~0.618 压制区受阻回落！建议逢高减仓防守，谨防二次探底 "
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

    // 1) 脉冲 V 型反转 / 倒 V 冲高跳水
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
                    res.strategy = "短线急跌洗盘被多头强势深V收复并突破起跌点！诱空失败，回踩原起跌点为买入支撑。";
                } else {
                    res.name = "V型急跌反转 (深V蓄势中)";
                    res.bias = "多头蓄势";
                    res.support = minPx;
                    res.resistance = preHigh;
                    res.strategy = "急速探底后快速抽升，多头承接盘强劲，现价正冲击起跌阻力位。";
                }
                res.valid = true;
                return returnRes(res);
            }
        }
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
                    res.strategy = "脉冲急拉冲高受阻，回落吞没涨幅并跌破起涨支撑，形成倒V见顶破位，注意防守止盈。";
                } else {
                    res.name = "倒V冲高跳水 (冲高承压中)";
                    res.bias = "空头蓄势";
                    res.resistance = maxPx;
                    res.support = preLow;
                    res.strategy = "脉冲拉升后遭遇沉重抛压大幅回落，正在测试起涨支撑。";
                }
                res.valid = true;
                return returnRes(res);
            }
        }
    }

    // 2) 头肩底 / 头肩顶
    if (troughs.size() >= 3) {
        const auto& lt1 = troughs[troughs.size() - 3];
        const auto& lt2 = troughs[troughs.size() - 2];
        const auto& lt3 = troughs.back();
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
                    res.strategy = "现价已突破头肩底颈线！颈线转为强支撑，上看目标位 " + fmtPrice(target) + "。";
                } else {
                    res.name = "头肩底形态 (倒头肩蓄势)";
                    res.bias = "多头蓄势";
                    res.support = std::min(lt1.price, lt3.price);
                    res.resistance = neckline;
                    res.strategy = "右肩构筑企稳，倒头肩形态蓄势冲击颈线阻力。";
                }
                res.valid = true;
                return returnRes(res);
            }
        }
    }

    if (peaks.size() >= 3) {
        const auto& lp1 = peaks[peaks.size() - 3];
        const auto& lp2 = peaks[peaks.size() - 2];
        const auto& lp3 = peaks.back();
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
                    res.strategy = "现价已跌破头肩顶颈线支撑！防范回落目标 " + fmtPrice(target) + "。";
                } else {
                    res.name = "头肩顶形态 (高位受阻)";
                    res.bias = "空头蓄势";
                    res.resistance = lp2.price;
                    res.support = neckline;
                    res.strategy = "右肩冲高乏力，头肩顶形成，当前测试颈线支撑。";
                }
                res.valid = true;
                return returnRes(res);
            }
        }
    }

    // 3) 三重底 / 三重顶
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
                    res.strategy = "三次踩底企稳并突破颈线，后市上看 " + fmtPrice(target) + "。";
                } else {
                    res.name = "三重底形态 (三重筑底)";
                    res.bias = "多头蓄势";
                    res.support = mnT;
                    res.resistance = neckline;
                    res.strategy = "关键支撑三次测试有效，等待突破颈线确认。";
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
                    res.strategy = "三次冲高受阻并跌破颈线，下方目标 " + fmtPrice(target) + "。";
                } else {
                    res.name = "三重顶形态 (三重承压)";
                    res.bias = "空头蓄势";
                    res.resistance = mxP;
                    res.support = neckline;
                    res.strategy = "上方水平阻力三次受阻，当前测试颈线支撑。";
                }
                res.valid = true;
                return returnRes(res);
            }
        }
    }

    // 4) 顺势旗形中继 (时间长度固定：旗面 10-25 分钟，旗杆 8-30 分钟)
    if (n >= 35) {
        const int flagLen = std::clamp(n / 5, 10, 25);
        const int poleLen = std::clamp((n - flagLen) / 2, 8, 30);
        const double poleStart = points[static_cast<size_t>(n - flagLen - poleLen)].price;
        const double poleEnd = points[static_cast<size_t>(n - flagLen)].price;
        const double poleHeight = poleEnd - poleStart;

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
                        res.strategy = "突破看涨旗面且契合黄金口袋支撑！第一目标上看 " + fmtPrice(target) + "。";
                    } else {
                        res.name = "看涨旗形 (顺势主升浪🚀)";
                        res.strategy = "向上突破看涨旗面，主升浪中继确立，第一目标上看 " + fmtPrice(target) + "。";
                    }
                    res.bias = "看多突破";
                    res.support = flagHigh;
                    res.resistance = target;
                } else {
                    res.name = "看涨旗形 (蓄势中继)";
                    res.bias = "多头蓄势";
                    res.resistance = flagHigh;
                    res.support = flagLow;
                    res.strategy = "强势冲高后微幅回踩构筑旗面整理，等待突破旗面上轨。";
                }
                res.valid = true;
                return returnRes(res);
            }
        }
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
                    res.strategy = "跌破看跌旗面下轨，谨防顺势加速探底 " + fmtPrice(target) + "。";
                } else {
                    res.name = "看跌旗形 (偏空中继)";
                    res.bias = "空头蓄势";
                    res.resistance = flagHigh;
                    res.support = flagLow;
                    res.strategy = "急跌后弱势反弹构筑看跌旗面，谨防二次破位。";
                }
                res.valid = true;
                return returnRes(res);
            }
        }
    }

    // 5) 双底 / W底
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
                    res.strategy = "突破W底颈线且筑底回踩契合黄金口袋！第一目标上看 " + fmtPrice(target) + "。";
                } else {
                    res.name = "双底突破 (W底确立🚀)";
                    res.strategy = "现价已突破W底颈线！颈线转为强支撑，第一目标上看 " + fmtPrice(target) + "。";
                }
                res.bias = "看多突破";
                res.support = neckline;
                res.resistance = target;
            } else {
                res.name = "双底形态 (W底筑底)";
                res.bias = "多头蓄势";
                res.support = baseSupport;
                res.resistance = neckline;
                res.strategy = "双底支撑有效，现价正在向颈线阻力蓄势冲击。";
            }
            res.valid = true;
            return returnRes(res);
        }
    }

    // 6) 双顶 / M顶
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
                res.strategy = "现价已跌破M顶颈线支撑！下行目标看至 " + fmtPrice(target) + "。";
            } else {
                res.name = "双顶形态 (M顶受阻)";
                res.bias = "空头蓄势";
                res.resistance = baseResistance;
                res.support = neckline;
                res.strategy = "高位双顶阻力压制，当前测试颈线支撑，谨防破位。";
            }
            res.valid = true;
            return returnRes(res);
        }
    }

    // 7~13) 最小二乘回归信道（取近 3~4 个拐点做 OLS 拟合与 R^2 校验，避免两点毛刺误判）
    const auto upperFit = fitSwingRegression(peaks, 4);
    const auto lowerFit = fitSwingRegression(troughs, 4);
    const double peakSlope = upperFit.valid
                                 ? upperFit.slope
                                 : ((p2.price - p1.price) / static_cast<double>(std::max(1, p2.index - p1.index)));
    const double troughSlope = lowerFit.valid
                                   ? lowerFit.slope
                                   : ((t2.price - t1.price) / static_cast<double>(std::max(1, t2.index - t1.index)));
    const double curUpper = upperFit.valid
                                ? (upperFit.intercept + upperFit.slope * static_cast<double>(n - 1))
                                : (p2.price + peakSlope * static_cast<double>(std::max(0, n - 1 - p2.index)));
    const double curLower = lowerFit.valid
                                ? (lowerFit.intercept + lowerFit.slope * static_cast<double>(n - 1))
                                : (t2.price + troughSlope * static_cast<double>(std::max(0, n - 1 - t2.index)));

    const bool goodFit = (!upperFit.valid || peaks.size() < 3 || upperFit.r2 >= 0.40) &&
                         (!lowerFit.valid || troughs.size() < 3 || lowerFit.r2 >= 0.40);

    if (goodFit && peakSlope > slopeThreshold && troughSlope < -slopeThreshold && priceRange >= curPx * 0.0035) {
        res.type = PatternType::Broadening;
        res.name = "扩散喇叭形 (情绪加剧)";
        res.bias = "剧烈变盘";
        res.resistance = curUpper;
        res.support = curLower;
        res.breakoutTarget = curUpper + priceRange * 0.5;
        res.strategy = "高点更高、低点更低，振幅发散放大，市场分歧剧烈，避免追单！";
        res.valid = true;
        return returnRes(res);
    }

    if (goodFit && peakSlope < -slopeThreshold && troughSlope < 0 && peakSlope < troughSlope) {
        res.type = PatternType::FallingWedge;
        res.breakoutTarget = p1.price;
        if (curPx >= curUpper + breakBuffer) {
            if (hasFibCandidate && (fibCandidate.bias == "强烈看多" || fibCandidate.bias == "多头蓄势")) {
                res.name = "下降楔形 + 黄金口袋共振 (向上突破🚀)";
                res.strategy = "下降楔形突破上轨且下沿获黄金口袋承接！第一目标上看 " + fmtPrice(p1.price) + "。";
            } else {
                res.name = "下降楔形 (向上突破🚀)";
                res.strategy = "现价向上突破下降楔形上轨！空头衰竭反转，顺势看多。";
            }
            res.bias = "看多突破";
            res.support = curUpper;
            res.resistance = p1.price;
        } else if (curPx <= curLower - breakBuffer) {
            res.name = "下降楔形 (向下破位⚠️)";
            res.bias = "看空破位";
            res.resistance = curLower;
            res.support = curLower - priceRange * 0.5;
            res.strategy = "跌破下轨支撑破位下行，防范加速探底。";
        } else {
            res.name = "下降楔形 (收敛蓄势)";
            res.bias = "多头蓄势";
            res.resistance = curUpper;
            res.support = curLower;
            res.strategy = "下降楔形波幅收敛，空头动能衰竭，等待突破上轨阻力。";
        }
        res.valid = true;
        return returnRes(res);
    }

    if (goodFit && peakSlope > slopeThreshold && troughSlope > 0 && troughSlope > peakSlope) {
        res.type = PatternType::RisingWedge;
        res.breakoutTarget = t1.price;
        if (curPx <= curLower - breakBuffer) {
            res.name = "上升楔形 (向下跌破⚠️)";
            res.bias = "看空破位";
            res.resistance = curLower;
            res.support = t1.price;
            res.strategy = "跌破上升楔形下轨支撑！多头衰竭，谨防加速下行。";
        } else {
            res.name = "上升楔形 (高位收敛)";
            res.bias = "空头蓄势";
            res.resistance = curUpper;
            res.support = curLower;
            res.strategy = "上升楔形持续收紧，高位动能递减，谨防跌破下轨。";
        }
        res.valid = true;
        return returnRes(res);
    }

    if (goodFit && peakSlope < -slopeThreshold && troughSlope > slopeThreshold) {
        res.type = PatternType::SymmetricalTriangle;
        res.breakoutTarget = curPx + (p1.price - t1.price) * 0.7;
        if (curPx >= curUpper + breakBuffer) {
            res.name = "对称三角形 (向上突破🚀)";
            res.bias = "看多突破";
            res.support = curUpper;
            res.resistance = p1.price;
            res.strategy = "对称三角形向上突破上轨压制，顺势看多。";
        } else if (curPx <= curLower - breakBuffer) {
            res.name = "对称三角形 (向下破位⚠️)";
            res.bias = "看空破位";
            res.resistance = curLower;
            res.support = t1.price;
            res.strategy = "对称三角形跌破下轨支撑，谨防深度调整。";
        } else {
            res.name = "对称三角形收敛 (Triangle)";
            res.bias = "中性蓄势";
            res.resistance = curUpper;
            res.support = curLower;
            res.strategy = "高低点对称收敛蓄势，关注突破方向。";
        }
        res.valid = true;
        return returnRes(res);
    }

    if (std::abs(peakSlope) <= slopeThreshold && troughSlope > slopeThreshold) {
        res.type = PatternType::AscendingTriangle;
        const double resLvl = std::max(p1.price, p2.price);
        res.breakoutTarget = resLvl + (resLvl - t1.price);
        if (curPx >= resLvl + breakBuffer) {
            res.name = "上升三角形 (向上突破🚀)";
            res.bias = "看多突破";
            res.support = resLvl;
            res.resistance = res.breakoutTarget;
            res.strategy = "现价突破上升三角形水平阻力位，顺势看多。";
        } else {
            res.name = "上升三角形 (Ascending Triangle)";
            res.bias = "多头蓄势";
            res.resistance = resLvl;
            res.support = t2.price;
            res.strategy = "水平阻力多次测试，低点抬高，等待向上突破。";
        }
        res.valid = true;
        return returnRes(res);
    }

    if (std::abs(troughSlope) <= slopeThreshold && peakSlope < -slopeThreshold) {
        res.type = PatternType::DescendingTriangle;
        const double supLvl = std::min(t1.price, t2.price);
        res.breakoutTarget = supLvl - (p1.price - supLvl);
        if (curPx <= supLvl - breakBuffer) {
            res.name = "下降三角形 (向下破位⚠️)";
            res.bias = "看空破位";
            res.resistance = supLvl;
            res.support = res.breakoutTarget;
            res.strategy = "现价跌破下降三角形水平支撑位，谨防加速下行。";
        } else {
            res.name = "下降三角形 (Descending Triangle)";
            res.bias = "空头蓄势";
            res.support = supLvl;
            res.resistance = p2.price;
            res.strategy = "水平支撑多次受压，高点逐级压低，谨防破位。";
        }
        res.valid = true;
        return returnRes(res);
    }

    if (peakSlope > slopeThreshold && troughSlope > slopeThreshold) {
        res.type = PatternType::ChannelUp;
        res.name = "上升通道 (Channel Up)";
        res.bias = "震荡向上";
        res.resistance = curUpper;
        res.support = curLower;
        res.breakoutTarget = curUpper + priceRange * 0.5;
        res.strategy = "处于多头上升通道运行，沿下轨顺势做多，破下轨止损。";
        res.valid = true;
        return returnRes(res);
    } else if (peakSlope < -slopeThreshold && troughSlope < -slopeThreshold) {
        res.type = PatternType::ChannelDown;
        res.name = "下降通道 (Channel Down)";
        res.bias = "震荡向下";
        res.resistance = curUpper;
        res.support = curLower;
        res.breakoutTarget = curLower - priceRange * 0.5;
        res.strategy = "处于弱势下降通道运行，反弹受阻上轨注意防守。";
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
    const double effAtr = cfg.atr > 0.2 ? cfg.atr : (curPx > 1000.0 ? 25.0 : 4.0);
    const MarketRegime regime = cfg.multiDayTrend.valid ? cfg.multiDayTrend.regime : MarketRegime::LowVolRange;
    res.regime = regime;

    // -------------------------------------------------------------
    // 1) 亚盘基准箱体计算 (Asian Range: 09:00 - 14:30 CST)
    // -------------------------------------------------------------
    double aHigh = 0.0, aLow = 0.0;
    int aCount = 0;
    for (const auto& pt : points) {
        if (pt.price <= 0.0) continue;
        const int m = minuteOfDayFromEpochMs(pt.epochMs);
        if (m >= 540 && m <= 870) {
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
    const bool isPostAsian = (curMin > 870 || curMin < 540);

    if (res.hasAsianBox && isPostAsian) {
        const double boxTol = std::max(0.25, effAtr * 0.08);
        if (curPx >= res.asianHigh + boxTol) {
            res.asianBreakoutUp = true;
        } else if (curPx <= res.asianLow - boxTol) {
            res.asianBreakoutDown = true;
        }
    }

    // -------------------------------------------------------------
    // 2) 各因子归一化原始分 [0.0, 1.0]（后按市场状态动态配权至总分 100）
    // -------------------------------------------------------------
    double rTrend = 0.5;
    if (cfg.multiDayTrend.valid) {
        switch (cfg.multiDayTrend.bias) {
        case TrendBias::StrongBull: rTrend = 1.00; break;
        case TrendBias::MildBull:   rTrend = 0.78; break;
        case TrendBias::Range:      rTrend = 0.50; break;
        case TrendBias::MildBear:   rTrend = 0.25; break;
        case TrendBias::StrongBear: rTrend = 0.06; break;
        }
        if (cfg.multiDayTrend.rsi14 > 76.0)      rTrend = std::max(0.0, rTrend - 0.14);
        else if (cfg.multiDayTrend.rsi14 < 28.0) rTrend = std::min(1.0, rTrend + 0.14);
    }

    double rSession = 0.5;
    if (res.hasAsianBox) {
        if (!isPostAsian) {
            rSession = 0.50;
        } else if (res.asianBreakoutUp) {
            rSession = 1.00;
        } else if (res.asianBreakoutDown) {
            rSession = 0.05;
        } else {
            if (curPx >= res.asianHigh - 0.35) rSession = 0.70;
            else if (curPx <= res.asianLow + 0.35) rSession = 0.30;
            else rSession = 0.50;
        }
    }
    if (curMin >= 1230) {
        if (rSession >= 0.70) rSession = std::min(1.0, rSession + 0.10);
        else if (rSession <= 0.40) rSession = std::max(0.0, rSession - 0.10);
    }

    // 因子3：时间加权均值回归 TWAP + 极值带 + 卡尔曼动量转折
    const double twap = computeTwap(points);
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

    double rMean = 0.50;
    if (predL > 0.0 && curPx <= predL + zoneTol) {
        rMean = (reboundFromLow >= minTurn) ? 0.96 : 0.56;
    } else if (predH > 0.0 && curPx >= predH - zoneTol) {
        rMean = (pullbackFromHigh >= minTurn) ? 0.16 : 0.64;
    } else {
        const double diff = curPx - twap;
        if (diff < -1.5 * effAtr)                     rMean = 0.84;
        else if (diff > 1.8 * effAtr)                 rMean = 0.20;
        else if (diff >= 0.0 && diff <= 0.8 * effAtr) rMean = 0.64;
        else if (diff < 0.0 && diff >= -0.8 * effAtr) rMean = 0.44;
    }

    // 因子4：形态与黄金口袋
    double rPattern = 0.48;
    if (cfg.pattern.valid) {
        if (cfg.pattern.type == PatternType::FibonacciGoldenPocket) {
            if (cfg.pattern.bias == "强烈看多" || cfg.pattern.bias == "看多突破") rPattern = 1.00;
            else if (cfg.pattern.bias == "多头蓄势")                              rPattern = 0.76;
            else if (cfg.pattern.bias == "看空破位")                              rPattern = 0.16;
            else                                                                  rPattern = 0.48;
        } else if (cfg.pattern.bias == "强烈看多" || cfg.pattern.bias == "看多突破") {
            rPattern = 0.96;
        } else if (cfg.pattern.bias == "多头蓄势") {
            rPattern = 0.68;
        } else if (cfg.pattern.bias == "强烈看空" || cfg.pattern.bias == "看空破位") {
            rPattern = 0.12;
        } else if (cfg.pattern.bias == "空头蓄势" || cfg.pattern.bias == "震荡向下") {
            rPattern = 0.32;
        }
    }

    // 状态动态配权：趋势市放大 Trend+Pattern，震荡市放大 MeanReversion，高波市均衡防守
    int wTrend = 30, wSession = 20, wMean = 25, wPattern = 25;
    if (regime == MarketRegime::Trending) {
        wTrend = 35; wSession = 20; wMean = 15; wPattern = 30;
    } else if (regime == MarketRegime::LowVolRange) {
        wTrend = 22; wSession = 16; wMean = 38; wPattern = 24;
    }

    res.trendScore         = static_cast<int>(std::lround(rTrend * wTrend));
    res.sessionScore       = static_cast<int>(std::lround(rSession * wSession));
    res.meanReversionScore = static_cast<int>(std::lround(rMean * wMean));
    res.patternScore       = static_cast<int>(std::lround(rPattern * wPattern));

    const int crossNudge = static_cast<int>(std::lround(std::clamp(cfg.crossMarketLeadSignal * 2.0, -5.0, 5.0)));
    const int sentNudge  = static_cast<int>(std::lround(std::clamp(cfg.sentimentScore * 1.0, -5.0, 5.0)));

    res.score = std::clamp(res.trendScore + res.sessionScore + res.meanReversionScore + res.patternScore + crossNudge + sentNudge, 0, 100);

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
    // 3) 关键执行挂单与止损价格计算
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
    res.aggressiveBuy = r + std::max(0.25, effAtr * 0.08);
    res.takeProfit1 = r;
    res.takeProfit2 = cfg.pattern.breakoutTarget > r ? cfg.pattern.breakoutTarget : (r + 1.2 * effAtr);
    res.stopLoss = s - 0.6 * effAtr;

    // -------------------------------------------------------------
    // 4) 手续费与安全边际过滤
    // -------------------------------------------------------------
    res.roundTripFee = curPx * cfg.feeRate;
    const double targetP = std::max(res.takeProfit1, curPx + 1.2 * effAtr);
    const double buyCost = curPx;
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

    char buf[256];
    std::snprintf(buf, sizeof(buf), "综合共振: %d分 [%s·%s] | 趋势%d·时段%d·波动%d·形态%d",
                  res.score, res.actionLabel.c_str(), MultiDayTrend::regimeLabel(regime),
                  res.trendScore, res.sessionScore, res.meanReversionScore, res.patternScore);
    res.summary = std::string(buf);

    if (!res.feeEdgeSatisfied) {
        res.summary += " [⚠️扣费净利空间微薄，防手续费损耗已转观望]";
    }

    std::string rat = "【多因子量化共振评测详情】\n";
    rat += "0. 市场状态识别: " + std::string(MultiDayTrend::regimeLabel(regime)) + "\n";
    rat += "1. 多日趋势维度 (" + std::to_string(res.trendScore) + "/" + std::to_string(wTrend) + "分)："
           + (cfg.multiDayTrend.valid ? cfg.multiDayTrend.label() : "震荡中性") + "\n";
    if (res.hasAsianBox) {
        rat += "2. 亚欧盘时段维度 (" + std::to_string(res.sessionScore) + "/" + std::to_string(wSession) + "分)：亚盘箱体 ["
               + fmtPrice(res.asianLow) + " ~ " + fmtPrice(res.asianHigh) + "]";
        if (res.asianBreakoutUp) rat += " (欧美盘上破🚀)\n";
        else if (res.asianBreakoutDown) rat += " (欧美盘破位⚠️)\n";
        else rat += " (区间内震荡整理)\n";
    } else {
        rat += "2. 亚欧盘时段维度 (" + std::to_string(res.sessionScore) + "/" + std::to_string(wSession) + "分)：数据积累中\n";
    }
    rat += "3. 波动与TWAP均值维度 (" + std::to_string(res.meanReversionScore) + "/" + std::to_string(wMean) + "分)：现价 vs 时间加权均价 " + fmtPrice(twap);
    if (predH > 0 && predL > 0) rat += " (推演区间 " + fmtPrice(predL) + " ~ " + fmtPrice(predH) + ")";
    rat += "\n";
    rat += "4. 形态与黄金口袋 (" + std::to_string(res.patternScore) + "/" + std::to_string(wPattern) + "分)："
           + (cfg.pattern.valid ? cfg.pattern.name : "箱体震荡整理") + "\n";
    if (crossNudge != 0 || sentNudge != 0) {
        rat += "5. 跨市场/舆情微调: 跨市场" + std::to_string(crossNudge) + " · 舆情" + std::to_string(sentNudge) + "\n";
    }
    rat += "\n【挂单执行点位参考】\n";
    rat += "• 稳健低吸买点: " + fmtPrice(res.conservativeBuy) + "\n";
    rat += "• 突破顺势买点: " + fmtPrice(res.aggressiveBuy) + "\n";
    rat += "• 目标止盈一: " + fmtPrice(res.takeProfit1) + "\n";
    rat += "• 目标止盈二: " + fmtPrice(res.takeProfit2) + "\n";
    rat += "• 硬防守止损位: " + fmtPrice(res.stopLoss) + "\n\n";
    rat += "【成本与风控测算】\n";
    rat += "• 预计卖出手续费: -" + fmtPrice(res.roundTripFee) + "\n";
    rat += "• 扣费后理论净利润: " + fmtPrice(res.netReward) + "\n";
    rat += "• 理论盈亏比 (RRR): " + fmtPrice(res.rrr) + ":1\n";

    res.rationale = rat;
    return res;
}

} // namespace goldsdk
