#pragma once
#include "goldsdk/types.hpp"
#include <vector>
#include <cmath>
#include <algorithm>

namespace goldsdk {

struct DayRangeForecast {
    double predHigh = 0;
    double predLow = 0;
    std::string predHighTimeWindow;
    std::string predLowTimeWindow;
    std::string scenario;
    std::string keyCatalyst;
    bool valid = false;
};

/** 本地当日高低预测（无 Qt）。 */
class ForecastEngine {
public:
    /**
     * @param points 当日分时点（时间升序）
     * @param actHigh 已实现最高（0 表示用 points 推算）
     * @param actLow  已实现最低
     * @param dayFraction 0~1 当前已过交易日比例（可用钟表估算）
     * @param atr 历史真实波幅（可选）
     * @param prevClose 前日收盘价（可选）
     */
    static DayRangeForecast dayRange(const std::vector<IntradayPoint>& points,
                                     double actHigh,
                                     double actLow,
                                     double dayFraction,
                                     double atr = 0.0,
                                     double prevClose = 0.0);
};

} // namespace goldsdk
