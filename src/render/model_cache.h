#pragma once

#include "formats/bsa.h"
#include "geometry/model_mesh.h"

#include <epoxy/gl.h>

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace de::render {

/// One model on the GPU, with the CPU mesh kept for picking.
struct GpuModel {
    GLuint vao = 0, vbo = 0, ebo = 0;
    geometry::ModelMesh mesh;
};

/// ARCH3D models built and uploaded on first use. A model that will not build is skipped and reported once.
class ModelCache {
public:
    explicit ModelCache(const formats::BsaArchive& arch3d) : arch3d_(arch3d) {}
    ~ModelCache();
    ModelCache(const ModelCache&) = delete;
    ModelCache& operator=(const ModelCache&) = delete;

    /// The model, or null if it has no mesh.
    const GpuModel* get(std::uint32_t id);

    [[nodiscard]] const std::vector<std::string>& problems() const { return problems_; }

private:
    const formats::BsaArchive& arch3d_;
    std::map<std::uint32_t, std::optional<GpuModel>> models_;
    std::vector<std::string> problems_;
};

} // namespace de::render
