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

/// Rounds a file coordinate to a grid step (no rounding for step 0 or 1).
inline std::int32_t snapTo(double value, int step) {
    if (step <= 1) return static_cast<std::int32_t>(std::lround(value));
    return static_cast<std::int32_t>(std::lround(value / step) * step);
}

} // namespace de::edit
