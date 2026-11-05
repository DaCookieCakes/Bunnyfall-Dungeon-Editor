#include "mesh/mesh_edit.h"

namespace de::mesh {

namespace {
constexpr std::size_t kMaxUndo = 200;
} // namespace

Mesh& MeshEdit::change() {
    if (!inGesture()) {
        undo_.push_back(current_);
        if (undo_.size() > kMaxUndo) undo_.erase(undo_.begin());
        redo_.clear();
    }
    ++revision_;
    return current_;
}

void MeshEdit::beginGesture() {
    if (inGesture()) return;
    gestureStart_ = current_;
}

void MeshEdit::endGesture() {
    if (!inGesture()) return;
    if (!(*gestureStart_ == current_)) {
        undo_.push_back(std::move(*gestureStart_));
        if (undo_.size() > kMaxUndo) undo_.erase(undo_.begin());
        redo_.clear();
    }
    gestureStart_.reset();
}

void MeshEdit::cancelGesture() {
    if (!inGesture()) return;
    current_ = std::move(*gestureStart_);
    gestureStart_.reset();
    ++revision_;
}

bool MeshEdit::undo() {
    if (inGesture() || undo_.empty()) return false;
    redo_.push_back(std::move(current_));
    current_ = std::move(undo_.back());
    undo_.pop_back();
    ++revision_;
    return true;
}

bool MeshEdit::redo() {
    if (inGesture() || redo_.empty()) return false;
    undo_.push_back(std::move(current_));
    current_ = std::move(redo_.back());
    redo_.pop_back();
    ++revision_;
    return true;
}

} // namespace de::mesh
