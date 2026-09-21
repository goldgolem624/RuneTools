#pragma once
// Marks one scenery object as hovered for the game's own outline, for this frame.

namespace rtx::scenehover {

// `x`, `y` are the tile the scene walk reports for the object and `id` its definition, as the
// launcher read them from the scene share. Call once per presented frame while the object
// should stay outlined; the game fades it out by itself once the calls stop. Returns false
// when the object is not in the last walk or does not look the way this expects.
bool Mark(int x, int y, int id);

}  // namespace rtx::scenehover
