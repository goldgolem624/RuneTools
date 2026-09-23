#pragma once
// Marks one scenery object as hovered for the game's own outline, for this frame.

namespace rtx::scenehover {

// `x`, `y` are the tile the scene walk reports for the object and `id` its definition, as the
// launcher read them from the scene share. Call once per presented frame while the object
// should stay outlined; the game fades it out by itself once the calls stop. Returns false
// when the object is not in the last walk or does not look the way this expects.
bool Mark(int x, int y, int id);
// Development only: logs the outline state of the object marked last, every frame it changes,
// for a few seconds after the last mark. Called once per present.
void Trace();
// While on, a highlight that continues from one frame to the next keeps its pulse instead of
// restarting it (the game restarts it every frame a right-click menu is open on the object).
void SetPulseHold(bool on);

}  // namespace rtx::scenehover
