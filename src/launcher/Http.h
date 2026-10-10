#pragma once

#include <functional>
#include <string>
#include <vector>


namespace rtx::launcher::http {

struct Header {
    std::string name;
    std::string value;
};

struct Response {
    bool        ok      = false;
    int         status  = 0;
    std::string body;
    std::string detail;
    std::vector<Header> headers;                           // captured response headers
    std::string header(const std::string& name) const;    // case-insensitive lookup; "" if absent
};

// Where a request goes. Plain HTTP is refused unless the host is this PC (127.0.0.1, localhost, ::1);
// requests to this PC use their own session without a proxy.
struct Endpoint {
    std::wstring   host;
    unsigned short port   = 443;
    bool           secure = true;
};

// A POST with the caller's headers and raw body (any content type); the answer body is capped at 1 MB.
Response Post(const Endpoint& ep,
              const std::wstring& path,
              const std::vector<Header>& headers,
              const std::string& body);

Response PostJson(const std::wstring& host,
                  const std::wstring& path,
                  const std::vector<Header>& headers,
                  const std::string& body);

Response Get(const std::wstring& host,
             const std::wstring& path,
             const std::vector<Header>& headers);

Response Fetch(const std::wstring& host,
               const std::wstring& path,
               const std::vector<Header>& headers,
               std::size_t max_bytes = 16u * 1024 * 1024);

Response Download(const std::wstring& host,
                  const std::wstring& path,
                  const std::vector<Header>& headers,
                  const std::wstring& dest_path,
                  const std::function<void(long long, long long)>& on_progress,
                  long long max_bytes = 256ll * 1024 * 1024);   // larger is refused and deleted

Response Stream(const std::wstring& host,
                const std::wstring& path,
                const std::vector<Header>& headers,
                const std::function<bool(const char*, std::size_t)>& on_data,
                const std::function<bool(int)>& on_status = nullptr);

// Fire-and-forget worker pool: 2 workers, queue capped at 64, oldest job dropped when full.
void Enqueue(std::function<void()> job);
void Shutdown();

}  // namespace rtx::launcher::http
