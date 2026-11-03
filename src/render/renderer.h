#pragma once

#include "render/camera.h"
#include "render/model_cache.h"
#include "render/shader.h"
#include "render/texture_cache.h"
#include "scene/dungeon_scene.h"
#include "scene/picking.h"

#include <optional>

namespace de::render {

struct DrawOptions {
    bool flats = true;      ///< Scenery flats (torches, bones, ...).
    bool markers = true;    ///< Editor markers (archive 199): start, enemies, treasure, ...
    bool cullBackFaces = true;
    bool wireframe = false;
};

/// Draws a scene: textured models, billboarded flats and markers, the selection tinted.
class Renderer {
public:
    static Result<Renderer> create(TextureCache& textures, ModelCache& models);

    void draw(const scene::Scene& scene, const Camera& camera, int width, int height, const DrawOptions& options,
              std::optional<scene::Hit> selection);

    Renderer(Renderer&&) noexcept;
    Renderer& operator=(Renderer&&) = delete;
    Renderer(const Renderer&) = delete;
    ~Renderer();

private:
    Renderer(TextureCache& textures, ModelCache& models) : textures_(&textures), models_(&models) {}

    TextureCache* textures_;
    ModelCache* models_;
    ShaderProgram modelShader_;
    ShaderProgram billboardShader_;
    GLuint quadVao_ = 0, quadVbo_ = 0;
};

} // namespace de::render
