#pragma once

#include "render/camera.h"
#include "render/model_cache.h"
#include "render/shader.h"
#include "render/texture_cache.h"
#include "scene/dungeon_scene.h"
#include "scene/grid.h"
#include "scene/picking.h"

#include <cstdint>
#include <optional>
#include <vector>

namespace de::render {

struct DrawOptions {
    bool flats = true;      ///< Scenery flats (torches, bones, ...).
    bool markers = true;    ///< Editor markers (archive 199): start, enemies, treasure, ...
    bool lights = true;     ///< Light gizmos (lights have no picture of their own in the game).
    bool cullBackFaces = true;
    bool wireframe = false;
    bool wireColours = false;         ///< Wireframe in one even, vivid colour per texture (the 2D views).
    bool grid = false;                ///< The sizing grid (block edges, cells, sockets).
    bool gridOnTop = true;            ///< Show the grid through walls.
    scene::GridOptions gridOptions;
};

/// Per-instance state for drawing: selected (tinted) or hidden (not drawn). Indexed like Scene::models, flats and
/// lights; missing entries are neither.
struct InstanceFlags {
    static constexpr std::uint8_t kSelected = 1, kHidden = 2;
    std::vector<std::uint8_t> models, flats, lights;
    [[nodiscard]] static std::uint8_t at(const std::vector<std::uint8_t>& v, std::size_t i) { return i < v.size() ? v[i] : 0; }
};

/// Draws a scene: textured models, billboarded flats and markers, light gizmos, the selection tinted.
/// Light gizmos are this size, in block units (picking uses the same).
inline constexpr double kLightGizmoSize = 16;
class Renderer {
public:
    static Result<Renderer> create(TextureCache& textures, ModelCache& models);

    void draw(const scene::Scene& scene, const Camera& camera, int width, int height, const DrawOptions& options,
              const InstanceFlags& flags);

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
