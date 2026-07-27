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
    // `ok` means the request reached the server. `status` may still be
    // 4xx/5xx; transport failures (DNS/TLS/timeout) set `ok=false`.
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

// GET https://host<path>; body returned in-memory (1 MB cap) -- for small JSON
// like the update manifest. Same TLS/headers as PostJson.
Response Get(const std::wstring& host,
             const std::wstring& path,
             const std::vector<Header>& headers);

// GET https://host<path> to memory with a configurable cap and captured response
// headers -- for plugin bundles: the bytes are hashed/verified and the
// X-Plugin-Signature/Hash/Version headers are read.
Response Fetch(const std::wstring& host,
               const std::wstring& path,
               const std::vector<Header>& headers,
               std::size_t max_bytes = 16u * 1024 * 1024);

// GET https://host<path> streamed to `dest_path` on disk (no size cap) -- for the
// update download. `on_progress(received, total)` fires per chunk (total is 0 if
// the server sends no Content-Length). `.ok && .status==200` means fully written;
// a partial/failed file is deleted. Overwrites dest_path.
Response Download(const std::wstring& host,
                  const std::wstring& path,
                  const std::vector<Header>& headers,
                  const std::wstring& dest_path,
                  const std::function<void(long long, long long)>& on_progress);

// GET https://host<path> as a LONG-LIVED stream (e.g. Server-Sent Events). `on_data(data,len)`
// is invoked per chunk AS bytes arrive; return false to abort. Blocks until the server closes the
// connection (or on_data aborts), so run on a worker thread and reconnect in a loop. The session
// receive timeout (120s) means a stream must produce data (e.g. an SSE heartbeat) within that
// window or it returns and the caller reconnects.
// `on_status(code)` fires once, after the response headers land and BEFORE any body bytes reach
// on_data; return false to abort (Response keeps .status, .ok stays false). Without it a 429/5xx
// error body would stream into the caller's parser and the completed drain would look like a
// clean end (.ok == true).
Response Stream(const std::wstring& host,
                const std::wstring& path,
                const std::vector<Header>& headers,
                const std::function<bool(const char*, std::size_t)>& on_data,
                const std::function<bool(int)>& on_status = nullptr);

}  // namespace rtx::launcher::http
