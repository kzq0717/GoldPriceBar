#pragma once
#include <string>
#include <map>
#include <optional>
#include <filesystem>

namespace goldsdk {

/** 简易 INI（无 Qt QSettings）。 */
class Config {
public:
    bool loadFile(const std::filesystem::path& path);
    bool saveFile(const std::filesystem::path& path) const;

    std::optional<std::string> getString(const std::string& key) const;
    std::optional<int> getInt(const std::string& key) const;
    std::optional<double> getDouble(const std::string& key) const;
    std::optional<bool> getBool(const std::string& key) const;

    void set(const std::string& key, const std::string& value);
    void set(const std::string& key, int value);
    void set(const std::string& key, double value);
    void set(const std::string& key, bool value);

    /** 解析 databaseDir 并拼 gold_extremes.db */
    std::filesystem::path resolveDatabasePath(
        const std::filesystem::path& defaultPath) const;

private:
    std::map<std::string, std::string> kv_;
};

} // namespace goldsdk
