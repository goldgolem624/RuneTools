#pragma once
// One-click self-update. JS bindings (startUpdate / updateState / version) live in Bridge.cpp.
#include <JavaScriptCore/JavaScript.h>
#include <string>
#include <vector>

namespace rtx::launcher {

// Running version from the exe's VERSIONINFO (app.rc FILEVERSION).
std::string running_version();
// Minimal JSON string-field reader.
std::string json_str(const std::string& body, const char* key);
// SHA-256 of a file as lowercase hex ("" on failure).
std::string sha256_hex(const std::wstring& file);

// Defined in Bridge.cpp.
extern const unsigned char kPluginPubKey[64];
bool plugin_b64_decode(const std::string& in, std::vector<std::uint8_t>& out);

JSValueRef StartUpdate(JSContextRef ctx, JSObjectRef, JSObjectRef, size_t, const JSValueRef[], JSValueRef*);
JSValueRef UpdateState(JSContextRef ctx, JSObjectRef, JSObjectRef, size_t, const JSValueRef[], JSValueRef*);
JSValueRef Version(JSContextRef ctx, JSObjectRef, JSObjectRef, size_t, const JSValueRef[], JSValueRef*);

}  // namespace rtx::launcher
