#pragma once

#include "geometry/model_mesh.h"
#include "scene/dungeon_scene.h"

#include <functional>
#include <optional>

namespace de::scene {

struct Ray {
    catalog::Vec3 origin;
    catalog::Vec3 direction;   ///< Unit length.
};

struct Hit {
    enum class Kind { Model, Flat } kind = Kind::Model;
    std::size_t index = 0;    ///< Into Scene::models or Scene::flats.
    double distance = 0;      ///< Block units along the ray.
};

/// The mesh drawn for a model id, or null when it has none.
using MeshLookup = std::function<const geometry::ModelMesh*(std::uint32_t modelId)>;
/// A flat's drawn size in block units (width, height).
using FlatSizeLookup = std::function<std::pair<double, double>(int archive, int record)>;

struct PickOptions {
    bool flats = true;     ///< Scenery and other visible flats.
    bool markers = true;   ///< Editor markers (archive 199).
};

/// The nearest model triangle or flat the ray meets. Triangles count from both sides.
std::optional<Hit> pick(const Scene& scene, const Ray& ray, const MeshLookup& meshes, const FlatSizeLookup& flatSize,
                        PickOptions options = {});

/// Distance along the ray to the triangle (a, b, c), either side, if it is hit in front of the origin.
std::optional<double> intersectTriangle(const Ray& ray, catalog::Vec3 a, catalog::Vec3 b, catalog::Vec3 c);

} // namespace de::scene
