#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <optional>

namespace goldsdk {

struct PriceQuote {
    double price = 0.0;
    double change = 0.0;       // absolute or percent depending on source
    std::string source;        // zs / ms / gj
    std::string sourceName;
    std::string currency;      // CNY / USD
    std::string unit;          // CNY/g / USD/oz
    int64_t epochMs = 0;
};

struct DailyBar {
    std::string tradeDate; // YYYY-MM-DD
    std::string source;
    double open = 0, high = 0, low = 0, close = 0;
    std::string unit;
};

struct IntradayPoint {
    int64_t epochMs = 0;
    double price = 0.0;
};

enum class DataSource {
    Zs,   // 浙商积存金
    Ms,   // 民生
    Gj    // 伦敦金 XAU/USD
};

inline std::string toSourceCode(DataSource s) {
    switch (s) {
    case DataSource::Ms: return "ms";
    case DataSource::Gj: return "gj";
    default: return "zs";
    }
}

inline DataSource fromSourceCode(const std::string& s) {
    if (s == "ms") return DataSource::Ms;
    if (s == "gj" || s == "xau" || s == "london") return DataSource::Gj;
    return DataSource::Zs;
}

} // namespace goldsdk
