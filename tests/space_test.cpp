#include "catalog/space.h"

#include <doctest/doctest.h>

using namespace de::catalog;

namespace {
bool near(Vec3 a, Vec3 b) {
    return std::abs(a.x - b.x) < 1e-9 && std::abs(a.y - b.y) < 1e-9 && std::abs(a.z - b.z) < 1e-9;
}
} // namespace

TEST_CASE("file rotation units: 512 is a quarter turn, the other way round") {
    CHECK(rotationDegrees(512) == doctest::Approx(-90));
    CHECK(rotationDegrees(-1024) == doctest::Approx(180));
    CHECK(rotationDegrees(2048) == doctest::Approx(-360));
}

TEST_CASE("file Y points down; editor Y points up") {
    CHECK(fromFilePosition(10, -1280, 30) == Vec3{10, 1280, 30});
    CHECK(fromMeshPoint(256, -512, 1024) == Vec3{1, 2, 4});
}

TEST_CASE("model rotation: a file yRotation of 512 turns +Z toward -X") {
    const auto r = rdbModelRotation(0, 512, 0);   // editor -90 degrees about Y
    CHECK(near(r * Vec3{0, 0, 1}, Vec3{-1, 0, 0}));
    CHECK(near(r * Vec3{1, 0, 0}, Vec3{0, 0, 1}));
    CHECK(near(r * Vec3{0, 1, 0}, Vec3{0, 1, 0}));
}

TEST_CASE("model rotation composes Rz * Rx * Ry") {
    // Ry first: (0,0,1) -> (-1,0,0); then Rx(-90) leaves it; then Rz(-90) turns -X to +Y.
    const auto r = rdbModelRotation(512, 512, 512);
    CHECK(near(r * Vec3{0, 0, 1}, Vec3{0, 1, 0}));
}

TEST_CASE("transformBox turns and moves a box, exactly for quarter turns") {
    const Box corridor{{-64, 0, -128}, {64, 128, 128}};   // model 67000, editor space
    const auto turned = transformBox(corridor, rdbModelRotation(0, 512, 0), Vec3{1000, 5, 2000});
    CHECK(turned.min == Vec3{872, 5, 1936});
    CHECK(turned.max == Vec3{1128, 133, 2064});
    const auto same = transformBox(corridor, rdbModelRotation(0, 0, 0), Vec3{});
    CHECK(same.min == corridor.min);
    CHECK(same.max == corridor.max);
}
