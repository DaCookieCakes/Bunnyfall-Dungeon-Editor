#pragma once

// Editing one dungeon block: the classic block it started from (what a patch is made against), the block as edited,
// and the history of edits. Every change goes through here, so undo, the save identities of new objects and the
// action links stay right whatever the UI does.

#include "formats/rdb.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace de::edit {

/// An object in the block: which root, which place in it.
struct ObjectSlot {
    std::size_t root = 0;
    std::size_t object = 0;
    friend bool operator==(const ObjectSlot&, const ObjectSlot&) = default;
};

/// New objects take save identities (their Position) from here up, past any offset a classic block has.
inline constexpr std::int32_t kFirstNewPosition = 900000;
inline constexpr std::int32_t kPositionStep = 32;

class BlockEdit {
public:
    /// `classic` is the block as the game reads it unpatched; `start` what editing begins from (the classic block, or
    /// a saved patch of it). `bsaIndex` and `bsaOffset` place it in BLOCKS.BSA.
    BlockEdit(std::string name, std::size_t bsaIndex, std::uint32_t bsaOffset, formats::RdbBlock classic,
              formats::RdbBlock start);

    [[nodiscard]] const std::string& name() const { return name_; }
    [[nodiscard]] std::size_t bsaIndex() const { return bsaIndex_; }
    [[nodiscard]] std::uint32_t bsaOffset() const { return bsaOffset_; }
    [[nodiscard]] const formats::RdbBlock& classic() const { return classic_; }
    [[nodiscard]] const formats::RdbBlock& block() const { return current_; }
    [[nodiscard]] const formats::RdbObject* object(ObjectSlot slot) const;

    // ---- edits; each is one step of undo unless inside a gesture ----

    /// Adds `obj` with a new save identity. Goes into the first root holding objects (root 0 if none).
    ObjectSlot add(formats::RdbObject obj);
    /// Adds a model object (`obj` holds an RdbModel) showing model `modelId`: its modelIndex is set to the block's
    /// slot for that model, a free slot taking it (with `description`) when the block has none; one undo step.
    /// Nullopt when all 750 slots are used by other models.
    std::optional<ObjectSlot> addModel(std::uint32_t modelId, std::string_view description, formats::RdbObject obj);
    /// Shows another model on a model object (keeping its place, turn and actions); takes a model slot as addModel
    /// does. False when it is not a model or no slot is free.
    bool setModel(ObjectSlot slot, std::uint32_t modelId, std::string_view description);
    /// The block's model slot for `modelId`, if it has one.
    [[nodiscard]] std::optional<std::uint16_t> modelSlot(std::uint32_t modelId) const;
    /// Replaces an object's fields (moving it, changing what it is). Its save identity is kept.
    bool replace(ObjectSlot slot, formats::RdbObject obj);
    bool remove(ObjectSlot slot);
    /// A copy beside the original (`dx`, `dz` file units away) with its own save identity.
    std::optional<ObjectSlot> duplicate(ObjectSlot slot, std::int32_t dx = 32, std::int32_t dz = 0);

    /// A gesture (dragging an object about) is many changes and one undo step. cancelGesture puts everything back.
    void beginGesture();
    void endGesture();
    void cancelGesture();
    [[nodiscard]] bool inGesture() const { return gestureStart_.has_value(); }

    bool undo();
    bool redo();
    [[nodiscard]] bool canUndo() const { return !undo_.empty(); }
    [[nodiscard]] bool canRedo() const { return !redo_.empty(); }

    /// Changed since the last save (or since editing began).
    [[nodiscard]] bool dirty() const { return revision_ != savedRevision_; }
    void markSaved() { savedRevision_ = revision_; }
    /// Differs from the classic block at all.
    [[nodiscard]] bool differsFromClassic() const;

private:
    void beforeChange();
    ObjectSlot insert(formats::RdbObject obj);
    /// A slot to put a new model reference in (an unused one, or one past the end while under 750).
    [[nodiscard]] std::optional<std::size_t> freeModelSlot() const;
    std::uint16_t claimModelSlot(std::uint32_t modelId, std::string_view description, std::size_t free);
    void afterChange();
    std::int32_t nextPosition() const;

    std::string name_;
    std::size_t bsaIndex_;
    std::uint32_t bsaOffset_;
    formats::RdbBlock classic_;
    formats::RdbBlock current_;
    std::vector<formats::RdbBlock> undo_, redo_;
    std::optional<formats::RdbBlock> gestureStart_;
    std::uint64_t revision_ = 0, savedRevision_ = 0;
};

/// Re-numbers each root's objects and resolves the action links (next index, previous offset) from the offsets, as
/// the block reader does. Idempotent on any block the reader produced.
void relinkActions(formats::RdbBlock& block);

} // namespace de::edit
