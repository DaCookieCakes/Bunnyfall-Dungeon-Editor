#pragma once

// Editor space: block units (1 = 0.025 m), Y up. X and Z are the file's; Y is the file's negated. ARCH3D mesh points
// are 256 per block unit and also Y down. Everything the catalog and the editor measure is in this space; conversion
// to and from file values happens only here.

#include <array>
#include <cmath>
#include <cstdint>

namespace de::catalog {

struct Vec3 {
    double x = 0, y = 0, z = 0;
    friend Vec3 operator+(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
    friend Vec3 operator-(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
    friend Vec3 operator*(Vec3 a, double s) { return {a.x * s, a.y * s, a.z * s}; }
    friend bool operator==(Vec3, Vec3) = default;
};

struct Box {
    Vec3 min, max;
    [[nodiscard]] Vec3 size() const { return {max.x - min.x, max.y - min.y, max.z - min.z}; }
};

/// Row-major 3x3 rotation.
struct Mat3 {
    std::array<double, 9> m{1, 0, 0, 0, 1, 0, 0, 0, 1};
    [[nodiscard]] Vec3 operator*(Vec3 v) const {
        return {m[0] * v.x + m[1] * v.y + m[2] * v.z, m[3] * v.x + m[4] * v.y + m[5] * v.z,
                m[6] * v.x + m[7] * v.y + m[8] * v.z};
    }
    [[nodiscard]] Mat3 operator*(const Mat3& o) const {
        Mat3 r;
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j)
                r.m[static_cast<std::size_t>(i * 3 + j)] =
                    m[static_cast<std::size_t>(i * 3)] * o.m[static_cast<std::size_t>(j)] +
                    m[static_cast<std::size_t>(i * 3 + 1)] * o.m[static_cast<std::size_t>(3 + j)] +
                    m[static_cast<std::size_t>(i * 3 + 2)] * o.m[static_cast<std::size_t>(6 + j)];
        return r;
    }
};

constexpr double kBlockUnitsPerMetre = 40.0;
constexpr double kArch3dUnitsPerBlockUnit = 256.0;
constexpr std::int32_t kRotationUnitsPerTurn = 2048;   // 512 = 90 degrees
constexpr std::int32_t kRdbBlockSide = 2048;           // block units

/// A file rotation value (2048 per turn) as degrees in editor space. Daggerfall turns the other way.
inline double rotationDegrees(std::int32_t fileRotation) {
    return -static_cast<double>(fileRotation) * 360.0 / kRotationUnitsPerTurn;
}

/// The rotation of a placed RDB model: Rz * Rx * Ry, as Daggerfall Unity composes it (not the usual TRS order).
Mat3 rdbModelRotation(std::int32_t xRotation, std::int32_t yRotation, std::int32_t zRotation);

/// An RDB object's file position in editor space.
inline Vec3 fromFilePosition(std::int32_t x, std::int32_t y, std::int32_t z) {
    return {static_cast<double>(x), -static_cast<double>(y), static_cast<double>(z)};
}

/// An ARCH3D point in editor space (block units).
inline Vec3 fromMeshPoint(std::int32_t x, std::int32_t y, std::int32_t z) {
    return {x / kArch3dUnitsPerBlockUnit, -y / kArch3dUnitsPerBlockUnit, z / kArch3dUnitsPerBlockUnit};
}

/// The box around `local` after rotating and moving it.
Box transformBox(const Box& local, const Mat3& rotation, Vec3 position);

} // namespace de::catalog
