#include "scene/texture_table.h"

#include "formats/classic_random.h"
#include "formats/climate.h"

namespace de::scene {

namespace {
constexpr int kExitDoorArchive = 74;
constexpr int kInteriorSewer = 68;
// TravelTimeCalculator.climateIndices, by (climate - Ocean).
constexpr std::array<int, 10> kClimateIndices = {0, 0, 0, 1, 2, 3, 4, 5, 5, 5};
constexpr std::array<int, 10> kArchiveIndexOf = {0, 0, 1, 4, 4, 0, 3, 3, 3, 0};
constexpr std::array<int, 5> kClassicArchives = {19, 119, 319, 419, 119};
} // namespace

TextureTable TextureTable::classic(std::uint16_t locationId, int worldClimate) {
    using namespace formats;
    TextureTable t;
    t.climateBase = climateBaseType(worldClimate);
    int climate = worldClimate == kClimateOcean ? kClimateSwamp : worldClimate;
    if (climate < kClimateOcean || climate > kClimateHauntedWoodlands) climate = kClimateWoodlands;
    const auto classicIndex = static_cast<std::size_t>(kClimateIndices[static_cast<std::size_t>(climate - kClimateOcean)]);
    const auto archiveIndex = static_cast<std::size_t>(kArchiveIndexOf[classicIndex]);

    ClassicRandom random(locationId);
    for (std::size_t i = 0; i < 5; ++i) {
        int offset = random.rangeInclusive(0, 4);
        if (offset == 2) offset = 4;   // there is no archive at offset 2
        t.archives[i] = kClassicArchives[archiveIndex] + offset;
    }
    // The sewer slot follows the climate directly (Desert is index 0 here; Ocean was already read as Swamp).
    const auto sewerIndex = static_cast<std::size_t>(std::max(0, climate - kClimateDesert));
    t.archives[5] = kInteriorSewer + 100 * kArchiveIndexOf[sewerIndex];
    return t;
}

int TextureTable::apply(int archive) const {
    if (archive == kExitDoorArchive) return archive + climateBase;
    for (std::size_t i = 0; i < kBaseArchives.size(); ++i) {
        if (archive == kBaseArchives[i]) return archives[i];
    }
    return archive;
}

} // namespace de::scene
