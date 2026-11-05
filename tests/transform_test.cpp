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

TEST_CASE("Daggerfall's three model angles come back out of their rotation") {
    for (const std::int32_t x : {0, 64, 300, 1700, 2047}) {
        for (const std::int32_t y : {0, 128, 512, 1024, 1999}) {
            for (const std::int32_t z : {0, 256, 777, 1536}) {
                const auto back = de::catalog::rdbRotationOf(de::catalog::rdbModelRotation(x, y, z));
                const auto again = de::catalog::rdbModelRotation(back[0], back[1], back[2]);
                const auto want = de::catalog::rdbModelRotation(x, y, z);
                for (std::size_t i = 0; i < 9; ++i) CHECK(again.m[i] == doctest::Approx(want.m[i]).epsilon(0.005));
                CHECK(back == std::array{x, y, z});
            }
        }
    }
    // Gimbal lock (X a quarter turn): the same rotation, with Y taken as 0.
    const auto locked = de::catalog::rdbRotationOf(de::catalog::rdbModelRotation(512, 300, 100));
    CHECK(locked[0] == 512);
    CHECK(locked[1] == 0);
    const auto again = de::catalog::rdbModelRotation(locked[0], locked[1], locked[2]);
    const auto want = de::catalog::rdbModelRotation(512, 300, 100);
    for (std::size_t i = 0; i < 9; ++i) CHECK(again.m[i] == doctest::Approx(want.m[i]).epsilon(0.005));
}

TEST_CASE("turning about any axis: about Y it is the level turn; about X a model stands on its side") {
    de::formats::RdbObject obj;
    obj.x = 1100;
    obj.y = -200;
    obj.z = 1000;
    de::formats::RdbModel m;
    m.yRotation = 256;
    obj.resource = m;
    const std::array<double, 3> pivot{1000, -200, 1000};
    const auto level = de::edit::transformed(obj, {0, 0, 0}, 512, 1000, 1000);
    const auto about = de::edit::turnedAbout(obj, 1, 512, pivot);
    CHECK(about.x == level.x);
    CHECK(about.z == level.z);
    CHECK(about.model()->yRotation == level.model()->yRotation);
    CHECK(about.model()->xRotation == 0);
    CHECK(about.model()->zRotation == 0);

    // A quarter turn about X: the model's up (+Y) goes to +Z, and a point above the pivot goes north of it.
    de::formats::RdbObject up;
    up.x = 1000;
    up.y = -300;   // 100 above the pivot (file Y points down)
    up.z = 1000;
    up.resource = de::formats::RdbModel{};
    const auto stood = de::edit::turnedAbout(up, 0, 512, pivot);
    CHECK(stood.y == -200);
    CHECK(stood.z == 1100);
    const auto* sm = stood.model();
    const auto r = de::catalog::rdbModelRotation(sm->xRotation, sm->yRotation, sm->zRotation);
    const auto modelUp = r * de::catalog::Vec3{0, 1, 0};
    CHECK(modelUp.z == doctest::Approx(1));
    // Four quarter turns about Z come back where they began.
    auto spun = obj;
    for (int i = 0; i < 4; ++i) spun = de::edit::turnedAbout(spun, 2, 512, pivot);
    CHECK(spun.x == obj.x);
    CHECK(spun.y == obj.y);
    CHECK(spun.model()->yRotation == 256);
    CHECK(spun.model()->xRotation == 0);
    CHECK(spun.model()->zRotation == 0);
}
