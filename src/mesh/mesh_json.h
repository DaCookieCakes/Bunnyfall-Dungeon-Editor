#pragma once

// A custom model's mesh as JSON, two ways:
// - the editor's own form, kept in the project file: points and faces with their textures and alignments, exactly;
// - the file Bunnyfall loads (`vendor/<mod>/Models/<id>.json`, format "bunnyfall-dungeon-model/1"): triangles in the
//   shape Bunnyfall's custom model registry takes (world/customModels.js, dfMeshToModel's output): metres, Unity's
//   frame (editor space scaled by 0.025), unit normals, UVs as Daggerfall Unity makes them (u / width,
//   -(v / height), wrapping), and sub-meshes by texture with a start index and a triangle count. Daggerfall's
//   texture numbers stay in it, so a dungeon's texture table swaps a custom wall like a classic one.

#include "core/error.h"
#include "mesh/mesh.h"

#include <nlohmann/json.hpp>

#include <functional>
#include <string_view>
#include <utility>

namespace de::mesh {

using Json = nlohmann::json;

inline constexpr std::string_view kModelFileFormat = "bunnyfall-dungeon-model/1";

Json meshToJson(const Mesh& mesh);
Result<Mesh> meshFromJson(const Json& json);

/// A texture's size in texels (width, height), for turning texel UVs into Daggerfall Unity's. The base archive's
/// size, as dfMeshToModel reads it before a dungeon's table swaps the picture.
using TextureSize = std::function<std::pair<int, int>(int archive, int record)>;

/// The model file Bunnyfall loads. Values are rounded (positions to 0.1 mm) so files stay small and stable.
Json bunnyfallModelJson(const Mesh& mesh, std::uint32_t modelId, std::string_view name, const TextureSize& size);

} // namespace de::mesh
