#pragma once

// A model the designer builds: points and polygon faces in editor space (block units, Y up), each face with a
// Daggerfall texture and a texture alignment. This is the editor's own form of a custom model; it becomes a
// geometry::ModelMesh to draw and pick, and a Bunnyfall model file on export (mesh_json.h).
//
// A face's points go round it so that, for the triangles fanned from its first point, cross(b - a, c - a) points to
// the side it is seen from. That is the winding Daggerfall Unity's ARCH3D triangles have in editor space, so custom
// and classic pieces draw and cull alike. A room is built from faces seen from inside.

#include "catalog/catalog.h"
#include "catalog/space.h"
#include "geometry/model_mesh.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace de::mesh {

using catalog::Vec3;

/// Classic pieces lay 1 texel over 2 block units (a 64-texel wall picture covers a 128-unit cell; measured on the
/// corridors and rooms, DESIGN.md §7.3).
inline constexpr double kClassicUnitsPerTexel = 2.0;

struct FaceTexture {
    int archive = 120;   ///< A dungeon base archive by default, so the dungeon's texture table swaps it like classic walls.
    int record = 0;
    friend bool operator==(const FaceTexture&, const FaceTexture&) = default;
};

/// How a texture lies on a face, like Hammer's texture alignment: projected onto the face's own plane along axes
/// that do not depend on its shape, so neighbouring faces line up. Walls run their texture across and down; floors
/// and ceilings along X and Z.
struct UvAlign {
    double scaleU = kClassicUnitsPerTexel, scaleV = kClassicUnitsPerTexel;   ///< Block units per texel.
    double offsetU = 0, offsetV = 0;   ///< Texels.
    double angle = 0;                  ///< Degrees, turning the texture on the face.
    friend bool operator==(const UvAlign&, const UvAlign&) = default;
};

struct Face {
    std::vector<std::uint32_t> points;   ///< Indices into Mesh::points, round the face (see the winding above).
    FaceTexture texture;
    UvAlign uv;
    friend bool operator==(const Face&, const Face&) = default;
};

struct Mesh {
    std::vector<Vec3> points;
    std::vector<Face> faces;
    friend bool operator==(const Mesh&, const Mesh&) = default;

    /// The side a face is seen from, unit length (Newell's method, so any simple polygon works); zero when it has no
    /// area.
    [[nodiscard]] Vec3 normal(const Face& face) const;
    /// The average of a face's points.
    [[nodiscard]] Vec3 centre(const Face& face) const;
    /// Around every point; all zero for an empty mesh.
    [[nodiscard]] catalog::Box bounds() const;
};

// ---- small vector maths, editor space ----
inline double dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3 cross(Vec3 a, Vec3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
double length(Vec3 v);
/// Unit length, or zero for a zero vector.
Vec3 normalised(Vec3 v);

/// The directions one texel across (u) and one texel down (v) run on a face with this normal, before scaling,
/// turned by `angleDegrees`. Walls: u runs to the right as seen from the front, v straight down the face. Floors
/// and ceilings (|normal.y| > 0.7): u along +X, v along -Z on a floor and +Z on a ceiling (north is up on the
/// screen looking down).
struct UvAxes {
    Vec3 u, v;
};
UvAxes uvAxes(Vec3 normal, double angleDegrees);

/// The texel coordinates (v down from the picture's top) of a point on a face.
std::array<double, 2> texelsAt(const UvAlign& align, const UvAxes& axes, Vec3 point);

/// Triangles covering a face, as index triples into face.points. Strictly convex faces fan from the first point (as
/// ARCH3D faces do); concave ones, and ones with points along an edge, are cut into ears on the face's plane. Empty
/// for a face of fewer than three points or no area.
std::vector<std::array<std::uint32_t, 3>> triangulate(const Mesh& mesh, const Face& face);

/// The drawable mesh: triangles grouped by texture in order of first use, flat normals, texel UVs.
geometry::ModelMesh toModelMesh(const Mesh& mesh, std::uint32_t modelId);

/// What the catalog knows of a custom model (bounds, points and faces for picking and doorway detection, its main
/// texture archive), so it is placed, snapped and measured like a classic one. It has no twins.
catalog::ModelInfo modelInfoOf(const Mesh& mesh, std::uint32_t modelId, std::string description);

/// Things that will draw wrongly or not at all: faces with fewer than three points, repeated points, no area, or
/// points off their plane. One line each, naming the face.
std::vector<std::string> checkMesh(const Mesh& mesh);

} // namespace de::mesh
