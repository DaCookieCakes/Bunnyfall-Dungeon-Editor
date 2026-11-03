#pragma once

#include <array>
#include <cstdint>

namespace de::scene {

/// A dungeon's texture swaps. Blocks are built with six base archives for walls, floors and sewers; each dungeon
/// swaps them for its own set, so one block looks different from dungeon to dungeon. Exit doors (archive 74) take
/// the climate's door set. Everything else draws as stored.
struct TextureTable {
    static constexpr std::array<int, 6> kBaseArchives = {119, 120, 122, 123, 124, 168};

    std::array<int, 6> archives = kBaseArchives;   ///< What each base archive becomes.
    int climateBase = 0;                           ///< 0, 100, 300 or 400 (Desert, Mountain, Temperate, Swamp).

    /// Daggerfall's own table: seeded by the dungeon's location id, drawn from the climate's sets. This is what
    /// Bunnyfall shows with its default setting (classic textures) for every dungeon.
    static TextureTable classic(std::uint16_t locationId, int worldClimate);

    /// The archive to draw for one a model names.
    [[nodiscard]] int apply(int archive) const;
};

} // namespace de::scene
