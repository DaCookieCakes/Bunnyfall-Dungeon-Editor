#include "arena2_fixture.h"
#include "catalog/catalog.h"
#include "edit/vertex_snap.h"

#include <doctest/doctest.h>

#include <cmath>

using namespace de::catalog;
using de::edit::openRims;
using de::edit::placedRim;
using de::edit::Rim;
using de::edit::rimShift;

namespace {

/// A corridor 256 long (x), 50 wide (z), 128 high, open at both ends: floor, ceiling and two side walls.
ModelInfo corridor() {
    ModelInfo m;
    m.points = {{-128, 0, -25}, {128, 0, -25}, {128, 0, 25}, {-128, 0, 25},
                {-128, 128, -25}, {128, 128, -25}, {128, 128, 25}, {-128, 128, 25}};
    m.faces = {{0, 1, 2, 3}, {4, 5, 6, 7}, {0, 1, 5, 4}, {3, 2, 6, 7}};
    return m;
}

const Rim* rimAt(const std::vector<Rim>& rims, double x) {
    for (const auto& r : rims)
        if (std::abs(r.centre.x - x) < 1e-6) return &r;
    return nullptr;
}

} // namespace

TEST_CASE("an open corridor has a rim at each end") {
    const auto m = corridor();
    const auto rims = openRims(m.points, m.faces);
    REQUIRE(rims.size() == 2);
    for (const auto& r : rims) CHECK(r.points.size() == 4);
    CHECK(rimAt(rims, -128));
    CHECK(rimAt(rims, 128));
}

TEST_CASE("a closed box has no rims, and points at the same place are one") {
    // A cube whose faces each have their own copies of the corners.
    std::vector<Vec3> points;
    std::vector<std::vector<std::uint32_t>> faces;
    const auto face = [&](std::initializer_list<Vec3> corners) {
        std::vector<std::uint32_t> f;
        for (const auto& c : corners) {
            f.push_back(static_cast<std::uint32_t>(points.size()));
            points.push_back(c);
        }
        faces.push_back(f);
    };
    face({{0, 0, 0}, {1, 0, 0}, {1, 0, 1}, {0, 0, 1}});
    face({{0, 1, 0}, {1, 1, 0}, {1, 1, 1}, {0, 1, 1}});
    face({{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}});
    face({{0, 0, 1}, {1, 0, 1}, {1, 1, 1}, {0, 1, 1}});
    face({{0, 0, 0}, {0, 0, 1}, {0, 1, 1}, {0, 1, 0}});
    face({{1, 0, 0}, {1, 0, 1}, {1, 1, 1}, {1, 1, 0}});
    CHECK(openRims(points, faces).empty());
}

TEST_CASE("two corridors meet end to end, turned or not") {
    const auto m = corridor();
    const auto rims = openRims(m.points, m.faces);
    const auto& east = *rimAt(rims, 128);
    const auto& west = *rimAt(rims, -128);

    // One placed at 1000; the moving one's west end goes onto its east end.
    const auto fixed = placedRim(east, Mat3{}, {1000, 0, 0});
    const auto moving = placedRim(west, Mat3{}, {1200, 3, 40});
    const auto shift = rimShift(moving, fixed);
    REQUIRE(shift);
    CHECK(shift->x == doctest::Approx(56));
    CHECK(shift->y == doctest::Approx(-3));
    CHECK(shift->z == doctest::Approx(-40));

    // Turned half way round, its east end now faces west and meets as well.
    const auto halfTurn = rdbModelRotation(0, 1024, 0);
    CHECK(rimShift(placedRim(east, halfTurn, {0, 0, 0}), fixed));
    // A quarter turn puts the rim across the other way: it does not meet.
    CHECK_FALSE(rimShift(placedRim(west, rdbModelRotation(0, 512, 0), {0, 0, 0}), fixed));
}

TEST_CASE("rims of different shapes do not meet") {
    const auto m = corridor();
    auto wide = m;
    for (auto& p : wide.points) p.z *= 2;
    const auto a = openRims(m.points, m.faces), b = openRims(wide.points, wide.faces);
    CHECK_FALSE(rimShift(a[0], b[0]));
    CHECK_FALSE(rimShift(a[0], b[1]));
}

TEST_CASE("classic corridors and cave pieces have rims that meet [data]") {
    DE_REQUIRE_ARENA2(a);
    auto catalog = Catalog::build(*a);
    REQUIRE(catalog);
    const auto* corridor67000 = catalog->model(67000);
    REQUIRE(corridor67000);
    const auto rims = openRims(corridor67000->points, corridor67000->faces);
    REQUIRE(rims.size() >= 2);
    // Two copies of the corridor laid end to end along its length meet at some pair of ends.
    const auto size = corridor67000->bounds.size();
    const bool alongX = size.x >= size.z;
    const Vec3 step = alongX ? Vec3{size.x, 0, 0} : Vec3{0, 0, size.z};
    int meetings = 0;
    for (const auto& r : rims)
        for (const auto& s : rims)
            if (auto shift = rimShift(placedRim(r, Mat3{}, step), s)) meetings += std::abs(shift->x) + std::abs(shift->z) < 1;
    CHECK(meetings >= 1);

    // How much of the classic architecture has rims at all (printed, for a feel of the data).
    int pieces = 0, open = 0;
    for (const auto& [id, info] : catalog->models()) {
        if (info.kind != ModelKind::Architecture || info.uses == 0) continue;
        ++pieces;
        open += openRims(info.points, info.faces).empty() ? 0 : 1;
    }
    MESSAGE(open << " of " << pieces << " classic architecture pieces have open rims");
    CHECK(open > pieces / 2);
}

