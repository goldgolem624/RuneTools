#pragma once

#include <string>

namespace rtx::cache {

// Jagex-style ASCII name hash (keys archives/files by short identifiers).
// Currently unused by the lookup paths.
int NameHash(const std::string& name);

}  // namespace rtx::cache
