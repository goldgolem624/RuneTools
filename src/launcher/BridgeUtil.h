#pragma once
// Helpers shared by the JS bridge translation units (Bridge.cpp, Update.cpp): JSValue <-> UTF-8
// conversion, JSON string escaping, the detached-thread guard, and the RuneTools server host.
#include <JavaScriptCore/JavaScript.h>
#include <exception>
#include <string>
#include "../shared/Log.h"

namespace rtx::launcher {

// The RuneTools server and the two public update endpoints (party-sync, VoS, world events and
// the self-updater all talk to the same host).
inline constexpr wchar_t kUpdateHost[]  = L"runetools.io";
inline constexpr wchar_t kLatestPath[]  = L"/api/client/latest-version";
// Fallback launcher version; the real one is read from the exe's FILEVERSION (app.rc).
inline constexpr const char* kAppVersion = "1.0.0";

// Body wrapper for the long-lived detached threads: an escaping exception would std::terminate
// the whole launcher, so log it and let the thread end.
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
