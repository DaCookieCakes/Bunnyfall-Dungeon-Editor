#include "logic/action.h"

#include "catalog/catalog.h"
#include "formats/text_rsc.h"

#include <algorithm>
#include <array>
#include <format>
#include <unordered_set>

namespace de::logic {

namespace {

constexpr int kEditorFlatArchive = 199;
constexpr std::uint32_t kRedBrickDoorModel = 72100;   // tagged DOR but never built as a door
constexpr double kUnitsPerDegree = 2048.0 / 360.0;   // a turn's magnitude is in 1/2048ths of a full turn
constexpr double kUnitsPerMetre = 40.0;

/// Raw axis values (DFBlock.RdbActionAxes).
enum Axis : std::uint32_t { kAxisNone = 0, kAxisMinusX = 1, kAxisPlusX = 2, kAxisMinusY = 3, kAxisPlusY = 4,
                            kAxisMinusZ = 5, kAxisPlusZ = 6 };

/// The riddle answers the game accepts for "show text with input" (DaggerfallAction.actionTypeTwelveLookup).
struct Riddle {
    std::uint16_t text;
    std::string_view answers;
};
constexpr std::array kRiddles{
    Riddle{5404, "bow, bow arrow, crossbow, bows, crossbows"},
    Riddle{5406, "one, 1"},
    Riddle{5423, "benefactor, the benefactor"},
    Riddle{5424, "shut up, shutup, shaddup"},
    Riddle{5464, "yes, ok, i agree, y, agreed, done, fine, okay, sure, yep"},
};

std::string metres(double units) { return std::format("{:.0f} units ({:.1f} m)", units, units / kUnitsPerMetre); }

std::string seconds(std::uint32_t twentieths) {
    if (twentieths == 0) return "at once";
    return std::format("over {:.2g} s", twentieths / 20.0);
}

/// Where a move goes in editor space (Y up). The game's own table: X and Z go against the axis name.
std::string_view moveDirection(std::uint32_t axis) {
    switch (axis) {
        case kAxisMinusX: return "+X";
        case kAxisPlusX: return "-X";
        case kAxisMinusY: return "down";
        case kAxisPlusY: return "up";
        case kAxisMinusZ: return "+Z";
        case kAxisPlusZ: return "-Z";
        default: return {};
    }
}

std::string_view turnAxis(std::uint32_t axis, int& sign) {
    sign = (axis == kAxisMinusX || axis == kAxisMinusY || axis == kAxisMinusZ) ? -1 : 1;
    switch (axis) {
        case kAxisMinusX: case kAxisPlusX: return "X";
        case kAxisMinusY: case kAxisPlusY: return "Y (upright)";
        case kAxisMinusZ: case kAxisPlusZ: return "Z";
        default: return {};
    }
}

std::string quoted(const formats::TextResource* texts, std::uint16_t id) {
    if (!texts) return std::format("text {}", id);
    const auto t = texts->text(id);
    if (!t) return std::format("text {} (not in TEXT.RSC: nothing shows)", id);
    return std::format("\"{}\"", *t);
}

} // namespace

bool isDefinedTrigger(std::uint32_t raw) {
    switch (raw) {
        case trigger::kNone: case trigger::kWalkOn: case trigger::kUse: case trigger::kWalkInto:
        case trigger::kAttack: case trigger::kUse6: case trigger::kUseHitOrBump: case trigger::kUseOrBump:
        case trigger::kDoor:
            return true;
        default:
            return false;
    }
}

bool isDefinedAction(std::uint32_t raw) {
    if (raw >= action::kNone && raw <= action::kMinusZ) return true;
    if (raw >= action::kLockDoor && raw <= action::kUnknown32 && raw != 0x13) return true;
    switch (raw) {
        case action::kTurn: case action::kCastSpell: case action::kShowText: case action::kShowTextWithInput:
        case action::kTeleport: case action::kUnknown50: case action::kDoorText: case action::kUnknown100:
            return true;
        default:
            return false;
    }
}

std::string_view triggerName(std::uint8_t t) {
    switch (t) {
        case trigger::kNone: return "Set off by another";
        case trigger::kWalkOn: return "Walk on";
        case trigger::kUse: return "Use";
        case trigger::kWalkInto: return "Walk into";
        case trigger::kAttack: return "Hit";
        case trigger::kUse6: return "Use (6)";
        case trigger::kUseHitOrBump: return "Use, hit or walk into";
        case trigger::kUseOrBump: return "Use or walk into";
        case trigger::kDoor: return "Door opened or closed";
        default: return "(undefined)";
    }
}

std::string_view actionName(std::uint8_t a) {
    switch (a) {
        case action::kNone: return "Nothing (passes it on)";
        case action::kMove: return "Move";
        case action::kPlusX: return "Move -X (fixed)";
        case action::kMinusX: return "Move +X (fixed)";
        case action::kPlusY: return "Move up (fixed)";
        case action::kMinusY: return "Move down (fixed)";
        case action::kPlusZ: return "Move -Z (fixed)";
        case action::kMinusZ: return "Move +Z (fixed)";
        case action::kTurn: return "Turn";
        case action::kCastSpell: return "Cast spell";
        case action::kShowText: return "Show text";
        case action::kShowTextWithInput: return "Ask a riddle";
        case action::kTeleport: return "Teleport";
        case action::kLockDoor: return "Lock door";
        case action::kUnlockDoor: return "Unlock door";
        case action::kOpenDoor: return "Open door";
        case action::kCloseDoor: return "Close door";
        case action::kHurt21: return "Hurt (range)";
        case 0x16: case 0x17: case 0x18: case action::kHurt25: return "Hurt";
        case action::kPoison: return "Poison";
        case action::kDrainMagicka: return "Drain magicka";
        case action::kDialogue: return "Dialogue";
        case action::kActivate: return "Activate (passes it on)";
        case action::kSetGlobalVar: return "Set global variable";
        case action::kDoorText: return "Door text";
        case action::kUnknown27: case action::kUnknown32: case action::kUnknown50: case action::kUnknown100:
            return "Unknown (does nothing)";
        default: return "(undefined)";
    }
}

bool isActionDoor(const formats::RdbBlock& block, const formats::RdbModel& model) {
    if (model.modelIndex >= block.modelRefs.size()) return false;
    const auto& ref = block.modelRefs[model.modelIndex];
    if (ref.modelIdNum == kRedBrickDoorModel) return false;
    const auto& d = ref.description;
    return d == "DOR" || d == "DDR" || d == "NEW" || d == "CAV";
}

std::optional<Lock> startingLock(const formats::RdbBlock& block, const formats::RdbObject& obj) {
    const auto* m = obj.model();
    if (!m || !isActionDoor(block, *m)) return std::nullopt;
    const int index = static_cast<int>((m->triggerFlagStartingLock >> 4) & 0xF);
    if (kLockValues[index] == 0) return std::nullopt;
    return Lock{index, kLockValues[index]};
}

std::optional<Node> nodeOf(const formats::RdbBlock& block, const formats::RdbObject& obj) {
    Node n;
    n.position = obj.position;
    if (const auto* m = obj.model()) {
        if (!m->hasAction() || m->action.flags == 0) return std::nullopt;
        n.rawTrigger = m->triggerFlagStartingLock;
        n.rawAction = m->action.flags;
        n.axis = m->action.axis;
        n.duration = m->action.duration;
        n.magnitude = m->action.magnitude;
        n.index = m->soundIndex;
        n.next = m->action.nextObjectOffset;
        n.description = m->modelIndex < block.modelRefs.size() ? block.modelRefs[m->modelIndex].description : "";
    } else if (const auto* f = obj.flat()) {
        if (f->action == 0 || f->textureArchive() == kEditorFlatArchive) return std::nullopt;
        n.isFlat = true;
        n.rawTrigger = f->flags;
        n.rawAction = f->action;
        n.axis = f->magnitude;   // the game reads a flat's magnitude as its axis too
        n.duration = 0;          // and gives a flat's move no time
        n.magnitude = f->magnitude;
        n.index = f->soundIndex;
        n.next = f->nextObjectOffset;
        n.description = "FLT";
    } else {
        return std::nullopt;
    }
    if (n.next <= 0) n.next = -1;   // 0 is the block's header, never an object: flats use it for "none"
    n.trigger = isDefinedTrigger(n.rawTrigger) ? static_cast<std::uint8_t>(n.rawTrigger) : trigger::kNone;
    n.action = isDefinedAction(n.rawAction) ? n.rawAction : action::kNone;
    if (n.action >= action::kPlusX && n.action <= action::kMinusZ) {
        n.duration = 50;
        n.magnitude = static_cast<std::int32_t>(n.axis * 8);
    }
    return n;
}

Description describe(const Node& n, bool isDoor, const formats::TextResource* texts) {
    Description d;
    switch (n.trigger) {
        case trigger::kNone: d.when = "Only when set off by another object"; break;
        case trigger::kWalkOn: d.when = "When the player stands on it"; break;
        case trigger::kUse: case trigger::kUse6: d.when = "When the player uses it"; break;
        case trigger::kWalkInto: d.when = "When the player walks into it"; break;
        case trigger::kAttack: d.when = "When the player hits it"; break;
        case trigger::kUseHitOrBump: d.when = "When the player uses, hits or walks into it"; break;
        case trigger::kUseOrBump: d.when = "When the player uses or walks into it"; break;
        case trigger::kDoor: d.when = "When the player opens or closes it"; break;
        default: break;
    }
    if (isDoor && (n.rawTrigger >> 4) != 0) {
        if ((n.rawTrigger & 0xF) != 0) {
            d.warnings.push_back(std::format("Trigger {} is ignored: the door has a starting lock, which shares its field.",
                                             n.rawTrigger & 0xF));
        }
    }
    std::string triggerNote;
    if (!isDefinedTrigger(n.rawTrigger) && !(isDoor && (n.rawTrigger >> 4) != 0)) {
        triggerNote = std::format("Its trigger field holds {}, which the game ignores.", n.rawTrigger);
    }
    if (n.trigger == trigger::kDoor && !isDoor) d.warnings.push_back("Only a door can be set off by opening it.");

    const auto toggle = n.duration > 0 ? "; back again the next time" : "; back again the next time (it jumps)";
    switch (n.action) {
        case action::kNone:
            d.does = "Does nothing itself; passes it on.";
            if (!isDefinedAction(n.rawAction)) {
                d.warnings.push_back(std::format("Action value {} isn't one the game knows, so it does nothing.", n.rawAction));
            }
            break;
        case action::kMove: case action::kPlusX: case action::kMinusX: case action::kPlusY: case action::kMinusY:
        case action::kPlusZ: case action::kMinusZ: {
            std::uint32_t axis = n.axis;
            if (n.action != action::kMove) {
                static constexpr std::uint32_t kFixed[] = {kAxisPlusX, kAxisMinusX, kAxisPlusY, kAxisMinusY, kAxisPlusZ, kAxisMinusZ};
                axis = kFixed[n.action - action::kPlusX];
            }
            const auto dir = moveDirection(axis);
            if (dir.empty()) {
                d.does = "Moves nowhere (no axis set).";
                d.warnings.push_back("A move needs an axis.");
            } else {
                d.does = std::format("Moves {} {} {}{}.", dir, metres(n.magnitude), seconds(n.duration), toggle);
            }
            if (n.action != action::kMove) d.detail = "This move kind always takes 2.5 s and goes axis × 8 units.";
            break;
        }
        case action::kTurn: {
            if (n.description == "LID") {
                d.does = std::format("Turns -90° about Z {}{}.", seconds(n.duration), toggle);
                d.detail = "Coffin lids (LID) always turn this way, whatever the record says.";
            } else if (n.description == "WHE") {
                d.does = std::format("Turns a full circle about Y (upright) {}{}.", seconds(n.duration), toggle);
                d.detail = "Wheels (WHE) always turn a full circle, whatever the record says.";
            } else if (n.axis == 13 && n.description == "TRP") {
                d.does = std::format("Turns {:.1f}° about X {}{}.", -400 / kUnitsPerDegree, seconds(n.duration), toggle);
                d.detail = "Trapdoors (TRP) with axis 13 get a fixed tilt.";
            } else {
                int sign = 1;
                const auto axisName = turnAxis(n.axis, sign);
                if (axisName.empty()) {
                    d.does = "Turns nowhere (no axis set).";
                    d.warnings.push_back("A turn needs an axis.");
                } else {
                    d.does = std::format("Turns {:.1f}° about {} {}{}.", sign * n.magnitude / kUnitsPerDegree, axisName,
                                         seconds(n.duration), toggle);
                }
            }
            break;
        }
        case action::kCastSpell:
            d.does = std::format("Casts spell {} at the player.", n.index);
            d.detail = "It fires the first time, then only every 22nd time it is set off.";
            break;
        case action::kShowText:
            d.does = "Shows " + quoted(texts, static_cast<std::uint16_t>(kShowTextBase + n.index));
            break;
        case action::kShowTextWithInput: {
            const auto id = static_cast<std::uint16_t>(kShowTextWithInputBase + n.index);
            d.does = "Asks " + quoted(texts, id);
            const auto riddle = std::ranges::find(kRiddles, id, &Riddle::text);
            d.detail = riddle != kRiddles.end()
                           ? std::format("The chain goes on only on a right answer: {}.", riddle->answers)
                           : "No answers are known for this text, so the chain never goes on.";
            break;
        }
        case action::kTeleport:
            d.does = "Teleports the player to the next object.";
            d.detail = "The next object is set off too, if it has an action.";
            if (n.next < 0) d.warnings.push_back("A teleport needs a next object to land on.");
            break;
        case action::kLockDoor: case action::kUnlockDoor: case action::kOpenDoor: case action::kCloseDoor: {
            static constexpr const char* kVerb[] = {"Locks", "Unlocks", "Opens", "", "Closes"};
            const bool swings = n.action == action::kOpenDoor || n.action == action::kCloseDoor;
            if (isDoor) {
                d.does = std::format("{} this door.", kVerb[n.action - action::kLockDoor]);
            } else if (swings) {
                d.does = "Swings open and shut like a door (-90° over 1.5 s), with no lock.";
                d.detail = "Anything given this action becomes a door that can't be locked: a secret wall, a trapdoor.";
            } else {
                d.does = "Nothing: only a door can lock or unlock, and only itself.";
                d.warnings.push_back("Link to the door and give the door this action instead.");
            }
            break;
        }
        case action::kHurt21:
            d.does = std::format("Hurts the player for {} to {} × their level.", std::max(1, n.magnitude),
                                 std::max(1, int(n.index)));
            d.detail = "Only every 20th time it is set off.";
            break;
        case 0x16: case 0x17: case 0x18: case action::kHurt25:
            d.does = std::format("Hurts the player for {} × their level.", n.axis);
            break;
        case action::kPoison:
            d.does = "Nothing: poison was never made to work.";
            break;
        case action::kDrainMagicka:
            d.does = std::format("Drains {} magicka from the player.", std::max<std::uint32_t>(1, n.axis));
            break;
        case action::kSetGlobalVar:
            d.does = std::format("Sets global variable {} (quests can read it).", n.axis);
            break;
        case action::kDoorText: {
            const auto id = static_cast<std::uint16_t>(kDoorTextBase + n.index);
            d.does = "The first time, shows " + quoted(texts, id);
            if (n.axis > 5) d.detail = "After that, using it makes the dungeon's people hostile (axis above 5).";
            break;
        }
        default:
            d.does = "Does nothing itself; passes it on.";
            break;
    }
    if (!triggerNote.empty()) d.detail = d.detail.empty() ? triggerNote : d.detail + " " + triggerNote;
    if (n.index > 0) {
        const auto sound = std::format("Plays sound {} each time.", n.index);
        d.detail = d.detail.empty() ? sound : d.detail + " " + sound;
    }
    if (n.action == action::kShowTextWithInput) {
        d.detail += " Unlike every other action, it doesn't set off its next object at once.";
    }
    return d;
}

std::optional<edit::ObjectSlot> findPosition(const formats::RdbBlock& block, std::int32_t position) {
    for (std::size_t r = 0; r < block.roots.size(); ++r) {
        const auto& objects = block.roots[r].objects;
        for (std::size_t o = 0; o < objects.size(); ++o) {
            if (objects[o].position == position) return edit::ObjectSlot{r, o};
        }
    }
    return std::nullopt;
}

std::vector<Link> linksOf(const formats::RdbBlock& block) {
    std::vector<Link> out;
    for (std::size_t r = 0; r < block.roots.size(); ++r) {
        const auto& objects = block.roots[r].objects;
        for (std::size_t o = 0; o < objects.size(); ++o) {
            const auto node = nodeOf(block, objects[o]);
            if (!node || node->next < 0) continue;
            Link link{{r, o}, {}, node->next, LinkKind::Missing};
            if (const auto to = findPosition(block, node->next)) {
                link.to = *to;
                const auto& target = block.roots[to->root].objects[to->object];
                if (node->action == action::kTeleport) link.kind = LinkKind::Teleport;
                else if (nodeOf(block, target)) link.kind = LinkKind::Chain;
                else link.kind = LinkKind::Ignored;
            }
            out.push_back(link);
        }
    }
    return out;
}

std::string objectLabel(const formats::RdbBlock& block, const formats::RdbObject& obj) {
    if (const auto* m = obj.model()) {
        if (m->modelIndex >= block.modelRefs.size()) return "model ?";
        const auto& ref = block.modelRefs[m->modelIndex];
        return std::format("{} {}", ref.modelIdNum, ref.description);
    }
    if (const auto* f = obj.flat()) {
        if (f->textureArchive() == kEditorFlatArchive) return std::string(catalog::markerName(f->textureRecord()));
        return std::format("flat {}/{}", f->textureArchive(), f->textureRecord());
    }
    if (const auto* l = obj.light()) return std::format("light r{}", l->radius);
    return "?";
}

std::vector<ChainStep> chainFrom(const formats::RdbBlock& block, edit::ObjectSlot start) {
    std::vector<ChainStep> out;
    std::unordered_set<std::int32_t> seen;
    std::optional<edit::ObjectSlot> at = start;
    while (at) {
        const auto& obj = block.roots[at->root].objects[at->object];
        if (!seen.insert(obj.position).second) break;   // a loop
        auto node = nodeOf(block, obj);
        out.push_back({*at, node});
        if (!node || node->next < 0) break;
        at = findPosition(block, node->next);
    }
    return out;
}

} // namespace de::logic
