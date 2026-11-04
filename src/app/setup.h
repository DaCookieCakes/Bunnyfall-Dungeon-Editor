#pragma once

#include "app/config.h"

#include <SDL3/SDL.h>

namespace de::app {

/// The setup window: asks where ARENA2, Bunnyfall and the project file are, checking each as it is typed or browsed
/// to. Runs its own frames until the designer continues (true, `config` filled in and saved) or quits (false).
/// Needs SDL, the GL context and ImGui running.
bool runSetup(SDL_Window* window, Config& config);

/// The same fields as a window inside the running editor (File > Paths). Answers true when the paths were saved; they
/// take effect at the next start.
bool drawPathsWindow(SDL_Window* window, Config& config, bool* open);

} // namespace de::app
