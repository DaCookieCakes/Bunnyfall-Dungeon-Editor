#pragma once

// A project into Bunnyfall's mod folder (DESIGN.md §6.5): its blocks as patches over their classic templates (or
// whole, when built from empty), its dungeons as new places, its custom models as model files (§7.3). Checked
// first; a project with errors writes nothing.

#include "catalog/catalog.h"
#include "export/dfu_json.h"
#include "formats/arena2.h"
#include "formats/maps.h"
#include "project/project.h"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace de::project {

/// What the export needs to know about the world, read once.
struct World {
    const formats::Arena2& arena2;
    const catalog::Catalog& catalog;
    std::vector<formats::LocationSummary> locations;
};

struct Issue {
    enum class Severity { Error, Warning } severity = Severity::Error;
    std::string where;   ///< The dungeon or block it is about.
    std::string text;
};

/// Everything wrong with one dungeon of the project.
std::vector<Issue> checkDungeon(const Project& project, const ProjectDungeon& dungeon, const World& world);
/// Everything wrong with the whole project.
std::vector<Issue> checkProject(const Project& project, const World& world);

/// The `locationnew-` file for a dungeon.
exporter::Json locationJson(const ProjectDungeon& dungeon, const World& world);
/// Its file name: `locationnew-bunnyfall-<number>-<slug>-<region>.json` (the `bunnyfall-` part is how Bunnyfall knows
/// it for an editor dungeon).
std::string locationFileName(const ProjectDungeon& dungeon);

/// Where a custom model goes in the mod folder: `Models/<id>.json` (Bunnyfall's loader reads every such file).
std::string modelFileName(std::uint32_t modelId);

/// The doorways a block opens on each edge: the catalog's for a classic block, measured for one of the project's.
std::optional<catalog::SocketMap> socketsOf(const Project& project, const World& world, const std::string& blockName);
/// A block's role, from its name's first letter.
formats::RdbRole roleOf(const std::string& blockName);

/// The file that makes a vendor folder a mod Bunnyfall loads by itself (src/systems/editorMods.js).
inline constexpr std::string_view kModManifestName = "dungeon-editor.mod.json";
inline constexpr std::string_view kModManifestFormat = "bunnyfall-dungeon-mod/1";

/// The mod's manifest: its title (the project's name), author, description and id range.
exporter::Json modManifestJson(const Project& project);

/// Writes the manifest if `modDir` has none, so a classic block saved before the first export is already a mod Bunnyfall
/// has a switch for (it throws on a patch in a vendor folder it has no switch for). Refuses a folder another mod owns.
Result<void> ensureModManifest(const Project& project, const std::filesystem::path& modDir);

/// A mod the editor made, found beside the project's own.
struct EditorMod {
    std::string folder;
    std::string title;
    int idRange = 0;
};
/// Every mod in `vendorDir` (Bunnyfall's vendor/) that holds a dungeon-editor.mod.json.
std::vector<EditorMod> editorModsIn(const std::filesystem::path& vendorDir);
/// The lowest id range no mod in `vendorDir` but `ownFolder` uses, or nothing when all are taken.
std::optional<int> freeIdRange(const std::filesystem::path& vendorDir, std::string_view ownFolder);

/// What is wrong with exporting into `modDir`: a folder another mod owns, an id range a mod beside it uses too, block
/// files two mods both ship, a Bunnyfall too old to load a mod of this name.
std::vector<Issue> checkModFolder(const Project& project, const std::filesystem::path& modDir);

struct ExportReport {
    std::vector<Issue> issues;
    std::vector<std::string> written;   ///< Paths relative to the mod folder.
    std::vector<std::string> removed;   ///< Files an earlier export wrote that this one no longer does.
    [[nodiscard]] bool hasErrors() const;
};

/// Checks the project and the folder and, when nothing is an error, writes it into `modDir` (Bunnyfall's
/// vendor/<mod>/) with the mod's manifest, and a README when the folder has none of its own. Files an earlier export
/// wrote and this one does not are removed; files it did not write are never touched.
Result<ExportReport> exportProject(const Project& project, const World& world, const std::filesystem::path& modDir);

} // namespace de::project
