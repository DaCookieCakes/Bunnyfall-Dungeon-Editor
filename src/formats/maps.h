#pragma once

#include "core/error.h"
#include "formats/bsa.h"

#include <cstdint>
#include <string>
#include <vector>

namespace de::formats {

/// One block on a dungeon's grid. Blocks sit at (x * 2048, 0, z * 2048) block units and cannot be rotated.
struct DungeonBlockRef {
    std::int8_t x = 0, z = 0;
    std::uint16_t bitfield = 0;   ///< As stored: number (10 bits), starting flag (bit 10), letter index (bits 11+).
    std::string blockName;        ///< e.g. "N0000071.RDB"
    bool isStartingBlock = false;
};

/// A location's row in its region's map table (MAPTABLE).
struct MapTableEntry {
    std::int32_t mapId = 0;
    std::int32_t longitude = 0, latitude = 0;
    int locationType = 0;
    int dungeonType = 255;   ///< 0..18 (Crypt .. Cemetery), 255 none. Picks the enemy and loot tables.
    bool discovered = false;
};

struct DungeonRecord {
    int regionIndex = 0;
    int locationIndex = -1;                 ///< The location this dungeon is under, within its region.
    std::string name;                       ///< The dungeon record's own location name.
    std::uint16_t locationId = 0;           ///< Seeds the dungeon's texture table and random enemies.
    std::uint32_t exteriorLocationId = 0;   ///< The exterior location this dungeon belongs to.
    MapTableEntry map;                      ///< The location's map table row.
    std::vector<DungeonBlockRef> blocks;
};

/// Every dungeon in MAPS.BSA, region by region, in file order, linked to its location's map table row. Applies the
/// one layout fix-up classic Daggerfall makes (Orsinium's overlapping border block).
Result<std::vector<DungeonRecord>> readDungeons(const BsaArchive& maps);

/// One classic location: enough to keep a new place from clashing with it.
struct LocationSummary {
    int regionIndex = 0;
    int locationIndex = 0;
    std::string name;
    MapTableEntry map;
    [[nodiscard]] int pixelX() const { return map.longitude / 128; }
    [[nodiscard]] int pixelY() const { return 499 - map.latitude / 128; }
};

/// Every classic location in MAPS.BSA, region by region.
Result<std::vector<LocationSummary>> readLocations(const BsaArchive& maps);

/// Daggerfall's 62 region names, by region index.
std::string_view regionName(int regionIndex);
inline constexpr int kRegionCount = 62;

/// The name a dungeon block bitfield encodes, or an error for a letter index outside N W L S B M.
Result<std::string> dungeonBlockName(std::uint16_t bitfield);

} // namespace de::formats
