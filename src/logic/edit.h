#pragma once

// Changing an object's logic, in the game's terms (see action.h for how the game reads it). Each function changes one
// object; the caller hands it to BlockEdit::replace, which keeps undo and the links' back references right.

#include "catalog/space.h"
#include "formats/rdb.h"
#include "logic/action.h"

#include <cstdint>
#include <optional>

namespace de::logic {

/// Models can act, and flats that aren't editor markers. Lights and markers can't.
[[nodiscard]] bool canAct(const formats::RdbObject& obj);

/// Gives the object an action of `kind` (creating its record if it has none), keeping its link and values.
/// Kind 0 takes the action away, link and all.
void setAction(formats::RdbObject& obj, std::uint8_t kind);
/// The trigger. On a model it is the whole TriggerFlag_StartingLock field, so it replaces a door's starting lock.
void setTrigger(formats::RdbObject& obj, std::uint8_t trigger);
/// The next object, by its Position; negative clears the link. Does nothing on an object with no action.
void setNext(formats::RdbObject& obj, std::int32_t position);

/// Raw values (a flat has one field for axis and magnitude, and no duration: setting one sets both or nothing).
void setAxis(formats::RdbObject& obj, std::uint32_t axis);
void setMagnitude(formats::RdbObject& obj, std::uint32_t magnitude);
void setDuration(formats::RdbObject& obj, std::uint32_t twentieths);
/// The sound index, which is also the text record, spell or damage cap of some actions.
void setIndex(formats::RdbObject& obj, std::uint8_t index);

/// Directions in editor space (Y up).
enum class Direction : std::uint8_t { Up, Down, PlusX, MinusX, PlusZ, MinusZ };
struct Move {
    Direction direction = Direction::Up;
    int units = 0;
    std::uint32_t duration = 0;   ///< 1/20 s
};
/// A model moves with a Move action (any distance, any time). A flat can only use the fixed moves: axis × 8 units over
/// 2.5 s, so its distance is rounded to 8 and its time ignored.
void setMove(formats::RdbObject& obj, const Move& move);
/// The move a node makes, if it is a move.
[[nodiscard]] std::optional<Move> moveOf(const Node& node);

enum class TurnAxis : std::uint8_t { X, Y, Z };
struct Turn {
    TurnAxis axis = TurnAxis::Y;
    double degrees = 0;   ///< Signed, as the game turns it.
    std::uint32_t duration = 0;
};
void setTurn(formats::RdbObject& obj, const Turn& turn);
[[nodiscard]] std::optional<Turn> turnOf(const Node& node);

/// What an object does to itself when set off, for a preview: a move in editor space and a turn in its own space.
struct Motion {
    catalog::Vec3 move;
    std::optional<TurnAxis> turnAxis;
    double degrees = 0;
    double seconds = 0;   ///< 0 jumps
};
/// The motion of a node, if it moves or turns (doors and swinging things included). `isDoor`: an action door.
[[nodiscard]] std::optional<Motion> motionOf(const Node& node, bool isDoor);
/// The object's own rotation part of a motion `t` of the way through (0..1), to go after its placed rotation.
[[nodiscard]] catalog::Mat3 turnAt(const Motion& motion, double t);

} // namespace de::logic
