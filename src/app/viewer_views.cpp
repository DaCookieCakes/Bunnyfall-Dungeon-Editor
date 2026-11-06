// The four views (3D, Top, Front, Side) in the Viewports window: their cameras, mouse input, the 2D grid, and drawing
// each into its own picture.

#include "app/viewer.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace de::app {

namespace {

constexpr double kOrthoDistance = 30000;   // how far behind its centre an orthographic camera stands
constexpr double kMinHalfHeight = 40, kMaxHalfHeight = 40000;

ImVec2 add(ImVec2 a, ImVec2 b) { return {a.x + b.x, a.y + b.y}; }

double dot(catalog::Vec3 a, catalog::Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

/// The grid step for a zoom: the smallest power of two (from 8 units up) at least ~12 points apart on screen.
double gridStep(double unitsPerPoint) {
    double step = 8;
    while (step / unitsPerPoint < 12 && step < 2048) step *= 2;
    return step;
}

} // namespace

render::Camera Viewer::cameraOf(std::size_t index) const {
    const auto& v = views_[index];
    if (v.kind == ViewKind::Perspective) return camera_;
    render::Camera c;
    c.orthographic = true;
    c.orthoHalfHeight = v.halfHeight;
    c.nearPlane = 1;
    c.farPlane = 2 * kOrthoDistance;
    switch (v.kind) {
        case ViewKind::Top: c.pitch = -std::numbers::pi / 2; break;   // looking down, north up
        case ViewKind::Front: break;                                  // looking north
        case ViewKind::Side: c.yaw = std::numbers::pi / 2; break;     // looking east
        case ViewKind::Perspective: break;
    }
    const auto f = c.forward();
    c.position = {v.centre.x - f.x * kOrthoDistance, v.centre.y - f.y * kOrthoDistance, v.centre.z - f.z * kOrthoDistance};
    return c;
}

scene::Ray Viewer::rayAt(double x, double y) const {
    const auto& v = views_[pointerView_];
    const double w = v.size.x > 0 ? v.size.x : viewWidth_, h = v.size.y > 0 ? v.size.y : viewHeight_;
    return cameraOf(pointerView_).rayThrough(x, y, w, h);
}

void Viewer::frameOrthoViews() {
    if (modelling()) {
        frameModel();
        return;
    }
    if (scene_.blocks.empty()) return;
    catalog::Vec3 lo{1e300, 1e300, 1e300}, hi{-1e300, -1e300, -1e300};
    for (const auto& b : scene_.blocks) {
        lo = {std::min(lo.x, b.origin.x), std::min(lo.y, b.origin.y), std::min(lo.z, b.origin.z)};
        hi = {std::max(hi.x, b.origin.x + catalog::kRdbBlockSide), std::max(hi.y, b.origin.y),
              std::max(hi.z, b.origin.z + catalog::kRdbBlockSide)};
    }
    // Heights from what is actually there (blocks only say where they sit on the map).
    lo.y = 1e300;
    hi.y = -1e300;
    for (const auto& m : scene_.models) {
        lo.y = std::min(lo.y, m.transform.translation.y);
        hi.y = std::max(hi.y, m.transform.translation.y);
    }
    if (lo.y > hi.y) lo.y = hi.y = 0;
    lo.y -= 200;   // model origins: leave room for the pieces around them
    hi.y += 200;
    const catalog::Vec3 centre{(lo.x + hi.x) / 2, (lo.y + hi.y) / 2, (lo.z + hi.z) / 2};
    const double span = std::max({hi.x - lo.x, hi.z - lo.z, 2048.0});
    for (auto& v : views_) {
        if (v.kind == ViewKind::Perspective) continue;
        v.centre = centre;
        v.halfHeight = std::clamp(span * 0.4, kMinHalfHeight, kMaxHalfHeight);
    }
}

void Viewer::drawViewsWindow() {
    for (auto& v : views_) v.shown = false;
    // The tab shown decides the mode: the dungeon's (Viewports) or the model editor's. Read from last frame's
    // windows, except while a tab the program chose is coming to the front.
    if (focusTabFrames_ > 0) {
        --focusTabFrames_;
    } else if (modelEditor_) {
        if (modelTabShown_ && !dungeonTabShown_) setModelling(true);
        else if (dungeonTabShown_ && !modelTabShown_) setModelling(false);
    }
    const auto* viewports = ImGui::FindWindowByName("Viewports");
    if (focusTabFrames_ > 0 && !focusModelTab_) ImGui::SetNextWindowFocus();
    dungeonTabShown_ = drawViewsTab("Viewports", false, nullptr);
    if (modelEditor_) {
        if (viewports && viewports->DockId) ImGui::SetNextWindowDockID(viewports->DockId, ImGuiCond_FirstUseEver);
        if (focusTabFrames_ > 0 && focusModelTab_) ImGui::SetNextWindowFocus();
        bool open = true;
        modelTabShown_ = drawViewsTab("Model editor", true, &open);
        if (!open) closeModelEditor();
    } else {
        modelTabShown_ = false;
    }
}

bool Viewer::drawViewsTab(const char* name, bool model, bool* open) {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0, 0});
    const bool shown = ImGui::Begin(name, open, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar();
    if (shown) {
        if (model == modelling()) {
            drawViewGrid();
        } else {
            // Both tabs on screen at once (split apart): only one holds the views. A click here brings them over.
            ImGui::SetCursorPos({16, 16});
            ImGui::TextDisabled(model ? "The model editor: click to work on the model here."
                                      : "The dungeon: click to work on it here (the model waits in its tab).");
            const ImVec2 lo = ImGui::GetWindowPos(), hi{lo.x + ImGui::GetWindowWidth(), lo.y + ImGui::GetWindowHeight()};
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && ImGui::IsMouseHoveringRect(lo, hi) && ImGui::IsWindowHovered()) {
                setModelling(model);
            }
        }
    }
    ImGui::End();
    return shown;
}

void Viewer::drawViewGrid() {
    const ImVec2 origin = ImGui::GetCursorScreenPos(), avail = ImGui::GetContentRegionAvail();
    if (maximised_) {
        drawView(*maximised_, origin, avail);
    } else {
        constexpr float kGap = 2;
        const ImVec2 cell{std::floor((avail.x - kGap) / 2), std::floor((avail.y - kGap) / 2)};
        for (std::size_t i = 0; i < views_.size(); ++i) {
            const float col = float(i % 2), row = (i >= 2 ? 1.0f : 0.0f);
            drawView(i, add(origin, {col * (cell.x + kGap), row * (cell.y + kGap)}), cell);
        }
    }
    // The pointer, in the coordinates of the view it was last over (a grab keeps following it across views).
    if (!looking_) {
        const auto& v = views_[pointerView_];
        mouseX_ = ImGui::GetIO().MousePos.x - v.min.x;
        mouseY_ = ImGui::GetIO().MousePos.y - v.min.y;
    }
    if (modelling()) {
        continueModelDrag();
        // A right-click that neither looked nor panned: the model's menu for what is under the pointer.
        if (rightPress_ && ImGui::IsMouseReleased(ImGuiMouseButton_Right)) {
            const auto press = *rightPress_;
            rightPress_.reset();
            if (press.moved < 6 && ImGui::GetTime() - press.time < 0.5) {
                pointerView_ = press.view;
                auto& e = *modelEditor_;
                ModelEditor::Menu request;
                request.element = pickModelElement(press.view, press.at);
                request.screen = press.at;
                if (!request.element && e.selectedCount() > 0) {
                    // Away from everything, with something picked: the selection's menu all the same.
                    request.elsewhere = true;
                    if (e.pick == ModelEditor::Pick::Points) request.element = *e.points.begin();
                    else if (e.pick == ModelEditor::Pick::Faces) request.element = *e.faces.begin();
                    else if (const auto& list = e.edgesList(); true) {
                        const auto it = std::ranges::find(list, *e.edges.begin());
                        if (it != list.end()) request.element = static_cast<std::uint32_t>(it - list.begin());
                    }
                    if (!request.element) request.elsewhere = false;
                } else if (request.element) {
                    if (!e.isSelected(*request.element)) {   // like Hammer: it selects
                        e.clearPicked();
                        e.select(*request.element, false);
                    }
                    const auto& m = e.edit.mesh();
                    switch (e.pick) {
                        case ModelEditor::Pick::Points: request.at = m.points[*request.element]; break;
                        case ModelEditor::Pick::Edges: {
                            const auto edge = e.edgesList()[*request.element];
                            request.at = (m.points[edge.first] + m.points[edge.second]) * 0.5;
                            break;
                        }
                        case ModelEditor::Pick::Faces: request.at = modelFaceHit(press.view, press.at, *request.element); break;
                    }
                }
                e.menuRequest = request;
            }
        }
        auto& e = *modelEditor_;
        if (e.menuRequest) {
            e.menu = e.menuRequest;
            e.menuRequest.reset();
            ImGui::SetNextWindowPos(e.menu->screen);
            ImGui::OpenPopup("model-menu");
        }
        e.hoverOpening.reset();   // the menu sets it again while its pointer is on a cut
        if (ImGui::BeginPopup("model-menu")) {
            drawModelContextMenu();
            ImGui::EndPopup();
        }
    } else {
        if (grabbing_) grabTo(mouseX_, mouseY_);
        continueDrag();
        updateGhost();
    }

    // A right-click that neither looked nor panned: the menu for what is under the pointer.
    if (rightPress_ && ImGui::IsMouseReleased(ImGuiMouseButton_Right)) {
        const auto press = *rightPress_;
        rightPress_.reset();
        if (press.moved < 6 && ImGui::GetTime() - press.time < 0.5) {
            pointerView_ = press.view;
            const auto& pv = views_[press.view];
            ContextMenu menu{std::nullopt, press.at};
            if (const auto hit = pickAt(press.at.x - pv.min.x, press.at.y - pv.min.y, selectOptions())) {
                menu.ref = refOf(hit->kind, hit->index);
                if (!isSelected(*menu.ref)) setSelection({*menu.ref});   // like Hammer: right-click selects
            }
            contextRequest_ = menu;
        }
    }
    if (contextRequest_) {
        context_ = contextRequest_;
        contextRequest_.reset();
        ImGui::SetNextWindowPos(context_->at);
        ImGui::OpenPopup("view-menu");
    }
    if (ImGui::BeginPopup("view-menu")) {
        drawViewContextMenu();
        ImGui::EndPopup();
    }
}

void Viewer::drawView(std::size_t index, ImVec2 min, ImVec2 size) {
    auto& v = views_[index];
    auto* list = ImGui::GetWindowDrawList();
    const float header = ImGui::GetFrameHeight();
    if (size.x < 8 || size.y < header + 8) return;

    // The header strip: name, zoom, and the maximise toggle.
    list->AddRectFilled(min, add(min, {size.x, header}), ImGui::GetColorU32(ImGuiCol_TitleBg));
    ImGui::PushID(static_cast<int>(index));
    ImGui::SetCursorScreenPos(add(min, {6, (header - ImGui::GetTextLineHeight()) / 2}));
    if (pointerView_ == index) ImGui::TextColored(ImGui::GetStyleColorVec4(ImGuiCol_CheckMark), "%s", v.name);
    else ImGui::TextUnformatted(v.name);
    if (v.kind != ViewKind::Perspective) {
        const double upp = 2 * v.halfHeight / std::max(1.0f, size.y - header);
        ImGui::SameLine();
        ImGui::TextDisabled("grid %.0f", gridStep(upp));
    }
    const char* label = maximised_ ? "Restore" : "Maximise";
    const float bw = ImGui::CalcTextSize(label).x + ImGui::GetStyle().FramePadding.x * 2;
    ImGui::SetCursorScreenPos({min.x + size.x - bw - 2, min.y + (header - ImGui::GetFontSize() - 2) / 2});
    if (ImGui::SmallButton(label)) maximised_ = maximised_ ? std::nullopt : std::optional(index);
    ImGui::SetItemTooltip("Shift+Z over a view");

    // The picture, drawn by renderViews after the UI is laid out.
    v.min = add(min, {0, header});
    v.size = {size.x, size.y - header};
    v.shown = true;
    ImGui::SetCursorScreenPos(v.min);
    ImGui::InvisibleButton("view", v.size,
                           ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
    if (v.target.texture()) {
        list->AddImage(static_cast<ImTextureID>(v.target.texture()), v.min, add(v.min, v.size), {0, 1}, {1, 0});
    }
    if (v.kind != ViewKind::Perspective) drawOrthoGrid(index);
    if (modelling()) {
        const bool overHandle = drawModelOverlay(index);
        modelViewInput(index, overHandle);
        viewInput(index, overHandle);
    } else {
        drawLogicLinks(index);
        drawSnapRim(index);
        const bool overHandle = drawGizmo(index);
        viewInput(index, overHandle);
    }
    if (pointerView_ == index && !maximised_) {
        list->AddRect(v.min, add(v.min, v.size), ImGui::GetColorU32(ImGuiCol_CheckMark, 0.5f));
    }
    ImGui::PopID();
}

void Viewer::drawOrthoGrid(std::size_t index) {
    const auto& v = views_[index];
    const auto cam = cameraOf(index);
    const auto r = cam.right(), u = cam.up();
    const double upp = 2 * v.halfHeight / v.size.y;   // block units per point
    const double halfW = v.halfHeight * v.size.x / v.size.y;
    // Screen x of a world coordinate along the view's right axis, and y along its up axis.
    const double cr = dot(v.centre, r), cu = dot(v.centre, u);
    auto* list = ImGui::GetWindowDrawList();
    list->PushClipRect(v.min, add(v.min, v.size), true);
    const double step = gridStep(upp);
    const ImU32 fine = IM_COL32(255, 255, 255, 18), block = IM_COL32(110, 170, 255, 70), axis = IM_COL32(255, 120, 90, 90);
    // The right and up axes are always a world axis (or its negative), so the grid lines are world coordinates.
    auto lines = [&](double centre, double half, bool vertical) {
        const double first = std::floor((centre - half) / step) * step;
        for (double t = first; t <= centre + half; t += step) {
            const double world = std::abs(t) < 1e-6 ? 0 : t;
            const bool isBlock = std::fmod(std::abs(world), 2048.0) < 1e-6;
            const ImU32 colour = world == 0 ? axis : isBlock ? block : fine;
            if (vertical) {
                const float x = v.min.x + float((t - (centre - half)) / upp);
                list->AddLine({x, v.min.y}, {x, v.min.y + v.size.y}, colour);
            } else {
                const float y = v.min.y + float(((centre + half) - t) / upp);
                list->AddLine({v.min.x, y}, {v.min.x + v.size.x, y}, colour);
            }
        }
    };
    lines(cr, halfW, true);
    lines(cu, v.halfHeight, false);
    list->PopClipRect();
}

void Viewer::viewInput(std::size_t index, bool overHandle) {
    auto& v = views_[index];
    const auto& io = ImGui::GetIO();
    const bool hovered = ImGui::IsItemHovered();
    if (hovered && !looking_ && !panning_ && drag_.kind == Drag::Kind::None) pointerView_ = index;
    const double mx = io.MousePos.x - v.min.x, my = io.MousePos.y - v.min.y;
    const bool ortho = v.kind != ViewKind::Perspective;

    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !modelling()) {
        if (linkFrom_) finishLink(pickAt(mx, my, selectOptions()));
        else if (grabbing_) endGrab(true);
        else if (placingAnything()) placeAt(mx, my);
        else if (!overHandle && drag_.kind == Drag::Kind::None) {
            // Decided when the button comes up (a click selects) or the pointer moves (a box, or a move).
            drag_ = {};
            drag_.kind = Drag::Kind::Pending;
            drag_.view = index;
            drag_.press = io.MousePos;
            drag_.add = io.KeyShift;
            drag_.toggle = io.KeyCtrl;
        }
    }
    if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && !linkFrom_ && !grabbing_ && !placingAnything() &&
        !modelling()) {
        // Like Hammer: a double-click opens the object's properties (it becomes the one Properties shows).
        if (const auto hit = pickAt(mx, my, selectOptions())) {
            const auto ref = refOf(hit->kind, hit->index);
            auto refs = selection_;
            std::erase(refs, ref);
            refs.push_back(ref);
            setSelection(std::move(refs));
            selectedDistance_ = hit->distance;
            drag_ = {};
            openProperties(io.MousePos);
        }
    }
    const bool modelDragging = modelling() && modelEditor_->drag.kind != ModelEditor::Drag::Kind::None;
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right) && drag_.kind == Drag::Kind::None && !modelDragging) {
        if (modelling() && io.KeyAlt) modelWrapTextureAt(index, io.MousePos);
        else if (linkFrom_) linkFrom_.reset();
        else if (grabbing_) endGrab(false);
        else if (placingAnything()) stopPlacing();
        else if (!ortho) {
            rightPress_ = RightPress{index, io.MousePos, ImGui::GetTime(), 0};
            looking_ = true;
            if (window_) SDL_SetWindowRelativeMouseMode(window_, true);
        } else {
            rightPress_ = RightPress{index, io.MousePos, ImGui::GetTime(), 0};
            panning_ = index;
        }
    }
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Middle)) panning_ = index;

    if (panning_ == index) {
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Right) && !ImGui::IsMouseDown(ImGuiMouseButton_Middle)) {
            panning_.reset();
        } else if (io.MouseDelta.x != 0 || io.MouseDelta.y != 0) {
            if (rightPress_) rightPress_->moved += std::abs(io.MouseDelta.x) + std::abs(io.MouseDelta.y);
            const auto cam = cameraOf(index);
            const auto r = cam.right(), u = cam.up();
            // Orthographic: the picture follows the pointer exactly. 3D: the camera slides, faster at speed.
            const double k = ortho ? 2 * v.halfHeight / v.size.y : speed_ / 300;
            const double dx = -io.MouseDelta.x * k, dy = io.MouseDelta.y * k;
            auto& p = ortho ? v.centre : camera_.position;
            p = {p.x + r.x * dx + u.x * dy, p.y + r.y * dx + u.y * dy, p.z + r.z * dx + u.z * dy};
        }
    }

    if (hovered && io.MouseWheel != 0) {
        if (!ortho) {
            speed_ = std::clamp(speed_ * std::pow(1.2, io.MouseWheel), 50.0, 8000.0);
        } else {
            // Zoom about the point under the pointer, so it stays put.
            const auto cam = cameraOf(index);
            const auto r = cam.right(), u = cam.up();
            const double upp = 2 * v.halfHeight / v.size.y;
            const double ox = (mx - v.size.x / 2) * upp, oy = (v.size.y / 2 - my) * upp;   // pointer from centre
            const double next = std::clamp(v.halfHeight * std::pow(0.85, io.MouseWheel), kMinHalfHeight, kMaxHalfHeight);
            const double s = 1 - next / v.halfHeight;
            v.centre = {v.centre.x + (r.x * ox + u.x * oy) * s, v.centre.y + (r.y * ox + u.y * oy) * s,
                        v.centre.z + (r.z * ox + u.z * oy) * s};
            v.halfHeight = next;
        }
    }
}

void Viewer::renderViews(float pixelsPerPoint) {
    thumbnails_.renderPending(*renderer_);
    if (modelling()) {
        // The model alone, drawn as the dungeon is (2D views as wireframe).
        modelChanged();
        for (std::size_t i = 0; i < views_.size(); ++i) {
            auto& v = views_[i];
            if (!v.shown) continue;
            const int w = std::max(1, int(std::lround(v.size.x * pixelsPerPoint)));
            const int h = std::max(1, int(std::lround(v.size.y * pixelsPerPoint)));
            if (!v.target.bind(w, h)) continue;
            auto options = drawOptions_;
            if (v.kind != ViewKind::Perspective) {
                options.wireframe = true;
                options.wireColours = true;
                options.cullBackFaces = false;
                options.grid = false;
            }
            renderer_->draw(modelEditor_->drawn, cameraOf(i), w, h, options, {});
        }
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        return;
    }
    updateFlags();
    // The preview, drawn tinted as if selected, for this frame only.
    const auto realModels = scene_.models.size(), realFlats = scene_.flats.size();
    for (const auto& g : ghostModels_) {
        scene_.models.push_back(g);
        flags_.models.push_back(render::InstanceFlags::kSelected);
    }
    for (const auto& g : ghostFlats_) {
        scene_.flats.push_back(g);
        flags_.flats.push_back(render::InstanceFlags::kSelected);
    }
    poseForPreview(ImGui::GetIO().DeltaTime);
    for (std::size_t i = 0; i < views_.size(); ++i) {
        auto& v = views_[i];
        if (!v.shown) continue;
        const int w = std::max(1, int(std::lround(v.size.x * pixelsPerPoint)));
        const int h = std::max(1, int(std::lround(v.size.y * pixelsPerPoint)));
        if (!v.target.bind(w, h)) continue;
        auto options = drawOptions_;
        if (v.kind != ViewKind::Perspective) {
            // Like Hammer's 2D views: see through everything, and the 2D grid (drawn by the UI) instead of the 3D one.
            options.wireframe = true;
            options.wireColours = true;
            options.cullBackFaces = false;
            options.grid = false;
        }
        renderer_->draw(scene_, cameraOf(i), w, h, options, flags_);
    }
    scene_.models.resize(realModels);
    scene_.flats.resize(realFlats);
    endPreviewPose();
    flags_.models.resize(realModels);
    flags_.flats.resize(realFlats);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

} // namespace de::app
