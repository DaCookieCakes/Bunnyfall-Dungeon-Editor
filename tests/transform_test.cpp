#include "edit/transform.h"

#include "catalog/space.h"

#include <doctest/doctest.h>

using namespace de;

namespace {

formats::RdbObject modelAt(std::int32_t x, std::int32_t z, std::int32_t yRotation = 0) {
    formats::RdbObject o;
    o.x = x;
    o.z = z;
    formats::RdbModel m;
    m.yRotation = yRotation;
    o.resource = m;
    return o;
}

} // namespace

TEST_CASE("transform: a quarter turn clockwise takes north to east, for positions and models alike") {
    // A model 100 units north of the pivot, facing north.
    const auto turned = edit::transformed(modelAt(0, 100), {0, 0, 0}, 512, 0, 0);
    CHECK(turned.x == 100);
    CHECK(turned.z == 0);
    // Its own north now points east too, the way its position went.
    const auto& m = std::get<formats::RdbModel>(turned.resource);
    const auto facing = catalog::rdbModelRotation(0, m.yRotation, 0) * catalog::Vec3{0, 0, 1};
    CHECK(facing.x == doctest::Approx(1));
    CHECK(facing.z == doctest::Approx(0).epsilon(1e-9));
}

TEST_CASE("transform: offset applies after the turn; flats only move") {
    formats::RdbObject flat;
    flat.x = 10;
    flat.z = 0;
    flat.resource = formats::RdbFlat{};
    const auto t = edit::transformed(flat, {5, -3, 7}, 1024, 0, 0);   // half a turn
    CHECK(t.x == -10 + 5);
    CHECK(t.y == -3);
    CHECK(t.z == 7);
    // A full turn comes back exactly.
    const auto round = edit::transformed(modelAt(37, -91, 300), {0, 0, 0}, 2048, 5, 5);
    CHECK(round.x == 37);
    CHECK(round.z == -91);
    CHECK(std::get<formats::RdbModel>(round.resource).yRotation == 300);
}

TEST_CASE("transform: forgetting a removed object shifts later ones in its root only") {
    std::vector<edit::ObjectSlot> slots{{0, 1}, {0, 3}, {0, 5}, {1, 4}};
    edit::forgetRemoved(slots, {0, 3});
    REQUIRE(slots.size() == 3);
    CHECK(slots[0] == edit::ObjectSlot{0, 1});
    CHECK(slots[1] == edit::ObjectSlot{0, 4});
    CHECK(slots[2] == edit::ObjectSlot{1, 4});
}
