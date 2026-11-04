#pragma once

#include "catalog/catalog.h"
#include "edit/block_edit.h"
#include "edit/entities.h"
#include "export/patch_folder.h"
#include "formats/arena2.h"
#include "formats/maps.h"
#include "project/project.h"
#include "project/world_export.h"
#include "render/renderer.h"

#include <SDL3/SDL.h>

#include <array>

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace de::app {

/// The editor's window contents: what is loaded, how it is looked at, the block being edited, and the panels.
class Viewer {
public:
    static Result<std::unique_ptr<Viewer>> create(const formats::Arena2& arena2);

    /// Where edited blocks are saved as patches, and read from (Bunnyfall's vendor/<mod>/WorldDataPatches).
    void setPatchFolder(std::filesystem::path dir) { patches_.emplace(std::move(dir)); }
    /// The mod folder a project exports to (Bunnyfall's vendor/<mod>/); its WorldDataPatches is the patch folder.
    void setModFolder(std::filesystem::path dir);
    /// The project file: loaded if it exists, else a new project saved there on first save.
    Result<void> setProjectFile(std::filesystem::path file);
    [[nodiscard]] project::Project& project() { return project_; }
    /// Show one of the project's dungeons (by number) in the 3D view.
    Result<void> openProjectDungeon(int number);

    /// Show a classic dungeon (index into dungeons()) or a single block. Blocks with a saved patch show patched.
    Result<void> openDungeon(std::size_t index);
    Result<void> openBlock(const std::string& name);
    /// The first dungeon whose name contains `key`, or whose location id is `key`.
    [[nodiscard]] std::optional<std::size_t> findDungeon(const std::string& key) const;

    /// Put the camera where a new game starts (the enter marker, else the start marker), at the entrance (the start
    /// marker), or above the middle of the scene looking down.
    void cameraToStart();
    void cameraToEntrance();
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
    /// Turn the sizing grid on, at the floor below the camera (or `height` when given).
    void showGrid(std::optional<double> height = std::nullopt);
    /// Everything the texture and model caches could not load so far.
    [[nodiscard]] std::vector<std::string> problems() const;
    /// Unsaved edits (the window asks before closing on them): a block, or the project.
    [[nodiscard]] bool hasUnsavedEdits() const { return (edit_ && edit_->dirty()) || projectDirty_; }
    /// The window was asked to close: closes at once with nothing unsaved, else asks first (wantsQuit says when).
    void requestQuit();
    [[nodiscard]] bool wantsQuit() const { return quit_; }

    /// A scripted run of the project tools (--check-project): a new dungeon on free land, a custom block copied from a
    /// classic one, caps all round, checked and exported to `modDir`. Answers a line per step.
    Result<std::string> checkProject(const std::filesystem::path& modDir);

    /// A scripted run of the edit tools, as clicks would drive them (--check-edit): edits the starting block, places
    /// one of every kind on the surface ahead, moves one, undoes and redoes, saves to `patchDir`, reloads the patch and
    /// compares. Leaves the edit in place (for a screenshot). Answers a line per step.
    Result<std::string> checkEdit(const std::filesystem::path& patchDir);

private:
    Viewer(const formats::Arena2& arena2, catalog::Catalog catalog, std::vector<formats::DungeonRecord> dungeons,
           formats::Palette palette);

    // ---- viewing (viewer.cpp) ----
    void setScene(scene::Scene scene);
    bool cameraToMarker(int record);
    [[nodiscard]] scene::BlockSource blockSource() const;
    [[nodiscard]] std::optional<scene::Hit> pickAt(double x, double y, scene::PickOptions options);
    [[nodiscard]] scene::PickOptions selectOptions() const;
    void select(std::optional<scene::Hit> hit);
    /// The drawn instance of the selected object, found again after the scene is rebuilt.
    [[nodiscard]] std::optional<scene::Hit> selectionHit() const;
    void drawBrowser();
    void drawInspector();
    void drawViewPanel();
    /// Editor Y of the floor straight below a point, if there is one.
    [[nodiscard]] std::optional<double> floorBelow(catalog::Vec3 point);
    /// Editor Y of the selected object's base (a flat's or a light's bottom, a model's origin).
    [[nodiscard]] std::optional<double> selectedBase();

    // ---- editing (viewer_edit.cpp) ----
    /// Start editing the block `sceneBlock` of the scene (its saved patch, if any, is where editing starts).
    void beginEdit(std::size_t sceneBlock);
    void endEdit();
    /// After any change to the edited block: every copy of it in the scene shows the change.
    void editChanged();
    void saveEdit();
    [[nodiscard]] bool editing(const scene::ObjectRef& ref) const;
    [[nodiscard]] std::optional<edit::ObjectSlot> selectedSlot() const;
    /// The editor-space point where the pointer meets the dungeon, and the scene block it belongs to.
    struct SurfacePoint {
        catalog::Vec3 point, normal;
        std::size_t sceneBlock;
    };
    [[nodiscard]] std::optional<SurfacePoint> surfaceAt(double x, double y);
    void placeAt(double x, double y);
    void startGrab();
    void grabTo(double x, double y);
    void endGrab(bool keep);
    void nudge(int dx, int dy, int dz);
    void deleteSelected();
    void duplicateSelected();
    bool handleEditKey(const SDL_KeyboardEvent& key);
    void drawEditPanel();
    void drawEditInspector(edit::ObjectSlot slot);
    void drawConfirmations();

    // ---- the project (viewer_project.cpp) ----
    void drawProjectPanel();
    void drawDungeonSettings(project::ProjectDungeon& dungeon);
    void drawGrid(project::ProjectDungeon& dungeon);
    void drawProjectBlocks();
    void saveProject();
    void exportProject();
    void markProjectChanged() { projectDirty_ = true; }
    /// Every empty side of a non-cap block gets a classic border cap.
    int addCaps(project::ProjectDungeon& dungeon);
    [[nodiscard]] std::optional<std::pair<int, int>> freePixel(int region, int nearX, int nearY) const;
    [[nodiscard]] bool isProjectBlock(const std::string& name) const { return project_.block(name) != nullptr; }
    Result<void> openProjectBlock(const std::string& name);
    /// What a block (classic or the project's) is used by: dungeons in the world and in the project.
    [[nodiscard]] std::pair<std::ptrdiff_t, std::ptrdiff_t> usersOf(const std::string& name) const;
    /// Open the project dungeon and start editing the block at a grid cell, the camera over it.
    void editCell(int dungeonNumber, int x, int z);
    void refreshIssues();
    /// Puts the scene's copies of the edited block back to what is saved, and stops editing.
    void discardEdit();
    [[nodiscard]] std::pair<double, double> entitySize(const formats::RdbObject& obj);
    /// The file coordinates (block-local) of an editor-space point in a scene block.
    [[nodiscard]] std::array<std::int32_t, 3> toFile(catalog::Vec3 world, std::size_t sceneBlock) const;

    const formats::Arena2& arena2_;
    catalog::Catalog catalog_;
    std::vector<formats::DungeonRecord> dungeons_;
    render::TextureCache textures_;
    render::ModelCache models_;
    std::optional<render::Renderer> renderer_;
    std::optional<exporter::PatchFolder> patches_;

    scene::Scene scene_;
    render::Camera camera_;
    render::DrawOptions drawOptions_;
    std::optional<scene::ObjectRef> selected_;   // what is selected, by where it lives in the blocks
    double selectedDistance_ = 0;
    std::string status_;

    // the project
    project::Project project_;
    std::filesystem::path projectFile_;
    std::optional<std::filesystem::path> modDir_;
    bool projectDirty_ = false;
    std::unique_ptr<project::World> world_;
    int currentDungeon_ = -1;                    // the project dungeon the panel shows
    std::string gridBlock_ = "N0000071.RDB";     // what a click on an empty grid cell puts there
    char gridFilter_[32] = {};
    char newBlockTemplate_[16] = "N0000071";
    char newBlockLabel_[64] = "";
    int newBlockRole_ = 0;                       // index into "NWSMB"
    std::vector<project::Issue> issues_;
    bool issuesStale_ = true;
    std::optional<std::pair<int, int>> gridMenuCell_;   // the cell a right-click menu is open on
    std::vector<std::string> entranceBlocks_;           // surface blocks a dungeon can be entered by (filled once)
    bool confirmDeleteDungeon_ = false;

    // editing
    std::optional<edit::BlockEdit> edit_;
    std::size_t editBlock_ = 0;                  // the scene block the edit started from
    std::optional<edit::EntityKind> placing_;    // a palette entry is armed: the next click places it
    int sceneryArchive_ = 210, sceneryRecord_ = 0;
    int enemyType_ = 7;                          // Orc
    int snap_ = 16;                              // file units; 1 = off
    bool grabbing_ = false;
    std::size_t grabBlock_ = 0;                  // the scene block the grab is measured in
    bool confirmDiscard_ = false;
    bool confirmQuit_ = false;
    bool quit_ = false;

    double viewWidth_ = 1, viewHeight_ = 1;
    double mouseX_ = 0, mouseY_ = 0;
    bool looking_ = false;     // right mouse held: mouse turns the camera
    double speed_ = 600;       // block units per second
    char filter_[64] = {};
};

} // namespace de::app
