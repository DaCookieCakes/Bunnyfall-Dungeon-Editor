#pragma once

// Dungeon actions as the game reads them (DESIGN.md §2.6), measured against Bunnyfall's loader (rdbLayout.js) and
// action system (actionSystem.js):
//
// - A model takes part when its action record's kind is not 0; a flat when its action byte is not 0. Editor markers
//   (archive 199) can always be pointed at, so a teleport can land on one.
// - Each object has one trigger, one action and one "next" link, by the next object's Position.
// - When an object is set off it first sets off its next object and then does its own action, so a whole chain
//   fires at once. The one exception is "show text with input": its next fires only on a right answer.
// - Moves and turns are toggles: the first time forward, the next time back.

#include "edit/block_edit.h"
#include "formats/rdb.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace de::formats {
class TextResource;
}

namespace de::logic {

/// Trigger values (DFBlock.RdbTriggerFlags). Anything else counts as None.
namespace trigger {
inline constexpr std::uint8_t kNone = 0, kWalkOn = 1, kUse = 2, kWalkInto = 3, kAttack = 5, kUse6 = 6,
                              kUseHitOrBump = 8, kUseOrBump = 9, kDoor = 10;
}

/// Action kinds (DFBlock.RdbActionFlags). Undefined values count as None.
namespace action {
inline constexpr std::uint8_t kNone = 0x00, kMove = 0x01, kPlusX = 0x02, kMinusX = 0x03, kPlusY = 0x04,
                              kMinusY = 0x05, kPlusZ = 0x06, kMinusZ = 0x07, kTurn = 0x08, kCastSpell = 0x09,
                              kShowText = 0x0b, kShowTextWithInput = 0x0c, kTeleport = 0x0e, kLockDoor = 0x10,
                              kUnlockDoor = 0x11, kOpenDoor = 0x12, kCloseDoor = 0x14, kHurt21 = 0x15,
                              kHurt22 = 0x16, kHurt25 = 0x19, kPoison = 0x1a, kUnknown27 = 0x1b,
                              kDrainMagicka = 0x1c, kDialogue = 0x1d, kActivate = 0x1e, kSetGlobalVar = 0x1f,
                              kUnknown32 = 0x20, kUnknown50 = 0x32, kDoorText = 0x63, kUnknown100 = 0x64;
}

/// Text record bases: a text action shows record base + its index.
inline constexpr std::uint16_t kShowTextBase = 8600, kShowTextWithInputBase = 5400, kDoorTextBase = 7700;
/// A starting lock at or above this is magic: it can't be picked or bashed.
inline constexpr int kMagicLock = 20;
/// The 16 starting lock values a door's lock index (TriggerFlag_StartingLock >> 4) picks from.
inline constexpr int kLockValues[16] = {0, 2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 25, 30, 50, 128, 255};

[[nodiscard]] bool isDefinedTrigger(std::uint32_t raw);
[[nodiscard]] bool isDefinedAction(std::uint32_t raw);
/// Short names for lists: "Use", "Walk on", "Move", "Open door", ...
[[nodiscard]] std::string_view triggerName(std::uint8_t trigger);
[[nodiscard]] std::string_view actionName(std::uint8_t action);

/// One object's part in the block's logic, with the values the game actually uses.
struct Node {
    std::int32_t position = 0;   ///< The object's Position: what links point at.
    bool isFlat = false;
    std::uint32_t rawTrigger = 0;   ///< As stored (a model's whole TriggerFlag_StartingLock, a flat's Flags).
    std::uint8_t trigger = trigger::kNone;   ///< What the game uses: rawTrigger if defined, else None.
    std::uint8_t rawAction = 0;
    std::uint8_t action = action::kNone;     ///< rawAction if defined, else None.
    std::uint32_t axis = 0;        ///< Raw axis (for a flat, its magnitude: a game quirk).
    std::uint32_t duration = 0;    ///< In 1/20 s, as the game uses it (fixed 50 for the ±X/Y/Z kinds, 0 for flats).
    std::int32_t magnitude = 0;    ///< As the game uses it (axis × 8 for the ±X/Y/Z kinds).
    std::uint8_t index = 0;        ///< Sound index; also the text record, spell or damage cap for some actions.
    std::int32_t next = -1;        ///< The next object's Position, or -1 (stored as 0 or negative).
    std::string description;       ///< The model's 3-letter description ("DOR", "LID", ...), "FLT" for a flat.
};

/// The node an object makes, or nullopt when the game gives it no action.
[[nodiscard]] std::optional<Node> nodeOf(const formats::RdbBlock& block, const formats::RdbObject& obj);

/// Whether the game builds this model as a swinging door (DOR/DDR/NEW/CAV descriptions, not the red brick door).
[[nodiscard]] bool isActionDoor(const formats::RdbBlock& block, const formats::RdbModel& model);

/// A door's starting lock, if it is an action door with one.
struct Lock {
    int index = 0;   ///< 0..15
    int value = 0;   ///< kLockValues[index]
    [[nodiscard]] bool magic() const { return value >= kMagicLock; }
};
[[nodiscard]] std::optional<Lock> startingLock(const formats::RdbBlock& block, const formats::RdbObject& obj);

/// Plain words for a node, split so the UI can lay them out.
struct Description {
    std::string when;    ///< "When used", "When walked on", "Only when set off by another object"
    std::string does;    ///< "Moves up 128 units (3.2 m) over 2.5 s; back the next time."
    std::string detail;  ///< Extra: the text shown, the sound played, the quirk applied. Can be empty.
    std::vector<std::string> warnings;   ///< Things that won't work the way they look.
};
/// `isDoor` says whether the object is an action door (door actions only work on one). `texts` may be null.
[[nodiscard]] Description describe(const Node& node, bool isDoor, const formats::TextResource* texts);

/// How a link from one object to the next turns out in the game.
enum class LinkKind : std::uint8_t {
    Chain,         ///< The next object takes part: it is set off too.
    Teleport,      ///< A teleport's destination (any object; set off too if it has an action).
    Ignored,       ///< The next object has no action, so the chain stops there.
    Missing,       ///< No object in the block has that Position.
};

struct Link {
    edit::ObjectSlot from, to;   ///< `to` is unset for Missing
    std::int32_t toPosition = -1;
    LinkKind kind = LinkKind::Chain;
};

/// Every link in the block, in object order. A link whose source does nothing (a model with action kind 0) is not
/// listed: the game never follows it.
[[nodiscard]] std::vector<Link> linksOf(const formats::RdbBlock& block);

/// Where an object with this Position sits, if any.
[[nodiscard]] std::optional<edit::ObjectSlot> findPosition(const formats::RdbBlock& block, std::int32_t position);

/// A short label for an object: "55000 DOR", "Start marker", "flat 211/3", "light r200".
[[nodiscard]] std::string objectLabel(const formats::RdbBlock& block, const formats::RdbObject& obj);

/// A whole chain from `start`, following next links until one stops, is missing or loops. The first entry is start.
struct ChainStep {
    edit::ObjectSlot slot;
    std::optional<Node> node;
};
[[nodiscard]] std::vector<ChainStep> chainFrom(const formats::RdbBlock& block, edit::ObjectSlot start);

} // namespace de::logic
