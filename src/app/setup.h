#pragma once

#include "app/config.h"

#include <SDL3/SDL.h>

#include <filesystem>
#include <optional>
#include <utility>

namespace de::app {

/// The setup window: asks where ARENA2, Bunnyfall and the project file are, checking each as it is typed or browsed
/// to. Runs its own frames until the designer continues (true, `config` filled in and saved) or quits (false).
/// Needs SDL, the GL context and ImGui running.
bool runSetup(SDL_Window* window, Config& config);

/// The same fields as a window inside the running editor (File > Paths). Answers true when the paths were saved; they
/// take effect at the next start.
bool drawPathsWindow(SDL_Window* window, Config& config, bool* open);

/// What a project file dialog was opened for.
enum class ProjectDialog { New, Open, SaveAs };
/// Opens SDL's file dialog for a project file (a save dialog for New and SaveAs). It answers on its own time:
/// takeProjectFile hands the choice over, once, on a later frame. A cancelled dialog hands nothing.
void askForProjectFile(SDL_Window* window, ProjectDialog purpose, const std::filesystem::path& start);
std::optional<std::pair<ProjectDialog, std::filesystem::path>> takeProjectFile();

} // namespace de::app
