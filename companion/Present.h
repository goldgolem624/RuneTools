#pragma once
// Present callback: composites launcher UI into the game frame before wglSwapBuffers.

namespace rtx::present {

bool Install();

void Uninstall();

}  // namespace rtx::present
