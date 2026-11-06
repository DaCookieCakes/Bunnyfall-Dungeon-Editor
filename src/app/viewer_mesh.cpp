// The model editor: a custom model built from points and faces in the four views, with Daggerfall's textures laid on
// its faces. The views show the model alone; a left-click picks points or faces, a drag moves them (onto the grid, or
// onto another point near the pointer), handles move them along an axis. The rules live in src/mesh/; this is the
// part that knows about views and panels.

#include "app/viewer.h"

#include "mesh/mesh_json.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cmath>
#include <format>
#include <fstream>
#include <functional>
#include <numbers>
#include <set>

namespace de::app {

namespace {

using catalog::Vec3;

constexpr float kPickRadius = 8;      // screen points
constexpr float kSnapRadius = 10;     // a moved corner lands on a point this close to the pointer
constexpr float kHandleLength = 70;   // screen points
constexpr double kMinHalfHeight = 40;

const ImU32 kEdgeColour = IM_COL32(235, 235, 235, 60);
const ImU32 kPointColour = IM_COL32(235, 235, 235, 210);
const ImU32 kCentreColour = IM_COL32(235, 235, 235, 110);
const ImU32 kSelectedColour = IM_COL32(255, 170, 40, 255);
const ImU32 kSelectedFill = IM_COL32(255, 170, 40, 60);
const ImU32 kReferenceColour = IM_COL32(90, 200, 255, 120);
constexpr ImU32 kNormalColour = IM_COL32(90, 215, 235, 255);
constexpr std::array<ImU32, 3> kAxisColours = {IM_COL32(235, 80, 70, 255), IM_COL32(110, 210, 90, 255),
                                               IM_COL32(80, 140, 255, 255)};
constexpr std::array<Vec3, 3> kAxes = {{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}};
constexpr double kTurnSnap = 15;      // degrees a turn handle snaps to (Shift: free)
constexpr std::array<double, 9> kGridSteps = {1, 2, 4, 8, 16, 32, 64, 128, 256};

/// The dungeon base archives (a dungeon's texture table swaps them) and a few fixed sets classic pieces also use.
constexpr std::array<int, 6> kBaseArchives = {119, 120, 122, 123, 124, 168};

ImVec2 sub(ImVec2 a, ImVec2 b) { return {a.x - b.x, a.y - b.y}; }
float distance(ImVec2 a, ImVec2 b) { return std::hypot(a.x - b.x, a.y - b.y); }
float segmentDistance(ImVec2 p, ImVec2 a, ImVec2 b) {
    const ImVec2 ab = sub(b, a), ap = sub(p, a);
    const float len2 = ab.x * ab.x + ab.y * ab.y;
    const float t = len2 > 0 ? std::clamp((ap.x * ab.x + ap.y * ab.y) / len2, 0.0f, 1.0f) : 0.0f;
    return distance(p, {a.x + ab.x * t, a.y + ab.y * t});
}

/// The two directions across the plane a turn about `axis` happens in, `u` turning toward `v` for a positive turn
/// (catalog::axisRotation): X turns Y toward Z, Y turns Z toward X, Z turns X toward Y.
std::pair<Vec3, Vec3> ringBasis(int axis) {
    return axis == 0 ? std::pair{Vec3{0, 1, 0}, Vec3{0, 0, 1}}
           : axis == 1 ? std::pair{Vec3{0, 0, 1}, Vec3{1, 0, 0}}
                       : std::pair{Vec3{1, 0, 0}, Vec3{0, 1, 0}};
}

/// Where the ray meets a turn ring's plane, as an angle round `centre` (0 along u, growing toward v).
std::optional<double> angleOnRing(const scene::Ray& ray, Vec3 centre, int axis) {
    const auto [u, v] = ringBasis(axis);
    const Vec3 n = kAxes[static_cast<std::size_t>(axis)];
    const double denom = mesh::dot(ray.direction, n);
    if (std::abs(denom) < 1e-3) return std::nullopt;
    const Vec3 h = ray.origin + ray.direction * (mesh::dot(centre - ray.origin, n) / denom) - centre;
    const double a = mesh::dot(h, u), b = mesh::dot(h, v);
    if (a * a + b * b < 1e-6) return std::nullopt;
    return std::atan2(b, a);
}

double& component(Vec3& v, int axis) { return axis == 0 ? v.x : axis == 1 ? v.y : v.z; }

/// Edits a value of every selected face's alignment with one undo step per drag of the field.
template <typename Edit>
void alignField(mesh::MeshEdit& edit, const mesh::FaceSet& faces, bool changed, Edit apply) {
    if (ImGui::IsItemActivated()) edit.beginGesture();
    if (changed) {
        auto& m = edit.change();
        for (const auto f : faces) {
            if (f < m.faces.size()) apply(m.faces[f].uv);
        }
    }
    if (ImGui::IsItemDeactivated()) edit.endGesture();
}

} // namespace

// ---- the model and the project ----

void Viewer::registerCustomModels() {
    for (const auto& m : project_.models()) syncCustomModel(m.id);
}

void Viewer::syncCustomModel(std::uint32_t id) {
    const auto* m = project_.model(id);
    thumbnails_.forget(id);
    if (!m) {
        catalog_.removeModel(id);
        models_.forget(id);
        return;
    }
    catalog_.setModel(mesh::modelInfoOf(m->mesh, id, m->description));
    models_.setMesh(id, mesh::toModelMesh(m->mesh, id));
}

project::CustomModel* Viewer::editedModel() { return modelEditor_ ? project_.model(modelEditor_->modelId) : nullptr; }

void Viewer::newCustomModel() {
    mesh::Mesh start;
    mesh::addBox(start, {{-64, 0, -128}, {64, 128, 128}}, true, {true, true, true, true, true, true}, {});
    auto added = project_.addModel(std::format("Model {}", project_.modelsIssued() + 1), "C00", std::move(start));
    if (!added) {
        status_ = added.error().message;
        return;
    }
    const auto id = (*added)->id;
    markProjectChanged();
    syncCustomModel(id);
    openModelEditor(id);
    status_ = std::format("New model {}: a room one cell across and two high to start from.", id);
}

void Viewer::setModelling(bool on) {
    if (!modelEditor_ || modelEditor_->active == on) return;
    auto& e = *modelEditor_;
    if (on) {   // leave the dungeon's drags behind
        if (grabbing_) endGrab(false);
        if (!moving_.empty()) endTransform(false);
        stopPlacing();
        drag_ = {};
        linkFrom_.reset();
        preview_.reset();
    } else {
        if (e.edit.inGesture()) e.edit.cancelGesture();
        e.drag = {};
        modelChanged();
    }
    // Each tab keeps its own camera and framing.
    const auto camera = camera_;
    std::array<std::pair<Vec3, double>, 4> views{};
    for (std::size_t i = 0; i < views_.size(); ++i) views[i] = {views_[i].centre, views_[i].halfHeight};
    camera_ = e.savedCamera;
    for (std::size_t i = 0; i < views_.size(); ++i) {
        views_[i].centre = e.savedViews[i].first;
        views_[i].halfHeight = e.savedViews[i].second;
    }
    e.savedCamera = camera;
    e.savedViews = views;
    e.active = on;
    rightPress_.reset();
    focusModelTab_ = on;
    focusTabFrames_ = 3;
    if (!on) ++sceneStamp_;   // the model's size may have changed: link arrows measure from it
}

void Viewer::openModelEditor(std::uint32_t id) {
    const auto* model = project_.model(id);
    if (!model) return;
    if (modelEditor_ && modelEditor_->modelId == id) {
        setModelling(true);
        return;
    }
    if (modelEditor_) closeModelEditor();
    if (grabbing_) endGrab(false);
    if (!moving_.empty()) endTransform(false);
    stopPlacing();
    drag_ = {};
    linkFrom_.reset();
    preview_.reset();
    focusModelTab_ = true;
    focusTabFrames_ = 3;
    ModelEditor e(id, model->mesh);
    e.syncedRevision = e.edit.revision();
    e.savedCamera = camera_;
    for (std::size_t i = 0; i < views_.size(); ++i) e.savedViews[i] = {views_[i].centre, views_[i].halfHeight};
    e.drawn.title = model->label;
    e.drawn.models.push_back({id, {}, {}});
    modelEditor_.emplace(std::move(e));
    frameModel();
}

void Viewer::closeModelEditor() {
    if (!modelEditor_) return;
    modelChanged();
    if (modelEditor_->edit.inGesture()) modelEditor_->edit.endGesture();
    if (modelEditor_->active) {   // the dungeon's views back
        camera_ = modelEditor_->savedCamera;
        for (std::size_t i = 0; i < views_.size(); ++i) {
            views_[i].centre = modelEditor_->savedViews[i].first;
            views_[i].halfHeight = modelEditor_->savedViews[i].second;
        }
    }
    modelEditor_.reset();
    focusModelTab_ = false;
    focusTabFrames_ = 3;
    ++sceneStamp_;   // the model's size may have changed: link arrows measure from it
}

void Viewer::modelChanged() {
    if (!modelEditor_) return;
    auto& e = *modelEditor_;
    if (e.edit.revision() == e.syncedRevision) return;
    e.syncedRevision = e.edit.revision();
    const auto& m = e.edit.mesh();
    std::erase_if(e.points, [&](std::uint32_t p) { return p >= m.points.size(); });
    std::erase_if(e.faces, [&](std::uint32_t f) { return f >= m.faces.size(); });
    if (!e.edges.empty()) {   // only edges some face still has
        const auto all = mesh::edgesOf(m);
        std::erase_if(e.edges, [&](const mesh::Edge& edge) { return !all.contains(edge); });
    }
    if (auto* model = editedModel()) model->mesh = m;
    markProjectChanged();
    issuesStale_ = true;
    syncCustomModel(e.modelId);
}

// ---- selection ----

mesh::PointSet Viewer::modelMovingPoints() const {
    if (!modelEditor_) return {};
    const auto& e = *modelEditor_;
    switch (e.pick) {
        case ModelEditor::Pick::Points: return e.points;
        case ModelEditor::Pick::Edges: return mesh::pointsOf(e.edit.mesh(), e.edges);
        case ModelEditor::Pick::Faces: break;
    }
    return mesh::pointsOf(e.edit.mesh(), e.faces);
}

std::optional<Vec3> Viewer::modelSelectionCentre() const {
    const auto moving = modelMovingPoints();
    if (moving.empty()) return std::nullopt;
    const auto& m = modelEditor_->edit.mesh();
    Vec3 lo{1e300, 1e300, 1e300}, hi{-1e300, -1e300, -1e300};
    for (const auto p : moving) {
        const auto& q = m.points[p];
        lo = {std::min(lo.x, q.x), std::min(lo.y, q.y), std::min(lo.z, q.z)};
        hi = {std::max(hi.x, q.x), std::max(hi.y, q.y), std::max(hi.z, q.z)};
    }
    return Vec3{(lo.x + hi.x) / 2, (lo.y + hi.y) / 2, (lo.z + hi.z) / 2};
}

std::optional<std::uint32_t> Viewer::pickModelElement(std::size_t view, ImVec2 at) const {
    const auto& e = *modelEditor_;
    const auto& m = e.edit.mesh();
    const auto& v = views_[view];
    auto nearest = [&](auto count, auto position) -> std::optional<std::uint32_t> {
        std::optional<std::uint32_t> best;
        float bestDistance = kPickRadius;
        for (std::uint32_t i = 0; i < count; ++i) {
            const auto s = toScreen(view, position(i));
            if (!s) continue;
            const float d = distance(*s, at);
            if (d <= bestDistance) {
                bestDistance = d;
                best = i;
            }
        }
        return best;
    };
    if (e.pick == ModelEditor::Pick::Points) {
        return nearest(static_cast<std::uint32_t>(m.points.size()), [&](std::uint32_t i) { return m.points[i]; });
    }
    if (e.pick == ModelEditor::Pick::Edges) {
        // The edge passing nearest the pointer on screen.
        const auto& list = e.edgesList();
        std::optional<std::uint32_t> best;
        float bestDistance = kPickRadius;
        for (std::uint32_t i = 0; i < list.size(); ++i) {
            const auto a = toScreen(view, m.points[list[i].first]), b = toScreen(view, m.points[list[i].second]);
            if (!a || !b) continue;
            const float d = segmentDistance(at, *a, *b);
            if (d <= bestDistance) {
                bestDistance = d;
                best = i;
            }
        }
        return best;
    }
    const auto centre = [&](std::uint32_t i) { return m.centre(m.faces[i]); };
    const bool perspective = v.kind == ViewKind::Perspective;
    if (!perspective) {
        // Edge-on faces are lines in a 2D view: their middles are the handles.
        if (auto hit = nearest(static_cast<std::uint32_t>(m.faces.size()), centre)) return hit;
    }
    const auto ray = cameraOf(view).rayThrough(at.x - v.min.x, at.y - v.min.y, v.size.x, v.size.y);
    std::optional<std::uint32_t> best;
    double bestT = 1e300;
    for (std::uint32_t f = 0; f < m.faces.size(); ++f) {
        const auto& face = m.faces[f];
        for (const auto& t : mesh::triangulate(m, face)) {
            const auto hit = scene::intersectTriangle(ray, m.points[face.points[t[0]]], m.points[face.points[t[1]]],
                                                      m.points[face.points[t[2]]]);
            if (hit && *hit < bestT) {
                bestT = *hit;
                best = f;
            }
        }
    }
    if (best) return best;
    return perspective ? nearest(static_cast<std::uint32_t>(m.faces.size()), centre) : std::nullopt;
}

void Viewer::clickModelSelect(std::optional<std::uint32_t> hit, bool add, bool toggle) {
    auto& e = *modelEditor_;
    if (!add && !toggle) e.clearPicked();
    if (hit) e.select(*hit, toggle);
}

void Viewer::boxModelSelect(std::size_t view, ImVec2 a, ImVec2 b, bool add, bool toggle) {
    auto& e = *modelEditor_;
    const auto& m = e.edit.mesh();
    const ImVec2 lo{std::min(a.x, b.x), std::min(a.y, b.y)}, hi{std::max(a.x, b.x), std::max(a.y, b.y)};
    auto inside = [&](Vec3 p) {
        const auto s = toScreen(view, p);
        return s && s->x >= lo.x && s->x <= hi.x && s->y >= lo.y && s->y <= hi.y;
    };
    if (!add && !toggle) e.clearPicked();
    // Points by where they are, edges and faces by their middles.
    const auto& edges = e.edgesList();
    std::uint32_t count = 0;
    std::function<Vec3(std::uint32_t)> where;
    switch (e.pick) {
        case ModelEditor::Pick::Points:
            count = static_cast<std::uint32_t>(m.points.size());
            where = [&](std::uint32_t i) { return m.points[i]; };
            break;
        case ModelEditor::Pick::Edges:
            count = static_cast<std::uint32_t>(edges.size());
            where = [&](std::uint32_t i) { return (m.points[edges[i].first] + m.points[edges[i].second]) * 0.5; };
            break;
        case ModelEditor::Pick::Faces:
            count = static_cast<std::uint32_t>(m.faces.size());
            where = [&](std::uint32_t i) { return m.centre(m.faces[i]); };
            break;
    }
    for (std::uint32_t i = 0; i < count; ++i) {
        if (inside(where(i))) e.select(i, toggle);
    }
}

// ---- moving with the pointer ----

Vec3 Viewer::snapModelPoint(std::size_t view, Vec3 wanted, ImVec2 pointer, std::array<bool, 3> axes) const {
    const auto& e = *modelEditor_;
    if (e.snapPoints) {
        const auto& m = e.edit.mesh();
        const auto moving = modelMovingPoints();
        std::optional<Vec3> best;
        float bestDistance = kSnapRadius;
        auto consider = [&](Vec3 p) {
            const auto s = toScreen(view, p);
            if (!s) return;
            const float d = distance(*s, pointer);
            if (d <= bestDistance) {
                bestDistance = d;
                best = p;
            }
        };
        for (std::uint32_t i = 0; i < m.points.size(); ++i) {
            if (!moving.contains(i)) consider(m.points[i]);
        }
        if (e.reference) {
            if (const auto* info = catalog_.model(*e.reference)) {
                for (const auto& p : info->points) consider(p);
            }
        }
        if (best) {
            // Only along the axes that move: a 2D view never shifts the depth it cannot show.
            for (int a = 0; a < 3; ++a) {
                if (axes[static_cast<std::size_t>(a)]) component(wanted, a) = component(*best, a);
            }
            return wanted;
        }
    }
    for (int a = 0; a < 3; ++a) {
        if (axes[static_cast<std::size_t>(a)]) component(wanted, a) = mesh::snapTo(component(wanted, a), e.grid);
    }
    return wanted;
}

void Viewer::continueModelDrag() {
    if (!modelEditor_) return;
    auto& e = *modelEditor_;
    auto& d = e.drag;
    if (d.kind == ModelEditor::Drag::Kind::None) return;
    const auto& io = ImGui::GetIO();
    const auto& v = views_[d.view];

    // Right button or Esc (handleModelKey) cancels a move.
    if (d.moving() && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        cancelModelDrag();
        status_ = "Cancelled.";
        return;
    }
    // A drag ends when the button comes up; a move started by a key, on the next left click.
    const bool done = d.byKey ? ImGui::IsMouseClicked(ImGuiMouseButton_Left) : !ImGui::IsMouseDown(ImGuiMouseButton_Left);
    const bool free = io.KeyShift;   // Shift: no snapping (turns, scales and slides)
    // Putting it down: corners left on other points become one with them (when asked), in the same undo step.
    auto finish = [&] {
        // An extrusion put down where it started would leave faces of no size: it makes nothing.
        if (d.extruding) {
            const auto& m = e.edit.mesh();
            const bool still = std::ranges::all_of(d.start, [&](const auto& moved) {
                return moved.first >= m.points.size() || mesh::length(m.points[moved.first] - moved.second) < 1e-6;
            });
            if (still) {
                cancelModelDrag();
                status_ = "Extrude: not moved, so nothing made.";
                return;
            }
        }
        if (e.weldSnapped) {
            mesh::PointSet moved;
            for (const auto& [p, _] : d.start) moved.insert(p);
            const auto& m = e.edit.mesh();
            const bool touching = std::ranges::any_of(moved, [&](std::uint32_t p) {
                for (std::uint32_t q = 0; q < m.points.size(); ++q) {
                    if (q != p && !moved.contains(q) && mesh::length(m.points[p] - m.points[q]) < 1e-3) return true;
                }
                return false;
            });
            if (touching) {
                const auto n = mesh::mergeOnto(e.edit.change(), moved, 1e-3);
                e.points.clear();
                e.edges.clear();
                e.faces.clear();
                status_ = std::format("Welded {} point{} onto the point{} {} landed on.", n, n == 1 ? "" : "s", n == 1 ? "" : "s",
                                      n == 1 ? "it" : "they");
            }
        }
        e.edit.endGesture();
        d = {};
    };
    auto applyOffset = [&](Vec3 offset) {
        auto& m = e.edit.change();
        for (const auto& [p, start] : d.start) {
            if (p < m.points.size()) m.points[p] = start + offset;
        }
    };

    switch (d.kind) {
        case ModelEditor::Drag::Kind::Pending:
            if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {   // a click
                clickModelSelect(pickModelElement(d.view, d.press), d.add, d.toggle);
                d = {};
                return;
            }
            if (distance(io.MousePos, d.press) < 4) return;
            if (!d.onSelection) {
                d.kind = ModelEditor::Drag::Kind::Box;
                return;
            }
            {
                const auto press = d.press;
                const auto view = d.view;
                if (!startModelGrab(view, press, false)) return;
                if (d.kind == ModelEditor::Drag::Kind::Normal) return;   // read from the next frame on
            }
            [[fallthrough]];
        case ModelEditor::Drag::Kind::Plane: {
            const auto cam = cameraOf(d.view);
            const auto ray = cam.rayThrough(io.MousePos.x - v.min.x, io.MousePos.y - v.min.y, v.size.x, v.size.y);
            const double denom = mesh::dot(ray.direction, d.planeNormal);
            if (std::abs(denom) > 1e-6) {
                const double t = mesh::dot(d.grabHit - ray.origin, d.planeNormal) / denom;
                const Vec3 hit = ray.origin + ray.direction * t;
                std::array<bool, 3> axes{true, true, true};
                switch (v.kind) {
                    case ViewKind::Top: axes = {true, false, true}; break;
                    case ViewKind::Front: axes = {true, true, false}; break;
                    case ViewKind::Side: axes = {false, true, true}; break;
                    case ViewKind::Perspective: axes = d.vertical ? std::array{false, true, false} : std::array{true, false, true}; break;
                }
                Vec3 wanted = d.anchor + (hit - d.grabHit);
                for (int a = 0; a < 3; ++a) {
                    if (!axes[static_cast<std::size_t>(a)]) component(wanted, a) = component(d.anchor, a);
                }
                const Vec3 target = snapModelPoint(d.view, wanted, io.MousePos, axes);
                applyOffset(target - d.anchor);
            }
            if (done) finish();
            break;
        }
        case ModelEditor::Drag::Kind::Axis: {
            // Along one axis: the pointer's travel along the handle's direction on screen, snapped as a whole step so
            // corners on the grid stay on it.
            const auto a = toScreen(d.view, d.anchor);
            const Vec3 tip = d.anchor + kAxes[static_cast<std::size_t>(d.axis)] * 100.0;
            const auto b = toScreen(d.view, tip);
            if (a && b) {
                const ImVec2 s = sub(*b, *a);
                const float len = std::hypot(s.x, s.y);
                if (len > 1) {
                    const ImVec2 moved = sub(io.MousePos, d.press);
                    const double along = (moved.x * s.x + moved.y * s.y) / len * (100.0 / len);
                    Vec3 offset;
                    component(offset, d.axis) = mesh::snapTo(along, e.grid);
                    applyOffset(offset);
                }
            }
            if (done) finish();
            break;
        }
        case ModelEditor::Drag::Kind::Normal: {
            // The pointer's travel along the lead's direction on screen, as for an axis. Each point goes its own way by it,
            // and the distance is what snaps: corners of a sloping face have no grid to land on.
            const auto a = toScreen(d.view, d.anchor);
            const auto b = toScreen(d.view, d.anchor + d.normalLead * 100.0);
            if (a && b) {
                const ImVec2 s = sub(*b, *a);
                const float len = std::hypot(s.x, s.y);
                if (len > 1) {
                    const ImVec2 moved = sub(io.MousePos, d.press);
                    const double along = (moved.x * s.x + moved.y * s.y) / len * (100.0 / len);
                    const double by = free ? along : mesh::snapTo(along, e.grid);
                    auto& m = e.edit.change();
                    for (const auto& [p, start] : d.start) {
                        if (p < m.points.size()) m.points[p] = start + d.directions.at(p) * by;
                    }
                    status_ = std::format("{:g} units along the normals (Shift: no snap)", by);
                }
            }
            if (done) finish();
            break;
        }
        case ModelEditor::Drag::Kind::Turn: {
            const auto ray = cameraOf(d.view).rayThrough(io.MousePos.x - v.min.x, io.MousePos.y - v.min.y, v.size.x, v.size.y);
            if (const auto angle = angleOnRing(ray, d.centre, d.axis)) {
                double step = *angle - d.lastAngle;   // unwrapped, so turning past half a turn keeps going
                while (step > std::numbers::pi) step -= 2 * std::numbers::pi;
                while (step < -std::numbers::pi) step += 2 * std::numbers::pi;
                d.turned += step;
                d.lastAngle = *angle;
            }
            double degrees = d.turned * 180 / std::numbers::pi;
            if (!free) degrees = mesh::snapTo(degrees, kTurnSnap);
            const auto r = catalog::axisRotation(d.axis, degrees);
            auto& m = e.edit.change();
            for (const auto& [p, start] : d.start) m.points[p] = d.centre + r * (start - d.centre);
            status_ = std::format("Turning {:.1f} degrees about {} (Shift: no snap)", degrees, "XYZ"[d.axis]);
            if (done) finish();
            break;
        }
        case ModelEditor::Drag::Kind::Scale: {
            // Along the handle: how far the pointer is out along it now against at the press. All three at once:
            // right grows, left shrinks.
            double factor = 1;
            const auto c = toScreen(d.view, d.centre);
            if (d.axis == 3) {
                factor = std::max(0.01, 1 + double(io.MousePos.x - d.press.x) / kHandleLength);
            } else if (c) {
                const auto tip = toScreen(d.view, d.centre + kAxes[static_cast<std::size_t>(d.axis)] * 100.0);
                if (tip) {
                    const ImVec2 s = sub(*tip, *c);
                    const double at = (d.press.x - c->x) * s.x + (d.press.y - c->y) * s.y;
                    const double now = (io.MousePos.x - c->x) * s.x + (io.MousePos.y - c->y) * s.y;
                    if (std::abs(at) > 1e-6) factor = std::max(0.01, now / at);
                }
            }
            // Snapped so the selection's size lands on the grid.
            double size = d.extent * factor;
            if (!free) size = std::max(e.grid, mesh::snapTo(size, e.grid));
            factor = size / d.extent;
            const Vec3 f{d.axis == 0 || d.axis == 3 ? factor : 1, d.axis == 1 || d.axis == 3 ? factor : 1,
                         d.axis == 2 || d.axis == 3 ? factor : 1};
            auto& m = e.edit.change();
            for (const auto& [p, start] : d.start) {
                const Vec3 o = start - d.centre;
                m.points[p] = d.centre + Vec3{o.x * f.x, o.y * f.y, o.z * f.z};
            }
            status_ = std::format("Scaling {} by {:.3f}: {:g} units (Shift: no snap)", d.axis == 3 ? "all" : std::string(1, "XYZ"[d.axis]),
                                  factor, size);
            if (done) finish();
            break;
        }
        case ModelEditor::Drag::Kind::Slide: {
            // The pointer leads the anchor along whichever of its edges points most its way; every other point takes
            // its own edge that runs most like that one, the same share of the way along.
            auto& m = e.edit.change();
            for (const auto& [p, start] : d.start) m.points[p] = start;
            const ImVec2 moved = sub(io.MousePos, d.press);
            const float movedLength = std::hypot(moved.x, moved.y);
            const auto anchorScreen = toScreen(d.view, d.start[d.anchorPoint]);
            const auto choices = d.neighbours.find(d.anchorPoint);
            if (movedLength > 2 && anchorScreen && choices != d.neighbours.end()) {
                std::optional<std::uint32_t> best;
                double bestDot = -2;
                ImVec2 bestScreen{};
                for (const auto q : choices->second) {
                    const auto qs = toScreen(d.view, m.points[q]);
                    if (!qs) continue;
                    const ImVec2 s = sub(*qs, *anchorScreen);
                    const float len = std::hypot(s.x, s.y);
                    if (len < 1) continue;
                    const double dd = (s.x * moved.x + s.y * moved.y) / (len * movedLength);
                    if (dd > bestDot) {
                        bestDot = dd;
                        best = q;
                        bestScreen = s;
                    }
                }
                if (best) {
                    const Vec3 edge = m.points[*best] - d.start[d.anchorPoint];
                    const double edgeLength = mesh::length(edge);
                    double t = std::clamp((moved.x * bestScreen.x + moved.y * bestScreen.y) /
                                              double(bestScreen.x * bestScreen.x + bestScreen.y * bestScreen.y),
                                          0.0, 1.0);
                    if (!free && edgeLength > 1e-6) t = std::clamp(mesh::snapTo(t * edgeLength, e.grid), 0.0, edgeLength) / edgeLength;
                    mesh::slidePoints(m, d.start, d.neighbours, edge, t);
                    auto* list = ImGui::GetForegroundDrawList();
                    if (const auto q = toScreen(d.view, m.points[*best])) list->AddLine(*anchorScreen, *q, IM_COL32(255, 230, 90, 200), 2);
                    status_ = std::format("Sliding {:.0f}% along ({:g} of {:.0f} units; Shift: no snap, G again: move freely)",
                                          t * 100, t * edgeLength, edgeLength);
                }
            }
            if (done) finish();
            break;
        }
        case ModelEditor::Drag::Kind::Box: {
            // ImGui draws a rectangle only from its top-left corner: a box dragged up or left is put in order first.
            const ImVec2 lo{std::min(d.press.x, io.MousePos.x), std::min(d.press.y, io.MousePos.y)};
            const ImVec2 hi{std::max(d.press.x, io.MousePos.x), std::max(d.press.y, io.MousePos.y)};
            auto* list = ImGui::GetForegroundDrawList();
            list->PushClipRect(v.min, {v.min.x + v.size.x, v.min.y + v.size.y}, true);
            list->AddRectFilled(lo, hi, IM_COL32(255, 170, 40, 30));
            list->AddRect(lo, hi, kSelectedColour);
            list->PopClipRect();
            if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                boxModelSelect(d.view, d.press, io.MousePos, d.add, d.toggle);
                d = {};
            }
            break;
        }
        case ModelEditor::Drag::Kind::None: break;
    }
    modelChanged();
}

void Viewer::modelViewInput(std::size_t view, bool overHandle) {
    auto& e = *modelEditor_;
    const auto& io = ImGui::GetIO();
    if (!ImGui::IsItemHovered() || !ImGui::IsMouseClicked(ImGuiMouseButton_Left) || overHandle ||
        e.drag.kind != ModelEditor::Drag::Kind::None) {
        return;
    }
    e.drag = {};
    e.drag.kind = ModelEditor::Drag::Kind::Pending;
    e.drag.view = view;
    e.drag.press = io.MousePos;
    e.drag.add = io.KeyShift;
    e.drag.toggle = io.KeyCtrl;
    const auto hit = pickModelElement(view, io.MousePos);
    e.drag.onSelection = hit && e.isSelected(*hit) && !io.KeyCtrl;
}

// ---- drawing in the views ----

bool Viewer::drawModelOverlay(std::size_t view) {
    auto& e = *modelEditor_;
    const auto& m = e.edit.mesh();
    const auto& v = views_[view];
    auto* list = ImGui::GetWindowDrawList();
    list->PushClipRect(v.min, {v.min.x + v.size.x, v.min.y + v.size.y}, true);
    const bool perspective = v.kind == ViewKind::Perspective;

    std::vector<std::optional<ImVec2>> screen(m.points.size());
    for (std::size_t i = 0; i < m.points.size(); ++i) screen[i] = toScreen(view, m.points[i]);
    auto line = [&](const std::optional<ImVec2>& a, const std::optional<ImVec2>& b, ImU32 colour, float width = 1) {
        if (a && b) list->AddLine(*a, *b, colour, width);
    };

    // The reference model, to trace and snap to.
    if (e.reference) {
        if (const auto* info = catalog_.model(*e.reference)) {
            std::vector<std::optional<ImVec2>> ref(info->points.size());
            for (std::size_t i = 0; i < info->points.size(); ++i) ref[i] = toScreen(view, info->points[i]);
            for (const auto& f : info->faces) {
                for (std::size_t i = 0; i < f.size(); ++i) line(ref[f[i]], ref[f[(i + 1) % f.size()]], kReferenceColour);
            }
        }
    }
    // The origin: where the model's position in a block is.
    const auto origin = toScreen(view, {0, 0, 0});
    for (std::size_t a = 0; a < 3; ++a) line(origin, toScreen(view, kAxes[a] * 24.0), kAxisColours[a] & 0x90FFFFFF, 2);

    // Edges (the 2D views already show the wireframe), the selected faces filled, with a stroke for which way each
    // faces.
    if (perspective) {
        for (const auto& f : m.faces) {
            for (std::size_t i = 0; i < f.points.size(); ++i) line(screen[f.points[i]], screen[f.points[(i + 1) % f.points.size()]], kEdgeColour);
        }
    }
    for (const auto fi : e.faces) {
        if (fi >= m.faces.size()) continue;
        const auto& f = m.faces[fi];
        for (const auto& t : mesh::triangulate(m, f)) {
            const auto& a = screen[f.points[t[0]]];
            const auto& b = screen[f.points[t[1]]];
            const auto& c = screen[f.points[t[2]]];
            if (a && b && c) list->AddTriangleFilled(*a, *b, *c, kSelectedFill);
        }
        for (std::size_t i = 0; i < f.points.size(); ++i) line(screen[f.points[i]], screen[f.points[(i + 1) % f.points.size()]], kSelectedColour, 2);
        const Vec3 c = m.centre(f);
        const double reach = 18 * unitsPerPoint(view, c);
        line(toScreen(view, c), toScreen(view, c + m.normal(f) * reach), kSelectedColour, 2);
    }
    // The opening the Cuts section would make, on each selected face.
    if ((e.showCuts || e.hoverOpening) && e.pick == ModelEditor::Pick::Faces) {
        const auto& shownOpening = e.hoverOpening ? *e.hoverOpening : e.opening;
        for (const auto fi : e.faces) {
            const auto corners = mesh::openingCorners(m, fi, shownOpening);
            if (!corners) continue;
            for (std::size_t i = 0; i < 4; ++i) {
                line(toScreen(view, (*corners)[i]), toScreen(view, (*corners)[(i + 1) % 4]), IM_COL32(80, 230, 255, 230), 2);
            }
        }
    }
    // Points, or the faces' middles (what a click on an edge-on face in a 2D view picks).
    if (e.pick == ModelEditor::Pick::Edges) {
        // Every edge (the 3D view has them already), the selected ones bright and thick, each with a dot at its
        // middle (what a box takes).
        for (const auto& edge : e.edgesList()) {
            const bool selected = e.edges.contains(edge);
            const auto& a = screen[edge.first];
            const auto& b = screen[edge.second];
            if (!a || !b) continue;
            if (selected) list->AddLine(*a, *b, kSelectedColour, 3);
            else if (!perspective) list->AddLine(*a, *b, kEdgeColour);
            list->AddCircleFilled({(a->x + b->x) / 2, (a->y + b->y) / 2}, selected ? 3.5f : 2.0f,
                                  selected ? kSelectedColour : kCentreColour);
        }
    } else if (e.pick == ModelEditor::Pick::Points) {
        for (std::uint32_t i = 0; i < m.points.size(); ++i) {
            if (!screen[i]) continue;
            const bool selected = e.points.contains(i);
            const float r = selected ? 4.0f : 2.5f;
            list->AddRectFilled({screen[i]->x - r, screen[i]->y - r}, {screen[i]->x + r, screen[i]->y + r},
                                selected ? kSelectedColour : kPointColour);
        }
    } else {
        for (std::uint32_t f = 0; f < m.faces.size(); ++f) {
            if (e.faces.contains(f)) continue;
            if (const auto c = toScreen(view, m.centre(m.faces[f]))) list->AddCircleFilled(*c, 2.5f, kCentreColour);
        }
    }

    // Handles on the selection's middle, by tool: arrows to move along an axis, rings to turn about one, and
    // square-ended arms to scale along one (the middle square scales all three).
    bool overHandle = false;
    const auto centre = modelSelectionCentre();
    const bool idle = e.drag.kind == ModelEditor::Drag::Kind::None && ImGui::IsItemHovered();
    const bool handlesShown = centre && (e.drag.kind == ModelEditor::Drag::Kind::None || e.drag.view == view) &&
                              e.drag.kind != ModelEditor::Drag::Kind::Box && e.drag.kind != ModelEditor::Drag::Kind::Slide &&
                              !((e.drag.kind == ModelEditor::Drag::Kind::Plane || e.drag.kind == ModelEditor::Drag::Kind::Normal) && e.drag.byKey);
    const auto c = handlesShown ? toScreen(view, *centre) : std::nullopt;
    if (c) {
        const double reach = kHandleLength * unitsPerPoint(view, *centre);
        const auto& io = ImGui::GetIO();
        const ImU32 hotColour = IM_COL32(255, 255, 255, 255);
        std::optional<int> hovered;
        auto startDrag = [&](ModelEditor::Drag::Kind kind, int axis) {
            e.drag = {};
            e.drag.kind = kind;
            e.drag.view = view;
            e.drag.press = io.MousePos;
            e.drag.axis = axis;
            e.drag.anchor = *centre;
            e.drag.centre = *centre;
            for (const auto p : modelMovingPoints()) e.drag.start[p] = m.points[p];
            e.edit.beginGesture();
        };
        if (e.tool == ModelEditor::Tool::Turn) {
            // Rings: all three in 3D; in each 2D view the one facing it (Top turns about Y, Front Z, Side X).
            std::vector<int> axes;
            switch (v.kind) {
                case ViewKind::Perspective: axes = {0, 1, 2}; break;
                case ViewKind::Top: axes = {1}; break;
                case ViewKind::Front: axes = {2}; break;
                case ViewKind::Side: axes = {0}; break;
            }
            constexpr int kSegments = 48;
            float hotDistance = 6;
            std::array<std::vector<ImVec2>, 3> rings;
            for (const int a : axes) {
                const auto [u, w] = ringBasis(a);
                for (int i = 0; i <= kSegments; ++i) {
                    const double t = i * 2 * std::numbers::pi / kSegments;
                    if (const auto p = toScreen(view, *centre + u * (std::cos(t) * reach) + w * (std::sin(t) * reach))) {
                        rings[static_cast<std::size_t>(a)].push_back(*p);
                    }
                }
                const auto& ring = rings[static_cast<std::size_t>(a)];
                for (std::size_t i = 1; i < ring.size() && idle; ++i) {
                    const float dd = segmentDistance(io.MousePos, ring[i - 1], ring[i]);
                    if (dd < hotDistance) {
                        hotDistance = dd;
                        hovered = a;
                    }
                }
            }
            for (const int a : axes) {
                const auto& ring = rings[static_cast<std::size_t>(a)];
                const bool hot = hovered == a || (e.drag.kind == ModelEditor::Drag::Kind::Turn && e.drag.axis == a);
                if (ring.size() > 1) {
                    list->AddPolyline(ring.data(), int(ring.size()), hot ? hotColour : kAxisColours[static_cast<std::size_t>(a)],
                                      0, hot ? 3.5f : 2.5f);
                }
            }
            list->AddCircleFilled(*c, 3, kPointColour);
            if (hovered) {
                overHandle = true;
                if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                    const auto ray = cameraOf(view).rayThrough(io.MousePos.x - v.min.x, io.MousePos.y - v.min.y, v.size.x, v.size.y);
                    if (const auto angle = angleOnRing(ray, *centre, *hovered)) {
                        startDrag(ModelEditor::Drag::Kind::Turn, *hovered);
                        e.drag.lastAngle = *angle;
                    }
                }
            }
        } else if (e.tool == ModelEditor::Tool::Move && e.alongNormals) {
            // One arrow, along the selection's normals taken together.
            auto directions = modelSelectionDirections();
            Vec3 sum;
            for (const auto& [p, way] : directions) sum = sum + way;
            const Vec3 way = mesh::length(sum) > 1e-6 ? mesh::normalised(sum)
                             : directions.empty() ? Vec3{} : mesh::normalised(directions.begin()->second);
            const auto tip = directions.empty() ? std::nullopt : toScreen(view, *centre + way * reach);
            if (tip && distance(*tip, *c) >= 12) {
                const bool hot = e.drag.kind == ModelEditor::Drag::Kind::Normal || (idle && segmentDistance(io.MousePos, *c, *tip) < 6);
                const auto colour = hot ? hotColour : kNormalColour;
                list->AddLine(*c, *tip, colour, hot ? 3.5f : 2.5f);
                list->AddCircleFilled(*tip, hot ? 6.0f : 5.0f, colour);
                if (hot && idle) {
                    overHandle = true;
                    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
                        startModelNormalMove(view, io.MousePos, false, std::move(directions))) {
                        e.drag.anchor = *centre;
                        e.drag.normalLead = way;
                    }
                }
            }
        } else {
            const bool scaling = e.tool == ModelEditor::Tool::Scale;
            const auto activeKind = scaling ? ModelEditor::Drag::Kind::Scale : ModelEditor::Drag::Kind::Axis;
            for (int a = 0; a < 3; ++a) {
                const auto tip = toScreen(view, *centre + kAxes[static_cast<std::size_t>(a)] * reach);
                if (!tip || distance(*tip, *c) < 12) continue;   // pointing at the viewer: no handle
                const bool hot = (e.drag.kind == activeKind && e.drag.axis == a) ||
                                 (idle && !hovered && segmentDistance(io.MousePos, *c, *tip) < 6);
                if (hot && idle) hovered = a;
                const auto colour = hot ? hotColour : kAxisColours[static_cast<std::size_t>(a)];
                list->AddLine(*c, *tip, colour, hot ? 3.5f : 2.5f);
                if (scaling) list->AddRectFilled({tip->x - 5, tip->y - 5}, {tip->x + 5, tip->y + 5}, colour);
                else list->AddCircleFilled(*tip, hot ? 6.0f : 5.0f, colour);
            }
            if (scaling) {
                const bool hot = (e.drag.kind == activeKind && e.drag.axis == 3) ||
                                 (idle && std::abs(io.MousePos.x - c->x) <= 7 && std::abs(io.MousePos.y - c->y) <= 7);
                if (hot && idle) hovered = 3;
                list->AddRectFilled({c->x - 6, c->y - 6}, {c->x + 6, c->y + 6}, hot ? hotColour : kPointColour);
            }
            if (hovered) {
                overHandle = true;
                if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                    startDrag(activeKind, *hovered);
                    if (scaling) {
                        // The size it scales: along its axis, or for all three the largest.
                        Vec3 lo{1e300, 1e300, 1e300}, hi{-1e300, -1e300, -1e300};
                        for (const auto& [p, q] : e.drag.start) {
                            lo = {std::min(lo.x, q.x), std::min(lo.y, q.y), std::min(lo.z, q.z)};
                            hi = {std::max(hi.x, q.x), std::max(hi.y, q.y), std::max(hi.z, q.z)};
                        }
                        Vec3 size = hi - lo;
                        e.drag.extent = *hovered == 3 ? std::max({size.x, size.y, size.z}) : component(size, *hovered);
                        if (e.drag.extent < 1e-6) {
                            e.edit.cancelGesture();
                            e.drag = {};
                            status_ = "The selection has no size along that axis to scale.";
                        }
                    }
                }
            }
        }
    }
    list->PopClipRect();
    return overHandle;
}

void Viewer::frameModel() {
    if (!modelEditor_) return;
    const auto& e = *modelEditor_;
    const auto& m = e.edit.mesh();
    catalog::Box box{{-128, 0, -128}, {128, 256, 128}};
    const auto moving = modelMovingPoints();
    if (!moving.empty()) {
        box = {m.points[*moving.begin()], m.points[*moving.begin()]};
        for (const auto p : moving) {
            box.min = {std::min(box.min.x, m.points[p].x), std::min(box.min.y, m.points[p].y), std::min(box.min.z, m.points[p].z)};
            box.max = {std::max(box.max.x, m.points[p].x), std::max(box.max.y, m.points[p].y), std::max(box.max.z, m.points[p].z)};
        }
    } else if (!m.points.empty()) {
        box = m.bounds();
    }
    const Vec3 c{(box.min.x + box.max.x) / 2, (box.min.y + box.max.y) / 2, (box.min.z + box.max.z) / 2};
    const auto s = box.size();
    const double span = std::max({s.x, s.y, s.z, 64.0});
    for (auto& v : views_) {
        if (v.kind == ViewKind::Perspective) continue;
        v.centre = c;
        v.halfHeight = std::max(kMinHalfHeight, span * 0.75);
    }
    // A three-quarter view from the south-west and above, as the palette's pictures.
    const double yaw = 0.75, pitch = -0.45, dist = span * 1.6 + 64;
    camera_.yaw = yaw;
    camera_.pitch = pitch;
    camera_.position = c - camera_.forward() * dist;
}

// ---- the actions ----

void Viewer::modelExtrude(double distance) {
    auto& e = *modelEditor_;
    if (e.pick != ModelEditor::Pick::Faces || e.faces.empty()) {
        status_ = "Extrude works on faces: pick some first (2).";
        return;
    }
    const auto made = mesh::extrude(e.edit.change(), e.faces, distance);
    modelChanged();
    status_ = std::format("Extruded {} face{} {} units {}; {} new.", e.faces.size(), e.faces.size() == 1 ? "" : "s",
                          std::abs(distance), distance < 0 ? "out" : "in", made.size());
}

void Viewer::modelExtrudeAndMove(std::size_t view, ImVec2 at) {
    auto& e = *modelEditor_;
    if (e.drag.kind != ModelEditor::Drag::Kind::None) return;
    if (e.selectedCount() == 0) {
        status_ = "Pick something to extrude first.";
        return;
    }
    const ModelEditor::Drag::Before before{e.points, e.edges, e.faces};
    // Read before anything is made: the new faces join edges to their copies with no area yet, so they have no normal.
    const auto directionsBefore = e.alongNormals ? modelSelectionDirections() : mesh::PointDirections{};
    e.edit.beginGesture();
    auto& m = e.edit.change();
    mesh::Extrusion made;
    switch (e.pick) {
        case ModelEditor::Pick::Points: made = mesh::extrudePointsInPlace(m, e.points); break;
        case ModelEditor::Pick::Edges: made = mesh::extrudeEdgesInPlace(m, e.edges); break;
        case ModelEditor::Pick::Faces: made = mesh::extrudeFacesInPlace(m, e.faces); break;
    }
    if (made.moving.empty()) {
        e.edit.cancelGesture();
        status_ = "Nothing to extrude there.";
        return;
    }
    // The copies are what moves: the faces keep their indices and take them; edges and points pick the copies.
    if (e.pick == ModelEditor::Pick::Points) e.points = made.moving;
    else if (e.pick == ModelEditor::Pick::Edges) e.edges = made.edges;
    modelChanged();
    // Floors and ceilings pull up and down in 3D, walls across (Shift the other way round).
    bool level = false;
    if (e.pick == ModelEditor::Pick::Faces) {
        Vec3 n;
        for (const auto f : e.faces) n = n + e.edit.mesh().normal(e.edit.mesh().faces[f]);
        level = std::abs(mesh::normalised(n).y) > 0.7;
    }
    // Along the normals: copies go the way the points they were made from would (faces kept theirs, so read them again).
    mesh::PointDirections directions;
    if (e.alongNormals) {
        if (e.pick == ModelEditor::Pick::Faces) {
            directions = mesh::directionsOfFaces(e.edit.mesh(), e.faces);
        } else {
            for (const auto& [copy, original] : made.originals) {
                if (const auto way = directionsBefore.find(original); way != directionsBefore.end()) directions[copy] = way->second;
            }
        }
    }
    if (e.alongNormals ? !startModelNormalMove(view, at, true, std::move(directions)) : !startModelGrab(view, at, true, level)) {
        const auto why = status_;
        e.edit.cancelGesture();
        e.points = before.points;
        e.edges = before.edges;
        e.faces = before.faces;
        modelChanged();
        status_ = e.alongNormals ? why : "Extrude: this view cannot move it (try another view).";
        return;
    }
    e.drag.extruding = before;
    status_ = e.alongNormals
                  ? std::format("Extruding {} new face{} along the normals: move the pointer, click to put it down, "
                                "right-click or Esc cancels.", made.faces.size(), made.faces.size() == 1 ? "" : "s")
                  : std::format("Extruding {} new face{}: move the pointer, click to put it down (3D: Shift+E the other way), "
                                "right-click or Esc cancels.", made.faces.size(), made.faces.size() == 1 ? "" : "s");
}

void Viewer::cancelModelDrag() {
    auto& e = *modelEditor_;
    if (e.drag.moving()) e.edit.cancelGesture();
    if (e.drag.extruding) {
        e.points = e.drag.extruding->points;
        e.edges = e.drag.extruding->edges;
        e.faces = e.drag.extruding->faces;
    }
    e.drag = {};
    modelChanged();
}

void Viewer::modelDelete() {
    auto& e = *modelEditor_;
    if (e.pick == ModelEditor::Pick::Edges && !e.edges.empty()) {
        // An edge goes by joining the faces either side of it (undoing a cut or a slice).
        auto copy = e.edit.mesh();
        int dissolved = 0;
        for (const auto& edge : e.edges) dissolved += mesh::dissolveEdge(copy, edge) ? 1 : 0;
        if (dissolved == 0) {
            status_ = "Those edges cannot go: an edge goes only between two faces lying flat together.";
            return;
        }
        e.edit.change() = std::move(copy);
        status_ = std::format("Removed {} edge{}, joining the faces either side.", dissolved, dissolved == 1 ? "" : "s");
        e.edges.clear();
        modelChanged();
        return;
    }
    if (e.pick == ModelEditor::Pick::Points && !e.points.empty()) {
        mesh::removePoints(e.edit.change(), e.points);
        status_ = std::format("Removed {} point{}.", e.points.size(), e.points.size() == 1 ? "" : "s");
        e.points.clear();
    } else if (e.pick == ModelEditor::Pick::Faces && !e.faces.empty()) {
        mesh::removeFaces(e.edit.change(), e.faces);
        status_ = std::format("Removed {} face{}.", e.faces.size(), e.faces.size() == 1 ? "" : "s");
        e.faces.clear();
    }
    e.points.clear();
    modelChanged();
}

void Viewer::modelFlip() {
    auto& e = *modelEditor_;
    if (e.faces.empty()) return;
    mesh::flipFaces(e.edit.change(), e.faces);
    modelChanged();
}

void Viewer::modelJoin() {
    auto& e = *modelEditor_;
    if (e.pick == ModelEditor::Pick::Edges) {
        if (e.edges.size() != 2) {
            status_ = "Join: pick two edges of one face; it is cut between their middles.";
            return;
        }
        auto copy = e.edit.mesh();
        const auto half = mesh::connectEdges(copy, *e.edges.begin(), *e.edges.rbegin());
        if (!half) {
            status_ = "Those two edges are not on one face.";
            return;
        }
        e.edit.change() = std::move(copy);
        e.edges.clear();
        modelChanged();
        status_ = "Cut the face between the two edges' middles.";
        return;
    }
    if (e.pick != ModelEditor::Pick::Points || e.points.size() < 2) {
        status_ = "Join: pick two points of one face to cut it, three or more to fill a face (1), or two edges (2).";
        return;
    }
    if (e.points.size() == 2) {
        const auto a = *e.points.begin(), b = *e.points.rbegin();
        const auto& m = e.edit.mesh();
        for (std::uint32_t f = 0; f < m.faces.size(); ++f) {
            const auto& p = m.faces[f].points;
            if (std::ranges::find(p, a) == p.end() || std::ranges::find(p, b) == p.end()) continue;
            auto copy = m;
            if (const auto half = mesh::splitFace(copy, f, a, b)) {
                e.edit.change() = std::move(copy);
                e.faces = {f, *half};
                modelChanged();
                status_ = "Cut the face in two.";
                return;
            }
        }
        status_ = "Those two points are not across a face from each other.";
        return;
    }
    const auto viewer = cameraOf(pointerView_).position;
    auto copy = e.edit.mesh();
    const auto made = mesh::fillFace(copy, e.points, e.brush, viewer);
    if (!made) {
        status_ = "Those points are in a line: no face fits them.";
        return;
    }
    e.edit.change() = std::move(copy);
    e.pick = ModelEditor::Pick::Faces;
    e.faces = {*made};
    modelChanged();
    status_ = std::format("Filled a face of {} points, facing this view (N flips it).", e.points.size());
}

void Viewer::modelSplitEdge() {
    auto& e = *modelEditor_;
    if (e.pick != ModelEditor::Pick::Points || e.points.size() != 2) {
        status_ = "Split edge: pick the two points at its ends.";
        return;
    }
    auto copy = e.edit.mesh();
    const auto mid = mesh::splitEdge(copy, *e.points.begin(), *e.points.rbegin());
    if (!mid) {
        status_ = "No face has that edge.";
        return;
    }
    e.edit.change() = std::move(copy);
    e.points = {*mid};
    modelChanged();
}

void Viewer::modelSplitEdges() {
    auto& e = *modelEditor_;
    if (e.pick != ModelEditor::Pick::Edges || e.edges.empty()) return;
    auto copy = e.edit.mesh();
    mesh::PointSet made;
    for (const auto& [a, b] : e.edges) {
        if (const auto mid = mesh::splitEdge(copy, a, b)) made.insert(*mid);
    }
    e.edit.change() = std::move(copy);
    e.edges.clear();
    e.pick = ModelEditor::Pick::Points;
    e.points = std::move(made);
    modelChanged();
    status_ = std::format("Split {} edge{}; their new middle points are selected.", e.points.size(), e.points.size() == 1 ? "" : "s");
}

void Viewer::modelExtendEdges(int vertical) {
    auto& e = *modelEditor_;
    if (e.pick != ModelEditor::Pick::Edges || e.edges.empty()) return;
    auto copy = e.edit.mesh();
    std::vector<std::uint32_t> made;
    if (vertical != 0) {
        made = mesh::extendEdges(copy, e.edges, {0, vertical * e.extrudeBy, 0});
    } else {
        // Each edge out across its own face (a floor's edges outward, a wall's top upward...).
        for (const auto& edge : e.edges) {
            const auto out = mesh::outwardOf(copy, edge);
            if (!out) continue;
            const auto one = mesh::extendEdges(copy, {edge}, *out * e.extrudeBy);
            made.insert(made.end(), one.begin(), one.end());
        }
    }
    if (made.empty()) {
        status_ = "Nothing to grow from: those edges border no face.";
        return;
    }
    e.edit.change() = std::move(copy);
    // The new faces' far edges, ready to grow again.
    const auto& m = e.edit.mesh();
    e.edges.clear();
    for (const auto f : made) {
        const auto& p = m.faces[f].points;
        e.edges.insert(mesh::edgeOf(p[2], p[3]));
    }
    modelChanged();
    status_ = std::format("Grew {} face{} {:g} units; their far edges are selected (grow again to go further).", made.size(),
                          made.size() == 1 ? "" : "s", e.extrudeBy);
}

void Viewer::modelWeld() {
    auto& e = *modelEditor_;
    if (e.pick == ModelEditor::Pick::Edges && !e.edges.empty()) {
        // Each edge to a point at its middle; later edges found again by where their ends are (welds renumber).
        auto copy = e.edit.mesh();
        std::vector<std::pair<Vec3, Vec3>> ends;
        for (const auto& [a, b] : e.edges) ends.emplace_back(copy.points[a], copy.points[b]);
        auto find = [&](Vec3 p) -> std::optional<std::uint32_t> {
            for (std::uint32_t i = 0; i < copy.points.size(); ++i) {
                if (mesh::length(copy.points[i] - p) < 1e-6) return i;
            }
            return std::nullopt;
        };
        for (const auto& [pa, pb] : ends) {
            const auto a = find(pa), b = find(pb);
            if (a && b && *a != *b) mesh::weld(copy, {*a, *b}, (pa + pb) * 0.5);
        }
        e.edit.change() = std::move(copy);
        e.edges.clear();
        modelChanged();
        status_ = "Each edge became a point at its middle.";
        return;
    }
    if (e.pick != ModelEditor::Pick::Points || e.points.size() < 2) {
        status_ = "Weld: pick two or more points; they become one at their middle.";
        return;
    }
    const auto centre = modelSelectionCentre();
    const auto kept = mesh::weld(e.edit.change(), e.points, mesh::snapTo(*centre, e.grid));
    e.points.clear();
    if (kept) e.points.insert(*kept);
    e.faces.clear();
    modelChanged();
}

void Viewer::modelMove(Vec3 offset) {
    const auto moving = modelMovingPoints();
    if (moving.empty()) return;
    mesh::movePoints(modelEditor_->edit.change(), moving, offset);
    modelChanged();
}

mesh::PointDirections Viewer::modelSelectionDirections() const {
    const auto& e = *modelEditor_;
    switch (e.pick) {
        case ModelEditor::Pick::Points: return mesh::directionsOfPoints(e.edit.mesh(), e.points);
        case ModelEditor::Pick::Edges: return mesh::directionsOfEdges(e.edit.mesh(), e.edges);
        case ModelEditor::Pick::Faces: return mesh::directionsOfFaces(e.edit.mesh(), e.faces);
    }
    return {};
}

bool Viewer::startModelNormalMove(std::size_t view, ImVec2 at, bool byKey, mesh::PointDirections directions) {
    auto& e = *modelEditor_;
    if (directions.empty()) {
        status_ = "No normal to go along: the selection is on no face.";
        return false;
    }
    const auto& m = e.edit.mesh();
    // The point nearest the pointer leads.
    float best = 1e30f;
    auto lead = directions.begin();
    for (auto it = directions.begin(); it != directions.end(); ++it) {
        if (const auto s = toScreen(view, m.points[it->first]); s && distance(*s, at) < best) {
            best = distance(*s, at);
            lead = it;
        }
    }
    const Vec3 anchor = m.points[lead->first], way = mesh::normalised(lead->second);
    const auto a = toScreen(view, anchor), b = toScreen(view, anchor + way * 100.0);
    if (!a || !b || distance(*a, *b) < 4) {
        status_ = "The normal points straight at this view: move it in another.";
        return false;
    }
    auto& d = e.drag;
    d = {};
    d.kind = ModelEditor::Drag::Kind::Normal;
    d.view = view;
    d.press = at;
    d.byKey = byKey;
    d.anchor = anchor;
    d.normalLead = way;
    for (const auto& [p, _] : directions) d.start[p] = m.points[p];
    d.directions = std::move(directions);
    e.edit.beginGesture();
    if (byKey) status_ = "Moving along the normals: click to put it down, right-click or Esc cancels, G slides along the edges.";
    return true;
}

bool Viewer::startModelGrab(std::size_t view, ImVec2 at, bool byKey, bool upAndDown) {
    auto& e = *modelEditor_;
    auto& d = e.drag;
    const auto moving = modelMovingPoints();
    if (moving.empty()) return false;
    if (e.alongNormals) return startModelNormalMove(view, at, byKey, modelSelectionDirections());
    const auto& v = views_[view];
    d = {};
    d.view = view;
    d.press = at;
    d.byKey = byKey;
    // The corner nearest the pointer leads (and snaps).
    const auto& m = e.edit.mesh();
    float best = 1e30f;
    for (const auto p : moving) {
        d.start[p] = m.points[p];
        if (const auto s = toScreen(view, m.points[p]); s && distance(*s, at) < best) {
            best = distance(*s, at);
            d.anchor = m.points[p];
        }
    }
    const auto cam = cameraOf(view);
    d.vertical = v.kind == ViewKind::Perspective && (ImGui::GetIO().KeyShift != upAndDown);
    if (v.kind != ViewKind::Perspective) d.planeNormal = cam.forward();
    else if (d.vertical) d.planeNormal = mesh::normalised({cam.forward().x, 0, cam.forward().z});
    else d.planeNormal = {0, 1, 0};
    const auto ray = cam.rayThrough(at.x - v.min.x, at.y - v.min.y, v.size.x, v.size.y);
    const double denom = mesh::dot(ray.direction, d.planeNormal);
    if (std::abs(denom) < 1e-6) {   // the plane is edge-on: nothing sensible to drag along
        d = {};
        return false;
    }
    d.grabHit = ray.origin + ray.direction * (mesh::dot(d.anchor - ray.origin, d.planeNormal) / denom);
    d.kind = ModelEditor::Drag::Kind::Plane;
    e.edit.beginGesture();
    if (byKey) status_ = "Moving: click to put it down, right-click or Esc cancels, G again slides along the edges.";
    return true;
}

bool Viewer::startModelSlide(std::size_t view, ImVec2 at) {
    auto& e = *modelEditor_;
    auto& d = e.drag;
    const auto moving = modelMovingPoints();
    if (moving.empty()) return false;
    const auto& m = e.edit.mesh();
    auto neighbours = mesh::slideNeighbours(m, moving);
    if (neighbours.empty()) {
        status_ = "Nothing to slide along: no edge leads away from the selection.";
        return false;
    }
    d = {};
    d.kind = ModelEditor::Drag::Kind::Slide;
    d.view = view;
    d.press = at;
    d.byKey = true;
    d.neighbours = std::move(neighbours);
    float best = 1e30f;
    for (const auto p : moving) {
        d.start[p] = m.points[p];
        if (!d.neighbours.contains(p)) continue;
        if (const auto s = toScreen(view, m.points[p]); s && distance(*s, at) < best) {
            best = distance(*s, at);
            d.anchorPoint = p;
        }
    }
    e.edit.beginGesture();
    status_ = "Sliding along the edges: move the pointer, click to put it down, right-click or Esc cancels.";
    return true;
}

void Viewer::modelScale(Vec3 factors) {
    const auto moving = modelMovingPoints();
    const auto centre = modelSelectionCentre();
    if (moving.empty() || !centre || factors.x == 0 || factors.y == 0 || factors.z == 0) return;
    mesh::scalePoints(modelEditor_->edit.change(), moving, *centre, factors);
    modelChanged();
}

void Viewer::modelTurnAbout(int axis, double degrees) {
    const auto moving = modelMovingPoints();
    const auto centre = modelSelectionCentre();
    if (moving.empty() || !centre) return;
    mesh::turnPointsAbout(modelEditor_->edit.change(), moving, *centre, axis, degrees);
    modelChanged();
}

void Viewer::modelTurn(double degrees) {
    const auto moving = modelMovingPoints();
    const auto centre = modelSelectionCentre();
    if (moving.empty() || !centre) return;
    mesh::turnPoints(modelEditor_->edit.change(), moving, *centre, degrees);
    modelChanged();
}

void Viewer::modelMirror(int axis) {
    const auto moving = modelMovingPoints();
    const auto centre = modelSelectionCentre();
    if (moving.empty() || !centre) return;
    Vec3 factors{1, 1, 1};
    component(factors, axis) = -1;
    mesh::scalePoints(modelEditor_->edit.change(), moving, *centre, factors);
    modelChanged();
}

void Viewer::modelDuplicate() {
    auto& e = *modelEditor_;
    if (e.pick != ModelEditor::Pick::Faces || e.faces.empty()) return;
    const auto made = mesh::duplicateFaces(e.edit.change(), e.faces, {e.grid, 0, 0});
    e.faces = {made.begin(), made.end()};
    modelChanged();
    status_ = std::format("Copied {} face{}, one grid step east.", made.size(), made.size() == 1 ? "" : "s");
}

void Viewer::modelApplyTexture(mesh::FaceTexture texture) {
    auto& e = *modelEditor_;
    e.brush = texture;
    if (e.pick != ModelEditor::Pick::Faces || e.faces.empty()) return;
    mesh::setTexture(e.edit.change(), e.faces, texture);
    modelChanged();
}

void Viewer::modelAddBox() {
    auto& e = *modelEditor_;
    const auto made = mesh::addBox(e.edit.change(), e.shape, e.inward, e.sides, {});
    e.pick = ModelEditor::Pick::Faces;
    e.faces = {made.begin(), made.end()};
    e.points.clear();
    modelChanged();
}

void Viewer::modelCutOpening() {
    auto& e = *modelEditor_;
    if (e.pick != ModelEditor::Pick::Faces || e.faces.empty()) return;
    auto copy = e.edit.mesh();
    mesh::FaceSet result;
    int cut = 0;
    // Highest index first: a face cut away whole leaves the indices below it alone.
    for (auto it = e.faces.rbegin(); it != e.faces.rend(); ++it) {
        const auto made = mesh::cutOpening(copy, *it, e.opening);
        if (made.pieces.empty() && made.reveal.empty()) continue;
        ++cut;
        result.insert(made.reveal.begin(), made.reveal.end());
    }
    if (cut == 0) {
        status_ = "The opening misses the selected faces (it is placed from each face's bottom and middle).";
        return;
    }
    e.edit.change() = std::move(copy);
    e.faces = std::move(result);   // the reveal, ready to texture; nothing when it is just a hole
    modelChanged();
    status_ = std::format("Cut {} opening{} {:g} x {:g}{}.", cut, cut == 1 ? "" : "s", e.opening.width, e.opening.height,
                          e.opening.depth > 0 ? std::format(", lined {:g} deep (selected)", e.opening.depth) : "");
}

void Viewer::modelSlice() {
    auto& e = *modelEditor_;
    if (e.pick != ModelEditor::Pick::Faces || e.faces.empty()) return;
    auto copy = e.edit.mesh();
    const auto halves = mesh::sliceFaces(copy, e.faces, e.sliceAxis, e.sliceAt);
    if (halves.empty()) {
        status_ = std::format("{} {:g} does not cross the selected faces.", "XYZ"[e.sliceAxis], e.sliceAt);
        return;
    }
    e.edit.change() = std::move(copy);
    e.faces.insert(halves.begin(), halves.end());
    modelChanged();
    status_ = std::format("Sliced {} face{} at {} = {:g}.", halves.size(), halves.size() == 1 ? "" : "s", "XYZ"[e.sliceAxis], e.sliceAt);
}

void Viewer::modelAddStairs() {
    auto& e = *modelEditor_;
    const auto made = mesh::addStairs(e.edit.change(), e.shape, e.steps, {});
    e.pick = ModelEditor::Pick::Faces;
    e.faces = {made.begin(), made.end()};
    e.points.clear();
    modelChanged();
}

// ---- the right-click menu ----

std::optional<Vec3> Viewer::modelFaceHit(std::size_t view, ImVec2 at, std::uint32_t face) const {
    const auto& m = modelEditor_->edit.mesh();
    if (face >= m.faces.size()) return std::nullopt;
    const auto& v = views_[view];
    const auto ray = cameraOf(view).rayThrough(at.x - v.min.x, at.y - v.min.y, v.size.x, v.size.y);
    const auto& f = m.faces[face];
    std::optional<double> best;
    for (const auto& t : mesh::triangulate(m, f)) {
        const auto hit = scene::intersectTriangle(ray, m.points[f.points[t[0]]], m.points[f.points[t[1]]], m.points[f.points[t[2]]]);
        if (hit && (!best || *hit < *best)) best = hit;
    }
    if (best) return ray.origin + ray.direction * *best;
    // Edge-on in a 2D view (picked by its middle): where the ray passes closest to that middle.
    const Vec3 c = m.centre(f);
    return ray.origin + ray.direction * mesh::dot(c - ray.origin, ray.direction);
}

void Viewer::modelSnapToGrid() {
    auto& e = *modelEditor_;
    const auto moving = modelMovingPoints();
    if (moving.empty()) return;
    auto& m = e.edit.change();
    for (const auto p : moving) m.points[p] = mesh::snapTo(m.points[p], e.grid);
    modelChanged();
    status_ = std::format("{} point{} onto the {:g} grid.", moving.size(), moving.size() == 1 ? "" : "s", e.grid);
}

void Viewer::modelSelectByTexture(mesh::FaceTexture texture) {
    auto& e = *modelEditor_;
    const auto& m = e.edit.mesh();
    e.pick = ModelEditor::Pick::Faces;
    e.faces.clear();
    for (std::uint32_t f = 0; f < m.faces.size(); ++f) {
        if (m.faces[f].texture == texture) e.faces.insert(f);
    }
    status_ = std::format("{} face{} in {}/{}.", e.faces.size(), e.faces.size() == 1 ? "" : "s", texture.archive, texture.record);
}

void Viewer::drawModelEmptyMenu() {
    auto& e = *modelEditor_;
    const auto& m = e.edit.mesh();
    const bool faces = e.pick == ModelEditor::Pick::Faces;
    if (ImGui::MenuItem("Add box", nullptr, false)) modelAddBox();
    ImGui::SetItemTooltip("The Shapes section's box: %.0f, %.0f, %.0f to %.0f, %.0f, %.0f", e.shape.min.x, e.shape.min.y,
                          e.shape.min.z, e.shape.max.x, e.shape.max.y, e.shape.max.z);
    if (ImGui::MenuItem("Add stairs")) modelAddStairs();
    ImGui::Separator();
    if (ImGui::MenuItem("Select all", "Ctrl+A")) {
        e.clearPicked();
        if (e.pick == ModelEditor::Pick::Edges) {
            e.edges = mesh::edgesOf(m);
        } else {
            const auto n = static_cast<std::uint32_t>(faces ? m.faces.size() : m.points.size());
            for (std::uint32_t i = 0; i < n; ++i) e.select(i, false);
        }
    }
    if (ImGui::MenuItem("Frame the model", "F")) {
        e.points.clear();
        e.edges.clear();
        e.faces.clear();
        frameModel();
    }
    ImGui::Separator();
    if (ImGui::MenuItem("Pick points", "1", e.pick == ModelEditor::Pick::Points)) e.pick = ModelEditor::Pick::Points;
    if (ImGui::MenuItem("Pick edges", "2", e.pick == ModelEditor::Pick::Edges)) e.pick = ModelEditor::Pick::Edges;
    if (ImGui::MenuItem("Pick faces", "3", e.pick == ModelEditor::Pick::Faces)) e.pick = ModelEditor::Pick::Faces;
}

void Viewer::drawModelContextMenu() {
    if (!modelEditor_ || !modelEditor_->menu) return;
    const auto menu = *modelEditor_->menu;
    if (!menu.element) {
        ImGui::TextDisabled("Nothing here");
        ImGui::Separator();
        drawModelEmptyMenu();
        return;
    }
    drawModelSelectionMenu(menu);
    // Opened away from the selection: what empty space offers, after the selection's menu.
    if (menu.elsewhere && modelEditor_) {
        ImGui::Separator();
        if (ImGui::BeginMenu("Empty space")) {
            drawModelEmptyMenu();
            ImGui::EndMenu();
        }
    }
}

void Viewer::drawModelSelectionMenu(const ModelEditor::Menu& menu) {
    auto& e = *modelEditor_;
    const auto& m = e.edit.mesh();
    const bool faces = e.pick == ModelEditor::Pick::Faces;
    const auto accent = ImGui::GetStyleColorVec4(ImGuiCol_CheckMark);

    if (e.pick == ModelEditor::Pick::Edges) {
        const auto& list = e.edgesList();
        if (*menu.element >= list.size()) return;
        const auto edge = list[*menu.element];
        const double len = mesh::length(m.points[edge.second] - m.points[edge.first]);
        if (e.edges.size() > 1) ImGui::TextColored(accent, "%zu edges", e.edges.size());
        else ImGui::TextColored(accent, "Edge %u-%u  (%.0f long)", edge.first, edge.second, len);
        ImGui::Separator();
        if (ImGui::MenuItem("Extrude", "E")) modelExtrudeAndMove(pointerView_, menu.screen);
        ImGui::SetItemTooltip("A new face from each edge to a copy of it that follows the pointer; a click puts it down");
        if (ImGui::MenuItem(std::format("Grow out {:g}", e.extrudeBy).c_str())) modelExtendEdges(0);
        ImGui::SetItemTooltip("A new face from each edge, out across the face beside it");
        if (ImGui::MenuItem(std::format("Grow up {:g}", e.extrudeBy).c_str())) modelExtendEdges(1);
        if (ImGui::MenuItem(std::format("Grow down {:g}", e.extrudeBy).c_str())) modelExtendEdges(-1);
        ImGui::Separator();
        if (ImGui::MenuItem("Split in the middle")) modelSplitEdges();
        if (ImGui::MenuItem("Cut the face between them", "J", false, e.edges.size() == 2)) modelJoin();
        ImGui::SetItemTooltip("Two edges of one face: a cut straight across it between their middles");
        if (ImGui::MenuItem("Collapse to a point")) modelWeld();
        if (ImGui::MenuItem("Onto the grid")) modelSnapToGrid();
        if (ImGui::MenuItem("Select the faces beside it")) {
            mesh::FaceSet beside;
            for (const auto& sel : e.edges) {
                for (const auto f : mesh::facesWithEdge(m, sel)) beside.insert(f);
            }
            e.pick = ModelEditor::Pick::Faces;
            e.faces = std::move(beside);
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Remove (join the faces either side)", "Del")) modelDelete();
        if (ImGui::MenuItem("Move with the pointer", "G")) startModelGrab(pointerView_, menu.screen, true);
        if (ImGui::MenuItem("Slide along the edges", "G G")) startModelSlide(pointerView_, menu.screen);
        if (ImGui::MenuItem("Frame", "F")) frameModel();
        return;
    }

    if (faces) {
        const auto f = *menu.element;
        if (f >= m.faces.size()) return;
        const auto texture = m.faces[f].texture;
        if (e.faces.size() > 1) ImGui::TextColored(accent, "%zu faces", e.faces.size());
        else ImGui::TextColored(accent, "Face %u  (%d/%d)", f, texture.archive, texture.record);
        ImGui::Separator();
        if (ImGui::MenuItem("Extrude", "E")) modelExtrudeAndMove(pointerView_, menu.screen);
        ImGui::SetItemTooltip("The faces follow the pointer, joined to where they were by new sides; a click puts them down.\n"
                              "In 3D floors and ceilings go up and down, walls across (Shift+E the other way)");
        if (ImGui::MenuItem(std::format("Push out {:g}", e.extrudeBy).c_str(), "X")) modelExtrude(-e.extrudeBy);
        if (ImGui::MenuItem(std::format("Pull in {:g}", e.extrudeBy).c_str(), "Shift+X")) modelExtrude(e.extrudeBy);
        if (ImGui::BeginMenu("Cut an opening")) {
            const double depth = e.opening.depth;
            auto cut = [&](const char* label, mesh::Opening o) {
                if (ImGui::MenuItem(label)) {
                    e.opening = o;
                    modelCutOpening();
                }
                if (ImGui::IsItemHovered()) e.hoverOpening = o;
            };
            cut("Door 64 x 96", {.width = 64, .height = 96, .bottom = 0, .across = 0, .depth = depth});
            cut("Arch 128 x 128", {.width = 128, .height = 128, .bottom = 0, .across = 0, .depth = depth});
            cut("Window 32 x 32", {.width = 32, .height = 32, .bottom = 48, .across = 0, .depth = depth});
            ImGui::Separator();
            if (ImGui::MenuItem(std::format("The Cuts section's: {:g} x {:g}, up {:g}, across {:g}", e.opening.width,
                                            e.opening.height, e.opening.bottom, e.opening.across).c_str())) {
                modelCutOpening();
            }
            if (ImGui::IsItemHovered()) e.hoverOpening = e.opening;
            ImGui::Separator();
            ImGui::TextDisabled("Reveal depth %g (set in the Cuts section)", depth);
            ImGui::EndMenu();
        }
        if (menu.at && ImGui::BeginMenu("Slice here")) {
            const auto at = mesh::snapTo(*menu.at, e.grid);
            for (int axis = 0; axis < 3; ++axis) {
                const double value = axis == 0 ? at.x : axis == 1 ? at.y : at.z;
                const char* what = axis == 1 ? "across (level)" : "up and down";
                if (ImGui::MenuItem(std::format("{} = {:g}   {}", "XYZ"[axis], value, what).c_str())) {
                    e.sliceAxis = axis;
                    e.sliceAt = value;
                    modelSlice();
                }
            }
            ImGui::EndMenu();
        }
        ImGui::SetItemTooltip("Cut the selected faces along a grid line through the point clicked");
        ImGui::Separator();
        if (ImGui::MenuItem("Take its texture")) {
            e.brush = texture;
            e.browseArchive = texture.archive;
        }
        if (ImGui::MenuItem(std::format("Lay {}/{} on it", e.brush.archive, e.brush.record).c_str(), nullptr, false,
                            !(texture == e.brush) || e.faces.size() > 1)) {
            modelApplyTexture(e.brush);
        }
        if (ImGui::MenuItem("Fit the texture")) {
            auto& changing = e.edit.change();
            for (const auto sf : e.faces) {
                const auto& t = textures_.get(changing.faces[sf].texture.archive, changing.faces[sf].texture.record);
                mesh::fitTexture(changing, sf, t.width, t.height);
            }
            modelChanged();
        }
        if (ImGui::MenuItem("Classic alignment")) {
            auto& changing = e.edit.change();
            for (const auto sf : e.faces) changing.faces[sf].uv = {};
            modelChanged();
        }
        if (ImGui::MenuItem(std::format("Select every face in {}/{}", texture.archive, texture.record).c_str())) {
            modelSelectByTexture(texture);
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Flip", "N")) modelFlip();
        if (ImGui::MenuItem("Copy", "Ctrl+D")) modelDuplicate();
        if (ImGui::MenuItem("Delete", "Del")) modelDelete();
        if (ImGui::MenuItem("Move with the pointer", "G")) startModelGrab(pointerView_, menu.screen, true);
        if (ImGui::MenuItem("Slide along the edges", "G G")) startModelSlide(pointerView_, menu.screen);
        ImGui::SetItemTooltip("The faces slide along the edges leading away from them: a wall along the floor");
        if (ImGui::MenuItem("Frame", "F")) frameModel();
        return;
    }

    const auto p = *menu.element;
    if (p >= m.points.size()) return;
    if (e.points.size() > 1) {
        ImGui::TextColored(accent, "%zu points", e.points.size());
    } else {
        const auto& q = m.points[p];
        ImGui::TextColored(accent, "Point %u  (%.0f, %.0f, %.0f)", p, q.x, q.y, q.z);
    }
    ImGui::Separator();
    const auto n = e.points.size();
    if (ImGui::MenuItem("Extrude", "E")) modelExtrudeAndMove(pointerView_, menu.screen);
    ImGui::SetItemTooltip("Copies follow the pointer: where two picked points share a face's edge, a new face joins\n"
                          "the edge to its copy; a point on its own is copied alone (fill faces to it with J)");
    if (ImGui::MenuItem(n == 2 ? "Cut the face between them" : "Fill a face between them", "J", false, n >= 2)) modelJoin();
    if (ImGui::MenuItem("Split the edge between them", nullptr, false, n == 2)) modelSplitEdge();
    if (ImGui::MenuItem("Weld into one", nullptr, false, n >= 2)) modelWeld();
    if (ImGui::MenuItem("Onto the grid")) modelSnapToGrid();
    ImGui::SetItemTooltip("Each selected point to the nearest %g grid corner", e.grid);
    ImGui::Separator();
    if (ImGui::MenuItem("Move with the pointer", "G")) startModelGrab(pointerView_, menu.screen, true);
    if (ImGui::MenuItem("Slide along an edge", "G G")) startModelSlide(pointerView_, menu.screen);
    if (ImGui::MenuItem("Delete", "Del")) modelDelete();
    if (ImGui::MenuItem("Frame", "F")) frameModel();
}

// ---- keys ----

bool Viewer::handleModelKey(const SDL_KeyboardEvent& key) {
    auto& e = *modelEditor_;
    const bool ctrl = (key.mod & SDL_KMOD_CTRL) != 0;
    const bool shift = (key.mod & SDL_KMOD_SHIFT) != 0;
    if (key.key == SDLK_ESCAPE) {
        if (e.drag.kind != ModelEditor::Drag::Kind::None) cancelModelDrag();
        else if (!e.points.empty() || !e.edges.empty() || !e.faces.empty()) {
            e.points.clear();
            e.edges.clear();
            e.faces.clear();
        }
        modelChanged();
        return true;
    }
    if (key.key == SDLK_Z && shift && !ctrl) {
        maximised_ = maximised_ ? std::nullopt : std::optional(pointerView_);
        return true;
    }
    if (key.key == SDLK_G && !ctrl) {
        const auto at = ImGui::GetIO().MousePos;
        if (e.drag.extruding) {
            // Sliding starts from where things were; an extrusion has nowhere to slide from.
        } else if ((e.drag.kind == ModelEditor::Drag::Kind::Plane || e.drag.kind == ModelEditor::Drag::Kind::Normal) && e.drag.byKey) {
            // G again: from moving freely to sliding along the edges (from where it all started).
            const auto view = e.drag.view;
            e.edit.cancelGesture();
            e.drag = {};
            modelChanged();
            startModelSlide(view, at);
        } else if (e.drag.kind == ModelEditor::Drag::Kind::Slide) {
            const auto view = e.drag.view;
            e.edit.cancelGesture();
            e.drag = {};
            modelChanged();
            startModelGrab(view, at, true);
        } else if (e.drag.kind == ModelEditor::Drag::Kind::None) {
            if (modelMovingPoints().empty()) status_ = "Pick something to move first.";
            else startModelGrab(pointerView_, at, true);
        }
        return true;
    }
    if (e.drag.kind != ModelEditor::Drag::Kind::None) return false;
    if (key.key == SDLK_E && !ctrl && !looking_) {
        modelExtrudeAndMove(pointerView_, ImGui::GetIO().MousePos);
        return true;
    }
    if (key.key == SDLK_R && !ctrl) {
        e.tool = e.tool == ModelEditor::Tool::Move ? ModelEditor::Tool::Turn
                 : e.tool == ModelEditor::Tool::Turn ? ModelEditor::Tool::Scale
                                                     : ModelEditor::Tool::Move;
        return true;
    }
    if (ctrl) {
        switch (key.key) {
            case SDLK_Z:
                if (e.edit.undo()) modelChanged();
                return true;
            case SDLK_Y:
                if (e.edit.redo()) modelChanged();
                return true;
            case SDLK_S: saveProject(); return true;
            case SDLK_D: modelDuplicate(); return true;
            case SDLK_A: {
                const auto& m = e.edit.mesh();
                if (e.pick == ModelEditor::Pick::Edges) {
                    e.edges = mesh::edgesOf(m);
                    return true;
                }
                auto& set = e.pick == ModelEditor::Pick::Points ? e.points : e.faces;
                const auto n = static_cast<std::uint32_t>(e.pick == ModelEditor::Pick::Points ? m.points.size() : m.faces.size());
                set.clear();
                for (std::uint32_t i = 0; i < n; ++i) set.insert(i);
                return true;
            }
            default: return false;
        }
    }
    const double g = e.grid;
    switch (key.key) {
        case SDLK_LEFTBRACKET: stepModelGrid(-1); return true;
        case SDLK_RIGHTBRACKET: stepModelGrid(1); return true;
        case SDLK_1: e.pick = ModelEditor::Pick::Points; return true;
        case SDLK_2: e.pick = ModelEditor::Pick::Edges; return true;
        case SDLK_3: e.pick = ModelEditor::Pick::Faces; return true;
        case SDLK_TAB:
            e.pick = e.pick == ModelEditor::Pick::Points  ? ModelEditor::Pick::Edges
                     : e.pick == ModelEditor::Pick::Edges ? ModelEditor::Pick::Faces
                                                          : ModelEditor::Pick::Points;
            return true;
        case SDLK_DELETE:
        case SDLK_BACKSPACE: modelDelete(); return true;
        case SDLK_X: modelExtrude(shift ? e.extrudeBy : -e.extrudeBy); return true;
        case SDLK_N:
            if (shift) {
                e.alongNormals = !e.alongNormals;
                status_ = e.alongNormals ? "Along normals: moving and extruding go along the faces' normals." : "Along normals off.";
            } else {
                modelFlip();
            }
            return true;
        case SDLK_J: modelJoin(); return true;
        case SDLK_F: frameModel(); return true;
        case SDLK_HOME: frameModel(); return true;
        case SDLK_LEFT: modelMove({-g, 0, 0}); return true;
        case SDLK_RIGHT: modelMove({g, 0, 0}); return true;
        case SDLK_UP: modelMove({0, 0, g}); return true;
        case SDLK_DOWN: modelMove({0, 0, -g}); return true;
        case SDLK_PAGEUP: modelMove({0, g, 0}); return true;
        case SDLK_PAGEDOWN: modelMove({0, -g, 0}); return true;
        default: return false;
    }
}

// ---- the panel ----

void Viewer::stepModelGrid(int direction) {
    auto& e = *modelEditor_;
    auto it = std::ranges::find(kGridSteps, e.grid);
    if (it == kGridSteps.end()) it = std::ranges::lower_bound(kGridSteps, e.grid);
    const auto i = std::clamp<std::ptrdiff_t>((it - kGridSteps.begin()) + direction, 0, std::ssize(kGridSteps) - 1);
    e.grid = kGridSteps[static_cast<std::size_t>(i)];
    status_ = std::format("Grid {:g} ([ smaller, ] bigger).", e.grid);
}

void Viewer::loadTextureArchiveLists() {
    if (!textureArchives_.empty()) return;
    textureArchives_ = formats::listTextureArchives(arena2_.dir);
    // Which archives classic dungeon pieces wear, counted over every face of every model a classic block places.
    for (const auto& [id, info] : catalog_.models()) {
        if (info.uses == 0 || project::isCustomModelId(id)) continue;
        const auto index = arena2_.arch3d.findById(id);
        if (!index) continue;
        const auto mesh = formats::parseArch3dMesh(arena2_.arch3d.record(*index), id);
        if (!mesh) continue;
        std::set<int> mine;
        for (const auto& plane : mesh->planes) mine.insert(plane.textureArchive());
        for (const int a : mine) ++classicTextureUses_[a];
    }
}

void Viewer::drawModelTextures() {
    auto& e = *modelEditor_;
    loadTextureArchiveLists();
    const auto descriptionOf = [&](int a) -> std::string {
        const auto it = std::ranges::find(textureArchives_, a, &formats::TextureArchiveEntry::archive);
        return it == textureArchives_.end() ? std::string() : it->description;
    };
    const auto isBase = [](int a) { return std::ranges::find(kBaseArchives, a) != kBaseArchives.end(); };
    const auto label = [&](int a) {
        const auto d = descriptionOf(a);
        return std::format("{}{}", a, d.empty() ? "" : "  " + d);
    };
    // Step to the archive before or after (only ones that exist).
    auto step = [&](int direction) {
        if (textureArchives_.empty()) return;
        auto it = std::ranges::lower_bound(textureArchives_, e.browseArchive, {}, &formats::TextureArchiveEntry::archive);
        if (direction < 0) {
            if (it != textureArchives_.begin()) --it;
        } else if (it != textureArchives_.end() && it->archive == e.browseArchive) {
            ++it;
        }
        if (it == textureArchives_.end()) --it;
        e.browseArchive = it->archive;
    };
    if (ImGui::ArrowButton("##previous", ImGuiDir_Left)) step(-1);
    ImGui::SameLine(0, 2);
    ImGui::SetNextItemWidth(-ImGui::GetFrameHeight() - 4);
    ImGui::SetNextWindowSizeConstraints({320, 0}, {600, 460});
    if (ImGui::BeginCombo("##archive", label(e.browseArchive).c_str(), ImGuiComboFlags_HeightLarge)) {
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextWithHint("##filter", "number or name (stone, wood, sewer...)", textureFilter_, sizeof textureFilter_);
        std::string want(textureFilter_);
        for (auto& c : want) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        auto matches = [&](int a) {
            if (want.empty()) return true;
            auto text = label(a);
            for (auto& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return text.find(want) != std::string::npos;
        };
        auto item = [&](int a, const std::string& extra) {
            if (!matches(a)) return;
            if (ImGui::Selectable(std::format("{}{}##{}", label(a), extra, a).c_str(), a == e.browseArchive)) {
                e.browseArchive = a;
                textureFilter_[0] = 0;
            }
        };
        ImGui::SeparatorText("Dungeon sets (each dungeon swaps them)");
        for (const int a : kBaseArchives) item(a, "");
        ImGui::SeparatorText("Used by classic dungeon pieces");
        std::vector<std::pair<std::size_t, int>> used;
        for (const auto& [a, n] : classicTextureUses_) {
            if (!isBase(a)) used.emplace_back(n, a);
        }
        std::ranges::sort(used, std::greater{});
        for (const auto& [n, a] : used) item(a, std::format("   ({} model{})", n, n == 1 ? "" : "s"));
        ImGui::SeparatorText("All the others");
        for (const auto& t : textureArchives_) {
            if (!isBase(t.archive) && !classicTextureUses_.contains(t.archive)) item(t.archive, "");
        }
        ImGui::EndCombo();
    }
    ImGui::SetItemTooltip("Every TEXTURE file in your ARENA2. The six dungeon sets are swapped by each dungeon's texture\n"
                          "table, as classic walls are (a face in set 120 shows that dungeon's walls); any other\n"
                          "archive shows as it is, in every dungeon.");
    ImGui::SameLine(0, 2);
    if (ImGui::ArrowButton("##next", ImGuiDir_Right)) step(1);
    ImGui::SetNextItemWidth(70);
    ImGui::InputInt("archive##number", &e.browseArchive, 0);
    e.browseArchive = std::clamp(e.browseArchive, 0, 511);
    ImGui::SameLine();
    ImGui::BeginDisabled(e.faces.empty());
    if (ImGui::Button("Pick from face")) {
        const auto& m = e.edit.mesh();
        e.brush = m.faces[*e.faces.begin()].texture;
        e.browseArchive = e.brush.archive;
    }
    ImGui::EndDisabled();

    const auto count = textures_.recordCount(e.browseArchive);
    if (count == 0) {
        ImGui::TextDisabled("No TEXTURE.%03d.", e.browseArchive);
        return;
    }
    const float side = 46;
    const int perRow = std::max(1, int((ImGui::GetContentRegionAvail().x + 4) / (side + 10)));
    for (std::size_t r = 0; r < count; ++r) {
        if (r % std::size_t(perRow)) ImGui::SameLine();
        ImGui::PushID(static_cast<int>(r));
        const auto& t = textures_.get(e.browseArchive, int(r));
        const bool current = e.brush == mesh::FaceTexture{e.browseArchive, int(r)};
        if (current) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_HeaderActive));
        if (ImGui::ImageButton("t", ImTextureRef(static_cast<ImTextureID>(t.id)), {side, side})) {
            modelApplyTexture({e.browseArchive, int(r)});
        }
        if (current) ImGui::PopStyleColor();
        ImGui::SetItemTooltip("%d/%zu  %dx%d texels", e.browseArchive, r, t.width, t.height);
        ImGui::PopID();
    }
    ImGui::TextDisabled("Click one to lay it on the selected faces (and on new faces).");
}

void Viewer::drawModelUv() {
    auto& e = *modelEditor_;
    if (e.pick != ModelEditor::Pick::Faces || e.faces.empty()) {
        ImGui::TextDisabled("Pick faces to align their textures.");
        return;
    }
    const auto& m = e.edit.mesh();
    auto shown = m.faces[*e.faces.begin()].uv;
    const double speed = 0.01, texelSpeed = 0.5, angleSpeed = 1;
    ImGui::SetNextItemWidth(160);
    double scale[2] = {shown.scaleU, shown.scaleV};
    bool changed = ImGui::DragScalarN("units per texel", ImGuiDataType_Double, scale, 2, float(speed), nullptr, nullptr, "%.3f");
    alignField(e.edit, e.faces, changed, [&](mesh::UvAlign& a) {
        a.scaleU = std::max(0.01, scale[0]);
        a.scaleV = std::max(0.01, scale[1]);
    });
    ImGui::SetItemTooltip("Classic pieces: 2 (a 64-texel picture over a 128-unit cell)");
    ImGui::SetNextItemWidth(160);
    double offset[2] = {shown.offsetU, shown.offsetV};
    changed = ImGui::DragScalarN("offset (texels)", ImGuiDataType_Double, offset, 2, float(texelSpeed), nullptr, nullptr, "%.1f");
    alignField(e.edit, e.faces, changed, [&](mesh::UvAlign& a) {
        a.offsetU = offset[0];
        a.offsetV = offset[1];
    });
    ImGui::SetNextItemWidth(160);
    double angle = shown.angle;
    changed = ImGui::DragScalar("turn (degrees)", ImGuiDataType_Double, &angle, float(angleSpeed), nullptr, nullptr, "%.1f");
    alignField(e.edit, e.faces, changed, [&](mesh::UvAlign& a) { a.angle = angle; });
    if (ImGui::Button("Fit")) {
        auto& changing = e.edit.change();
        for (const auto f : e.faces) {
            const auto& t = textures_.get(changing.faces[f].texture.archive, changing.faces[f].texture.record);
            mesh::fitTexture(changing, f, t.width, t.height);
        }
        modelChanged();
    }
    ImGui::SetItemTooltip("One copy of the texture across each face");
    ImGui::SameLine();
    if (ImGui::Button("Classic")) {
        auto& changing = e.edit.change();
        for (const auto f : e.faces) changing.faces[f].uv = {};
        modelChanged();
    }
    ImGui::SetItemTooltip("The classic density, lined up on the world (neighbouring faces meet seamlessly)");
    ImGui::SameLine();
    if (ImGui::Button("Turn 90##texture")) {
        auto& changing = e.edit.change();
        for (const auto f : e.faces) changing.faces[f].uv.angle = std::fmod(changing.faces[f].uv.angle + 90, 360);
        modelChanged();
    }
}

void Viewer::drawModelEditorPanel() {
    // First open: where the Edit panel is (the model editor takes its place), else beside the Outliner.
    for (const char* beside : {"Edit", "Outliner"}) {
        if (const auto* w = ImGui::FindWindowByName(beside); w && w->DockId) {
            ImGui::SetNextWindowDockID(w->DockId, ImGuiCond_FirstUseEver);
            break;
        }
    }
    ImGui::SetNextWindowSize({360, 640}, ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Model")) {
        ImGui::End();
        return;
    }
    auto& e = *modelEditor_;
    auto* model = editedModel();
    if (!model) {
        ImGui::TextDisabled("The model is gone from the project.");
        if (ImGui::Button("Close")) closeModelEditor();
        ImGui::End();
        return;
    }
    const auto& m = e.edit.mesh();

    // What it is.
    char label[64];
    std::snprintf(label, sizeof label, "%s", model->label.c_str());
    ImGui::SetNextItemWidth(-90);
    if (ImGui::InputText("##label", label, sizeof label)) {
        model->label = label;
        e.drawn.title = label;
        markProjectChanged();
    }
    ImGui::SameLine();
    if (ImGui::Button("Close", {-1, 0})) {
        closeModelEditor();
        ImGui::End();
        return;
    }
    ImGui::SetItemTooltip("Stop editing this model (it stays in the project: save it with the project).\n"
                          "To look at the dungeon meanwhile, pick the Viewports tab: the model waits in its own.");
    ImGui::TextDisabled("model %u   %zu points, %zu faces", model->id, m.points.size(), m.faces.size());
    static constexpr std::array<std::pair<const char*, const char*>, 3> kKinds = {
        {{"C00", "Architecture (corridor or room piece)"}, {"DOR", "Action door (swings open, can lock)"}, {"XBF", "Prop"}}};
    const char* kindLabel = "Other";
    for (const auto& [code, text] : kKinds) {
        if (model->description == code) kindLabel = text;
    }
    ImGui::SetNextItemWidth(-1);
    if (ImGui::BeginCombo("##kind", std::format("{}  ({})", kindLabel, model->description).c_str())) {
        for (const auto& [code, text] : kKinds) {
            if (ImGui::Selectable(text, model->description == code)) {
                model->description = code;
                markProjectChanged();
                syncCustomModel(model->id);
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SetItemTooltip("Its description in a block's model list, which the game reads: DOR makes an action door.\n"
                          "Pieces snap to the classic grid. Copies already placed keep the description they had.");

    ImGui::BeginDisabled(!e.edit.canUndo());
    if (ImGui::Button("Undo") && e.edit.undo()) modelChanged();
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!e.edit.canRedo());
    if (ImGui::Button("Redo") && e.edit.redo()) modelChanged();
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Frame")) frameModel();

    // Picking and snapping.
    ImGui::SeparatorText("Pick");
    int pick = static_cast<int>(e.pick);
    if (ImGui::RadioButton("Points (1)", &pick, 0)) e.pick = ModelEditor::Pick::Points;
    ImGui::SameLine();
    if (ImGui::RadioButton("Edges (2)", &pick, 1)) e.pick = ModelEditor::Pick::Edges;
    ImGui::SameLine();
    if (ImGui::RadioButton("Faces (3)", &pick, 2)) e.pick = ModelEditor::Pick::Faces;
    ImGui::SameLine();
    ImGui::TextDisabled("%zu selected", e.selectedCount());
    int tool = static_cast<int>(e.tool);
    if (ImGui::RadioButton("Move", &tool, 0)) e.tool = ModelEditor::Tool::Move;
    ImGui::SameLine();
    if (ImGui::RadioButton("Turn", &tool, 1)) e.tool = ModelEditor::Tool::Turn;
    ImGui::SameLine();
    if (ImGui::RadioButton("Scale", &tool, 2)) e.tool = ModelEditor::Tool::Scale;
    ImGui::SameLine();
    ImGui::TextDisabled("handles (R)");
    ImGui::Checkbox("Along normals (Shift+N)", &e.alongNormals);
    ImGui::SetItemTooltip("Moving (the arrow, a drag, G) and extruding (E) go along the normals of the selected faces,\n"
                          "whatever angle they are at. Faces at an angle stay joined and each moves the full distance.\n"
                          "The distance snaps to the grid. Shift: no snapping.");
    ImGui::SetItemTooltip("Arrows move along an axis; rings turn about one (15-degree steps); square-ended arms scale\n"
                          "along one, the middle square all three (sizes land on the grid). Shift: no snapping.\n"
                          "G moves with the pointer, G again slides along the edges.");
    {
        int index = 0;
        for (std::size_t i = 0; i < kGridSteps.size(); ++i) {
            if (kGridSteps[i] == e.grid) index = int(i);
        }
        ImGui::SetNextItemWidth(80);
        const auto preview = std::format("{:g}", e.grid);
        if (ImGui::BeginCombo("grid", preview.c_str())) {
            for (std::size_t i = 0; i < kGridSteps.size(); ++i) {
                if (ImGui::Selectable(std::format("{:g}", kGridSteps[i]).c_str(), int(i) == index)) e.grid = kGridSteps[i];
            }
            ImGui::EndCombo();
        }
        ImGui::SetItemTooltip("Block units; [ and ] make it smaller and bigger.\nClassic pieces sit on 64 across and 128 up; a cell is 128 (3.2 m).");
    }
    ImGui::SameLine();
    ImGui::Checkbox("snap to points", &e.snapPoints);
    ImGui::SetItemTooltip("A dragged corner lands on any point near the pointer: the model's own or the reference's");
    ImGui::SameLine();
    ImGui::Checkbox("weld", &e.weldSnapped);
    ImGui::SetItemTooltip("A corner put down on another point becomes one with it (else two points sit in one place,\n"
                          "and faces through both can draw wrongly)");
    ImGui::SetNextItemWidth(80);
    const bool entered = ImGui::InputTextWithHint("##reference", "model id", e.referenceText, sizeof e.referenceText,
                                                  ImGuiInputTextFlags_CharsDecimal | ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    if (ImGui::Button("Show reference") || entered) {
        const auto id = static_cast<std::uint32_t>(std::strtoul(e.referenceText, nullptr, 10));
        if (catalog_.model(id) && id != e.modelId) e.reference = id;
        else status_ = std::format("No model {} to show.", e.referenceText);
    }
    ImGui::SetItemTooltip("A classic model drawn in blue lines at the origin, to build to its size and snap to.\n"
                          "Its shape is only a guide: nothing of it is copied into the model.");
    if (e.reference) {
        ImGui::SameLine();
        if (ImGui::SmallButton("x")) e.reference.reset();
        ImGui::SetItemTooltip("Hide the reference (%u)", *e.reference);
    }

    if (ImGui::CollapsingHeader("Shapes", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::SetNextItemWidth(-60);
        ImGui::DragScalarN("from", ImGuiDataType_Double, &e.shape.min.x, 3, float(e.grid / 4), nullptr, nullptr, "%.0f");
        ImGui::SetNextItemWidth(-60);
        ImGui::DragScalarN("to", ImGuiDataType_Double, &e.shape.max.x, 3, float(e.grid / 4), nullptr, nullptr, "%.0f");
        e.shape.min = mesh::snapTo(e.shape.min, e.grid);
        e.shape.max = mesh::snapTo(e.shape.max, e.grid);
        ImGui::BeginDisabled(modelMovingPoints().empty());
        if (ImGui::SmallButton("from the selection")) {
            const auto moving = modelMovingPoints();
            catalog::Box b{m.points[*moving.begin()], m.points[*moving.begin()]};
            for (const auto p : moving) {
                b.min = {std::min(b.min.x, m.points[p].x), std::min(b.min.y, m.points[p].y), std::min(b.min.z, m.points[p].z)};
                b.max = {std::max(b.max.x, m.points[p].x), std::max(b.max.y, m.points[p].y), std::max(b.max.z, m.points[p].z)};
            }
            e.shape = b;
        }
        ImGui::EndDisabled();
        ImGui::Checkbox("seen from inside (a room)", &e.inward);
        static constexpr const char* kSideNames[] = {"W", "E", "bottom", "top", "S", "N"};
        for (std::size_t s = 0; s < 6; ++s) {
            if (s) ImGui::SameLine();
            ImGui::Checkbox(kSideNames[s], &e.sides[s]);
        }
        if (ImGui::Button("Add box")) modelAddBox();
        ImGui::SetItemTooltip("Leave sides off for a corridor's open ends, or a floor alone");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(70);
        ImGui::InputInt("steps", &e.steps);
        e.steps = std::clamp(e.steps, 1, 64);
        ImGui::SameLine();
        if (ImGui::Button("Add stairs")) modelAddStairs();
        ImGui::SetItemTooltip("Treads and risers climbing north across the box");
    }

    ImGui::TextDisabled("Right-click a face or point in a view for what you can do with it.");
    if (ImGui::CollapsingHeader("Edit")) {
        ImGui::SetNextItemWidth(90);
        ImGui::InputDouble("##extrude", &e.extrudeBy, 0, 0, "%.0f");
        ImGui::SameLine();
        if (ImGui::Button("Push out (X)")) modelExtrude(-e.extrudeBy);
        ImGui::SetItemTooltip("Faces move back behind themselves, joined to where they were: a room's wall becomes a corridor");
        ImGui::SameLine();
        if (ImGui::Button("Pull in (Shift+X)")) modelExtrude(e.extrudeBy);
        ImGui::SetItemTooltip("Faces move out the way they face: grows a solid thing");
        if (ImGui::Button("Flip (N)")) modelFlip();
        ImGui::SetItemTooltip("Faces are seen from one side: turn the selected round");
        ImGui::SameLine();
        if (ImGui::Button("Join (J)")) modelJoin();
        ImGui::SetItemTooltip("Two points across a face cut it in two; three or more make a face; two edges of a face\n"
                              "cut it between their middles");
        ImGui::SameLine();
        if (ImGui::Button("Split edge")) {
            if (e.pick == ModelEditor::Pick::Edges) modelSplitEdges();
            else modelSplitEdge();
        }
        ImGui::SetItemTooltip("A point in the middle of each selected edge (or of the edge between two points)");
        ImGui::SameLine();
        if (ImGui::Button("Weld")) modelWeld();
        ImGui::BeginDisabled(e.pick != ModelEditor::Pick::Edges || e.edges.empty());
        if (ImGui::Button("Grow out")) modelExtendEdges(0);
        ImGui::SetItemTooltip("A new face from each edge, out across the face beside it (more floor, more wall), by\n"
                              "the distance above");
        ImGui::SameLine();
        if (ImGui::Button("Grow up")) modelExtendEdges(1);
        ImGui::SameLine();
        if (ImGui::Button("Grow down")) modelExtendEdges(-1);
        ImGui::EndDisabled();
        if (ImGui::Button("Copy (Ctrl+D)")) modelDuplicate();
        ImGui::SameLine();
        if (ImGui::Button("Delete (Del)")) modelDelete();
        ImGui::SameLine();
        if (ImGui::Button("Merge close points")) {
            const auto n = mesh::mergeByDistance(e.edit.change(), e.mergeDistance);
            e.points.clear();
            e.faces.clear();
            modelChanged();
            status_ = std::format("Merged {} point{}.", n, n == 1 ? "" : "s");
        }
        ImGui::SetItemTooltip("Points closer than %.1f units become one", e.mergeDistance);
        ImGui::SetNextItemWidth(-60);
        ImGui::InputScalarN("##moveby", ImGuiDataType_Double, e.moveBy, 3, nullptr, nullptr, "%.0f");
        ImGui::SameLine();
        if (ImGui::Button("Move", {-1, 0})) modelMove({e.moveBy[0], e.moveBy[1], e.moveBy[2]});
        ImGui::SetNextItemWidth(70);
        ImGui::InputDouble("##turnby", &e.turnBy, 0, 0, "%.1f");
        ImGui::SetItemTooltip("Degrees; about the selection's middle");
        for (int a = 0; a < 3; ++a) {
            ImGui::SameLine();
            if (ImGui::Button(std::format("Turn {}##turnabout", "XYZ"[a]).c_str())) modelTurnAbout(a, e.turnBy);
        }
        ImGui::SetNextItemWidth(-60);
        ImGui::InputScalarN("##scaleby", ImGuiDataType_Double, e.scaleBy, 3, nullptr, nullptr, "%.3f");
        ImGui::SetItemTooltip("X, Y, Z factors about the selection's middle (0.5 halves, 2 doubles; -1 mirrors)");
        ImGui::SameLine();
        if (ImGui::Button("Scale", {-1, 0})) modelScale({e.scaleBy[0], e.scaleBy[1], e.scaleBy[2]});
        if (ImGui::Button("Mirror X")) modelMirror(0);
        ImGui::SameLine();
        if (ImGui::Button("Mirror Y")) modelMirror(1);
        ImGui::SetItemTooltip("Upside down about the selection's middle (a floor piece into a ceiling piece)");
        ImGui::SameLine();
        if (ImGui::Button("Mirror Z")) modelMirror(2);
    }

    e.showCuts = ImGui::CollapsingHeader("Cuts");
    if (e.showCuts) {
        ImGui::TextDisabled("A doorway, window or trapdoor out of the selected faces (drawn on them in cyan).");
        double size[2] = {e.opening.width, e.opening.height};
        ImGui::SetNextItemWidth(-110);
        ImGui::DragScalarN("width, height", ImGuiDataType_Double, size, 2, float(e.grid / 4), nullptr, nullptr, "%.0f");
        e.opening.width = std::max(1.0, mesh::snapTo(size[0], e.grid));
        e.opening.height = std::max(1.0, mesh::snapTo(size[1], e.grid));
        double place[2] = {e.opening.bottom, e.opening.across};
        ImGui::SetNextItemWidth(-110);
        ImGui::DragScalarN("up, across", ImGuiDataType_Double, place, 2, float(e.grid / 4), nullptr, nullptr, "%.0f");
        ImGui::SetItemTooltip("Up from the face's bottom edge, and across from its middle (to the right, seen from the front)");
        e.opening.bottom = std::max(0.0, mesh::snapTo(place[0], e.grid));
        e.opening.across = mesh::snapTo(place[1], e.grid);
        ImGui::SetNextItemWidth(-110);
        ImGui::DragScalar("reveal depth", ImGuiDataType_Double, &e.opening.depth, float(e.grid / 4), nullptr, nullptr, "%.0f");
        e.opening.depth = std::max(0.0, mesh::snapTo(e.opening.depth, e.grid));
        ImGui::SetItemTooltip("0: just a hole. More: the hole is lined that deep behind the face (sides, sill, lintel),\n"
                              "so a doorway through a thick wall, or between a room and a corridor, is closed in.");
        if (ImGui::SmallButton("Door 64x96")) e.opening = {.width = 64, .height = 96, .bottom = 0, .across = 0, .depth = e.opening.depth};
        ImGui::SameLine();
        if (ImGui::SmallButton("Arch 128x128")) e.opening = {.width = 128, .height = 128, .bottom = 0, .across = 0, .depth = e.opening.depth};
        ImGui::SameLine();
        if (ImGui::SmallButton("Window 32x32")) e.opening = {.width = 32, .height = 32, .bottom = 48, .across = 0, .depth = e.opening.depth};
        ImGui::BeginDisabled(e.pick != ModelEditor::Pick::Faces || e.faces.empty());
        if (ImGui::Button("Cut opening", {-1, 0})) modelCutOpening();
        ImGui::EndDisabled();
        ImGui::Separator();
        static constexpr const char* kAxisNames[] = {"X =", "Y =", "Z ="};
        ImGui::SetNextItemWidth(60);
        ImGui::Combo("##sliceaxis", &e.sliceAxis, kAxisNames, 3);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(80);
        ImGui::InputDouble("##sliceat", &e.sliceAt, 0, 0, "%.0f");
        ImGui::SameLine();
        ImGui::BeginDisabled(e.faces.empty() && e.points.empty());
        if (ImGui::Button("here")) {
            if (auto c = modelSelectionCentre()) e.sliceAt = mesh::snapTo(component(*c, e.sliceAxis), e.grid);
        }
        ImGui::EndDisabled();
        ImGui::SetItemTooltip("The selection's middle, on the grid");
        ImGui::SameLine();
        ImGui::BeginDisabled(e.pick != ModelEditor::Pick::Faces || e.faces.empty());
        if (ImGui::Button("Slice", {-1, 0})) modelSlice();
        ImGui::EndDisabled();
        ImGui::SetItemTooltip("Cut the selected faces in two along that line: Y = 64 splits walls into a lower and an\n"
                              "upper part, to texture or push out separately");
    }

    if (ImGui::CollapsingHeader("Texture", ImGuiTreeNodeFlags_DefaultOpen)) drawModelTextures();
    if (ImGui::CollapsingHeader("Texture alignment", ImGuiTreeNodeFlags_DefaultOpen)) drawModelUv();

    const auto problems = mesh::checkMesh(m);
    if (!problems.empty()) {
        ImGui::SeparatorText("Problems");
        for (const auto& p : problems) ImGui::TextColored({1.0f, 0.7f, 0.3f, 1.0f}, "%s", p.c_str());
    }
    ImGui::Separator();
    ImGui::TextWrapped("%s", status_.c_str());
    ImGui::TextDisabled("Drag a selected point or face to move it (3D: Shift for up and down), the arrows along an "
                        "axis; a drag elsewhere draws a box. Right-click cancels a move.");
    ImGui::End();
}

// ---- a scripted run ----

Result<std::string> Viewer::checkModel(const std::filesystem::path& modDir) {
    std::string log;
    // A project of its own in the check's folder, so the export holds only what the check makes.
    if (auto r = setProjectFile(modDir / "check.project.json"); !r) return std::unexpected(r.error());
    newCustomModel();
    if (!modelEditor_) return fail("no model editor: " + status_);
    auto& e = *modelEditor_;
    const auto id = e.modelId;
    log += std::format("new model {}: {} faces\n", id, e.edit.mesh().faces.size());

    // Push the east wall out two cells: a corridor off the room.
    const auto& start = e.edit.mesh();
    std::optional<std::uint32_t> east;
    for (std::uint32_t f = 0; f < start.faces.size(); ++f) {
        if (mesh::length(start.centre(start.faces[f]) - Vec3{64, 64, 0}) < 1e-3) east = f;
    }
    if (!east) return fail("the new model has no east wall");
    e.pick = ModelEditor::Pick::Faces;
    e.faces = {*east};
    modelExtrude(-256);
    if (e.edit.mesh().faces.size() != 10) return fail(std::format("extrude made {} faces", e.edit.mesh().faces.size()));
    log += std::format("pushed the east wall out: {} faces, the wall at x {:.0f}\n", e.edit.mesh().faces.size(),
                       e.edit.mesh().centre(e.edit.mesh().faces[*east]).x);

    // A drag of the wall a cell further, as the pointer would: in the Top view, from the wall to 128 units east.
    views_[1].min = {0, 0};
    views_[1].size = {800, 600};
    frameModel();
    const auto wall = toScreen(1, e.edit.mesh().centre(e.edit.mesh().faces[*east]));
    const auto further = toScreen(1, e.edit.mesh().centre(e.edit.mesh().faces[*east]) + Vec3{128, 0, 0});
    if (!wall || !further) return fail("the Top view does not show the wall");
    e.drag = {};
    e.drag.kind = ModelEditor::Drag::Kind::Plane;
    e.drag.view = 1;
    e.drag.press = *wall;
    e.drag.anchor = e.edit.mesh().points[e.edit.mesh().faces[*east].points[0]];
    e.drag.planeNormal = cameraOf(1).forward();
    const auto pressRay = cameraOf(1).rayThrough(wall->x, wall->y, 800, 600);
    e.drag.grabHit = pressRay.origin + pressRay.direction *
                                           (mesh::dot(e.drag.anchor - pressRay.origin, e.drag.planeNormal) /
                                            mesh::dot(pressRay.direction, e.drag.planeNormal));
    for (const auto p : modelMovingPoints()) e.drag.start[p] = e.edit.mesh().points[p];
    e.edit.beginGesture();
    {
        // One frame of the drag, by hand (no UI frame runs here).
        const auto ray = cameraOf(1).rayThrough(further->x, further->y, 800, 600);
        const double t = mesh::dot(e.drag.grabHit - ray.origin, e.drag.planeNormal) / mesh::dot(ray.direction, e.drag.planeNormal);
        const Vec3 wanted = e.drag.anchor + ((ray.origin + ray.direction * t) - e.drag.grabHit);
        const Vec3 target = snapModelPoint(1, {wanted.x, e.drag.anchor.y, wanted.z}, *further, {true, false, true});
        auto& m = e.edit.change();
        for (const auto& [p, s] : e.drag.start) m.points[p] = s + (target - e.drag.anchor);
    }
    e.edit.endGesture();
    e.drag = {};
    modelChanged();
    log += std::format("dragged it a cell east in the Top view: the wall at x {:.0f}\n",
                       e.edit.mesh().centre(e.edit.mesh().faces[*east]).x);

    // Extrude (E) in each pick kind, then cancel: the mesh and the selection are as they were, with no undo step.
    {
        const auto shape = e.edit.mesh();
        const auto pickBefore = e.pick;
        const auto wallMiddle = toScreen(1, shape.centre(shape.faces[*east]));
        if (!wallMiddle) return fail("the Top view does not show the wall");
        const mesh::FaceSet facesBefore = e.faces;
        const auto wallEdges = [&] {
            mesh::EdgeSet out;
            const auto& p = shape.faces[*east].points;
            for (std::size_t i = 0; i < p.size(); ++i) out.insert(mesh::edgeOf(p[i], p[(i + 1) % p.size()]));
            return out;
        }();
        for (const auto pick : {ModelEditor::Pick::Faces, ModelEditor::Pick::Edges, ModelEditor::Pick::Points}) {
            e.pick = pick;
            e.faces = {*east};
            e.edges = {*wallEdges.begin()};
            e.points = mesh::pointsOf(shape, mesh::FaceSet{*east});
            const auto picked = std::tuple{e.points, e.edges, e.faces};
            modelExtrudeAndMove(1, *wallMiddle);
            if (!e.drag.extruding) return fail("extrude did not start: " + status_);
            if (e.edit.mesh().faces.size() <= shape.faces.size()) return fail("extrude made no faces");
            cancelModelDrag();
            if (e.edit.mesh().faces.size() != shape.faces.size() || e.edit.mesh().points.size() != shape.points.size()) {
                return fail("cancelling an extrude left something behind");
            }
            if (std::tuple{e.points, e.edges, e.faces} != picked) return fail("cancelling an extrude lost the selection");
        }
        // Along the normals: the same, with the pointer led 64 units out through the drag itself.
        e.alongNormals = true;
        const auto pointerBefore = ImGui::GetIO().MousePos;
        for (const auto pick : {ModelEditor::Pick::Faces, ModelEditor::Pick::Edges, ModelEditor::Pick::Points}) {
            e.pick = pick;
            e.faces = {*east};
            e.edges = {*wallEdges.begin()};
            e.points = mesh::pointsOf(shape, mesh::FaceSet{*east});
            modelExtrudeAndMove(1, *wallMiddle);
            if (!e.drag.extruding || e.drag.kind != ModelEditor::Drag::Kind::Normal) return fail("extrude along normals did not start: " + status_);
            const auto from = toScreen(1, e.drag.anchor), to = toScreen(1, e.drag.anchor + e.drag.normalLead * 64.0);
            if (!from || !to) return fail("the Top view does not show the normal");
            ImGui::GetIO().MousePos = {wallMiddle->x + to->x - from->x, wallMiddle->y + to->y - from->y};
            continueModelDrag();
            if (e.drag.kind != ModelEditor::Drag::Kind::Normal) return fail("the drag along normals ended by itself");
            for (const auto& [p, start] : e.drag.start) {
                if (mesh::length(e.edit.mesh().points[p] - (start + e.drag.directions.at(p) * 64.0)) > 1e-6) {
                    return fail("a point did not go 64 units along its normal");
                }
            }
            cancelModelDrag();
            if (e.edit.mesh().points.size() != shape.points.size() || e.edit.mesh().faces.size() != shape.faces.size()) {
                return fail("cancelling an extrude along normals left something behind");
            }
        }
        // Moving (G) puts everything back on a cancel too, and the wall's normal is its way.
        e.pick = ModelEditor::Pick::Faces;
        e.faces = {*east};
        if (!startModelGrab(1, *wallMiddle, true) || e.drag.kind != ModelEditor::Drag::Kind::Normal) return fail("G along normals did not start: " + status_);
        cancelModelDrag();
        ImGui::GetIO().MousePos = pointerBefore;
        e.alongNormals = false;
        log += "extrude and move along normals: followed the pointer 64 units and cancelled cleanly in all three picks\n";
        e.pick = pickBefore;
        e.faces = facesBefore;
        e.points.clear();
        e.edges.clear();
        log += "extrude (E) started and cancelled cleanly for faces, edges and points\n";
    }

    modelApplyTexture({123, 1});
    {
        auto& m = e.edit.change();
        const auto& t = textures_.get(123, 1);
        mesh::fitTexture(m, *east, t.width, t.height);
    }
    modelChanged();
    log += std::format("retextured the end wall 123/1 and fitted it ({}x{} texels)\n", textures_.get(123, 1).width,
                       textures_.get(123, 1).height);

    if (!e.edit.undo() || !e.edit.undo()) return fail("undo failed");
    modelChanged();
    const bool undone = e.edit.mesh().faces[*east].texture == mesh::FaceTexture{120, 0};
    if (!e.edit.redo() || !e.edit.redo()) return fail("redo failed");
    modelChanged();
    log += std::format("undo twice: texture back to 120/0 {}; redo twice\n", undone ? "yes" : "NO");
    if (!undone) return fail("undo did not put the texture back");

    // A door through the west wall, lined 32 deep, as the Cuts section makes it.
    {
        const auto& now = e.edit.mesh();
        std::optional<std::uint32_t> west;
        for (std::uint32_t f = 0; f < now.faces.size(); ++f) {
            if (mesh::length(now.centre(now.faces[f]) - Vec3{-64, 64, 0}) < 1e-3) west = f;
        }
        if (!west) return fail("the model has no west wall");
        const auto before = now.faces.size();
        e.pick = ModelEditor::Pick::Faces;
        e.faces = {*west};
        e.opening = {.width = 64, .height = 96, .bottom = 0, .across = 0, .depth = 32};
        modelCutOpening();
        const auto problems = mesh::checkMesh(e.edit.mesh());
        log += std::format("cut a 64 x 96 door in the west wall, lined 32 deep: {} faces (was {}), {} lining selected, "
                           "{} problems\n", e.edit.mesh().faces.size(), before, e.faces.size(), problems.size());
        if (e.faces.size() != 4 || !problems.empty()) return fail("the door did not cut cleanly");
    }

    // Edges: the south wall's top and bottom edges, joined across the wall, then the cut removed again.
    {
        const auto& now = e.edit.mesh();
        std::optional<std::uint32_t> south;
        for (std::uint32_t f = 0; f < now.faces.size(); ++f) {
            if (mesh::length(now.centre(now.faces[f]) - Vec3{0, 64, -128}) < 1e-3) south = f;
        }
        if (!south) return fail("the model has no south wall");
        e.pick = ModelEditor::Pick::Edges;
        e.edges.clear();
        const auto& p = now.faces[*south].points;
        for (std::size_t i = 0; i < p.size(); ++i) {
            const auto edge = mesh::edgeOf(p[i], p[(i + 1) % p.size()]);
            if (std::abs(now.points[edge.first].y - now.points[edge.second].y) < 1e-6) e.edges.insert(edge);   // level ones
        }
        const auto before = now.faces.size();
        modelJoin();
        const auto after = e.edit.mesh().faces.size();
        // The cut: the new edge between the two halves (both faces seen from inside, lying flat together).
        const auto& cut = e.edit.mesh();
        e.edges.clear();
        for (const auto& edge : mesh::edgesOf(cut)) {
            const auto beside = mesh::facesWithEdge(cut, edge);
            const Vec3 mid = (cut.points[edge.first] + cut.points[edge.second]) * 0.5;
            if (beside.size() == 2 && std::abs(mid.z + 128) < 1e-6 && std::abs(mid.x) < 1e-6) e.edges.insert(edge);
        }
        modelDelete();
        log += std::format("edges: joined the south wall's top and bottom edges ({} faces to {}), removed the cut ({} again)\n",
                           before, after, e.edit.mesh().faces.size());
        if (after != before + 1 || e.edit.mesh().faces.size() != before) return fail("the edge join did not undo cleanly");
        e.pick = ModelEditor::Pick::Faces;
    }

    // Scaling and turning the whole model, each undone; a slide started on the end wall and cancelled.
    {
        const auto all = e.edit.mesh().faces.size();
        e.pick = ModelEditor::Pick::Faces;
        e.faces.clear();
        for (std::uint32_t f = 0; f < all; ++f) e.faces.insert(f);
        const auto size = e.edit.mesh().bounds().size();
        modelScale({2, 2, 2});
        const auto doubled = e.edit.mesh().bounds().size();
        if (!e.edit.undo()) return fail("undo after scaling failed");
        modelTurnAbout(0, 90);
        const auto tipped = e.edit.mesh().bounds().size();
        if (!e.edit.undo()) return fail("undo after turning failed");
        modelChanged();
        log += std::format("scaled by 2: {:.0f} x {:.0f} x {:.0f} to {:.0f} x {:.0f} x {:.0f}; turned 90 about X: {:.0f} x {:.0f} x {:.0f}; "
                           "both undone\n", size.x, size.y, size.z, doubled.x, doubled.y, doubled.z, tipped.x, tipped.y, tipped.z);
        if (std::abs(doubled.x - 2 * size.x) > 1e-6 || std::abs(tipped.y - size.z) > 1e-6) return fail("scale or turn went wrong");
        e.faces = {*east};
        if (!startModelSlide(1, {400, 300})) return fail("the end wall has nothing to slide along");
        const auto along = e.drag.neighbours.size();
        e.edit.cancelGesture();
        e.drag = {};
        log += std::format("slide on the end wall: {} corners each with an edge to slide along\n", along);
    }

    const auto* info = catalog_.model(id);
    if (!info) return fail("the catalog does not know the model");
    const auto size = info->bounds.size();
    log += std::format("catalog: {} {:.0f} x {:.0f} x {:.0f}, {}\n", std::string(catalog::kindName(info->kind)), size.x,
                       size.y, size.z, models_.get(id) ? "drawn" : "NOT DRAWN");

    // Placed in a new block of the project, then exported.
    auto block = project::emptyBlock();
    block.modelRefs[0] = {std::to_string(id), id, "C00"};
    formats::RdbObject obj;
    obj.position = edit::kFirstNewPosition;
    obj.x = 1024;
    obj.z = 1024;
    obj.resource = formats::RdbModel{};
    block.roots[0].objects.push_back(obj);
    auto added = project_.addBlock('N', "model check", std::nullopt, std::move(block));
    if (!added) return std::unexpected(added.error());
    auto report = project::exportProject(project_, *world_, modDir);
    if (!report) return std::unexpected(report.error());
    for (const auto& i : report->issues) {
        if (i.severity == project::Issue::Severity::Error) return fail("export: " + i.where + ": " + i.text);
    }
    const auto path = modDir / project::modelFileName(id);
    std::ifstream in(path);
    if (!in) return fail("export wrote no " + path.string());
    const auto json = mesh::Json::parse(in, nullptr, false);
    if (json.is_discarded()) return fail(path.string() + " is not JSON");
    const auto drawn = mesh::toModelMesh(e.edit.mesh(), id);
    log += std::format("exported {}: {} vertices, {} triangles in {} textures (the editor draws {} triangles)\n",
                       project::modelFileName(id), json["positions"].size() / 3, json["indices"].size() / 3,
                       json["subMeshes"].size(), drawn.indices.size() / 3);
    frameModel();
    return log;
}

} // namespace de::app
