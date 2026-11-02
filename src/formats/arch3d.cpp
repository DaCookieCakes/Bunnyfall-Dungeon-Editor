#include "formats/arch3d.h"

#include <algorithm>

namespace de::formats {

namespace {
constexpr std::size_t kPointSize = 12;
constexpr std::size_t kNormalSize = 12;
constexpr std::size_t kPlaneHeaderSize = 8;
constexpr std::size_t kPlaneVertexSize = 8;
// Limits well above anything classic, so a corrupt count fails cleanly instead of allocating gigabytes.
constexpr std::int32_t kMaxPoints = 1 << 20;
constexpr std::int32_t kMaxPlanes = 1 << 20;
} // namespace

Result<Arch3dMesh> parseArch3dMesh(ByteView v, std::uint32_t id) {
    try {
        Arch3dMesh mesh;
        mesh.id = id;
        const auto version = v.fixedString(0, 4);
        if (version == "v2.7") mesh.version = Arch3dVersion::V27;
        else if (version == "v2.6") mesh.version = Arch3dVersion::V26;
        else if (version == "v2.5") mesh.version = Arch3dVersion::V25;
        else return fail("model " + std::to_string(id) + ": unknown version '" + version + "'");

        const auto pointCount = v.i32(4);
        const auto planeCount = v.i32(8);
        mesh.radius = v.u32(12);
        const auto pointListOffset = v.i32(48);
        const auto normalListOffset = v.i32(52);
        const auto planeListOffset = v.i32(60);
        if (pointCount < 0 || pointCount > kMaxPoints || planeCount < 0 || planeCount > kMaxPlanes) {
            return fail("model " + std::to_string(id) + ": implausible counts");
        }
        if (pointListOffset < 0 || normalListOffset < 0 || planeListOffset < 0) {
            return fail("model " + std::to_string(id) + ": negative section offset");
        }

        mesh.points.resize(static_cast<std::size_t>(pointCount));
        for (std::size_t i = 0; i < mesh.points.size(); ++i) {
            const auto at = static_cast<std::size_t>(pointListOffset) + i * kPointSize;
            mesh.points[i] = {v.i32(at), v.i32(at + 4), v.i32(at + 8)};
        }
        if (!mesh.points.empty()) {
            mesh.bounds.min = mesh.bounds.max = mesh.points.front();
            for (const auto& p : mesh.points) {
                mesh.bounds.min = {std::min(mesh.bounds.min.x, p.x), std::min(mesh.bounds.min.y, p.y),
                                   std::min(mesh.bounds.min.z, p.z)};
                mesh.bounds.max = {std::max(mesh.bounds.max.x, p.x), std::max(mesh.bounds.max.y, p.y),
                                   std::max(mesh.bounds.max.z, p.z)};
            }
        }

        // A plane vertex stores its point as a byte offset into the point list; v2.5 stores a quarter of it.
        const std::size_t offsetScale = mesh.version == Arch3dVersion::V25 ? 3 : 1;
        auto pos = static_cast<std::size_t>(planeListOffset);
        mesh.planes.resize(static_cast<std::size_t>(planeCount));
        for (std::size_t p = 0; p < mesh.planes.size(); ++p) {
            auto& plane = mesh.planes[p];
            const auto vertexCount = v.u8(pos);
            plane.unknown1 = v.u8(pos + 1);
            plane.texture = v.u16(pos + 2);
            plane.unknown2 = v.u32(pos + 4);
            pos += kPlaneHeaderSize;

            const auto normalAt = static_cast<std::size_t>(normalListOffset) + p * kNormalSize;
            plane.nx = v.i32(normalAt);
            plane.ny = v.i32(normalAt + 4);
            plane.nz = v.i32(normalAt + 8);

            plane.vertices.resize(vertexCount);
            for (auto& vertex : plane.vertices) {
                const auto rawOffset = v.i32(pos);
                vertex.u = v.i16(pos + 4);
                vertex.v = v.i16(pos + 6);
                pos += kPlaneVertexSize;
                if (rawOffset < 0) return fail("model " + std::to_string(id) + ": negative point offset");
                const auto byteOffset = static_cast<std::size_t>(rawOffset) * offsetScale;
                if (byteOffset % kPointSize != 0 || byteOffset / kPointSize >= mesh.points.size()) {
                    return fail("model " + std::to_string(id) + ": plane " + std::to_string(p) +
                                " names a point outside the list");
                }
                vertex.point = static_cast<std::uint32_t>(byteOffset / kPointSize);
            }
        }
        return mesh;
    } catch (const OutOfBounds& e) {
        return fail("model " + std::to_string(id) + ": truncated: " + e.what());
    }
}

} // namespace de::formats
