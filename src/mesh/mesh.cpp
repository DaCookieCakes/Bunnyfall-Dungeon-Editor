#include "mesh/mesh.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <map>
#include <numbers>
#include <utility>

namespace de::mesh {

namespace {

constexpr double kFlatTolerance = 0.5;   // block units a point may sit off its face's plane
constexpr double kFloorNormalY = 0.7;    // steeper than this is a floor or ceiling for texturing

/// Two directions across a plane with normal `n`, at right angles to each other and to it.
std::pair<Vec3, Vec3> planeBasis(Vec3 n) {
    const Vec3 helper = std::abs(n.y) < 0.9 ? Vec3{0, 1, 0} : Vec3{1, 0, 0};
    const Vec3 a = normalised(cross(helper, n));
    return {a, cross(n, a)};
}

double cross2(std::array<double, 2> a, std::array<double, 2> b, std::array<double, 2> c) {
    return (b[0] - a[0]) * (c[1] - b[1]) - (b[1] - a[1]) * (c[0] - b[0]);
}

bool insideTriangle(std::array<double, 2> p, std::array<double, 2> a, std::array<double, 2> b, std::array<double, 2> c,
                    double sign) {
    // On the inner side of all three edges (points on an edge count as inside, so they block the ear).
    auto side = [&](std::array<double, 2> s, std::array<double, 2> e) {
        return ((e[0] - s[0]) * (p[1] - s[1]) - (e[1] - s[1]) * (p[0] - s[0])) * sign;
    };
    return side(a, b) >= -1e-9 && side(b, c) >= -1e-9 && side(c, a) >= -1e-9;
}

} // namespace

double length(Vec3 v) { return std::sqrt(dot(v, v)); }

Vec3 normalised(Vec3 v) {
    const double l = length(v);
    return l > 1e-12 ? v * (1 / l) : Vec3{};
}

Vec3 Mesh::normal(const Face& face) const {
    // Newell's method gives the area-weighted normal of the polygon in the order its points are listed; the fan
    // triangles' cross(b - a, c - a) points the same way.
    Vec3 n;
    const auto count = face.points.size();
    for (std::size_t i = 0; i < count; ++i) {
        const auto& a = points[face.points[i]];
        const auto& b = points[face.points[(i + 1) % count]];
        n.x += (a.y - b.y) * (a.z + b.z);
        n.y += (a.z - b.z) * (a.x + b.x);
        n.z += (a.x - b.x) * (a.y + b.y);
    }
    // Newell's sums equal the sum of cross(a, b) round the face: the same algebra as the fan triangles' cross products.
    return normalised(n);
}

Vec3 Mesh::centre(const Face& face) const {
    Vec3 c;
    for (const auto i : face.points) c = c + points[i];
    return face.points.empty() ? c : c * (1.0 / double(face.points.size()));
}

catalog::Box Mesh::bounds() const {
    if (points.empty()) return {};
    catalog::Box b{points.front(), points.front()};
    for (const auto& p : points) {
        b.min = {std::min(b.min.x, p.x), std::min(b.min.y, p.y), std::min(b.min.z, p.z)};
        b.max = {std::max(b.max.x, p.x), std::max(b.max.y, p.y), std::max(b.max.z, p.z)};
    }
    return b;
}

UvAxes uvAxes(Vec3 n, double angleDegrees) {
    UvAxes axes;
    if (n.y > kFloorNormalY) {
        axes = {{1, 0, 0}, {0, 0, -1}};
    } else if (n.y < -kFloorNormalY) {
        axes = {{1, 0, 0}, {0, 0, 1}};
    } else {
        const Vec3 flat = normalised({n.x, 0, n.z});
        axes.u = {-flat.z, 0, flat.x};   // to the right, seen from the front (editor space is left-handed)
        axes.v = normalised(cross(n, axes.u));
        if (axes.v.y > 0) axes.v = axes.v * -1;   // down the face
    }
    if (angleDegrees != 0) {
        const double a = angleDegrees * std::numbers::pi / 180;
        const double c = std::cos(a), s = std::sin(a);
        axes = {axes.u * c + axes.v * s, axes.v * c - axes.u * s};
    }
    return axes;
}

std::array<double, 2> texelsAt(const UvAlign& align, const UvAxes& axes, Vec3 p) {
    const double su = align.scaleU != 0 ? align.scaleU : kClassicUnitsPerTexel;
    const double sv = align.scaleV != 0 ? align.scaleV : kClassicUnitsPerTexel;
    return {dot(p, axes.u) / su + align.offsetU, dot(p, axes.v) / sv + align.offsetV};
}

std::vector<std::array<std::uint32_t, 3>> triangulate(const Mesh& mesh, const Face& face) {
    std::vector<std::array<std::uint32_t, 3>> out;
    const auto count = face.points.size();
    if (count < 3) return out;
    const Vec3 n = mesh.normal(face);
    if (length(n) == 0) return out;
    const auto [e1, e2] = planeBasis(n);
    std::vector<std::array<double, 2>> flat(count);
    double area = 0;
    for (std::size_t i = 0; i < count; ++i) {
        const auto& p = mesh.points[face.points[i]];
        flat[i] = {dot(p, e1), dot(p, e2)};
    }
    for (std::size_t i = 0; i < count; ++i) {
        const auto& a = flat[i];
        const auto& b = flat[(i + 1) % count];
        area += a[0] * b[1] - b[0] * a[1];
    }
    const double sign = area >= 0 ? 1 : -1;

    // A fan only for a strictly convex face: a point lying along an edge (a cut puts corners there, so the faces
    // beside stay crack-free) would give it triangles of no area. Those faces, and concave ones, are cut into ears,
    // where every point is the corner of a real triangle.
    bool convex = true;
    for (std::size_t i = 0; i < count && convex; ++i) {
        convex = cross2(flat[i], flat[(i + 1) % count], flat[(i + 2) % count]) * sign > 1e-9;
    }
    if (convex) {
        for (std::uint32_t i = 1; i + 1 < count; ++i) out.push_back({0, i, i + 1});
        return out;
    }

    // Ear clipping: cut off a corner that turns the polygon's way and holds no other point, until a triangle is left.
    std::vector<std::uint32_t> left(count);
    for (std::uint32_t i = 0; i < count; ++i) left[i] = i;
    std::size_t guard = count * count;
    while (left.size() > 3 && guard-- > 0) {
        bool cut = false;
        for (std::size_t i = 0; i < left.size(); ++i) {
            const auto a = left[(i + left.size() - 1) % left.size()], b = left[i], c = left[(i + 1) % left.size()];
            if (cross2(flat[a], flat[b], flat[c]) * sign <= 1e-9) continue;
            bool empty = true;
            for (const auto o : left) {
                if (o == a || o == b || o == c) continue;
                if (insideTriangle(flat[o], flat[a], flat[b], flat[c], sign)) {
                    empty = false;
                    break;
                }
            }
            if (!empty) continue;
            out.push_back({a, b, c});
            left.erase(left.begin() + static_cast<std::ptrdiff_t>(i));
            cut = true;
            break;
        }
        if (!cut) break;   // a self-crossing face: what is left stays uncovered
    }
    if (left.size() == 3 && std::abs(cross2(flat[left[0]], flat[left[1]], flat[left[2]])) > 1e-9) {
        out.push_back({left[0], left[1], left[2]});
    }
    return out;
}

geometry::ModelMesh toModelMesh(const Mesh& mesh, std::uint32_t modelId) {
    geometry::ModelMesh out;
    out.modelId = modelId;
    // Faces grouped by texture, the groups in order of each texture's first use (as ARCH3D models come out).
    std::vector<std::pair<FaceTexture, std::vector<std::size_t>>> groups;
    for (std::size_t f = 0; f < mesh.faces.size(); ++f) {
        const auto& t = mesh.faces[f].texture;
        auto it = std::ranges::find_if(groups, [&](const auto& g) { return g.first == t; });
        if (it == groups.end()) {
            groups.push_back({t, {}});
            it = groups.end() - 1;
        }
        it->second.push_back(f);
    }
    for (const auto& [texture, faces] : groups) {
        geometry::ModelMesh::SubMesh sub;
        sub.textureArchive = texture.archive;
        sub.textureRecord = texture.record;
        sub.firstIndex = static_cast<std::uint32_t>(out.indices.size());
        for (const auto f : faces) {
            const auto& face = mesh.faces[f];
            const auto triangles = triangulate(mesh, face);
            if (triangles.empty()) continue;
            const Vec3 n = mesh.normal(face);
            const auto axes = uvAxes(n, face.uv.angle);
            const auto base = static_cast<std::uint32_t>(out.vertices.size());
            for (const auto i : face.points) {
                const auto& p = mesh.points[i];
                const auto uv = texelsAt(face.uv, axes, p);
                out.vertices.push_back({p, n, float(uv[0]), float(uv[1])});
            }
            for (const auto& t : triangles) {
                for (const auto i : t) out.indices.push_back(base + i);
            }
        }
        sub.indexCount = static_cast<std::uint32_t>(out.indices.size()) - sub.firstIndex;
        if (sub.indexCount > 0) out.subMeshes.push_back(sub);
    }
    return out;
}

catalog::ModelInfo modelInfoOf(const Mesh& mesh, std::uint32_t modelId, std::string description) {
    catalog::ModelInfo info;
    info.id = modelId;
    info.bounds = mesh.bounds();
    info.kind = catalog::classifyModel(modelId, description, info.bounds);
    info.description = std::move(description);
    info.points = mesh.points;
    std::map<int, std::size_t> archives;
    for (const auto& f : mesh.faces) {
        info.faces.push_back(f.points);
        ++archives[f.texture.archive];
    }
    if (!archives.empty()) {
        info.textureArchive = std::ranges::max_element(archives, [](const auto& a, const auto& b) { return a.second < b.second; })->first;
    }
    // A key no classic shape has, so no classic model counts as its twin.
    info.shapeKey = 0x8000'0000'0000'0000ull | modelId;
    return info;
}

std::vector<std::string> checkMesh(const Mesh& mesh) {
    std::vector<std::string> problems;
    for (std::size_t f = 0; f < mesh.faces.size(); ++f) {
        const auto& face = mesh.faces[f];
        if (face.points.size() < 3) {
            problems.push_back(std::format("face {} has {} point{}", f, face.points.size(), face.points.size() == 1 ? "" : "s"));
            continue;
        }
        bool bad = false;
        for (const auto i : face.points) bad = bad || i >= mesh.points.size();
        if (bad) {
            problems.push_back(std::format("face {} names a point that does not exist", f));
            continue;
        }
        auto sorted = face.points;
        std::ranges::sort(sorted);
        if (std::ranges::adjacent_find(sorted) != sorted.end()) {
            problems.push_back(std::format("face {} uses a point twice", f));
            continue;
        }
        const Vec3 n = mesh.normal(face);
        if (length(n) == 0) {
            problems.push_back(std::format("face {} has no area", f));
            continue;
        }
        const Vec3 c = mesh.centre(face);
        double off = 0;
        for (const auto i : face.points) off = std::max(off, std::abs(dot(mesh.points[i] - c, n)));
        if (off > kFlatTolerance) problems.push_back(std::format("face {} is not flat ({:.1f} units off)", f, off));
    }
    return problems;
}

} // namespace de::mesh
