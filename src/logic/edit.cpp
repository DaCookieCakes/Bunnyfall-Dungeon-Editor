#include "logic/edit.h"

#include <algorithm>
#include <cmath>

namespace de::logic {

namespace {

constexpr int kEditorFlatArchive = 199;
constexpr double kUnitsPerDegree = 2048.0 / 360.0;
constexpr std::int32_t kActionRecordOffset = 16;   // where a new action record is said to sit, after its object
constexpr double kDoorSwingDegrees = -90, kDoorSwingSeconds = 1.5;

// Raw axis values (DFBlock.RdbActionAxes).
constexpr std::uint32_t kMinusX = 1, kPlusX = 2, kMinusY = 3, kPlusY = 4, kMinusZ = 5, kPlusZ = 6;

/// The raw axis of a Move action going this way. The game moves X and Z against the axis name.
std::uint32_t moveAxis(Direction d) {
    switch (d) {
        case Direction::Up: return kPlusY;
        case Direction::Down: return kMinusY;
        case Direction::PlusX: return kMinusX;
        case Direction::MinusX: return kPlusX;
        case Direction::PlusZ: return kMinusZ;
        case Direction::MinusZ: return kPlusZ;
    }
    return kPlusY;
}

std::optional<Direction> directionOfAxis(std::uint32_t axis) {
    switch (axis) {
        case kPlusY: return Direction::Up;
        case kMinusY: return Direction::Down;
        case kMinusX: return Direction::PlusX;
        case kPlusX: return Direction::MinusX;
        case kMinusZ: return Direction::PlusZ;
        case kPlusZ: return Direction::MinusZ;
        default: return std::nullopt;
    }
}

/// The fixed move kinds (±X/Y/Z) are named by the axis they pass to the move, so they go the same odd ways.
std::uint8_t fixedMoveKind(Direction d) {
    switch (d) {
        case Direction::Up: return action::kPlusY;
        case Direction::Down: return action::kMinusY;
        case Direction::PlusX: return action::kMinusX;
        case Direction::MinusX: return action::kPlusX;
        case Direction::PlusZ: return action::kMinusZ;
        case Direction::MinusZ: return action::kPlusZ;
    }
    return action::kPlusY;
}

catalog::Vec3 unit(Direction d) {
    switch (d) {
        case Direction::Up: return {0, 1, 0};
        case Direction::Down: return {0, -1, 0};
        case Direction::PlusX: return {1, 0, 0};
        case Direction::MinusX: return {-1, 0, 0};
        case Direction::PlusZ: return {0, 0, 1};
        case Direction::MinusZ: return {0, 0, -1};
    }
    return {};
}

} // namespace

bool canAct(const formats::RdbObject& obj) {
    if (obj.model()) return true;
    if (const auto* f = obj.flat()) return f->textureArchive() != kEditorFlatArchive;
    return false;
}

void setAction(formats::RdbObject& obj, std::uint8_t kind) {
    if (auto* m = std::get_if<formats::RdbModel>(&obj.resource)) {
        if (kind == 0) {
            m->actionOffset = 0;
            m->action = {};
            return;
        }
        if (!m->hasAction()) {
            m->actionOffset = obj.position + kActionRecordOffset;
            m->action = {};
            m->action.position = static_cast<std::uint32_t>(m->actionOffset);
            m->action.nextObjectOffset = -1;
        }
        m->action.flags = kind;
    } else if (auto* f = std::get_if<formats::RdbFlat>(&obj.resource)) {
        if (f->textureArchive() == kEditorFlatArchive) return;
        f->action = kind;
        if (kind == 0) f->nextObjectOffset = 0;
    }
}

void setTrigger(formats::RdbObject& obj, std::uint8_t trigger) {
    if (auto* m = std::get_if<formats::RdbModel>(&obj.resource)) m->triggerFlagStartingLock = trigger;
    else if (auto* f = std::get_if<formats::RdbFlat>(&obj.resource)) f->flags = trigger;
}

void setNext(formats::RdbObject& obj, std::int32_t position) {
    if (auto* m = std::get_if<formats::RdbModel>(&obj.resource)) {
        if (m->hasAction()) m->action.nextObjectOffset = position > 0 ? position : -1;
    } else if (auto* f = std::get_if<formats::RdbFlat>(&obj.resource)) {
        if (f->action != 0) f->nextObjectOffset = position > 0 ? position : 0;   // flats store "none" as 0
    }
}

void setAxis(formats::RdbObject& obj, std::uint32_t axis) {
    if (auto* m = std::get_if<formats::RdbModel>(&obj.resource)) m->action.axis = static_cast<std::uint8_t>(std::min(axis, 255u));
    else if (auto* f = std::get_if<formats::RdbFlat>(&obj.resource)) f->magnitude = static_cast<std::uint8_t>(std::min(axis, 255u));
}

void setMagnitude(formats::RdbObject& obj, std::uint32_t magnitude) {
    if (auto* m = std::get_if<formats::RdbModel>(&obj.resource)) {
        m->action.magnitude = static_cast<std::uint16_t>(std::min(magnitude, 65535u));
    } else if (auto* f = std::get_if<formats::RdbFlat>(&obj.resource)) {
        f->magnitude = static_cast<std::uint8_t>(std::min(magnitude, 255u));
    }
}

void setDuration(formats::RdbObject& obj, std::uint32_t twentieths) {
    if (auto* m = std::get_if<formats::RdbModel>(&obj.resource)) {
        m->action.duration = static_cast<std::uint16_t>(std::min(twentieths, 65535u));
    }
}

void setIndex(formats::RdbObject& obj, std::uint8_t index) {
    if (auto* m = std::get_if<formats::RdbModel>(&obj.resource)) m->soundIndex = index;
    else if (auto* f = std::get_if<formats::RdbFlat>(&obj.resource)) f->soundIndex = index;
}

void setMove(formats::RdbObject& obj, const Move& move) {
    if (obj.model()) {
        setAction(obj, action::kMove);
        setAxis(obj, moveAxis(move.direction));
        setMagnitude(obj, static_cast<std::uint32_t>(std::max(0, move.units)));
        setDuration(obj, move.duration);
    } else if (obj.flat()) {
        setAction(obj, fixedMoveKind(move.direction));
        setAxis(obj, static_cast<std::uint32_t>(std::clamp((move.units + 4) / 8, 1, 255)));
    }
}

std::optional<Move> moveOf(const Node& node) {
    if (node.action == action::kMove) {
        const auto d = directionOfAxis(node.axis);
        if (!d) return std::nullopt;
        return Move{*d, node.magnitude, node.duration};
    }
    if (node.action >= action::kPlusX && node.action <= action::kMinusZ) {
        static constexpr Direction kFixed[] = {Direction::MinusX, Direction::PlusX, Direction::Up,
                                               Direction::Down, Direction::MinusZ, Direction::PlusZ};
        return Move{kFixed[node.action - action::kPlusX], node.magnitude, node.duration};
    }
    return std::nullopt;
}

void setTurn(formats::RdbObject& obj, const Turn& turn) {
    if (!obj.model()) return;   // a flat always faces the camera: its turn can't be seen
    setAction(obj, action::kTurn);
    const bool negative = turn.degrees < 0;
    std::uint32_t axis = 0;
    switch (turn.axis) {
        case TurnAxis::X: axis = negative ? kMinusX : kPlusX; break;
        case TurnAxis::Y: axis = negative ? kMinusY : kPlusY; break;
        case TurnAxis::Z: axis = negative ? kMinusZ : kPlusZ; break;
    }
    setAxis(obj, axis);
    setMagnitude(obj, static_cast<std::uint32_t>(std::lround(std::abs(turn.degrees) * kUnitsPerDegree)));
    setDuration(obj, turn.duration);
}

std::optional<Turn> turnOf(const Node& node) {
    if (node.action != action::kTurn) return std::nullopt;
    Turn t;
    switch (node.axis) {
        case kMinusX: case kPlusX: t.axis = TurnAxis::X; break;
        case kMinusY: case kPlusY: t.axis = TurnAxis::Y; break;
        case kMinusZ: case kPlusZ: t.axis = TurnAxis::Z; break;
        default: return std::nullopt;
    }
    const bool negative = node.axis == kMinusX || node.axis == kMinusY || node.axis == kMinusZ;
    t.degrees = (negative ? -1 : 1) * node.magnitude / kUnitsPerDegree;
    t.duration = node.duration;
    return t;
}

std::optional<Motion> motionOf(const Node& node, bool isDoor) {
    Motion m;
    m.seconds = node.duration / 20.0;
    if (const auto move = moveOf(node)) {
        const auto u = unit(move->direction);
        m.move = {u.x * move->units, u.y * move->units, u.z * move->units};
        return m;
    }
    if (node.action == action::kTurn) {
        // The game's fixed turns come first, as in describe().
        if (node.description == "LID") {
            m.turnAxis = TurnAxis::Z;
            m.degrees = -90;
        } else if (node.description == "WHE") {
            m.turnAxis = TurnAxis::Y;
            m.degrees = -360;
        } else if (node.axis == 13 && node.description == "TRP") {
            m.turnAxis = TurnAxis::X;
            m.degrees = -400 / kUnitsPerDegree;
        } else if (const auto t = turnOf(node)) {
            m.turnAxis = t->axis;
            m.degrees = t->degrees;
        } else {
            return std::nullopt;
        }
        return m;
    }
    const bool swings = node.action == action::kOpenDoor || (node.action == action::kCloseDoor && !isDoor);
    if (swings && !node.isFlat) {
        m.turnAxis = TurnAxis::Y;
        m.degrees = kDoorSwingDegrees;
        m.seconds = kDoorSwingSeconds;
        return m;
    }
    return std::nullopt;
}

catalog::Mat3 turnAt(const Motion& motion, double t) {
    if (!motion.turnAxis) return {};
    // A turn of d degrees is what a placed rotation of -d * 2048 / 360 file units gives.
    const auto units = static_cast<std::int32_t>(std::lround(-motion.degrees * t * kUnitsPerDegree));
    switch (*motion.turnAxis) {
        case TurnAxis::X: return catalog::rdbModelRotation(units, 0, 0);
        case TurnAxis::Y: return catalog::rdbModelRotation(0, units, 0);
        case TurnAxis::Z: return catalog::rdbModelRotation(0, 0, units);
    }
    return {};
}

} // namespace de::logic
