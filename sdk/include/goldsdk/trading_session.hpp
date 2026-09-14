#pragma once
#include <string>
#include <ctime>

namespace goldsdk {

/** 交易时段判断（本地钟表，示意规则，无 Qt）。 */
class TradingSession {
public:
    static std::string normalizeSource(std::string src);
    /** now 为本地 time_t；nullptr 表示当前时间 */
    static bool isTradingNow(const std::string& source, const std::tm* localTm = nullptr);
    static std::string statusText(const std::string& source, const std::tm* localTm = nullptr);
    static std::string hoursDescription(const std::string& source);
};

} // namespace goldsdk
