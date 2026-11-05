#pragma once

// Editing one custom model's mesh: the mesh as it stands, and its history. Every change goes through change() (one
// undo step) or a gesture (a drag: many changes, one step), as BlockEdit does for blocks.

#include "mesh/mesh.h"

#include <optional>
#include <vector>

namespace de::mesh {

class MeshEdit {
public:
    explicit MeshEdit(Mesh start) : current_(std::move(start)) {}

    [[nodiscard]] const Mesh& mesh() const { return current_; }

    /// The mesh to change, after recording an undo step (inside a gesture, the gesture's step).
    Mesh& change();

    void beginGesture();
    /// Ends the gesture; no undo step when nothing changed.
    void endGesture();
    /// Puts everything back as it was when the gesture began.
    void cancelGesture();
    [[nodiscard]] bool inGesture() const { return gestureStart_.has_value(); }

    bool undo();
    bool redo();
    [[nodiscard]] bool canUndo() const { return !undo_.empty(); }
    [[nodiscard]] bool canRedo() const { return !redo_.empty(); }

    /// Changed since the last markSaved (or since editing began).
    [[nodiscard]] bool dirty() const { return revision_ != savedRevision_; }
    void markSaved() { savedRevision_ = revision_; }
    /// Bumped by every change, undo and redo: what the drawn mesh was made from.
    [[nodiscard]] std::uint64_t revision() const { return revision_; }

private:
    Mesh current_;
    std::vector<Mesh> undo_, redo_;
    std::optional<Mesh> gestureStart_;
    std::uint64_t revision_ = 0, savedRevision_ = 0;
};

} // namespace de::mesh
