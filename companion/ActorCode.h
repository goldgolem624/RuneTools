#pragma once
// Where an actor keeps its position, read off its own code. The engine asks an actor for its
// position through a virtual getter (the COORD handler calls it: rule actor.posSlot names the
// vtable slot), and that getter is `lea rax,[rcx+position]; ret`. Fine east, up and north follow as
// three floats; the movement route sits 8 bytes before them (950-1 and 951-1 alike).
#include <cstdint>
#include <cstring>

namespace rtx::actorcode {

// The position offset from the getter's first bytes; 0 when they are not that shape.
inline std::uint32_t PositionFromGetter(const std::uint8_t* b) {
    if (b[0] != 0x48 || b[1] != 0x8D || b[2] != 0x81 || b[7] != 0xC3) return 0;   // lea rax,[rcx+disp32] ; ret
    std::uint32_t d; std::memcpy(&d, b + 3, 4);
    return d >= 0x100 && d < 0x2000 ? d : 0;
}

}  // namespace rtx::actorcode
