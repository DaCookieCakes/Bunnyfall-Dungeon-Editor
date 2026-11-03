#include "scene/dungeon_scene.h"

namespace de::scene {

namespace {

constexpr std::uint32_t kExitDoorModel = 70300;
constexpr int kEditorFlatArchive = 199;

void addBlock(Scene& scene, SceneBlock block, bool allowExitDoor) {
    const std::size_t blockIndex = scene.blocks.size();
    const auto& data = block.data;
    for (std::size_t r = 0; r < data.roots.size(); ++r) {
        const auto& objects = data.roots[r].objects;
        for (std::size_t o = 0; o < objects.size(); ++o) {
            const auto& obj = objects[o];
            const ObjectRef ref{blockIndex, r, o};
            const auto local = catalog::fromFilePosition(obj.x, obj.y, obj.z);
            const auto world = local + block.origin;
            if (const auto* m = obj.model()) {
                const auto id = data.modelRefs[m->modelIndex].modelIdNum;
                if (id == kExitDoorModel && !allowExitDoor) continue;
                scene.models.push_back(
                    {id, {catalog::rdbModelRotation(m->xRotation, m->yRotation, m->zRotation), world}, ref});
            } else if (const auto* f = obj.flat()) {
                scene.flats.push_back(
                    {f->textureArchive(), f->textureRecord(), world, f->textureArchive() == kEditorFlatArchive, ref});
            } else if (const auto* l = obj.light()) {
                scene.lights.push_back({world, l->radius, ref});
            }
        }
    }
    scene.blocks.push_back(std::move(block));
}

} // namespace

Scene blockScene(std::string name, formats::RdbBlock block, bool allowExitDoor) {
    Scene scene;
    scene.title = name;
    addBlock(scene, SceneBlock{std::move(name), {}, allowExitDoor, std::move(block)}, allowExitDoor);
    return scene;
}

Result<Scene> dungeonScene(const formats::DungeonRecord& dungeon, const formats::BsaArchive& blocks,
                           const formats::PakMap& climate) {
    Scene scene;
    scene.title = dungeon.name;
    const auto [px, py] = formats::mapPixel(dungeon.map.longitude, dungeon.map.latitude);
    scene.worldClimate = climate.at(px, py);
    scene.textures = TextureTable::classic(dungeon.locationId, scene.worldClimate);
    for (const auto& ref : dungeon.blocks) {
        const auto index = blocks.findByName(ref.blockName);
        if (!index) return fail(dungeon.name + ": block " + ref.blockName + " not in BLOCKS.BSA");
        auto data = formats::readClassicRdbBlock(blocks, *index);
        if (!data) return std::unexpected(data.error());
        const catalog::Vec3 origin{double(ref.x) * catalog::kRdbBlockSide, 0, double(ref.z) * catalog::kRdbBlockSide};
        addBlock(scene, SceneBlock{ref.blockName, origin, ref.isStartingBlock, std::move(*data)}, ref.isStartingBlock);
    }
    return scene;
}

} // namespace de::scene
