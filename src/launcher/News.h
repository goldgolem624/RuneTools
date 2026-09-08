#pragma once
// RuneScape news for the home screen: runetools.io /api/news, refreshed in the background.
#include <JavaScriptCore/JavaScript.h>

namespace rtx::launcher {

// Cached document ("{}" until the first fetch). First call starts the refresher; never blocks.
JSValueRef NewsCached(JSContextRef ctx, JSObjectRef, JSObjectRef, size_t, const JSValueRef[], JSValueRef*);

}  // namespace rtx::launcher
