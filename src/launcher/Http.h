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
                  const std::function<void(long long, long long)>& on_progress);

Response Stream(const std::wstring& host,
                const std::wstring& path,
                const std::vector<Header>& headers,
                const std::function<bool(const char*, std::size_t)>& on_data,
                const std::function<bool(int)>& on_status = nullptr);

// Fire-and-forget worker pool: 2 workers, queue capped at 64, oldest job dropped when full.
void Enqueue(std::function<void()> job);
void Shutdown();

}  // namespace rtx::launcher::http
