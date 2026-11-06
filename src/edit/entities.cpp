#include "edit/entities.h"

#include <algorithm>
#include <array>

namespace de::edit {

namespace {

constexpr std::array<EntityInfo, 10> kKinds = {{
    {EntityKind::RandomEnemy, "Random enemy", 15},
    {EntityKind::FixedEnemy, "Fixed enemy", 16},
    {EntityKind::Treasure, "Treasure", 19},
    {EntityKind::QuestSpawn, "Quest spawn", 11},
    {EntityKind::QuestItem, "Quest item", 18},
    {EntityKind::EnterMarker, "Enter marker", 8},
    {EntityKind::StartMarker, "Start marker", 10},
    {EntityKind::Person, "Person", -1},
    {EntityKind::Scenery, "Scenery", -1},
    {EntityKind::Light, "Light", -1},
}};

constexpr std::array<EnemyType, 59> kEnemies = {{
    {0, "Rat"}, {1, "Imp"}, {2, "Spriggan"}, {3, "Giant Bat"}, {4, "Grizzly Bear"}, {5, "Sabertooth Tiger"},
    {6, "Spider"}, {7, "Orc"}, {8, "Centaur"}, {9, "Werewolf"}, {10, "Nymph"}, {11, "Slaughterfish"},
    {12, "Orc Sergeant"}, {13, "Harpy"}, {14, "Wereboar"}, {15, "Skeletal Warrior"}, {16, "Giant"}, {17, "Zombie"},
    {18, "Ghost"}, {19, "Mummy"}, {20, "Giant Scorpion"}, {21, "Orc Shaman"}, {22, "Gargoyle"}, {23, "Wraith"},
    {24, "Orc Warlord"}, {25, "Frost Daedra"}, {26, "Fire Daedra"}, {27, "Daedroth"}, {28, "Vampire"},
    {29, "Daedra Seducer"}, {30, "Vampire Ancient"}, {31, "Daedra Lord"}, {32, "Lich"}, {33, "Ancient Lich"},
    {34, "Dragonling"}, {35, "Fire Atronach"}, {36, "Iron Atronach"}, {37, "Flesh Atronach"}, {38, "Ice Atronach"},
    {41, "Dreugh"}, {42, "Lamia"},
    {128, "Mage"}, {129, "Spellsword"}, {130, "Battlemage"}, {131, "Sorcerer"}, {132, "Healer"}, {133, "Nightblade"},
    {134, "Bard"}, {135, "Burglar"}, {136, "Rogue"}, {137, "Acrobat"}, {138, "Thief"}, {139, "Assassin"},
    {140, "Monk"}, {141, "Archer"}, {142, "Ranger"}, {143, "Barbarian"}, {144, "Warrior"}, {145, "Knight"},
}};

constexpr std::array<int, 13> kPersonArchives = {175, 176, 177, 178, 179, 180, 181, 182, 183, 184, 334, 346, 357};

constexpr std::uint16_t kFemaleFlag = 1;
constexpr std::uint16_t kMaleFlag = 2;
constexpr std::uint16_t kDefaultLightRadius = 100;   // the commonest classic radius

} // namespace

std::span<const EntityInfo> entityKinds() { return kKinds; }

std::span<const int> personArchives() { return kPersonArchives; }
bool isPersonArchive(int archive) { return std::ranges::contains(kPersonArchives, archive); }

std::string clampText(std::string text, std::size_t maxChars) {
    std::size_t chars = 0;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if ((static_cast<unsigned char>(text[i]) & 0xC0) == 0x80) continue;   // inside a character
        if (chars++ == maxChars) {
            text.resize(i);
            break;
        }
    }
    return text;
}

const EntityInfo& entityInfo(EntityKind kind) {
    return *std::ranges::find(kKinds, kind, &EntityInfo::kind);
}

EntityKind kindOf(const formats::RdbObject& obj) {
    if (obj.light()) return EntityKind::Light;
    if (const auto* f = obj.flat(); f && f->textureArchive() == kEditorFlatArchive) {
        for (const auto& k : kKinds) {
            if (k.markerRecord == f->textureRecord()) return k.kind;
        }
    }
    if (const auto* f = obj.flat(); f && isPersonArchive(f->textureArchive())) return EntityKind::Person;
    return EntityKind::Scenery;
}

formats::RdbObject makeEntity(EntityKind kind, std::int32_t x, std::int32_t y, std::int32_t z, int sceneryArchive,
                              int sceneryRecord) {
    formats::RdbObject obj;
    obj.x = x;
    obj.y = y;
    obj.z = z;
    if (kind == EntityKind::Light) {
        obj.type = formats::RdbObjectType::Light;
        obj.resource = formats::RdbLight{0, 0, kDefaultLightRadius};
        return obj;
    }
    formats::RdbFlat flat;
    flat.nextObjectOffset = -1;   // no action chain
    const auto& info = entityInfo(kind);
    if (info.markerRecord >= 0) setFlatTexture(flat, kEditorFlatArchive, info.markerRecord);
    else if (kind == EntityKind::Person && !isPersonArchive(sceneryArchive)) setFlatTexture(flat, 182, 0);
    else setFlatTexture(flat, sceneryArchive, sceneryRecord);
    obj.type = formats::RdbObjectType::Flat;
    obj.resource = flat;
    return obj;
}

std::span<const EnemyType> enemyTypes() { return kEnemies; }

std::string_view enemyName(int id) {
    const auto it = std::ranges::find(kEnemies, id, &EnemyType::id);
    return it == kEnemies.end() ? std::string_view("unknown") : it->name;
}

int fixedEnemyType(const formats::RdbFlat& flat) { return flat.factionOrMobileId() & 0xff; }

void setFixedEnemyType(formats::RdbFlat& flat, int type) {
    flat.magnitude = static_cast<std::uint8_t>(type & 0xff);
    flat.soundIndex = 0;   // the high byte is read as the spawn distance band; classic types never need it
}

bool isPassive(const formats::RdbFlat& flat) { return flat.action == kPassiveAction; }
void setPassive(formats::RdbFlat& flat, bool passive) { flat.action = passive ? kPassiveAction : 0; }

Gender enemyGender(const formats::RdbFlat& flat) {
    if (flat.flags & kMaleFlag) return Gender::Male;   // Daggerfall checks female first, then male overrides
    if (flat.flags & kFemaleFlag) return Gender::Female;
    return Gender::Unspecified;
}

void setEnemyGender(formats::RdbFlat& flat, Gender gender) {
    flat.flags = static_cast<std::uint16_t>(flat.flags & ~(kFemaleFlag | kMaleFlag));
    if (gender == Gender::Female) flat.flags = static_cast<std::uint16_t>(flat.flags | kFemaleFlag);
    if (gender == Gender::Male) flat.flags = static_cast<std::uint16_t>(flat.flags | kMaleFlag);
}

int randomEnemySlot(const formats::RdbFlat& flat) { return flat.flags; }
void setRandomEnemySlot(formats::RdbFlat& flat, int slot) {
    flat.flags = static_cast<std::uint16_t>(std::clamp(slot, 0, 255));
}

void setFlatTexture(formats::RdbFlat& flat, int archive, int record) {
    flat.textureBitfield = static_cast<std::uint16_t>(((archive & 0x1ff) << 7) | (record & 0x7f));
}

} // namespace de::edit
