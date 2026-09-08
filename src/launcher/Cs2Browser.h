// CS2 Scripts panel backend: runs the cs2export sidecar (separate GPL process) and serves its output.
#pragma once
#include <string>

namespace cs2browser {

// %USERPROFILE%\RuneToolsX\cs2 (created on demand).
std::wstring OutDir();

// {"running":bool,"sidecar":bool,"meta":<meta.json|null>,"progress":<progress.json|null>}
std::string StatusJson();

// Spawn the sidecar (node dist/cs2export.js <outdir>). {"ok":true} or {"err":"..."}.
std::string StartExtract();

// Terminate a running extraction. {"ok":bool}
std::string Cancel();

// Case-insensitive substring search over every extracted script.
// {"query":"..","files":N,"truncated":bool,"results":[{"id":n,"line":n,"text":".."}]}
std::string SearchJson(const std::string& query, int max_results);

// One script's text, chunked. {"id":n,"size":n,"offset":n,"more":bool,"text":".."}
std::string ScriptJson(int id, size_t offset);

// {"varbit":{"<id>":"name"},"varp":{"<id>":"name"}} or {} when never extracted.
std::string NamesJson();
// Baked switch maps from the last extraction; "{}" when none has been run.
std::string SwitchesJson();

}  // namespace cs2browser
