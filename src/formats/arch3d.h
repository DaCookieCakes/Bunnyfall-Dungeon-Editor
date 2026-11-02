#pragma once

#include "core/byte_view.h"
#include "core/error.h"

#include <cstdint>
#include <string>
#include <vector>

namespace de::formats {

/// ARCH3D point units: 256 per block unit, so 256 * 40 = 1 metre.
constexpr double kArch3dUnitsPerBlockUnit = 256.0;

struct Arch3dPoint {
    std::int32_t x = 0, y = 0, z = 0;   ///< Daggerfall axes (Y down), ARCH3D units.
};

struct Arch3dPlaneVertex {
    std::uint32_t point = 0;   ///< Index into Arch3dMesh::points.
    std::int16_t u = 0, v = 0; ///< Raw texture coordinates as stored (decoding is the renderer's job).
};

struct Arch3dPlane {
    std::uint16_t texture = 0;
    std::uint8_t unknown1 = 0;
    std::uint32_t unknown2 = 0;
    std::int32_t nx = 0, ny = 0, nz = 0;   ///< Fixed-point normal as stored.
    std::vector<Arch3dPlaneVertex> vertices;
    [[nodiscard]] int textureArchive() const { return texture >> 7; }
    [[nodiscard]] int textureRecord() const { return texture & 0x7f; }
};

struct Arch3dBounds {
    Arch3dPoint min, max;
    [[nodiscard]] std::int64_t sizeX() const { return std::int64_t{max.x} - min.x; }
    [[nodiscard]] std::int64_t sizeY() const { return std::int64_t{max.y} - min.y; }
    [[nodiscard]] std::int64_t sizeZ() const { return std::int64_t{max.z} - min.z; }
};

enum class Arch3dVersion { V25, V26, V27 };

struct Arch3dMesh {
    std::uint32_t id = 0;
    Arch3dVersion version = Arch3dVersion::V27;
    std::uint32_t radius = 0;
    std::vector<Arch3dPoint> points;
    std::vector<Arch3dPlane> planes;
    Arch3dBounds bounds;   ///< Over every point of the mesh; all zero for a mesh without points.
};

/// Parses one ARCH3D.BSA record. `id` is the record's directory id (the model id blocks refer to).
/// Classic data only; Bunnyfall's byte patches to a few broken models (arch3dPatch) are not applied here.
Result<Arch3dMesh> parseArch3dMesh(ByteView record, std::uint32_t id);

} // namespace de::formats
