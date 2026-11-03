// Tests against the real ARENA2 data. They read DE_ARENA2 (CTest sets it to ../DFData/ARENA2 by default) and are
// skipped, with a message, when the data is not there. The expected numbers were measured by tools/dfprobe.py.

#include "arena2_fixture.h"
#include "formats/arch3d.h"
#include "formats/arena2.h"
#include "formats/maps.h"
#include "formats/rdb.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <cstdlib>
#include <map>
#include <optional>
#include <set>

using namespace de::formats;


TEST_CASE("ARENA2: every RDB block parses, with the classic role mix") {
    DE_REQUIRE_ARENA2(a);
    std::map<RdbRole, int> roles;
    int total = 0;
    std::size_t maxObjects = 0;
    for (std::size_t i = 0; i < a->blocks.count(); ++i) {
        const auto& name = a->blocks.entry(i).name;
        if (!name.ends_with(".RDB")) continue;
        auto block = parseRdbBlock(a->blocks.record(i));
        INFO(name);
        REQUIRE_MESSAGE(block, (block ? "" : block.error().message));
        ++total;
        ++roles[rdbRoleFromName(name)];
        maxObjects = std::max(maxObjects, block->objectCount());
    }
    CHECK(total == 187);
    CHECK(roles[RdbRole::Normal] == 93);
    CHECK(roles[RdbRole::Quest] == 40);
    CHECK(roles[RdbRole::Wet] == 30);
    CHECK(roles[RdbRole::Border] == 15);
    CHECK(roles[RdbRole::Mausoleum] == 9);
    CHECK(maxObjects == 391);
}

TEST_CASE("ARENA2: every dungeon starts at (0,0) and names real blocks") {
    DE_REQUIRE_ARENA2(a);
    auto dungeons = readDungeons(a->maps);
    REQUIRE_MESSAGE(dungeons, (dungeons ? "" : dungeons.error().message));
    CHECK(dungeons->size() == 4232);
    for (const auto& d : *dungeons) {
        INFO(d.name);
        int starts = 0;
        for (const auto& b : d.blocks) {
            CHECK(a->blocks.findByName(b.blockName).has_value());
            if (b.isStartingBlock) {
                ++starts;
                CHECK(b.x == 0);
                CHECK(b.z == 0);
                CHECK(rdbRoleFromName(b.blockName) != RdbRole::Border);
            }
        }
        CHECK(starts == 1);
        CHECK(d.blocks.size() >= 5);
        CHECK(d.blocks.size() <= 22);
    }
}

TEST_CASE("ARENA2: every model a dungeon block uses parses") {
    DE_REQUIRE_ARENA2(a);
    std::set<std::uint32_t> ids;
    for (std::size_t i = 0; i < a->blocks.count(); ++i) {
        if (!a->blocks.entry(i).name.ends_with(".RDB")) continue;
        auto block = parseRdbBlock(a->blocks.record(i));
        REQUIRE(block);
        for (const auto& root : block->roots)
            for (const auto& obj : root.objects)
                if (const auto* m = obj.model()) ids.insert(block->modelRefs[m->modelIndex].modelIdNum);
    }
    CHECK(ids.size() >= 1000);
    for (const auto id : ids) {
        INFO("model " << id);
        const auto index = a->arch3d.findById(id);
        REQUIRE(index);
        auto mesh = parseArch3dMesh(a->arch3d.record(*index), id);
        REQUIRE_MESSAGE(mesh, (mesh ? "" : mesh.error().message));
    }
}

TEST_CASE("ARENA2: the standard corridor piece is 128 x 128 x 256 block units") {
    DE_REQUIRE_ARENA2(a);
    const auto index = a->arch3d.findById(67000);
    REQUIRE(index);
    auto mesh = parseArch3dMesh(a->arch3d.record(*index), 67000);
    REQUIRE(mesh);
    CHECK(mesh->bounds.sizeX() == 128 * 256);
    CHECK(mesh->bounds.sizeY() == 128 * 256);
    CHECK(mesh->bounds.sizeZ() == 256 * 256);
}
