#pragma once

// Prefabs: a set of objects (models, flats, lights) saved to place again as one, in any block. A prefab is kept as a
// small RDB block in World Data JSON (the same form as the editor's exports), its objects placed about its anchor:
// (0, 0, 0) is the middle of its bottom. Action links between its own objects survive placing; links to anything
// outside it are dropped.

#include "core/error.h"
#include "edit/block_edit.h"
#include "formats/rdb.h"

#include <array>
#include <filesystem>
#include <string>
#include <vector>

namespace de::project {

struct Prefab {
    std::string name;
    formats::RdbBlock block;   ///< One root; model references for its models only.
    std::filesystem::path file;   ///< Where it was loaded from (empty for one not saved yet).

    [[nodiscard]] const std::vector<formats::RdbObject>& objects() const { return block.roots.front().objects; }
    /// The model id an object of it shows (0 for flats and lights).
    [[nodiscard]] std::uint32_t modelIdOf(const formats::RdbObject& obj) const;
};

/// A prefab of `slots` of `source`, moved so `anchor` (file units, in the source block) becomes its (0, 0, 0).
Prefab makePrefab(std::string name, const formats::RdbBlock& source, const std::vector<edit::ObjectSlot>& slots,
                  std::array<std::int32_t, 3> anchor);

Result<void> savePrefab(const Prefab& prefab, const std::filesystem::path& file);
Result<Prefab> loadPrefab(const std::filesystem::path& file);
/// Every *.prefab.json in a folder, by name; ones that do not load are reported in `problems`.
std::vector<Prefab> loadPrefabFolder(const std::filesystem::path& dir, std::vector<std::string>& problems);
/// A file name for a prefab's name (letters, digits and dashes).
std::string prefabFileName(std::string_view name);

/// Adds the prefab's objects to `edit`, turned by `turn` (clockwise from above, file rotation units) about its anchor,
/// with the anchor at `at` (file units); one undo step. The new objects' slots, or nullopt (and nothing added) when
/// the block has no model slots left for its models.
std::optional<std::vector<edit::ObjectSlot>> placePrefab(edit::BlockEdit& edit, const Prefab& prefab,
                                                         std::array<std::int32_t, 3> at, std::int32_t turn);

} // namespace de::project
