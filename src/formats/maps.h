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

struct DungeonRecord {
    int regionIndex = 0;
    std::string name;                       ///< The dungeon record's own location name.
    std::uint16_t locationId = 0;
    std::uint32_t exteriorLocationId = 0;   ///< The exterior location this dungeon belongs to.
    std::vector<DungeonBlockRef> blocks;
};

/// Every dungeon in MAPS.BSA, region by region, in file order. Applies the one layout fix-up classic Daggerfall
/// makes (Orsinium's overlapping border block).
Result<std::vector<DungeonRecord>> readDungeons(const BsaArchive& maps);

/// The name a dungeon block bitfield encodes, or an error for a letter index outside N W L S B M.
Result<std::string> dungeonBlockName(std::uint16_t bitfield);

} // namespace de::formats
