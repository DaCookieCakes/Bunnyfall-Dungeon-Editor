#include "edit/entities.h"
#include "project/prefab.h"

#include <doctest/doctest.h>

#include <filesystem>

using namespace de;

namespace {

/// A block with a lever (model 1, its action naming the door) and a door (model 2), and a treasure.
formats::RdbBlock leverAndDoor() {
    formats::RdbBlock b;
    b.width = b.height = 1;
    b.modelRefs.resize(formats::kRdbModelRefSlots);
    b.modelRefs[5] = {"41000", 41000, "LEV"};
    b.modelRefs[9] = {"55000", 55000, "DOR"};
    b.roots.resize(1);
    formats::RdbObject lever;
    lever.position = 100;
    lever.x = 1000;
    lever.y = -200;
    lever.z = 1000;
    formats::RdbModel lm;
    lm.modelIndex = 5;
    lm.actionOffset = 500;
    lm.action.position = 500;   // the JSON keeps this one: a model has an action when it is set
    lm.action.flags = 1;
    lm.action.nextObjectOffset = 200;   // the door
    lever.resource = lm;
    formats::RdbObject door = lever;
    door.position = 200;
    door.x = 1100;
    formats::RdbModel dm;
    dm.modelIndex = 9;
    door.resource = dm;
    b.roots[0].objects = {lever, door, edit::makeEntity(edit::EntityKind::Treasure, 1050, -200, 1000)};
    b.roots[0].objects[2].position = 300;
    edit::relinkActions(b);
    return b;
}

} // namespace

TEST_CASE("prefabs: made from a selection, saved, loaded and placed twice, links following the copies") {
    const auto source = leverAndDoor();
    // The lever and the door, about a point between them on the floor.
    auto p = project::makePrefab("Lever and door", source, {{0, 0}, {0, 1}}, {1050, -200, 1000});
    REQUIRE(p.objects().size() == 2);
    CHECK(p.objects()[0].x == -50);
    CHECK(p.objects()[1].x == 50);
    CHECK(p.block.modelRefs.size() == 2);   // only its own models
    CHECK(p.modelIdOf(p.objects()[1]) == 55000);

    const auto dir = std::filesystem::temp_directory_path() / "de_prefab_test";
    std::filesystem::remove_all(dir);
    const auto file = dir / project::prefabFileName(p.name);
    CHECK(file.filename() == "lever-and-door.prefab.json");
    REQUIRE(project::savePrefab(p, file));
    std::vector<std::string> problems;
    const auto all = project::loadPrefabFolder(dir, problems);
    CHECK(problems.empty());
    REQUIRE(all.size() == 1);
    const auto& loaded = all[0];
    CHECK(loaded.name == "Lever and door");
    REQUIRE(loaded.objects().size() == 2);
    CHECK(loaded.modelIdOf(loaded.objects()[0]) == 41000);

    // Into a block that already has the door's model in another slot.
    formats::RdbBlock target;
    target.width = target.height = 1;
    target.modelRefs.resize(formats::kRdbModelRefSlots);
    target.modelRefs[0] = {"55000", 55000, "DOR"};
    target.roots.resize(1);
    edit::BlockEdit e("T.RDB", 0, 0, target, target);
    const auto a = project::placePrefab(e, loaded, {0, 0, 0}, 0);
    const auto b = project::placePrefab(e, loaded, {500, 0, 0}, 512);
    REQUIRE(a);
    REQUIRE(b);
    CHECK(e.block().objectCount() == 4);
    for (const auto& placed : {*a, *b}) {
        const auto* lever = e.object(placed[0]);
        const auto* door = e.object(placed[1]);
        CHECK(lever->model()->action.nextObjectOffset == door->position);   // its own copy of the door
        CHECK(e.block().modelRefs[door->model()->modelIndex].modelIdNum == 55000);
        CHECK(door->model()->modelIndex == 0);   // the block's existing slot for it
    }
    // The second one is turned a quarter: the lever, west of the anchor, ends up north of it.
    CHECK(e.object((*b)[0])->x == 500);
    CHECK(e.object((*b)[0])->z == 50);
    // Each placing is one undo step.
    CHECK(e.undo());
    CHECK(e.block().objectCount() == 2);
    std::filesystem::remove_all(dir);
}
