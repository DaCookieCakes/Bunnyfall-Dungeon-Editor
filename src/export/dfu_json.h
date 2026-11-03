#pragma once

// Daggerfall Unity's World Data JSON, as Bunnyfall reads and writes it (Bunnyfall/src/formats/worldData/
// worldDataJson.js). Field names and shapes follow DFU's FullSerializer output member for member: public fields
// only, enums by name, the RDB model list cut at the first unused slot, each RDB object's resources pruned to the one
// its type uses.

#include "core/error.h"
#include "formats/rdb.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <string>
#include <string_view>

namespace de::exporter {

/// Objects keep their keys sorted (nlohmann's default), so a compact dump is already canonical.
using Json = nlohmann::json;

/// An RDB block as a `<NAME>.RDB.json` world-data file. `index` is the block's BLOCKS.BSA record number (or the
/// number a new block is given) and `position` its byte offset there; together with each object's own offset they
/// make the save identity Bunnyfall gives the block's doors and enemies.
Json rdbBlockToJson(const formats::RdbBlock& block, std::string_view name, std::size_t index, std::uint32_t position);

/// The reverse of rdbBlockToJson: a `<NAME>.RDB.json` world-data file back into a block (to keep editing a saved
/// patch). The model list is padded back to 750 slots with unused ones. Refuses what rdbBlockToJson could not write
/// back unchanged (a flat whose FactionOrMobileId disagrees with its Magnitude and SoundIndex, IsCustomData), so a
/// load and a save never quietly lose anything.
Result<formats::RdbBlock> rdbBlockFromJson(const Json& json);

/// The form a patch's sha256 is taken over: every object's keys sorted, no whitespace, values as JavaScript's
/// JSON.stringify writes them.
std::string canonicalJson(const Json& json);

/// sha256 (hex) of canonicalJson.
std::string canonicalSha256(const Json& json);

} // namespace de::exporter
