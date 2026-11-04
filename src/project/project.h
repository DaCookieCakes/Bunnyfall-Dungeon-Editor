#pragma once

// An editor project: the blocks and dungeons the designer makes, kept in the editor's own file. The mod folder is
// built from it (export/), never edited by hand. Every number that becomes part of the game's identity of something
// (block names, block save bases, map ids, location ids) is handed out here once, stored, and never reused, so a
// save made in an exported dungeon stays valid however the project changes afterwards (DESIGN.md §6.2, §6.5).

#include "core/error.h"
#include "formats/rdb.h"

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
    [[nodiscard]] const DungeonCell* cellAt(int x, int z) const;
    [[nodiscard]] const DungeonCell* startingCell() const;
};

inline constexpr int kFirstMapIdHigh = 1900;          // classic high parts go up to 1832
inline constexpr int kMaxDungeons = 2048 - kFirstMapIdHigh;   // map ids stay below 2^31
inline constexpr std::uint16_t kFirstLocationId = 60000;      // classic ids reach 51000; Bunnyfall's undercroft is 55398
inline constexpr std::uint32_t kFirstBlockSavePosition = 40'000'000;   // past BLOCKS.BSA's 32,884,997 bytes
/// Room for one block's own object offsets: classic ones stay under 65,536 and the editor's new objects start at
/// 900,000 (edit::kFirstNewPosition), so 2^20 holds either.
inline constexpr std::uint32_t kBlockSaveStride = 1u << 20;
/// Keeps every save identity (block base + object offset) within a signed 32-bit number.
inline constexpr int kMaxBlocks = 1999;

class Project {
public:
    std::string name = "Bunnyfall Dungeons";

    [[nodiscard]] const std::vector<CustomBlock>& blocks() const { return blocks_; }
    /// The blocks, to change their labels and contents in place (names and save bases stay as issued).
    [[nodiscard]] std::vector<CustomBlock>& blocks() { return blocks_; }
    [[nodiscard]] const std::vector<ProjectDungeon>& dungeons() const { return dungeons_; }
    [[nodiscard]] CustomBlock* block(std::string_view blockName);
    [[nodiscard]] const CustomBlock* block(std::string_view blockName) const;
    [[nodiscard]] ProjectDungeon* dungeon(int number);

    /// A new block of the role `letter` (N, W, S, M or B), started from `data` (a copy of `templateName`, or empty).
    /// Pointers into the project last until the next block or dungeon is added or removed.
    Result<CustomBlock*> addBlock(char letter, std::string label, std::optional<std::string> templateName,
                                  formats::RdbBlock data);
    /// A new dungeon with its own number. Its cells start empty.
    Result<ProjectDungeon*> addDungeon(std::string placeName);
    bool removeBlock(std::string_view blockName);
    bool removeDungeon(int number);

    Result<void> save(const std::filesystem::path& file) const;
    static Result<Project> load(const std::filesystem::path& file);

    /// The numbers handed out so far (stored so they are never handed out again).
    [[nodiscard]] int blocksIssued() const { return blocksIssued_; }
    [[nodiscard]] int dungeonsIssued() const { return dungeonsIssued_; }

private:
    std::vector<CustomBlock> blocks_;
    std::vector<ProjectDungeon> dungeons_;
    int blocksIssued_ = 0;
    int dungeonsIssued_ = 0;
};

/// An empty RDB block: no objects, 16 object roots, an empty model list.
formats::RdbBlock emptyBlock();

/// Whether a name is one of the editor's block names.
bool isProjectBlockName(std::string_view name);

} // namespace de::project
