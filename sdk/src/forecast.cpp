#include "goldsdk/forecast.hpp"
#include <cmath>
#include <algorithm>

namespace goldsdk {

DayRangeForecast ForecastEngine::dayRange(const std::vector<IntradayPoint>& points,
                                          double actHigh,
                                          double actLow,
                                          double dayFraction)
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

    // 近窗波动（样本标准差）
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

    // 用「交易时段」近似剩余比例，而不是 0~24 整点钟
    // 积存金约 09:00–23:30 → 14.5h；伦敦金近似全天
    dayFraction = std::clamp(dayFraction, 0.0, 1.0);
    // dayFraction 仍按自然日传入；映射到剩余活跃度：早盘给更大扩张
    const double remain = std::clamp(1.0 - dayFraction, 0.05, 1.0);
    const double timeScale = std::sqrt(remain);

    // 预期全日振幅：近窗波动、已走振幅、价格比例三者取合理合成
    // 积存金常见日内振幅约 0.3%~1.2%；伦敦金美元盎司也类似比例
    const double pctFloor = lastPrice * 0.0035;   // 至少约 0.35%
    const double pctCeil  = lastPrice * 0.018;    // 至多约 1.8%（本地模型保守上沿）
    double expectedFullRange = std::max({rangeSoFar * 1.35, stdev * 4.5, pctFloor});
    expectedFullRange = std::min(expectedFullRange, pctCeil);

    // 尚未走完的振幅预算
    double remainingBudget = std::max(0.0, expectedFullRange - rangeSoFar);
    remainingBudget = std::max(remainingBudget, pctFloor * 0.25 * timeScale);
    remainingBudget *= (0.45 + 0.55 * timeScale);

    // 方向：略偏向突破近端高低（不对称 55/45）
    double up = remainingBudget * 0.55;
    double down = remainingBudget * 0.45;

    // 若现价贴近今高，增加上沿；贴近今低增加下沿
    if (actHigh > actLow) {
        const double pos = (lastPrice - actLow) / (actHigh - actLow);
        if (pos > 0.7) {
            up *= 1.15;
            down *= 0.90;
        } else if (pos < 0.3) {
            up *= 0.90;
            down *= 1.15;
        }
    }

    double predHigh = std::max(actHigh, lastPrice) + up;
    double predLow = std::min(actLow, lastPrice) - down;

    // 必须严格包住已实现区间，并保证与今高/今低有可见差
    const double minGap = std::max(lastPrice * 0.0008, 0.15); // 至少约 0.08% 或 0.15 元
    predHigh = std::max(predHigh, actHigh + minGap);
    predLow = std::min(predLow, actLow - minGap);

    // 最终宽度限制，避免再次撑爆坐标轴
    const double maxWidth = pctCeil * 1.15;
    if (predHigh - predLow > maxWidth) {
        const double mid = lastPrice;
        predHigh = std::min(predHigh, mid + maxWidth * 0.55);
        predLow = std::max(predLow, mid - maxWidth * 0.45);
        predHigh = std::max(predHigh, actHigh + minGap * 0.5);
        predLow = std::min(predLow, actLow - minGap * 0.5);
    }

    out.predHigh = predHigh;
    out.predLow = predLow;
    out.valid = predHigh > predLow && predHigh > 0.0 && predLow > 0.0;
    return out;
}

} // namespace goldsdk
