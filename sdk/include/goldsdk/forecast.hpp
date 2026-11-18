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

enum class TrendBias {
    StrongBull,
    MildBull,
    Range,
    MildBear,
    StrongBear
};

enum class MarketRegime {
    LowVolRange,   // 低波震荡态
    Trending,      // 趋势推进态
    HighVolCrisis  // 高波剧烈波动态
};

struct VolatilityEstimate {
    double harRvRange = 0.0;       // HAR-RV 预测全日振幅（价格单位）
    double garmanKlassVol = 0.0;   // Garman-Klass 日波动率（价格单位）
    double intradayRv = 0.0;       // 日内已实现波动率外推振幅
    double regimeVolRatio = 1.0;   // 短波(5d) / 长波(22d) 比值
    double effectiveAtr = 0.0;     // 稳健有效波动基准
    bool valid = false;
};

struct KalmanTrendState {
    double filteredPrice = 0.0;    // 卡尔曼滤波平滑价格
    double velocityPerMin = 0.0;   // 瞬时价格速度（元/分钟 或 美元/分钟）
    double velocityZ = 0.0;        // 归一化速度 Z-score
    double stateStd = 0.0;         // 状态估计标准差
    bool valid = false;
};

struct MultiDayTrend {
    TrendBias bias = TrendBias::Range;
    MarketRegime regime = MarketRegime::LowVolRange;
    double score = 0.0;
    double rsi14 = 50.0;           // Wilder 平滑 RSI(14)
    double ma5 = 0.0, ma10 = 0.0, ma20 = 0.0;
    double ema5 = 0.0, ema10 = 0.0, ema20 = 0.0;
    double efficiencyRatio = 0.0;  // Kaufman ER (0~1)
    double atr14 = 0.0;
    bool valid = false;

    static const char* biasLabel(TrendBias b);
    static const char* regimeLabel(MarketRegime r);
    const char* label() const { return biasLabel(bias); }
    bool allowLongBias() const {
        return bias == TrendBias::StrongBull || bias == TrendBias::MildBull;
    }
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

    // 量化模型诊断字段
    double kalmanSlopePerMin = 0.0;
    double kalmanVelocityZ = 0.0;
    double harExpectedRange = 0.0;
    double remainVolRatio = 1.0;
    MarketRegime regime = MarketRegime::LowVolRange;
};

struct ForecastContext {
    double actHigh = 0.0;
    double actLow = 0.0;
    double dayFraction = 0.5;
    double atr = 0.0;
    double prevClose = 0.0;
    std::vector<int> historicalPeakBucketCounts;     // 长度 48：历史日高点时段计数
    std::vector<double> historicalBucketVarWeights;  // 长度 48：历史分时段方差占比
    std::vector<DailyBar> dailyBars;                 // 旧→新 日线 OHLC（用于 HAR-RV / GK）
    MultiDayTrend multiDayTrend;                     // 多日趋势与状态
    double crossMarketLeadSignal = 0.0;              // 跨市场领先/内外盘溢价 Z-score (-3~+3)
    double sentimentScore = 0.0;                     // 时间衰减净舆情得分 (-6~+6)
    double conformalScale = 1.0;                     // 自适应共形区间缩放系数 (0.78~1.45)
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
    std::string name;            // 形态名称，如 "下降楔形 (Falling Wedge)"
    std::string bias;            // "看多" / "看空" / "震荡"
    std::string strategy;        // 交易策略与操作建议
    double resistance = 0.0;     // 上轨 / 颈线阻力位
    double support = 0.0;        // 下轨 / 支撑位
    double breakoutTarget = 0.0; // 突破理论目标位
    double fib50 = 0.0;          // 斐波那契 0.500 水平位
    double fib618 = 0.0;         // 斐波那契 0.618 水平位
    double fibSwingHigh = 0.0;   // 主波段波峰
    double fibSwingLow = 0.0;    // 主波段波谷
    double rrr = 0.0;            // 扣除手续费后的预期净盈亏比 (Reward-to-Risk Ratio)
    double netReward = 0.0;      // 扣除手续费后的理论净收益
    double netRisk = 0.0;        // 理论防守风险
    double atrTrailingStop = 0.0;// 动态 ATR 追踪移动止盈位
    int64_t pivotEpochMs = 0;    // 关键拐点时间戳（波峰 Peak 或波谷 Trough）
    double pivotPrice = 0.0;     // 关键拐点价格
    bool valid = false;
};

struct PatternConfig {
    double feeRate = 0.004;         // 预估卖出交易费率（默认 0.4% 浙商积存金，买入0费率；gj可为0）
    double highAlreadyInProb = 0.0; // 日内高点已确立概率 (0~1)
    double lowAlreadyInProb = 0.0;  // 日内低点已确立概率 (0~1)
    double atr = 0.0;               // 真实波幅均值
    double predHigh = 0.0;          // 当日推演高点阻力位
    double predLow = 0.0;           // 当日推演低点支撑位
    TrendBias multiDayBias = TrendBias::Range; // 多日中长趋势
};

enum class DecisionAction {
    StrongBuy,      // 强力做多 / 加仓 (得分 >= 80)
    BuyOnDips,      // 逢低分批建仓 (得分 65~79)
    WaitAndWatch,   // 震荡蓄势 / 观望 (得分 41~64)
    SellOnRallies,  // 冲高减仓 / 止盈防守 (得分 26~40)
    StrongSell      // 果断卖出 / 清仓离场 (得分 <= 25)
};

struct ConfluenceDecision {
    DecisionAction action = DecisionAction::WaitAndWatch;
    int score = 50;                  // 综合决策得分 (0 ~ 100)
    std::string actionLabel;         // "强力买入", "逢低布局", "震荡观望", "冲高减仓", "果断离场"
    std::string actionColor;         // 颜色代码：如 "#52c41a", "#73d13d", "#faad14", "#ff7875", "#ff4d4f"
    std::string summary;             // 简要决策摘要
    std::string rationale;           // 详细的多因子共振归因

    // 四大核心因子打分（随市场状态动态配权，总和满分 100）
    int trendScore = 15;             // 多日大趋势 + 状态识别
    int sessionScore = 10;           // 时段规律与亚盘突破 (Asian Box Breakout)
    int meanReversionScore = 12;     // 均值回归(TWAP)与波动带
    int patternScore = 12;           // 形态与斐波那契共振 (0.5~0.618 / 颈线)
    MarketRegime regime = MarketRegime::LowVolRange;

    // 关键执行价格指引（精确到分）
    double conservativeBuy = 0.0;    // 稳健买点（关键支撑/黄金口袋企稳）
    double aggressiveBuy = 0.0;      // 激进/突破买点
    double takeProfit1 = 0.0;        // 目标止盈一 (中轴/前高)
    double takeProfit2 = 0.0;        // 目标止盈二 (扩展目标)
    double stopLoss = 0.0;           // 防守止损位

    // 手续费与安全边际
    double roundTripFee = 0.0;       // 预计全周期手续费 (买入0费率 + 卖出0.4%, 元/g)
    double netReward = 0.0;          // 扣费后预期净收益 (元/g)
    double netRisk = 0.0;            // 理论防守风险 (元/g)
    double rrr = 0.0;                // 扣费后预期盈亏比
    bool feeEdgeSatisfied = false;   // 是否满足覆盖手续费的安全边际（净利 >= 1.2 元/g 且 RRR >= 1.4）

    // 亚盘基准箱体（Asian Range: 09:00 - 14:30 CST）
    double asianHigh = 0.0;
    double asianLow = 0.0;
    bool hasAsianBox = false;
    bool asianBreakoutUp = false;
    bool asianBreakoutDown = false;

    bool valid = false;
};

struct DecisionConfig {
    double feeRate = 0.004;                   // 预估卖出手续费率（默认 0.4% 浙商积存金，买入0费率）
    double currentPrice = 0.0;                // 当前实时价
    double atr = 0.0;                         // 真实波幅均值
    double predHigh = 0.0;                    // 日内预测高点阻力
    double predLow = 0.0;                     // 日内预测低点支撑
    double highAlreadyInProb = 0.0;           // 日内高点已确立概率
    double lowAlreadyInProb = 0.0;            // 日内低点已确立概率
    bool hasForecast = false;
    MultiDayTrend multiDayTrend;              // 多日中大趋势
    PatternResult pattern;                    // 几何形态识别结果
    double crossMarketLeadSignal = 0.0;       // 跨市场领先信号 (-3~+3)
    double sentimentScore = 0.0;              // 时间衰减净舆情得分 (-6~+6)
};

/** 本地当日高低预测 + HAR-RV波动率 + 卡尔曼趋势 + 形态 + 共振决策引擎（无 Qt）。 */
class ForecastEngine {
public:
    /** 兼容接口：委托至 dayRangeWithContext */
    static DayRangeForecast dayRange(const std::vector<IntradayPoint>& points,
                                     double actHigh,
                                     double actLow,
                                     double dayFraction,
                                     double atr = 0.0,
                                     double prevClose = 0.0,
                                     const std::vector<int>& historicalPeakBucketCounts = {});

    /** 完整上下文驱动的日内区间与时间窗口预测（HAR-RV + 时段方差曲线 + 卡尔曼滤波 + 共形校准） */
    static DayRangeForecast dayRangeWithContext(const std::vector<IntradayPoint>& points,
                                                const ForecastContext& ctx);

    /** @param closes 近 20~60 日收盘，旧→新 */
    static MultiDayTrend multiDayTrend(const std::vector<double>& closes);

    /** 基于 OHLC 日线的多日趋势与市场状态识别（含 Wilder ATR 与 Kaufman ER） */
    static MultiDayTrend multiDayTrendFromBars(const std::vector<DailyBar>& bars);

    /** 二维局部线性趋势卡尔曼滤波（抑制微结构毛刺，提取平滑瞬时速度） */
    static KalmanTrendState kalmanTrend(const std::vector<IntradayPoint>& points, double atr = 0.0);

    /** HAR-RV + Garman-Klass 波动率与预测日振幅估计 */
    static VolatilityEstimate estimateVolatility(const std::vector<DailyBar>& bars,
                                                 const std::vector<IntradayPoint>& intradayPoints,
                                                 double fallbackAtr = 0.0);

    /** 在线自适应共形区间校准因子 */
    static double computeConformalScale(const std::vector<double>& recentPredHighs,
                                        const std::vector<double>& recentPredLows,
                                        const std::vector<double>& recentActHighs,
                                        const std::vector<double>& recentActLows,
                                        double targetCoverage = 0.80);

    /** 按时间桶重采样分时序列（消除采样率漂移对形态几何尺度的影响） */
    static std::vector<IntradayPoint> resamplePoints(const std::vector<IntradayPoint>& points,
                                                     int intervalMinutes = 1);

    /** 识别价格序列中的经典形态（时间重采样 + ATR-ZigZag + 回归信道） */
    static PatternResult identifyPattern(const std::vector<IntradayPoint>& points,
                                         const PatternConfig& cfg = PatternConfig{});

    /** 综合多因子共振决策引擎：按市场状态动态配权融合趋势、时段突破、TWAP均值回归与形态 */
    static ConfluenceDecision evaluateDecision(const std::vector<IntradayPoint>& points,
                                               const DecisionConfig& cfg);
};

} // namespace goldsdk
