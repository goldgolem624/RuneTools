#pragma once
// One-click self-update: manifest check, anti-downgrade, verified download, silent installer
// launch. The JS bindings (startUpdate / updateState / version) are installed by Bridge.cpp.
#include <JavaScriptCore/JavaScript.h>
#include <string>

namespace rtx::launcher {

// Running launcher version from the exe's own VERSIONINFO (app.rc FILEVERSION).
std::string running_version();
// Minimal JSON string-field reader (find "key":"value"); the backend responses are flat enough.
std::string json_str(const std::string& body, const char* key);
// SHA-256 of a file as lowercase hex ("" on failure).
std::string sha256_hex(const std::wstring& file);

JSValueRef StartUpdate(JSContextRef ctx, JSObjectRef, JSObjectRef, size_t, const JSValueRef[], JSValueRef*);
JSValueRef UpdateState(JSContextRef ctx, JSObjectRef, JSObjectRef, size_t, const JSValueRef[], JSValueRef*);
JSValueRef Version(JSContextRef ctx, JSObjectRef, JSObjectRef, size_t, const JSValueRef[], JSValueRef*);

}  // namespace rtx::launcher
