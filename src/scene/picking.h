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
    enum class Kind { Model, Flat, Light } kind = Kind::Model;
    std::size_t index = 0;    ///< Into Scene::models, Scene::flats or Scene::lights.
    double distance = 0;      ///< Block units along the ray.
    catalog::Vec3 normal;     ///< The surface hit, facing back along the ray (a flat's faces the viewer).
};

/// The mesh drawn for a model id, or null when it has none.
using MeshLookup = std::function<const geometry::ModelMesh*(std::uint32_t modelId)>;
/// A flat's drawn size in block units (width, height).
using FlatSizeLookup = std::function<std::pair<double, double>(int archive, int record)>;

struct PickOptions {
    bool flats = true;     ///< Scenery and other visible flats.
    bool markers = true;   ///< Editor markers (archive 199).
    bool models = true;
    std::optional<ObjectRef> ignore;   ///< One object the ray passes through (one being moved).
    double lightSize = 0;              ///< > 0: lights are picked as spheres of this diameter.
    std::function<bool(Hit::Kind, std::size_t)> skip;   ///< Instances the ray passes through (hidden ones).
};

/// The nearest model triangle, flat or light the ray meets. Triangles count from both sides.
std::optional<Hit> pick(const Scene& scene, const Ray& ray, const MeshLookup& meshes, const FlatSizeLookup& flatSize,
                        PickOptions options = {});

/// Distance along the ray to the triangle (a, b, c), either side, if it is hit in front of the origin.
std::optional<double> intersectTriangle(const Ray& ray, catalog::Vec3 a, catalog::Vec3 b, catalog::Vec3 c);

} // namespace de::scene
