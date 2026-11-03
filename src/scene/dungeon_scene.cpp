#include "scene/dungeon_scene.h"

namespace de::scene {

namespace {

constexpr std::uint32_t kExitDoorModel = 70300;
constexpr int kEditorFlatArchive = 199;

void addInstances(Scene& scene, std::size_t blockIndex) {
    const auto& block = scene.blocks[blockIndex];
    const auto& data = block.data;
    for (std::size_t r = 0; r < data.roots.size(); ++r) {
        const auto& objects = data.roots[r].objects;
        for (std::size_t o = 0; o < objects.size(); ++o) {
            const auto& obj = objects[o];
            const ObjectRef ref{blockIndex, r, o};
            const auto world = catalog::fromFilePosition(obj.x, obj.y, obj.z) + block.origin;
            if (const auto* m = obj.model()) {
                const auto id = data.modelRefs[m->modelIndex].modelIdNum;
                if (id == kExitDoorModel && !block.isStartingBlock) continue;   // only the starting block shows it
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
}

} // namespace

void rebuildInstances(Scene& scene) {
    scene.models.clear();
    scene.flats.clear();
    scene.lights.clear();
    for (std::size_t b = 0; b < scene.blocks.size(); ++b) addInstances(scene, b);
}

std::optional<std::size_t> modelIndexOf(const Scene& scene, const ObjectRef& ref) {
    for (std::size_t i = 0; i < scene.models.size(); ++i) {
        if (scene.models[i].ref == ref) return i;
    }
    return std::nullopt;
}

std::optional<std::size_t> flatIndexOf(const Scene& scene, const ObjectRef& ref) {
    for (std::size_t i = 0; i < scene.flats.size(); ++i) {
        if (scene.flats[i].ref == ref) return i;
    }
    return std::nullopt;
}

Scene blockScene(std::string name, formats::RdbBlock block, std::size_t bsaIndex) {
    Scene scene;
    scene.title = name;
    scene.blocks.push_back(SceneBlock{std::move(name), bsaIndex, {}, true, std::move(block)});
    rebuildInstances(scene);
    return scene;
}

Result<Scene> dungeonScene(const formats::DungeonRecord& dungeon, const formats::BsaArchive& blocks,
                           const formats::PakMap& climate, const BlockSource& source) {
    Scene scene;
    scene.title = dungeon.name;
    const auto [px, py] = formats::mapPixel(dungeon.map.longitude, dungeon.map.latitude);
    scene.worldClimate = climate.at(px, py);
    scene.textures = TextureTable::classic(dungeon.locationId, scene.worldClimate);
    for (const auto& ref : dungeon.blocks) {
        const auto index = blocks.findByName(ref.blockName);
        if (!index) return fail(dungeon.name + ": block " + ref.blockName + " not in BLOCKS.BSA");
        auto data = source ? source(*index) : formats::readClassicRdbBlock(blocks, *index);
        if (!data) return std::unexpected(data.error());
        const catalog::Vec3 origin{double(ref.x) * catalog::kRdbBlockSide, 0, double(ref.z) * catalog::kRdbBlockSide};
        scene.blocks.push_back(SceneBlock{ref.blockName, *index, origin, ref.isStartingBlock, std::move(*data)});
    }
    rebuildInstances(scene);
    return scene;
}

} // namespace de::scene
