#include "arena2_fixture.h"
#include "export/world_data_patch.h"
#include "formats/climate.h"
#include "project/world_export.h"

#include <doctest/doctest.h>

#include <algorithm>
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
    CHECK(report->written.size() == 5);   // two blocks, the place, the mod's manifest and its README
    {
        std::ifstream manifestFile(dir.path / std::string(kModManifestName));
        REQUIRE(manifestFile);
        const auto manifest = de::exporter::Json::parse(manifestFile);
        CHECK(manifest["format"] == kModManifestFormat);
        CHECK(manifest["title"] == p.name);
        CHECK(manifest["idRange"] == 0);
        CHECK(std::filesystem::exists(dir.path / "README.md"));
    }

    // The copied block ships as a patch over N0000071 that rebuilds under its own name.
    std::ifstream patchFile(dir.path / "WorldDataPatches" / (copiedName + ".json"));
    REQUIRE(patchFile);
    const auto patch = de::exporter::Json::parse(patchFile);
    patchFile.close();   // Windows will not replace or delete a file that is open
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
    in.close();
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

TEST_CASE("a mod folder left with only empty folders is still the editor's to save into") {
    TempDir dir;
    const auto mod = dir.path / "vendor" / "my-mod";
    std::filesystem::create_directories(mod / "Models");
    std::filesystem::create_directories(mod / "WorldDataPatches");
    Project p;
    auto saved = ensureModManifest(p, mod);
    REQUIRE_MESSAGE(saved, (saved ? "" : saved.error().message));
    CHECK(std::filesystem::exists(mod / std::string(kModManifestName)));

    // Files it did not write still keep it out, whichever folder they are in.
    const auto other = dir.path / "vendor" / "another-mod";
    std::filesystem::create_directories(other / "Models");
    std::ofstream(other / "Models" / "theirs.json") << "{}";
    CHECK_FALSE(ensureModManifest(p, other));
    CHECK_FALSE(std::filesystem::exists(other / std::string(kModManifestName)));
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

TEST_CASE("mod folders: a classic block saved before the first export still makes a mod") {
    TempDir dir;
    const auto vendor = dir.path / "vendor";
    Project p;
    p.name = "Dungeons PR";
    p.mod.folder = "dungeons-pr";

    // A patch saved with no manifest beside it, as the editor once left them: the export must not call it another mod's.
    const auto orphan = vendor / "dungeons-pr";
    std::filesystem::create_directories(orphan / "WorldDataPatches");
    std::ofstream(orphan / "WorldDataPatches" / "S0000999.RDB.json") << "{}";
    CHECK(checkModFolder(p, orphan).empty());
    REQUIRE(ensureModManifest(p, orphan));
    {
        std::ifstream in(orphan / std::string(kModManifestName));
        REQUIRE(in);
        const auto manifest = de::exporter::Json::parse(in);
        CHECK(manifest["format"] == kModManifestFormat);
        CHECK(manifest["title"] == "Dungeons PR");
    }

    // A folder that does not exist yet is made; a manifest already there is left alone.
    REQUIRE(ensureModManifest(p, vendor / "fresh"));
    CHECK(std::filesystem::exists(vendor / "fresh" / std::string(kModManifestName)));
    std::ofstream(vendor / "fresh" / std::string(kModManifestName)) << "{\"kept\":true}";
    REQUIRE(ensureModManifest(p, vendor / "fresh"));
    {
        std::ifstream in(vendor / "fresh" / std::string(kModManifestName));
        CHECK(de::exporter::Json::parse(in).contains("kept"));
    }

    // Another mod's patches, with its own files beside them, are never written into.
    const auto other = vendor / "aquatic-sprites";
    std::filesystem::create_directories(other / "WorldDataPatches");
    std::ofstream(other / "aquatic-sprites.dfmod.json") << "{}";
    CHECK_FALSE(ensureModManifest(p, other));
    CHECK_FALSE(std::filesystem::exists(other / std::string(kModManifestName)));
}

TEST_CASE("mod folders: another mod's folder, a shared id range and a shared block are found") {
    TempDir dir;
    const auto vendor = dir.path / "vendor";
    const auto writeManifest = [&](const std::string& folder, int range) {
        std::filesystem::create_directories(vendor / folder / "WorldDataPatches");
        std::ofstream(vendor / folder / std::string(kModManifestName))
            << de::exporter::Json{{"format", kModManifestFormat}, {"title", folder}, {"idRange", range}}.dump();
    };
    writeManifest("crypts-of-hope", 0);
    std::ofstream(vendor / "crypts-of-hope" / "WorldDataPatches" / "N0000071.RDB.json") << "{}";
    std::filesystem::create_directories(vendor / "aquatic-sprites" / "WorldData");
    std::ofstream(vendor / "aquatic-sprites" / "WorldData" / "X.json") << "{}";

    const auto mods = editorModsIn(vendor);
    REQUIRE(mods.size() == 1);
    CHECK(mods[0].folder == "crypts-of-hope");
    CHECK(freeIdRange(vendor, "mine") == 1);
    CHECK(freeIdRange(vendor, "crypts-of-hope") == 0);   // its own range is free to itself

    Project p;
    p.mod.folder = "mine";
    const auto hasError = [](const std::vector<Issue>& issues, std::string_view text) {
        return std::ranges::any_of(issues, [&](const Issue& i) { return i.severity == Issue::Severity::Error && i.text.find(text) != std::string::npos; });
    };
    // Range 0 is the other mod's.
    CHECK(hasError(checkModFolder(p, vendor / "mine"), "uses id range 0 too"));
    REQUIRE(p.setIdRange(1));
    CHECK(checkModFolder(p, vendor / "mine").empty());
    // A folder the editor did not make is never written into.
    CHECK(hasError(checkModFolder(p, vendor / "aquatic-sprites"), "did not write"));
    // Nor a name Bunnyfall cannot take for a mod id.
    CHECK(hasError(checkModFolder(p, vendor / "My Mod"), "a-z, 0-9"));
    // A classic block both mods patch: a warning (once "mine" is an editor mod with a range of its own).
    writeManifest("mine", 1);
    std::ofstream(vendor / "mine" / "WorldDataPatches" / "N0000071.RDB.json") << "{}";
    const auto issues = checkModFolder(p, vendor / "mine");
    REQUIRE(issues.size() == 1);
    CHECK(issues[0].severity == Issue::Severity::Warning);
    CHECK(issues[0].text.find("N0000071.RDB.json") != std::string::npos);
}
