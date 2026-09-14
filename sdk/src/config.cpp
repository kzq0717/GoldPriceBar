#include "goldsdk/config.hpp"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>

namespace goldsdk {
namespace {

std::string trim(std::string s) {
    while (!s.empty() && std::isspace((unsigned char)s.front())) s.erase(s.begin());
    while (!s.empty() && std::isspace((unsigned char)s.back())) s.pop_back();
    return s;
}
std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return (char)std::tolower(c); });
    return s;
}

} // namespace

bool Config::loadFile(const std::filesystem::path& path) {
    std::ifstream in(path);
    if (!in) return false;
    kv_.clear();
    std::string line;
    while (std::getline(in, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;
        if (line.front() == '[' && line.back() == ']') continue;
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        auto k = trim(line.substr(0, eq));
        auto v = trim(line.substr(eq + 1));
        if (!v.empty() && ((v.front() == '"' && v.back() == '"') ||
                           (v.front() == '\'' && v.back() == '\'')))
            v = v.substr(1, v.size() - 2);
        if (!k.empty())
            kv_[k] = v;
    }
    return true;
}

bool Config::saveFile(const std::filesystem::path& path) const {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path);
    if (!out) return false;
    out << "# GoldSdk config\n";
    for (const auto& [k, v] : kv_)
        out << k << "=" << v << "\n";
    return true;
}

std::optional<std::string> Config::getString(const std::string& key) const {
    auto it = kv_.find(key);
    if (it == kv_.end()) {
        // case-insensitive
        for (const auto& [k, v] : kv_)
            if (lower(k) == lower(key)) return v;
        return std::nullopt;
    }
    return it->second;
}

std::optional<int> Config::getInt(const std::string& key) const {
    auto s = getString(key);
    if (!s) return std::nullopt;
    try { return std::stoi(*s); } catch (...) { return std::nullopt; }
}

std::optional<double> Config::getDouble(const std::string& key) const {
    auto s = getString(key);
    if (!s) return std::nullopt;
    try { return std::stod(*s); } catch (...) { return std::nullopt; }
}

std::optional<bool> Config::getBool(const std::string& key) const {
    auto s = getString(key);
    if (!s) return std::nullopt;
    const auto v = lower(*s);
    if (v == "1" || v == "true" || v == "yes" || v == "on") return true;
    if (v == "0" || v == "false" || v == "no" || v == "off") return false;
    return std::nullopt;
}

void Config::set(const std::string& key, const std::string& value) { kv_[key] = value; }
void Config::set(const std::string& key, int value) { kv_[key] = std::to_string(value); }
void Config::set(const std::string& key, double value) {
    std::ostringstream os;
    os << value;
    kv_[key] = os.str();
}
void Config::set(const std::string& key, bool value) { kv_[key] = value ? "true" : "false"; }

std::filesystem::path Config::resolveDatabasePath(const std::filesystem::path& defaultPath) const {
    auto dir = getString("databaseDir");
    if (!dir || dir->empty())
        return defaultPath;
    std::filesystem::path p(*dir);
    if (p.extension() == ".db")
        return p;
    return p / "gold_extremes.db";
}

} // namespace goldsdk
