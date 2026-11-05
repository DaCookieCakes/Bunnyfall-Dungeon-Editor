#include "arena2_fixture.h"
#include "edit/entities.h"
#include "export/dfu_json.h"
#include "project/project.h"

#include <doctest/doctest.h>

#include <filesystem>

using namespace de::project;

TEST_CASE("project: block names, save bases and ids are handed out once") {
    Project p;
    auto a = p.addBlock('N', "Hall", std::nullopt, emptyBlock());
    REQUIRE(a);
    CHECK((*a)->name == "NBF00001.RDB");
    CHECK(isProjectBlockName((*a)->name));
    const auto firstBase = (*a)->savePosition;
    auto b = p.addBlock('B', "Cap", std::nullopt, emptyBlock());
    REQUIRE(b);
    CHECK((*b)->name == "BBF00002.RDB");
    CHECK((*b)->savePosition == firstBase + kBlockSaveStride);
    CHECK_FALSE(p.addBlock('X', "bad", std::nullopt, emptyBlock()));

    // A removed block's number is not reused.
    p.removeBlock("BBF00002.RDB");
    auto c = p.addBlock('W', "Flooded", std::nullopt, emptyBlock());
    REQUIRE(c);
    CHECK((*c)->name == "WBF00003.RDB");

    auto d = p.addDungeon("The Burrow");
    REQUIRE(d);
    (*d)->pixelX = 109;
    (*d)->pixelY = 158;
    CHECK((*d)->number == 0);
    CHECK((*d)->pixelId() == 158109);
    CHECK((*d)->mapId() == ((1900u << 20) | 158109u));
    CHECK((*d)->mapId() < (1u << 31));
    CHECK((*d)->exteriorLocationId() == 60000);
    CHECK((*d)->dungeonLocationId() == 60001);

    CHECK_FALSE(isProjectBlockName("N0000071.RDB"));
    CHECK_FALSE(isProjectBlockName("NBF0001.RDB"));
}

TEST_CASE("project: the largest numbers stay within 32-bit signed") {
    const auto lastBase = kFirstBlockSavePosition + static_cast<std::uint64_t>(kMaxBlocks) * kBlockSaveStride;
    CHECK(lastBase + kBlockSaveStride < (1ull << 31));
    ProjectDungeon d;
    d.number = kMaxDungeons - 1;
    d.pixelX = 999;
    d.pixelY = 499;
    CHECK(d.mapId() < (1u << 31));
    CHECK(d.dungeonLocationId() < 65535);
}

TEST_CASE("project: saves and loads everything") {
    Project p;
    p.name = "Test";
    auto block = emptyBlock();
    block.roots[0].objects.push_back(de::edit::makeEntity(de::edit::EntityKind::Treasure, 10, -20, 30));
    block.roots[0].objects.back().position = 900000;
    REQUIRE(p.addBlock('N', "Hall", "N0000071.RDB", block));
    auto d = p.addDungeon("The Burrow");
    REQUIRE(d);
    (*d)->pixelX = 300;
    (*d)->pixelY = 200;
    (*d)->dungeonType = 6;
    (*d)->entrance = "RUINAA03.RMB";
    (*d)->discovered = false;
    (*d)->cells = {{0, 0, "NBF00001.RDB", true}, {1, 0, "B0000001.RDB", false}};

    const auto file = std::filesystem::temp_directory_path() / "de_project_test.json";
    REQUIRE(p.save(file));
    auto loaded = Project::load(file);
    std::filesystem::remove(file);
    REQUIRE_MESSAGE(loaded, (loaded ? "" : loaded.error().message));
    CHECK(loaded->name == "Test");
    REQUIRE(loaded->blocks().size() == 1);
    const auto& b = loaded->blocks()[0];
    CHECK(b.label == "Hall");
    CHECK(b.templateName == "N0000071.RDB");
    CHECK(de::exporter::rdbBlockToJson(b.data, b.name, 0, b.savePosition) ==
          de::exporter::rdbBlockToJson(p.blocks()[0].data, b.name, 0, b.savePosition));
    REQUIRE(loaded->dungeons().size() == 1);
    const auto& ld = loaded->dungeons()[0];
    CHECK(ld.name == "The Burrow");
    CHECK(ld.pixelX == 300);
    CHECK(ld.dungeonType == 6);
    CHECK(ld.entrance == "RUINAA03.RMB");
    CHECK_FALSE(ld.discovered);
    CHECK(ld.cells == (*d)->cells);
    CHECK(ld.startingCell()->blockName == "NBF00001.RDB");
    // Counters come back too: the next block is number 2.
    Project again = std::move(*loaded);
    auto next = again.addBlock('N', "Next", std::nullopt, emptyBlock());
    REQUIRE(next);
    CHECK((*next)->name == "NBF00002.RDB");
}

TEST_CASE("project: an id range numbers blocks, places and models apart from other projects'") {
    Project p;
    CHECK(p.mod.idRange == 0);
    REQUIRE(p.setIdRange(3));
    auto b = p.addBlock('N', "Hall", std::nullopt, emptyBlock());
    REQUIRE(b);
    CHECK((*b)->name == "NBF00748.RDB");   // 3 * 249 + 1
    CHECK((*b)->savePosition == kFirstBlockSavePosition + 748u * kBlockSaveStride);
    auto d = p.addDungeon("Far Away");
    REQUIRE(d);
    CHECK((*d)->number == 0);
    CHECK((*d)->exteriorLocationId() == 60000 + 2 * 3 * 148);
    auto m = p.addModel("Room", "C00", {});
    REQUIRE(m);
    CHECK((*m)->id == kFirstCustomModelId + 3u * kModelsPerRange + 1u);
    // Fixed once numbers are out.
    CHECK_FALSE(p.setIdRange(4));
    CHECK_FALSE(p.setIdRange(kIdRanges));

    // Saved and loaded, the range and the counters come back.
    const auto file = std::filesystem::temp_directory_path() / "de_project_range_test.json";
    p.mod.folder = "far-away";
    p.mod.author = "Kay";
    REQUIRE(p.save(file));
    auto loaded = Project::load(file);
    std::filesystem::remove(file);
    REQUIRE_MESSAGE(loaded, (loaded ? "" : loaded.error().message));
    CHECK(loaded->mod.idRange == 3);
    CHECK(loaded->mod.folder == "far-away");
    CHECK(loaded->mod.author == "Kay");
    CHECK(loaded->dungeons()[0].exteriorLocationId() == (*d)->exteriorLocationId());
    auto next = loaded->addBlock('N', "Next", std::nullopt, emptyBlock());
    REQUIRE(next);
    CHECK((*next)->name == "NBF00749.RDB");

    // The highest range stays inside every limit.
    ProjectDungeon last;
    last.idRange = kIdRanges - 1;
    last.number = kDungeonsPerRange - 1;
    CHECK(last.dungeonLocationId() < 65535);
    CHECK(kIdRanges * kBlocksPerRange <= kMaxBlocks);
}

TEST_CASE("project: mod folder names") {
    CHECK_FALSE(modFolderProblem("bunnyfall-dungeons"));
    CHECK_FALSE(modFolderProblem("crypts2"));
    CHECK(modFolderProblem(""));
    CHECK(modFolderProblem("My Mod"));
    CHECK(modFolderProblem("-lead"));
    CHECK(modFolderProblem("../escape"));
}
