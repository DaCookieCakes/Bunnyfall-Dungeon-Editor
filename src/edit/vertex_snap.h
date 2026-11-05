#pragma once

// Vertex snap: a piece's open rims (the mouth of a corridor, the edge of a cave tile) and where a moved piece meets a
// placed one rim to rim, every point of one on a point of the other.

#include "catalog/space.h"

#include <cstdint>
#include <optional>
#include <vector>

namespace de::edit {

/// One open rim: the points of a loop of edges that only one face uses.
struct Rim {
    std::vector<catalog::Vec3> points;   ///< Each once.
    catalog::Vec3 centre;                ///< Their average.
};

/// The open rims of a model (points and faces in editor space), its own coordinates. Points at the same place count
/// as one. A closed model has none.
std::vector<Rim> openRims(const std::vector<catalog::Vec3>& points, const std::vector<std::vector<std::uint32_t>>& faces);

/// `rim` rotated and then moved.
Rim placedRim(const Rim& rim, const catalog::Mat3& rotation, catalog::Vec3 translation);

/// How far `moving` has to go so that every one of its points lands on a point of `fixed` (and they have the same
/// number), within `tolerance` block units. Nothing when they do not meet that way.
std::optional<catalog::Vec3> rimShift(const Rim& moving, const Rim& fixed, double tolerance = 0.5);

} // namespace de::edit
