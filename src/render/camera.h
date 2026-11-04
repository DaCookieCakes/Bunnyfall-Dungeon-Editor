#pragma once

// Camera and projection maths. Editor space is left-handed (X right, Y up, Z forward, as in Unity), so the view and
// projection are left-handed too and the picture matches Daggerfall Unity's without mirroring. Output is OpenGL clip
// space (depth -1..1). Matrices are column-major floats, as OpenGL takes them.

#include "catalog/space.h"
#include "scene/picking.h"

#include <array>
#include <cmath>

namespace de::render {

using Mat4 = std::array<float, 16>;   // column-major

inline Mat4 multiply(const Mat4& a, const Mat4& b) {
    Mat4 r{};
    for (int c = 0; c < 4; ++c)
        for (int row = 0; row < 4; ++row) {
            float s = 0;
            for (int k = 0; k < 4; ++k) s += a[static_cast<std::size_t>(k * 4 + row)] * b[static_cast<std::size_t>(c * 4 + k)];
            r[static_cast<std::size_t>(c * 4 + row)] = s;
        }
    return r;
}

/// An instance's model matrix: rotation then translation.
inline Mat4 modelMatrix(const catalog::Mat3& r, catalog::Vec3 t) {
    const auto& m = r.m;   // row-major
    return {float(m[0]), float(m[3]), float(m[6]), 0, float(m[1]), float(m[4]), float(m[7]), 0,
            float(m[2]), float(m[5]), float(m[8]), 0, float(t.x),  float(t.y),  float(t.z),  1};
}

struct Camera {
    catalog::Vec3 position;
    double yaw = 0;     ///< Radians; 0 looks along +Z, positive turns toward +X.
    double pitch = 0;   ///< Radians; positive looks up.
    double fovY = 70.0 * 3.14159265358979 / 180.0;
    double nearPlane = 2.0, farPlane = 40000.0;   // block units
    bool orthographic = false;
    double orthoHalfHeight = 1000;   ///< Orthographic: block units from the view's centre to its top edge.

    [[nodiscard]] catalog::Vec3 forward() const {
        return {std::sin(yaw) * std::cos(pitch), std::sin(pitch), std::cos(yaw) * std::cos(pitch)};
    }
    [[nodiscard]] catalog::Vec3 right() const { return {std::cos(yaw), 0, -std::sin(yaw)}; }
    [[nodiscard]] catalog::Vec3 up() const {
        const auto f = forward(), r = right();
        return {f.y * r.z - f.z * r.y, f.z * r.x - f.x * r.z, f.x * r.y - f.y * r.x};   // f x r: up, left-handed
    }

    [[nodiscard]] Mat4 view() const {
        const auto f = forward(), r = right(), u = up(), e = position;
        auto d = [&](catalog::Vec3 a) { return -float(a.x * e.x + a.y * e.y + a.z * e.z); };
        return {float(r.x), float(u.x), float(f.x), 0, float(r.y), float(u.y), float(f.y), 0,
                float(r.z), float(u.z), float(f.z), 0, d(r),       d(u),       d(f),       1};
    }

    [[nodiscard]] Mat4 projection(double aspect) const {
        if (orthographic) {
            const double h = orthoHalfHeight, w = orthoHalfHeight * aspect, n = nearPlane, fa = farPlane;
            Mat4 o{};
            o[0] = float(1 / w);
            o[5] = float(1 / h);
            o[10] = float(2 / (fa - n));   // left-handed: +Z (forward) goes to +1
            o[14] = float(-(fa + n) / (fa - n));
            o[15] = 1;
            return o;
        }
        const double t = 1.0 / std::tan(fovY / 2), n = nearPlane, fa = farPlane;
        Mat4 p{};
        p[0] = float(t / aspect);
        p[5] = float(t);
        p[10] = float((fa + n) / (fa - n));
        p[11] = 1;   // w = z: +Z is forward
        p[14] = float(-2 * fa * n / (fa - n));
        return p;
    }

    /// The ray through a pixel (0,0 top-left) of a width x height viewport.
    [[nodiscard]] scene::Ray rayThrough(double px, double py, double width, double height) const {
        if (orthographic) {
            const double h = orthoHalfHeight, w = orthoHalfHeight * width / height;
            const double x = (2 * px / width - 1) * w, y = (1 - 2 * py / height) * h;
            const auto f = forward(), r = right(), u = up();
            return {{position.x + r.x * x + u.x * y, position.y + r.y * x + u.y * y, position.z + r.z * x + u.z * y}, f};
        }
        const double t = std::tan(fovY / 2), aspect = width / height;
        const double x = (2 * px / width - 1) * t * aspect;
        const double y = (1 - 2 * py / height) * t;
        const auto f = forward(), r = right(), u = up();
        catalog::Vec3 d{f.x + r.x * x + u.x * y, f.y + r.y * x + u.y * y, f.z + r.z * x + u.z * y};
        const double len = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
        return {position, {d.x / len, d.y / len, d.z / len}};
    }

    /// Point the camera at `target`.
    void lookAt(catalog::Vec3 target) {
        const double dx = target.x - position.x, dy = target.y - position.y, dz = target.z - position.z;
        yaw = std::atan2(dx, dz);
        pitch = std::atan2(dy, std::sqrt(dx * dx + dz * dz));
    }
};

} // namespace de::render
