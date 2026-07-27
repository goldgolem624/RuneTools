#include "Http.h"

#include <Windows.h>
#include <winhttp.h>

#pragma comment(lib, "winhttp.lib")

namespace rtx::launcher::http {

namespace {

struct SessionScope { HINTERNET h = nullptr; ~SessionScope() { if (h) WinHttpCloseHandle(h); } };
struct ConnectScope { HINTERNET h = nullptr; ~ConnectScope() { if (h) WinHttpCloseHandle(h); } };
struct RequestScope { HINTERNET h = nullptr; ~RequestScope() { if (h) WinHttpCloseHandle(h); } };

std::wstring wide_from_utf8(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(),
                                nullptr, 0);
    std::wstring out(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(),
                        out.data(), n);
    return out;
}

std::string utf8_from_wide(const std::wstring& s) {
    if (s.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(),
                                nullptr, 0, nullptr, nullptr);
    std::string out(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(),
                        out.data(), n, nullptr, nullptr);
    return out;
}

std::string format_winhttp_error(DWORD err) {
    HMODULE mod = GetModuleHandleW(L"winhttp.dll");
    char buf[256] = {};
    DWORD len = FormatMessageA(
        FORMAT_MESSAGE_FROM_HMODULE | FORMAT_MESSAGE_FROM_SYSTEM |
        FORMAT_MESSAGE_IGNORE_INSERTS,
        mod, err, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        buf, sizeof(buf), nullptr);
    std::string out = "WinHTTP error " + std::to_string(err);
    if (len > 0) {
        std::string msg(buf, len);
        while (!msg.empty() && (msg.back() == '\r' || msg.back() == '\n' || msg.back() == ' '))
            msg.pop_back();
        out += " (" + msg + ")";
    }
    return out;
}

// Shared request engine. `sink(data,len)` consumes each body chunk (return false
// to abort -- set out.detail first); `on_progress(received,total)` fires per chunk
// when set; `on_status(code)` fires after the status line, before the body -- return
// false to abort with .ok == false (the body is never drained). Fills out.status/ok/detail.
void do_request(Response& out,
                const std::wstring& host, const std::wstring& path,
                const wchar_t* verb,
                const std::vector<Header>& headers,
                const std::string& body,
                const std::function<bool(const char*, DWORD)>& sink,
                const std::function<void(long long, long long)>& on_progress,
                const std::function<bool(int)>& on_status = nullptr) {
    SessionScope sess;
    sess.h = WinHttpOpen(L"RuneToolsX/0.1",
                         WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                         WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!sess.h) { out.detail = format_winhttp_error(GetLastError()); return; }

    DWORD secProtocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
    WinHttpSetOption(sess.h, WINHTTP_OPTION_SECURE_PROTOCOLS,
                     &secProtocols, sizeof(secProtocols));
    // Long receive timeout: the update download can be tens of MB.
    WinHttpSetTimeouts(sess.h, 10000, 10000, 30000, 120000);

    ConnectScope conn;
    conn.h = WinHttpConnect(sess.h, host.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!conn.h) { out.detail = format_winhttp_error(GetLastError()); return; }

    RequestScope req;
    req.h = WinHttpOpenRequest(conn.h, verb, path.c_str(), nullptr,
                               WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                               WINHTTP_FLAG_SECURE);
    if (!req.h) { out.detail = format_winhttp_error(GetLastError()); return; }

    // Per-row AddRequestHeaders; packing into pwszHeaders intermittently
    // drops headers on the server side.
    bool saw_content_type = false;
    for (const auto& h : headers) {
        if (h.name.empty()) continue;
        if (_stricmp(h.name.c_str(), "Content-Type") == 0) saw_content_type = true;
        std::wstring line = wide_from_utf8(h.name);
        line.append(L": ");
        line.append(wide_from_utf8(h.value));
        WinHttpAddRequestHeaders(req.h, line.c_str(), (DWORD)-1L,
            WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);
    }
    if (!saw_content_type && !body.empty()) {
        const wchar_t* ct = L"Content-Type: application/json";
        WinHttpAddRequestHeaders(req.h, ct, (DWORD)-1L,
            WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);
    }

    if (!WinHttpSendRequest(req.h, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                            body.empty() ? WINHTTP_NO_REQUEST_DATA : (LPVOID)body.data(),
                            (DWORD)body.size(), (DWORD)body.size(), 0)) {
        out.detail = format_winhttp_error(GetLastError()); return;
    }
    if (!WinHttpReceiveResponse(req.h, nullptr)) {
        out.detail = format_winhttp_error(GetLastError()); return;
    }

    DWORD status = 0, status_size = sizeof(status);
    if (!WinHttpQueryHeaders(req.h,
                             WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                             WINHTTP_HEADER_NAME_BY_INDEX,
                             &status, &status_size, WINHTTP_NO_HEADER_INDEX)) {
        out.detail = format_winhttp_error(GetLastError()); return;
    }
    out.status = (int)status;
    if (on_status && !on_status(out.status)) {
        if (out.detail.empty()) out.detail = "rejected by status " + std::to_string(out.status);
        return;                                      // .ok stays false; body never drained
    }

    // Capture response headers (raw CRLF block) so callers can read X-Plugin-* etc.
    {
        DWORD hsz = 0;
        WinHttpQueryHeaders(req.h, WINHTTP_QUERY_RAW_HEADERS_CRLF,
                            WINHTTP_HEADER_NAME_BY_INDEX, nullptr, &hsz, WINHTTP_NO_HEADER_INDEX);
        if (hsz > 0) {
            std::wstring raw(hsz / sizeof(wchar_t), L'\0');
            if (WinHttpQueryHeaders(req.h, WINHTTP_QUERY_RAW_HEADERS_CRLF,
                                    WINHTTP_HEADER_NAME_BY_INDEX, raw.data(), &hsz,
                                    WINHTTP_NO_HEADER_INDEX)) {
                std::string all = utf8_from_wide(raw.c_str());
                size_t p = 0;
                while (p < all.size()) {
                    size_t e = all.find("\r\n", p);
                    if (e == std::string::npos) e = all.size();
                    std::string line = all.substr(p, e - p);
                    p = e + 2;
                    size_t c = line.find(':');
                    if (c != std::string::npos) {
                        std::string nm = line.substr(0, c);
                        std::string vl = line.substr(c + 1);
                        while (!vl.empty() && (vl.front() == ' ' || vl.front() == '\t')) vl.erase(vl.begin());
                        out.headers.push_back({ nm, vl });
                    }
                }
            }
        }
    }

    long long total = 0;
    {
        DWORD len = 0, sz = sizeof(len);
        if (WinHttpQueryHeaders(req.h,
                                WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER,
                                WINHTTP_HEADER_NAME_BY_INDEX, &len, &sz,
                                WINHTTP_NO_HEADER_INDEX))
            total = (long long)len;
    }

    long long got = 0;
    for (;;) {
        DWORD avail = 0;
        if (!WinHttpQueryDataAvailable(req.h, &avail)) {
            out.detail = format_winhttp_error(GetLastError()); return;
        }
        if (avail == 0) break;
        std::string chunk(avail, '\0');
        DWORD read = 0;
        if (!WinHttpReadData(req.h, chunk.data(), avail, &read)) {
            out.detail = format_winhttp_error(GetLastError()); return;
        }
        if (read == 0) break;
        if (sink && !sink(chunk.data(), read)) return;   // sink set out.detail
        got += read;
        if (on_progress) on_progress(got, total);
    }
    out.ok = true;
    if (out.detail.empty()) out.detail = "OK";
}

bool append_capped(Response& out, const char* d, DWORD n) {
    if (out.body.size() + n > 1u * 1024 * 1024) { out.detail = "response too large"; return false; }
    out.body.append(d, n);
    return true;
}

}  // namespace

Response PostJson(const std::wstring& host, const std::wstring& path,
                  const std::vector<Header>& headers, const std::string& body) {
    Response out;
    do_request(out, host, path, L"POST", headers, body,
        [&out](const char* d, DWORD n) { return append_capped(out, d, n); }, nullptr);
    return out;
}

Response Get(const std::wstring& host, const std::wstring& path,
             const std::vector<Header>& headers) {
    Response out;
    do_request(out, host, path, L"GET", headers, std::string(),
        [&out](const char* d, DWORD n) { return append_capped(out, d, n); }, nullptr);
    return out;
}

std::string Response::header(const std::string& name) const {
    for (const auto& h : headers)
        if (_stricmp(h.name.c_str(), name.c_str()) == 0) return h.value;
    return {};
}

Response Fetch(const std::wstring& host, const std::wstring& path,
               const std::vector<Header>& headers, std::size_t max_bytes) {
    Response out;
    do_request(out, host, path, L"GET", headers, std::string(),
        [&out, max_bytes](const char* d, DWORD n) {
            if (out.body.size() + (size_t)n > max_bytes) { out.detail = "response too large"; return false; }
            out.body.append(d, n);
            return true;
        }, nullptr);
    return out;
}

Response Download(const std::wstring& host, const std::wstring& path,
                  const std::vector<Header>& headers, const std::wstring& dest_path,
                  const std::function<void(long long, long long)>& on_progress) {
    Response out;
    HANDLE f = CreateFileW(dest_path.c_str(), GENERIC_WRITE, 0, nullptr,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) { out.detail = "cannot open destination file"; return out; }
    do_request(out, host, path, L"GET", headers, std::string(),
        [f](const char* d, DWORD n) { DWORD w = 0; return WriteFile(f, d, n, &w, nullptr) && w == n; },
        on_progress);
    CloseHandle(f);
    if (!out.ok || out.status != 200) DeleteFileW(dest_path.c_str());   // no partial files
    return out;
}

Response Stream(const std::wstring& host, const std::wstring& path,
                const std::vector<Header>& headers,
                const std::function<bool(const char*, std::size_t)>& on_data,
                const std::function<bool(int)>& on_status) {
    Response out;
    do_request(out, host, path, L"GET", headers, std::string(),
        [&on_data](const char* d, DWORD n) { return on_data ? on_data(d, (std::size_t)n) : true; }, nullptr,
        on_status);
    return out;
}

}  // namespace rtx::launcher::http
