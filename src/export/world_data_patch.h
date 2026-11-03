#pragma once

// World-data patches (Bunnyfall's `dfe-worlddata-patch/1`, src/formats/worldData/worldDataPatch.js): a block shipped
// as edits over the player's own classic block, so no Daggerfall data is distributed. Bunnyfall rebuilds the block
// at load and checks it against the patch's sha256.

#include "core/error.h"
#include "export/dfu_json.h"
#include "formats/bsa.h"

#include <functional>

namespace de::exporter {

inline constexpr std::string_view kPatchFormat = "dfe-worlddata-patch/1";

/// Where a copy op's node comes from: `{ block: <BLOCKS.BSA index>, path: [...] }`, answered with the node.
using CopySource = std::function<Result<Json>(const Json& ref)>;

/// Applies patch ops in order to `root` (taken by value: the caller's document is left alone). Ops: `s` set,
/// `d` delete a key, `i` insert into an array, `r` remove from an array, and `ci`/`cs` (insert/set a copy of a classic
/// node). A path that does not land refuses the whole patch, as Bunnyfall does.
Result<Json> applyPatchOps(Json root, const Json& ops, const CopySource& copySource = {});

/// Rebuilds the file a block patch describes from the player's BLOCKS.BSA: the classic block (repaired, as Bunnyfall
/// reads it) as World Data JSON, with the ops laid on. Refuses a patch whose base block is not at its index.
Result<Json> rebuildBlockPatch(const Json& patch, const formats::BsaArchive& blocks);

/// The classic block at a BLOCKS.BSA index as World Data JSON, exactly the base Bunnyfall builds patches on.
Result<Json> classicBlockJson(const formats::BsaArchive& blocks, std::size_t index);

/// The ops that turn `from` into `to` (Bunnyfall's diffJson): objects key by key, arrays by a shortest edit script
/// over whole elements, or element by element when the arrays are the same length and that is smaller.
Json diffJson(const Json& from, const Json& to);

/// A patch that rebuilds `edited` from the classic block at `baseIndex`, written as file `rebuilds` (the block's
/// file name: "<NAME>.RDB.json"). Checked by rebuilding it before it is returned.
Result<Json> makeBlockPatch(const formats::BsaArchive& blocks, std::size_t baseIndex, const Json& edited,
                            const std::string& rebuilds);

} // namespace de::exporter
