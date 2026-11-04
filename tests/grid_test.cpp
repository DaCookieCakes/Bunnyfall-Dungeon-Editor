#include "scene/dungeon_scene.h"
#include "scene/grid.h"

#include <doctest/doctest.h>

using namespace de::scene;

TEST_CASE("grid: one block's edges, cells and sockets") {
    std::vector<GridLine> lines;
    appendBlockGrid(lines, {0, 0, 0}, {.height = 100, .cellStep = 128, .sockets = false});
    CHECK(lines.size() == 15 * 2 + 4);   // 15 inner lines each way, four edges
    for (const auto& l : lines) {
        CHECK(l.a.y == 100);
        CHECK(l.b.y == 100);
    }
    lines.clear();
    appendBlockGrid(lines, {2048, 0, 0}, {.height = 0, .cellStep = 0, .sockets = true});
    CHECK(lines.size() == 4 + 4 * 2 * 4);   // edges, and two four-line doorways on each of four edges
    // Every doorway stands on the block's edge, at a standard socket's height.
    for (std::size_t i = 4; i < lines.size(); ++i) {
        const auto& p = lines[i].a;
        const bool onEdge = p.x == 2048 || p.x == 4096 || p.z == 0 || p.z == 2048;
        CHECK(onEdge);
        CHECK(p.y >= 512);
    }
}

TEST_CASE("grid: a block square drawn once however many blocks share it") {
    Scene scene;
    scene.blocks.resize(3);
    scene.blocks[1].origin = {2048, 0, 0};
    scene.blocks[2].origin = {2048, 0, 0};
    const GridOptions options{.height = 0, .cellStep = 0, .sockets = false};
    CHECK(gridLines(scene, options).size() == 8);
}
