#include "arena2_fixture.h"
#include "core/sha256.h"
#include "core/utf8.h"
#include "export/world_data_patch.h"
#include "formats/rdb.h"

#include <doctest/doctest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <vector>

using namespace de::exporter;

namespace {
std::string decode(std::vector<std::uint8_t> bytes) { return de::decodeUtf8Lossy(bytes); }
} // namespace

TEST_CASE("sha256 known answers") {
    CHECK(de::sha256Hex("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    CHECK(de::sha256Hex("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CHECK(de::sha256Hex(std::string(1000, 'a')) == "41edece42d63e8d9bf515a9ba6932e1c20cbc9f5a5d134645adb5db1b9737ea3");
}

TEST_CASE("UTF-8 decoding replaces what a browser replaces") {
    CHECK(decode({'A', 'B'}) == "AB");
    CHECK(decode({0xFF, 0xFF, 0xFF}) == "\xEF\xBF\xBD\xEF\xBF\xBD\xEF\xBF\xBD");   // one U+FFFD per byte
    CHECK(decode({0xC3, 0xA9}) == "\xC3\xA9");                                       // a valid e-acute stays
    CHECK(decode({0xC3, 'A'}) == "\xEF\xBF\xBD" "A");                                // truncated: one, then A
    CHECK(decode({0xE0, 0x80}) == "\xEF\xBF\xBD\xEF\xBF\xBD");                       // overlong lead: two
    CHECK(decode({'x', 0xE2, 0x82}) == "x\xEF\xBF\xBD");                              // cut off at the end: one
}

TEST_CASE("text fields keep inner NULs and drop trailing ones, as Bunnyfall reads them") {
    const std::vector<std::uint8_t> bytes{'6', '7', 0, '0', 0, 0xFF, 0xFF, 0xFF};
    const de::ByteView v(bytes);
    CHECK(v.textField(0, 5) == std::string("67\0" "0", 4));
    CHECK(v.textField(5, 3) == de::formats::kUnusedModelDescription);
}

TEST_CASE("canonical JSON sorts keys and writes nothing extra") {
    const Json j = Json::parse(R"({"b": 1, "a": {"d": [3, {"z": null, "y": "\u00e9"}], "c": true}})");
    CHECK(canonicalJson(j) == "{\"a\":{\"c\":true,\"d\":[3,{\"y\":\"\xC3\xA9\",\"z\":null}]},\"b\":1}");
    CHECK(canonicalJson(Json("tab\there")) == "\"tab\\there\"");
    CHECK(canonicalJson(Json(std::string("\x01", 1))) == "\"\\u0001\"");
}

TEST_CASE("patch ops") {
    const Json base = Json::parse(R"({"a": [1, 2, 3], "o": {"k": 1}})");
    SUBCASE("set, insert, remove, delete in order") {
        const Json ops = Json::parse(R"([["s", ["a", 0], 9], ["i", ["a", 3], 4], ["r", ["a", 1]], ["d", ["o", "k"]],
                                         ["s", ["o", "n"], "new"]])");
        auto out = applyPatchOps(base, ops);
        REQUIRE(out);
        CHECK(*out == Json::parse(R"({"a": [9, 3, 4], "o": {"n": "new"}})"));
    }
    SUBCASE("the root") {
        auto out = applyPatchOps(base, Json::parse(R"([["s", [], {"x": 1}]])"));
        REQUIRE(out);
        CHECK(*out == Json::parse(R"({"x": 1})"));
    }
    SUBCASE("ops that do not land refuse the patch") {
        CHECK_FALSE(applyPatchOps(base, Json::parse(R"([["s", ["a", 3], 0]])")));
        CHECK_FALSE(applyPatchOps(base, Json::parse(R"([["r", ["a", 3]]])")));
        CHECK_FALSE(applyPatchOps(base, Json::parse(R"([["i", ["a", 4], 0]])")));
        CHECK_FALSE(applyPatchOps(base, Json::parse(R"([["d", ["o", "missing"]]])")));
        CHECK_FALSE(applyPatchOps(base, Json::parse(R"([["s", ["nope", "x"], 0]])")));
        CHECK_FALSE(applyPatchOps(base, Json::parse(R"([["sr", ["a", 0], [1]]])")));
        CHECK_FALSE(applyPatchOps(base, Json::parse(R"([["ci", ["a", 0], {"block": 1, "path": []}]])")));
    }
    SUBCASE("copy ops take their node from the source") {
        const CopySource source = [](const Json&) -> de::Result<Json> { return Json(42); };
        auto out = applyPatchOps(base, Json::parse(R"([["ci", ["a", 0], {}], ["cs", ["o", "k"], {}]])"), source);
        REQUIRE(out);
        CHECK(*out == Json::parse(R"({"a": [42, 1, 2, 3], "o": {"k": 42}})"));
    }
}

TEST_CASE("ARENA2: the classic repairs land where Daggerfall Unity puts them") {
    DE_REQUIRE_ARENA2(a);
    auto n71 = de::formats::readClassicRdbBlock(a->blocks, 994);
    REQUIRE(n71);
    CHECK(a->blocks.entry(994).name == "N0000071.RDB");
    CHECK(n71->roots[4].objects[18].x == 640);
    CHECK(n71->roots[4].objects[18].model()->modelIndex == 10);

    auto n22 = de::formats::readClassicRdbBlock(a->blocks, 945);
    REQUIRE(n22);
    CHECK(n22->roots[2].objects.empty());

    auto w9 = de::formats::readClassicRdbBlock(a->blocks, 1025);
    REQUIRE(w9);
    CHECK(a->blocks.entry(1025).name == "W0000009.RDB");
    const auto& added = w9->roots[8].objects.back();
    CHECK(added.position == 0);
    CHECK(w9->modelRefs[added.model()->modelIndex].modelIdNum == 72100);

    for (const std::size_t index : {945u, 946u, 958u, 975u, 994u, 1025u, 1034u, 1036u}) {
        INFO(a->blocks.entry(index).name);
        CHECK(de::formats::readClassicRdbBlock(a->blocks, index));
    }
}

TEST_CASE("ARENA2: classic block JSON keeps the used model slots and every object") {
    DE_REQUIRE_ARENA2(a);
    auto json = classicBlockJson(a->blocks, 994);
    REQUIRE(json);
    CHECK((*json)["Name"] == "N0000071.RDB");
    CHECK((*json)["Type"] == "Rdb");
    CHECK((*json)["Position"] == a->blocks.entry(994).offset);
    const auto& refs = (*json)["RdbBlock"]["ModelReferenceList"];
    CHECK(refs.size() > 10);
    CHECK(refs.size() < 750);
    auto block = de::formats::readClassicRdbBlock(a->blocks, 994);
    std::size_t objects = 0;
    for (const auto& root : (*json)["RdbBlock"]["ObjectRootList"]) {
        if (!root["RdbObjects"].is_null()) objects += root["RdbObjects"].size();
    }
    CHECK(objects == block->objectCount());
}

TEST_CASE("ARENA2 + Bunnyfall: Aquatic Sprites' patches rebuild to the author's sha256") {
    DE_REQUIRE_ARENA2(a);
    const char* bunnyfall = std::getenv("DE_BUNNYFALL");
    const auto dir = std::filesystem::path(bunnyfall ? bunnyfall : "../Bunnyfall") / "vendor/aquatic-sprites/WorldDataPatches";
    if (!std::filesystem::exists(dir)) {
        MESSAGE("skipped: set DE_BUNNYFALL to a Bunnyfall checkout");
        return;
    }
    int rebuilt = 0;
    for (const auto& file : std::filesystem::directory_iterator(dir)) {
        std::ifstream in(file.path());
        const Json patch = Json::parse(in);
        INFO(file.path().filename().string());
        auto json = rebuildBlockPatch(patch, a->blocks);
        REQUIRE_MESSAGE(json, (json ? "" : json.error().message));
        CHECK(canonicalSha256(*json) == patch["sha256"].get<std::string>());
        ++rebuilt;
    }
    CHECK(rebuilt == 3);
}

TEST_CASE("diff then patch gives back the target") {
    const std::vector<std::pair<const char*, const char*>> cases = {
        {R"({"a": 1})", R"({"a": 2})"},
        {R"({"a": 1, "b": 2})", R"({"b": 2, "c": 3})"},
        {R"([1, 2, 3, 4, 5])", R"([1, 3, 4, 9, 5, 6])"},
        {R"([1, 2, 3])", R"([3, 2, 1])"},
        {R"({"x": [{"k": 1}, {"k": 2}, {"k": 3}]})", R"({"x": [{"k": 1}, {"k": 20, "n": 1}, {"k": 3}, {"k": 4}]})"},
        {R"({"x": null})", R"({"x": [1, 2]})"},
        {R"([])", R"([{"a": 1}])"},
    };
    for (const auto& [from, to] : cases) {
        const auto a = Json::parse(from), b = Json::parse(to);
        CAPTURE(from);
        CAPTURE(to);
        const auto ops = diffJson(a, b);
        auto out = applyPatchOps(a, ops);
        REQUIRE(out);
        CHECK(*out == b);
    }
    CHECK(diffJson(Json::parse(R"({"a": [1, 2]})"), Json::parse(R"({"a": [1, 2]})")).empty());
}

TEST_CASE("ARENA2: an unedited block makes an empty patch; edits make patches that rebuild") {
    DE_REQUIRE_ARENA2(a);
    auto base = classicBlockJson(a->blocks, 994);
    REQUIRE(base);
    auto same = makeBlockPatch(a->blocks, 994, *base, "N0000071.RDB.json");
    REQUIRE(same);
    CHECK((*same)["ops"].empty());

    // Move a torch, drop an object, add a flat and rename the block: the patch carries only that.
    Json edited = *base;
    auto& roots = edited["RdbBlock"]["ObjectRootList"];
    std::size_t root = 0;
    while (roots[root]["RdbObjects"].is_null()) ++root;
    auto& objects = roots[root]["RdbObjects"];
    objects[0]["XPos"] = objects[0]["XPos"].get<int>() + 64;
    objects.erase(objects.begin() + 1);
    Json flat = objects[0];
    flat["Type"] = "Flat";
    flat["Resources"] = {{"FlatResource", {{"Position", 0}, {"TextureArchive", 210}, {"TextureRecord", 0},
                                           {"Flags", 0}, {"Magnitude", 0}, {"SoundIndex", 0},
                                           {"FactionOrMobileId", 0}, {"NextObjectOffset", -1}, {"Action", 0}}}};
    objects.push_back(flat);
    edited["Name"] = "N9000071.RDB";
    auto patch = makeBlockPatch(a->blocks, 994, edited, "N9000071.RDB.json");
    REQUIRE_MESSAGE(patch, (patch ? "" : patch.error().message));
    CHECK((*patch)["ops"].size() < 12);
    CHECK((*patch)["rebuilds"] == "N9000071.RDB.json");
    auto rebuilt = rebuildBlockPatch(*patch, a->blocks);
    REQUIRE(rebuilt);
    CHECK(*rebuilt == edited);
}

TEST_CASE("ARENA2 + Bunnyfall: our own patches of Aquatic Sprites' blocks rebuild the same files") {
    DE_REQUIRE_ARENA2(a);
    const char* bunnyfall = std::getenv("DE_BUNNYFALL");
    const auto dir = std::filesystem::path(bunnyfall ? bunnyfall : "../Bunnyfall") / "vendor/aquatic-sprites/WorldDataPatches";
    if (!std::filesystem::exists(dir)) {
        MESSAGE("skipped: set DE_BUNNYFALL to a Bunnyfall checkout");
        return;
    }
    for (const auto& file : std::filesystem::directory_iterator(dir)) {
        std::ifstream in(file.path());
        const Json theirs = Json::parse(in);
        INFO(file.path().filename().string());
        auto target = rebuildBlockPatch(theirs, a->blocks);
        REQUIRE(target);
        auto ours = makeBlockPatch(a->blocks, theirs["base"]["index"].get<std::size_t>(), *target,
                                   theirs["rebuilds"].get<std::string>());
        REQUIRE_MESSAGE(ours, (ours ? "" : ours.error().message));
        CHECK((*ours)["sha256"] == theirs["sha256"]);
        MESSAGE(file.path().filename().string() << ": theirs " << theirs["ops"].size() << " ops, ours "
                                                << (*ours)["ops"].size());
    }
}
