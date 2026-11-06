#include "mesh/ops.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <numbers>
#include <ranges>

namespace de::mesh {

namespace {

/// Adds points, reusing one already added through it at the same place (so faces made together share corners).
class PointMaker {
public:
    explicit PointMaker(Mesh& mesh) : mesh_(mesh) {}
    std::uint32_t operator()(Vec3 p) {
        const auto key = std::tuple{std::llround(p.x * 64), std::llround(p.y * 64), std::llround(p.z * 64)};
        if (const auto it = made_.find(key); it != made_.end()) return it->second;
        const auto i = static_cast<std::uint32_t>(mesh_.points.size());
        mesh_.points.push_back(p);
        made_.emplace(key, i);
        return i;
    }

private:
    Mesh& mesh_;
    std::map<std::tuple<long long, long long, long long>, std::uint32_t> made_;
};

/// Adds a face through `points`, reversed if needed so it is seen from the side `facing` points to.
std::uint32_t addFacing(Mesh& mesh, std::vector<std::uint32_t> points, Vec3 facing, FaceTexture texture) {
    Face f{std::move(points), texture, {}};
    if (dot(mesh.normal(f), facing) < 0) std::ranges::reverse(f.points);
    mesh.faces.push_back(std::move(f));
    return static_cast<std::uint32_t>(mesh.faces.size() - 1);
}

/// The texture a face of a box gets from which way it is seen.
FaceTexture boxTexture(Vec3 facing, const BoxTextures& t) {
    if (facing.y > 0.5) return t.floor;
    if (facing.y < -0.5) return t.ceiling;
    return t.walls;
}

/// Drops repeated neighbours round each face, then faces left with under three points.
void tidyFaces(Mesh& mesh) {
    for (auto& f : mesh.faces) {
        auto& p = f.points;
        p.erase(std::unique(p.begin(), p.end()), p.end());
        while (p.size() > 1 && p.front() == p.back()) p.pop_back();
    }
    std::erase_if(mesh.faces, [](const Face& f) { return f.points.size() < 3; });
}


/// Keeps the part of a polygon where dot(p, dir) is at most `limit` (or at least, with `atLeast`): one
/// Sutherland-Hodgman pass. New corners land exactly on the line.
std::vector<Vec3> clipPolygon(const std::vector<Vec3>& poly, Vec3 dir, double limit, bool atLeast) {
    std::vector<Vec3> out;
    auto side = [&](Vec3 p) { return atLeast ? dot(p, dir) - limit : limit - dot(p, dir); };   // >= 0 inside
    for (std::size_t i = 0; i < poly.size(); ++i) {
        const Vec3 a = poly[i], b = poly[(i + 1) % poly.size()];
        const double sa = side(a), sb = side(b);
        if (sa >= -1e-9) out.push_back(a);
        if ((sa > 1e-9 && sb < -1e-9) || (sa < -1e-9 && sb > 1e-9)) out.push_back(a + (b - a) * (sa / (sa - sb)));
    }
    return out;
}

double polygonArea(const std::vector<Vec3>& poly) {
    Vec3 n;
    for (std::size_t i = 0; i < poly.size(); ++i) n = n + cross(poly[i], poly[(i + 1) % poly.size()]);
    return length(n) / 2;
}

/// The mesh point at `p` (one already there, within a hair), else a new one, noted in `made`.
std::uint32_t pointAt(Mesh& mesh, Vec3 p, std::vector<std::uint32_t>& made) {
    for (std::uint32_t i = 0; i < mesh.points.size(); ++i) {
        if (length(mesh.points[i] - p) < 1e-4) return i;
    }
    mesh.points.push_back(p);
    made.push_back(static_cast<std::uint32_t>(mesh.points.size() - 1));
    return made.back();
}

/// Puts each of `points` into every face edge it lies on (inside the edge, not at its ends), in order along it, so
/// a cut leaves no T-junction for the renderer to crack along.
void insertOnEdges(Mesh& mesh, const std::vector<std::uint32_t>& points) {
    if (points.empty()) return;
    for (auto& f : mesh.faces) {
        std::vector<std::uint32_t> out;
        for (std::size_t i = 0; i < f.points.size(); ++i) {
            const auto a = f.points[i], b = f.points[(i + 1) % f.points.size()];
            out.push_back(a);
            const Vec3 pa = mesh.points[a], ab = mesh.points[b] - pa;
            const double len2 = dot(ab, ab);
            if (len2 < 1e-12) continue;
            std::vector<std::pair<double, std::uint32_t>> on;
            for (const auto p : points) {
                if (p == a || p == b) continue;
                const double t = dot(mesh.points[p] - pa, ab) / len2;
                if (t <= 1e-6 || t >= 1 - 1e-6) continue;
                if (length(pa + ab * t - mesh.points[p]) < 1e-3) on.emplace_back(t, p);
            }
            std::ranges::sort(on);
            for (const auto& [_, p] : on) out.push_back(p);
        }
        f.points = std::move(out);
    }
}

/// A face's plane directions for cutting: across (the texture's u, laid flat in the plane) and up it.
std::pair<Vec3, Vec3> cutAxes(Vec3 n) {
    const Vec3 u = uvAxes(n, 0).u;
    const Vec3 across = normalised(u - n * dot(u, n));
    return {across, cross(across, n)};
}

/// Replaces face `index` by `pieces` (polygons on its plane); answers the faces they became.
std::vector<std::uint32_t> replaceByPieces(Mesh& mesh, std::uint32_t index, const std::vector<std::vector<Vec3>>& pieces,
                                           std::vector<std::uint32_t>& made) {
    const Face original = mesh.faces[index];
    const Vec3 n = mesh.normal(original);
    std::vector<std::uint32_t> faces;
    for (const auto& poly : pieces) {
        if (poly.size() < 3 || polygonArea(poly) < 1e-3) continue;
        Face f{{}, original.texture, original.uv};
        for (const auto& p : poly) {
            const auto i = pointAt(mesh, p, made);
            if (f.points.empty() || (f.points.back() != i && f.points.front() != i)) f.points.push_back(i);
        }
        if (f.points.size() < 3) continue;
        if (dot(mesh.normal(f), n) < 0) std::ranges::reverse(f.points);
        if (faces.empty()) {
            mesh.faces[index] = std::move(f);
            faces.push_back(index);
        } else {
            mesh.faces.push_back(std::move(f));
            faces.push_back(static_cast<std::uint32_t>(mesh.faces.size() - 1));
        }
    }
    return faces;
}

} // namespace

std::vector<std::uint32_t> addBox(Mesh& mesh, const catalog::Box& box, bool inward, const std::array<bool, 6>& sides,
                                  const BoxTextures& textures) {
    PointMaker point(mesh);
    auto corner = [&](int i) {
        return Vec3{(i & 1) ? box.max.x : box.min.x, (i & 2) ? box.max.y : box.min.y, (i & 4) ? box.max.z : box.min.z};
    };
    // Each side's corners in order round it, and the way out of the box through it.
    static constexpr std::array<std::array<int, 4>, 6> kCorners = {{
        {0, 2, 6, 4}, {1, 3, 7, 5}, {0, 1, 5, 4}, {2, 3, 7, 6}, {0, 1, 3, 2}, {4, 5, 7, 6},
    }};
    static constexpr std::array<Vec3, 6> kOut = {{{-1, 0, 0}, {1, 0, 0}, {0, -1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}}};
    std::vector<std::uint32_t> made;
    for (std::size_t s = 0; s < 6; ++s) {
        if (!sides[s]) continue;
        std::vector<std::uint32_t> points;
        for (const int c : kCorners[s]) points.push_back(point(corner(c)));
        const Vec3 facing = inward ? kOut[s] * -1 : kOut[s];
        made.push_back(addFacing(mesh, std::move(points), facing, boxTexture(facing, textures)));
    }
    return made;
}

std::uint32_t addQuad(Mesh& mesh, const std::array<Vec3, 4>& corners, Vec3 facing, FaceTexture texture) {
    PointMaker point(mesh);
    std::vector<std::uint32_t> points;
    for (const auto& c : corners) points.push_back(point(c));
    return addFacing(mesh, std::move(points), facing, texture);
}

std::vector<std::uint32_t> addStairs(Mesh& mesh, const catalog::Box& box, int steps, const BoxTextures& textures) {
    std::vector<std::uint32_t> made;
    if (steps < 1) return made;
    PointMaker point(mesh);
    const auto size = box.size();
    const double rise = size.y / steps, run = size.z / steps;
    for (int i = 0; i < steps; ++i) {
        const double y0 = box.min.y + rise * i, y1 = y0 + rise;
        const double z0 = box.min.z + run * i, z1 = z0 + run;
        made.push_back(addFacing(mesh,
                                 {point({box.min.x, y0, z0}), point({box.max.x, y0, z0}), point({box.max.x, y1, z0}),
                                  point({box.min.x, y1, z0})},
                                 {0, 0, -1}, textures.walls));
        made.push_back(addFacing(mesh,
                                 {point({box.min.x, y1, z0}), point({box.max.x, y1, z0}), point({box.max.x, y1, z1}),
                                  point({box.min.x, y1, z1})},
                                 {0, 1, 0}, textures.floor));
    }
    return made;
}

namespace {

// extrude, also at no distance (the start of a drag out).
std::vector<std::uint32_t> extrudeFaces(Mesh& mesh, const FaceSet& faces, double distance) {
    std::vector<std::uint32_t> made;
    if (faces.empty()) return made;
    for (const auto f : faces) {
        if (f >= mesh.faces.size()) return made;
    }
    // Each point moves along the average normal of the chosen faces using it (one normal for a flat region).
    std::map<std::uint32_t, Vec3> normals;
    for (const auto f : faces) {
        const Vec3 n = mesh.normal(mesh.faces[f]);
        for (const auto p : mesh.faces[f].points) normals[p] = normals[p] + n;
    }
    std::map<std::uint32_t, std::uint32_t> moved;
    for (const auto& [p, n] : normals) {
        moved[p] = static_cast<std::uint32_t>(mesh.points.size());
        mesh.points.push_back(mesh.points[p] + normalised(n) * distance);
    }
    // The region's rim: edges only one chosen face has.
    std::map<std::pair<std::uint32_t, std::uint32_t>, int> edgeUses;
    auto key = [](std::uint32_t a, std::uint32_t b) { return std::pair{std::min(a, b), std::max(a, b)}; };
    for (const auto f : faces) {
        const auto& pts = mesh.faces[f].points;
        for (std::size_t i = 0; i < pts.size(); ++i) ++edgeUses[key(pts[i], pts[(i + 1) % pts.size()])];
    }
    // A side face across rim edge a->b of a chosen face goes b', a', a, b: it meets the moved face along a'->b' and
    // whatever bordered a->b the other way round, so the surface stays seen from one side throughout.
    std::vector<Face> sides;
    for (const auto f : faces) {
        const auto& face = mesh.faces[f];
        const auto& pts = face.points;
        for (std::size_t i = 0; i < pts.size(); ++i) {
            const auto a = pts[i], b = pts[(i + 1) % pts.size()];
            if (edgeUses[key(a, b)] != 1) continue;
            sides.push_back({{moved[b], moved[a], a, b}, face.texture, face.uv});
        }
    }
    for (const auto f : faces) {
        for (auto& p : mesh.faces[f].points) p = moved[p];
    }
    for (auto& s : sides) {
        mesh.faces.push_back(std::move(s));
        made.push_back(static_cast<std::uint32_t>(mesh.faces.size() - 1));
    }
    return made;
}

} // namespace

std::vector<std::uint32_t> extrude(Mesh& mesh, const FaceSet& faces, double distance) {
    if (distance == 0) return {};
    return extrudeFaces(mesh, faces, distance);
}

std::optional<std::uint32_t> fillFace(Mesh& mesh, const PointSet& given, FaceTexture texture, Vec3 viewer) {
    // One point for each place: corners snapped onto each other are often picked together, and a face through both
    // would have an edge of no length.
    PointSet points;
    for (const auto p : given) {
        if (p >= mesh.points.size()) return std::nullopt;
        const bool stacked = std::ranges::any_of(points, [&](std::uint32_t q) { return length(mesh.points[p] - mesh.points[q]) < 1e-3; });
        if (!stacked) points.insert(p);
    }
    if (points.size() < 3) return std::nullopt;
    Vec3 c;
    for (const auto p : points) c = c + mesh.points[p];
    c = c * (1.0 / double(points.size()));
    // The plane: the widest triangle the centre makes with two of the points.
    Vec3 best;
    for (const auto a : points) {
        for (const auto b : points) {
            const Vec3 n = cross(mesh.points[a] - c, mesh.points[b] - c);
            if (length(n) > length(best)) best = n;
        }
    }
    if (length(best) < 1e-6) return std::nullopt;
    const Vec3 n = normalised(best);
    Vec3 e1;
    for (const auto p : points) {
        const Vec3 d = mesh.points[p] - c;
        const Vec3 flat = d - n * dot(d, n);
        if (length(flat) > length(e1)) e1 = flat;
    }
    e1 = normalised(e1);
    const Vec3 e2 = cross(n, e1);
    std::vector<std::pair<double, std::uint32_t>> round;
    for (const auto p : points) {
        const Vec3 d = mesh.points[p] - c;
        round.emplace_back(std::atan2(dot(d, e2), dot(d, e1)), p);
    }
    std::ranges::sort(round);
    std::vector<std::uint32_t> ordered;
    for (const auto& [_, p] : round) ordered.push_back(p);
    return addFacing(mesh, std::move(ordered), viewer - c, texture);
}

std::optional<std::uint32_t> splitFace(Mesh& mesh, std::uint32_t face, std::uint32_t a, std::uint32_t b) {
    if (face >= mesh.faces.size()) return std::nullopt;
    const auto pts = mesh.faces[face].points;
    const auto ia = std::ranges::find(pts, a), ib = std::ranges::find(pts, b);
    if (ia == pts.end() || ib == pts.end() || a == b) return std::nullopt;
    const auto n = pts.size();
    const auto i = static_cast<std::size_t>(ia - pts.begin()), j = static_cast<std::size_t>(ib - pts.begin());
    if ((i + 1) % n == j || (j + 1) % n == i) return std::nullopt;   // already an edge
    auto walk = [&](std::size_t from, std::size_t to) {
        std::vector<std::uint32_t> out;
        for (std::size_t k = from;; k = (k + 1) % n) {
            out.push_back(pts[k]);
            if (k == to) break;
        }
        return out;
    };
    Face second = mesh.faces[face];
    mesh.faces[face].points = walk(i, j);
    second.points = walk(j, i);
    mesh.faces.push_back(std::move(second));
    return static_cast<std::uint32_t>(mesh.faces.size() - 1);
}

std::optional<std::uint32_t> splitEdge(Mesh& mesh, std::uint32_t a, std::uint32_t b) {
    if (a >= mesh.points.size() || b >= mesh.points.size() || a == b) return std::nullopt;
    const auto m = static_cast<std::uint32_t>(mesh.points.size());
    bool used = false;
    for (auto& f : mesh.faces) {
        auto& p = f.points;
        for (std::size_t i = 0; i < p.size(); ++i) {
            const auto x = p[i], y = p[(i + 1) % p.size()];
            if ((x == a && y == b) || (x == b && y == a)) {
                p.insert(p.begin() + static_cast<std::ptrdiff_t>(i + 1), m);
                used = true;
                break;
            }
        }
    }
    if (!used) return std::nullopt;
    mesh.points.push_back((mesh.points[a] + mesh.points[b]) * 0.5);
    return m;
}

std::optional<std::array<Vec3, 4>> openingCorners(const Mesh& mesh, std::uint32_t face, const Opening& o) {
    if (face >= mesh.faces.size()) return std::nullopt;
    const auto& f = mesh.faces[face];
    const Vec3 n = mesh.normal(f);
    if (length(n) == 0) return std::nullopt;
    const auto [across, up] = cutAxes(n);
    double s0 = 1e300, s1 = -1e300, t0 = 1e300, t1 = -1e300;
    for (const auto p : f.points) {
        s0 = std::min(s0, dot(mesh.points[p], across));
        s1 = std::max(s1, dot(mesh.points[p], across));
        t0 = std::min(t0, dot(mesh.points[p], up));
        t1 = std::max(t1, dot(mesh.points[p], up));
    }
    const double middle = (s0 + s1) / 2 + o.across;
    const double a = std::max(s0, middle - o.width / 2), b = std::min(s1, middle + o.width / 2);
    const double c = std::max(t0, t0 + o.bottom), d = std::min(t1, t0 + o.bottom + o.height);
    if (b - a < 1e-3 || d - c < 1e-3) return std::nullopt;
    const Vec3 centre = mesh.centre(f);
    auto at = [&](double s, double t) { return centre + across * (s - dot(centre, across)) + up * (t - dot(centre, up)); };
    return std::array<Vec3, 4>{at(a, c), at(b, c), at(b, d), at(a, d)};
}

CutResult cutOpening(Mesh& mesh, std::uint32_t face, const Opening& opening) {
    CutResult result;
    const auto corners = openingCorners(mesh, face, opening);
    if (!corners) return result;
    const auto& f = mesh.faces[face];
    const Vec3 n = mesh.normal(f);
    const auto [across, up] = cutAxes(n);
    const double a = dot((*corners)[0], across), b = dot((*corners)[1], across);
    const double c = dot((*corners)[0], up), d = dot((*corners)[3], up);
    std::vector<Vec3> poly;
    for (const auto p : f.points) poly.push_back(mesh.points[p]);
    // Round the hole: the whole height left and right of it, then below and above it between those.
    const auto middle = clipPolygon(clipPolygon(poly, across, a, true), across, b, false);
    const std::vector<std::vector<Vec3>> pieces = {
        clipPolygon(poly, across, a, false),
        clipPolygon(poly, across, b, true),
        clipPolygon(middle, up, c, false),
        clipPolygon(middle, up, d, true),
    };
    const FaceTexture texture = f.texture;
    std::vector<std::uint32_t> made;
    result.pieces = replaceByPieces(mesh, face, pieces, made);
    if (result.pieces.empty()) {   // the opening covers the whole face: it simply goes
        mesh.faces.erase(mesh.faces.begin() + face);
    }
    std::array<std::uint32_t, 4> rim{};
    for (std::size_t i = 0; i < 4; ++i) rim[i] = pointAt(mesh, (*corners)[i], made);
    if (opening.depth > 0) {
        const Vec3 holeCentre = ((*corners)[0] + (*corners)[2]) * 0.5;
        const BoxTextures textures{texture, {120, 2}, {120, 3}};
        for (std::size_t i = 0; i < 4; ++i) {
            const Vec3 pa = (*corners)[i], pb = (*corners)[(i + 1) % 4];
            const Vec3 facing = holeCentre - (pa + pb) * 0.5;
            const auto backA = pointAt(mesh, pa - n * opening.depth, made);
            const auto backB = pointAt(mesh, pb - n * opening.depth, made);
            result.reveal.push_back(addFacing(mesh, {rim[i], rim[(i + 1) % 4], backB, backA}, facing,
                                              boxTexture(normalised(facing), textures)));
        }
    }
    insertOnEdges(mesh, made);
    removeUnusedPoints(mesh);
    return result;
}

std::vector<std::uint32_t> sliceFaces(Mesh& mesh, const FaceSet& faces, int axis, double at) {
    std::vector<std::uint32_t> halves;
    const Vec3 dir = axis == 0 ? Vec3{1, 0, 0} : axis == 1 ? Vec3{0, 1, 0} : Vec3{0, 0, 1};
    std::vector<std::uint32_t> made;
    for (const auto f : faces) {
        if (f >= mesh.faces.size()) continue;
        std::vector<Vec3> poly;
        bool below = false, above = false;
        for (const auto p : mesh.faces[f].points) {
            poly.push_back(mesh.points[p]);
            const double side = dot(mesh.points[p], dir) - at;
            below = below || side < -1e-6;
            above = above || side > 1e-6;
        }
        if (!below || !above) continue;
        const auto parts = replaceByPieces(mesh, f, {clipPolygon(poly, dir, at, false), clipPolygon(poly, dir, at, true)}, made);
        halves.insert(halves.end(), parts.begin() + 1, parts.end());
    }
    insertOnEdges(mesh, made);
    return halves;
}

EdgeSet edgesOf(const Mesh& mesh) {
    EdgeSet out;
    for (const auto& f : mesh.faces) {
        for (std::size_t i = 0; i < f.points.size(); ++i) out.insert(edgeOf(f.points[i], f.points[(i + 1) % f.points.size()]));
    }
    return out;
}

std::vector<std::uint32_t> facesWithEdge(const Mesh& mesh, Edge edge) {
    std::vector<std::uint32_t> out;
    for (std::uint32_t fi = 0; fi < mesh.faces.size(); ++fi) {
        const auto& p = mesh.faces[fi].points;
        for (std::size_t i = 0; i < p.size(); ++i) {
            if (edgeOf(p[i], p[(i + 1) % p.size()]) == edge) {
                out.push_back(fi);
                break;
            }
        }
    }
    return out;
}

PointSet pointsOf(const Mesh& mesh, const EdgeSet& edges) {
    PointSet out;
    for (const auto& [a, b] : edges) {
        if (a < mesh.points.size()) out.insert(a);
        if (b < mesh.points.size()) out.insert(b);
    }
    return out;
}

std::optional<Vec3> outwardOf(const Mesh& mesh, Edge edge) {
    const auto faces = facesWithEdge(mesh, edge);
    if (faces.empty()) return std::nullopt;
    const auto& f = mesh.faces[faces.front()];
    const Vec3 a = mesh.points[edge.first], b = mesh.points[edge.second];
    const Vec3 along = normalised(b - a);
    Vec3 out = normalised(cross(along, mesh.normal(f)));   // square to the edge, in the face's plane
    if (dot(out, (a + b) * 0.5 - mesh.centre(f)) < 0) out = out * -1;
    return out;
}

namespace {

// extendEdges, also with no offset (the start of a drag out).
std::vector<std::uint32_t> growEdges(Mesh& mesh, const EdgeSet& edges, Vec3 offset) {
    std::vector<std::uint32_t> made;
    std::map<std::uint32_t, std::uint32_t> moved;   // points edges share move once
    auto movedOf = [&](std::uint32_t p) {
        auto [it, fresh] = moved.try_emplace(p, static_cast<std::uint32_t>(mesh.points.size()));
        if (fresh) mesh.points.push_back(mesh.points[p] + offset);
        return it->second;
    };
    for (const auto& edge : edges) {
        if (edge.first >= mesh.points.size() || edge.second >= mesh.points.size()) continue;
        // The way round the bordering face goes along this edge, so the new face meets it the other way round.
        std::uint32_t a = edge.first, b = edge.second;
        Vec3 facing{0, 1, 0};
        if (const auto faces = facesWithEdge(mesh, edge); !faces.empty()) {
            const auto& p = mesh.faces[faces.front()].points;
            for (std::size_t i = 0; i < p.size(); ++i) {
                if (edgeOf(p[i], p[(i + 1) % p.size()]) == edge) {
                    a = p[i];
                    b = p[(i + 1) % p.size()];
                    break;
                }
            }
            facing = mesh.normal(mesh.faces[faces.front()]);
        }
        Face f{{b, a, movedOf(a), movedOf(b)}, {}, {}};
        if (const auto faces = facesWithEdge(mesh, edge); !faces.empty()) {
            f.texture = mesh.faces[faces.front()].texture;
            f.uv = mesh.faces[faces.front()].uv;
        } else if (dot(mesh.normal(f), facing) < 0) {
            std::ranges::reverse(f.points);
        }
        mesh.faces.push_back(std::move(f));
        made.push_back(static_cast<std::uint32_t>(mesh.faces.size() - 1));
    }
    return made;
}

} // namespace

std::vector<std::uint32_t> extendEdges(Mesh& mesh, const EdgeSet& edges, Vec3 offset) {
    if (length(offset) == 0) return {};
    return growEdges(mesh, edges, offset);
}

Extrusion extrudeFacesInPlace(Mesh& mesh, const FaceSet& faces) {
    Extrusion out;
    if (std::ranges::any_of(faces, [&](std::uint32_t f) { return f >= mesh.faces.size(); })) return out;
    out.faces = extrudeFaces(mesh, faces, 0);
    out.moving = pointsOf(mesh, faces);
    return out;
}

Extrusion extrudeEdgesInPlace(Mesh& mesh, const EdgeSet& edges) {
    Extrusion out;
    out.faces = growEdges(mesh, edges, {});
    for (const auto f : out.faces) {
        const auto& p = mesh.faces[f].points;
        out.moving.insert(p[2]);
        out.moving.insert(p[3]);
        out.edges.insert(edgeOf(p[2], p[3]));
        out.originals[p[2]] = p[1];
        out.originals[p[3]] = p[0];
    }
    return out;
}

Extrusion extrudePointsInPlace(Mesh& mesh, const PointSet& points) {
    // The edges of faces with both ends chosen grow as edges do.
    EdgeSet edges;
    for (const auto& edge : edgesOf(mesh)) {
        if (points.contains(edge.first) && points.contains(edge.second)) edges.insert(edge);
    }
    auto out = extrudeEdgesInPlace(mesh, edges);
    // Their far ends are the chosen points' copies; points on no such edge are copied loose.
    std::map<std::uint32_t, std::uint32_t> copyOf;
    for (const auto f : out.faces) {
        const auto& p = mesh.faces[f].points;   // b, a, a', b'
        copyOf[p[1]] = p[2];
        copyOf[p[0]] = p[3];
    }
    for (const auto p : points) {
        if (p >= mesh.points.size() || copyOf.contains(p)) continue;
        const auto copy = static_cast<std::uint32_t>(mesh.points.size());
        mesh.points.push_back(mesh.points[p]);
        copyOf[p] = copy;
        out.moving.insert(copy);
        out.originals[copy] = p;
    }
    return out;
}

namespace {

/// The normals of the faces round each point, each way counted once, so three faces on one side and one on the other
/// do not lean the corner toward the three.
using PointNormals = std::map<std::uint32_t, std::vector<Vec3>>;

void addNormal(PointNormals& normals, std::uint32_t point, Vec3 normal) {
    auto& seen = normals[point];
    if (std::ranges::none_of(seen, [&](Vec3 n) { return dot(n, normal) > 0.9999; })) seen.push_back(normal);
}

PointDirections directionsFrom(const PointNormals& normals) {
    constexpr double kSharpest = 0.25;   // at most four times as far, however sharp the corner
    PointDirections out;
    for (const auto& [point, seen] : normals) {
        Vec3 sum;
        for (const auto n : seen) sum = sum + n;
        // Faces back to back cancel out: go the way of the first, with no corner to stretch to.
        if (length(sum) < 1e-6) {
            out[point] = seen.front();
            continue;
        }
        const Vec3 way = normalised(sum);
        double least = 1;
        for (const auto n : seen) least = std::min(least, dot(way, n));
        out[point] = way * (1 / std::max(least, kSharpest));
    }
    return out;
}

} // namespace

PointDirections directionsOfFaces(const Mesh& mesh, const FaceSet& faces) {
    PointNormals normals;
    for (const auto f : faces) {
        if (f >= mesh.faces.size()) continue;
        const Vec3 n = mesh.normal(mesh.faces[f]);
        if (length(n) < 0.5) continue;   // no area: it has no normal
        for (const auto p : mesh.faces[f].points) addNormal(normals, p, n);
    }
    return directionsFrom(normals);
}

PointDirections directionsOfEdges(const Mesh& mesh, const EdgeSet& edges) {
    PointNormals normals;
    for (const auto& edge : edges) {
        for (const auto f : facesWithEdge(mesh, edge)) {
            const Vec3 n = mesh.normal(mesh.faces[f]);
            if (length(n) < 0.5) continue;
            addNormal(normals, edge.first, n);
            addNormal(normals, edge.second, n);
        }
    }
    return directionsFrom(normals);
}

PointDirections directionsOfPoints(const Mesh& mesh, const PointSet& points) {
    PointNormals normals;
    for (const auto& face : mesh.faces) {
        const Vec3 n = mesh.normal(face);
        if (length(n) < 0.5) continue;
        for (const auto p : face.points) {
            if (points.contains(p)) addNormal(normals, p, n);
        }
    }
    return directionsFrom(normals);
}

bool dissolveEdge(Mesh& mesh, Edge edge) {
    const auto faces = facesWithEdge(mesh, edge);
    if (faces.size() != 2) return false;
    const auto& f1 = mesh.faces[faces[0]];
    const auto& f2 = mesh.faces[faces[1]];
    if (dot(mesh.normal(f1), mesh.normal(f2)) < 0.999) return false;
    // Walk the first face from the edge's far end round to its near end, then the second the same way: the shared
    // edge is left out and the rest joins up.
    auto walk = [&](const Face& f) {
        const auto& p = f.points;
        std::size_t i = 0;
        while (edgeOf(p[i], p[(i + 1) % p.size()]) != edge) ++i;
        std::vector<std::uint32_t> out;
        for (std::size_t k = 1; k <= p.size(); ++k) out.push_back(p[(i + k) % p.size()]);   // the edge's far end round to its near end
        return out;
    };
    auto merged = walk(f1);
    const auto rest = walk(f2);
    // f1 gives far end .. near end; f2, running the edge the other way round, gives near end .. far end. Joined
    // without the repeats, they go round both faces at once.
    merged.insert(merged.end(), rest.begin() + 1, rest.end());
    merged.pop_back();   // back at the start
    Face joined{std::move(merged), f1.texture, f1.uv};
    const auto keep = std::min(faces[0], faces[1]), drop = std::max(faces[0], faces[1]);
    mesh.faces[keep] = std::move(joined);
    mesh.faces.erase(mesh.faces.begin() + drop);
    tidyFaces(mesh);
    return true;
}

std::optional<std::uint32_t> connectEdges(Mesh& mesh, Edge first, Edge second) {
    if (first == second) return std::nullopt;
    const auto a = facesWithEdge(mesh, first), b = facesWithEdge(mesh, second);
    std::optional<std::uint32_t> face;
    for (const auto f : a) {
        if (std::ranges::find(b, f) != b.end()) face = f;
    }
    if (!face) return std::nullopt;
    const auto m1 = splitEdge(mesh, first.first, first.second);
    const auto m2 = splitEdge(mesh, second.first, second.second);
    if (!m1 || !m2) return std::nullopt;
    return splitFace(mesh, *face, *m1, *m2);
}

std::optional<std::uint32_t> weld(Mesh& mesh, const PointSet& points, Vec3 at) {
    if (points.empty() || *points.rbegin() >= mesh.points.size()) return std::nullopt;
    const auto kept = *points.begin();
    mesh.points[kept] = at;
    for (auto& f : mesh.faces) {
        for (auto& p : f.points) {
            if (points.contains(p)) p = kept;
        }
    }
    tidyFaces(mesh);
    const auto remap = removeUnusedPoints(mesh);
    return remap[kept];
}

std::size_t mergeOnto(Mesh& mesh, const PointSet& moved, double tolerance) {
    std::vector<std::uint32_t> to(mesh.points.size());
    for (std::uint32_t i = 0; i < to.size(); ++i) to[i] = i;
    std::size_t merged = 0;
    for (const auto p : moved) {
        if (p >= mesh.points.size()) continue;
        for (std::uint32_t q = 0; q < mesh.points.size(); ++q) {
            if (q == p || moved.contains(q) || length(mesh.points[p] - mesh.points[q]) > tolerance) continue;
            to[p] = q;
            ++merged;
            break;
        }
    }
    if (merged == 0) return 0;
    for (auto& f : mesh.faces) {
        for (auto& p : f.points) p = to[p];
    }
    tidyFaces(mesh);
    removeUnusedPoints(mesh);
    return merged;
}

std::size_t mergeByDistance(Mesh& mesh, double tolerance) {
    std::vector<std::uint32_t> to(mesh.points.size());
    std::size_t merged = 0;
    for (std::uint32_t i = 0; i < to.size(); ++i) {
        to[i] = i;
        for (std::uint32_t j = 0; j < i; ++j) {
            if (to[j] == j && length(mesh.points[i] - mesh.points[j]) <= tolerance) {
                to[i] = j;
                ++merged;
                break;
            }
        }
    }
    if (merged == 0) return 0;
    for (auto& f : mesh.faces) {
        for (auto& p : f.points) p = to[p];
    }
    tidyFaces(mesh);
    removeUnusedPoints(mesh);
    return merged;
}

void removeFaces(Mesh& mesh, const FaceSet& faces) {
    for (const auto f : faces | std::views::reverse) {
        if (f < mesh.faces.size()) mesh.faces.erase(mesh.faces.begin() + static_cast<std::ptrdiff_t>(f));
    }
    removeUnusedPoints(mesh);
}

void removePoints(Mesh& mesh, const PointSet& points) {
    for (auto& f : mesh.faces) std::erase_if(f.points, [&](std::uint32_t p) { return points.contains(p); });
    tidyFaces(mesh);
    // The points themselves go even if a face kept them (it cannot, now) or none ever used them.
    std::vector<std::optional<std::uint32_t>> remap(mesh.points.size());
    std::vector<Vec3> kept;
    for (std::uint32_t i = 0; i < mesh.points.size(); ++i) {
        if (points.contains(i)) continue;
        remap[i] = static_cast<std::uint32_t>(kept.size());
        kept.push_back(mesh.points[i]);
    }
    mesh.points = std::move(kept);
    for (auto& f : mesh.faces) {
        for (auto& p : f.points) p = *remap[p];
    }
    removeUnusedPoints(mesh);
}

void flipFaces(Mesh& mesh, const FaceSet& faces) {
    for (const auto f : faces) {
        if (f < mesh.faces.size()) std::ranges::reverse(mesh.faces[f].points);
    }
}

std::vector<std::uint32_t> duplicateFaces(Mesh& mesh, const FaceSet& faces, Vec3 offset) {
    std::vector<std::uint32_t> made;
    std::map<std::uint32_t, std::uint32_t> copied;
    for (const auto f : faces) {
        if (f >= mesh.faces.size()) continue;
        Face copy = mesh.faces[f];
        for (auto& p : copy.points) {
            auto [it, fresh] = copied.try_emplace(p, static_cast<std::uint32_t>(mesh.points.size()));
            if (fresh) mesh.points.push_back(mesh.points[p] + offset);
            p = it->second;
        }
        mesh.faces.push_back(std::move(copy));
        made.push_back(static_cast<std::uint32_t>(mesh.faces.size() - 1));
    }
    return made;
}

std::vector<std::optional<std::uint32_t>> removeUnusedPoints(Mesh& mesh) {
    std::vector<bool> used(mesh.points.size(), false);
    for (const auto& f : mesh.faces) {
        for (const auto p : f.points) used[p] = true;
    }
    std::vector<std::optional<std::uint32_t>> remap(mesh.points.size());
    std::vector<Vec3> kept;
    for (std::uint32_t i = 0; i < mesh.points.size(); ++i) {
        if (!used[i]) continue;
        remap[i] = static_cast<std::uint32_t>(kept.size());
        kept.push_back(mesh.points[i]);
    }
    mesh.points = std::move(kept);
    for (auto& f : mesh.faces) {
        for (auto& p : f.points) p = *remap[p];
    }
    return remap;
}

PointSet pointsOf(const Mesh& mesh, const FaceSet& faces) {
    PointSet out;
    for (const auto f : faces) {
        if (f < mesh.faces.size()) out.insert(mesh.faces[f].points.begin(), mesh.faces[f].points.end());
    }
    return out;
}

void movePoints(Mesh& mesh, const PointSet& points, Vec3 offset) {
    for (const auto p : points) {
        if (p < mesh.points.size()) mesh.points[p] = mesh.points[p] + offset;
    }
}

void turnPoints(Mesh& mesh, const PointSet& points, Vec3 centre, double degrees) {
    const double a = degrees * std::numbers::pi / 180, c = std::cos(a), s = std::sin(a);
    for (const auto p : points) {
        if (p >= mesh.points.size()) continue;
        const Vec3 d = mesh.points[p] - centre;
        // North (+Z) toward east (+X): clockwise seen from above.
        mesh.points[p] = centre + Vec3{d.x * c + d.z * s, d.y, -d.x * s + d.z * c};
    }
}

void turnPointsAbout(Mesh& mesh, const PointSet& points, Vec3 centre, int axis, double degrees) {
    const auto r = catalog::axisRotation(axis, degrees);
    for (const auto p : points) {
        if (p < mesh.points.size()) mesh.points[p] = centre + r * (mesh.points[p] - centre);
    }
}

std::map<std::uint32_t, std::vector<std::uint32_t>> slideNeighbours(const Mesh& mesh, const PointSet& points) {
    std::map<std::uint32_t, std::vector<std::uint32_t>> out;
    for (const auto& [a, b] : edgesOf(mesh)) {
        if (points.contains(a) && !points.contains(b)) out[a].push_back(b);
        if (points.contains(b) && !points.contains(a)) out[b].push_back(a);
    }
    return out;
}

void slidePoints(Mesh& mesh, const std::map<std::uint32_t, Vec3>& start,
                 const std::map<std::uint32_t, std::vector<std::uint32_t>>& neighbours, Vec3 way, double share) {
    const Vec3 w = normalised(way);
    for (const auto& [p, from] : start) {
        if (p >= mesh.points.size()) continue;
        mesh.points[p] = from;
        const auto it = neighbours.find(p);
        if (it == neighbours.end()) continue;
        std::optional<std::uint32_t> along;
        double most = 1e-6;   // only edges running at least a little the right way
        for (const auto q : it->second) {
            const double d = dot(normalised(mesh.points[q] - from), w);
            if (d > most) {
                most = d;
                along = q;
            }
        }
        if (along) mesh.points[p] = from + (mesh.points[*along] - from) * share;
    }
}

void scalePoints(Mesh& mesh, const PointSet& points, Vec3 centre, Vec3 factors) {
    for (const auto p : points) {
        if (p >= mesh.points.size()) continue;
        const Vec3 d = mesh.points[p] - centre;
        mesh.points[p] = centre + Vec3{d.x * factors.x, d.y * factors.y, d.z * factors.z};
    }
    if (factors.x * factors.y * factors.z < 0) {
        // A mirror: faces wholly inside it would now be seen from behind. Turn them back.
        FaceSet mirrored;
        for (std::uint32_t f = 0; f < mesh.faces.size(); ++f) {
            if (std::ranges::all_of(mesh.faces[f].points, [&](std::uint32_t p) { return points.contains(p); })) {
                mirrored.insert(f);
            }
        }
        flipFaces(mesh, mirrored);
    }
}

void setTexture(Mesh& mesh, const FaceSet& faces, FaceTexture texture) {
    for (const auto f : faces) {
        if (f < mesh.faces.size()) mesh.faces[f].texture = texture;
    }
}

void fitTexture(Mesh& mesh, std::uint32_t face, int width, int height) {
    if (face >= mesh.faces.size() || width <= 0 || height <= 0) return;
    auto& f = mesh.faces[face];
    if (f.points.empty()) return;
    const auto axes = uvAxes(mesh.normal(f), f.uv.angle);
    double u0 = 1e300, u1 = -1e300, v0 = 1e300, v1 = -1e300;
    for (const auto p : f.points) {
        const double u = dot(mesh.points[p], axes.u), v = dot(mesh.points[p], axes.v);
        u0 = std::min(u0, u);
        u1 = std::max(u1, u);
        v0 = std::min(v0, v);
        v1 = std::max(v1, v);
    }
    if (u1 - u0 > 1e-6) {
        f.uv.scaleU = (u1 - u0) / width;
        f.uv.offsetU = -u0 / f.uv.scaleU;
    }
    if (v1 - v0 > 1e-6) {
        f.uv.scaleV = (v1 - v0) / height;
        f.uv.offsetV = -v0 / f.uv.scaleV;
    }
}

std::optional<Edge> sharedEdge(const Mesh& mesh, std::uint32_t a, std::uint32_t b) {
    if (a >= mesh.faces.size() || b >= mesh.faces.size() || a == b) return std::nullopt;
    const auto& p = mesh.faces[a].points;
    for (std::size_t i = 0; i < p.size(); ++i) {
        const auto edge = edgeOf(p[i], p[(i + 1) % p.size()]);
        const auto& q = mesh.faces[b].points;
        for (std::size_t k = 0; k < q.size(); ++k) {
            if (edgeOf(q[k], q[(k + 1) % q.size()]) == edge) return edge;
        }
    }
    return std::nullopt;
}

std::optional<UvAlign> wrappedAlign(const Mesh& mesh, std::uint32_t source, std::uint32_t target) {
    if (source == target || source >= mesh.faces.size() || target >= mesh.faces.size()) return std::nullopt;
    const auto& from = mesh.faces[source];
    const auto& to = mesh.faces[target];
    const Vec3 nFrom = mesh.normal(from), nTo = mesh.normal(to);
    if (length(nFrom) < 0.5 || length(nTo) < 0.5) return std::nullopt;

    // The fold: a turn about the line the planes meet on (`along`, through `hinge`). It leaves that line where it is and
    // takes the source's direction square to the line on its plane, cross(n, along), to the target's. So the two faces can
    // be measured in the same terms: the target must change texels along those two directions as the source does.
    const auto shared = sharedEdge(mesh, source, target);
    const Vec3 meet = cross(nFrom, nTo);
    constexpr double kParallel = 1e-6;
    Vec3 hinge = shared ? (mesh.points[shared->first] + mesh.points[shared->second]) * 0.5 : mesh.centre(to);
    Vec3 along;
    if (shared) {
        along = normalised(mesh.points[shared->second] - mesh.points[shared->first]);
    } else if (length(meet) > kParallel) {
        along = normalised(meet);
        // The point on both planes (n.x = d for each).
        const double dFrom = dot(nFrom, mesh.centre(from)), dTo = dot(nTo, mesh.centre(to));
        hinge = (cross(nTo, meet) * dFrom + cross(meet, nFrom) * dTo) * (1 / dot(meet, meet));
    } else {
        // Parallel, with no line to fold on: any way across the source will do.
        const Vec3 u = uvAxes(nFrom, 0).u;
        along = normalised(u - nFrom * dot(u, nFrom));
        if (dot(nFrom, nTo) < 0) hinge = (mesh.centre(from) + mesh.centre(to)) * 0.5;
    }
    // A shared edge may run either way: the fold takes `nFrom` to `nTo` whichever, as long as both squares use one `along`.
    const Vec3 squareFrom = cross(nFrom, along), squareTo = cross(nTo, along);

    const auto axesFrom = uvAxes(nFrom, from.uv.angle);
    const double su = from.uv.scaleU != 0 ? from.uv.scaleU : kClassicUnitsPerTexel;
    const double sv = from.uv.scaleV != 0 ? from.uv.scaleV : kClassicUnitsPerTexel;
    // Texels per unit the target must have, along the line and square to it, for its U and its V.
    const double wantU[2] = {dot(along, axesFrom.u) / su, dot(squareFrom, axesFrom.u) / su};
    const double wantV[2] = {dot(along, axesFrom.v) / sv, dot(squareFrom, axesFrom.v) / sv};
    // What the target's unturned axes give along those two directions. On a wall they lie in the face and this is a plain
    // turn; on a slope laid from above they are stretched, which is why scales can differ from the source's there.
    const auto base = uvAxes(nTo, 0);
    const double m00 = dot(along, base.u), m01 = dot(along, base.v), m10 = dot(squareTo, base.u), m11 = dot(squareTo, base.v);
    const double det = m00 * m11 - m01 * m10;
    if (std::abs(det) < 1e-9) return std::nullopt;
    // Solve for U and V in terms of the unturned axes: each should be the turned axis over its scale.
    const double w1x = (m11 * wantU[0] - m01 * wantU[1]) / det, w1y = (m00 * wantU[1] - m10 * wantU[0]) / det;
    const double w2x = (m11 * wantV[0] - m01 * wantV[1]) / det, w2y = (m00 * wantV[1] - m10 * wantV[0]) / det;
    // U gives (cos, sin), V (-sin, cos). They agree unless a slope skews the picture, which one turn cannot match: then take
    // the turn between them.
    const double len1 = std::hypot(w1x, w1y), len2 = std::hypot(w2x, w2y);
    if (len1 < 1e-12 || len2 < 1e-12) return std::nullopt;
    const double c = w1x / len1 + w2y / len2, s = w1y / len1 - w2x / len2;
    double degrees = std::atan2(s, c) * 180 / std::numbers::pi;
    degrees = std::round(degrees * 1e6) / 1e6 + 0.0;   // + 0.0: no minus zero
    if (degrees <= -180) degrees += 360;

    const double radians = degrees * std::numbers::pi / 180;
    const double along1 = w1x * std::cos(radians) + w1y * std::sin(radians);
    const double along2 = -w2x * std::sin(radians) + w2y * std::cos(radians);
    UvAlign out = from.uv;
    out.angle = degrees;
    if (along1 > 1e-12) out.scaleU = std::round(1e9 / along1) / 1e9;
    if (along2 > 1e-12) out.scaleV = std::round(1e9 / along2) / 1e9;
    out.offsetU = out.offsetV = 0;
    const auto wanted = texelsAt(from.uv, axesFrom, hinge);
    const auto got = texelsAt(out, uvAxes(nTo, degrees), hinge);
    out.offsetU = wanted[0] - got[0];
    out.offsetV = wanted[1] - got[1];
    return out;
}

double snapTo(double value, double step) { return step > 0 ? std::round(value / step) * step : value; }

Vec3 snapTo(Vec3 value, double step) { return {snapTo(value.x, step), snapTo(value.y, step), snapTo(value.z, step)}; }

} // namespace de::mesh
