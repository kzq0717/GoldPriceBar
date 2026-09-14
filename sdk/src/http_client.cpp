#include "goldsdk/http_client.hpp"

#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#  include <winhttp.h>
#  pragma comment(lib, "winhttp.lib")
#else
#  include <cstdio>
#  include <memory>
#endif

namespace goldsdk {
namespace {

#if defined(_WIN32)
std::wstring toWide(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), w.data(), n);
    return w;
}

HttpResponse winHttpGet(const std::string& url, int timeoutMs, const std::string& ua,
                        const std::string& proxyHost, int proxyPort,
                        const std::map<std::string, std::string>& headers) {
    HttpResponse out;
    URL_COMPONENTS uc{};
    uc.dwStructSize = sizeof(uc);
    uc.dwSchemeLength = (DWORD)-1;
    uc.dwHostNameLength = (DWORD)-1;
    uc.dwUrlPathLength = (DWORD)-1;
    uc.dwExtraInfoLength = (DWORD)-1;
    std::wstring wurl = toWide(url);
    if (!WinHttpCrackUrl(wurl.c_str(), (DWORD)wurl.size(), 0, &uc)) {
        out.error = "WinHttpCrackUrl failed";
        return out;
    }
    std::wstring host(uc.lpszHostName, uc.dwHostNameLength);
    std::wstring path(uc.lpszUrlPath, uc.dwUrlPathLength);
    if (uc.dwExtraInfoLength)
        path.append(uc.lpszExtraInfo, uc.dwExtraInfoLength);

    HINTERNET session = WinHttpOpen(toWide(ua).c_str(),
                                    WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) {
        out.error = "WinHttpOpen failed";
        return out;
    }
    if (!proxyHost.empty() && proxyPort > 0) {
        std::wstring px = toWide(proxyHost + ":" + std::to_string(proxyPort));
        WINHTTP_PROXY_INFO pi{};
        pi.dwAccessType = WINHTTP_ACCESS_TYPE_NAMED_PROXY;
        pi.lpszProxy = px.data();
        WinHttpSetOption(session, WINHTTP_OPTION_PROXY, &pi, sizeof(pi));
    }
    WinHttpSetTimeouts(session, timeoutMs, timeoutMs, timeoutMs, timeoutMs);

    INTERNET_PORT port = uc.nPort ? uc.nPort
                                  : (uc.nScheme == INTERNET_SCHEME_HTTPS ? INTERNET_DEFAULT_HTTPS_PORT
                                                                        : INTERNET_DEFAULT_HTTP_PORT);
    HINTERNET conn = WinHttpConnect(session, host.c_str(), port, 0);
    if (!conn) {
        out.error = "WinHttpConnect failed";
        WinHttpCloseHandle(session);
        return out;
    }
    DWORD flags = (uc.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET req = WinHttpOpenRequest(conn, L"GET", path.c_str(), nullptr,
                                       WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!req) {
        out.error = "WinHttpOpenRequest failed";
        WinHttpCloseHandle(conn);
        WinHttpCloseHandle(session);
        return out;
    }
    std::wstring hdrs;
    for (const auto& kv : headers) {
        hdrs += toWide(kv.first + ": " + kv.second + "\r\n");
    }
    BOOL ok = WinHttpSendRequest(req,
                                 hdrs.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : hdrs.c_str(),
                                 hdrs.empty() ? 0 : (DWORD)-1,
                                 WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
    if (!ok || !WinHttpReceiveResponse(req, nullptr)) {
        out.error = "WinHttp request failed";
        WinHttpCloseHandle(req);
        WinHttpCloseHandle(conn);
        WinHttpCloseHandle(session);
        return out;
    }
    DWORD status = 0, sz = sizeof(status);
    WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &status, &sz, WINHTTP_NO_HEADER_INDEX);
    out.status = (int)status;
    std::string body;
    for (;;) {
        DWORD avail = 0;
        if (!WinHttpQueryDataAvailable(req, &avail) || avail == 0)
            break;
        std::string buf(avail, '\0');
        DWORD read = 0;
        if (!WinHttpReadData(req, buf.data(), avail, &read))
            break;
        body.append(buf.data(), read);
    }
    out.body = std::move(body);
    WinHttpCloseHandle(req);
    WinHttpCloseHandle(conn);
    WinHttpCloseHandle(session);
    return out;
}
#endif

} // namespace

HttpResponse HttpClient::get(const std::string& url,
                             const std::map<std::string, std::string>& headers) const {
#if defined(_WIN32)
    return winHttpGet(url, timeoutMs_, userAgent_, proxyHost_, proxyPort_, headers);
#else
    // 非 Windows：尝试 curl 命令行（无 libcurl 时的兜底）
    HttpResponse out;
    std::string cmd = "curl -sS -L --max-time " + std::to_string(std::max(1, timeoutMs_ / 1000));
    cmd += " -A \"" + userAgent_ + "\"";
    if (!proxyHost_.empty())
        cmd += " -x " + proxyHost_ + ":" + std::to_string(proxyPort_);
    for (const auto& kv : headers)
        cmd += " -H \"" + kv.first + ": " + kv.second + "\"";
    cmd += " \"" + url + "\"";
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) {
        out.error = "popen curl failed";
        return out;
    }
    char buf[4096];
    while (fgets(buf, sizeof(buf), pipe))
        out.body += buf;
    const int rc = pclose(pipe);
    out.status = (rc == 0) ? 200 : 0;
    if (rc != 0 && out.body.empty())
        out.error = "curl failed";
    return out;
#endif
}

} // namespace goldsdk
