#pragma once

#include "core/byte_view.h"
#include "core/error.h"

#include <cstdint>
#include <utility>
#include <vector>

namespace de::formats {

/// A world map PAK (CLIMATE.PAK, POLITIC.PAK): one byte per map pixel, 1001 x 500, stored as run-length rows.
class PakMap {
public:
    static constexpr int kWidth = 1001;
    static constexpr int kHeight = 500;

    static Result<PakMap> fromBytes(ByteView bytes);

    /// The value at a map pixel, or -1 outside the map (or before anything is loaded).
    [[nodiscard]] int at(int x, int y) const {
        if (values_.empty() || x < 0 || x >= kWidth || y < 0 || y >= kHeight) return -1;
        return values_[static_cast<std::size_t>(y * kWidth + x)];
    }

private:
    std::vector<std::uint8_t> values_;
};

/// Daggerfall's world climates (CLIMATE.PAK values).
enum WorldClimate : int {
    kClimateOcean = 223,
    kClimateDesert = 224,
    kClimateDesert2 = 225,
    kClimateMountain = 226,
    kClimateRainforest = 227,
    kClimateSwamp = 228,
    kClimateSubtropical = 229,
    kClimateMountainWoods = 230,
    kClimateWoodlands = 231,
    kClimateHauntedWoodlands = 232,
};

/// The texture set a climate builds with: Desert 0, Mountain 100, Temperate 300, Swamp 400.
int climateBaseType(int worldClimate);

/// The region a map pixel belongs to (0..61), or -1 for water or off the map. POLITIC.PAK sits one pixel west of the
/// map table's pixels: read at x + 1, 15,248 of the 15,251 classic locations fall in their own region (3 at x).
/// Bunnyfall reads the same +1 for terrain (MapsFile.getClimateIndex).
inline int regionAtPixel(const PakMap& politic, int x, int y) {
    const int v = politic.at(x + 1, y);
    return v >= 128 && v < 128 + 62 ? v - 128 : -1;
}

/// The world climate of the land at a map pixel: CLIMATE.PAK read at x + 1, as the terrain is (the same offset as
/// POLITIC.PAK). Classic locations carry the value at x itself (Bunnyfall reads them so); the editor's own places use
/// this one, so their dungeon textures match the land around the entrance.
inline int terrainClimateAt(const PakMap& climate, int x, int y) { return climate.at(x + 1, y); }

/// A location's map pixel from its map table longitude and latitude.
inline std::pair<int, int> mapPixel(std::int32_t longitude, std::int32_t latitude) {
    return {longitude / 128, 499 - latitude / 128};
}

} // namespace de::formats
