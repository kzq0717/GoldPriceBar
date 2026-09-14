#pragma once
#include "goldsdk/types.hpp"
#include "goldsdk/http_client.hpp"
#include <functional>
#include <string>
#include <vector>

namespace goldsdk {

/** 行情解析与拉取（无 Qt）。UI 层可只调 parse*，网络仍用 Qt；或直接 fetch。 */
class PriceClient {
public:
    explicit PriceClient(HttpClient http = {});

    void setHttp(HttpClient http) { http_ = std::move(http); }
    HttpClient& http() { return http_; }

    /** 解析 jin.20021002.xyz 等主接口 JSON */
    static std::optional<PriceQuote> parseJinStyle(const std::string& jsonBody, DataSource prefer);
    static std::optional<PriceQuote> parseGoldApi(const std::string& jsonBody);
    static std::optional<PriceQuote> parseGoldPriceDev(const std::string& jsonBody);

    /** 同步拉取主源（失败返回 nullopt） */
    std::optional<PriceQuote> fetchPrimary(DataSource source) const;
    std::optional<PriceQuote> fetchBackup(int index) const;

    static std::string primaryUrl(DataSource source);
    static std::vector<std::string> backupUrls();

private:
    HttpClient http_;
};

} // namespace goldsdk
