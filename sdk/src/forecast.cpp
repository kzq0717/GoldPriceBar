#include "goldsdk/forecast.hpp"
#include <cmath>
#include <algorithm>

namespace goldsdk {

DayRangeForecast ForecastEngine::dayRange(const std::vector<IntradayPoint>& points,
                                          double actHigh,
                                          double actLow,
                                          double dayFraction,
                                          double atr,
                                          double prevClose)
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

    dayFraction = std::clamp(dayFraction, 0.0, 1.0);
    const double remain = std::clamp(1.0 - dayFraction, 0.05, 1.0);
    const double timeScale = std::sqrt(remain);

    // 预期全日振幅估算：
    // 若提供了 ATR 则优先参考 ATR（通常为最近 10 日真实日波幅均值）
    const double pctFloor = lastPrice * 0.005;   // 基础日振幅下限约 0.5%
    const double pctCeil  = lastPrice * 0.025;   // 极端日振幅上限约 2.5%
    double expectedFullRange = 0.0;
    if (atr > 0.0) {
        expectedFullRange = std::clamp(atr * (0.85 + 0.35 * (1.0 - timeScale)), pctFloor, pctCeil);
    } else {
        expectedFullRange = std::max({rangeSoFar * 1.25, stdev * 4.2, pctFloor});
        expectedFullRange = std::min(expectedFullRange, pctCeil);
    }

    // 尚未走完的振幅预算（随交易日时间衰减，杜绝无脑漂移）
    double remainingBudget = std::max(0.0, expectedFullRange - rangeSoFar);
    remainingBudget *= (0.35 + 0.65 * timeScale);

    // 方向分布：综合动量与位置
    double up = remainingBudget * 0.5;
    double down = remainingBudget * 0.5;
    if (actHigh > actLow) {
        const double pos = (lastPrice - actLow) / (actHigh - actLow);
        // 如果处于震荡区间上部，向上延伸空间递减（触碰天花板），向下回撤空间递增
        if (pos > 0.75) {
            up *= 0.75;
            down *= 1.25;
        } else if (pos < 0.25) {
            up *= 1.25;
            down *= 0.75;
        }
    }

    // 锚定基准：已实现区间 + 衰减预算
    double predHigh = actHigh + up;
    double predLow  = actLow - down;

    // 当日振幅已经大幅超标（如突发单边行情），预测线不随价格无限追涨杀跌
    if (rangeSoFar >= expectedFullRange) {
        predHigh = actHigh + std::max(lastPrice * 0.0008, 0.20) * timeScale;
        predLow  = actLow  - std::max(lastPrice * 0.0008, 0.20) * timeScale;
    }

    // 保证预测线与今高/今低有清晰可见间隔
    const double minGap = std::max(lastPrice * 0.0005, 0.15);
    predHigh = std::max(predHigh, actHigh + minGap);
    predLow  = std::min(predLow, actLow - minGap);

    // 根据交易时钟模型推演时间窗口与情景
    if (dayFraction < 0.35) { // 亚盘时段（09:00 - 14:00）
        out.predHighTimeWindow = "20:30 - 22:30 (美盘数据期)";
        out.predLowTimeWindow  = "15:00 - 16:30 (欧盘开盘期)";
        out.scenario = "亚盘多为震荡蓄势，重点关注欧盘初探底支撑与美盘主浪冲高";
        out.keyCatalyst = "欧盘资金进场接力 + 20:30 美盘重要宏观数据发布";
    } else if (dayFraction < 0.70) { // 欧盘时段（14:00 - 19:30）
        out.predHighTimeWindow = "20:30 - 22:00 (美盘主推浪)";
        out.predLowTimeWindow  = "16:00 - 17:30 (欧盘二次下探)";
        out.scenario = "欧盘阶段多空反复争夺关键位，全天主极值大概率在美盘出炉";
        out.keyCatalyst = "欧美盘交叠期资金再平衡与美债收益率异动";
    } else { // 美盘与夜盘时段（19:30 - 02:30）
        out.predHighTimeWindow = "21:30 - 23:00 (美盘高潮段)";
        out.predLowTimeWindow  = "20:30 - 21:30 (数据发布下影)";
        out.scenario = "美盘主力对决阶段，需防范数据冲高诱多回落或探底快速V转";
        out.keyCatalyst = "美盘核心经济指标冲击与隔夜头寸获利了结";
    }

    out.predHigh = predHigh;
    out.predLow = predLow;
    out.valid = predHigh > predLow && predHigh > 0.0 && predLow > 0.0;
    return out;
}

} // namespace goldsdk
