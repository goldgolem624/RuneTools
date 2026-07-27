#include "Utils.h"

namespace rtx::cache {

int NameHash(const std::string& name) {
    int h = 0;
    for (char c : name) h = h * 31 + (int)(unsigned char)c;
    return h;
}

}  // namespace rtx::cache
