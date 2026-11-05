// Vertex snap: a model being placed or moved goes onto a still model where an open rim of one lies point for point
// on an open rim of the other (a corridor's mouth on the next corridor's, a cave tile's edge on its neighbour's).

#include "app/viewer.h"

#include "project/project.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <set>

namespace de::app {

namespace {

// How near a rim has to come to the rim it meets: this near on screen, or (close up in 3D, where a few units fill the
// screen) this near in block units, a piece-snap step.
constexpr float kSnapRadius = 48;
constexpr double kSnapUnits = 64;

double dot(catalog::Vec3 a, catalog::Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
double length(catalog::Vec3 a) { return std::sqrt(dot(a, a)); }
catalog::Vec3 middle(const catalog::Box& b) { return (b.min + b.max) * 0.5; }

bool overlaps(const catalog::Box& a, const catalog::Box& b) {
    return a.min.x <= b.max.x && b.min.x <= a.max.x && a.min.y <= b.max.y && b.min.y <= a.max.y && a.min.z <= b.max.z &&
           b.min.z <= a.max.z;
}

} // namespace

std::vector<edit::Rim> Viewer::rimsOf(std::uint32_t modelId) {
    // Custom models change while they are built: theirs are worked out each time.
    const bool custom = project::isCustomModelId(modelId);
    if (!custom) {
        if (const auto it = rims_.find(modelId); it != rims_.end()) return it->second;
    }
    std::vector<edit::Rim> rims;
    if (const auto* info = catalog_.model(modelId)) rims = edit::openRims(info->points, info->faces);
    if (!custom) rims_.emplace(modelId, rims);
    return rims;
}

std::optional<catalog::Vec3> Viewer::vertexSnapShift(const std::vector<scene::ModelInstance>& moving) {
    snapRim_.reset();
    if (!vertexSnap_ || moving.empty()) return std::nullopt;
    const std::size_t view = pointerView_;

    // The moving models' rims where they are now, and the room around them worth searching.
    struct Moving {
        edit::Rim rim;
        catalog::Vec3 body;   // the middle of the model it belongs to
    };
    std::vector<Moving> rims;
    std::optional<catalog::Box> around;
    for (const auto& m : moving) {
        const auto* info = catalog_.model(m.modelId);
        if (!info) continue;
        const auto box = catalog::transformBox(info->bounds, m.transform.rotation, m.transform.translation);
        around = around ? catalog::Box{{std::min(around->min.x, box.min.x), std::min(around->min.y, box.min.y),
                                        std::min(around->min.z, box.min.z)},
                                       {std::max(around->max.x, box.max.x), std::max(around->max.y, box.max.y),
                                        std::max(around->max.z, box.max.z)}}
                        : box;
        for (const auto& r : rimsOf(m.modelId)) {
            rims.push_back({edit::placedRim(r, m.transform.rotation, m.transform.translation), middle(box)});
        }
    }
    if (rims.empty() || !around) return std::nullopt;
    const double reach = 2 * kSnapRadius * unitsPerPoint(view, middle(*around)) + kSnapUnits;
    const catalog::Box search{around->min - catalog::Vec3{reach, reach, reach}, around->max + catalog::Vec3{reach, reach, reach}};

    // What is being moved stays out of it (in every copy of the edited block).
    std::set<std::pair<std::size_t, std::size_t>> movingSlots;
    for (const auto& m : moving_) movingSlots.insert({m.slot.root, m.slot.object});

    std::optional<catalog::Vec3> best;
    double bestDistance = 1e300;
    for (std::size_t i = 0; i < scene_.models.size(); ++i) {
        const auto& still = scene_.models[i];
        if (still.ref.block >= scene_.blocks.size() || isHidden(scene::Hit::Kind::Model, i)) continue;
        if (edit_ && !movingSlots.empty() && scene_.blocks[still.ref.block].name == edit_->name() &&
            movingSlots.contains({still.ref.root, still.ref.object})) {
            continue;
        }
        const auto* info = catalog_.model(still.modelId);
        if (!info) continue;
        const auto box = catalog::transformBox(info->bounds, still.transform.rotation, still.transform.translation);
        if (!overlaps(box, search)) continue;
        const auto body = middle(box);
        for (const auto& r : rimsOf(still.modelId)) {
            const auto fixed = edit::placedRim(r, still.transform.rotation, still.transform.translation);
            for (const auto& m : rims) {
                const auto shift = edit::rimShift(m.rim, fixed);
                if (!shift) continue;
                // The two models have to be on either side of the rim, not one over the other.
                const auto mine = m.body + *shift - fixed.centre, theirs = body - fixed.centre;
                const double lengths = length(mine) * length(theirs);
                if (lengths > 1e-6 && dot(mine, theirs) > 0.3 * lengths) continue;
                // Near enough on screen to where the pointer has the rim now; of those, the shortest move (on screen
                // alone, a rim far behind could look nearer than the one beside it).
                const double d = length(*shift);
                const auto from = toScreen(view, m.rim.centre), to = toScreen(view, fixed.centre);
                const bool nearOnScreen = from && to && std::hypot(from->x - to->x, from->y - to->y) <= kSnapRadius;
                if (!nearOnScreen && d > kSnapUnits) continue;
                if (d < bestDistance) {
                    bestDistance = d;
                    best = shift;
                    snapRim_ = fixed;
                }
            }
        }
    }
    return best;
}

std::array<std::int32_t, 3> Viewer::vertexSnapOffset(std::array<std::int32_t, 3> offset) {
    if (!vertexSnap_ || moving_.empty() || !edit_) {
        snapRim_.reset();
        return offset;
    }
    const auto& origin = scene_.blocks[movingBlock_].origin;
    std::vector<scene::ModelInstance> moving;
    for (const auto& m : moving_) {
        const auto placed = edit::transformed(m.start, offset, 0, pivotX_, pivotZ_);
        const auto* model = placed.model();
        if (!model) continue;
        moving.push_back({edit_->block().modelRefs[model->modelIndex].modelIdNum,
                          {catalog::rdbModelRotation(model->xRotation, model->yRotation, model->zRotation),
                           catalog::fromFilePosition(placed.x, placed.y, placed.z) + origin},
                          {}});
    }
    const auto shift = vertexSnapShift(moving);
    if (!shift) return offset;
    // File Y points down.
    return {offset[0] + static_cast<std::int32_t>(std::lround(shift->x)), offset[1] - static_cast<std::int32_t>(std::lround(shift->y)),
            offset[2] + static_cast<std::int32_t>(std::lround(shift->z))};
}

void Viewer::drawSnapRim(std::size_t view) {
    if (!snapRim_ || !vertexSnap_ || (!grabbing_ && moving_.empty() && !placingAnything())) return;
    const auto& v = views_[view];
    auto* list = ImGui::GetWindowDrawList();
    list->PushClipRect(v.min, {v.min.x + v.size.x, v.min.y + v.size.y}, true);
    constexpr ImU32 kColour = IM_COL32(255, 200, 60, 255);
    for (const auto& p : snapRim_->points) {
        if (const auto s = toScreen(view, p)) list->AddCircleFilled(*s, 4, kColour);
    }
    if (const auto c = toScreen(view, snapRim_->centre)) list->AddCircle(*c, 7, kColour, 0, 2);
    list->PopClipRect();
}

} // namespace de::app
