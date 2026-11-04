#pragma once

// Where a placed thing goes, given the surface the pointer is on. Flats are placed by their centre, so a sprite on a
// floor stands half its height above the floor, one on a wall half its width out from it.

#include "catalog/space.h"

#include <cmath>

namespace de::edit {

/// The centre for a `width` x `height` sprite resting on the surface at `point` with unit `normal` (facing the viewer).
inline catalog::Vec3 restingCentre(catalog::Vec3 point, catalog::Vec3 normal, double width, double height) {
    constexpr double kFloorish = 0.7;   // normals steeper than this count as floor or ceiling
    constexpr double kClearance = 1;    // block units off the surface, so the sprite never sinks into it
    if (normal.y > kFloorish) return {point.x, point.y + height / 2 + kClearance, point.z};
    if (normal.y < -kFloorish) return {point.x, point.y - height / 2 - kClearance, point.z};
    const double len = std::sqrt(normal.x * normal.x + normal.z * normal.z);
    const double out = width / 2 + kClearance;
    return {point.x + normal.x / len * out, point.y, point.z + normal.z / len * out};
}

/// The origin for a model with unrotated `bounds` (editor space, about its origin) resting on the surface at `point`:
/// on a floor its bottom sits on the floor, under a ceiling its top touches it, against a wall it stands out from it.
inline catalog::Vec3 restingModelOrigin(catalog::Vec3 point, catalog::Vec3 normal, const catalog::Box& bounds) {
    constexpr double kFloorish = 0.7;
    if (normal.y > kFloorish) return {point.x, point.y - bounds.min.y, point.z};
    if (normal.y < -kFloorish) return {point.x, point.y - bounds.max.y, point.z};
    const double len = std::sqrt(normal.x * normal.x + normal.z * normal.z);
    const double nx = normal.x / len, nz = normal.z / len;
    // How far the model reaches toward the wall, along the wall's normal.
    const double back = -(nx > 0 ? bounds.min.x * nx : bounds.max.x * nx) - (nz > 0 ? bounds.min.z * nz : bounds.max.z * nz);
    return {point.x + nx * back, point.y - bounds.min.y, point.z + nz * back};
}

/// Rounds a file coordinate to a grid step (no rounding for step 0 or 1).
inline std::int32_t snapTo(double value, int step) {
    if (step <= 1) return static_cast<std::int32_t>(std::lround(value));
    return static_cast<std::int32_t>(std::lround(value / step) * step);
}

} // namespace de::edit
