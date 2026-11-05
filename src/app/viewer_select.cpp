// Selecting (clicks, Shift/Ctrl, boxes), hiding (by kind, by hand, isolating a block), and the move and turn handles
// with the drags that run them.

#include "app/viewer.h"

#include "edit/placement.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <format>
#include <numbers>

namespace de::app {

namespace {

ImVec2 sub(ImVec2 a, ImVec2 b) { return {a.x - b.x, a.y - b.y}; }
ImVec2 add(ImVec2 a, ImVec2 b) { return {a.x + b.x, a.y + b.y}; }
float length(ImVec2 v) { return std::sqrt(v.x * v.x + v.y * v.y); }

/// Distance from p to the segment a-b, screen points.
float distanceToSegment(ImVec2 p, ImVec2 a, ImVec2 b) {
    const ImVec2 ab = sub(b, a), ap = sub(p, a);
    const float len2 = ab.x * ab.x + ab.y * ab.y;
    const float t = len2 > 0 ? std::clamp((ap.x * ab.x + ap.y * ab.y) / len2, 0.0f, 1.0f) : 0.0f;
    return length(sub(p, {a.x + ab.x * t, a.y + ab.y * t}));
}

std::int32_t snapped(double v, int step) {
    const double s = step > 1 ? step : 1;
    return static_cast<std::int32_t>(std::round(v / s) * s);
}

constexpr ImU32 kAxisColours[3] = {IM_COL32(230, 80, 70, 255), IM_COL32(110, 210, 90, 255), IM_COL32(80, 140, 255, 255)};
constexpr ImU32 kHot = IM_COL32(255, 230, 90, 255);
constexpr float kArrow = 70, kRing = 60, kGrabRadius = 6;

} // namespace

// ---- kinds and hiding ----

const char* Viewer::categoryName(Category c) {
    switch (c) {
        case Category::Architecture: return "Architecture";
        case Category::Door: return "Doors";
        case Category::Prop: return "Props";
        case Category::Scenery: return "Scenery flats";
        case Category::Enemy: return "Enemies";
        case Category::Treasure: return "Treasure";
        case Category::Quest: return "Quest markers";
        case Category::StartEnter: return "Start and enter";
        case Category::OtherMarker: return "Other markers";
        case Category::Light: return "Lights";
        case Category::Count: break;
    }
    return "?";
}

Viewer::Category Viewer::categoryOf(scene::Hit::Kind kind, std::size_t index) const {
    switch (kind) {
        case scene::Hit::Kind::Model: {
            const auto* info = catalog_.model(scene_.models[index].modelId);
            if (!info) return Category::Prop;
            switch (info->kind) {
                case catalog::ModelKind::Architecture: return Category::Architecture;
                case catalog::ModelKind::Door:
                case catalog::ModelKind::ExitDoor: return Category::Door;
                case catalog::ModelKind::Prop: return Category::Prop;
            }
            return Category::Prop;
        }
        case scene::Hit::Kind::Flat: {
            const auto& f = scene_.flats[index];
            if (!f.editorMarker) return Category::Scenery;
            switch (f.record) {
                case 15:
                case 16: return Category::Enemy;
                case 19: return Category::Treasure;
                case 11:
                case 18: return Category::Quest;
                case 8:
                case 10: return Category::StartEnter;
                default: return Category::OtherMarker;
            }
        }
        case scene::Hit::Kind::Light: return Category::Light;
    }
    return Category::Prop;
}

const scene::ObjectRef& Viewer::refOf(scene::Hit::Kind kind, std::size_t index) const {
    switch (kind) {
        case scene::Hit::Kind::Model: return scene_.models[index].ref;
        case scene::Hit::Kind::Flat: return scene_.flats[index].ref;
        case scene::Hit::Kind::Light: break;
    }
    return scene_.lights[index].ref;
}

bool Viewer::isHidden(scene::Hit::Kind kind, std::size_t index) const {
    const auto& ref = refOf(kind, index);
    if (isolated_ && ref.block != *isolated_) return true;
    if (!shown_[static_cast<std::size_t>(categoryOf(kind, index))]) return true;
    return hidden_.contains(ref);
}

void Viewer::updateFlags() {
    auto fill = [&](std::vector<std::uint8_t>& out, scene::Hit::Kind kind, std::size_t count) {
        out.assign(count, 0);
        for (std::size_t i = 0; i < count; ++i) {
            if (isHidden(kind, i)) out[i] |= render::InstanceFlags::kHidden;
            if (isSelected(refOf(kind, i))) out[i] |= render::InstanceFlags::kSelected;
        }
    };
    fill(flags_.models, scene::Hit::Kind::Model, scene_.models.size());
    fill(flags_.flats, scene::Hit::Kind::Flat, scene_.flats.size());
    fill(flags_.lights, scene::Hit::Kind::Light, scene_.lights.size());
}

void Viewer::hideSelected() {
    if (selection_.empty()) return;
    for (const auto& r : selection_) hidden_.insert(r);
    status_ = std::format("Hid {} (U shows everything again).", selection_.size());
    clearSelection();
}

void Viewer::showAll() {
    hidden_.clear();
    shown_.fill(true);
    isolated_.reset();
    status_ = "Showing everything.";
}

void Viewer::toggleIsolate() {
    if (isolated_) {
        isolated_.reset();
        status_ = "Showing every block.";
        return;
    }
    if (const auto p = primary()) isolated_ = p->block;
    else if (edit_) isolated_ = editBlock_;
    if (isolated_) status_ = "Showing " + scene_.blocks[*isolated_].name + " alone (I again shows the rest).";
}

void Viewer::drawShowMenu() {
    for (std::size_t i = 0; i < kCategories; ++i) {
        ImGui::Checkbox(categoryName(static_cast<Category>(i)), &shown_[i]);
    }
    ImGui::Separator();
    if (ImGui::MenuItem("Hide selected", "H", false, !selection_.empty())) hideSelected();
    if (ImGui::MenuItem(isolated_ ? "Show every block" : "Isolate the selection's block", "I")) toggleIsolate();
    if (ImGui::MenuItem("Show everything", "U")) showAll();
    if (!hidden_.empty()) ImGui::TextDisabled("%zu hidden by hand", hidden_.size());
    ImGui::Separator();
    ImGui::TextDisabled("Logic links");
    if (ImGui::RadioButton("All", linkMode_ == LinkMode::All)) linkMode_ = LinkMode::All;
    ImGui::SameLine();
    if (ImGui::RadioButton("Selection's", linkMode_ == LinkMode::Selection)) linkMode_ = LinkMode::Selection;
    ImGui::SameLine();
    if (ImGui::RadioButton("None", linkMode_ == LinkMode::Off)) linkMode_ = LinkMode::Off;
}

// ---- the selection ----

void Viewer::setSelection(std::vector<scene::ObjectRef> refs) {
    selection_.clear();
    selectedSet_.clear();
    for (const auto& r : refs) {
        if (selectedSet_.insert(r).second) selection_.push_back(r);
    }
}

void Viewer::clickSelect(std::optional<scene::Hit> hit, bool add, bool toggle) {
    if (!hit) {
        if (!add && !toggle) clearSelection();
        return;
    }
    const auto ref = refOf(hit->kind, hit->index);
    selectedDistance_ = hit->distance;
    auto refs = selection_;
    if (toggle && isSelected(ref)) {
        std::erase(refs, ref);
    } else if (toggle || add) {
        std::erase(refs, ref);
        refs.push_back(ref);   // the newest is the primary
    } else {
        refs = {ref};
    }
    setSelection(std::move(refs));
}

void Viewer::boxSelect(std::size_t view, ImVec2 a, ImVec2 b, bool add, bool toggle) {
    const ImVec2 lo{std::min(a.x, b.x), std::min(a.y, b.y)}, hi{std::max(a.x, b.x), std::max(a.y, b.y)};
    std::vector<scene::ObjectRef> inside;
    auto consider = [&](scene::Hit::Kind kind, std::size_t i, catalog::Vec3 at) {
        if (isHidden(kind, i)) return;
        const auto p = toScreen(view, at);
        if (p && p->x >= lo.x && p->x <= hi.x && p->y >= lo.y && p->y <= hi.y) inside.push_back(refOf(kind, i));
    };
    for (std::size_t i = 0; i < scene_.models.size(); ++i) {
        const auto& m = scene_.models[i];
        catalog::Vec3 at = m.transform.translation;
        if (const auto* info = catalog_.model(m.modelId)) {
            const auto box = catalog::transformBox(info->bounds, m.transform.rotation, m.transform.translation);
            at = {(box.min.x + box.max.x) / 2, (box.min.y + box.max.y) / 2, (box.min.z + box.max.z) / 2};
        }
        consider(scene::Hit::Kind::Model, i, at);
    }
    for (std::size_t i = 0; i < scene_.flats.size(); ++i) {
        const auto& f = scene_.flats[i];
        if (f.editorMarker ? !drawOptions_.markers : !drawOptions_.flats) continue;
        consider(scene::Hit::Kind::Flat, i, f.position);
    }
    for (std::size_t i = 0; drawOptions_.lights && i < scene_.lights.size(); ++i) {
        consider(scene::Hit::Kind::Light, i, scene_.lights[i].position);
    }
    auto refs = (add || toggle) ? selection_ : std::vector<scene::ObjectRef>{};
    for (const auto& r : inside) {
        const bool had = std::ranges::find(refs, r) != refs.end();
        if (toggle && had) std::erase(refs, r);
        else if (!had) refs.push_back(r);
    }
    setSelection(std::move(refs));
    status_ = std::format("{} selected.", selection_.size());
}

void Viewer::selectAll() {
    std::vector<scene::ObjectRef> refs;
    auto take = [&](scene::Hit::Kind kind, std::size_t count) {
        for (std::size_t i = 0; i < count; ++i) {
            const auto& r = refOf(kind, i);
            if (isHidden(kind, i) || (edit_ && r.block != editBlock_)) continue;
            refs.push_back(r);
        }
    };
    take(scene::Hit::Kind::Model, scene_.models.size());
    take(scene::Hit::Kind::Flat, scene_.flats.size());
    take(scene::Hit::Kind::Light, scene_.lights.size());
    setSelection(std::move(refs));
    status_ = std::format("{} selected{}.", selection_.size(), edit_ ? " in " + edit_->name() : "");
}

std::vector<edit::ObjectSlot> Viewer::selectedSlots() const {
    std::vector<edit::ObjectSlot> slots;
    for (const auto& r : selection_) {
        if (!editing(r)) continue;
        const edit::ObjectSlot s{r.root, r.object};
        if (edit_->object(s) && std::ranges::find(slots, s) == slots.end()) slots.push_back(s);
    }
    return slots;
}

std::optional<catalog::Vec3> Viewer::selectionCentre() const {
    const auto slots = selectedSlots();
    if (slots.empty()) return std::nullopt;
    const auto p = primary();
    const std::size_t block = p && editing(*p) ? p->block : editBlock_;
    double x = 0, y = 0, z = 0;
    for (const auto& s : slots) {
        const auto* o = edit_->object(s);
        x += o->x;
        y += o->y;
        z += o->z;
    }
    const double n = double(slots.size());
    return catalog::Vec3{x / n, -y / n, z / n} + scene_.blocks[block].origin;
}

// ---- moving and turning together ----

void Viewer::beginTransform() {
    moving_.clear();
    const auto slots = selectedSlots();
    if (slots.empty()) return;
    const auto p = primary();
    movingBlock_ = p && editing(*p) ? p->block : editBlock_;
    // The primary first: a grab moves it to the pointer and the rest keep their places around it.
    if (const auto ps = selectedSlot()) moving_.push_back({*ps, *edit_->object(*ps)});
    double x = 0, y = 0, z = 0;
    for (const auto& s : slots) {
        if (moving_.empty() || moving_.front().slot != s) moving_.push_back({s, *edit_->object(s)});
    }
    for (const auto& m : moving_) {
        x += m.start.x;
        y += m.start.y;
        z += m.start.z;
    }
    pivotX_ = x / double(moving_.size());
    pivotY_ = y / double(moving_.size());
    pivotZ_ = z / double(moving_.size());
    edit_->beginGesture();
}

void Viewer::applyTransform(std::array<std::int32_t, 3> offset, std::int32_t turn) {
    if (moving_.empty()) return;
    for (const auto& m : moving_) edit_->replace(m.slot, edit::transformed(m.start, offset, turn, pivotX_, pivotZ_));
    editChanged();
}

void Viewer::applyTurn(int axis, std::int32_t turn) {
    if (moving_.empty()) return;
    for (const auto& m : moving_) edit_->replace(m.slot, edit::turnedAbout(m.start, axis, turn, {pivotX_, pivotY_, pivotZ_}));
    editChanged();
}

void Viewer::turnSelectedAbout(int axis, std::int32_t turn) {
    if (!edit_ || selectedSlots().empty()) return;
    beginTransform();
    applyTurn(axis, turn);
    endTransform(true);
}

void Viewer::endTransform(bool keep) {
    if (moving_.empty()) return;
    moving_.clear();
    if (keep) edit_->endGesture();
    else edit_->cancelGesture();
    editChanged();
}

// ---- screen maths ----

std::optional<ImVec2> Viewer::toScreen(std::size_t view, catalog::Vec3 p) const {
    const auto& v = views_[view];
    if (v.size.x <= 0 || v.size.y <= 0) return std::nullopt;
    const auto cam = cameraOf(view);
    const auto m = render::multiply(cam.projection(double(v.size.x) / v.size.y), cam.view());
    const double x = m[0] * p.x + m[4] * p.y + m[8] * p.z + m[12];
    const double y = m[1] * p.x + m[5] * p.y + m[9] * p.z + m[13];
    const double w = m[3] * p.x + m[7] * p.y + m[11] * p.z + m[15];
    if (w <= 1e-6) return std::nullopt;
    return ImVec2{v.min.x + float((x / w + 1) / 2 * v.size.x), v.min.y + float((1 - y / w) / 2 * v.size.y)};
}

double Viewer::unitsPerPoint(std::size_t view, catalog::Vec3 at) const {
    const auto& v = views_[view];
    if (v.kind != ViewKind::Perspective) return 2 * v.halfHeight / std::max(1.0f, v.size.y);
    const auto f = camera_.forward();
    const double depth = std::max(1.0, (at.x - camera_.position.x) * f.x + (at.y - camera_.position.y) * f.y +
                                           (at.z - camera_.position.z) * f.z);
    return 2 * depth * std::tan(camera_.fovY / 2) / std::max(1.0f, v.size.y);
}

namespace {

/// The yaw (0 north, toward +X positive) of where a ray meets the level plane through `centre`.
/// The two directions across the plane a turn about `axis` happens in, `u` turning toward `v` for a positive turn
/// (catalog::axisRotation): X turns Y toward Z, Y turns Z toward X, Z turns X toward Y.
std::pair<catalog::Vec3, catalog::Vec3> ringBasis(int axis) {
    const catalog::Vec3 x{1, 0, 0}, y{0, 1, 0}, z{0, 0, 1};
    return axis == 0 ? std::pair{y, z} : axis == 1 ? std::pair{z, x} : std::pair{x, y};
}

/// Where the ray meets the ring's plane, as an angle round the centre (0 along u, growing toward v).
std::optional<double> angleOnRing(const scene::Ray& ray, catalog::Vec3 centre, int axis) {
    const auto [u, v] = ringBasis(axis);
    const catalog::Vec3 n{axis == 0 ? 1.0 : 0.0, axis == 1 ? 1.0 : 0.0, axis == 2 ? 1.0 : 0.0};
    const double denom = ray.direction.x * n.x + ray.direction.y * n.y + ray.direction.z * n.z;
    if (std::abs(denom) < 1e-3) return std::nullopt;
    const double t = ((centre.x - ray.origin.x) * n.x + (centre.y - ray.origin.y) * n.y + (centre.z - ray.origin.z) * n.z) / denom;
    const catalog::Vec3 h{ray.origin.x + ray.direction.x * t - centre.x, ray.origin.y + ray.direction.y * t - centre.y,
                          ray.origin.z + ray.direction.z * t - centre.z};
    const double a = h.x * u.x + h.y * u.y + h.z * u.z, b = h.x * v.x + h.y * v.y + h.z * v.z;
    if (a * a + b * b < 1e-6) return std::nullopt;
    return std::atan2(b, a);
}

} // namespace

// ---- the handles ----

bool Viewer::drawGizmo(std::size_t index) {
    if (!edit_ || placingAnything() || grabbing_) return false;
    const auto centre = selectionCentre();
    if (!centre) return false;
    const auto c = toScreen(index, *centre);
    if (!c) return false;
    auto& v = views_[index];
    auto* list = ImGui::GetWindowDrawList();
    list->PushClipRect(v.min, add(v.min, v.size), true);
    const auto& io = ImGui::GetIO();
    const ImVec2 mouse = io.MousePos;
    const bool idle = drag_.kind == Drag::Kind::None && ImGui::IsItemHovered();
    const double upp = unitsPerPoint(index, *centre);
    bool hot = false;

    if (gizmoMode_ == GizmoMode::Move) {
        int hotAxis = -1;
        ImVec2 ends[3]{};
        float lens[3]{};
        for (int a = 0; a < 3; ++a) {
            catalog::Vec3 dir{a == 0 ? 1.0 : 0.0, a == 1 ? 1.0 : 0.0, a == 2 ? 1.0 : 0.0};
            const double L = kArrow * upp;
            const auto e = toScreen(index, {centre->x + dir.x * L, centre->y + dir.y * L, centre->z + dir.z * L});
            if (!e) continue;
            ends[a] = *e;
            lens[a] = length(sub(*e, *c));
            if (lens[a] < 12) continue;   // pointing at the viewer
            if (idle && hotAxis < 0 && distanceToSegment(mouse, *c, *e) < kGrabRadius) hotAxis = a;
        }
        const bool hotPlane = idle && std::abs(mouse.x - c->x) <= 7 && std::abs(mouse.y - c->y) <= 7;
        if (hotPlane) hotAxis = -1;
        for (int a = 0; a < 3; ++a) {
            if (lens[a] < 12) continue;
            const bool active = (drag_.kind == Drag::Kind::Axis && drag_.view == index && drag_.axis == a);
            const ImU32 colour = (hotAxis == a || active) ? kHot : kAxisColours[a];
            list->AddLine(*c, ends[a], colour, hotAxis == a ? 3.0f : 2.0f);
            const ImVec2 d{(ends[a].x - c->x) / lens[a], (ends[a].y - c->y) / lens[a]}, n{-d.y, d.x};
            list->AddTriangleFilled(add(ends[a], {d.x * 10, d.y * 10}), add(ends[a], {n.x * 5, n.y * 5}),
                                    sub(ends[a], {n.x * 5, n.y * 5}), colour);
        }
        list->AddRectFilled(sub(*c, {6, 6}), add(*c, {6, 6}), hotPlane ? kHot : IM_COL32(230, 230, 230, 200));
        hot = hotPlane || hotAxis >= 0;
        if (hot && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            drag_ = {};
            drag_.view = index;
            drag_.press = mouse;
            drag_.centre = *centre;
            if (hotPlane) {
                drag_.kind = Drag::Kind::Plane;
                drag_.unitsPerPoint = upp;
            } else {
                drag_.kind = Drag::Kind::Axis;
                drag_.axis = hotAxis;
                const ImVec2 d = sub(ends[hotAxis], *c);
                drag_.screenAxis = {d.x / lens[hotAxis], d.y / lens[hotAxis]};
                drag_.unitsPerPoint = kArrow * upp / lens[hotAxis];
            }
            beginTransform();
        }
    } else {
        // Turning rings about the selection's middle: all three in 3D, and in each 2D view the one that faces it
        // (Top turns about Y, Front about Z, Side about X).
        std::vector<int> axes;
        switch (v.kind) {
            case ViewKind::Perspective: axes = {0, 1, 2}; break;
            case ViewKind::Top: axes = {1}; break;
            case ViewKind::Front: axes = {2}; break;
            case ViewKind::Side: axes = {0}; break;
        }
        constexpr int kSegments = 48;
        const double R = kRing * upp;
        int hotAxis = -1;
        float hotDistance = kGrabRadius;
        std::array<std::vector<ImVec2>, 3> rings;
        for (const int a : axes) {
            const auto [u, w] = ringBasis(a);
            for (int i = 0; i <= kSegments; ++i) {
                const double t = i * 2 * std::numbers::pi / kSegments, cu = std::cos(t) * R, sw = std::sin(t) * R;
                if (const auto p = toScreen(index, {centre->x + u.x * cu + w.x * sw, centre->y + u.y * cu + w.y * sw,
                                                    centre->z + u.z * cu + w.z * sw})) {
                    rings[static_cast<std::size_t>(a)].push_back(*p);
                }
            }
            const auto& ring = rings[static_cast<std::size_t>(a)];
            for (std::size_t i = 1; i < ring.size() && idle; ++i) {
                const float d = distanceToSegment(mouse, ring[i - 1], ring[i]);
                if (d < hotDistance) {
                    hotDistance = d;
                    hotAxis = a;
                }
            }
        }
        for (const int a : axes) {
            const auto& ring = rings[static_cast<std::size_t>(a)];
            const bool active = drag_.kind == Drag::Kind::Turn && drag_.view == index && drag_.axis == a;
            if (ring.size() > 1) {
                list->AddPolyline(ring.data(), int(ring.size()), (hotAxis == a || active) ? kHot : kAxisColours[a], 0,
                                  hotAxis == a ? 3.0f : 2.0f);
            }
        }
        list->AddCircleFilled(*c, 3, IM_COL32(230, 230, 230, 200));
        hot = hotAxis >= 0;
        if (hot && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            const auto ray = cameraOf(index).rayThrough(mouse.x - v.min.x, mouse.y - v.min.y, v.size.x, v.size.y);
            if (const auto angle = angleOnRing(ray, *centre, hotAxis)) {
                drag_ = {};
                drag_.kind = Drag::Kind::Turn;
                drag_.view = index;
                drag_.axis = hotAxis;
                drag_.press = mouse;
                drag_.centre = *centre;
                drag_.startYaw = *angle;
                drag_.lastYaw = *angle;
                beginTransform();
            }
        }
    }
    list->PopClipRect();
    return hot;
}

void Viewer::continueDrag() {
    if (drag_.kind == Drag::Kind::None) return;
    const auto& io = ImGui::GetIO();
    const auto& v = views_[drag_.view];
    const ImVec2 mouse = io.MousePos, moved = sub(mouse, drag_.press);
    const bool released = !ImGui::IsMouseDown(ImGuiMouseButton_Left);
    const bool transforming = drag_.kind == Drag::Kind::Axis || drag_.kind == Drag::Kind::Plane || drag_.kind == Drag::Kind::Turn;
    if (transforming && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {   // right-click cancels, as with G
        endTransform(false);
        drag_ = {};
        status_ = "Cancelled.";
        return;
    }

    switch (drag_.kind) {
        case Drag::Kind::Pending: {
            const double x = drag_.press.x - v.min.x, y = drag_.press.y - v.min.y;
            if (released) {
                pointerView_ = drag_.view;
                clickSelect(pickAt(x, y, selectOptions()), drag_.add, drag_.toggle);
                drag_ = {};
            } else if (length(moved) > 4) {
                // Dragging a selected object of the edited block moves the selection; anywhere else draws a box.
                pointerView_ = drag_.view;
                const auto hit = pickAt(x, y, selectOptions());
                const auto centre = selectionCentre();
                if (hit && centre && !drag_.add && !drag_.toggle && isSelected(refOf(hit->kind, hit->index)) &&
                    editing(refOf(hit->kind, hit->index))) {
                    drag_.kind = Drag::Kind::Plane;
                    drag_.centre = *centre;
                    drag_.unitsPerPoint = unitsPerPoint(drag_.view, *centre);
                    beginTransform();
                } else {
                    drag_.kind = Drag::Kind::Box;
                }
            }
            break;
        }
        case Drag::Kind::Box: {
            auto* list = ImGui::GetForegroundDrawList();
            const ImVec2 lo{std::min(drag_.press.x, mouse.x), std::min(drag_.press.y, mouse.y)};
            const ImVec2 hi{std::max(drag_.press.x, mouse.x), std::max(drag_.press.y, mouse.y)};
            list->PushClipRect(v.min, add(v.min, v.size), true);
            list->AddRectFilled(lo, hi, IM_COL32(90, 145, 210, 40));
            list->AddRect(lo, hi, IM_COL32(120, 175, 240, 220));
            list->PopClipRect();
            if (released) {
                boxSelect(drag_.view, drag_.press, mouse, drag_.add, drag_.toggle);
                drag_ = {};
            }
            break;
        }
        case Drag::Kind::Axis: {
            const double t = (moved.x * drag_.screenAxis.x + moved.y * drag_.screenAxis.y) * drag_.unitsPerPoint;
            const auto steps = moveSteps(selectionHasPieces());
            const auto s = snapped(t, steps[static_cast<std::size_t>(drag_.axis)]);
            // File Y points down.
            const std::array<std::int32_t, 3> offset{drag_.axis == 0 ? s : 0, drag_.axis == 1 ? -s : 0, drag_.axis == 2 ? s : 0};
            if (offset != drag_.lastOffset) {
                applyTransform(offset, 0);
                drag_.lastOffset = offset;
            }
            status_ = std::format("Moving along {}: {} units", "XYZ"[drag_.axis], s);
            break;
        }
        case Drag::Kind::Plane: {
            const auto cam = cameraOf(drag_.view);
            const auto r = cam.right(), u = cam.up();
            const double dx = moved.x * drag_.unitsPerPoint, dy = -moved.y * drag_.unitsPerPoint;
            const auto steps = moveSteps(selectionHasPieces());
            const auto offset = vertexSnapOffset({snapped(r.x * dx + u.x * dy, steps[0]), -snapped(r.y * dx + u.y * dy, steps[1]),
                                                  snapped(r.z * dx + u.z * dy, steps[2])});
            if (offset != drag_.lastOffset) {
                applyTransform(offset, 0);
                drag_.lastOffset = offset;
            }
            status_ = std::format("Moving by ({}, {}, {})", offset[0], offset[1], offset[2]);
            break;
        }
        case Drag::Kind::Turn: {
            const auto ray = cameraOf(drag_.view).rayThrough(mouse.x - v.min.x, mouse.y - v.min.y, v.size.x, v.size.y);
            if (const auto angle = angleOnRing(ray, drag_.centre, drag_.axis)) {
                // Unwrapped, so turning past half a turn keeps going.
                double step = *angle - drag_.lastYaw;
                while (step > std::numbers::pi) step -= 2 * std::numbers::pi;
                while (step < -std::numbers::pi) step += 2 * std::numbers::pi;
                drag_.turned += step;
                drag_.lastYaw = *angle;
            }
            const auto units = snapped(drag_.turned * catalog::kRotationUnitsPerTurn / (2 * std::numbers::pi),
                                       turnStep(selectionHasPieces()));
            if (units != drag_.lastTurn) {
                applyTurn(drag_.axis, units);
                drag_.lastTurn = units;
            }
            status_ = std::format("Turning {:.1f} degrees about {}", units * 360.0 / catalog::kRotationUnitsPerTurn, "XYZ"[drag_.axis]);
            break;
        }
        case Drag::Kind::None: break;
    }
    if (transforming && released) {
        endTransform(true);
        drag_ = {};
    }
}

} // namespace de::app
