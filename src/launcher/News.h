#pragma once
// RuneScape news for the launcher home screen: one JSON document from runetools.io
// (/api/news, cover images already inlined by the server), refreshed in the background.
#include <JavaScriptCore/JavaScript.h>

namespace rtx::launcher {

// rtx.newsCached(): the cached document ("{}" until the first fetch lands). The first call
// starts the background refresher; never blocks the JS thread.
JSValueRef NewsCached(JSContextRef ctx, JSObjectRef, JSObjectRef, size_t, const JSValueRef[], JSValueRef*);

}  // namespace rtx::launcher
