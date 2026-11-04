#include "arena2_fixture.h"
#include "export/world_data_patch.h"
#include "formats/climate.h"
#include "project/world_export.h"

#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>

using namespace de::project;

namespace {

struct Fixture {
    const de::formats::Arena2& a;
    de::catalog::Catalog catalog;
    World world;
    explicit Fixture(const de::formats::Arena2& arena2)
        : a(arena2), catalog(*de::catalog::Catalog::build(arena2)), world{arena2, catalog, *de::formats::readLocations(arena2.maps)} {}

    /// A free land pixel in a region, near (x, y).
    std::pair<int, int> freePixel(int region, int x, int y) const {
        for (int r = 1; r < 40; ++r)
            for (int dy = -r; dy <= r; ++dy)
                for (int dx = -r; dx <= r; ++dx) {
                    const int px = x + dx, py = y + dy;
                    if (de::formats::regionAtPixel(a.politic, px, py) != region) continue;
                    if (de::formats::terrainClimateAt(a.climate, px, py) == de::formats::kClimateOcean) continue;
                    const bool taken = std::ranges::any_of(world.locations, [&](const auto& l) {
                        return l.pixelX() == px && l.pixelY() == py;
                    });
                    if (!taken) return {px, py};
                }
        return {-1, -1};
    }
};

/// A classic-style dungeon: one interior block and four caps.
void plusShape(ProjectDungeon& d, const std::string& centre) {
    d.cells = {{0, 0, centre, true},
               {-1, 0, "B0000001.RDB", false},
               {1, 0, "B0000002.RDB", false},
               {0, -1, "B0000003.RDB", false},
               {0, 1, "B0000004.RDB", false}};
}

bool hasIssue(const std::vector<Issue>& issues, Issue::Severity severity, std::string_view text) {
    return std::ranges::any_of(issues, [&](const Issue& i) { return i.severity == severity && i.text.find(text) != std::string::npos; });
}

struct TempDir {
    std::filesystem::path path = std::filesystem::temp_directory_path() / "de_world_export_test";
    TempDir() { std::filesystem::remove_all(path); }
    ~TempDir() { std::filesystem::remove_all(path); }
};

} // namespace

TEST_CASE("ARENA2: a dungeon on free land with closed sides has nothing wrong") {
    DE_REQUIRE_ARENA2(a);
    Fixture f(*a);
    Project p;
    auto d = *p.addDungeon("The Burrow");
    std::tie(d->pixelX, d->pixelY) = f.freePixel(17, 109, 158);
    REQUIRE(d->pixelX >= 0);
    plusShape(*d, "N0000071.RDB");
    const auto issues = checkDungeon(p, *d, f.world);
    for (const auto& i : issues) MESSAGE(i.text);
    CHECK(issues.empty());
}

TEST_CASE("ARENA2: the checks refuse what Bunnyfall would refuse or get wrong") {
    DE_REQUIRE_ARENA2(a);
    Fixture f(*a);
    Project p;
    auto d = *p.addDungeon("Privateer's Hold");   // a name the region already has
    d->pixelX = 109;                               // and its pixel
    d->pixelY = 158;
    d->cells = {{0, 0, "B0000001.RDB", true}, {1, 0, "N9999999.RDB", false}};
    d->entrance = "NOPE.RMB";
    const auto issues = checkDungeon(p, *d, f.world);
    using S = Issue::Severity;
    CHECK(hasIssue(issues, S::Error, "already has a place called"));
    CHECK(hasIssue(issues, S::Error, "already holds Privateer's Hold"));
    CHECK(hasIssue(issues, S::Error, "starting block is a border cap"));
    CHECK(hasIssue(issues, S::Error, "does not exist"));
    CHECK(hasIssue(issues, S::Error, "is not a surface block"));

    d->name = "Somewhere Wet";
    d->pixelX = 500;   // the middle of the Iliac Bay
    d->pixelY = 250;
    CHECK(hasIssue(checkDungeon(p, *d, f.world), S::Error, "is water"));
}

TEST_CASE("ARENA2: a starting block with no start marker is an error") {
    DE_REQUIRE_ARENA2(a);
    Fixture f(*a);
    Project p;
    auto empty = *p.addBlock('N', "Empty", std::nullopt, emptyBlock());
    const auto emptyName = empty->name;
    auto d = *p.addDungeon("The Burrow");
    std::tie(d->pixelX, d->pixelY) = f.freePixel(17, 109, 158);
    d->cells = {{0, 0, emptyName, true}};
    CHECK(hasIssue(checkDungeon(p, *d, f.world), Issue::Severity::Error, "no start marker"));
}

TEST_CASE("ARENA2: open sides and one-sided doorways are warnings") {
    DE_REQUIRE_ARENA2(a);
    Fixture f(*a);
    Project p;
    auto empty = *p.addBlock('N', "Empty", std::nullopt, emptyBlock());
    const auto emptyName = empty->name;
    auto d = *p.addDungeon("The Burrow");
    std::tie(d->pixelX, d->pixelY) = f.freePixel(17, 109, 158);
    d->cells = {{0, 0, "N0000071.RDB", true}, {1, 0, emptyName, false}};
    const auto issues = checkDungeon(p, *d, f.world);
    using S = Issue::Severity;
    CHECK(hasIssue(issues, S::Warning, "is open to the west"));
    CHECK(hasIssue(issues, S::Warning, "opens on one side only"));
    CHECK_FALSE(hasIssue(issues, S::Error, ""));
}

TEST_CASE("ARENA2: a project exports its blocks and places, and a re-export tidies up") {
    DE_REQUIRE_ARENA2(a);
    Fixture f(*a);
    Project p;
    const auto n71 = *a->blocks.findByName("N0000071.RDB");
    auto copied = *p.addBlock('N', "Hall", "N0000071.RDB", *de::formats::readClassicRdbBlock(a->blocks, n71));
    const auto copiedName = copied->name;
    auto scratch = *p.addBlock('N', "Scratch", std::nullopt, emptyBlock());
    const auto scratchName = scratch->name;
    auto d = *p.addDungeon("The Burrow");
    std::tie(d->pixelX, d->pixelY) = f.freePixel(17, 109, 158);
    plusShape(*d, copiedName);
    const int number = d->number;

    TempDir dir;
    auto report = exportProject(p, f.world, dir.path);
    REQUIRE_MESSAGE(report, (report ? "" : report.error().message));
    CHECK_FALSE(report->hasErrors());
    CHECK(report->written.size() == 3);

    // The copied block ships as a patch over N0000071 that rebuilds under its own name.
    std::ifstream patchFile(dir.path / "WorldDataPatches" / (copiedName + ".json"));
    REQUIRE(patchFile);
    const auto patch = de::exporter::Json::parse(patchFile);
    CHECK(patch["rebuilds"] == copiedName + ".json");
    CHECK(patch["base"]["block"] == "N0000071.RDB");
    auto rebuilt = de::exporter::rebuildBlockPatch(patch, a->blocks);
    REQUIRE(rebuilt);
    CHECK((*rebuilt)["Name"] == copiedName);
    CHECK(de::exporter::canonicalSha256(*rebuilt) == patch["sha256"].get<std::string>());
    // The empty one ships whole.
    CHECK(std::filesystem::exists(dir.path / "WorldData" / (scratchName + ".json")));

    // The place.
    const auto locationFile = dir.path / "WorldData" / locationFileName(*d);
    std::ifstream in(locationFile);
    REQUIRE(in);
    const auto loc = de::exporter::Json::parse(in);
    CHECK(loc["Name"] == "The Burrow");
    CHECK(loc["RegionName"] == "Daggerfall");
    CHECK(loc["Climate"]["WorldClimate"] != 223);   // the land's climate, never the sea's
    CHECK(loc["HasDungeon"] == true);
    CHECK(loc["MapTableData"]["MapId"] == d->mapId());
    CHECK(loc["Exterior"]["ExteriorData"]["BlockNames"][0] == "DUNGAA00.RMB");
    CHECK(loc["Dungeon"]["RecordElement"]["Header"]["ExteriorLocationId"] == d->exteriorLocationId());
    CHECK(loc["Dungeon"]["Blocks"].size() == 5);
    CHECK(loc["Exterior"]["RecordElement"]["Header"]["X"].get<int>() / 32768 == d->pixelX);
    CHECK(499 - loc["Exterior"]["RecordElement"]["Header"]["Y"].get<int>() / 32768 == d->pixelY);
    CHECK(locationFileName(*d).starts_with("locationnew-bunnyfall-"));
    CHECK(locationFileName(*d).ends_with("-17.json"));

    // Remove the dungeon and the scratch block: the next export takes their files away, and leaves others alone.
    std::ofstream(dir.path / "WorldDataPatches" / "S0000999.RDB.json") << "{}";
    p.removeDungeon(number);
    p.removeBlock(scratchName);
    report = exportProject(p, f.world, dir.path);
    REQUIRE(report);
    CHECK(report->removed.size() == 2);
    CHECK_FALSE(std::filesystem::exists(locationFile));
    CHECK(std::filesystem::exists(dir.path / "WorldDataPatches" / "S0000999.RDB.json"));
}

TEST_CASE("ARENA2: a project with errors writes nothing") {
    DE_REQUIRE_ARENA2(a);
    Fixture f(*a);
    Project p;
    auto d = *p.addDungeon("");
    d->pixelX = 500;
    d->pixelY = 250;
    TempDir dir;
    auto report = exportProject(p, f.world, dir.path);
    REQUIRE(report);
    CHECK(report->hasErrors());
    CHECK(report->written.empty());
    CHECK_FALSE(std::filesystem::exists(dir.path));
}
