#include "formats/maps.h"

#include <array>
#include <cstdio>

namespace de::formats {

namespace {
constexpr std::array<char, 6> kBlockLetters = {'N', 'W', 'L', 'S', 'B', 'M'};
constexpr std::size_t kRecordsPerRegion = 4;   // MAPPITEM, MAPDITEM, MAPTABLE, MAPNAMES
constexpr std::size_t kDungeonRecordInRegion = 1;
constexpr std::size_t kDoorSize = 6;
constexpr std::size_t kLocationHeaderSize = 45 + 26 + 32 + 9;
constexpr std::size_t kLocationNameAt = 71;
constexpr std::size_t kDungeonHeaderSize = 17;
constexpr std::size_t kBlockEntrySize = 4;
constexpr std::uint16_t kOrsiniumLocationId = 50015;
} // namespace

Result<std::string> dungeonBlockName(std::uint16_t bitfield) {
    const auto letter = static_cast<std::size_t>(bitfield >> 11);
    if (letter >= kBlockLetters.size()) return fail("block letter index " + std::to_string(letter));
    char name[16];
    std::snprintf(name, sizeof name, "%c%07u.RDB", kBlockLetters[letter], static_cast<unsigned>(bitfield & 0x3ff));
    return std::string(name);
}

Result<std::vector<DungeonRecord>> readDungeons(const BsaArchive& maps) {
    std::vector<DungeonRecord> out;
    const auto regionCount = maps.count() / kRecordsPerRegion;
    for (std::size_t region = 0; region < regionCount; ++region) {
        const auto recordIndex = region * kRecordsPerRegion + kDungeonRecordInRegion;
        const auto& entry = maps.entry(recordIndex);
        if (!entry.name.starts_with("MAPDITEM")) {
            return fail("MAPS.BSA record " + std::to_string(recordIndex) + " is " + entry.name + ", expected MAPDITEM");
        }
        if (entry.size == 0) continue;
        const auto v = maps.record(recordIndex);
        try {
            const auto dungeonCount = v.u32(0);
            const std::size_t tableEnd = 4 + std::size_t{dungeonCount} * 8;
            for (std::uint32_t i = 0; i < dungeonCount; ++i) {
                DungeonRecord d;
                d.regionIndex = static_cast<int>(region);
                std::size_t r = tableEnd + v.u32(4 + std::size_t{i} * 8);

                const auto doorCount = v.u32(r);
                r += 4 + std::size_t{doorCount} * kDoorSize;
                d.locationId = v.u16(r + 33);
                d.exteriorLocationId = v.u32(r + 41);
                d.name = v.fixedString(r + kLocationNameAt, 32);
                r += kLocationHeaderSize;

                const auto blockCount = v.u16(r + 10);
                r += kDungeonHeaderSize;
                d.blocks.reserve(blockCount);
                for (std::size_t b = 0; b < blockCount; ++b, r += kBlockEntrySize) {
                    DungeonBlockRef ref;
                    ref.x = v.i8(r);
                    ref.z = v.i8(r + 1);
                    ref.bitfield = v.u16(r + 2);
                    ref.isStartingBlock = (ref.bitfield & 0x400) != 0;
                    auto name = dungeonBlockName(ref.bitfield);
                    if (!name) return fail(d.name + ": " + name.error().message);
                    ref.blockName = std::move(*name);
                    d.blocks.push_back(std::move(ref));
                }

                // Classic moves one of Orsinium's border blocks off another (reverse engineered; DFU does the same).
                if (d.locationId == kOrsiniumLocationId && d.blocks.size() > 13) d.blocks[13].z = -2;

                out.push_back(std::move(d));
            }
        } catch (const OutOfBounds& e) {
            return fail("region " + std::to_string(region) + " dungeons truncated: " + e.what());
        }
    }
    return out;
}

} // namespace de::formats
