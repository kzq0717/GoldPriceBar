#pragma once
#include <string>
#include <optional>
#include <map>

namespace goldsdk {

struct HttpResponse {
    int status = 0;
    std::string body;
    std::string error;
};

/** 同步 HTTP GET（无 Qt）。Windows 使用 WinHTTP；其它平台优先 libcurl（若链接）。 */
class HttpClient {
public:
    HttpClient() = default;
    void setTimeoutMs(int ms) { timeoutMs_ = ms; }
    void setUserAgent(std::string ua) { userAgent_ = std::move(ua); }
    void setProxy(std::string host, int port) { proxyHost_ = std::move(host); proxyPort_ = port; }
    void clearProxy() { proxyHost_.clear(); proxyPort_ = 0; }

    HttpResponse get(const std::string& url,
                     const std::map<std::string, std::string>& headers = {}) const;

private:
    int timeoutMs_ = 12000;
    std::string userAgent_ = "GoldSdk/1.0";
    std::string proxyHost_;
    int proxyPort_ = 0;
};

} // namespace goldsdk
