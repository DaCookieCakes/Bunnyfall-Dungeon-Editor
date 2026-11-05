#pragma once

// An editor project: the blocks and dungeons the designer makes, kept in the editor's own file. The mod folder is
// built from it (export/), never edited by hand. Every number that becomes part of the game's identity of something
// (block names, block save bases, map ids, location ids) is handed out here once, stored, and never reused, so a
// save made in an exported dungeon stays valid however the project changes afterwards (DESIGN.md §6.2, §6.5).

#include "core/error.h"
#include "formats/rdb.h"
#include "mesh/mesh.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace de::project {

/// A block the project owns.
struct CustomBlock {
    std::string name;                      ///< "<role letter>BF<5 digits>.RDB", e.g. "NBF00001.RDB".
    std::string label;                     ///< The designer's own name for it.
    std::optional<std::string> templateName;   ///< The classic block it started as; none when built from empty.
    std::uint32_t savePosition = 0;        ///< Its Position: the base of its objects' save identities.
    formats::RdbBlock data;
};

/// A model the project builds (the model editor's), placed in blocks like any ARCH3D model. Bunnyfall loads it from the
/// mod's Models folder; Daggerfall Unity has no such loader, so a block using one shows nothing there (DESIGN.md §7.3).
struct CustomModel {
    std::uint32_t id = 0;            ///< Issued once, from kFirstCustomModelId: blocks name the model by it.
    std::string label;               ///< The designer's own name for it.
    /// The three letters a block's model list gives it, which say what it is to the game and the catalog: "C00" a
    /// corridor or room piece, "DOR" an action door, anything else a prop (or architecture, when big enough).
    std::string description = "C00";
    mesh::Mesh mesh;
};

struct DungeonCell {
    int x = 0, z = 0;
    std::string blockName;   ///< A classic block or one of the project's.
    bool isStartingBlock = false;
    friend bool operator==(const DungeonCell&, const DungeonCell&) = default;
};

/// Daggerfall's location types a dungeon can be shown as on the map.
enum class DungeonLocationType : int { Labyrinth = 4, Keep = 7, Ruin = 10, Graveyard = 12 };

/// A dungeon the project adds to the world, as a new place.
struct ProjectDungeon {
    int number = 0;            ///< Allocated once: its map id and location ids come from it.
    int idRange = 0;           ///< Its project's id range (ModInfo): the location ids and file name take it in.
    std::string name;          ///< The place's name (unique in its region).
    int regionIndex = 17;      ///< Daggerfall
    int pixelX = 0, pixelY = 0;   ///< World map pixel (0..999, 0..499).
    int dungeonType = 2;       ///< 0..18 (Crypt .. Cemetery); picks the enemy and loot tables. 2: Human Stronghold.
    DungeonLocationType locationType = DungeonLocationType::Labyrinth;
    std::string entrance = "DUNGAA00.RMB";   ///< The classic surface block with the way in.
    bool discovered = true;    ///< Shown on the travel map from the start.
    std::vector<DungeonCell> cells;

    [[nodiscard]] std::uint32_t mapId() const;
    [[nodiscard]] int pixelId() const { return pixelY * 1000 + pixelX; }
    [[nodiscard]] std::uint16_t exteriorLocationId() const;
    [[nodiscard]] std::uint16_t dungeonLocationId() const;
    /// Its number among every editor mod's dungeons: the number in its project, past the ranges before its own.
    [[nodiscard]] int globalNumber() const;
    [[nodiscard]] const DungeonCell* cellAt(int x, int z) const;
    [[nodiscard]] const DungeonCell* startingCell() const;
};

inline constexpr int kFirstMapIdHigh = 1900;          // classic high parts go up to 1832
inline constexpr int kMaxDungeons = 2048 - kFirstMapIdHigh;   // map ids stay below 2^31
inline constexpr std::uint16_t kFirstLocationId = 60000;      // classic ids reach 51000; Bunnyfall's undercroft is 55398
/// Custom model ids: past every ARCH3D id (classic ones stay under 100,000) and every model Bunnyfall registers itself
/// (its own reach 864,111).
inline constexpr std::uint32_t kFirstCustomModelId = 7'000'000;
inline constexpr int kMaxModels = 999'999;
inline bool isCustomModelId(std::uint32_t id) { return id > kFirstCustomModelId && id <= kFirstCustomModelId + kMaxModels; }
inline constexpr std::uint32_t kFirstBlockSavePosition = 40'000'000;   // past BLOCKS.BSA's 32,884,997 bytes
/// Room for one block's own object offsets: classic ones stay under 65,536 and the editor's new objects start at
/// 900,000 (edit::kFirstNewPosition), so 2^20 holds either.
inline constexpr std::uint32_t kBlockSaveStride = 1u << 20;
/// Keeps every save identity (block base + object offset) within a signed 32-bit number.
inline constexpr int kMaxBlocks = 1999;

/// Several projects can each be a mod of their own, side by side in Bunnyfall's vendor/. Everything above is one space
/// for all of them, so each project numbers from an id range of its own: range r issues blocks r*249+1 .. r*249+249,
/// models 7,000,000 + r*124,999 + 1 .., and dungeons whose location ids start at 60000 + 2*148*r. Range 0 is the
/// numbering projects had before ranges, so nothing already exported moves.
inline constexpr int kIdRanges = 8;
inline constexpr int kBlocksPerRange = kMaxBlocks / kIdRanges;   // 249
inline constexpr int kModelsPerRange = kMaxModels / kIdRanges;   // 124,999
inline constexpr int kDungeonsPerRange = kMaxDungeons;           // 148: map ids take the pixel in, location ids do not

/// The mod a project exports as: a folder of Bunnyfall's vendor/ that the editor marks with dungeon-editor.mod.json,
/// which Bunnyfall reads for the mod's switch (its title is the project's name).
struct ModInfo {
    std::string folder = "bunnyfall-dungeons";
    std::string author = "Bunnyfall";
    std::string description;
    int idRange = 0;
};

/// What is wrong with a mod folder name (it is the mod's id in Bunnyfall: a-z, 0-9 and -), or nothing.
std::optional<std::string> modFolderProblem(std::string_view folder);

class Project {
public:
    std::string name = "Bunnyfall Dungeons";   ///< Also the mod's title in Bunnyfall.
    ModInfo mod;

    [[nodiscard]] const std::vector<CustomBlock>& blocks() const { return blocks_; }
    /// The blocks, to change their labels and contents in place (names and save bases stay as issued).
    [[nodiscard]] std::vector<CustomBlock>& blocks() { return blocks_; }
    [[nodiscard]] const std::vector<ProjectDungeon>& dungeons() const { return dungeons_; }
    [[nodiscard]] CustomBlock* block(std::string_view blockName);
    [[nodiscard]] const CustomBlock* block(std::string_view blockName) const;
    [[nodiscard]] ProjectDungeon* dungeon(int number);
    [[nodiscard]] const std::vector<CustomModel>& models() const { return models_; }
    [[nodiscard]] CustomModel* model(std::uint32_t id);
    [[nodiscard]] const CustomModel* model(std::uint32_t id) const;

    /// A new block of the role `letter` (N, W, S, M or B), started from `data` (a copy of `templateName`, or empty).
    /// Pointers into the project last until the next block or dungeon is added or removed.
    Result<CustomBlock*> addBlock(char letter, std::string label, std::optional<std::string> templateName,
                                  formats::RdbBlock data);
    /// A new dungeon with its own number. Its cells start empty.
    Result<ProjectDungeon*> addDungeon(std::string placeName);
    /// A new custom model with its own id.
    Result<CustomModel*> addModel(std::string label, std::string description, mesh::Mesh mesh);
    bool removeModel(std::uint32_t id);
    bool removeBlock(std::string_view blockName);
    bool removeDungeon(int number);

    Result<void> save(const std::filesystem::path& file) const;
    static Result<Project> load(const std::filesystem::path& file);

    /// The numbers handed out so far (stored so they are never handed out again).
    [[nodiscard]] int blocksIssued() const { return blocksIssued_; }
    [[nodiscard]] int dungeonsIssued() const { return dungeonsIssued_; }
    [[nodiscard]] int modelsIssued() const { return modelsIssued_; }
    /// Whether any number has been handed out: the id range is fixed from then on.
    [[nodiscard]] bool numbersIssued() const { return blocksIssued_ + dungeonsIssued_ + modelsIssued_ > 0; }
    /// Moves the project to another id range; refused once a number has been handed out from this one.
    Result<void> setIdRange(int range);

private:
    std::vector<CustomBlock> blocks_;
    std::vector<ProjectDungeon> dungeons_;
    std::vector<CustomModel> models_;
    int blocksIssued_ = 0;
    int dungeonsIssued_ = 0;
    int modelsIssued_ = 0;
};

/// An empty RDB block: no objects, 16 object roots, an empty model list.
formats::RdbBlock emptyBlock();

/// Whether a name is one of the editor's block names.
bool isProjectBlockName(std::string_view name);

} // namespace de::project
