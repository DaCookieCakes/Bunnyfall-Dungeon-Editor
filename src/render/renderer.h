#pragma once

#include "render/camera.h"
#include "render/model_cache.h"
#include "render/shader.h"
#include "render/texture_cache.h"
#include "scene/dungeon_scene.h"
#include "scene/grid.h"
#include "scene/picking.h"

#include <optional>

namespace de::render {

struct DrawOptions {
    bool flats = true;      ///< Scenery flats (torches, bones, ...).
    bool markers = true;    ///< Editor markers (archive 199): start, enemies, treasure, ...
    bool lights = true;     ///< Light gizmos (lights have no picture of their own in the game).
    bool cullBackFaces = true;
    bool wireframe = false;
    bool grid = false;                ///< The sizing grid (block edges, cells, sockets).
    bool gridOnTop = true;            ///< Show the grid through walls.
    scene::GridOptions gridOptions;
};

/// Draws a scene: textured models, billboarded flats and markers, light gizmos, the selection tinted.
/// Light gizmos are this size, in block units (picking uses the same).
inline constexpr double kLightGizmoSize = 16;
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
    GLuint lightIcon_ = 0;   // the gizmo drawn for a light
    ShaderProgram lineShader_;
    GLuint lineVao_ = 0, lineVbo_ = 0;

    void drawLines(const std::vector<scene::GridLine>& lines, const Mat4& viewProj, bool onTop);
};

} // namespace de::render
