#pragma once

#include "catalog/catalog.h"
#include "edit/block_edit.h"
#include "edit/entities.h"
#include "edit/transform.h"
#include "export/patch_folder.h"
#include "formats/arena2.h"
#include "formats/maps.h"
#include "formats/text_rsc.h"
#include "logic/action.h"
#include "logic/edit.h"
#include "project/prefab.h"
#include "project/project.h"
#include "project/world_export.h"
#include "app/config.h"
#include "render/render_target.h"
#include "render/renderer.h"
#include "render/thumbnails.h"

#include <SDL3/SDL.h>
#include <imgui.h>

#include <array>
#include <memory>
#include <unordered_set>
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

    /// The window it draws in (for the mouse-look grab and file dialogs) and the settings it may change.
    void attach(SDL_Window* window, Config* config) {
        window_ = window;
        config_ = config;
    }
    void handleEvent(const SDL_Event& event);
    void update(double seconds);
    /// The menu, toolbar, panels and view windows for this frame (between ImGui::NewFrame and ImGui::Render).
    void drawUi();
    /// Draws every view the UI shows into its own picture (after ImGui::Render, before the UI is drawn).
    void renderViews(float pixelsPerPoint);
    /// The perspective view alone into the bound framebuffer (screenshots without the UI).
    void drawPerspective(int pixelWidth, int pixelHeight);

    /// The perspective view's size when no UI lays it out (screenshots and scripted checks).
    void setViewSize(double logicalWidth, double logicalHeight) {
        viewWidth_ = logicalWidth;
        viewHeight_ = logicalHeight;
        views_[0].size = {float(logicalWidth), float(logicalHeight)};
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

    /// Selects the first object at this offset (in any block) and frames it. For scripted screenshots.
    bool selectAtOffset(std::int32_t offset);

private:
    Viewer(const formats::Arena2& arena2, catalog::Catalog catalog, std::vector<formats::DungeonRecord> dungeons,
           formats::Palette palette);

    // ---- the views (viewer_views.cpp) ----
    enum class ViewKind { Perspective, Top, Front, Side };
    struct View {
        ViewKind kind = ViewKind::Perspective;
        const char* name = "";
        catalog::Vec3 centre;          ///< Orthographic: the world point in the middle of the view.
        double halfHeight = 1500;      ///< Orthographic zoom: block units from the middle to the top edge.
        render::RenderTarget target;
        ImVec2 min{}, size{};          ///< Where the UI showed it last frame (screen points).
        bool shown = false;            ///< Laid out this frame (so drawn by renderViews).
    };
    [[nodiscard]] render::Camera cameraOf(std::size_t view) const;
    /// The ray under a point of the view the pointer is in (pointerView_).
    [[nodiscard]] scene::Ray rayAt(double x, double y) const;
    void drawViewsWindow();
    void drawView(std::size_t index, ImVec2 min, ImVec2 size);
    void drawOrthoGrid(std::size_t index);
    void viewInput(std::size_t index, bool overHandle);
    /// Fit the orthographic views around the scene.
    void frameOrthoViews();

    // ---- selection, hiding and gizmos (viewer_select.cpp) ----
    /// What can be shown or hidden as a kind.
    enum class Category { Architecture, Door, Prop, Scenery, Enemy, Treasure, Quest, StartEnter, OtherMarker, Light, Count };
    static constexpr std::size_t kCategories = static_cast<std::size_t>(Category::Count);
    static const char* categoryName(Category c);
    [[nodiscard]] Category categoryOf(scene::Hit::Kind kind, std::size_t index) const;
    [[nodiscard]] const scene::ObjectRef& refOf(scene::Hit::Kind kind, std::size_t index) const;
    /// Not drawn and not picked: its kind is hidden, it was hidden by hand, or another block is isolated.
    [[nodiscard]] bool isHidden(scene::Hit::Kind kind, std::size_t index) const;
    void updateFlags();
    [[nodiscard]] bool isSelected(const scene::ObjectRef& ref) const { return selectedSet_.contains(ref); }
    [[nodiscard]] std::optional<scene::ObjectRef> primary() const {
        return selection_.empty() ? std::nullopt : std::optional(selection_.back());
    }
    void setSelection(std::vector<scene::ObjectRef> refs);
    void clearSelection() { setSelection({}); }
    /// A click on `hit` (or on nothing): replaces the selection, or with Shift adds to it, with Ctrl toggles.
    void clickSelect(std::optional<scene::Hit> hit, bool add, bool toggle);
    /// Everything not hidden whose middle lands inside the screen rectangle of a view.
    void boxSelect(std::size_t view, ImVec2 a, ImVec2 b, bool add, bool toggle);
    void selectAll();
    void hideSelected();
    void showAll();
    void toggleIsolate();
    /// The selected objects of the edited block, each once (copies of the block share them).
    [[nodiscard]] std::vector<edit::ObjectSlot> selectedSlots() const;
    /// Middle of the selected objects of the edited block, editor space (in the primary's scene block).
    [[nodiscard]] std::optional<catalog::Vec3> selectionCentre() const;
    /// A move or turn of the selection: snapshot, then apply offsets/turns to the snapshot, then keep or cancel.
    void beginTransform();
    void applyTransform(std::array<std::int32_t, 3> offset, std::int32_t turn);
    void endTransform(bool keep);
    /// Where a world point shows in a view (screen points), if in front of it; and block units per point there.
    [[nodiscard]] std::optional<ImVec2> toScreen(std::size_t view, catalog::Vec3 world) const;
    [[nodiscard]] double unitsPerPoint(std::size_t view, catalog::Vec3 at) const;
    /// Draws the move or turn handles in a view and runs their drags. True while the pointer is over a handle.
    bool drawGizmo(std::size_t view);
    /// Runs the drag in progress (box, handle, or a selected object dragged), wherever the pointer is.
    void continueDrag();
    void drawShowMenu();

    /// The editor-space point where the pointer meets the dungeon, and the scene block it belongs to.
    struct SurfacePoint {
        catalog::Vec3 point, normal;
        std::size_t sceneBlock;
    };

    // ---- the model palette (viewer_palette.cpp) ----
    void drawModelPalette();
    [[nodiscard]] bool placingAnything() const {
        return placing_.has_value() || placingModel_.has_value() || placingPrefab_.has_value();
    }
    void stopPlacing() {
        setup_.reset();
        placing_.reset();
        placingModel_.reset();
        placingPrefab_.reset();
        ghostModels_.clear();
        ghostFlats_.clear();
    }
    // Prefabs: kept in a "prefabs" folder beside the project file.
    [[nodiscard]] std::filesystem::path prefabFolder() const { return projectFile_.parent_path() / "prefabs"; }
    void reloadPrefabs();
    void drawPrefabList();
    void armPrefab(std::size_t index);
    /// The selection (in the primary's block) as a prefab, anchored at the middle of its bottom.
    void savePrefabFromSelection(const std::string& name);
    /// Where the armed prefab would go for the pointer at (x, y): its anchor (file units) and scene block.
    [[nodiscard]] std::optional<std::pair<std::array<std::int32_t, 3>, std::size_t>> prefabPlacement(double x, double y);
    /// Fills the preview (ghostModels_, ghostFlats_) for what is armed, under the pointer.
    void updateGhost();
    void armModel(std::uint32_t id);
    /// The same pieces in other textures: the primary's variants, and texture sets for the whole selection.
    void drawTextureVariants();
    /// Every selected model that comes in texture archive `archive` (itself or a twin) is shown in it; one undo step.
    void selectionToTextureSet(int archive);
    /// Every selected model of `shapeKey`'s shape becomes `modelId`; one undo step.
    void selectionToModel(std::uint64_t shapeKey, std::uint32_t modelId);
    /// Where a model being placed would go for the pointer at (x, y): its instance (for the preview) and scene block.
    [[nodiscard]] std::optional<std::pair<scene::ModelInstance, std::size_t>> modelPlacement(double x, double y);
    /// Under the pointer: the dungeon's surface, else (empty space) the grid's level plane, or in the Front and Side
    /// views the plane through the view's middle. Only inside a scene block.
    [[nodiscard]] std::optional<SurfacePoint> placementSurface(double x, double y);

    // ---- the outliner (viewer_outliner.cpp) ----
    void drawOutliner();
    /// Fit the 2D views around the selection and back the 3D camera off until it is all in view.
    void frameSelection();
    /// The drawn instance of an object, if it is drawn.
    [[nodiscard]] std::optional<std::pair<scene::Hit::Kind, std::size_t>> instanceOf(const scene::ObjectRef& ref) const;

    // ---- the frame (viewer_layout.cpp) ----
    void drawMenuBar();
    void drawToolbar();
    void buildDefaultLayout(unsigned dockspace);

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
    void drawReadOnlyGeneral(const scene::ObjectRef& ref);
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
    /// The editor-space point where the pointer meets the dungeon (models only), and its scene block.
    [[nodiscard]] std::optional<SurfacePoint> surfaceAt(double x, double y);
    void placeAt(double x, double y);
    void startGrab();
    void grabTo(double x, double y);
    void endGrab(bool keep);
    void nudge(int dx, int dy, int dz);
    /// The selection turned clockwise (seen from above) about its middle, `turn` file rotation units.
    void turnSelected(std::int32_t turn);
    void deleteSelected();
    void duplicateSelected();
    bool handleEditKey(const SDL_KeyboardEvent& key);
    void drawEditPanel();
    void drawEditInspector(edit::ObjectSlot slot);
    /// Pickers shared by the Place list and Properties; true when something was picked (then close the popup).
    bool enemyPicker(int& type);
    bool sceneryPicker(int& archive, int& record);
    /// Opens Properties floating beside `at` (a double-click), or brings it to the front where it is docked.
    void openProperties(ImVec2 at);
    void drawConfirmations();

    // ---- logic (viewer_logic.cpp) ----
    struct RefHash {
        std::size_t operator()(const scene::ObjectRef& r) const {
            return (r.block * 1000003u) ^ (r.root * 7919u) ^ r.object;
        }
    };
    /// One action link in the scene, with its ends' places (cached until the scene changes).
    struct SceneLink {
        scene::ObjectRef from, to;   ///< `to` is meaningless for a missing target
        logic::LinkKind kind = logic::LinkKind::Chain;
        catalog::Vec3 a, b;
        std::optional<std::pair<scene::Hit::Kind, std::size_t>> fromInstance, toInstance;
    };
    const std::vector<SceneLink>& sceneLinks();
    [[nodiscard]] catalog::Vec3 objectCentre(const scene::ObjectRef& ref) const;
    /// Every object joined to the selection by links, either way.
    [[nodiscard]] std::unordered_set<scene::ObjectRef, RefHash> selectedChains();
    void drawLogicLinks(std::size_t view);
    void drawLogicSection(const scene::ObjectRef& ref);
    /// The editable part of the Logic section, for an object of the edited block.
    void drawLogicEditor(const scene::ObjectRef& ref, const std::vector<const SceneLink*>& incoming);
    void selectAndFrame(const scene::ObjectRef& ref);
    void drawViewContextMenu();
    /// The quick setups that change one object in place.
    enum class QuickSetup { UsedSetsOff, PlateSetsOff, SlidesUp, SwingsOpen, Teleporter, TrapHurts };
    void applyQuickSetup(const scene::ObjectRef& ref, QuickSetup kind);
    void setDoorLock(const scene::ObjectRef& ref, int lockIndex);
    /// Finishes a pending setup on the model just placed at `placed` (inside the placing's undo step).
    void finishSetup(const scene::ObjectRef& placed);
    /// Puts a changed object of the edited block back (one undo step, or part of an open gesture).
    void commitObject(const scene::ObjectRef& ref, formats::RdbObject obj);
    /// A left click while picking the next object for linkFrom_.
    void finishLink(std::optional<scene::Hit> hit);
    /// Takes an object out of its chain: whatever set it off sets off its next object instead.
    void takeOutOfChain(const scene::ObjectRef& ref);
    /// Plays what happens when `ref` is set off: its chain's moves, turns and swings, forward and back.
    void startPreview(const scene::ObjectRef& ref);
    /// Poses the previewed objects for this frame; undone by endPreviewPose.
    void poseForPreview(double seconds);
    void endPreviewPose();

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
    /// Snapped to `steps` (x, y, z file units), by default the snap setting across and none up.
    [[nodiscard]] std::array<std::int32_t, 3> toFile(catalog::Vec3 world, std::size_t sceneBlock,
                                                     std::optional<std::array<int, 3>> steps = std::nullopt) const;
    /// Architecture and doors: what piece snap puts on the classic grid.
    [[nodiscard]] bool isPiece(std::uint32_t modelId) const;
    [[nodiscard]] bool selectionHasPieces() const;
    /// The grid a move snaps to (x, y, z file units): with pieces and piece snap on, the classic grid (64 across, 128
    /// up; 96% and 94% of classic architecture sits on it), else the snap setting across and free up.
    [[nodiscard]] std::array<int, 3> moveSteps(bool pieces) const;
    /// Turn steps: quarter turns for pieces with piece snap on (99.9% of classic ones), else the turn snap.
    [[nodiscard]] int turnStep(bool pieces) const { return pieces && pieceSnap_ ? 512 : turnSnap_; }

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
    std::vector<scene::ObjectRef> selection_;              // what is selected; the last is the primary
    std::uint64_t sceneStamp_ = 0;                         // bumped whenever the scene's blocks change
    std::uint64_t linksStamp_ = ~std::uint64_t{0};
    std::vector<SceneLink> links_;
    enum class LinkMode { Off, Selection, All } linkMode_ = LinkMode::All;
    void cycleLinkMode() {
        linkMode_ = linkMode_ == LinkMode::All ? LinkMode::Selection : linkMode_ == LinkMode::Selection ? LinkMode::Off : LinkMode::All;
    }
    std::optional<formats::TextResource> texts_;           // TEXT.RSC, for the text actions
    bool showTriggersTab_ = false;                          // bring the Triggers tab to the front once
    std::optional<ImVec2> propertiesAt_;                    // where a double-click asked Properties to open
    char enemyFilter_[32] = {};
    int pickArchive_ = 0, pickRecord_ = 0;                  // the scenery picker's place, for a placed flat
    int focusInspectorFrames_ = 0;                         // bring Properties to the front for a few frames
    std::optional<scene::ObjectRef> linkFrom_;              // picking the next object for this one
    bool linkReverse_ = false;                              // picking what sets linkFrom_ off, not what it sets off
    // A right-click that didn't drag opens a menu for what was under the pointer.
    struct RightPress {
        std::size_t view = 0;
        ImVec2 at;
        double time = 0, moved = 0;
    };
    std::optional<RightPress> rightPress_;
    struct ContextMenu {
        std::optional<scene::ObjectRef> ref;
        ImVec2 at;
    };
    std::optional<ContextMenu> contextRequest_, context_;
    // A quick setup waiting for its new model to be placed (a lever or plate for a door).
    enum class Setup { LeverOpensDoor, PlateOpensDoor };
    struct PendingSetup {
        Setup kind;
        scene::ObjectRef door;
    };
    std::optional<PendingSetup> setup_;
    struct Preview {
        struct Item {
            std::optional<std::pair<scene::Hit::Kind, std::size_t>> instance;
            logic::Motion motion;
        };
        std::vector<Item> items;
        double time = 0, length = 0;
        std::uint64_t stamp = 0;                            // the scene it was made for
    };
    std::optional<Preview> preview_;
    std::vector<std::pair<std::size_t, scene::Transform>> posedModels_;   // real transforms, while posed
    std::vector<std::pair<std::size_t, catalog::Vec3>> posedFlats_;
    std::unordered_set<scene::ObjectRef, RefHash> selectedSet_;
    std::unordered_set<scene::ObjectRef, RefHash> hidden_;  // hidden one by one (H)
    std::array<bool, kCategories> shown_{true, true, true, true, true, true, true, true, true, true};
    std::optional<std::size_t> isolated_;                   // the one scene block shown
    render::InstanceFlags flags_;
    enum class GizmoMode { Move, Turn } gizmoMode_ = GizmoMode::Move;
    bool pieceSnap_ = true;                                 // architecture and doors on the classic grid
    int turnSnap_ = 128;                                    // file rotation units (22.5 degrees); 1 = off
    // A drag in a view: a box being drawn, or a handle (or a selected object) being dragged.
    struct Drag {
        enum class Kind { None, Pending, Box, Axis, Plane, Turn } kind = Kind::None;
        std::size_t view = 0;
        ImVec2 press{};                 // where the button went down (screen points)
        int axis = 0;                   // Axis: 0 X, 1 Y, 2 Z (world)
        ImVec2 screenAxis{};            // Axis: the handle's direction on screen, unit length
        double unitsPerPoint = 1;
        catalog::Vec3 centre;           // the selection's middle when the drag began
        double startYaw = 0, lastYaw = 0, turned = 0;   // Turn: radians, unwrapped
        std::array<std::int32_t, 3> lastOffset{};       // what was last applied (to apply only changes)
        std::int32_t lastTurn = 0;
        bool add = false, toggle = false;   // Shift, Ctrl at the press
    } drag_;
    std::vector<edit::Moving> moving_;                      // the snapshot a transform works from
    double pivotX_ = 0, pivotZ_ = 0;                        // its turning point, file units
    std::size_t movingBlock_ = 0;                           // the scene block it is measured in
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
    std::optional<std::uint32_t> placingModel_;  // a model is armed instead
    std::int32_t placeTurn_ = 0;                 // the armed model's yRotation ([ and ] turn it)
    std::optional<std::size_t> placingPrefab_;   // or a prefab (index into prefabs_)
    std::vector<project::Prefab> prefabs_;
    char prefabName_[64] = "";
    std::vector<scene::ModelInstance> ghostModels_;   // the preview of what a click would place, drawn tinted
    std::vector<scene::FlatInstance> ghostFlats_;
    render::Thumbnails thumbnails_;
    char modelFilter_[32] = {};
    int paletteKind_ = 0;                        // Architecture, Doors, Props, All
    float thumbSize_ = 72;
    bool showModels_ = true;
    bool showUnusedModels_ = false;
    bool showOutliner_ = true;
    char outlinerFilter_[48] = {};
    int outlinerKind_ = -1;                      // a Category, or -1 for all              // the palette lists variants no classic block uses
    int sceneryArchive_ = 210, sceneryRecord_ = 0;
    int enemyType_ = 7;                          // Orc
    int snap_ = 16;                              // file units; 1 = off
    bool grabbing_ = false;
    std::size_t grabBlock_ = 0;                  // the scene block the grab is measured in
    bool confirmDiscard_ = false;
    bool confirmQuit_ = false;
    bool quit_ = false;

    double viewWidth_ = 1, viewHeight_ = 1;
    std::array<View, 4> views_;
    std::size_t pointerView_ = 0;                // the view pointer coordinates refer to
    std::optional<std::size_t> maximised_;       // one view filling the views window
    std::optional<std::size_t> panning_;         // an orthographic view being dragged about
    SDL_Window* window_ = nullptr;
    Config* config_ = nullptr;
    bool pathsOpen_ = false, keysOpen_ = false, resetLayout_ = false;
    bool showBrowse_ = true, showProperties_ = false, showEdit_ = true, showViewPanel_ = true, showProject_ = true;
    double mouseX_ = 0, mouseY_ = 0;
    bool looking_ = false;     // right mouse held: mouse turns the camera
    double speed_ = 600;       // block units per second
    char filter_[64] = {};
};

} // namespace de::app
