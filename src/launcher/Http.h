#pragma once

#include <functional>
#include <string>
#include <vector>

// Synchronous WinHTTP wrapper. Callers must run off the UI thread.

namespace rtx::launcher::http {

struct Header {
    std::string name;
    std::string value;
};

struct Response {
    // `ok` = reached the server (status may still be 4xx/5xx); transport failures clear it.
    bool        ok      = false;
    int         status  = 0;
    std::string body;
    std::string detail;
    std::vector<Header> headers;                           // captured response headers
    std::string header(const std::string& name) const;    // case-insensitive lookup; "" if absent
};

// POST to https://host<path>. TLS 1.2 enforced.
Response PostJson(const std::wstring& host,
                  const std::wstring& path,
                  const std::vector<Header>& headers,
                  const std::string& body);

// GET https://host<path> to memory, 1 MB cap.
Response Get(const std::wstring& host,
             const std::wstring& path,
             const std::vector<Header>& headers);

// GET https://host<path> to memory with a configurable cap and captured response headers.
Response Fetch(const std::wstring& host,
               const std::wstring& path,
               const std::vector<Header>& headers,
               std::size_t max_bytes = 16u * 1024 * 1024);

// GET https://host<path> streamed to `dest_path` (no cap). `total` is 0 without Content-Length.
// `.ok && .status==200` means fully written; a partial or failed file is deleted.
Response Download(const std::wstring& host,
                  const std::wstring& path,
                  const std::vector<Header>& headers,
                  const std::wstring& dest_path,
                  const std::function<void(long long, long long)>& on_progress);

// Long-lived GET stream (SSE). Blocks until the server closes, on_data returns false, or the
// 120s receive timeout hits. `on_status` fires before any body bytes; return false to abort
// (.ok stays false) so error bodies never reach on_data.
Response Stream(const std::wstring& host,
                const std::wstring& path,
                const std::vector<Header>& headers,
                const std::function<bool(const char*, std::size_t)>& on_data,
                const std::function<bool(int)>& on_status = nullptr);

// Fire-and-forget worker pool: 2 workers, queue capped at 64, oldest job dropped when full.
void Enqueue(std::function<void()> job);
// Stops the workers, discarding pending jobs. Safe if never started.
void Shutdown();

}  // namespace rtx::launcher::http
