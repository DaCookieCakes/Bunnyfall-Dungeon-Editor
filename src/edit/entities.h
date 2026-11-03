#pragma once

// The things an editor places in a block that are not architecture: markers, enemies, treasure, lights, scenery.
// Each is an RDB flat or light with its fields set the way Daggerfall reads them (DESIGN.md §2.5, §2.7).

#include "formats/rdb.h"

#include <span>
#include <string_view>

namespace de::edit {

inline constexpr int kEditorFlatArchive = 199;

enum class EntityKind {
    RandomEnemy,    ///< 199/15: an enemy drawn from the dungeon type's table.
    FixedEnemy,     ///< 199/16: a chosen enemy type.
    Treasure,       ///< 199/19: a random treasure pile.
    QuestSpawn,     ///< 199/11: where a quest may put a person or foe.
    QuestItem,      ///< 199/18: where a quest may put an item.
    EnterMarker,    ///< 199/8: where a start inside the dungeon puts the player.
    StartMarker,    ///< 199/10: where a player walking in arrives.
    Scenery,        ///< Any other flat: torches, bones, furniture, people.
    Light,          ///< A point light.
};

struct EntityInfo {
    EntityKind kind;
    std::string_view name;
    int markerRecord;   ///< The 199 record for markers, -1 otherwise.
};

/// Every kind, in palette order.
std::span<const EntityInfo> entityKinds();
const EntityInfo& entityInfo(EntityKind kind);

/// What a placed object is, read back from its fields.
EntityKind kindOf(const formats::RdbObject& obj);

/// A new object of a kind at a file position, with every field at the value Daggerfall expects for it. `position`
/// is left 0: the block edit gives each new object its own.
formats::RdbObject makeEntity(EntityKind kind, std::int32_t x, std::int32_t y, std::int32_t z, int sceneryArchive = 210,
                              int sceneryRecord = 0);

// ---- enemies ----

struct EnemyType {
    int id;
    std::string_view name;
};
/// Daggerfall's enemy types: monsters 0-42 (39 and 40 unused) and classes 128-145.
std::span<const EnemyType> enemyTypes();
std::string_view enemyName(int id);

/// A fixed enemy's type lives in the flat's Magnitude (Daggerfall reads the low byte of FactionOrMobileId).
int fixedEnemyType(const formats::RdbFlat& flat);
void setFixedEnemyType(formats::RdbFlat& flat, int type);

/// Action byte 99 makes an enemy passive.
inline constexpr std::uint8_t kPassiveAction = 99;
bool isPassive(const formats::RdbFlat& flat);
void setPassive(formats::RdbFlat& flat, bool passive);

/// A fixed enemy's gender (flags bit 1 female, bit 2 male; Daggerfall reads it for types above 43 only).
enum class Gender { Unspecified, Female, Male };
Gender enemyGender(const formats::RdbFlat& flat);
void setEnemyGender(formats::RdbFlat& flat, Gender gender);

/// A random enemy's slot in the dungeon's pre-rolled list (its Flags); 0 rolls one between 1 and 6.
int randomEnemySlot(const formats::RdbFlat& flat);
void setRandomEnemySlot(formats::RdbFlat& flat, int slot);

/// The texture archive and record of a flat.
void setFlatTexture(formats::RdbFlat& flat, int archive, int record);

} // namespace de::edit
