#include "arena2_fixture.h"
#include "edit/block_edit.h"
#include "edit/entities.h"
#include "edit/placement.h"
#include "export/dfu_json.h"
#include "export/patch_folder.h"

#include <doctest/doctest.h>

#include <filesystem>
#include <set>

using namespace de::edit;
using de::formats::RdbBlock;

namespace {

RdbBlock smallBlock() {
    RdbBlock b;
    b.width = b.height = 1;
    b.modelRefs.resize(de::formats::kRdbModelRefSlots);
    b.roots.resize(2);
    b.roots[1].objects.push_back(makeEntity(EntityKind::Treasure, 10, -20, 30));
    b.roots[1].objects.back().position = 100;
    return b;
}

/// A temporary folder removed when the test ends.
struct TempDir {
    std::filesystem::path path;
    TempDir() {
        path = std::filesystem::temp_directory_path() /
               ("de_edit_test_" + std::to_string(reinterpret_cast<std::uintptr_t>(this)));
        std::filesystem::remove_all(path);
    }
    ~TempDir() { std::filesystem::remove_all(path); }
};

} // namespace

TEST_CASE("entities: markers by kind, and what an object is read back as") {
    for (const auto& info : entityKinds()) {
        CAPTURE(info.name);
        const auto obj = makeEntity(info.kind, 1, 2, 3);
        CHECK(kindOf(obj) == info.kind);
        if (info.markerRecord >= 0) {
            CHECK(obj.flat()->textureArchive() == 199);
            CHECK(obj.flat()->textureRecord() == info.markerRecord);
        }
    }
    const auto torch = makeEntity(EntityKind::Scenery, 0, 0, 0, 210, 6);
    CHECK(torch.flat()->textureArchive() == 210);
    CHECK(torch.flat()->textureRecord() == 6);
    CHECK(makeEntity(EntityKind::Light, 0, 0, 0).light()->radius > 0);
}

TEST_CASE("entities: a flat from a people archive is a person") {
    CHECK(kindOf(makeEntity(EntityKind::Scenery, 0, 0, 0, 357, 3)) == EntityKind::Person);
    CHECK(kindOf(makeEntity(EntityKind::Scenery, 0, 0, 0, 210, 3)) == EntityKind::Scenery);
    const auto chosen = makeEntity(EntityKind::Person, 0, 0, 0, 184, 2);
    CHECK(chosen.flat()->textureArchive() == 184);
    CHECK(chosen.flat()->textureRecord() == 2);
    CHECK(isPersonArchive(makeEntity(EntityKind::Person, 0, 0, 0, 210, 3).flat()->textureArchive()));
}

TEST_CASE("entities: text is cut by characters, never through one") {
    CHECK(clampText("abc", 5) == "abc");
    CHECK(clampText("abcdef", 3) == "abc");
    CHECK(clampText("h\xc3\xa9llo", 2) == "h\xc3\xa9");
    CHECK(clampText("", 0).empty());
    CHECK(clampText("a", 0).empty());
}

TEST_CASE("entities: enemy fields as Daggerfall reads them") {
    auto flat = *makeEntity(EntityKind::FixedEnemy, 0, 0, 0).flat();
    setFixedEnemyType(flat, 32);
    CHECK(fixedEnemyType(flat) == 32);
    CHECK(flat.factionOrMobileId() == 32);
    setFixedEnemyType(flat, 144);
    CHECK(fixedEnemyType(flat) == 144);
    CHECK(enemyName(144) == "Warrior");
    CHECK(enemyName(39) == "unknown");

    CHECK_FALSE(isPassive(flat));
    setPassive(flat, true);
    CHECK(flat.action == 99);

    setEnemyGender(flat, Gender::Female);
    CHECK(enemyGender(flat) == Gender::Female);
    setEnemyGender(flat, Gender::Male);
    CHECK(enemyGender(flat) == Gender::Male);
    CHECK((flat.flags & 3) == 2);

    auto random = *makeEntity(EntityKind::RandomEnemy, 0, 0, 0).flat();
    setRandomEnemySlot(random, 3);
    CHECK(randomEnemySlot(random) == 3);
    setRandomEnemySlot(random, 999);
    CHECK(randomEnemySlot(random) == 255);
}

TEST_CASE("placement: sprites stand on floors, hang from ceilings, stand off walls") {
    const auto floor = restingCentre({0, 100, 0}, {0, 1, 0}, 40, 60);
    CHECK(floor.y == doctest::Approx(131));
    const auto ceiling = restingCentre({0, 100, 0}, {0, -1, 0}, 40, 60);
    CHECK(ceiling.y == doctest::Approx(69));
    const auto wall = restingCentre({0, 100, 0}, {1, 0, 0}, 40, 60);
    CHECK(wall.x == doctest::Approx(21));
    CHECK(wall.y == doctest::Approx(100));
    CHECK(snapTo(37, 16) == 32);
    CHECK(snapTo(-41, 16) == -48);
    CHECK(snapTo(37.6, 1) == 38);
}

TEST_CASE("block edit: add, replace, remove, undo and redo") {
    BlockEdit edit("X.RDB", 0, 0, smallBlock(), smallBlock());
    CHECK_FALSE(edit.dirty());
    CHECK_FALSE(edit.differsFromClassic());

    const auto slot = edit.add(makeEntity(EntityKind::Light, 5, 6, 7));
    CHECK(slot.root == 1);   // the first root holding objects
    CHECK(edit.dirty());
    CHECK(edit.differsFromClassic());
    const auto* added = edit.object(slot);
    REQUIRE(added);
    CHECK(added->position >= kFirstNewPosition);
    CHECK(added->index == static_cast<std::int32_t>(slot.object));

    auto moved = *added;
    moved.x = 500;
    moved.position = 1;   // ignored: a save identity never changes
    REQUIRE(edit.replace(slot, moved));
    CHECK(edit.object(slot)->x == 500);
    CHECK(edit.object(slot)->position == added->position);

    REQUIRE(edit.undo());
    CHECK(edit.object(slot)->x == 5);
    REQUIRE(edit.redo());
    CHECK(edit.object(slot)->x == 500);

    REQUIRE(edit.remove({1, 0}));   // the treasure
    CHECK(edit.block().roots[1].objects.size() == 1);
    CHECK(edit.block().roots[1].objects[0].index == 0);   // re-numbered
    REQUIRE(edit.undo());
    REQUIRE(edit.undo());
    REQUIRE(edit.undo());
    CHECK_FALSE(edit.canUndo());
    CHECK_FALSE(edit.differsFromClassic());
}

TEST_CASE("block edit: a person's words are a change, one undo step of typing") {
    de::formats::RdbBlock classic;
    classic.roots.emplace_back();
    BlockEdit edit("N0000000.RDB", 0, 0, classic, classic);
    const auto slot = edit.add(makeEntity(EntityKind::Person, 0, 0, 0, 182, 1));
    const auto saved = edit.block();
    auto typedInto = [&](const std::string& name) {
        auto obj = *edit.object(slot);
        std::get<de::formats::RdbFlat>(obj.resource).personName = name;
        edit.replace(slot, std::move(obj));
    };
    edit.beginGesture();
    typedInto("O");
    typedInto("Ol");
    typedInto("Old");
    edit.endGesture();
    CHECK(edit.object(slot)->flat()->personName == "Old");
    edit.undo();
    CHECK(edit.object(slot)->flat()->personName.empty());
    CHECK(edit.block().objectCount() == saved.objectCount());
    edit.redo();

    auto obj = *edit.object(slot);
    std::get<de::formats::RdbFlat>(obj.resource).personName.clear();
    std::get<de::formats::RdbFlat>(obj.resource).personLines = {"Hello."};
    edit.replace(slot, std::move(obj));
    edit.undo();
    CHECK(edit.object(slot)->flat()->personLines.empty());

    const auto copy = edit.duplicate(slot);
    REQUIRE(copy);
    CHECK(edit.object(*copy)->flat()->personName == "Old");
}

TEST_CASE("block edit: new objects never share a save identity") {
    BlockEdit edit("X.RDB", 0, 0, smallBlock(), smallBlock());
    std::set<std::int32_t> seen;
    std::set<std::uint32_t> flatResources;
    for (int i = 0; i < 20; ++i) {
        const auto slot = edit.add(makeEntity(i % 2 ? EntityKind::Light : EntityKind::RandomEnemy, i, 0, 0));
        const auto* obj = edit.object(slot);
        CHECK(seen.insert(obj->position).second);
        if (const auto* f = obj->flat()) {
            CHECK(flatResources.insert(f->position).second);
            CHECK(!seen.contains(static_cast<std::int32_t>(f->position)));
        }
    }
    const auto copy = edit.duplicate({1, 1});
    REQUIRE(copy);
    CHECK(seen.insert(edit.object(*copy)->position).second);
}

TEST_CASE("block edit: a gesture is one undo step, and cancelling puts it back") {
    BlockEdit edit("X.RDB", 0, 0, smallBlock(), smallBlock());
    const ObjectSlot treasure{1, 0};
    edit.beginGesture();
    for (int x = 0; x < 10; ++x) {
        auto o = *edit.object(treasure);
        o.x = x * 10;
        edit.replace(treasure, o);
    }
    edit.endGesture();
    CHECK(edit.object(treasure)->x == 90);
    REQUIRE(edit.undo());
    CHECK(edit.object(treasure)->x == 10);
    CHECK_FALSE(edit.canUndo());

    edit.beginGesture();
    auto o = *edit.object(treasure);
    o.x = 777;
    edit.replace(treasure, o);
    edit.cancelGesture();
    CHECK(edit.object(treasure)->x == 10);
    CHECK_FALSE(edit.canUndo());

    edit.beginGesture();   // a gesture that changes nothing leaves no undo step
    edit.endGesture();
    CHECK_FALSE(edit.canUndo());
}

TEST_CASE("ARENA2: blocks survive JSON both ways, and relinking changes nothing") {
    DE_REQUIRE_ARENA2(a);
    int checked = 0;
    for (std::size_t i = 0; i < a->blocks.count(); ++i) {
        const auto& entry = a->blocks.entry(i);
        if (!entry.name.ends_with(".RDB")) continue;
        INFO(entry.name);
        auto block = de::formats::readClassicRdbBlock(a->blocks, i);
        REQUIRE(block);
        const auto json = de::exporter::rdbBlockToJson(*block, entry.name, i, entry.offset);
        auto back = de::exporter::rdbBlockFromJson(json);
        REQUIRE_MESSAGE(back, (back ? "" : back.error().message));
        CHECK(de::exporter::rdbBlockToJson(*back, entry.name, i, entry.offset) == json);

        auto relinked = *block;
        relinkActions(relinked);
        CHECK(de::exporter::rdbBlockToJson(relinked, entry.name, i, entry.offset) == json);
        ++checked;
    }
    CHECK(checked == 187);
}

TEST_CASE("ARENA2: an edited block saves as a patch, loads back the same, and saves away to nothing") {
    DE_REQUIRE_ARENA2(a);
    const auto index = *a->blocks.findByName("N0000071.RDB");
    auto classic = de::formats::readClassicRdbBlock(a->blocks, index);
    REQUIRE(classic);
    BlockEdit edit("N0000071.RDB", index, a->blocks.entry(index).offset, *classic, *classic);
    const auto added = edit.add(makeEntity(EntityKind::FixedEnemy, 1000, -500, 1000));
    auto enemy = *edit.object(added);
    setFixedEnemyType(std::get<de::formats::RdbFlat>(enemy.resource), 32);
    edit.replace(added, enemy);

    TempDir dir;
    const de::exporter::PatchFolder folder(dir.path);
    auto saved = folder.save(a->blocks, index, edit.block());
    REQUIRE_MESSAGE(saved, (saved ? "" : saved.error().message));
    CHECK(*saved == de::exporter::PatchFolder::Saved::Written);
    CHECK(folder.has("N0000071.RDB"));

    auto loaded = folder.load(a->blocks, index);
    REQUIRE(loaded);
    REQUIRE(loaded->has_value());
    const auto& entry = a->blocks.entry(index);
    CHECK(de::exporter::rdbBlockToJson(**loaded, entry.name, index, entry.offset) ==
          de::exporter::rdbBlockToJson(edit.block(), entry.name, index, entry.offset));

    // Undo the edit and save: the block is classic again, so the patch goes.
    edit.undo();
    edit.undo();
    saved = folder.save(a->blocks, index, edit.block());
    REQUIRE(saved);
    CHECK(*saved == de::exporter::PatchFolder::Saved::Removed);
    CHECK_FALSE(folder.has("N0000071.RDB"));
    auto none = folder.load(a->blocks, index);
    REQUIRE(none);
    CHECK_FALSE(none->has_value());
}

TEST_CASE("models: adding takes a free model slot once, reuses it, and undoes as one step") {
    BlockEdit e("TEST.RDB", 0, 0, smallBlock(), smallBlock());
    de::formats::RdbObject obj;
    obj.resource = de::formats::RdbModel{};
    const auto a = e.addModel(4321, "C00X", obj);
    REQUIRE(a);
    const auto slot = e.modelSlot(4321);
    REQUIRE(slot);
    CHECK(e.block().modelRefs[*slot].modelId == "4321");
    CHECK(e.block().modelRefs[*slot].description == "C00");   // three characters, as the file keeps it
    CHECK(e.object(*a)->model()->modelIndex == *slot);
    const auto b = e.addModel(4321, "C00", obj);
    REQUIRE(b);
    CHECK(e.object(*b)->model()->modelIndex == *slot);
    CHECK(e.object(*a)->position != e.object(*b)->position);   // each its own save identity
    CHECK(e.undo());
    CHECK(e.undo());
    CHECK_FALSE(e.modelSlot(4321));   // the slot went with the first add
    CHECK(e.block().objectCount() == 1);
    // Flats are not models.
    CHECK_FALSE(e.addModel(1, "XXX", makeEntity(EntityKind::Treasure, 0, 0, 0)));
}

TEST_CASE("placement: models rest their bounds on floors, under ceilings and against walls") {
    const de::catalog::Box bounds{{-100, -20, -50}, {100, 180, 50}};
    const auto floor = restingModelOrigin({0, 500, 0}, {0, 1, 0}, bounds);
    CHECK(floor.y + bounds.min.y == doctest::Approx(500));
    const auto ceiling = restingModelOrigin({0, 500, 0}, {0, -1, 0}, bounds);
    CHECK(ceiling.y + bounds.max.y == doctest::Approx(500));
    // A wall facing -X (the model sits on the wall's -X side): its +X side touches the wall.
    const auto wall = restingModelOrigin({1000, 500, 0}, {-1, 0, 0}, bounds);
    CHECK(wall.x + bounds.max.x == doctest::Approx(1000));
    const auto north = restingModelOrigin({0, 500, 1000}, {0, 0, 1}, bounds);   // a wall facing +Z
    CHECK(north.z + bounds.min.z == doctest::Approx(1000));
}

TEST_CASE("models: another model on the same object keeps its place and is one undo step") {
    BlockEdit e("TEST.RDB", 0, 0, smallBlock(), smallBlock());
    de::formats::RdbObject obj;
    obj.x = 128;
    obj.resource = de::formats::RdbModel{.yRotation = 512};
    const auto a = e.addModel(67000, "C00", obj);
    REQUIRE(a);
    CHECK(e.setModel(*a, 67100, "C00"));
    const auto* m = e.object(*a)->model();
    CHECK(e.block().modelRefs[m->modelIndex].modelIdNum == 67100);
    CHECK(m->yRotation == 512);
    CHECK(e.object(*a)->x == 128);
    CHECK(e.undo());
    CHECK(e.block().modelRefs[e.object(*a)->model()->modelIndex].modelIdNum == 67000);
    CHECK_FALSE(e.setModel({1, 0}, 1, "XXX"));   // the treasure is not a model
}
