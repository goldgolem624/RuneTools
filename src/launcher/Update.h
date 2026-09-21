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
// The same download, check and install, for the version already running: puts missing files back.
JSValueRef StartRepair(JSContextRef ctx, JSObjectRef, JSObjectRef, size_t, const JSValueRef[], JSValueRef*);
JSValueRef UpdateState(JSContextRef ctx, JSObjectRef, JSObjectRef, size_t, const JSValueRef[], JSValueRef*);
JSValueRef Version(JSContextRef ctx, JSObjectRef, JSObjectRef, size_t, const JSValueRef[], JSValueRef*);
// Files of the install that are not on disk any more, as {"missing":[...],"dir":"..."}. Security software that
// takes a dislike to one of them removes it without a word, and what breaks then gives no hint of why.
JSValueRef InstallHealth(JSContextRef ctx, JSObjectRef, JSObjectRef, size_t, const JSValueRef[], JSValueRef*);
JSValueRef OpenInstallDir(JSContextRef ctx, JSObjectRef, JSObjectRef, size_t, const JSValueRef[], JSValueRef*);

}  // namespace rtx::launcher
