#pragma once

#include "core/byte_view.h"
#include "core/error.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace de::formats {

/// The role a dungeon block plays, from the first letter of its name. The game reads this letter (border caps,
/// smaller-dungeon generation), so it is data, not decoration.
enum class RdbRole { Unknown, Border, Normal, Wet, Quest, Mausoleum };
RdbRole rdbRoleFromName(std::string_view blockName);
std::string_view roleName(RdbRole role);

/// One slot of the block's model reference table (750 slots, most unused).
struct RdbModelRef {
    std::string modelId;       ///< As stored: up to 5 characters, usually digits.
    std::uint32_t modelIdNum = 0;  ///< modelId parsed as a number; 0 when it is not all digits.
    std::string description;   ///< 3 characters: "DOR", "C00", "EXT", ...
};

/// A model's action record. Present only when the model's action offset is positive; the two link fields are filled
/// in when another model's action names this object.
struct RdbAction {
    std::uint32_t position = 0;   ///< Byte offset of the record in the block.
    std::uint8_t axis = 0;
    std::uint16_t duration = 0;   ///< In 1/20 s.
    std::uint16_t magnitude = 0;
    std::int32_t nextObjectOffset = 0;   ///< Position of the next object in the chain, or negative for none.
    std::uint8_t flags = 0;              ///< The action kind (move, rotate, teleport, ...).
    std::int32_t previousObjectOffset = -1;
    std::int32_t nextObjectIndex = -1;   ///< Index of the next object within its root, or -1.
};

struct RdbModel {
    std::int32_t xRotation = 0, yRotation = 0, zRotation = 0;   ///< 2048 = 360 degrees.
    std::uint16_t modelIndex = 0;                ///< Into RdbBlock::modelRefs.
    std::uint32_t triggerFlagStartingLock = 0;   ///< Whole value = trigger (if defined); >> 4 = starting lock.
    std::uint8_t soundIndex = 0;
    std::int32_t actionOffset = 0;
    RdbAction action;   ///< Meaningful when hasAction(); link fields may be set either way.
    [[nodiscard]] bool hasAction() const { return actionOffset > 0; }
};

struct RdbFlat {
    std::uint32_t position = 0;   ///< Byte offset of the flat resource in the block.
    std::uint16_t textureBitfield = 0;
    std::uint16_t flags = 0;
    std::uint8_t magnitude = 0;
    std::uint8_t soundIndex = 0;
    std::int32_t nextObjectOffset = 0;
    std::uint8_t action = 0;
    [[nodiscard]] int textureArchive() const { return textureBitfield >> 7; }
    [[nodiscard]] int textureRecord() const { return textureBitfield & 0x7f; }
    /// Magnitude and sound index read together: the enemy type of a fixed-enemy marker.
    [[nodiscard]] std::uint16_t factionOrMobileId() const {
        return static_cast<std::uint16_t>(magnitude | (soundIndex << 8));
    }
};

struct RdbLight {
    std::uint32_t unknown1 = 0, unknown2 = 0;
    std::uint16_t radius = 0;
};

enum class RdbObjectType : std::uint8_t { Model = 0x01, Light = 0x02, Flat = 0x03 };

struct RdbObject {
    std::int32_t position = 0;   ///< Byte offset in the block; the key action chains and save ids use.
    std::int32_t index = 0;      ///< Index within its root.
    std::int32_t x = 0, y = 0, z = 0;   ///< Block units (1 = 0.025 m), Daggerfall axes (Y down).
    RdbObjectType type = RdbObjectType::Model;
    std::variant<RdbModel, RdbLight, RdbFlat> resource;

    [[nodiscard]] const RdbModel* model() const { return std::get_if<RdbModel>(&resource); }
    [[nodiscard]] const RdbFlat* flat() const { return std::get_if<RdbFlat>(&resource); }
    [[nodiscard]] const RdbLight* light() const { return std::get_if<RdbLight>(&resource); }
};

/// One entry of the object root list. Roots are groups, not spatial cells.
struct RdbRoot {
    std::int32_t offset = -1;   ///< Negative: an empty root.
    std::vector<RdbObject> objects;
};

struct RdbBlock {
    std::uint32_t width = 0, height = 0;   ///< Root list dimensions (4x4 or 8x8 in classic data).
    std::vector<RdbModelRef> modelRefs;    ///< Always 750 slots.
    std::vector<RdbRoot> roots;            ///< width * height entries.

    [[nodiscard]] std::size_t objectCount() const;
};

constexpr std::size_t kRdbModelRefSlots = 750;

/// Parses one RDB record (a BLOCKS.BSA entry whose name ends in .RDB). Classic data only; DFU's per-block repairs
/// (FixRdbData) are not applied here.
Result<RdbBlock> parseRdbBlock(ByteView record);

} // namespace de::formats
