#include "scene/picking.h"

#include <cmath>

namespace de::scene {

namespace {

using catalog::Vec3;

Vec3 sub(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Vec3 cross(Vec3 a, Vec3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
double dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

/// The transpose undoes a pure rotation.
Vec3 unrotate(const catalog::Mat3& r, Vec3 v) {
    const auto& m = r.m;
    return {m[0] * v.x + m[3] * v.y + m[6] * v.z, m[1] * v.x + m[4] * v.y + m[7] * v.z,
            m[2] * v.x + m[5] * v.y + m[8] * v.z};
}

} // namespace

std::optional<double> intersectTriangle(const Ray& ray, Vec3 a, Vec3 b, Vec3 c) {
    // Moller-Trumbore.
    constexpr double kEpsilon = 1e-9;
    const Vec3 e1 = sub(b, a), e2 = sub(c, a);
    const Vec3 p = cross(ray.direction, e2);
    const double det = dot(e1, p);
    if (std::abs(det) < kEpsilon) return std::nullopt;
    const double inv = 1 / det;
    const Vec3 t = sub(ray.origin, a);
    const double u = dot(t, p) * inv;
    if (u < 0 || u > 1) return std::nullopt;
    const Vec3 q = cross(t, e1);
    const double v = dot(ray.direction, q) * inv;
    if (v < 0 || u + v > 1) return std::nullopt;
    const double distance = dot(e2, q) * inv;
    if (distance <= kEpsilon) return std::nullopt;
    return distance;
}

std::optional<Hit> pick(const Scene& scene, const Ray& ray, const MeshLookup& meshes, const FlatSizeLookup& flatSize,
                        PickOptions options) {
    std::optional<Hit> best;
    auto offer = [&](Hit::Kind kind, std::size_t index, double distance, Vec3 normal) {
        if (!best || distance < best->distance) best = Hit{kind, index, distance, normal};
    };
    const Vec3 towardViewer{-ray.direction.x, -ray.direction.y, -ray.direction.z};

    for (std::size_t i = 0; options.models && i < scene.models.size(); ++i) {
        const auto& inst = scene.models[i];
        if (options.ignore && inst.ref == *options.ignore) continue;
        const auto* mesh = meshes(inst.modelId);
        if (!mesh) continue;
        // Test in the model's own space: rotations keep lengths, so distances carry over unchanged.
        const Ray local{unrotate(inst.transform.rotation, sub(ray.origin, inst.transform.translation)),
                        unrotate(inst.transform.rotation, ray.direction)};
        for (std::size_t t = 0; t + 2 < mesh->indices.size(); t += 3) {
            const auto a = mesh->vertices[mesh->indices[t]].position;
            const auto b = mesh->vertices[mesh->indices[t + 1]].position;
            const auto c = mesh->vertices[mesh->indices[t + 2]].position;
            const auto hit = intersectTriangle(local, a, b, c);
            if (!hit || (best && *hit >= best->distance)) continue;
            Vec3 n = inst.transform.rotation * cross(sub(b, a), sub(c, a));
            const double len = std::sqrt(dot(n, n));
            n = len > 0 ? Vec3{n.x / len, n.y / len, n.z / len} : towardViewer;
            if (dot(n, ray.direction) > 0) n = {-n.x, -n.y, -n.z};
            offer(Hit::Kind::Model, i, *hit, n);
        }
    }

    for (std::size_t i = 0; i < scene.flats.size(); ++i) {
        const auto& flat = scene.flats[i];
        if (flat.editorMarker ? !options.markers : !options.flats) continue;
        if (options.ignore && flat.ref == *options.ignore) continue;
        const auto [w, h] = flatSize(flat.archive, flat.record);
        const double radius = std::max(w, h) / 2;
        // A billboard always faces the viewer, so a sphere around its centre stands in for it.
        const Vec3 toCentre = sub(flat.position, ray.origin);
        const double along = dot(toCentre, ray.direction);
        if (along <= 0) continue;
        const double missSq = dot(toCentre, toCentre) - along * along;
        if (missSq > radius * radius) continue;
        offer(Hit::Kind::Flat, i, along - std::sqrt(radius * radius - missSq), towardViewer);
    }

    const double lightRadius = options.lightSize / 2;
    for (std::size_t i = 0; lightRadius > 0 && i < scene.lights.size(); ++i) {
        const auto& light = scene.lights[i];
        if (options.ignore && light.ref == *options.ignore) continue;
        const Vec3 toCentre = sub(light.position, ray.origin);
        const double along = dot(toCentre, ray.direction);
        if (along <= 0) continue;
        const double missSq = dot(toCentre, toCentre) - along * along;
        if (missSq > lightRadius * lightRadius) continue;
        offer(Hit::Kind::Light, i, along - std::sqrt(lightRadius * lightRadius - missSq), towardViewer);
    }
    return best;
}

} // namespace de::scene
