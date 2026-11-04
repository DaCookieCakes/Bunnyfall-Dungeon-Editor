#include "formats/text_rsc.h"
#include "logic/action.h"

#include <doctest/doctest.h>

using namespace de;

namespace {

formats::RdbObject model(std::int32_t position, std::uint16_t modelIndex, std::uint8_t flags, std::int32_t next,
                         std::uint32_t trigger = 0) {
    formats::RdbObject obj;
    obj.position = position;
    obj.type = formats::RdbObjectType::Model;
    formats::RdbModel m;
    m.modelIndex = modelIndex;
    m.triggerFlagStartingLock = trigger;
    if (flags || next >= 0) {
        m.actionOffset = position + 100;
        m.action.position = static_cast<std::uint32_t>(position + 100);
        m.action.flags = flags;
        m.action.nextObjectOffset = next;
        m.action.axis = 4;           // up
        m.action.magnitude = 128;
        m.action.duration = 40;
    }
    obj.resource = m;
    return obj;
}

formats::RdbBlock block(std::vector<formats::RdbObject> objects) {
    formats::RdbBlock b;
    b.modelRefs = {{"21000", 21000, "LEV"}, {"55000", 55000, "DOR"}, {"72100", 72100, "DOR"}};
    b.roots.resize(1);
    b.roots[0].objects = std::move(objects);
    return b;
}

} // namespace

TEST_CASE("nodes follow the game's rules") {
    auto b = block({model(1000, 0, logic::action::kMove, 2000, logic::trigger::kUse),
                    model(2000, 1, logic::action::kOpenDoor, -1),
                    model(3000, 0, 0, 1000)});   // a record of kind 0: the game skips it
    auto lever = logic::nodeOf(b, b.roots[0].objects[0]);
    REQUIRE(lever);
    CHECK(lever->trigger == logic::trigger::kUse);
    CHECK(lever->next == 2000);
    CHECK_FALSE(logic::nodeOf(b, b.roots[0].objects[2]));

    const auto d = logic::describe(*lever, false, nullptr);
    CHECK(d.when == "When the player uses it");
    CHECK(d.does.find("Moves up 128 units (3.2 m) over 2 s") == 0);
    CHECK(d.warnings.empty());

    const auto door = logic::describe(*logic::nodeOf(b, b.roots[0].objects[1]), true, nullptr);
    CHECK(door.does == "Opens this door.");
    CHECK(door.warnings.empty());
    const auto notDoor = logic::describe(*logic::nodeOf(b, b.roots[0].objects[1]), false, nullptr);
    CHECK(notDoor.does.find("Swings open") == 0);   // anything opened like a door becomes one
}

TEST_CASE("the fixed moves use axis x 8 over 2.5 s") {
    auto b = block({model(1000, 0, logic::action::kPlusY, -1)});
    const auto n = logic::nodeOf(b, b.roots[0].objects[0]);
    REQUIRE(n);
    CHECK(n->magnitude == 32);
    CHECK(n->duration == 50);
    CHECK(logic::describe(*n, false, nullptr).does.find("Moves up 32 units") == 0);
}

TEST_CASE("links and chains") {
    auto b = block({model(1000, 0, logic::action::kMove, 2000, logic::trigger::kUse),
                    model(2000, 1, logic::action::kOpenDoor, 3000),
                    model(3000, 0, 0, -1),                                   // no action: the chain stops
                    model(4000, 0, logic::action::kTeleport, 3000, 2),       // teleports land anywhere
                    model(5000, 0, logic::action::kMove, 9999),              // nothing there
                    model(6000, 0, logic::action::kMove, 7000), model(7000, 0, logic::action::kMove, 6000)});
    const auto links = logic::linksOf(b);
    REQUIRE(links.size() == 6);
    CHECK(links[0].kind == logic::LinkKind::Chain);
    CHECK(links[1].kind == logic::LinkKind::Ignored);
    CHECK(links[2].kind == logic::LinkKind::Teleport);
    CHECK(links[3].kind == logic::LinkKind::Missing);
    CHECK(logic::chainFrom(b, {0, 0}).size() == 3);
    CHECK(logic::chainFrom(b, {0, 5}).size() == 2);   // a loop stops
}

TEST_CASE("door locks and triggers share a field") {
    auto b = block({model(1000, 1, 0, -1, 11u << 4), model(2000, 2, 0, -1, 11u << 4), model(3000, 1, 0, -1, 0)});
    const auto lock = logic::startingLock(b, b.roots[0].objects[0]);
    REQUIRE(lock);
    CHECK(lock->value == 25);
    CHECK(lock->magic());
    CHECK_FALSE(logic::startingLock(b, b.roots[0].objects[1]));   // the red brick door isn't a door
    CHECK_FALSE(logic::startingLock(b, b.roots[0].objects[2]));
}

TEST_CASE("TEXT.RSC records") {
    // Header: length 14, one entry (8604 at 14), the end entry; then the record.
    std::vector<std::uint8_t> bytes = {14, 0, 0x9C, 0x21, 14, 0, 0, 0, 0xFF, 0xFF, 0, 0, 0, 0};
    for (char c : std::string("Hello")) bytes.push_back(std::uint8_t(c));
    bytes.push_back(0xFD);
    for (char c : std::string("there")) bytes.push_back(std::uint8_t(c));
    bytes.push_back(0xFD);
    bytes.push_back(0xFE);
    auto t = formats::TextResource::fromBytes(bytes);
    REQUIRE(t);
    CHECK(t->text(8604) == "Hello\nthere");
    CHECK_FALSE(t->text(8605));
    CHECK(t->idsBetween(8600, 8699).size() == 1);
}

TEST_CASE("door actions on things that aren't doors") {
    auto b = block({model(1000, 0, logic::action::kOpenDoor, -1), model(2000, 0, logic::action::kLockDoor, -1),
                    model(3000, 1, logic::action::kOpenDoor, -1, (7u << 4) | 2u)});
    const auto swing = logic::describe(*logic::nodeOf(b, b.roots[0].objects[0]), false, nullptr);
    CHECK(swing.does.find("Swings open") == 0);
    CHECK(swing.warnings.empty());
    CHECK(logic::describe(*logic::nodeOf(b, b.roots[0].objects[1]), false, nullptr).warnings.size() == 1);
    const auto mixed = logic::describe(*logic::nodeOf(b, b.roots[0].objects[2]), true, nullptr);
    REQUIRE(mixed.warnings.size() == 1);
    CHECK(mixed.warnings[0].find("starting lock") != std::string::npos);
}
