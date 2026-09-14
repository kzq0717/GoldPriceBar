#pragma once
#include <string>
#include <optional>
#include <cmath>

namespace goldsdk::json {

/** 极简 JSON 数值/字符串提取（不依赖第三方）。 */
std::optional<double> findNumber(const std::string& json, const std::string& key);
std::optional<std::string> findString(const std::string& json, const std::string& key);
/** 在对象片段中找 key */
std::optional<double> findNumberIn(const std::string& fragment, const std::string& key);

} // namespace goldsdk::json
