#include "arena2_fixture.h"
#include "formats/arch3d.h"
#include "mesh/mesh_edit.h"
#include "mesh/mesh_json.h"
#include "mesh/ops.h"
#include "project/project.h"
#include "project/world_export.h"

#include <doctest/doctest.h>

#include <cmath>
#include <filesystem>
#include <fstream>

using namespace de;
using namespace de::mesh;

namespace {

constexpr std::array<bool, 6> kAllSides = {true, true, true, true, true, true};

/// A room one cell across and two high, seen from inside: classic corridor 67000's size.
Mesh room() {
    Mesh m;
    addBox(m, {{-64, 0, -128}, {64, 128, 128}}, true, kAllSides, {});
    return m;
}

bool near(Vec3 a, Vec3 b, double tolerance = 1e-6) { return length(a - b) <= tolerance; }

/// The face whose middle is at `c`, if any.
std::optional<std::uint32_t> faceAt(const Mesh& m, Vec3 c) {
    for (std::uint32_t f = 0; f < m.faces.size(); ++f) {
        if (near(m.centre(m.faces[f]), c, 1e-3)) return f;
    }
    return std::nullopt;
}

/// Every drawn triangle turns the way its normal says: cross(b - a, c - a) along it (DFU's winding, see mesh.h).
void checkWinding(const Mesh& m) {
    const auto drawn = toModelMesh(m, 1);
    for (std::size_t t = 0; t < drawn.indices.size(); t += 3) {
        const auto& a = drawn.vertices[drawn.indices[t]];
        const auto& b = drawn.vertices[drawn.indices[t + 1]];
        const auto& c = drawn.vertices[drawn.indices[t + 2]];
        CHECK(dot(cross(b.position - a.position, c.position - a.position), a.normal) > 0);
    }
}

} // namespace

TEST_CASE("a room box is seen from inside, shares its corners and draws as twelve triangles") {
    const auto m = room();
    CHECK(m.points.size() == 8);
    REQUIRE(m.faces.size() == 6);
    const auto floor = faceAt(m, {0, 0, 0});
    const auto ceiling = faceAt(m, {0, 128, 0});
    const auto east = faceAt(m, {64, 64, 0});
    REQUIRE(floor);
    REQUIRE(ceiling);
    REQUIRE(east);
    CHECK(near(m.normal(m.faces[*floor]), {0, 1, 0}));
    CHECK(near(m.normal(m.faces[*ceiling]), {0, -1, 0}));
    CHECK(near(m.normal(m.faces[*east]), {-1, 0, 0}));
    CHECK(m.faces[*floor].texture == FaceTexture{120, 2});
    CHECK(m.faces[*ceiling].texture == FaceTexture{120, 3});
    CHECK(m.faces[*east].texture == FaceTexture{120, 0});
    const auto drawn = toModelMesh(m, 7000001);
    CHECK(drawn.indices.size() == 36);
    CHECK(drawn.subMeshes.size() == 3);
    checkWinding(m);
    CHECK(checkMesh(m).empty());
}

TEST_CASE("an outward box faces out") {
    Mesh m;
    addBox(m, {{0, 0, 0}, {10, 10, 10}}, false, kAllSides, {});
    const auto top = faceAt(m, {5, 10, 5});
    REQUIRE(top);
    CHECK(near(m.normal(m.faces[*top]), {0, 1, 0}));
    CHECK(m.faces[*top].texture == FaceTexture{120, 2});   // a top is walked on: the floor texture
    checkWinding(m);
}

TEST_CASE("texture axes: walls run right and down as seen from the front, at the classic density") {
    // The east wall of a room, seen from inside (looking +X): right is -Z in editor space.
    const auto wall = uvAxes({-1, 0, 0}, 0);
    CHECK(near(wall.u, {0, 0, -1}));
    CHECK(near(wall.v, {0, -1, 0}));
    const auto floor = uvAxes({0, 1, 0}, 0);
    CHECK(near(floor.u, {1, 0, 0}));
    CHECK(near(floor.v, {0, 0, -1}));
    const auto turned = uvAxes({0, 1, 0}, 90);
    CHECK(near(turned.u, {0, 0, -1}));
    // A 128-unit wall is 64 texels: one classic wall picture.
    const UvAlign align;
    const auto a = texelsAt(align, wall, {64, 128, 64});
    const auto b = texelsAt(align, wall, {64, 0, -64});
    CHECK(std::abs(b[0] - a[0]) == doctest::Approx(64));
    CHECK(b[1] - a[1] == doctest::Approx(64));   // v grows downwards
}

TEST_CASE("ARENA2: a box built like corridor 67000 matches its texel density") {
    DE_REQUIRE_ARENA2(a);
    const auto index = a->arch3d.findById(67000);
    REQUIRE(index);
    const auto classic = geometry::buildModelMesh(*formats::parseArch3dMesh(a->arch3d.record(*index), 67000));
    REQUIRE(classic);
    const auto mine = toModelMesh(room(), 1);
    // Texels per unit along every triangle edge, the same in both (0.5).
    auto density = [](const geometry::ModelMesh& m) {
        double sum = 0;
        int n = 0;
        for (std::size_t t = 0; t < m.indices.size(); t += 3) {
            const auto& p = m.vertices[m.indices[t]];
            const auto& q = m.vertices[m.indices[t + 1]];
            const double d = length(q.position - p.position);
            if (d < 1) continue;
            sum += std::hypot(double(q.u - p.u), double(q.v - p.v)) / d;
            ++n;
        }
        return sum / n;
    };
    CHECK(density(mine) == doctest::Approx(0.5).epsilon(0.02));
    CHECK(density(*classic) == doctest::Approx(0.5).epsilon(0.02));
}

TEST_CASE("an L-shaped face is cut into ears, not fanned") {
    Mesh m;
    m.points = {{0, 0, 0}, {0, 0, 20}, {10, 0, 20}, {10, 0, 10}, {20, 0, 10}, {20, 0, 0}};
    m.faces.push_back({{0, 1, 2, 3, 4, 5}, {}, {}});
    REQUIRE(near(m.normal(m.faces[0]), {0, 1, 0}));
    const auto triangles = triangulate(m, m.faces[0]);
    REQUIRE(triangles.size() == 4);
    double area = 0;
    for (const auto& t : triangles) {
        const auto c = cross(m.points[t[1]] - m.points[t[0]], m.points[t[2]] - m.points[t[0]]);
        CHECK(c.y > 0);   // every ear keeps the face's winding
        area += length(c) / 2;
    }
    CHECK(area == doctest::Approx(300));
}

TEST_CASE("pushing a room's wall out makes a corridor seen from inside") {
    auto m = room();
    const auto east = *faceAt(m, {64, 64, 0});
    const auto made = extrude(m, {east}, -128);
    CHECK(made.size() == 4);
    CHECK(m.faces.size() == 10);
    CHECK(near(m.centre(m.faces[east]), {192, 64, 0}));
    CHECK(near(m.normal(m.faces[east]), {-1, 0, 0}));
    // The new walls, floor and ceiling face the corridor's middle line.
    for (const auto f : made) {
        const auto c = m.centre(m.faces[f]);
        const Vec3 toAxis{0, 64 - c.y, 0 - c.z};
        CHECK(dot(m.normal(m.faces[f]), toAxis) > 0);
        CHECK(m.faces[f].texture == m.faces[east].texture);
    }
    checkWinding(m);
    // The room's other walls stay; its corners are shared with the corridor's mouth.
    CHECK(faceAt(m, {0, 64, 128}).has_value());
}

TEST_CASE("pulling a solid's top out grows it, faces outward") {
    Mesh m;
    addBox(m, {{0, 0, 0}, {10, 10, 10}}, false, kAllSides, {});
    const auto top = *faceAt(m, {5, 10, 5});
    const auto made = extrude(m, {top}, 5);
    CHECK(near(m.centre(m.faces[top]), {5, 15, 5}));
    for (const auto f : made) {
        const auto c = m.centre(m.faces[f]);
        CHECK(dot(m.normal(m.faces[f]), Vec3{c.x - 5, 0, c.z - 5}) > 0);
    }
    checkWinding(m);
}

TEST_CASE("filling, cutting and welding") {
    Mesh m;
    m.points = {{0, 0, 0}, {10, 0, 10}, {10, 0, 0}, {0, 0, 10}};   // not in order round
    const auto f = fillFace(m, {0, 1, 2, 3}, {}, {5, 50, 5});
    REQUIRE(f);
    CHECK(near(m.normal(m.faces[*f]), {0, 1, 0}));   // toward the viewer above
    CHECK(triangulate(m, m.faces[*f]).size() == 2);
    CHECK_FALSE(fillFace(m, {0, 1}, {}, {}).has_value());

    // Cut along the diagonal 0-1 (not an edge of the ordered face).
    const auto half = splitFace(m, *f, 0, 1);
    REQUIRE(half);
    CHECK(m.faces[*f].points.size() == 3);
    CHECK(m.faces[*half].points.size() == 3);
    CHECK(near(m.normal(m.faces[*half]), {0, 1, 0}));
    CHECK_FALSE(splitFace(m, *f, 0, 0).has_value());

    // A point in the middle of the shared edge goes into both halves.
    const auto mid = splitEdge(m, 0, 1);
    REQUIRE(mid);
    CHECK(near(m.points[*mid], {5, 0, 5}));
    CHECK(m.faces[*f].points.size() == 4);
    CHECK(m.faces[*half].points.size() == 4);

    // Welding the middle onto corner 0 takes it back out of both.
    const auto kept = weld(m, {0, *mid}, m.points[0]);
    REQUIRE(kept);
    CHECK(m.points.size() == 4);
    CHECK(m.faces[0].points.size() == 3);
    CHECK(checkMesh(m).empty());
}

TEST_CASE("removing points and faces, merging by distance") {
    auto m = room();
    removeFaces(m, {0, 1});
    CHECK(m.faces.size() == 4);
    CHECK(checkMesh(m).empty());

    auto n = room();
    removePoints(n, {0});   // the corner goes from the three faces round it
    CHECK(n.points.size() == 7);
    CHECK(n.faces.size() == 6);
    std::size_t triangles = 0;
    for (const auto& f : n.faces) triangles += f.points.size() == 3 ? 1u : 0u;
    CHECK(triangles == 3);

    Mesh d;
    addQuad(d, {{{0, 0, 0}, {10, 0, 0}, {10, 0, 10}, {0, 0, 10}}}, {0, 1, 0}, {});
    addQuad(d, {{{10.2, 0, 0}, {20, 0, 0}, {20, 0, 10}, {10.2, 0, 10}}}, {0, 1, 0}, {});
    CHECK(d.points.size() == 8);
    CHECK(mergeByDistance(d, 0.5) == 2);
    CHECK(d.points.size() == 6);
}

TEST_CASE("moving, turning and mirroring points") {
    auto m = room();
    const auto all = pointsOf(m, {0, 1, 2, 3, 4, 5});
    turnPoints(m, all, {0, 0, 0}, 90);   // north to east
    CHECK(near(m.bounds().max, {128, 128, 64}, 1e-9));
    scalePoints(m, all, {0, 0, 0}, {-1, 1, 1});   // a mirror: faces turned back to be seen from inside
    const auto floor = *faceAt(m, {0, 0, 0});
    CHECK(near(m.normal(m.faces[floor]), {0, 1, 0}));
    checkWinding(m);
}

TEST_CASE("fitting a texture puts one copy of it on the face") {
    auto m = room();
    const auto east = *faceAt(m, {64, 64, 0});
    fitTexture(m, east, 64, 64);
    const auto& f = m.faces[east];
    const auto axes = uvAxes(m.normal(f), f.uv.angle);
    double u0 = 1e9, u1 = -1e9, v0 = 1e9, v1 = -1e9;
    for (const auto p : f.points) {
        const auto t = texelsAt(f.uv, axes, m.points[p]);
        u0 = std::min(u0, t[0]);
        u1 = std::max(u1, t[0]);
        v0 = std::min(v0, t[1]);
        v1 = std::max(v1, t[1]);
    }
    CHECK(u0 == doctest::Approx(0));
    CHECK(u1 == doctest::Approx(64));
    CHECK(v0 == doctest::Approx(0));
    CHECK(v1 == doctest::Approx(64));
}

TEST_CASE("undo, redo and gestures") {
    MeshEdit edit(room());
    CHECK_FALSE(edit.dirty());
    removeFaces(edit.change(), {0});
    CHECK(edit.mesh().faces.size() == 5);
    CHECK(edit.dirty());
    edit.beginGesture();
    movePoints(edit.change(), {0}, {1, 0, 0});
    movePoints(edit.change(), {0}, {1, 0, 0});
    edit.endGesture();
    REQUIRE(edit.undo());   // both moves at once
    CHECK(edit.mesh().faces.size() == 5);
    CHECK(edit.mesh() == [] { auto m = room(); removeFaces(m, {0}); return m; }());
    REQUIRE(edit.undo());
    CHECK(edit.mesh().faces.size() == 6);
    CHECK_FALSE(edit.undo());
    REQUIRE(edit.redo());
    CHECK(edit.mesh().faces.size() == 5);
    edit.beginGesture();
    movePoints(edit.change(), {0}, {5, 0, 0});
    edit.cancelGesture();
    CHECK(edit.canRedo());   // a cancelled gesture leaves the history alone
    edit.beginGesture();
    edit.endGesture();
    CHECK(edit.canRedo());   // nor does an empty one
}

TEST_CASE("the editor's JSON round-trips; Bunnyfall's is in metres with DFU's UVs") {
    auto m = room();
    m.faces[2].uv = {1.5, 3, 4, -2, 30};
    m.faces[3].texture = {168, 5};
    const auto back = meshFromJson(meshToJson(m));
    REQUIRE(back);
    CHECK(*back == m);
    CHECK_FALSE(meshFromJson(Json::object({{"points", {{0, 0, 0}}}, {"faces", {{{"points", {0, 1, 2}}, {"texture", {1, 2}}}}}})));

    const auto j = bunnyfallModelJson(room(), 7000001, "Corridor", [](int, int) { return std::pair{64, 64}; });
    CHECK(j["format"] == kModelFileFormat);
    CHECK(j["id"] == 7000001);
    CHECK(j["positions"].size() == 24 * 3);   // four corners on each of six faces
    CHECK(j["indices"].size() == 36);
    double top = -1e9;
    for (std::size_t i = 1; i < j["positions"].size(); i += 3) top = std::max(top, j["positions"][i].get<double>());
    CHECK(top == doctest::Approx(3.2));   // 128 units
    std::size_t triangles = 0;
    for (const auto& sm : j["subMeshes"]) triangles += sm["primitiveCount"].get<std::size_t>();
    CHECK(triangles == 12);
    // UVs: texels over the size, v negated (DFU's).
    const auto drawn = toModelMesh(room(), 1);
    CHECK(j["uvs"][0].get<double>() == doctest::Approx(drawn.vertices[0].u / 64.0));
    CHECK(j["uvs"][1].get<double>() == doctest::Approx(-drawn.vertices[0].v / 64.0));
}

TEST_CASE("custom models in the project: ids issued once, saved and loaded") {
    project::Project p;
    auto a = p.addModel("Corridor", "C00", room());
    REQUIRE(a);
    const auto first = (*a)->id;
    CHECK(first == project::kFirstCustomModelId + 1);
    CHECK(project::isCustomModelId(first));
    CHECK_FALSE(project::isCustomModelId(67000));
    auto b = p.addModel("Door", "DOREXTRA", {});
    REQUIRE(b);
    CHECK((*b)->description == "DOR");
    CHECK(p.removeModel(first + 1));
    auto c = p.addModel("Again", "C00", {});
    CHECK((*c)->id == first + 2);   // never handed out twice

    const auto file = std::filesystem::temp_directory_path() / "de_mesh_project_test.json";
    REQUIRE(p.save(file));
    auto loaded = project::Project::load(file);
    std::filesystem::remove(file);
    REQUIRE(loaded);
    REQUIRE(loaded->models().size() == 2);
    CHECK(loaded->model(first)->mesh == room());
    CHECK(loaded->model(first)->label == "Corridor");
    CHECK(loaded->modelsIssued() == 3);
}

TEST_CASE("the catalog's view of a custom model") {
    const auto info = modelInfoOf(room(), 7000001, "C00");
    CHECK(info.kind == catalog::ModelKind::Architecture);
    CHECK(info.bounds.size() == Vec3{128, 128, 256});
    CHECK(info.faces.size() == 6);
    CHECK(info.textureArchive == 120);
    CHECK(info.twins.empty());
    CHECK(modelInfoOf({}, 7000002, "DOR").kind == catalog::ModelKind::Door);
}

TEST_CASE("ARENA2: export writes each custom model; a block naming a missing one is an error") {
    DE_REQUIRE_ARENA2(a);
    auto catalog = catalog::Catalog::build(*a);
    REQUIRE(catalog);
    project::World world{*a, *catalog, *formats::readLocations(a->maps)};
    project::Project p;
    const auto id = (*p.addModel("Corridor", "C00", room()))->id;
    auto block = project::emptyBlock();
    block.modelRefs[0] = {std::to_string(id + 5), id + 5, "C00"};   // a model the project does not have
    formats::RdbObject obj;
    obj.resource = formats::RdbModel{};
    block.roots[0].objects.push_back(obj);
    REQUIRE(p.addBlock('N', "uses a lost model", std::nullopt, block));
    auto issues = project::checkProject(p, world);
    CHECK(std::ranges::any_of(issues, [](const auto& i) {
        return i.severity == project::Issue::Severity::Error && i.text.find("no longer has") != std::string::npos;
    }));

    p.blocks().front().data.modelRefs[0] = {std::to_string(id), id, "C00"};
    const auto dir = std::filesystem::temp_directory_path() / "de_mesh_export_test";
    std::filesystem::remove_all(dir);
    auto report = project::exportProject(p, world, dir);
    REQUIRE(report);
    CHECK_FALSE(report->hasErrors());
    const auto path = dir / project::modelFileName(id);
    REQUIRE(std::filesystem::exists(path));
    std::ifstream in(path);
    const auto j = Json::parse(in);
    CHECK(j["id"] == id);
    CHECK(j["subMeshes"].size() == 3);
    // Removed from the project, the next export takes the file away.
    REQUIRE(p.removeModel(id));
    p.blocks().clear();
    report = project::exportProject(p, world, dir);
    REQUIRE(report);
    CHECK_FALSE(std::filesystem::exists(path));
    std::filesystem::remove_all(dir);
}

namespace {
double faceArea(const Mesh& m, const Face& f) {
    Vec3 n;
    for (std::size_t i = 0; i < f.points.size(); ++i) n = n + cross(m.points[f.points[i]], m.points[f.points[(i + 1) % f.points.size()]]);
    return length(n) / 2;
}
} // namespace

TEST_CASE("a doorway cut in a wall: the wall goes round it, a reveal lines it, the floor gets its corners") {
    auto m = room();
    const auto east = *faceAt(m, {64, 64, 0});
    const auto wallTexture = m.faces[east].texture;
    const Opening door{.width = 64, .height = 96, .bottom = 0, .across = 0, .depth = 16};
    const auto corners = openingCorners(m, east, door);
    REQUIRE(corners);
    // Seen from inside the room (looking east), right is -Z: bottom-left is at +Z.
    CHECK(near((*corners)[0], {64, 0, 32}));
    CHECK(near((*corners)[2], {64, 96, -32}));

    const auto cut = cutOpening(m, east, door);
    REQUIRE(cut.pieces.size() == 3);   // left, right and above: the door starts at the floor
    CHECK(cut.pieces.front() == east);
    double area = 0;
    for (const auto f : cut.pieces) {
        area += faceArea(m, m.faces[f]);
        CHECK(near(m.normal(m.faces[f]), {-1, 0, 0}));
        CHECK(m.faces[f].texture == wallTexture);
    }
    CHECK(area == doctest::Approx(256 * 128 - 64 * 96));
    REQUIRE(cut.reveal.size() == 4);
    for (const auto f : cut.reveal) {
        const auto c = m.centre(m.faces[f]);
        CHECK(c.x == doctest::Approx(72));   // half the depth behind the wall
        CHECK(dot(m.normal(m.faces[f]), Vec3{0, 48 - c.y, 0 - c.z}) > 0);   // seen from inside the doorway
    }
    const auto sill = std::ranges::find_if(cut.reveal, [&](auto f) { return m.normal(m.faces[f]).y > 0.5; });
    REQUIRE(sill != cut.reveal.end());
    CHECK(m.faces[*sill].texture == FaceTexture{120, 2});
    // The floor's edge along the wall now runs through the door's two corners: no crack.
    std::size_t floorPoints = 0;
    for (const auto& f : m.faces) {
        if (m.normal(f).y > 0.5 && m.centre(f).x < 64) floorPoints = f.points.size();
    }
    CHECK(floorPoints == 6);
    for (const auto& f : m.faces) CHECK(triangulate(m, f).size() == f.points.size() - 2);   // every face covered
    checkWinding(m);
    CHECK(checkMesh(m).empty());
}

TEST_CASE("a window and a hole that misses") {
    auto m = room();
    const auto east = *faceAt(m, {64, 64, 0});
    const auto cut = cutOpening(m, east, {.width = 32, .height = 32, .bottom = 48, .across = 40, .depth = 0});
    CHECK(cut.pieces.size() == 4);
    CHECK(cut.reveal.empty());
    checkWinding(m);
    auto n = room();
    CHECK_FALSE(openingCorners(n, east, {.width = 32, .height = 32, .bottom = 500}).has_value());
    CHECK(cutOpening(n, east, {.width = 32, .height = 32, .bottom = 500}).pieces.empty());
    CHECK(n == room());
}

TEST_CASE("slicing a room's walls along a grid line") {
    auto m = room();
    mesh::FaceSet all;
    for (std::uint32_t f = 0; f < m.faces.size(); ++f) all.insert(f);
    const auto halves = sliceFaces(m, all, 1, 64);
    CHECK(halves.size() == 4);   // the four walls; the floor and ceiling lie wholly on one side
    CHECK(m.faces.size() == 10);
    CHECK(m.points.size() == 12);
    for (const auto& f : m.faces) CHECK(f.points.size() == 4);   // corners shared, nothing left hanging
    checkWinding(m);
    CHECK(checkMesh(m).empty());
}

TEST_CASE("edges: listed once, split, connected across a face, dissolved back") {
    auto m = room();
    CHECK(edgesOf(m).size() == 12);
    const auto east = *faceAt(m, {64, 64, 0});
    // The east wall's top and bottom edges.
    std::optional<Edge> top, bottom;
    const auto& wall = m.faces[east].points;
    for (std::size_t i = 0; i < wall.size(); ++i) {
        const auto e = edgeOf(wall[i], wall[(i + 1) % wall.size()]);
        const double y = (m.points[e.first].y + m.points[e.second].y) / 2;
        const double dz = std::abs(m.points[e.first].z - m.points[e.second].z);
        if (dz > 1 && y > 64) top = e;
        if (dz > 1 && y < 64) bottom = e;
    }
    REQUIRE(top);
    REQUIRE(bottom);
    CHECK(facesWithEdge(m, *top).size() == 2);   // the wall and the ceiling
    const auto half = connectEdges(m, *top, *bottom);
    REQUIRE(half);
    CHECK(m.faces.size() == 7);
    CHECK(m.faces[east].points.size() == 4);
    CHECK(m.faces[*half].points.size() == 4);
    const auto floor = std::ranges::find_if(m.faces, [&](const Face& f) { return m.normal(f).y > 0.5; });
    REQUIRE(floor != m.faces.end());
    CHECK(floor->points.size() == 5);   // the floor took the bottom edge's middle
    checkWinding(m);
    CHECK(checkMesh(m).empty());

    // The cut between the halves dissolves back into one wall.
    std::optional<Edge> cut;
    for (const auto& e : edgesOf(m)) {
        const auto faces = facesWithEdge(m, e);
        if (faces.size() == 2 && std::ranges::find(faces, east) != faces.end() && std::ranges::find(faces, *half) != faces.end()) cut = e;
    }
    REQUIRE(cut);
    REQUIRE(dissolveEdge(m, *cut));
    CHECK(m.faces.size() == 6);
    const auto merged = *faceAt(m, {64, 64, 0});
    CHECK(near(m.normal(m.faces[merged]), {-1, 0, 0}));
    CHECK(faceArea(m, m.faces[merged]) == doctest::Approx(256 * 128));
    checkWinding(m);
    // A corner edge (wall and floor) does not dissolve: the faces do not lie flat together.
    CHECK_FALSE(dissolveEdge(m, *bottom));
}

TEST_CASE("edges extend into new faces, seen from the side the face beside them is") {
    Mesh m;
    addQuad(m, {{{0, 0, 0}, {128, 0, 0}, {128, 0, 128}, {0, 0, 128}}}, {0, 1, 0}, {120, 2});
    // The north edge of a floor, grown out north and then up.
    const Edge north = edgeOf(2, 3);
    const auto out = outwardOf(m, north);
    REQUIRE(out);
    CHECK(near(*out, {0, 0, 1}));
    const auto more = extendEdges(m, {north}, *out * 64);
    REQUIRE(more.size() == 1);
    CHECK(near(m.normal(m.faces[more[0]]), {0, 1, 0}));   // more floor
    CHECK(m.faces[more[0]].texture == FaceTexture{120, 2});
    CHECK(near(m.centre(m.faces[more[0]]), {64, 0, 160}));
    checkWinding(m);
    CHECK(pointsOf(m, EdgeSet{north}).size() == 2);
}
