#pragma once

#include "catalog/space.h"
#include "core/error.h"
#include "formats/bsa.h"
#include "formats/climate.h"
#include "formats/maps.h"
#include "formats/rdb.h"
#include "scene/texture_table.h"

#include <cstdint>
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

struct SceneBlock {
    std::string name;
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

/// One block on its own at the origin. `allowExitDoor` keeps model 70300, which only the starting block shows.
Scene blockScene(std::string name, formats::RdbBlock block, bool allowExitDoor = true);

/// A whole classic dungeon: every block at (x * 2048, 0, z * 2048), the exit door in the starting block only, and
/// the dungeon's classic texture table from its location id and the climate at its map pixel.
Result<Scene> dungeonScene(const formats::DungeonRecord& dungeon, const formats::BsaArchive& blocks,
                           const formats::PakMap& climate);

} // namespace de::scene
