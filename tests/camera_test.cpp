#include "render/camera.h"

#include <doctest/doctest.h>

#include <numbers>

using de::catalog::Vec3;
using de::render::Camera;

namespace {

/// Where a point lands in normalised device coordinates.
std::array<double, 3> project(const Camera& c, Vec3 p, double aspect) {
    const auto m = de::render::multiply(c.projection(aspect), c.view());
    const double x = m[0] * p.x + m[4] * p.y + m[8] * p.z + m[12];
    const double y = m[1] * p.x + m[5] * p.y + m[9] * p.z + m[13];
    const double z = m[2] * p.x + m[6] * p.y + m[10] * p.z + m[14];
    const double w = m[3] * p.x + m[7] * p.y + m[11] * p.z + m[15];
    return {x / w, y / w, z / w};
}

} // namespace

TEST_CASE("camera: yaw 0 looks along +Z with +X to the right") {
    Camera c;
    c.position = {100, 50, 0};
    const auto ahead = project(c, {100, 50, 500}, 16.0 / 9);
    CHECK(ahead[0] == doctest::Approx(0).epsilon(1e-6));
    CHECK(ahead[1] == doctest::Approx(0).epsilon(1e-6));
    CHECK(ahead[2] > -1);
    CHECK(ahead[2] < 1);
    CHECK(project(c, {200, 50, 500}, 16.0 / 9)[0] > 0);   // +X is right
    CHECK(project(c, {100, 150, 500}, 16.0 / 9)[1] > 0);  // +Y is up
}

TEST_CASE("camera: near and far planes map to -1 and 1") {
    Camera c;
    CHECK(project(c, {0, 0, c.nearPlane}, 1)[2] == doctest::Approx(-1));
    CHECK(project(c, {0, 0, c.farPlane}, 1)[2] == doctest::Approx(1));
}

TEST_CASE("camera: a pixel's ray passes through what is drawn at that pixel") {
    Camera c;
    c.position = {10, 20, 30};
    c.yaw = 0.7;
    c.pitch = -0.3;
    const double w = 1280, h = 720;
    for (const auto& [px, py] : {std::pair{640.0, 360.0}, std::pair{100.0, 50.0}, std::pair{1200.0, 700.0}}) {
        const auto ray = c.rayThrough(px, py, w, h);
        const Vec3 p{ray.origin.x + ray.direction.x * 900, ray.origin.y + ray.direction.y * 900,
                     ray.origin.z + ray.direction.z * 900};
        const auto ndc = project(c, p, w / h);
        CHECK((ndc[0] + 1) / 2 * w == doctest::Approx(px).epsilon(1e-6));
        CHECK((1 - ndc[1]) / 2 * h == doctest::Approx(py).epsilon(1e-6));
    }
}

TEST_CASE("camera: lookAt turns toward the target") {
    Camera c;
    c.position = {0, 0, 0};
    c.lookAt({100, 100, 0});
    const auto f = c.forward();
    CHECK(f.x == doctest::Approx(std::sqrt(0.5)));
    CHECK(f.y == doctest::Approx(std::sqrt(0.5)));
    CHECK(f.z == doctest::Approx(0).epsilon(1e-9));
}

TEST_CASE("orthographic camera: rays are parallel and land where the projection draws") {
    Camera c;
    c.orthographic = true;
    c.orthoHalfHeight = 500;
    c.nearPlane = 1;
    c.farPlane = 60000;
    c.pitch = -std::numbers::pi / 2;   // straight down: the top view
    c.position = {1000, 20000, 2000};
    const double w = 800, h = 600;
    const auto a = c.rayThrough(0, 0, w, h), b = c.rayThrough(w, h, w, h);
    CHECK(a.direction.y == doctest::Approx(-1));
    CHECK(b.direction.y == doctest::Approx(-1));
    // The top-left corner is west and north of the centre (north is up in the top view).
    CHECK(a.origin.x < 1000);
    CHECK(a.origin.z > 2000);
    CHECK(b.origin.x - a.origin.x == doctest::Approx(2 * 500 * w / h));
    for (const auto& [px, py] : {std::pair{400.0, 300.0}, std::pair{10.0, 590.0}}) {
        const auto ray = c.rayThrough(px, py, w, h);
        const Vec3 p{ray.origin.x, 0, ray.origin.z};   // straight below, on the floor
        const auto ndc = project(c, p, w / h);
        CHECK((ndc[0] + 1) / 2 * w == doctest::Approx(px).epsilon(1e-6));
        CHECK((1 - ndc[1]) / 2 * h == doctest::Approx(py).epsilon(1e-6));
        CHECK(ndc[2] > -1);
        CHECK(ndc[2] < 1);
    }
}
