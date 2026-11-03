#include "geometry/model_mesh.h"

#include <cmath>
#include <map>
#include <optional>
#include <utility>

namespace de::geometry {

namespace {

constexpr std::uint32_t kPackedUvBelowId = 905;
constexpr double kPointDivisor = 256.0;
constexpr double kTextureDivisor = 16.0;
constexpr double kCornerAngle = 0.001;   // radians

/// A face point before decomposition, in ARCH3D units and Daggerfall axes.
struct RawPoint {
    double x = 0, y = 0, z = 0;
    std::int32_t u = 0, v = 0;
};

double dot(const RawPoint& a, const std::array<double, 3>& n) { return a.x * n[0] + a.y * n[1] + a.z * n[2]; }

/// FaceUVTool.ComputeDFUVMatrixXY: solve U = A*X + B*Y + D (and V likewise) through three points.
struct UvMatrix { double ua, ub, ud, va, vb, vd; };
std::optional<UvMatrix> fitUvMatrix(const std::array<double, 3>& X, const std::array<double, 3>& Y,
                                    const std::array<double, 3>& U, const std::array<double, 3>& V) {
    const double det = X[0] * Y[1] + Y[0] * X[2] + X[1] * Y[2] - Y[1] * X[2] - Y[0] * X[1] - X[0] * Y[2];
    if (det == 0) return std::nullopt;
    const std::array<double, 3> xi{(Y[1] - Y[2]) / det, (-X[1] + X[2]) / det, (X[1] * Y[2] - X[2] * Y[1]) / det};
    const std::array<double, 3> yi{(-Y[0] + Y[2]) / det, (X[0] - X[2]) / det, (-X[0] * Y[2] + X[2] * Y[0]) / det};
    const std::array<double, 3> zi{(Y[0] - Y[1]) / det, (-X[0] + X[1]) / det, (X[0] * Y[1] - X[1] * Y[0]) / det};
    return UvMatrix{U[0] * xi[0] + U[1] * yi[0] + U[2] * zi[0], U[0] * xi[1] + U[1] * yi[1] + U[2] * zi[1],
                    U[0] * xi[2] + U[1] * yi[2] + U[2] * zi[2], V[0] * xi[0] + V[1] * yi[0] + V[2] * zi[0],
                    V[0] * xi[1] + V[1] * yi[1] + V[2] * zi[1], V[0] * xi[2] + V[1] * yi[2] + V[2] * zi[2]};
}

/// Normalise the way Unity's Vector3.Normalize does it (multiply by the inverse); the result feeds truncations, so
/// the formulation matters. Fails on a zero vector, as Daggerfall Unity does.
std::optional<std::array<double, 3>> normalise(std::array<double, 3> v) {
    const double m = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (m == 0) return std::nullopt;
    const double inv = 1 / m;
    return std::array<double, 3>{v[0] * inv, v[1] * inv, v[2] * inv};
}

double trunc(double v) { return std::trunc(v); }

/// FaceUVTool.ComputeFaceUVCoordinates: absolute UVs for every point of an n-gon. Returns false when the fitted
/// matrix is singular (the points keep their raw values then, as in Daggerfall Unity).
Result<bool> computeFaceUvs(std::vector<RawPoint>& points) {
    const auto& p0 = points[0];
    const auto& p1 = points[1];
    const auto& p2 = points[2];
    const std::array<double, 3> v0{p1.x - p0.x, p1.y - p0.y, p1.z - p0.z};
    std::array<double, 3> v1{p2.x - p0.x, p2.y - p0.y, p2.z - p0.z};
    const double k = (v1[0] * v0[0] + v1[1] * v0[1] + v1[2] * v0[2]) / (v0[0] * v0[0] + v0[1] * v0[1] + v0[2] * v0[2]);
    v1 = {v1[0] - v0[0] * k, v1[1] - v0[1] * k, v1[2] - v0[2] * k};
    const auto n0 = normalise(v0);
    const auto n1 = normalise(v1);
    if (!n0 || !n1) return fail("face with a zero-length edge");

    const std::array<double, 3> X{trunc(dot(p0, *n0)), trunc(dot(p1, *n0)), trunc(dot(p2, *n0))};
    const std::array<double, 3> Y{trunc(dot(p0, *n1)), trunc(dot(p1, *n1)), trunc(dot(p2, *n1))};
    const std::array<double, 3> U{double(p0.u), double(p1.u + p0.u), double(p2.u + p1.u + p0.u)};
    const std::array<double, 3> V{double(p0.v), double(p1.v + p0.v), double(p2.v + p1.v + p0.v)};
    const auto m = fitUvMatrix(X, Y, U, V);
    if (!m) return false;

    std::vector<std::pair<std::int32_t, std::int32_t>> uvs(points.size());
    for (std::size_t i = 0; i < points.size(); ++i) {
        if (i < 3) {
            uvs[i] = {static_cast<std::int32_t>(U[i]), static_cast<std::int32_t>(V[i])};
            continue;
        }
        const double px = trunc(dot(points[i], *n0));
        const double py = trunc(dot(points[i], *n1));
        uvs[i] = {static_cast<std::int32_t>(trunc(px * m->ua + py * m->ub + m->ud)),
                  static_cast<std::int32_t>(trunc(px * m->va + py * m->vb + m->vd))};
    }
    for (std::size_t i = 0; i < points.size(); ++i) {
        points[i].u = uvs[i].first;
        points[i].v = uvs[i].second;
    }
    return true;
}

/// Arch3dFile.GetCornerPoints: the points where the outline turns.
Result<std::vector<std::size_t>> cornerPoints(const std::vector<RawPoint>& points) {
    std::vector<std::size_t> corners;
    const auto n = points.size();
    for (std::size_t i = 0; i < n; ++i) {
        const auto& a = points[i];
        const auto& b = points[(i + 1) % n];
        const auto& c = points[(i + 2) % n];
        const std::size_t corner = (i + 1) % n;
        const std::array<double, 3> l0{b.x - a.x, b.y - a.y, b.z - a.z};
        const std::array<double, 3> l1{c.x - a.x, c.y - a.y, c.z - a.z};
        const double m0 = std::sqrt(l0[0] * l0[0] + l0[1] * l0[1] + l0[2] * l0[2]);
        const double m1 = std::sqrt(l1[0] * l1[0] + l1[1] * l1[1] + l1[2] * l1[2]);
        if (m0 == 0 || m1 == 0) return fail("face with a zero-length edge");
        // Unclamped, as in the reference: a rounding overshoot past +-1 gives NaN, which is not a corner.
        const double angle = std::acos((l0[0] * l1[0] + l0[1] * l1[1] + l0[2] * l1[2]) / (m0 * m1));
        if (angle > kCornerAngle) corners.push_back(corner);
    }
    return corners;
}

} // namespace

std::int32_t unpackUv(std::int32_t u) {
    if (u > -14336 && u < 14336 && u != -7168) return u;
    // The nearest multiple of 8192, subtracted.
    std::int32_t next = ((u - 1) >> 13) + 1;
    next <<= 13;
    const std::int32_t prev = next - 8192;
    const std::int32_t mult = (u - prev < next - u) ? prev : next;
    return u - mult;
}

Result<ModelMesh> buildModelMesh(const formats::Arch3dMesh& mesh) {
    // Faces grouped by texture, textures in order of first use; each face's points ready to write.
    std::vector<std::pair<int, int>> textures;
    std::map<std::pair<int, int>, std::size_t> textureIndex;
    std::vector<std::vector<std::pair<std::vector<RawPoint>, catalog::Vec3>>> faces;   // per texture: (points, normal)

    for (std::size_t f = 0; f < mesh.planes.size(); ++f) {
        const auto& plane = mesh.planes[f];
        if (plane.vertices.size() < 3) continue;   // nothing to draw
        const std::pair key{plane.textureArchive(), plane.textureRecord()};
        auto [it, added] = textureIndex.try_emplace(key, textures.size());
        if (added) {
            textures.push_back(key);
            faces.emplace_back();
        }

        std::vector<RawPoint> points;
        points.reserve(plane.vertices.size());
        for (std::size_t i = 0; i < plane.vertices.size(); ++i) {
            const auto& pv = plane.vertices[i];
            const auto& p = mesh.points[pv.point];
            RawPoint rp{double(p.x), double(p.y), double(p.z), pv.u, pv.v};
            if (i < 3 && mesh.id < kPackedUvBelowId) {
                rp.u = unpackUv(rp.u);
                rp.v = unpackUv(rp.v);
            }
            points.push_back(rp);
        }

        std::vector<RawPoint> written;
        if (points.size() == 3) {
            points[1].u += points[0].u;
            points[1].v += points[0].v;
            points[2].u += points[1].u;
            points[2].v += points[1].v;
            written = std::move(points);
        } else {
            auto corners = cornerPoints(points);
            if (!corners) return fail("model " + std::to_string(mesh.id) + " face " + std::to_string(f) + ": " +
                                      corners.error().message);
            auto fitted = computeFaceUvs(points);
            if (!fitted) return fail("model " + std::to_string(mesh.id) + " face " + std::to_string(f) + ": " +
                                     fitted.error().message);
            if (corners->size() < 3) continue;   // collinear outline: no area to draw
            for (const auto c : *corners) written.push_back(points[c]);
        }

        const catalog::Vec3 normal{double(plane.nx), -double(plane.ny), double(plane.nz)};
        faces[it->second].emplace_back(std::move(written), normal);
    }

    ModelMesh out;
    out.modelId = mesh.id;
    for (std::size_t t = 0; t < textures.size(); ++t) {
        ModelMesh::SubMesh sub{textures[t].first, textures[t].second, static_cast<std::uint32_t>(out.indices.size()), 0};
        for (const auto& [points, rawNormal] : faces[t]) {
            const double len = std::sqrt(rawNormal.x * rawNormal.x + rawNormal.y * rawNormal.y + rawNormal.z * rawNormal.z);
            const catalog::Vec3 normal = len > 0 ? catalog::Vec3{rawNormal.x / len, rawNormal.y / len, rawNormal.z / len}
                                                 : catalog::Vec3{0, 1, 0};
            const auto first = static_cast<std::uint32_t>(out.vertices.size());
            for (const auto& p : points) {
                out.vertices.push_back({{p.x / kPointDivisor, -p.y / kPointDivisor, p.z / kPointDivisor}, normal,
                                        static_cast<float>(p.u / kTextureDivisor),
                                        static_cast<float>(p.v / kTextureDivisor)});
            }
            // A fan from the first point, in Daggerfall Unity's order.
            for (std::uint32_t k = 1; k + 1 < points.size(); ++k) {
                out.indices.insert(out.indices.end(), {first, first + k + 1, first + k});
            }
        }
        sub.indexCount = static_cast<std::uint32_t>(out.indices.size()) - sub.firstIndex;
        out.subMeshes.push_back(sub);
    }
    return out;
}

} // namespace de::geometry
