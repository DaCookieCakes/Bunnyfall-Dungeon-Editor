#include "arena2_fixture.h"
#include "formats/classic_random.h"
#include "formats/maps.h"
#include "scene/texture_table.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <set>

using de::formats::ClassicRandom;
using de::scene::TextureTable;

TEST_CASE("the classic generator is the C runtime's LCG") {
    ClassicRandom r(7);
    CHECK(r.next() == 19564);
    CHECK(r.next() == 9806);
    CHECK(r.next() == 10868);
    ClassicRandom zero(0);
    CHECK(zero.next() == 0);   // 12345 >> 16
    ClassicRandom range(7);
    CHECK(range.rangeInclusive(0, 4) == 19564 % 5);
}

TEST_CASE("texture table: only the six base archives and the exit door change") {
    TextureTable t;
    t.archives = {19, 20, 22, 23, 24, 368};
    t.climateBase = 300;
    CHECK(t.apply(119) == 19);
    CHECK(t.apply(124) == 24);
    CHECK(t.apply(168) == 368);
    CHECK(t.apply(74) == 374);
    CHECK(t.apply(121) == 121);
    CHECK(t.apply(210) == 210);
    CHECK(TextureTable{}.apply(119) == 119);   // the base table changes nothing
}

TEST_CASE("classic tables draw from the climate's set and skip offset 2") {
    for (int climate = 223; climate <= 232; ++climate) {
        for (const std::uint16_t id : {std::uint16_t{0}, std::uint16_t{1}, std::uint16_t{50027}, std::uint16_t{65535}}) {
            const auto t = TextureTable::classic(id, climate);
            CAPTURE(climate);
            CAPTURE(id);
            const int base = t.archives[0] - (t.archives[0] % 100 >= 19 ? t.archives[0] % 100 - 19 : 0);
            for (std::size_t i = 0; i < 5; ++i) {
                const int offset = t.archives[i] - base;
                CHECK((offset == 0 || offset == 1 || offset == 3 || offset == 4));
            }
            CHECK(t.archives[5] % 100 == 68);
        }
    }
    // Deterministic per location id.
    CHECK(TextureTable::classic(50027, 231).archives == TextureTable::classic(50027, 231).archives);
}

TEST_CASE("classic tables: climate sets and sewers") {
    // Woodlands use the 19.. set in the classic algorithm, and the temperate sewer.
    const auto woods = TextureTable::classic(1, 231);
    CHECK(woods.archives[0] >= 19);
    CHECK(woods.archives[0] <= 23);
    CHECK(woods.archives[5] == 368);
    CHECK(woods.climateBase == 300);
    // Desert also builds from the 19.. set in the classic algorithm, with the desert sewer.
    const auto desert = TextureTable::classic(1, 224);
    CHECK(desert.archives[0] >= 19);
    CHECK(desert.archives[0] <= 23);
    CHECK(desert.archives[5] == 68);
    CHECK(desert.climateBase == 0);
    // Swamps use the 119.. set (classic never uses its 319.. and 419.. sets).
    const auto swamp = TextureTable::classic(1, 228);
    CHECK(swamp.archives[0] >= 119);
    CHECK(swamp.archives[0] <= 123);
    CHECK(swamp.archives[5] == 468);
    // Ocean reads as swamp.
    CHECK(TextureTable::classic(1, 223).archives == TextureTable::classic(1, 228).archives);
}

TEST_CASE("ARENA2: every dungeon knows its location, type and climate") {
    DE_REQUIRE_ARENA2(a);
    auto dungeons = de::formats::readDungeons(a->maps);
    REQUIRE(dungeons);
    for (const auto& d : *dungeons) {
        INFO(d.name);
        REQUIRE(d.locationIndex >= 0);
        CHECK(d.map.dungeonType < 19);
        const auto [x, y] = de::formats::mapPixel(d.map.longitude, d.map.latitude);
        const int climate = a->climate.at(x, y);
        CHECK(climate >= 223);
        CHECK(climate <= 232);
    }
    const auto castle = std::ranges::find_if(*dungeons, [](const auto& d) { return d.locationId == 50027; });
    REQUIRE(castle != dungeons->end());
    CHECK(castle->map.mapId == 1291010263);   // Daggerfall Unity's main-story id for Castle Daggerfall
}
