#pragma once

#include "catalog/space.h"
#include "core/error.h"
#include "formats/arch3d.h"

#include <cstdint>
#include <vector>

namespace de::geometry {

/// A model ready to draw: triangles grouped by texture, in editor space (block units, Y up).
struct ModelMesh {
    struct Vertex {
        catalog::Vec3 position;
        catalog::Vec3 normal;   ///< Unit length.
        float u = 0, v = 0;     ///< Texels; divide by the texture's width and height. v counts down from the top.
    };
    struct SubMesh {
        int textureArchive = 0;
        int textureRecord = 0;
        std::uint32_t firstIndex = 0;
        std::uint32_t indexCount = 0;
    };

    std::uint32_t modelId = 0;
    std::vector<Vertex> vertices;
    std::vector<std::uint32_t> indices;   ///< Triangles, Daggerfall Unity's winding.
    std::vector<SubMesh> subMeshes;       ///< In order of each texture's first use.
};

/// Builds the drawable mesh the way Daggerfall Unity decomposes ARCH3D (Arch3dFile.cs, FaceUVTool.cs):
/// - each face becomes a triangle fan;
/// - three-point faces take their UVs as deltas from the previous point;
/// - larger faces keep only their corner points and get UVs from a matrix fitted to the first three;
/// - models below id 905 have packed UVs on their first three points, unpacked first.
/// A face Daggerfall Unity cannot decompose (zero-length edges) fails the model, as it does there.
Result<ModelMesh> buildModelMesh(const formats::Arch3dMesh& mesh);

/// Daggerfall Unity's UVunpack: undoes the packing of an out-of-range texture coordinate.
std::int32_t unpackUv(std::int32_t value);

} // namespace de::geometry
