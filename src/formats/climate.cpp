#include "formats/climate.h"

namespace de::formats {

Result<PakMap> PakMap::fromBytes(ByteView v) {
    PakMap map;
    map.values_.assign(static_cast<std::size_t>(kWidth * kHeight), 0);
    try {
        for (std::size_t row = 0; row < kHeight; ++row) {
            std::size_t pos = v.u32(row * 4);
            std::size_t x = 0;
            while (x < kWidth) {
                const auto count = v.u16(pos);
                const auto value = v.u8(pos + 2);
                pos += 3;
                if (count == 0) return fail("PAK row " + std::to_string(row) + " has an empty run");
                for (std::size_t c = 0; c < count && x < kWidth; ++c) map.values_[row * kWidth + x++] = value;
            }
        }
    } catch (const OutOfBounds& e) {
        return fail(std::string("PAK truncated: ") + e.what());
    }
    return map;
}

int climateBaseType(int worldClimate) {
    switch (worldClimate) {
        case kClimateDesert:
        case kClimateDesert2:
        case kClimateSubtropical: return 0;
        case kClimateMountain: return 100;
        case kClimateOcean:
        case kClimateRainforest:
        case kClimateSwamp: return 400;
        default: return 300;   // woodlands, mountain woods, haunted woodlands, and anything unknown
    }
}

} // namespace de::formats
