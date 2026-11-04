#pragma once

// Moving and turning several objects together, from a snapshot taken when the gesture started (so a drag never
// accumulates rounding), and keeping references to a block's objects right when one is removed.

#include "edit/block_edit.h"
#include "formats/rdb.h"

#include <array>
#include <cstdint>
#include <vector>

namespace de::edit {

/// An object as it was when the gesture started.
struct Moving {
    ObjectSlot slot;
    formats::RdbObject start;
};

/// `start` offset by `offset` (file units) and turned by `turn` about the vertical axis through (pivotX, pivotZ)
/// (file units). `turn` is in file rotation units (2048 a full turn); positive turns clockwise seen from above
/// (+Z toward +X). Models also turn on their own axis by the same amount; flats and lights only move.
formats::RdbObject transformed(const formats::RdbObject& start, std::array<std::int32_t, 3> offset, std::int32_t turn,
                               double pivotX, double pivotZ);

/// The amount a model's yRotation changes for a clockwise (seen from above) turn of `turn` units.
std::int32_t modelYRotationFor(std::int32_t turn);

/// Slots after `removed` in the same root move down one; `removed` itself is dropped. For slots of the block it was
/// removed from.
void forgetRemoved(std::vector<ObjectSlot>& slots, ObjectSlot removed);

} // namespace de::edit
