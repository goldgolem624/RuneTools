#include "BridgeUtil.h"

#include <Windows.h>
#include <cstdio>

namespace rtx::launcher {

std::string js_to_utf8(JSContextRef ctx, JSValueRef v) {
    JSStringRef s = JSValueToStringCopy(ctx, v, nullptr);
    if (!s) return {};
    size_t bytes = JSStringGetMaximumUTF8CStringSize(s);
    std::string out(bytes, '\0');
    size_t n = JSStringGetUTF8CString(s, out.data(), bytes);
    JSStringRelease(s);
    if (n) out.resize(n - 1);
    return out;
}

JSValueRef utf8_to_js(JSContextRef ctx, const std::string& s) {
    JSStringRef js = JSStringCreateWithUTF8CString(s.c_str());
    JSValueRef out = JSValueMakeString(ctx, js);
    JSStringRelease(js);
    return out;
}

std::string wide_to_utf8(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(),
                                nullptr, 0, nullptr, nullptr);
    std::string out(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(),
                        out.data(), n, nullptr, nullptr);
    return out;
}

std::string json_escape(const std::string& s) {
    std::string out; out.reserve(s.size() + 2);
    for (char c : s) {
        switch (c) {
        case '"':  out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n";  break;
        case '\r': out += "\\r";  break;
        case '\t': out += "\\t";  break;
        default:
            if (static_cast<unsigned char>(c) < 0x20) {
                char buf[8];
                std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                out += buf;
            } else {
                out += c;
            }
        }
    }
    return out;
}

std::string get_string_arg(JSContextRef ctx, size_t argc,
                           const JSValueRef argv[], size_t idx) {
    if (idx >= argc) return {};
    return js_to_utf8(ctx, argv[idx]);
}

}  // namespace rtx::launcher
