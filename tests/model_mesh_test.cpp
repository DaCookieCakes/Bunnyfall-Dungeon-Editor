#include "arena2_fixture.h"
#include "catalog/catalog.h"
#include "geometry/model_mesh.h"

#include <doctest/doctest.h>

#include <cmath>

using namespace de::geometry;
using de::formats::Arch3dMesh;
using de::formats::Arch3dPlane;

TEST_CASE("UV unpacking leaves ordinary values alone") {
    CHECK(unpackUv(0) == 0);
    CHECK(unpackUv(1000) == 1000);
    CHECK(unpackUv(-14335) == -14335);
    CHECK(unpackUv(14336) == -2048);    // nearest multiple of 8192 is 16384
    CHECK(unpackUv(-7168) == 1024);     // the one in-range value that is packed
    CHECK(unpackUv(20000) == 3616);
}

namespace {
Arch3dMesh square(std::uint32_t id) {
    // A 256 x 256 square (one block unit) at file y = 0; three faces would be a triangle, this is four points.
    Arch3dMesh m;
    m.id = id;
    m.points = {{0, 0, 0}, {256, 0, 0}, {256, 0, 256}, {0, 0, 256}};
    Arch3dPlane p;
    p.texture = static_cast<std::uint16_t>((120u << 7) | 3u);
    p.ny = -256;   // file Y is down: this faces up in editor space
    // UVs: first absolute, then deltas: (0,0), (+64,0), (0,+64)
    p.vertices = {{0, 0, 0}, {1, 64, 0}, {2, 0, 64}, {3, 0, 0}};
    m.planes.push_back(p);
    return m;
}
} // namespace

TEST_CASE("a triangle face takes its UVs as running deltas") {
    Arch3dMesh m;
    m.id = 1000;   // >= 905: no unpacking
    m.points = {{0, 0, 0}, {256, 0, 0}, {256, 0, 256}};
    Arch3dPlane p;
    p.texture = static_cast<std::uint16_t>(120u << 7);
    p.vertices = {{0, 16, 0}, {1, 16, 0}, {2, 0, 32}};
    m.planes.push_back(p);
    auto mesh = buildModelMesh(m);
    REQUIRE(mesh);
    REQUIRE(mesh->vertices.size() == 3);
    CHECK(mesh->vertices[0].u == 1.0f);
    CHECK(mesh->vertices[1].u == 2.0f);
    CHECK(mesh->vertices[2].u == 2.0f);
    CHECK(mesh->vertices[2].v == 2.0f);
    CHECK(mesh->indices == std::vector<std::uint32_t>{0, 2, 1});
}

TEST_CASE("a larger face gets the fourth corner's UV from the fitted matrix") {
    auto mesh = buildModelMesh(square(1000));
    REQUIRE(mesh);
    REQUIRE(mesh->vertices.size() == 4);
    REQUIRE(mesh->subMeshes.size() == 1);
    CHECK(mesh->subMeshes[0].textureArchive == 120);
    CHECK(mesh->subMeshes[0].textureRecord == 3);
    CHECK(mesh->subMeshes[0].indexCount == 6);
    // Corners come out starting at point 1 (the reference's corner order), so look the UVs up by position.
    for (const auto& v : mesh->vertices) {
        const auto& p = v.position;
        CAPTURE(p.x);
        CAPTURE(p.z);
        CHECK(v.u == doctest::Approx(p.x == 0 ? 0.0 : 4.0));
        CHECK(v.v == doctest::Approx(p.z == 0 ? 0.0 : 4.0));
        CHECK(v.normal.y == doctest::Approx(1.0));
        CHECK(p.y == 0.0);
    }
}

TEST_CASE("positions are in editor space: block units, Y up") {
    Arch3dMesh m = square(1000);
    for (auto& p : m.points) p.y = -512;   // two block units up
    auto mesh = buildModelMesh(m);
    REQUIRE(mesh);
    for (const auto& v : mesh->vertices) CHECK(v.position.y == 2.0);
}

TEST_CASE("a face with a repeated point fails the model, as in the reference") {
    Arch3dMesh m = square(1000);
    m.planes[0].vertices[1].point = 0;
    CHECK_FALSE(buildModelMesh(m));
}

TEST_CASE("ARENA2: every model a dungeon block uses builds a mesh") {
    DE_REQUIRE_ARENA2(a);
    auto catalog = de::catalog::Catalog::build(*a);
    REQUIRE(catalog);
    std::size_t triangles = 0;
    for (const auto& [id, info] : catalog->models()) {
        INFO("model " << id);
        auto raw = de::formats::parseArch3dMesh(a->arch3d.record(*a->arch3d.findById(id)), id);
        REQUIRE(raw);
        auto mesh = buildModelMesh(*raw);
        REQUIRE_MESSAGE(mesh, (mesh ? "" : mesh.error().message));
        CHECK_FALSE(mesh->indices.empty());
        for (const auto& v : mesh->vertices) {
            REQUIRE(std::isfinite(v.u));
            REQUIRE(std::isfinite(v.v));
        }
        triangles += mesh->indices.size() / 3;
    }
    MESSAGE(triangles << " triangles over " << catalog->models().size() << " models");

    // The corridor: 4 faces (floor, ceiling, two walls), each a quad.
    auto raw = de::formats::parseArch3dMesh(a->arch3d.record(*a->arch3d.findById(67000)), 67000);
    auto corridor = buildModelMesh(*raw);
    REQUIRE(corridor);
    CHECK(corridor->indices.size() == 4 * 2 * 3);
}
