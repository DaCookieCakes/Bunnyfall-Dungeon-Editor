#pragma once

#include "catalog/catalog.h"
#include "formats/arena2.h"
#include "formats/maps.h"
#include "render/renderer.h"

#include <SDL3/SDL.h>

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace de::app {

/// The dungeon viewer: what is loaded, how it is looked at, and the panels around it.
class Viewer {
public:
    static Result<std::unique_ptr<Viewer>> create(const formats::Arena2& arena2);

    /// Show a classic dungeon (index into dungeons()) or a single block.
    Result<void> openDungeon(std::size_t index);
    Result<void> openBlock(const std::string& name);
    /// The first dungeon whose name contains `key`, or whose location id is `key`.
    [[nodiscard]] std::optional<std::size_t> findDungeon(const std::string& key) const;

    /// Put the camera at the start marker, or above the middle of the scene looking down.
    void cameraToStart();
    void cameraToTop();

    void handleEvent(const SDL_Event& event, SDL_Window* window);
    void update(double seconds);
    /// Panels for this frame (between ImGui::NewFrame and ImGui::Render).
    void drawUi();
    /// The 3D view into the bound framebuffer.
    void drawScene(int pixelWidth, int pixelHeight);

    void setViewSize(double logicalWidth, double logicalHeight) {
        viewWidth_ = logicalWidth;
        viewHeight_ = logicalHeight;
    }

    [[nodiscard]] const std::vector<formats::DungeonRecord>& dungeons() const { return dungeons_; }
    render::Camera& camera() { return camera_; }
    /// Everything the texture and model caches could not load so far.
    [[nodiscard]] std::vector<std::string> problems() const;

private:
    Viewer(const formats::Arena2& arena2, catalog::Catalog catalog, std::vector<formats::DungeonRecord> dungeons,
           formats::Palette palette);

    void setScene(scene::Scene scene);
    void pickAt(double x, double y);
    void drawBrowser();
    void drawInspector();
    void drawViewPanel();

    const formats::Arena2& arena2_;
    catalog::Catalog catalog_;
    std::vector<formats::DungeonRecord> dungeons_;
    render::TextureCache textures_;
    render::ModelCache models_;
    std::optional<render::Renderer> renderer_;

    scene::Scene scene_;
    render::Camera camera_;
    render::DrawOptions drawOptions_;
    std::optional<scene::Hit> selection_;
    std::string status_;

    double viewWidth_ = 1, viewHeight_ = 1;
    bool looking_ = false;     // right mouse held: mouse turns the camera
    double speed_ = 600;       // block units per second
    char filter_[64] = {};
};

} // namespace de::app
