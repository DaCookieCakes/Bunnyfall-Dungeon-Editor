#pragma once

// Changes to a custom model's mesh: shapes to start from, extruding, filling, cutting, welding and removing, and
// laying textures. Plain functions over a Mesh; undo is MeshEdit's (mesh_edit.h). Functions that add faces answer
// their indices, so the caller can select what it made.

#include "mesh/mesh.h"

#include <array>
#include <cstdint>
#include <optional>
#include <set>
#include <vector>

namespace de::mesh {

using PointSet = std::set<std::uint32_t>;
using FaceSet = std::set<std::uint32_t>;
/// An edge by its two points, the smaller index first (the same edge whichever face it is read from).
using Edge = std::pair<std::uint32_t, std::uint32_t>;
using EdgeSet = std::set<Edge>;
inline Edge edgeOf(std::uint32_t a, std::uint32_t b) { return a < b ? Edge{a, b} : Edge{b, a}; }

/// Every edge of the mesh's faces, each once.
EdgeSet edgesOf(const Mesh& mesh);
/// The faces that have an edge.
std::vector<std::uint32_t> facesWithEdge(const Mesh& mesh, Edge edge);
/// Every point the edges use.
PointSet pointsOf(const Mesh& mesh, const EdgeSet& edges);

/// New faces grown from edges, each a quad from the edge to the edge moved by `offset`, seen from the same side as
/// the face the edge borders (when it borders one). Answers the new faces.
std::vector<std::uint32_t> extendEdges(Mesh& mesh, const EdgeSet& edges, Vec3 offset);
/// The way to grow an edge out across the face it borders: in that face's plane, square to the edge, away from the
/// face. Nullopt when the edge borders no face.
std::optional<Vec3> outwardOf(const Mesh& mesh, Edge edge);

/// The two faces either side of an edge become one (the first's texture kept). False when the edge does not have
/// exactly two faces, or they face different ways (merging them would bend the face).
bool dissolveEdge(Mesh& mesh, Edge edge);

/// Cuts a face between the middles of two of its edges (both edges get a new middle point, in every face that has
/// them). Answers the new half, or nullopt when no face has both edges.
std::optional<std::uint32_t> connectEdges(Mesh& mesh, Edge first, Edge second);

/// The sides of a box: -X, +X, -Y (bottom), +Y (top), -Z, +Z.
enum class BoxSide { West, East, Bottom, Top, South, North };
inline constexpr std::array<BoxSide, 6> kBoxSides = {BoxSide::West, BoxSide::East, BoxSide::Bottom,
                                                     BoxSide::Top,  BoxSide::South, BoxSide::North};
struct BoxTextures {
    FaceTexture walls, floor{120, 2}, ceiling{120, 3};
};

/// A box's sides as faces sharing its eight corners. `inward`: seen from inside (a room); else from outside (a
/// solid thing). `sides` says which of the six get a face (in kBoxSides order), so a corridor can leave its ends open.
std::vector<std::uint32_t> addBox(Mesh& mesh, const catalog::Box& box, bool inward, const std::array<bool, 6>& sides,
                                  const BoxTextures& textures);

/// One four-sided face through `corners` (in order round it), turned to be seen from the side `facing` points to.
std::uint32_t addQuad(Mesh& mesh, const std::array<Vec3, 4>& corners, Vec3 facing, FaceTexture texture);

/// A straight flight of `steps` steps climbing toward +Z across `box`, from its bottom at -Z to its top at +Z: each
/// step a tread (seen from above, floor texture) and a riser (seen from -Z, wall texture). Walls and a ceiling round
/// it are a box's or an extrusion's job.
std::vector<std::uint32_t> addStairs(Mesh& mesh, const catalog::Box& box, int steps, const BoxTextures& textures);

/// Moves the faces `distance` along their normals (negative: behind them, which pushes a room's wall out into a new
/// corridor) and joins the edge round them to where they were with new faces, which take the texture of the face
/// they border and face the same way the moved faces do (into a room pushed out, out of a solid pulled out).
/// Points the faces share with faces left behind are copied, so those stay where they were. Answers the new faces.
std::vector<std::uint32_t> extrude(Mesh& mesh, const FaceSet& faces, double distance);

/// A new face through `points`, ordered round them on their common plane and turned to face `viewer`. Nullopt when
/// fewer than three points or they lie on one line.
std::optional<std::uint32_t> fillFace(Mesh& mesh, const PointSet& points, FaceTexture texture, Vec3 viewer);

/// Cuts a face in two along the line between two of its points (not neighbours on it). The new half keeps the
/// texture. Answers the new face.
std::optional<std::uint32_t> splitFace(Mesh& mesh, std::uint32_t face, std::uint32_t a, std::uint32_t b);

/// A new point in the middle of the edge a-b, added to every face that has that edge. Nullopt when no face has it.
std::optional<std::uint32_t> splitEdge(Mesh& mesh, std::uint32_t a, std::uint32_t b);

/// A hole to cut in a face (a doorway, a window, a trapdoor), in the face's own terms: across it to the right as seen
/// from the front, and up it (on a floor: north). Block units.
struct Opening {
    double width = 64, height = 96;
    double bottom = 0;   ///< From the face's lowest edge to the hole's.
    double across = 0;   ///< From the face's middle to the hole's, to the right.
    double depth = 0;    ///< > 0: a reveal this deep lines the hole, going back behind the face (through a wall).
};

/// Where an opening falls on a face: its corners bottom-left, bottom-right, top-right, top-left (seen from the
/// front), cut back to the face's extent. Nullopt when the face has no area or the opening misses it.
std::optional<std::array<Vec3, 4>> openingCorners(const Mesh& mesh, std::uint32_t face, const Opening& opening);

struct CutResult {
    std::vector<std::uint32_t> pieces;   ///< What is left of the face (the first keeps its index).
    std::vector<std::uint32_t> reveal;   ///< The faces lining the hole.
};
/// Cuts an opening out of a face: the face becomes the pieces round the hole (each keeping its texture and
/// alignment, so the texture runs on unbroken), and with a depth the hole is lined, its faces seen from inside it
/// (a sill takes the floor texture, a lintel the ceiling's, the sides the face's). The new corners are also put into
/// any other face whose edge they lie on, so nothing cracks. Empty when the opening misses the face.
CutResult cutOpening(Mesh& mesh, std::uint32_t face, const Opening& opening);

/// Cuts faces in two where the plane `axis` = `at` crosses them (axis 0 X, 1 Y, 2 Z), like a knife along a grid
/// line. Faces it does not cross stay whole; new corners go into neighbouring edges as cutOpening's do. Answers the
/// halves made (the other half of each keeps its face's index). Meant for flat, convex faces (walls, floors).
std::vector<std::uint32_t> sliceFaces(Mesh& mesh, const FaceSet& faces, int axis, double at);

/// Points become one, at `at`. Faces lose the repeats; a face left with under three points goes. Answers the point
/// they became (its index after unused points are removed).
std::optional<std::uint32_t> weld(Mesh& mesh, const PointSet& points, Vec3 at);

/// Points closer than `tolerance` become one. Answers how many points went.
std::size_t mergeByDistance(Mesh& mesh, double tolerance);

/// Faces go; points no face uses go with them.
void removeFaces(Mesh& mesh, const FaceSet& faces);

/// Points go from every face (a face left with under three points goes too), then from the mesh.
void removePoints(Mesh& mesh, const PointSet& points);

/// Faces turn to be seen from their other side.
void flipFaces(Mesh& mesh, const FaceSet& faces);

/// Copies of the faces (with their own points), `offset` away. Answers the copies.
std::vector<std::uint32_t> duplicateFaces(Mesh& mesh, const FaceSet& faces, Vec3 offset);

/// Removes points no face uses. Answers each old index's new one (nullopt for a removed point).
std::vector<std::optional<std::uint32_t>> removeUnusedPoints(Mesh& mesh);

/// Every point the faces use.
PointSet pointsOf(const Mesh& mesh, const FaceSet& faces);

// ---- moving points ----
void movePoints(Mesh& mesh, const PointSet& points, Vec3 offset);
/// Turned about the vertical line through `centre`, clockwise seen from above (as models turn in a block).
void turnPoints(Mesh& mesh, const PointSet& points, Vec3 centre, double degrees);
/// Scaled about `centre`, each axis by its own factor. Faces keep facing their way, even when one factor is negative
/// (a mirror turns them round).
void scalePoints(Mesh& mesh, const PointSet& points, Vec3 centre, Vec3 factors);

// ---- textures ----
void setTexture(Mesh& mesh, const FaceSet& faces, FaceTexture texture);
/// Scales and shifts a face's texture so one copy of a `width` x `height` texel picture covers it exactly.
void fitTexture(Mesh& mesh, std::uint32_t face, int width, int height);

/// `value` to the nearest multiple of `step` (no change for a step of 0 or less).
double snapTo(double value, double step);
Vec3 snapTo(Vec3 value, double step);

} // namespace de::mesh
