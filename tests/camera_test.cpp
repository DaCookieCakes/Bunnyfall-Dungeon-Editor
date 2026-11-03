#include "render/camera.h"

#include <doctest/doctest.h>

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
