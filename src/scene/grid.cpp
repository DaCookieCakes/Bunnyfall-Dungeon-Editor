#include "scene/grid.h"

#include "catalog/catalog.h"
#include "scene/dungeon_scene.h"

#include <set>
#include <utility>

namespace de::scene {

namespace {

constexpr double kSocketHeight = 128;   // a doorway post's height, block units (one storey cell)

/// A doorway: two posts and a lintel standing on the edge, `along` from its start, `width` wide.
void appendSocket(std::vector<GridLine>& out, catalog::Vec3 origin, catalog::Edge edge, double along, double floor,
                  double width, const std::array<float, 4>& colour) {
    const double side = catalog::kRdbBlockSide;
    auto at = [&](double t, double y) -> catalog::Vec3 {
        switch (edge) {
            case catalog::Edge::West: return {origin.x, origin.y + y, origin.z + t};
            case catalog::Edge::East: return {origin.x + side, origin.y + y, origin.z + t};
            case catalog::Edge::South: return {origin.x + t, origin.y + y, origin.z};
            case catalog::Edge::North: break;
        }
        return {origin.x + t, origin.y + y, origin.z + side};
    };
    const double lo = along - width / 2, hi = along + width / 2;
    out.push_back({at(lo, floor), at(lo, floor + kSocketHeight), colour});
    out.push_back({at(hi, floor), at(hi, floor + kSocketHeight), colour});
    out.push_back({at(lo, floor + kSocketHeight), at(hi, floor + kSocketHeight), colour});
    out.push_back({at(lo, floor), at(hi, floor), colour});
}

} // namespace

void appendBlockGrid(std::vector<GridLine>& out, catalog::Vec3 origin, const GridOptions& options) {
    const double side = catalog::kRdbBlockSide;
    const double y = options.height;
    if (options.cellStep > 0) {
        for (double t = options.cellStep; t < side; t += options.cellStep) {
            out.push_back({{origin.x + t, y, origin.z}, {origin.x + t, y, origin.z + side}, grid_colours::kCell});
            out.push_back({{origin.x, y, origin.z + t}, {origin.x + side, y, origin.z + t}, grid_colours::kCell});
        }
    }
    const catalog::Vec3 c00{origin.x, y, origin.z}, c10{origin.x + side, y, origin.z};
    const catalog::Vec3 c01{origin.x, y, origin.z + side}, c11{origin.x + side, y, origin.z + side};
    out.push_back({c00, c10, grid_colours::kBlockEdge});
    out.push_back({c10, c11, grid_colours::kBlockEdge});
    out.push_back({c11, c01, grid_colours::kBlockEdge});
    out.push_back({c01, c00, grid_colours::kBlockEdge});
    if (!options.sockets) return;
    for (const auto edge : {catalog::Edge::West, catalog::Edge::East, catalog::Edge::South, catalog::Edge::North}) {
        appendSocket(out, origin, edge, catalog::kStandardSockets[0].along, catalog::kStandardSockets[0].floor,
                     catalog::kSocketWidth, grid_colours::kUpperSocket);
        appendSocket(out, origin, edge, catalog::kStandardSockets[1].along, catalog::kStandardSockets[1].floor,
                     catalog::kSocketWidth, grid_colours::kLowerSocket);
    }
}

std::vector<GridLine> gridLines(const Scene& scene, const GridOptions& options) {
    std::vector<GridLine> out;
    std::set<std::pair<double, double>> done;
    for (const auto& block : scene.blocks) {
        if (!done.insert({block.origin.x, block.origin.z}).second) continue;
        appendBlockGrid(out, block.origin, options);
    }
    return out;
}

} // namespace de::scene
