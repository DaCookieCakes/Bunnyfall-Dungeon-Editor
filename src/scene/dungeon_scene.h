#pragma once

#include "catalog/space.h"
#include "core/error.h"
#include "formats/bsa.h"
#include "formats/climate.h"
#include "formats/maps.h"
#include "formats/rdb.h"
#include "scene/texture_table.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace de::scene {

/// Rotation then translation, editor space.
struct Transform {
    catalog::Mat3 rotation;
    catalog::Vec3 translation;
    [[nodiscard]] catalog::Vec3 apply(catalog::Vec3 p) const { return rotation * p + translation; }
};

/// Where an instance came from, so a picked thing can be shown in the inspector.
struct ObjectRef {
    std::size_t block = 0;    ///< Index into Scene::blocks.
    std::size_t root = 0;
    std::size_t object = 0;   ///< Index within the root.
    friend bool operator==(const ObjectRef&, const ObjectRef&) = default;
};

struct ModelInstance {
    std::uint32_t modelId = 0;
    Transform transform;
    ObjectRef ref;
};

struct FlatInstance {
    int archive = 0, record = 0;
    catalog::Vec3 position;   ///< Editor space; the centre of the sprite (RDB flats are placed by their centre).
    bool editorMarker = false;   ///< Archive 199: invisible in the game, shown by the editor as a marker.
    ObjectRef ref;
};

struct LightInstance {
    catalog::Vec3 position;
    std::uint16_t radius = 0;
    ObjectRef ref;
};

inline constexpr std::size_t kNoBsaIndex = static_cast<std::size_t>(-1);

struct SceneBlock {
    std::string name;
    std::size_t bsaIndex = kNoBsaIndex;   ///< Its BLOCKS.BSA record (what a patch of it is made against).
    catalog::Vec3 origin;    ///< Editor space; the block's (0,0,0) corner.
    bool isStartingBlock = false;
    formats::RdbBlock data;
};

struct Scene {
    std::string title;
    TextureTable textures;   ///< Model texture swaps; the base set (no swaps) for a lone block.
    int worldClimate = 0;    ///< 0 when not known (a lone block).
    std::vector<SceneBlock> blocks;
    std::vector<ModelInstance> models;
    std::vector<FlatInstance> flats;
    std::vector<LightInstance> lights;
};

/// Where a scene's blocks come from: the classic block by default, or (say) a saved patch of it.
using BlockSource = std::function<Result<formats::RdbBlock>(std::size_t bsaIndex)>;

/// One block on its own at the origin, shown as a starting block (model 70300, the exit door, included).
Scene blockScene(std::string name, formats::RdbBlock block, std::size_t bsaIndex = kNoBsaIndex);

/// A whole classic dungeon: every block at (x * 2048, 0, z * 2048), the exit door in the starting block only, and
/// the dungeon's classic texture table from its location id and the climate at its map pixel.
Result<Scene> dungeonScene(const formats::DungeonRecord& dungeon, const formats::BsaArchive& blocks,
                           const formats::PakMap& climate, const BlockSource& source = {});

/// Rebuilds the scene's models, flats and lights from its blocks (after a block's data changed).
void rebuildInstances(Scene& scene);

/// The instance drawn for an object, if it is drawn.
std::optional<std::size_t> modelIndexOf(const Scene& scene, const ObjectRef& ref);
std::optional<std::size_t> flatIndexOf(const Scene& scene, const ObjectRef& ref);

} // namespace de::scene
