#include "goldsdk/json_util.hpp"
#include <cctype>
#include <cstdlib>

namespace goldsdk::json {
namespace {

std::string::size_type findKey(const std::string& s, const std::string& key, std::string::size_type from = 0) {
    const std::string pat = "\"" + key + "\"";
    auto pos = from;
    while (true) {
        pos = s.find(pat, pos);
        if (pos == std::string::npos) return std::string::npos;
        // 确保是 key 而非值中偶然匹配：后面应有冒号
        auto i = pos + pat.size();
        while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
        if (i < s.size() && s[i] == ':') return pos;
        pos += 1;
    }
}

} // namespace

std::optional<double> findNumberIn(const std::string& fragment, const std::string& key) {
    auto pos = findKey(fragment, key);
    if (pos == std::string::npos) return std::nullopt;
    pos = fragment.find(':', pos);
    if (pos == std::string::npos) return std::nullopt;
    ++pos;
    while (pos < fragment.size() && std::isspace(static_cast<unsigned char>(fragment[pos]))) ++pos;
    if (pos >= fragment.size()) return std::nullopt;
    char* end = nullptr;
    const double v = std::strtod(fragment.c_str() + pos, &end);
    if (end == fragment.c_str() + pos) return std::nullopt;
    if (!std::isfinite(v)) return std::nullopt;
    return v;
}

std::optional<double> findNumber(const std::string& json, const std::string& key) {
    return findNumberIn(json, key);
}

std::optional<std::string> findString(const std::string& json, const std::string& key) {
    auto pos = findKey(json, key);
    if (pos == std::string::npos) return std::nullopt;
    pos = json.find(':', pos);
    if (pos == std::string::npos) return std::nullopt;
    ++pos;
    while (pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos]))) ++pos;
    if (pos >= json.size() || json[pos] != '"') return std::nullopt;
    ++pos;
    std::string out;
    while (pos < json.size() && json[pos] != '"') {
        if (json[pos] == '\\' && pos + 1 < json.size()) {
            out.push_back(json[pos + 1]);
            pos += 2;
            continue;
        }
        out.push_back(json[pos++]);
    }
    return out;
}

} // namespace goldsdk::json
