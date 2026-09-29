#pragma once
// Helpers shared by the JS bridge translation units (Bridge.cpp, Update.cpp).
#include <JavaScriptCore/JavaScript.h>
#include <exception>
#include <string>
#include <vector>
#include "../shared/Log.h"

namespace rtx::launcher {

inline constexpr wchar_t kUpdateHost[]  = L"runetools.io";
inline constexpr wchar_t kLatestPath[]  = L"/api/client/latest-version";
// Fallback only; the real version comes from the exe's FILEVERSION (app.rc).
inline constexpr const char* kAppVersion = "1.0.0";

// Detached-thread body wrapper: an escaping exception would std::terminate the launcher.
template <class F>
void guarded(const char* what, F&& f) {
    try { f(); }
    catch (const std::exception& e) { rtx::log::Launcher(std::string(what) + ": thread threw: " + e.what()); }
    catch (...) { rtx::log::Launcher(std::string(what) + ": thread threw (non-std)"); }
}

// JavaScript callback wrapper: a C++ exception unwinding into the JS engine would end the launcher,
// so one that escapes the body is logged and the call returns undefined.
void log_js_throw(JSContextRef ctx, JSObjectRef fn, const char* what) noexcept;

template <JSObjectCallAsFunctionCallback Fn>
JSValueRef js_guarded(JSContextRef ctx, JSObjectRef fn, JSObjectRef self,
                      size_t argc, const JSValueRef argv[], JSValueRef* exception) {
    try { return Fn(ctx, fn, self, argc, argv, exception); }
    catch (const std::exception& e) { log_js_throw(ctx, fn, e.what()); }
    catch (...) { log_js_throw(ctx, fn, "non-std"); }
    return JSValueMakeUndefined(ctx);
}

std::string js_to_utf8(JSContextRef ctx, JSValueRef v);
JSValueRef  utf8_to_js(JSContextRef ctx, const std::string& s);
std::string wide_to_utf8(const std::wstring& w);
std::string json_escape(const std::string& s);
std::string get_string_arg(JSContextRef ctx, size_t argc, const JSValueRef argv[], size_t idx);

// A parsed JSON value. An object keeps every member in order and get() returns the last one of a
// name, the one JSON.parse keeps, so C++ reads the same value the page and the website read.
struct JsonValue {
    enum Kind { Null, Bool, Number, String, Array, Object };
    Kind kind = Null;
    bool flag = false;               // a Bool's value
    std::string text;                // a String's value (UTF-8), or a Number as written
    std::vector<JsonValue> items;    // an Array's elements, or an Object's member values
    std::vector<std::string> keys;   // an Object's member names, one per item
    const JsonValue* get(const std::string& key) const;   // nullptr when absent or not an object
    std::string str(const std::string& key) const;        // that member when it is a string, else ""
};

// Parses a whole JSON text as JSON.parse does (RFC 8259), at any depth. False when it is not valid
// JSON. A value nested deeper than 64 levels is checked but read as null.
bool json_parse(const std::string& text, JsonValue& out);

}  // namespace rtx::launcher
