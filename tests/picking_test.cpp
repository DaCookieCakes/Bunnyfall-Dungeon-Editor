#include "scene/picking.h"

#include <doctest/doctest.h>

using namespace de::scene;
using de::catalog::Vec3;

namespace {

/// A one-triangle mesh in the XY plane at z = 0.
de::geometry::ModelMesh triangle() {
    de::geometry::ModelMesh m;
    m.modelId = 1;
    m.vertices = {{{-10, -10, 0}, {0, 0, 1}, 0, 0}, {{10, -10, 0}, {0, 0, 1}, 0, 0}, {{0, 10, 0}, {0, 0, 1}, 0, 0}};
    m.indices = {0, 1, 2};
    m.subMeshes = {{0, 0, 0, 3}};
    return m;
}

Scene sceneWith(std::vector<ModelInstance> models, std::vector<FlatInstance> flats = {}) {
    Scene s;
    s.models = std::move(models);
    s.flats = std::move(flats);
    return s;
}

const auto kSize = [](int, int) { return std::pair{8.0, 8.0}; };

} // namespace

TEST_CASE("ray-triangle: hits from either side, misses outside and behind") {
    const Vec3 a{-10, -10, 0}, b{10, -10, 0}, c{0, 10, 0};
    CHECK(intersectTriangle({{0, 0, -5}, {0, 0, 1}}, a, b, c) == doctest::Approx(5));
    CHECK(intersectTriangle({{0, 0, 5}, {0, 0, -1}}, a, b, c) == doctest::Approx(5));
    CHECK_FALSE(intersectTriangle({{50, 0, -5}, {0, 0, 1}}, a, b, c));
    CHECK_FALSE(intersectTriangle({{0, 0, 5}, {0, 0, 1}}, a, b, c));
}

TEST_CASE("pick finds the nearest instance through its transform") {
    const auto mesh = triangle();
    const MeshLookup meshes = [&](std::uint32_t id) { return id == 1 ? &mesh : nullptr; };
    Transform near{{}, {0, 0, 100}};
    Transform far{{}, {0, 0, 200}};
    const auto scene = sceneWith({{1, far, {}}, {1, near, {}}});
    const auto hit = pick(scene, {{0, 0, 0}, {0, 0, 1}}, meshes, kSize);
    REQUIRE(hit);
    CHECK(hit->kind == Hit::Kind::Model);
    CHECK(hit->index == 1);
    CHECK(hit->distance == doctest::Approx(100));
}

TEST_CASE("pick respects rotation") {
    const auto mesh = triangle();
    const MeshLookup meshes = [&](std::uint32_t id) { return id == 1 ? &mesh : nullptr; };
    // Turned a quarter about Y, the triangle now lies in the YZ plane and faces along X.
    const Transform turned{de::catalog::rdbModelRotation(0, 512, 0), {50, 0, 0}};
    const auto scene = sceneWith({{1, turned, {}}});
    CHECK(pick(scene, {{0, 0, 0}, {1, 0, 0}}, meshes, kSize));
    CHECK_FALSE(pick(scene, {{50, 0, -100}, {0, 0, 1}}, meshes, kSize));   // edge-on now
}

TEST_CASE("pick sees flats and markers unless told not to") {
    const MeshLookup none = [](std::uint32_t) { return nullptr; };
    FlatInstance torch{210, 0, {0, 0, 50}, false, {}};
    FlatInstance marker{199, 10, {0, 0, 30}, true, {}};
    const auto scene = sceneWith({}, {torch, marker});
    const Ray ray{{0, 0, 0}, {0, 0, 1}};
    auto hit = pick(scene, ray, none, kSize);
    REQUIRE(hit);
    CHECK(hit->kind == Hit::Kind::Flat);
    CHECK(hit->index == 1);
    CHECK(hit->distance == doctest::Approx(26));
    hit = pick(scene, ray, none, kSize, {.flats = true, .markers = false});
    REQUIRE(hit);
    CHECK(hit->index == 0);
    CHECK_FALSE(pick(scene, ray, none, kSize, {.flats = false, .markers = false}));
}
