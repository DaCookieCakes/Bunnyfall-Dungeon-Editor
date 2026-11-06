// The frame around the views: menu bar, toolbar, status bar, the dock space and its default layout, and the small
// windows (paths, keys).

#include "app/setup.h"
#include "app/viewer.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>

namespace de::app {

namespace {

constexpr const char* kDockName = "editor-dock";

/// A toolbar toggle: highlighted while on.
bool toggle(const char* label, bool& value, const char* tip) {
    if (value) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_HeaderActive));
    const bool clicked = ImGui::Button(label);
    if (value) ImGui::PopStyleColor();
    if (clicked) value = !value;
    ImGui::SetItemTooltip("%s", tip);
    return clicked;
}

void separator() {
    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();
}

} // namespace

void Viewer::drawUi() {
    if (auto picked = takeProjectFile()) projectFilePicked(picked->first, std::move(picked->second));
    drawMenuBar();
    drawToolbar();

    // Status bar along the bottom.
    auto* viewport = ImGui::GetMainViewport();
    const ImGuiWindowFlags barFlags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_MenuBar;
    if (ImGui::BeginViewportSideBar("##status", viewport, ImGuiDir_Down, ImGui::GetFrameHeight(), barFlags)) {
        if (ImGui::BeginMenuBar()) {
            const auto& v = views_[pointerView_];
            if (v.kind != ViewKind::Perspective && v.size.x > 0) {
                const auto ray = rayAt(mouseX_, mouseY_);
                const auto f = cameraOf(pointerView_).forward();
                // The pointer on the plane through the view's centre.
                const double t = (v.centre.x - ray.origin.x) * f.x + (v.centre.y - ray.origin.y) * f.y +
                                 (v.centre.z - ray.origin.z) * f.z;
                ImGui::Text("%s  %.0f, %.0f, %.0f", v.name, ray.origin.x + f.x * t, ray.origin.y + f.y * t,
                            ray.origin.z + f.z * t);
            } else {
                const auto& p = camera_.position;
                ImGui::Text("3D  camera %.0f, %.0f, %.0f   speed %.0f", p.x, p.y, p.z, speed_);
            }
            separator();
            if (!selection_.empty()) {
                ImGui::Text("%zu selected", selection_.size());
                separator();
            }
            if (isolated_) {
                ImGui::TextColored(ImGui::GetStyleColorVec4(ImGuiCol_CheckMark), "isolated: %s", scene_.blocks[*isolated_].name.c_str());
                separator();
            }
            if (modelling()) {
                const auto* model = project_.model(modelEditor_->modelId);
                ImGui::TextColored(ImGui::GetStyleColorVec4(ImGuiCol_CheckMark), "building model %u %s (%s)",
                                   modelEditor_->modelId, model ? model->label.c_str() : "",
                                   modelEditor_->pick == ModelEditor::Pick::Points  ? "points"
                                   : modelEditor_->pick == ModelEditor::Pick::Edges ? "edges"
                                                                                    : "faces");
            } else if (edit_) {
                ImGui::Text("editing %s%s", edit_->name().c_str(), edit_->dirty() ? " *" : "");
            } else {
                ImGui::TextDisabled("viewing");
            }
            if (modelEditor_ && !modelling()) {
                separator();
                ImGui::TextDisabled("model %u open in its tab", modelEditor_->modelId);
            }
            if (placingAnything()) {
                separator();
                ImGui::TextColored(ImGui::GetStyleColorVec4(ImGuiCol_CheckMark), "placing: click a surface, right-click stops");
            } else if (grabbing_) {
                separator();
                ImGui::TextColored(ImGui::GetStyleColorVec4(ImGuiCol_CheckMark), "moving: click to put down, right-click cancels");
            }
            if (projectDirty_) {
                separator();
                ImGui::TextDisabled("project not saved");
            }
            ImGui::EndMenuBar();
        }
    }
    ImGui::End();

    const ImGuiID dock = ImHashStr(kDockName);
    if (resetLayout_ || !ImGui::DockBuilderGetNode(dock)) {
        buildDefaultLayout(dock);
        resetLayout_ = false;
    }
    ImGui::DockSpaceOverViewport(dock, viewport, ImGuiDockNodeFlags_PassthruCentralNode);

    drawViewsWindow();
    if (showBrowse_) drawBrowser();
    if (showModels_) drawModelPalette();
    if (showOutliner_) drawOutliner();
    if (showProperties_) drawInspector();
    if (showViewPanel_) drawViewPanel();
    if (modelEditor_) modelChanged();
    if (showEdit_ && !modelling()) drawEditPanel();
    if (modelling()) drawModelEditorPanel();
    if (showProject_) drawProjectPanel();
    drawConfirmations();
    if (typingUndoStep_ && typingFrame_ != ImGui::GetFrameCount()) endTyping();
    if (config_) drawPathsWindow(window_, *config_, &pathsOpen_);

    if (keysOpen_) {
        ImGui::SetNextWindowSize({520, 0}, ImGuiCond_Appearing);
        if (ImGui::Begin("Keys and mouse", &keysOpen_, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoDocking)) {
            if (ImGui::BeginTable("keys", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV)) {
                ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 170);
                const std::pair<const char*, const char*> rows[] = {
                    {"Right-drag (3D)", "look around; WASD moves, Shift faster"},
                    {"Wheel (3D)", "flying speed"},
                    {"Right/middle-drag (2D)", "pan"},
                    {"Middle-drag (3D)", "slide the camera"},
                    {"Wheel (2D)", "zoom about the pointer"},
                    {"Shift+Z", "maximise the view under the pointer / back to four"},
                    {"Home", "fit the 2D views to the dungeon"},
                    {"F", "frame the selection in every view"},
                    {"Left-click", "select (Shift adds, Ctrl toggles); with a palette entry armed, place"},
                    {"Double-click, Alt+Enter", "the object's Properties (General and Triggers tabs)"},
                    {"Right-click (no drag)", "a menu for what is under the pointer: properties, chain, quick setups"},
                    {"Left-drag", "a box selects (Shift adds, Ctrl toggles); on a selected object, moves the selection"},
                    {"Ctrl+A", "select everything shown (in the edited block, when editing)"},
                    {"Handles", "arrows move along X/Y/Z, the square freely; rings turn about X/Y/Z (R switches)"},
                    {"G", "move the selection with the pointer (2D views keep its height or depth)"},
                    {"[ and ]", "turn the model being placed (Shift: by the turn snap)"},
                    {"V", "vertex snap: placed and moved models meet open edges that match point for point"},
                    {"H / U / I", "hide the selection / show everything / isolate the selection's block"},
                    {"Arrows, PgUp/PgDn", "nudge (Shift: a big step)"},
                    {"Ctrl+D / Delete", "duplicate / delete"},
                    {"Ctrl+Z, Ctrl+Y", "undo, redo"},
                    {"Ctrl+S", "save the edited block"},
                    {"L", "trigger arrows: all / the selection's chains / none"},
                    {"Esc", "cancel a move, stop placing, deselect"},
                    {"Model editor", "File > New model, or a model's Edit button (Models > Custom)"},
                    {"  1 / 2 / 3 / Tab", "pick points / edges / faces"},
                    {"  Drag the selection", "move it (3D: Shift for up and down); corners snap to the grid or a point"},
                    {"  E", "extrude faces, edges or points: the copies follow the pointer, a click puts them down"},
                    {"  X / Shift+X", "push faces out (a wall into a corridor) / pull them in"},
                    {"  J", "two points: cut the face between them; three or more: fill a face; two edges: cut between"},
                    {"  N / Del / Ctrl+D", "flip faces / delete / copy"},
                    {"  Arrows, PgUp/PgDn", "move the selection a grid step"},
                    {"  [ and ]", "a smaller / bigger grid"},
                    {"  R", "handles: move / turn / scale (Shift while dragging: no snapping)"},
                    {"  G, then G", "move with the pointer (click puts it down); G again slides along the edges"},
                    {"  Right-click", "a menu for the face, edge or point under the pointer; elsewhere, the selection's"},
                };
                for (const auto& [k, what] : rows) {
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(k);
                    ImGui::TableNextColumn();
                    ImGui::TextWrapped("%s", what);
                }
                ImGui::EndTable();
            }
        }
        ImGui::End();
    }
}

void Viewer::drawMenuBar() {
    if (!ImGui::BeginMainMenuBar()) return;
    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("Save edited block", "Ctrl+S", false, edit_.has_value())) saveEdit();
        ImGui::Separator();
        const auto here = projectFile_.parent_path();
        if (ImGui::MenuItem("New project...")) askForProjectFile(window_, ProjectDialog::New, here);
        if (ImGui::MenuItem("Open project...")) askForProjectFile(window_, ProjectDialog::Open, here);
        if (ImGui::MenuItem("Save project", nullptr, false, projectDirty_)) saveProject();
        if (ImGui::MenuItem("Save project as...")) askForProjectFile(window_, ProjectDialog::SaveAs, here);
        if (ImGui::MenuItem("Export to Bunnyfall", nullptr, false, modDir_.has_value())) exportProject();
        ImGui::Separator();
        if (ImGui::MenuItem("New model")) newCustomModel();
        if (ImGui::MenuItem("Close the model editor", nullptr, false, modelEditor_.has_value())) closeModelEditor();
        ImGui::Separator();
        if (ImGui::MenuItem("Paths...")) pathsOpen_ = true;
        ImGui::Separator();
        if (ImGui::MenuItem("Quit")) requestQuit();
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Edit")) {
        if (modelling()) {   // the model's history, not the block's
            auto& history = modelEditor_->edit;
            if (ImGui::MenuItem("Undo", "Ctrl+Z", false, history.canUndo()) && history.undo()) modelChanged();
            if (ImGui::MenuItem("Redo", "Ctrl+Y", false, history.canRedo()) && history.redo()) modelChanged();
            ImGui::Separator();
            if (ImGui::MenuItem("Copy faces", "Ctrl+D", false, !modelEditor_->faces.empty())) modelDuplicate();
            if (ImGui::MenuItem("Delete", "Del")) modelDelete();
        } else {
            if (ImGui::MenuItem("Undo", "Ctrl+Z", false, edit_.has_value()) && edit_->undo()) editChanged();
            if (ImGui::MenuItem("Redo", "Ctrl+Y", false, edit_.has_value()) && edit_->redo()) editChanged();
            ImGui::Separator();
            if (ImGui::MenuItem("Duplicate", "Ctrl+D", false, edit_ && !selection_.empty())) duplicateSelected();
            if (ImGui::MenuItem("Delete", "Del", false, edit_ && !selection_.empty())) deleteSelected();
            ImGui::Separator();
            if (ImGui::MenuItem("Stop editing", nullptr, false, edit_.has_value())) endEdit();
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("View")) {
        ImGui::MenuItem("Browse", nullptr, &showBrowse_);
        ImGui::MenuItem("Models", nullptr, &showModels_);
        ImGui::MenuItem("Outliner", nullptr, &showOutliner_);
        ImGui::MenuItem("Properties", nullptr, &showProperties_);
        ImGui::MenuItem("Edit", nullptr, &showEdit_);
        ImGui::MenuItem("View options", nullptr, &showViewPanel_);
        ImGui::MenuItem("Project", nullptr, &showProject_);
        ImGui::Separator();
        if (ImGui::MenuItem("Four views", nullptr, !maximised_)) maximised_.reset();
        for (std::size_t i = 0; i < views_.size(); ++i) {
            const std::string label = std::string("Only ") + views_[i].name;
            if (ImGui::MenuItem(label.c_str(), nullptr, maximised_ == i)) maximised_ = i;
        }
        if (ImGui::MenuItem("Fit 2D views", "Home")) frameOrthoViews();
        if (ImGui::MenuItem("Frame the selection", "F", false, !selection_.empty())) frameSelection();
        ImGui::Separator();
        if (ImGui::MenuItem("Reset layout")) resetLayout_ = true;
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Help")) {
        if (ImGui::MenuItem("Keys and mouse")) keysOpen_ = true;
        ImGui::EndMenu();
    }
    ImGui::EndMainMenuBar();
}

void Viewer::drawToolbar() {
    auto* viewport = ImGui::GetMainViewport();
    const float height = ImGui::GetFrameHeight() + ImGui::GetStyle().WindowPadding.y * 2;
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings;
    if (ImGui::BeginViewportSideBar("##toolbar", viewport, ImGuiDir_Up, height, flags)) {
        bool selecting = !placingAnything();
        if (toggle("Select", selecting, "Click picks; Shift adds, Ctrl toggles, drag for a box (Esc)") && selecting) {
            stopPlacing();
        }
        ImGui::SameLine();
        bool moveMode = gizmoMode_ == GizmoMode::Move, turnMode = !moveMode;
        if (toggle("Move", moveMode, "Move handles: drag an arrow along its axis, the square freely (R)")) {
            gizmoMode_ = GizmoMode::Move;
        }
        ImGui::SameLine();
        if (toggle("Turn", turnMode, "Turn handles: rings about X (red), Y (green) and Z (blue) in 3D; in Top, Front and Side the one facing it (R)")) gizmoMode_ = GizmoMode::Turn;
        ImGui::SameLine();
        {
            const int turns[] = {1, 64, 128, 256, 512};
            const char* labels[] = {"free", "11.25", "22.5", "45", "90"};
            int index = 0;
            for (int i = 0; i < 5; ++i) {
                if (turns[i] == turnSnap_) index = i;
            }
            ImGui::SetNextItemWidth(70);
            if (ImGui::Combo("##turnsnap", &index, labels, 5)) turnSnap_ = turns[index];
            ImGui::SetItemTooltip("Turn snap, degrees");
        }
        ImGui::SameLine();
        toggle("Piece snap", pieceSnap_,
               "Architecture and doors snap to the classic grid: 64 units across, 128 up, quarter turns");
        ImGui::SameLine();
        toggle("Vertex snap", vertexSnap_,
               "A model being placed or moved goes onto a model already there where their open edges (a corridor's\n"
               "mouth, a cave tile's side) match point for point, nearest the pointer; the rim it meets is marked (V)");
        separator();
        toggle("Grid", drawOptions_.grid, "The sizing grid in the 3D view");
        ImGui::SameLine();
        toggle("Wire", drawOptions_.wireframe, "Wireframe in the 3D view");
        ImGui::SameLine();
        const bool filtered = std::ranges::find(shown_, false) != shown_.end() || !hidden_.empty() || isolated_;
        if (filtered) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_HeaderActive));
        if (ImGui::Button(filtered ? "Show (filtered)" : "Show")) ImGui::OpenPopup("show");
        if (filtered) ImGui::PopStyleColor();
        ImGui::SetItemTooltip("Show or hide kinds of things; H hides the selection, I isolates its block, U shows all");
        if (ImGui::BeginPopup("show")) {
            drawShowMenu();
            ImGui::EndPopup();
        }
        ImGui::SameLine();
        static constexpr const char* kLinkLabels[] = {"Links: off", "Links: selection", "Links: all"};
        if (ImGui::Button(kLinkLabels[int(linkMode_)])) cycleLinkMode();
        ImGui::SetItemTooltip("Trigger arrows in the views (L): all of them, only the chains the selection is part of,\n"
                              "or none");
        separator();
        bool four = !maximised_;
        if (toggle("4 views", four, "Four views, or the one under the pointer (Shift+Z)")) {
            maximised_ = four ? std::nullopt : std::optional(pointerView_);
        }
        ImGui::SameLine();
        if (ImGui::Button("Fit")) frameOrthoViews();
        ImGui::SetItemTooltip("Fit the 2D views to the dungeon (Home)");
        ImGui::SameLine();
        if (ImGui::Button("Start")) cameraToStart();
        ImGui::SetItemTooltip("3D camera to where a new game starts");
        separator();
        ImGui::BeginDisabled(!edit_ || !edit_->dirty());
        if (ImGui::Button("Save block")) saveEdit();
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(!projectDirty_);
        if (ImGui::Button("Save project")) saveProject();
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(!modDir_);
        if (ImGui::Button("Export")) exportProject();
        ImGui::EndDisabled();
        ImGui::SetItemTooltip("Write the project's dungeons into Bunnyfall's mod folder");
    }
    ImGui::End();
}

void Viewer::buildDefaultLayout(unsigned dockspace) {
    auto* viewport = ImGui::GetMainViewport();
    ImGui::DockBuilderRemoveNode(dockspace);
    ImGui::DockBuilderAddNode(dockspace, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockspace, viewport->WorkSize);
    ImGuiID centre = dockspace;
    ImGuiID left = ImGui::DockBuilderSplitNode(centre, ImGuiDir_Left, 0.2f, nullptr, &centre);
    const ImGuiID leftBottom = ImGui::DockBuilderSplitNode(left, ImGuiDir_Down, 0.5f, nullptr, &left);
    ImGuiID right = ImGui::DockBuilderSplitNode(centre, ImGuiDir_Right, 0.24f, nullptr, &centre);
    const ImGuiID bottom = ImGui::DockBuilderSplitNode(centre, ImGuiDir_Down, 0.3f, nullptr, &centre);
    const ImGuiID rightBottom = ImGui::DockBuilderSplitNode(right, ImGuiDir_Down, 0.5f, nullptr, &right);
    ImGui::DockBuilderDockWindow("Browse", left);
    ImGui::DockBuilderDockWindow("Models", leftBottom);
    ImGui::DockBuilderDockWindow("Outliner", right);
    ImGui::DockBuilderDockWindow("View", right);
    ImGui::DockBuilderDockWindow("Edit", rightBottom);
    ImGui::DockBuilderDockWindow("Model", rightBottom);
    ImGui::DockBuilderDockWindow("Project", bottom);
    ImGui::DockBuilderDockWindow("Viewports", centre);
    ImGui::DockBuilderDockWindow("Model editor", centre);
    ImGui::DockBuilderFinish(dockspace);
}

} // namespace de::app
