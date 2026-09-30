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

enum class PatternType {
    None,
    RisingWedge,            // 上升楔形（多头衰竭，警惕下破反转）
    FallingWedge,           // 下降楔形（空头衰竭，蓄势看涨向上突破）
    SymmetricalTriangle,    // 对称三角形（多空收敛，变盘临近）
    AscendingTriangle,      // 上升三角形（阻力水平，低点抬高）
    DescendingTriangle,     // 下降三角形（支撑水平，高点下移）
    DoubleBottom,           // 双底 / W底（筑底企稳）
    DoubleTop,              // 双顶 / M顶（顶部承压）
    TripleBottom,           // 三重底（多次筑底支撑稳固）
    TripleTop,              // 三重顶（多次冲高受阻）
    HeadAndShouldersBottom, // 倒头肩 / 头肩底（强力看多大级别反转）
    HeadAndShouldersTop,    // 头肩顶（强力看空大级别反转）
    BullFlag,               // 看涨旗形（急拉后微幅回踩整理，顺势中继）
    BearFlag,               // 看跌旗形（急跌后微幅反弹整理，顺势中继）
    VReversalBottom,        // 脉冲急跌 V 型反转（深度洗盘快速收复）
    VReversalTop,           // 脉冲冲高 倒V 型反转（假突破承压跳水）
    ChannelUp,              // 上升通道（顺势向上）
    ChannelDown,            // 下降通道（弱势下行）
    Broadening,             // 扩散喇叭形（情绪分歧加剧，振幅发散）
    FibonacciGoldenPocket   // 斐波那契黄金口袋回踩（0.5~0.618健康洗盘逢低买入）
};

struct PatternResult {
    PatternType type = PatternType::None;
    std::string name;          // 形态名称，如 "下降楔形 (Falling Wedge)"
    std::string bias;          // "看多" / "看空" / "震荡"
    std::string strategy;      // 交易策略与操作建议
    double resistance = 0.0;   // 上轨 / 颈线阻力位
    double support = 0.0;      // 下轨 / 支撑位
    double breakoutTarget = 0.0; // 突破理论目标位
    double fib50 = 0.0;        // 斐波那契 0.500 水平位
    double fib618 = 0.0;       // 斐波那契 0.618 水平位
    double fibSwingHigh = 0.0; // 主波段波峰
    double fibSwingLow = 0.0;  // 主波段波谷
    double rrr = 0.0;          // 扣除手续费后的预期净盈亏比 (Reward-to-Risk Ratio)
    double netReward = 0.0;    // 扣除手续费后的理论净收益
    double netRisk = 0.0;      // 理论防守风险
    double atrTrailingStop = 0.0; // 动态 ATR 追踪移动止盈位
    bool valid = false;
};

struct PatternConfig {
    double feeRate = 0.004;         // 预估交易费率（默认 0.4% 浙商积存金，gj可为0）
    double highAlreadyInProb = 0.0; // 日内高点已确立概率 (0~1)
    double lowAlreadyInProb = 0.0;  // 日内低点已确立概率 (0~1)
    double atr = 0.0;               // 真实波幅均值 ATR(10)
    TrendBias multiDayBias = TrendBias::Range; // 多日中长趋势
};

/** 本地当日高低预测 + 多日趋势 + 经典形态（无 Qt）。 */
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

    /** 识别价格序列中的经典形态（楔形、三角形、双底双顶、斐波那契黄金口袋等） */
    static PatternResult identifyPattern(const std::vector<IntradayPoint>& points,
                                         const PatternConfig& cfg = PatternConfig{});
};

} // namespace goldsdk
