#include "goldsdk/price_client.hpp"
#include "goldsdk/json_util.hpp"
#include <chrono>

namespace goldsdk {

PriceClient::PriceClient(HttpClient http) : http_(std::move(http)) {}

std::string PriceClient::primaryUrl(DataSource source) {
    // 与现有客户端主源保持一致（公开聚合）
    switch (source) {
    case DataSource::Ms:
        return "https://jin.20021002.xyz/ms";
    case DataSource::Gj:
        return "https://jin.20021002.xyz/gj";
    default:
        return "https://jin.20021002.xyz/zs";
    }
}

std::vector<std::string> PriceClient::backupUrls() {
    return {
        "https://api.gold-api.com/price/XAU",
        "https://api.goldprice.org/api/spot/v1/historical/XAU/USD"
    };
}

static int64_t nowMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

std::optional<PriceQuote> PriceClient::parseJinStyle(const std::string& body, DataSource prefer) {
    // 兼容 price / last / close 等字段
    auto price = json::findNumber(body, "price");
    if (!price) price = json::findNumber(body, "last");
    if (!price) price = json::findNumber(body, "close");
    if (!price) price = json::findNumber(body, "p");
    if (!price || *price <= 0.0) return std::nullopt;

    PriceQuote q;
    q.price = *price;
    q.change = json::findNumber(body, "change").value_or(
        json::findNumber(body, "chg").value_or(0.0));
    q.source = toSourceCode(prefer);
    q.epochMs = nowMs();
    if (prefer == DataSource::Gj) {
        q.currency = "USD";
        q.unit = "USD/oz";
        q.sourceName = "伦敦金";
    } else if (prefer == DataSource::Ms) {
        q.currency = "CNY";
        q.unit = "CNY/g";
        q.sourceName = "民生积存金";
    } else {
        q.currency = "CNY";
        q.unit = "CNY/g";
        q.sourceName = "浙商积存金";
    }
    if (auto n = json::findString(body, "name"))
        q.sourceName = *n;
    return q;
}

std::optional<PriceQuote> PriceClient::parseGoldApi(const std::string& body) {
    auto price = json::findNumber(body, "price");
    if (!price || *price <= 0.0) return std::nullopt;
    PriceQuote q;
    q.price = *price;
    q.source = "gj";
    q.sourceName = "Gold-API";
    q.currency = "USD";
    q.unit = "USD/oz";
    q.epochMs = nowMs();
    return q;
}

std::optional<PriceQuote> PriceClient::parseGoldPriceDev(const std::string& body) {
    auto price = json::findNumber(body, "price");
    if (!price) price = json::findNumber(body, "ask");
    if (!price || *price <= 0.0) return std::nullopt;
    PriceQuote q;
    q.price = *price;
    q.source = "gj";
    q.sourceName = "GoldPrice";
    q.currency = "USD";
    q.unit = "USD/oz";
    q.epochMs = nowMs();
    return q;
}

std::optional<PriceQuote> PriceClient::fetchPrimary(DataSource source) const {
    const auto resp = http_.get(primaryUrl(source));
    if (resp.status < 200 || resp.status >= 300 || resp.body.empty())
        return std::nullopt;
    return parseJinStyle(resp.body, source);
}

std::optional<PriceQuote> PriceClient::fetchBackup(int index) const {
    const auto urls = backupUrls();
    if (index < 0 || index >= (int)urls.size())
        return std::nullopt;
    const auto resp = http_.get(urls[index]);
    if (resp.body.empty())
        return std::nullopt;
    if (index == 0)
        return parseGoldApi(resp.body);
    return parseGoldPriceDev(resp.body);
}

} // namespace goldsdk
