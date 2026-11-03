#include "arena2_fixture.h"
#include "formats/maps.h"
#include "scene/dungeon_scene.h"

#include <doctest/doctest.h>

#include <algorithm>

using namespace de::scene;

TEST_CASE("ARENA2: Castle Daggerfall's dungeon assembles on its grid") {
    DE_REQUIRE_ARENA2(a);
    auto dungeons = de::formats::readDungeons(a->maps);
    REQUIRE(dungeons);
    const auto it = std::ranges::find_if(*dungeons, [](const auto& d) { return d.locationId == 50027; });
    REQUIRE(it != dungeons->end());
    auto scene = dungeonScene(*it, a->blocks, a->climate);
    REQUIRE(scene);
    CHECK(scene->blocks.size() == 16);
    CHECK_FALSE(scene->models.empty());
    CHECK_FALSE(scene->flats.empty());
    CHECK_FALSE(scene->lights.empty());

    // Every block sits on the 2048 grid, and its instances inside or near its square.
    for (std::size_t b = 0; b < scene->blocks.size(); ++b) {
        const auto& block = scene->blocks[b];
        CHECK(std::fmod(block.origin.x, 2048.0) == 0.0);
        CHECK(std::fmod(block.origin.z, 2048.0) == 0.0);
    }
    for (const auto& m : scene->models) {
        const auto& origin = scene->blocks[m.ref.block].origin;
        CHECK(m.transform.translation.x >= origin.x - 1024);
        CHECK(m.transform.translation.x <= origin.x + 2048 + 1024);
    }

    // The exit door shows only in the starting block.
    for (const auto& m : scene->models) {
        if (m.modelId == 70300) CHECK(scene->blocks[m.ref.block].isStartingBlock);
    }
}

TEST_CASE("the exit door shows in the starting block only") {
    de::formats::RdbBlock block;
    block.width = block.height = 1;
    block.modelRefs.resize(de::formats::kRdbModelRefSlots);
    block.modelRefs[0] = {"70300", 70300, "EXT"};
    de::formats::RdbObject obj;
    obj.resource = de::formats::RdbModel{};
    block.roots.push_back({0, {obj}});
    auto scene = blockScene("X", block);
    CHECK(scene.models.size() == 1);   // a lone block shows as a starting block
    scene.blocks[0].isStartingBlock = false;
    rebuildInstances(scene);
    CHECK(scene.models.empty());       // anywhere else the exit door is not drawn
}
