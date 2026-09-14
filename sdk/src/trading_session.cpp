#include "goldsdk/trading_session.hpp"
#include <algorithm>
#include <cctype>

namespace goldsdk {
namespace {

std::tm localNow()
{
    std::time_t t = std::time(nullptr);
    std::tm out{};
#if defined(_WIN32)
    localtime_s(&out, &t);
#else
    localtime_r(&t, &out);
#endif
    return out;
}

std::tm resolveTm(const std::tm* p)
{
    return p ? *p : localNow();
}

} // namespace

std::string TradingSession::normalizeSource(std::string src)
{
    std::transform(src.begin(), src.end(), src.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    while (!src.empty() && std::isspace(static_cast<unsigned char>(src.front())))
        src.erase(src.begin());
    while (!src.empty() && std::isspace(static_cast<unsigned char>(src.back())))
        src.pop_back();
    if (src == "xau" || src == "london")
        return "gj";
    if (src == "ms")
        return "ms";
    if (src == "gj")
        return "gj";
    return "zs";
}

bool TradingSession::isTradingNow(const std::string& source, const std::tm* localTm)
{
    const std::string src = normalizeSource(source);
    const std::tm tm = resolveTm(localTm);
    int dow = tm.tm_wday == 0 ? 7 : tm.tm_wday;
    const int minutes = tm.tm_hour * 60 + tm.tm_min;

    if (src == "gj") {
        if (dow == 7)
            return false;
        if (dow == 6)
            return minutes < 4 * 60;
        if (dow == 1)
            return minutes >= 7 * 60;
        return true;
    }
    if (dow == 6 || dow == 7)
        return false;
    return minutes >= 9 * 60 && minutes <= 23 * 60 + 30;
}

std::string TradingSession::statusText(const std::string& source, const std::tm* localTm)
{
    // ASCII-only in SDK core; UI may map to localized text.
    const std::string src = normalizeSource(source);
    const std::tm tm = resolveTm(localTm);
    if (isTradingNow(src, &tm))
        return "open";
    int dow = tm.tm_wday == 0 ? 7 : tm.tm_wday;
    const int minutes = tm.tm_hour * 60 + tm.tm_min;
    if (src == "gj") {
        if (dow == 7 || (dow == 6 && minutes >= 4 * 60))
            return "weekend_closed";
        if (dow == 1 && minutes < 7 * 60)
            return "pre_open";
        return "closed";
    }
    if (dow == 6 || dow == 7)
        return "weekend_closed";
    if (minutes < 9 * 60)
        return "pre_open";
    return "closed";
}

std::string TradingSession::hoursDescription(const std::string& source)
{
    const std::string src = normalizeSource(source);
    if (src == "gj")
        return "XAU approx Mon 07:00 - Sat 04:00 (Beijing, indicative)";
    return "Accum. gold approx weekdays 09:00 - 23:30 (Beijing, indicative)";
}

} // namespace goldsdk
