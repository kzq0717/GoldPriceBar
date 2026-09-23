#pragma once
#include "goldsdk/types.hpp"
#include <vector>
#include <string>
#include <cmath>
#include <algorithm>

namespace goldsdk {

struct PeakBucket {
    int bucketIndex = 0;   // 0..47, 30min
    int startMinute = 0;
    int endMinute = 0;
    double probability = 0.0;
};

struct DayRangeForecast {
    double predHigh = 0;
    double predLow = 0;
    std::string predHighTimeWindow;
    std::string predLowTimeWindow;
    std::string scenario;
    std::string keyCatalyst;
    bool valid = false;

    // 增强：高点时间概率与剩余空间
    double highAlreadyInProb = 0.0;
    double lowAlreadyInProb = 0.0;
    double remainingUpside = 0.0;
    double peakWindowProb = 0.0;
    double confidence = 0.0;
    std::vector<PeakBucket> peakBuckets;
};

enum class TrendBias {
    StrongBull,
    MildBull,
    Range,
    MildBear,
    StrongBear
};

struct MultiDayTrend {
    TrendBias bias = TrendBias::Range;
    double score = 0.0;
    double rsi14 = 50.0;
    double ma5 = 0.0, ma10 = 0.0, ma20 = 0.0;
    bool valid = false;

    static const char* biasLabel(TrendBias b);
    const char* label() const { return biasLabel(bias); }
    bool allowLongBias() const {
        return bias == TrendBias::StrongBull || bias == TrendBias::MildBull;
    }
};

/** 本地当日高低预测 + 多日趋势（无 Qt）。 */
class ForecastEngine {
public:
    /**
     * @param historicalPeakBucketCounts 可选长度 48 的历史「日高点落入桶」计数
     */
    static DayRangeForecast dayRange(const std::vector<IntradayPoint>& points,
                                     double actHigh,
                                     double actLow,
                                     double dayFraction,
                                     double atr = 0.0,
                                     double prevClose = 0.0,
                                     const std::vector<int>& historicalPeakBucketCounts = {});

    /** @param closes 近 20~60 日收盘，旧→新 */
    static MultiDayTrend multiDayTrend(const std::vector<double>& closes);
};

} // namespace goldsdk
