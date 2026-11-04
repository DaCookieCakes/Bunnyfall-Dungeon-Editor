#pragma once

// The sizing grid: block edges, the cell grid inside blocks, and the standard doorway sockets on every edge
// (DESIGN.md §2.3), as coloured line segments in editor space.

#include "catalog/space.h"

#include <array>
#include <vector>

namespace de::scene {

struct Scene;

struct GridOptions {
    double height = 0;      ///< Editor Y of the grid plane.
    int cellStep = 128;     ///< Block units between cell lines (0: block edges only).
    bool sockets = true;    ///< Draw the doorway sockets on each block edge.
};

struct GridLine {
    catalog::Vec3 a, b;
    std::array<float, 4> colour;   ///< RGBA, 0..1
};

namespace grid_colours {
inline constexpr std::array<float, 4> kBlockEdge = {0.35f, 0.85f, 1.0f, 0.9f};
inline constexpr std::array<float, 4> kCell = {0.8f, 0.8f, 0.85f, 0.45f};
inline constexpr std::array<float, 4> kUpperSocket = {0.4f, 1.0f, 0.45f, 0.95f};
inline constexpr std::array<float, 4> kLowerSocket = {1.0f, 0.6f, 0.2f, 0.95f};
} // namespace grid_colours

/// The grid for every block square in the scene (each square once, even where a block repeats).
std::vector<GridLine> gridLines(const Scene& scene, const GridOptions& options);

/// The grid for one block square whose (0,0,0) corner is `origin`.
void appendBlockGrid(std::vector<GridLine>& out, catalog::Vec3 origin, const GridOptions& options);

} // namespace de::scene
