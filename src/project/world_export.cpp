#include "project/world_export.h"

#include "export/world_data_patch.h"
#include "formats/climate.h"
#include "formats/texture.h"
#include "mesh/mesh_json.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <format>
#include <fstream>
#include <map>
#include <set>

namespace de::project {

namespace {

using exporter::Json;
using formats::RdbRole;

constexpr std::string_view kManifest = ".dungeon-editor-export.json";
constexpr std::int32_t kPixelWorldUnits = 32768;
constexpr std::int32_t kPixelMapUnits = 128;

constexpr std::array<std::string_view, 19> kDungeonTypeNames = {
    "Crypt",       "OrcStronghold",  "HumanStronghold", "Prison",      "DesecratedTemple", "Mine",
    "NaturalCave", "Coven",          "VampireHaunt",    "Laboratory",  "HarpyNest",        "RuinedCastle",
    "SpiderNest",  "GiantStronghold", "DragonsDen",     "BarbarianStronghold", "VolcanicCaves", "ScorpionNest",
    "Cemetery"};

std::string_view locationTypeName(DungeonLocationType t) {
    switch (t) {
        case DungeonLocationType::Keep: return "DungeonKeep";
        case DungeonLocationType::Ruin: return "DungeonRuin";
        case DungeonLocationType::Graveyard: return "Graveyard";
        case DungeonLocationType::Labyrinth: break;
    }
    return "DungeonLabyrinth";
}

/// GetWorldClimateSettings (Bunnyfall mapsFile.js) as DFU's JSON names it. Bunnyfall reads WorldClimate alone.
Json climateJson(int worldClimate) {
    struct Settings { std::string_view type, nature; int ground, natureArchive, sky; std::string_view people; };
    Settings s{"Temperate", "Nature_TemperateWoodland", 302, 504, 16, "Breton"};
    switch (worldClimate) {
        case formats::kClimateOcean: s = {"Swamp", "Nature_TemperateWoodland", 402, 504, 24, "Breton"}; break;
        case formats::kClimateDesert:
        case formats::kClimateDesert2: s = {"Desert", "Nature_Desert", 2, 503, 8, "Redguard"}; break;
        case formats::kClimateMountain: s = {"Mountain", "Nature_Mountains", 102, 510, 0, "Nord"}; break;
        case formats::kClimateRainforest: s = {"Swamp", "Nature_RainForest", 402, 500, 24, "Redguard"}; break;
        case formats::kClimateSwamp: s = {"Swamp", "Nature_Swamp", 402, 502, 24, "Breton"}; break;
        case formats::kClimateSubtropical: s = {"Desert", "Nature_SubTropical", 2, 501, 24, "Breton"}; break;
        case formats::kClimateMountainWoods: s = {"Temperate", "Nature_WoodlandHills", 102, 506, 16, "Breton"}; break;
        case formats::kClimateHauntedWoodlands: s = {"Temperate", "Nature_HauntedWoodlands", 302, 508, 16, "Breton"}; break;
        default: break;
    }
    return {{"WorldClimate", worldClimate}, {"ClimateType", s.type},   {"NatureSet", s.nature},
            {"GroundArchive", s.ground},    {"NatureArchive", s.natureArchive}, {"SkyBase", s.sky},
            {"People", s.people},           {"Names", "Nord"}};
}

std::string slugOf(const std::string& name) {
    std::string slug;
    for (const char c : name) {
        const auto u = static_cast<unsigned char>(c);
        if (std::isalnum(u)) slug.push_back(static_cast<char>(std::tolower(u)));
        else if (!slug.empty() && slug.back() != '-') slug.push_back('-');
    }
    while (!slug.empty() && slug.back() == '-') slug.pop_back();
    return slug.empty() ? "dungeon" : slug;
}

/// Whether a block (the project's, or classic as the game reads it) holds a start marker (199/10).
bool hasStartMarker(const Project& project, const World& world, const std::string& name) {
    formats::RdbBlock data;
    if (const auto* own = project.block(name)) {
        data = own->data;
    } else if (const auto index = world.arena2.blocks.findByName(name)) {
        auto classic = formats::readClassicRdbBlock(world.arena2.blocks, *index);
        if (!classic) return false;
        data = std::move(*classic);
    }
    for (const auto& root : data.roots)
        for (const auto& obj : root.objects)
            if (const auto* f = obj.flat(); f && f->textureArchive() == 199 && f->textureRecord() == 10) return true;
    return false;
}

bool blockExists(const Project& project, const World& world, const std::string& name) {
    if (project.block(name)) return true;
    return name.ends_with(".RDB") && world.arena2.blocks.findByName(name).has_value();
}

Result<void> writeJsonFile(const std::filesystem::path& path, const Json& json) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) return fail("cannot create " + path.parent_path().string() + ": " + ec.message());
    const auto temp = path.string() + ".tmp";
    {
        std::ofstream out(temp);
        out << json.dump(1) << "\n";
        if (!out) return fail("cannot write " + temp);
    }
    std::filesystem::rename(temp, path, ec);
    if (ec) return fail("cannot replace " + path.string() + ": " + ec.message());
    return {};
}

} // namespace

formats::RdbRole roleOf(const std::string& blockName) { return formats::rdbRoleFromName(blockName); }

std::optional<catalog::SocketMap> socketsOf(const Project& project, const World& world, const std::string& blockName) {
    if (const auto* b = project.block(blockName)) {
        return catalog::standardSocketsOf(catalog::detectPorts(b->data, world.catalog.models()));
    }
    if (const auto* info = world.catalog.block(blockName)) return info->sockets;
    return std::nullopt;
}

bool ExportReport::hasErrors() const {
    return std::ranges::any_of(issues, [](const Issue& i) { return i.severity == Issue::Severity::Error; });
}

std::vector<Issue> checkDungeon(const Project& project, const ProjectDungeon& d, const World& world) {
    std::vector<Issue> issues;
    const auto where = d.name.empty() ? std::format("dungeon {}", d.number) : d.name;
    auto error = [&](std::string text) { issues.push_back({Issue::Severity::Error, where, std::move(text)}); };
    auto warning = [&](std::string text) { issues.push_back({Issue::Severity::Warning, where, std::move(text)}); };

    // The place.
    if (d.name.empty()) error("It has no name.");
    if (d.regionIndex < 0 || d.regionIndex >= formats::kRegionCount) error("Its region is not one of Daggerfall's.");
    if (d.pixelX < 0 || d.pixelX > 999 || d.pixelY < 0 || d.pixelY > 499) {
        error("Its map position is off the map.");
    } else {
        const int region = formats::regionAtPixel(world.arena2.politic, d.pixelX, d.pixelY);
        if (region < 0 || formats::terrainClimateAt(world.arena2.climate, d.pixelX, d.pixelY) == formats::kClimateOcean) {
            error(std::format("Map pixel ({}, {}) is water.", d.pixelX, d.pixelY));
        }
        else if (region != d.regionIndex) {
            error(std::format("Map pixel ({}, {}) is in {}, not {}.", d.pixelX, d.pixelY, formats::regionName(region),
                              formats::regionName(d.regionIndex)));
        }
        for (const auto& l : world.locations) {
            if (l.pixelX() == d.pixelX && l.pixelY() == d.pixelY) {
                error(std::format("Map pixel ({}, {}) already holds {}.", d.pixelX, d.pixelY, l.name));
            }
        }
    }
    for (const auto& l : world.locations) {
        if (l.regionIndex == d.regionIndex && l.name == d.name && !d.name.empty()) {
            error("Its region already has a place called " + d.name + ".");
        }
    }
    for (const auto& other : project.dungeons()) {
        if (other.number == d.number) continue;
        if (other.regionIndex == d.regionIndex && other.name == d.name && !d.name.empty()) {
            error("Another of the project's dungeons in its region is called " + d.name + ".");
        }
        if (other.pixelX == d.pixelX && other.pixelY == d.pixelY) {
            error(std::format("{} stands on the same map pixel.", other.name));
        }
    }
    if (d.dungeonType < 0 || d.dungeonType >= static_cast<int>(kDungeonTypeNames.size())) error("Its dungeon type is unknown.");
    if (!d.entrance.ends_with(".RMB") || !world.arena2.blocks.findByName(d.entrance)) {
        error("Its entrance " + d.entrance + " is not a surface block of Daggerfall's.");
    }

    // The grid.
    if (d.cells.empty()) {
        error("It has no blocks.");
        return issues;
    }
    std::set<std::pair<int, int>> seen;
    int starts = 0;
    for (const auto& c : d.cells) {
        if (!seen.insert({c.x, c.z}).second) error(std::format("Two blocks stand on ({}, {}).", c.x, c.z));
        if (!blockExists(project, world, c.blockName)) error(std::format("({}, {}) names {}, which does not exist.", c.x, c.z, c.blockName));
        if (c.x < -64 || c.x > 63 || c.z < -64 || c.z > 63) error(std::format("({}, {}) is too far from the start.", c.x, c.z));
        if (c.isStartingBlock) ++starts;
    }
    if (starts != 1) error(starts == 0 ? "No block is the starting block." : "More than one block is the starting block.");
    if (const auto* start = d.startingCell(); start && starts == 1) {
        if (roleOf(start->blockName) == RdbRole::Border) error("The starting block is a border cap.");
        else if (blockExists(project, world, start->blockName) && !hasStartMarker(project, world, start->blockName)) {
            error("The starting block has no start marker: players walking in through the entrance arrive there, and "
                  "Bunnyfall will not let them in without one.");
        }
        if (start->x != 0 || start->z != 0) warning("The starting block is not at (0, 0), where every classic dungeon has it.");
    }

    // Openings: every block but a cap is closed on all four sides, and facing doorways agree.
    static constexpr std::array<std::tuple<int, int, catalog::Edge, catalog::Edge>, 2> kNeighbours = {{
        {1, 0, catalog::Edge::East, catalog::Edge::West},
        {0, 1, catalog::Edge::North, catalog::Edge::South},
    }};
    for (const auto& c : d.cells) {
        if (roleOf(c.blockName) != RdbRole::Border) {
            for (auto [dx, dz] : {std::pair{1, 0}, std::pair{-1, 0}, std::pair{0, 1}, std::pair{0, -1}}) {
                if (!d.cellAt(c.x + dx, c.z + dz)) {
                    warning(std::format("{} at ({}, {}) is open to the {} with no block there.", c.blockName, c.x, c.z,
                                        dx > 0 ? "east" : dx < 0 ? "west" : dz > 0 ? "north" : "south"));
                }
            }
        }
        const auto mine = socketsOf(project, world, c.blockName);
        for (const auto& [dx, dz, myEdge, theirEdge] : kNeighbours) {
            const auto* other = d.cellAt(c.x + dx, c.z + dz);
            if (!other || !mine) continue;
            const auto theirs = socketsOf(project, world, other->blockName);
            if (!theirs) continue;
            for (std::size_t s = 0; s < 2; ++s) {
                const bool a = (*mine)[static_cast<std::size_t>(myEdge)][s];
                const bool b = (*theirs)[static_cast<std::size_t>(theirEdge)][s];
                if (a != b) {
                    warning(std::format("The {} doorway between ({}, {}) and ({}, {}) opens on one side only.",
                                        s == 0 ? "upper" : "lower", c.x, c.z, other->x, other->z));
                }
            }
        }
    }
    return issues;
}

std::vector<Issue> checkProject(const Project& project, const World& world) {
    std::vector<Issue> issues;
    for (const auto& d : project.dungeons()) {
        auto mine = checkDungeon(project, d, world);
        issues.insert(issues.end(), mine.begin(), mine.end());
    }
    for (const auto& b : project.blocks()) {
        if (b.templateName && !world.arena2.blocks.findByName(*b.templateName)) {
            issues.push_back({Issue::Severity::Error, b.name, "Its template " + *b.templateName + " is not in BLOCKS.BSA."});
        }
        std::set<std::uint32_t> missing;
        for (const auto& root : b.data.roots) {
            for (const auto& obj : root.objects) {
                const auto* m = obj.model();
                if (!m || m->modelIndex >= b.data.modelRefs.size()) continue;
                const auto id = b.data.modelRefs[m->modelIndex].modelIdNum;
                if (isCustomModelId(id) && !project.model(id)) missing.insert(id);
            }
        }
        for (const auto id : missing) {
            issues.push_back({Issue::Severity::Error, b.name, std::format("It places model {}, which the project no longer has.", id)});
        }
    }
    for (const auto& m : project.models()) {
        const auto where = std::format("model {} ({})", m.id, m.label.empty() ? "no name" : m.label);
        if (m.mesh.faces.empty()) issues.push_back({Issue::Severity::Error, where, "It has no faces."});
        for (auto& problem : mesh::checkMesh(m.mesh)) {
            problem[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(problem[0])));
            issues.push_back({Issue::Severity::Warning, where, problem + "."});
        }
    }
    return issues;
}

std::string modelFileName(std::uint32_t modelId) { return std::format("Models/{}.json", modelId); }

std::string locationFileName(const ProjectDungeon& d) {
    return std::format("locationnew-bunnyfall-{}-{}-{}.json", d.globalNumber(), slugOf(d.name), d.regionIndex);
}

Json locationJson(const ProjectDungeon& d, const World& world) {
    const std::int32_t worldX = d.pixelX * kPixelWorldUnits + kPixelWorldUnits / 2;
    const std::int32_t worldY = (499 - d.pixelY) * kPixelWorldUnits + kPixelWorldUnits / 2;
    const int climate = formats::terrainClimateAt(world.arena2.climate, d.pixelX, d.pixelY);
    auto header = [&](bool exterior) -> Json {
        return {{"X", worldX},
                {"Y", worldY},
                {"IsExterior", exterior ? 32768 : 0},
                {"Unknown2", 0},
                {"LocationId", exterior ? d.exteriorLocationId() : d.dungeonLocationId()},
                {"IsInterior", exterior ? 0 : 1},
                {"ExteriorLocationId", exterior ? 0 : d.exteriorLocationId()},
                {"LocationName", d.name}};
    };
    Json blocks = Json::array();
    for (const auto& c : d.cells) {
        blocks.push_back({{"X", c.x}, {"Z", c.z}, {"IsStartingBlock", c.isStartingBlock}, {"BlockName", c.blockName},
                          {"WaterLevel", 0}, {"CastleBlock", false}});
    }
    return {
        {"Loaded", true},
        {"Name", d.name},
        {"RegionName", formats::regionName(d.regionIndex)},
        {"HasDungeon", true},
        {"MapTableData",
         {{"MapId", d.mapId()},
          {"Latitude", (499 - d.pixelY) * kPixelMapUnits + kPixelMapUnits / 2},
          {"Longitude", d.pixelX * kPixelMapUnits + kPixelMapUnits / 2},
          {"LocationType", locationTypeName(d.locationType)},
          {"DungeonType", kDungeonTypeNames[static_cast<std::size_t>(std::clamp(d.dungeonType, 0, 18))]},
          {"Discovered", d.discovered},
          {"Key", 0}}},
        {"Exterior",
         {{"RecordElement", {{"Header", header(true)}}},
          {"BuildingCount", 0},
          {"Buildings", Json::array()},
          {"ExteriorData",
           {{"AnotherName", d.name},
            {"MapId", d.pixelId()},
            {"LocationId", d.exteriorLocationId()},
            {"Width", 1},
            {"Height", 1},
            {"PortTownAndUnknown", 0},
            {"BlockNames", Json::array({d.entrance})}}}}},
        {"Dungeon",
         {{"RecordElement", {{"Header", header(false)}}},
          {"Header", {{"BlockCount", d.cells.size()}}},
          {"Blocks", std::move(blocks)}}},
        {"Climate", climateJson(climate)},
        {"Politic", d.regionIndex + 128},
        {"RegionIndex", d.regionIndex},
        {"LocationIndex", 0},
    };
}

Json modManifestJson(const Project& project) {
    return {{"format", kModManifestFormat},
            {"title", project.name},
            {"author", project.mod.author},
            {"description", project.mod.description},
            {"idRange", project.mod.idRange},
            {"written by", "Bunnyfall Dungeon Editor"}};
}

namespace {

std::optional<Json> readJson(const std::filesystem::path& file) {
    std::ifstream in(file);
    if (!in) return std::nullopt;
    auto j = Json::parse(in, nullptr, false);
    if (j.is_discarded() || !j.is_object()) return std::nullopt;
    return j;
}

/// The names of the block and place files a mod folder ships.
std::set<std::string> worldDataNames(const std::filesystem::path& modDir) {
    std::set<std::string> names;
    for (const auto* sub : {"WorldData", "WorldDataPatches"}) {
        std::error_code ec;
        for (const auto& e : std::filesystem::directory_iterator(modDir / sub, ec)) {
            if (e.path().extension() == ".json") names.insert(e.path().filename().string());
        }
    }
    return names;
}

/// Whether `modDir` sits in a Bunnyfall vendor/ folder (and not, say, a check's scratch folder).
bool inVendor(const std::filesystem::path& modDir) { return modDir.parent_path().filename() == "vendor"; }

std::string readmeText(const Project& project) {
    std::string text = std::format("# {}\n\n", project.name);
    if (!project.mod.description.empty()) text += project.mod.description + "\n\n";
    text += std::format(
        "By {}. Made with the Bunnyfall Dungeon Editor, which writes everything in this folder: edit the project, not "
        "these files.\n\n"
        "- `{}` - the mod's title, author and id range. Bunnyfall gives every folder with one a switch of its own "
        "(Settings > Gameplay > Dungeons; `src/systems/editorMods.js`).\n"
        "- `WorldDataPatches/*.json` - dungeon blocks as changes over classic ones, rebuilt from the player's own "
        "BLOCKS.BSA. No Daggerfall data is shipped.\n"
        "- `WorldData/*.json` - blocks built from nothing, and the mod's own places (`locationnew-bunnyfall-*`).\n"
        "- `Models/<id>.json` - models from the editor's model editor (Bunnyfall only; Daggerfall Unity cannot load "
        "them).\n\n"
        "Offline the mod also needs Replace Game Artwork (Asset Injection), like every world-data mod; online the room "
        "owns its switch.\n",
        project.mod.author.empty() ? "Bunnyfall" : project.mod.author, kModManifestName);
    return text;
}

} // namespace

std::vector<EditorMod> editorModsIn(const std::filesystem::path& vendorDir) {
    std::vector<EditorMod> mods;
    std::error_code ec;
    for (const auto& e : std::filesystem::directory_iterator(vendorDir, ec)) {
        if (!e.is_directory()) continue;
        const auto j = readJson(e.path() / kModManifestName);
        if (!j || j->value("format", "") != kModManifestFormat) continue;
        mods.push_back({e.path().filename().string(), j->value("title", ""), j->value("idRange", 0)});
    }
    std::ranges::sort(mods, {}, &EditorMod::folder);
    return mods;
}

std::optional<int> freeIdRange(const std::filesystem::path& vendorDir, std::string_view ownFolder) {
    std::set<int> taken;
    for (const auto& m : editorModsIn(vendorDir)) {
        if (m.folder != ownFolder) taken.insert(m.idRange);
    }
    for (int r = 0; r < kIdRanges; ++r) {
        if (!taken.contains(r)) return r;
    }
    return std::nullopt;
}

std::vector<Issue> checkModFolder(const Project& project, const std::filesystem::path& modDir) {
    std::vector<Issue> issues;
    const std::string where = "Mod";
    const auto folder = modDir.filename().string();
    if (inVendor(modDir)) {
        if (const auto problem = modFolderProblem(folder)) issues.push_back({Issue::Severity::Error, where, *problem + "."});
    }
    // Never write into a mod the editor did not make.
    std::error_code ec;
    const bool ours = std::filesystem::exists(modDir / kManifest) || std::filesystem::exists(modDir / kModManifestName);
    if (!ours && inVendor(modDir) && std::filesystem::exists(modDir, ec) && !std::filesystem::is_empty(modDir, ec) && folder != "bunnyfall-dungeons") {
        issues.push_back({Issue::Severity::Error, where,
                          std::format("{} holds files the editor did not write (another mod?): give the mod another folder.",
                                      modDir.string())});
    }
    if (!inVendor(modDir)) return issues;

    const auto vendor = modDir.parent_path();
    std::set<std::string> mine;
    for (const auto& b : project.blocks()) mine.insert(b.name + ".json");
    for (const auto& name : worldDataNames(modDir)) {
        if (name.ends_with(".RDB.json")) mine.insert(name);   // classic blocks patched in the block editor
    }
    for (const auto& other : editorModsIn(vendor)) {
        if (other.folder == folder) continue;
        if (other.idRange == project.mod.idRange) {
            issues.push_back({Issue::Severity::Error, where,
                              std::format("The mod {} uses id range {} too: their blocks and places would be one another's. "
                                          "Pick another range in the Mod tab (only while this project has numbered nothing).",
                                          other.folder, other.idRange)});
        }
        for (const auto& name : worldDataNames(vendor / other.folder)) {
            if (mine.contains(name)) {
                issues.push_back({Issue::Severity::Warning, where,
                                  std::format("{} is in the mod {} as well: Bunnyfall loads only one of them.", name, other.folder)});
            }
        }
    }
    const auto bunnyfall = vendor.parent_path();
    if (folder != "bunnyfall-dungeons" && std::filesystem::exists(bunnyfall / "src") &&
        !std::filesystem::exists(bunnyfall / "src" / "systems" / "editorMods.js")) {
        issues.push_back({Issue::Severity::Warning, where,
                          "This Bunnyfall only loads the editor's bunnyfall-dungeons folder: a mod of another name needs "
                          "src/systems/editorMods.js (BUNNYFALL_CHANGES.md, change 4)."});
    }
    return issues;
}

Result<ExportReport> exportProject(const Project& project, const World& world, const std::filesystem::path& modDir) {
    ExportReport report;
    report.issues = checkProject(project, world);
    for (auto& issue : checkModFolder(project, modDir)) report.issues.push_back(std::move(issue));
    if (report.hasErrors()) return report;

    // Everything this export writes, by path relative to the mod folder.
    std::map<std::string, Json> files;
    for (const auto& b : project.blocks()) {
        const auto json = exporter::rdbBlockToJson(b.data, b.name, 0, b.savePosition);
        if (b.templateName) {
            const auto base = world.arena2.blocks.findByName(*b.templateName);
            auto patch = exporter::makeBlockPatch(world.arena2.blocks, *base, json, b.name + ".json");
            if (!patch) return fail(b.name + ": " + patch.error().message);
            files["WorldDataPatches/" + b.name + ".json"] = std::move(*patch);
        } else {
            files["WorldData/" + b.name + ".json"] = json;
        }
    }
    for (const auto& d : project.dungeons()) files["WorldData/" + locationFileName(d)] = locationJson(d, world);
    // Custom models, with their texel UVs divided by each base texture's size as Bunnyfall's own models are.
    std::map<int, std::optional<formats::TextureArchive>> archives;
    const mesh::TextureSize sizeOf = [&](int archive, int record) -> std::pair<int, int> {
        auto it = archives.find(archive);
        if (it == archives.end()) {
            auto loaded = formats::TextureArchive::load(world.arena2.dir, archive);
            it = archives.emplace(archive, loaded ? std::optional(std::move(*loaded)) : std::nullopt).first;
        }
        if (!it->second || record < 0 || static_cast<std::size_t>(record) >= it->second->recordCount()) return {64, 64};
        const auto& r = it->second->record(static_cast<std::size_t>(record));
        return {r.width, r.height};
    };
    for (const auto& m : project.models()) files[modelFileName(m.id)] = mesh::bunnyfallModelJson(m.mesh, m.id, m.label, sizeOf);
    files[std::string(kModManifestName)] = modManifestJson(project);

    for (const auto& [rel, json] : files) {
        if (auto r = writeJsonFile(modDir / rel, json); !r) return std::unexpected(r.error());
        report.written.push_back(rel);
    }

    Json old;
    if (std::ifstream in(modDir / kManifest); in) old = Json::parse(in, nullptr, false);

    // A README, unless the folder has one the editor did not write.
    bool readme = !std::filesystem::exists(modDir / "README.md");
    if (old.is_object()) {
        for (const auto& rel : old.value("files", Json::array())) readme = readme || rel == "README.md";
    }
    if (readme) {
        std::ofstream out(modDir / "README.md");
        out << readmeText(project);
        if (!out) return fail("cannot write " + (modDir / "README.md").string());
        report.written.push_back("README.md");
    }

    // Files the last export wrote that this one does not: a renamed, removed or re-based block or dungeon.
    {
        if (old.is_object()) {
            for (const auto& rel : old.value("files", Json::array())) {
                if (!rel.is_string() || files.contains(rel.get<std::string>())) continue;
                const auto path = modDir / rel.get<std::string>();
                // Only plain names inside the folders the export writes: a damaged manifest can do no harm.
                const auto relPath = std::filesystem::path(rel.get<std::string>());
                const auto top = relPath.begin() != relPath.end() ? relPath.begin()->string() : "";
                if ((top != "WorldData" && top != "WorldDataPatches" && top != "Models") || relPath.lexically_normal() != relPath) {
                    continue;
                }
                std::error_code ec;
                if (std::filesystem::remove(path, ec)) report.removed.push_back(rel.get<std::string>());
            }
        }
    }
    Json manifest = {{"written by", "Bunnyfall Dungeon Editor"}, {"project", project.name}, {"files", Json::array()}};
    for (const auto& [rel, _] : files) manifest["files"].push_back(rel);
    if (readme) manifest["files"].push_back("README.md");
    if (auto r = writeJsonFile(modDir / kManifest, manifest); !r) return std::unexpected(r.error());
    return report;
}

} // namespace de::project
