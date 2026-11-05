#include "catalog/space.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace de::catalog {

namespace {

// Single-axis rotations matching Bunnyfall's trs(): positive angles turn Y from Z toward X, X from Y toward Z, and
// Z from X toward Y.
Mat3 rotX(double deg) {
    const double r = deg * std::numbers::pi / 180.0, c = std::cos(r), s = std::sin(r);
    return Mat3{{1, 0, 0, 0, c, -s, 0, s, c}};
}
Mat3 rotY(double deg) {
    const double r = deg * std::numbers::pi / 180.0, c = std::cos(r), s = std::sin(r);
    return Mat3{{c, 0, s, 0, 1, 0, -s, 0, c}};
}
Mat3 rotZ(double deg) {
    const double r = deg * std::numbers::pi / 180.0, c = std::cos(r), s = std::sin(r);
    return Mat3{{c, -s, 0, s, c, 0, 0, 0, 1}};
}

/// Snap away floating-point noise so a 90-degree turn of an integer box gives integers back.
double clean(double v) {
    const double r = std::round(v);
    return std::abs(v - r) < 1e-6 ? r : v;
}

} // namespace

Mat3 rdbModelRotation(std::int32_t xRotation, std::int32_t yRotation, std::int32_t zRotation) {
    return rotZ(rotationDegrees(zRotation)) * rotX(rotationDegrees(xRotation)) * rotY(rotationDegrees(yRotation));
}

Mat3 axisRotation(int axis, double degrees) {
    return axis == 0 ? rotX(degrees) : axis == 1 ? rotY(degrees) : rotZ(degrees);
}

std::array<std::int32_t, 3> rdbRotationOf(const Mat3& r) {
    // rotZ(g) * rotX(a) * rotY(b) is, row-major:
    //   [cg cb - sg sa sb, -sg ca, cg sb + sg sa cb]
    //   [sg cb + cg sa sb,  cg ca, sg sb - cg sa cb]
    //   [-ca sb,            sa,    ca cb           ]
    const auto& m = r.m;
    const double a = std::asin(std::clamp(m[7], -1.0, 1.0));
    double b = 0, g = 0;
    if (std::abs(std::cos(a)) > 1e-6) {
        b = std::atan2(-m[6], m[8]);
        g = std::atan2(-m[1], m[4]);
    } else {
        g = std::atan2(m[3], m[0]);   // b = 0: the first column is (cg, sg, 0)
    }
    // Editor degrees are the file's negated (rotationDegrees).
    auto units = [](double radians) {
        const auto u = static_cast<std::int32_t>(std::lround(-radians * kRotationUnitsPerTurn / (2 * std::numbers::pi)));
        return ((u % kRotationUnitsPerTurn) + kRotationUnitsPerTurn) % kRotationUnitsPerTurn;
    };
    return {units(a), units(b), units(g)};
}

Box transformBox(const Box& local, const Mat3& rotation, Vec3 position) {
    Box out{{1e300, 1e300, 1e300}, {-1e300, -1e300, -1e300}};
    for (int corner = 0; corner < 8; ++corner) {
        const Vec3 p{(corner & 1) ? local.max.x : local.min.x, (corner & 2) ? local.max.y : local.min.y,
                     (corner & 4) ? local.max.z : local.min.z};
        const Vec3 w = rotation * p + position;
        out.min = {std::min(out.min.x, w.x), std::min(out.min.y, w.y), std::min(out.min.z, w.z)};
        out.max = {std::max(out.max.x, w.x), std::max(out.max.y, w.y), std::max(out.max.z, w.z)};
    }
    out.min = {clean(out.min.x), clean(out.min.y), clean(out.min.z)};
    out.max = {clean(out.max.x), clean(out.max.y), clean(out.max.z)};
    return out;
}

} // namespace de::catalog
