#pragma once

// The editor's own settings file: where Daggerfall's data and Bunnyfall live, and the project file.
// $XDG_CONFIG_HOME/bunnyfall-dungeon-editor/config.json (~/.config/... when XDG_CONFIG_HOME is unset); on Windows
// %APPDATA%/bunnyfall-dungeon-editor/config.json.

#include "core/error.h"

#include <filesystem>
#include <optional>
#include <string>

namespace de::app {

struct Config {
    std::filesystem::path arena2;      ///< Daggerfall's ARENA2 folder.
    std::filesystem::path bunnyfall;   ///< The Bunnyfall checkout (empty: no export).
    std::filesystem::path project;     ///< The project file.

    /// A mod folder inside Bunnyfall (a project's mod is one of them).
    [[nodiscard]] std::filesystem::path modFolder(const std::string& folder) const { return bunnyfall / "vendor" / folder; }

    static std::filesystem::path file();
    /// The saved settings, or nothing if there are none yet.
    static Result<std::optional<Config>> load();
    Result<void> save() const;
};

/// What is wrong with a folder as ARENA2, or nothing when the editor can read it.
std::optional<std::string> arena2Problem(const std::filesystem::path& dir);
/// What is wrong with a folder as a Bunnyfall checkout, or nothing.
std::optional<std::string> bunnyfallProblem(const std::filesystem::path& dir);

} // namespace de::app
