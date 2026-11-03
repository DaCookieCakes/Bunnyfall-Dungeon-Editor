#include "arena2_fixture.h"
#include "catalog/catalog.h"

#include <doctest/doctest.h>

#include <cstdlib>
#include <optional>

using namespace de::catalog;
using de::formats::RdbBlock;
using de::formats::RdbModel;
using de::formats::RdbObject;
using de::formats::RdbObjectType;

namespace {

/// A corridor 256 long (x), 50 wide (z), 128 high, open at both ends: floor, ceiling and two side walls.
ModelInfo openCorridor(std::uint32_t id) {
    ModelInfo m;
    m.id = id;
    m.kind = ModelKind::Architecture;
    m.bounds = {{-128, 0, -25}, {128, 128, 25}};
    m.points = {{-128, 0, -25}, {128, 0, -25}, {128, 0, 25}, {-128, 0, 25},
                {-128, 128, -25}, {128, 128, -25}, {128, 128, 25}, {-128, 128, 25}};
    m.faces = {{0, 1, 2, 3}, {4, 5, 6, 7}, {0, 1, 5, 4}, {3, 2, 6, 7}};
    return m;
}

/// One wall 50 wide, lying in its own x = 0 plane.
ModelInfo flatWall(std::uint32_t id) {
    ModelInfo m;
    m.id = id;
    m.kind = ModelKind::Architecture;
    m.bounds = {{0, 0, -25}, {0, 128, 25}};
    m.points = {{0, 0, -25}, {0, 0, 25}, {0, 128, 25}, {0, 128, -25}};
    m.faces = {{0, 1, 2, 3}};
    return m;
}

/// A block holding one model, placed at file coordinates with a file y rotation.
RdbBlock blockWith(std::uint32_t modelId, std::int32_t x, std::int32_t y, std::int32_t z, std::int32_t yRotation) {
    RdbBlock block;
    block.width = block.height = 1;
    block.modelRefs.resize(de::formats::kRdbModelRefSlots);
    block.modelRefs[0] = {std::to_string(modelId), modelId, "C00"};
    RdbObject obj;
    obj.type = RdbObjectType::Model;
    obj.x = x;
    obj.y = y;
    obj.z = z;
    RdbModel model;
    model.yRotation = yRotation;
    obj.resource = model;
    block.roots.push_back({0, {obj}});
    return block;
}

} // namespace

TEST_CASE("a corridor crossing the west edge opens a port there") {
    const std::map<std::uint32_t, ModelInfo> models{{1, openCorridor(1)}};
    const auto ports = detectPorts(blockWith(1, 0, -1280, 640, 0), models);
    REQUIRE(ports.size() == 1);
    CHECK(ports[0] == Port{Edge::West, 640, 1280, 50, 1});
    CHECK(standardSocketsOf(ports)[static_cast<std::size_t>(Edge::West)][0]);
}

TEST_CASE("a corridor ending exactly on the edge opens a port") {
    const std::map<std::uint32_t, ModelInfo> models{{1, openCorridor(1)}};
    const auto ports = detectPorts(blockWith(1, 2048 - 128, -512, 1408, 0), models);   // east end at x = 2048
    REQUIRE(ports.size() == 1);
    CHECK(ports[0] == Port{Edge::East, 1408, 512, 50, 1});
}

TEST_CASE("a turned corridor opens the edge it now points at") {
    const std::map<std::uint32_t, ModelInfo> models{{1, openCorridor(1)}};
    // yRotation 512 turns the corridor's length from x onto z, so it crosses the south edge (z = 0).
    const auto ports = detectPorts(blockWith(1, 640, -1280, 0, 512), models);
    REQUIRE(ports.size() == 1);
    CHECK(ports[0] == Port{Edge::South, 640, 1280, 50, 1});
}

TEST_CASE("a wall lying in the edge plane opens nothing") {
    const std::map<std::uint32_t, ModelInfo> models{{2, flatWall(2)}};
    CHECK(detectPorts(blockWith(2, 0, 0, 640, 0), models).empty());
}

TEST_CASE("pieces away from every edge open nothing; props never do") {
    auto prop = openCorridor(3);
    prop.kind = ModelKind::Prop;
    const std::map<std::uint32_t, ModelInfo> models{{1, openCorridor(1)}, {3, prop}};
    CHECK(detectPorts(blockWith(1, 1024, 0, 1024, 0), models).empty());
    CHECK(detectPorts(blockWith(3, 0, 0, 640, 0), models).empty());
}

TEST_CASE("model kinds") {
    const Box cell{{-64, 0, -64}, {64, 128, 64}};
    const Box lamp{{-2, 0, -1}, {2, 22, 1}};
    CHECK(classifyModel(70300, "EXT", cell) == ModelKind::ExitDoor);
    CHECK(classifyModel(55005, "DOR", lamp) == ModelKind::Door);
    CHECK(classifyModel(72100, "DOR", lamp) == ModelKind::Prop);   // the red brick door is not an action door
    CHECK(classifyModel(67000, "C00", cell) == ModelKind::Architecture);
    CHECK(classifyModel(70007, "011", lamp) == ModelKind::Architecture);   // a room named by its size
    CHECK(classifyModel(64300, "R01", lamp) == ModelKind::Architecture);
    CHECK(classifyModel(58012, "LDB", cell) == ModelKind::Architecture);   // mixed family: big enough
    CHECK(classifyModel(61028, "L0Z", lamp) == ModelKind::Prop);           // mixed family: a lamp
    CHECK(classifyModel(41105, "XXX", lamp) == ModelKind::Prop);
}

// ---- against ARENA2 ----

namespace {
const Catalog* catalog() {
    static const std::optional<Catalog> data = []() -> std::optional<Catalog> {
        const auto* a = de::test::arena2();
        if (!a) return std::nullopt;
        auto c = Catalog::build(*a);
        if (!c) FAIL(c.error().message);
        return std::move(*c);
    }();
    return data ? &*data : nullptr;
}
} // namespace

TEST_CASE("ARENA2 catalog: every normal, wet and border block opens all eight standard sockets") {
    const Catalog* c = catalog();
    if (!c) {
        MESSAGE("skipped: set DE_ARENA2 to an ARENA2 directory");
        return;
    }
    int checked = 0;
    for (const auto& b : c->blocks()) {
        using de::formats::RdbRole;
        if (b.role != RdbRole::Normal && b.role != RdbRole::Wet && b.role != RdbRole::Border) continue;
        INFO(b.name);
        ++checked;
        for (const auto& edge : b.sockets) {
            CHECK(edge[0]);
            CHECK(edge[1]);
        }
    }
    CHECK(checked == 138);
}

TEST_CASE("ARENA2 catalog: models and blocks") {
    const Catalog* c = catalog();
    if (!c) {
        MESSAGE("skipped: set DE_ARENA2 to an ARENA2 directory");
        return;
    }
    // Repaired blocks (DFU's FixRdbData): N0000022/23 lose their bad door, model 09004, used nowhere else.
    CHECK(c->models().size() == 1042);
    CHECK(c->model(9004) == nullptr);
    CHECK(c->blocks().size() == 187);

    const auto* corridor = c->model(67000);
    REQUIRE(corridor);
    CHECK(corridor->kind == ModelKind::Architecture);
    CHECK(corridor->uses == 1304);   // 1302 stored; W0000018's and W0000020's repairs each add one
    CHECK(corridor->bounds.min == Vec3{-64, 0, -128});
    CHECK(corridor->bounds.max == Vec3{64, 128, 128});
    CHECK(c->model(70300)->kind == ModelKind::ExitDoor);

    const auto* cap = c->block("B0000001.RDB");
    REQUIRE(cap);
    CHECK(cap->ports.size() == 8);
    CHECK(cap->otherPorts.empty());
    for (const auto& p : cap->ports) CHECK(p.width == kSocketWidth);

    int exits = 0;
    for (const auto& b : c->blocks()) exits += b.hasExitDoor ? 1 : 0;
    CHECK(exits > 0);
}
