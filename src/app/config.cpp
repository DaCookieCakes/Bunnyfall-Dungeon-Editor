#include "app/config.h"

#include "core/file.h"

#include <nlohmann/json.hpp>

#include <cstdlib>
#include <fstream>

namespace de::app {

std::filesystem::path Config::file() {
    if (const char* xdg = std::getenv("XDG_CONFIG_HOME"); xdg && *xdg) {
        return std::filesystem::path(xdg) / "bunnyfall-dungeon-editor" / "config.json";
    }
    const char* home = std::getenv("HOME");
    return std::filesystem::path(home ? home : ".") / ".config" / "bunnyfall-dungeon-editor" / "config.json";
}

Result<std::optional<Config>> Config::load() {
    std::ifstream in(file());
    if (!in) return std::optional<Config>{};
    const auto j = nlohmann::json::parse(in, nullptr, false);
    if (j.is_discarded() || !j.is_object()) return fail(file().string() + " is not JSON");
    Config c;
    c.arena2 = j.value("arena2", "");
    c.bunnyfall = j.value("bunnyfall", "");
    c.project = j.value("project", "");
    return std::optional(c);
}

Result<void> Config::save() const {
    const auto path = file();
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) return fail("cannot create " + path.parent_path().string() + ": " + ec.message());
    const nlohmann::json j = {{"arena2", arena2.string()}, {"bunnyfall", bunnyfall.string()}, {"project", project.string()}};
    std::ofstream out(path);
    out << j.dump(1) << "\n";
    if (!out) return fail("cannot write " + path.string());
    return {};
}

std::optional<std::string> arena2Problem(const std::filesystem::path& dir) {
    if (dir.empty()) return "Not set.";
    std::error_code ec;
    if (!std::filesystem::is_directory(dir, ec)) return "Not a folder.";
    for (const char* name : {"BLOCKS.BSA", "ARCH3D.BSA", "MAPS.BSA", "CLIMATE.PAK", "POLITIC.PAK", "ART_PAL.COL"}) {
        if (!findArena2File(dir, name)) return std::string("No ") + name + " in it.";
    }
    return std::nullopt;
}

std::optional<std::string> bunnyfallProblem(const std::filesystem::path& dir) {
    if (dir.empty()) return "Not set: exporting is off.";
    std::error_code ec;
    if (!std::filesystem::is_directory(dir, ec)) return "Not a folder.";
    if (!std::filesystem::is_directory(dir / "vendor", ec)) return "No vendor folder in it.";
    if (!std::filesystem::exists(dir / "src" / "formats" / "worldData" / "worldDataReplacement.js", ec)) {
        return "Does not look like Bunnyfall (no src/formats/worldData).";
    }
    return std::nullopt;
}

} // namespace de::app
