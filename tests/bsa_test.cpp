#include "bytes_builder.h"
#include "formats/bsa.h"
#include "formats/maps.h"
#include "formats/rdb.h"

#include <doctest/doctest.h>

using namespace de::formats;
using de::ByteView;
using de::test::Bytes;

namespace {

Bytes namedArchive() {
    Bytes b;
    b.i16(2).u16(0x0100);
    b.text("abc", 3).text("hello", 5);   // two records: 3 and 5 bytes
    b.text("FIRST.RDB", 14).i32(3);
    b.text("SECOND.RMB", 14).i32(5);
    return b;
}

} // namespace

TEST_CASE("BSA with a name directory") {
    auto archive = BsaArchive::fromBytes(namedArchive().data);
    REQUIRE(archive);
    CHECK(archive->directoryType() == BsaArchive::DirectoryType::Names);
    REQUIRE(archive->count() == 2);
    CHECK(archive->entry(0).name == "FIRST.RDB");
    CHECK(archive->entry(1).offset == 7);
    CHECK(archive->record(1).fixedString(0, 5) == "hello");
    CHECK(archive->findByName("SECOND.RMB") == 1);
    CHECK_FALSE(archive->findByName("MISSING"));
    CHECK_FALSE(archive->findById(0));
}

TEST_CASE("BSA with a number directory") {
    Bytes b;
    b.i16(1).u16(0x0200).text("xy", 2).u32(67000).i32(2);
    auto archive = BsaArchive::fromBytes(b.data);
    REQUIRE(archive);
    CHECK(archive->findById(67000) == 0);
    CHECK(archive->entry(0).name == "67000");
}

TEST_CASE("BSA errors instead of reading out of bounds") {
    SUBCASE("unknown directory type") {
        Bytes b;
        b.i16(0).u16(0x0300);
        CHECK_FALSE(BsaArchive::fromBytes(b.data));
    }
    SUBCASE("directory bigger than the file") {
        Bytes b;
        b.i16(100).u16(0x0100);
        CHECK_FALSE(BsaArchive::fromBytes(b.data));
    }
    SUBCASE("record runs into the directory") {
        auto b = namedArchive();
        b.patchI32(b.size() - 4, 50);
        CHECK_FALSE(BsaArchive::fromBytes(b.data));
    }
    SUBCASE("truncated header") {
        CHECK_FALSE(BsaArchive::fromBytes({0x01}));
    }
}

TEST_CASE("dungeon block names decode from the bitfield") {
    CHECK(dungeonBlockName(71) == "N0000071.RDB");
    CHECK(dungeonBlockName(static_cast<std::uint16_t>((4u << 11) | 13u)) == "B0000013.RDB");
    CHECK(dungeonBlockName(static_cast<std::uint16_t>((1u << 11) | 0x400u | 5u)) == "W0000005.RDB");   // start flag ignored
    CHECK_FALSE(dungeonBlockName(static_cast<std::uint16_t>(6u << 11)));
}

TEST_CASE("block roles come from the first letter") {
    CHECK(rdbRoleFromName("B0000001.RDB") == RdbRole::Border);
    CHECK(rdbRoleFromName("N0000071.RDB") == RdbRole::Normal);
    CHECK(rdbRoleFromName("W0000009.RDB") == RdbRole::Wet);
    CHECK(rdbRoleFromName("S0000999.RDB") == RdbRole::Quest);
    CHECK(rdbRoleFromName("M0000000.RDB") == RdbRole::Mausoleum);
    CHECK(rdbRoleFromName("X.RDB") == RdbRole::Unknown);
    CHECK(rdbRoleFromName("") == RdbRole::Unknown);
}

namespace {

/// A 1x1 RDB block: two models in one root, the first's action pointing at the second, then a flat.
Bytes tinyRdb() {
    constexpr std::uint32_t rootOffset = 20 + 750 * 8 + 750 * 4 + 512;
    Bytes b;
    b.u32(0).u32(1).u32(1).u32(rootOffset).u32(0);
    b.text("67000", 5).text("C00", 3);
    b.text("55005", 5).text("DOR", 3);
    b.zeros(748 * 8).zeros(750 * 4).zeros(512);
    REQUIRE(b.size() == rootOffset);
    b.i32(static_cast<std::int32_t>(rootOffset + 4));   // the one root

    const std::uint32_t obj0 = rootOffset + 4, obj1 = obj0 + 25, obj2 = obj1 + 25;
    const std::uint32_t res0 = obj2 + 25, act0 = res0 + 23, res1 = act0 + 10, res2 = res1 + 23;
    // object header: next, previous, x, y, z, type, resource offset
    b.i32(static_cast<std::int32_t>(obj1)).i32(-1).i32(128).i32(-256).i32(64).u8(1).u32(res0);
    b.i32(static_cast<std::int32_t>(obj2)).i32(static_cast<std::int32_t>(obj0)).i32(0).i32(0).i32(0).u8(1).u32(res1);
    b.i32(-1).i32(static_cast<std::int32_t>(obj1)).i32(10).i32(20).i32(30).u8(3).u32(res2);
    // model 0: rotations, model index 0, trigger/lock, sound, action offset
    b.i32(0).i32(512).i32(0).u16(0).u32(0x02).u8(0).i32(static_cast<std::int32_t>(act0));
    // its action: axis, duration, magnitude, next object, flags (Rotation)
    b.u8(4).u16(40).u16(512).i32(static_cast<std::int32_t>(obj1)).u8(0x08);
    // model 1: a door, no action
    b.i32(0).i32(-1024).i32(0).u16(1).u32(0x30).u8(0).i32(0);
    // flat: 199/16 fixed enemy, magnitude 7 + sound 1 -> mobile id 263
    b.u16(static_cast<std::uint16_t>((199u << 7) | 16u)).u16(2).u8(7).u8(1).i32(-1).u8(99);
    return b;
}

} // namespace

TEST_CASE("RDB block: objects, resources and action links") {
    const auto bytes = tinyRdb();
    auto block = parseRdbBlock(ByteView(bytes.data));
    REQUIRE(block);
    CHECK(block->modelRefs.size() == kRdbModelRefSlots);
    CHECK(block->modelRefs[0].modelIdNum == 67000);
    CHECK(block->modelRefs[1].description == "DOR");
    CHECK(block->modelRefs[2].modelId.empty());
    REQUIRE(block->roots.size() == 1);
    const auto& objs = block->roots[0].objects;
    REQUIRE(objs.size() == 3);

    const auto* first = objs[0].model();
    REQUIRE(first);
    CHECK(objs[0].x == 128);
    CHECK(objs[0].y == -256);
    CHECK(first->yRotation == 512);
    CHECK(first->hasAction());
    CHECK(first->action.flags == 0x08);
    CHECK(first->action.duration == 40);
    CHECK(first->action.nextObjectOffset == objs[1].position);
    CHECK(first->action.nextObjectIndex == 1);

    const auto* door = objs[1].model();
    REQUIRE(door);
    CHECK_FALSE(door->hasAction());
    const auto startingLock = door->triggerFlagStartingLock >> 4;
    CHECK(startingLock == 3);
    CHECK(door->action.previousObjectOffset == objs[0].position);   // linked from the first model

    const auto* flat = objs[2].flat();
    REQUIRE(flat);
    CHECK(flat->textureArchive() == 199);
    CHECK(flat->textureRecord() == 16);
    CHECK(flat->factionOrMobileId() == 263);
    CHECK(flat->action == 99);
}

TEST_CASE("RDB block: malformed input fails cleanly") {
    SUBCASE("truncated") {
        auto bytes = tinyRdb();
        bytes.data.resize(bytes.size() - 30);
        CHECK_FALSE(parseRdbBlock(ByteView(bytes.data)));
    }
    SUBCASE("wrong root offset") {
        auto bytes = tinyRdb();
        bytes.patchU32(12, 1234);
        CHECK_FALSE(parseRdbBlock(ByteView(bytes.data)));
    }
    SUBCASE("object chain that loops") {
        auto bytes = tinyRdb();
        constexpr std::size_t rootOffset = 20 + 750 * 8 + 750 * 4 + 512;
        constexpr std::size_t obj2 = rootOffset + 4 + 50;
        bytes.patchI32(obj2, static_cast<std::int32_t>(rootOffset + 4));   // last object points back at the first
        auto block = parseRdbBlock(ByteView(bytes.data));
        REQUIRE_FALSE(block);
        CHECK(block.error().message.find("loops") != std::string::npos);
    }
}
