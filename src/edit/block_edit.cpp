#include "edit/block_edit.h"

#include <algorithm>

namespace de::edit {

namespace {

constexpr std::size_t kUndoLimit = 200;
constexpr std::int32_t kFlatResourceOffset = 25;   // a flat's resource follows its 25-byte object header

bool sameBlock(const formats::RdbBlock& a, const formats::RdbBlock& b) {
    if (a.roots.size() != b.roots.size()) return false;
    for (std::size_t i = 0; i < a.modelRefs.size() && i < b.modelRefs.size(); ++i) {
        const auto &x = a.modelRefs[i], &y = b.modelRefs[i];
        if (x.modelId != y.modelId || x.modelIdNum != y.modelIdNum || x.description != y.description) return false;
    }
    for (std::size_t r = 0; r < a.roots.size(); ++r) {
        const auto &ra = a.roots[r].objects, &rb = b.roots[r].objects;
        if (ra.size() != rb.size()) return false;
        for (std::size_t i = 0; i < ra.size(); ++i) {
            const auto &oa = ra[i], &ob = rb[i];
            if (oa.position != ob.position || oa.x != ob.x || oa.y != ob.y || oa.z != ob.z || oa.type != ob.type) return false;
            if (const auto* ma = oa.model()) {
                const auto* mb = ob.model();
                if (ma->xRotation != mb->xRotation || ma->yRotation != mb->yRotation || ma->zRotation != mb->zRotation ||
                    ma->modelIndex != mb->modelIndex || ma->triggerFlagStartingLock != mb->triggerFlagStartingLock ||
                    ma->soundIndex != mb->soundIndex || ma->action.flags != mb->action.flags ||
                    ma->action.nextObjectOffset != mb->action.nextObjectOffset) {
                    return false;
                }
            } else if (const auto* fa = oa.flat()) {
                const auto* fb = ob.flat();
                if (fa->textureBitfield != fb->textureBitfield || fa->flags != fb->flags || fa->magnitude != fb->magnitude ||
                    fa->soundIndex != fb->soundIndex || fa->action != fb->action || fa->nextObjectOffset != fb->nextObjectOffset ||
                    fa->position != fb->position) {
                    return false;
                }
            } else if (oa.light()->radius != ob.light()->radius) {
                return false;
            }
        }
    }
    return true;
}

} // namespace

void relinkActions(formats::RdbBlock& block) {
    for (auto& root : block.roots) {
        for (std::size_t i = 0; i < root.objects.size(); ++i) {
            auto& obj = root.objects[i];
            obj.index = static_cast<std::int32_t>(i);
            if (auto* m = std::get_if<formats::RdbModel>(&obj.resource)) {
                m->action.nextObjectIndex = -1;
                m->action.previousObjectOffset = -1;
            }
        }
        for (auto& obj : root.objects) {
            auto* m = std::get_if<formats::RdbModel>(&obj.resource);
            if (!m || !m->hasAction() || m->action.nextObjectOffset < 0) continue;
            for (std::size_t i = 0; i < root.objects.size(); ++i) {
                if (root.objects[i].position != m->action.nextObjectOffset) continue;
                m->action.nextObjectIndex = static_cast<std::int32_t>(i);
                if (auto* target = std::get_if<formats::RdbModel>(&root.objects[i].resource)) {
                    target->action.previousObjectOffset = obj.position;
                }
                break;
            }
        }
    }
}

BlockEdit::BlockEdit(std::string name, std::size_t bsaIndex, std::uint32_t bsaOffset, formats::RdbBlock classic,
                     formats::RdbBlock start)
    : name_(std::move(name)), bsaIndex_(bsaIndex), bsaOffset_(bsaOffset), classic_(std::move(classic)),
      current_(std::move(start)) {}

const formats::RdbObject* BlockEdit::object(ObjectSlot slot) const {
    if (slot.root >= current_.roots.size() || slot.object >= current_.roots[slot.root].objects.size()) return nullptr;
    return &current_.roots[slot.root].objects[slot.object];
}

void BlockEdit::beforeChange() {
    if (inGesture()) return;   // the gesture's start is the undo step
    undo_.push_back(current_);
    if (undo_.size() > kUndoLimit) undo_.erase(undo_.begin());
    redo_.clear();
}

void BlockEdit::afterChange() {
    relinkActions(current_);
    ++revision_;
}

std::int32_t BlockEdit::nextPosition() const {
    std::int32_t highest = kFirstNewPosition - kPositionStep;
    for (const auto& root : current_.roots) {
        for (const auto& obj : root.objects) {
            highest = std::max(highest, obj.position);
            if (const auto* f = obj.flat()) highest = std::max(highest, static_cast<std::int32_t>(f->position));
        }
    }
    // Round up past the highest, so a flat's resource offset (position + 25) never lands on another object's.
    return (highest / kPositionStep + 1) * kPositionStep;
}

ObjectSlot BlockEdit::add(formats::RdbObject obj) {
    beforeChange();
    if (current_.roots.empty()) current_.roots.emplace_back();
    std::size_t root = 0;
    for (std::size_t r = 0; r < current_.roots.size(); ++r) {
        if (!current_.roots[r].objects.empty()) {
            root = r;
            break;
        }
    }
    obj.position = nextPosition();
    if (auto* f = std::get_if<formats::RdbFlat>(&obj.resource)) {
        f->position = static_cast<std::uint32_t>(obj.position + kFlatResourceOffset);
    }
    auto& objects = current_.roots[root].objects;
    objects.push_back(std::move(obj));
    afterChange();
    return {root, objects.size() - 1};
}

bool BlockEdit::replace(ObjectSlot slot, formats::RdbObject obj) {
    const auto* old = object(slot);
    if (!old) return false;
    beforeChange();
    obj.position = old->position;   // a save identity never changes
    if (auto* f = std::get_if<formats::RdbFlat>(&obj.resource); f && old->flat()) f->position = old->flat()->position;
    current_.roots[slot.root].objects[slot.object] = std::move(obj);
    afterChange();
    return true;
}

bool BlockEdit::remove(ObjectSlot slot) {
    if (!object(slot)) return false;
    beforeChange();
    auto& objects = current_.roots[slot.root].objects;
    objects.erase(objects.begin() + static_cast<std::ptrdiff_t>(slot.object));
    afterChange();
    return true;
}

std::optional<ObjectSlot> BlockEdit::duplicate(ObjectSlot slot, std::int32_t dx, std::int32_t dz) {
    const auto* old = object(slot);
    if (!old) return std::nullopt;
    auto copy = *old;
    copy.x += dx;
    copy.z += dz;
    // A copy is not part of the original's action chain.
    if (auto* m = std::get_if<formats::RdbModel>(&copy.resource)) {
        m->actionOffset = 0;
        m->action = {};
    }
    if (auto* f = std::get_if<formats::RdbFlat>(&copy.resource); f && f->action != 0 && f->nextObjectOffset >= 0) {
        f->nextObjectOffset = -1;
    }
    return add(std::move(copy));
}

void BlockEdit::beginGesture() {
    if (inGesture()) return;
    beforeChange();
    gestureStart_ = current_;
}

void BlockEdit::endGesture() {
    if (!inGesture()) return;
    const bool changed = !sameBlock(*gestureStart_, current_);
    gestureStart_.reset();
    if (!changed) undo_.pop_back();   // nothing happened: no undo step
}

void BlockEdit::cancelGesture() {
    if (!inGesture()) return;
    current_ = std::move(*gestureStart_);
    gestureStart_.reset();
    undo_.pop_back();
    ++revision_;
}

bool BlockEdit::undo() {
    if (inGesture() || undo_.empty()) return false;
    redo_.push_back(std::move(current_));
    current_ = std::move(undo_.back());
    undo_.pop_back();
    ++revision_;
    return true;
}

bool BlockEdit::redo() {
    if (inGesture() || redo_.empty()) return false;
    undo_.push_back(std::move(current_));
    current_ = std::move(redo_.back());
    redo_.pop_back();
    ++revision_;
    return true;
}

bool BlockEdit::differsFromClassic() const { return !sameBlock(classic_, current_); }

} // namespace de::edit
