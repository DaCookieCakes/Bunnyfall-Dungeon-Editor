#include "render/model_cache.h"

#include <cstddef>

namespace de::render {

namespace {

struct PackedVertex {
    float position[3];
    float normal[3];
    float uv[2];
};

std::optional<GpuModel> upload(geometry::ModelMesh mesh) {
    std::vector<PackedVertex> packed;
    packed.reserve(mesh.vertices.size());
    for (const auto& v : mesh.vertices) {
        packed.push_back({{float(v.position.x), float(v.position.y), float(v.position.z)},
                          {float(v.normal.x), float(v.normal.y), float(v.normal.z)},
                          {v.u, v.v}});
    }
    GpuModel m;
    glGenVertexArrays(1, &m.vao);
    glGenBuffers(1, &m.vbo);
    glGenBuffers(1, &m.ebo);
    glBindVertexArray(m.vao);
    glBindBuffer(GL_ARRAY_BUFFER, m.vbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(packed.size() * sizeof(PackedVertex)), packed.data(),
                 GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m.ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(mesh.indices.size() * sizeof(std::uint32_t)),
                 mesh.indices.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(PackedVertex),
                          reinterpret_cast<void*>(offsetof(PackedVertex, position)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(PackedVertex),
                          reinterpret_cast<void*>(offsetof(PackedVertex, normal)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(PackedVertex),
                          reinterpret_cast<void*>(offsetof(PackedVertex, uv)));
    glBindVertexArray(0);
    m.mesh = std::move(mesh);
    return m;
}

} // namespace

ModelCache::~ModelCache() {
    for (auto& [_, m] : models_) {
        if (!m) continue;
        glDeleteVertexArrays(1, &m->vao);
        glDeleteBuffers(1, &m->vbo);
        glDeleteBuffers(1, &m->ebo);
    }
}

const GpuModel* ModelCache::get(std::uint32_t id) {
    auto it = models_.find(id);
    if (it == models_.end()) {
        std::optional<GpuModel> model;
        const auto index = arch3d_.findById(id);
        if (!index) {
            problems_.push_back("model " + std::to_string(id) + " is not in ARCH3D.BSA");
        } else if (auto raw = formats::parseArch3dMesh(arch3d_.record(*index), id); !raw) {
            problems_.push_back(raw.error().message);
        } else if (auto mesh = geometry::buildModelMesh(*raw); !mesh) {
            problems_.push_back(mesh.error().message);
        } else {
            model = upload(std::move(*mesh));
        }
        it = models_.emplace(id, std::move(model)).first;
    }
    return it->second ? &*it->second : nullptr;
}

} // namespace de::render
