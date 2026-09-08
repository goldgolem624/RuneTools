#pragma once
// Helpers shared by the JS bridge translation units (Bridge.cpp, Update.cpp).
#include <JavaScriptCore/JavaScript.h>
#include <exception>
#include <string>
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

std::string js_to_utf8(JSContextRef ctx, JSValueRef v);
JSValueRef  utf8_to_js(JSContextRef ctx, const std::string& s);
std::string wide_to_utf8(const std::wstring& w);
std::string json_escape(const std::string& s);
std::string get_string_arg(JSContextRef ctx, size_t argc, const JSValueRef argv[], size_t idx);

}  // namespace rtx::launcher
