#pragma once

#include "core/file.h"
#include "formats/bsa.h"
#include "formats/climate.h"

#include <filesystem>

namespace de::formats {

/// The ARENA2 files the dungeon editor reads. Opening loads them whole (about 80 MB).
struct Arena2 {
    std::filesystem::path dir;
    BsaArchive blocks;   ///< BLOCKS.BSA
    BsaArchive arch3d;   ///< ARCH3D.BSA
    BsaArchive maps;     ///< MAPS.BSA
    PakMap climate;      ///< CLIMATE.PAK: each map pixel's world climate

    static Result<Arena2> open(const std::filesystem::path& dir) {
        Arena2 a;
        a.dir = dir;
        for (auto [name, target] : {std::pair{"BLOCKS.BSA", &a.blocks}, std::pair{"ARCH3D.BSA", &a.arch3d},
                                    std::pair{"MAPS.BSA", &a.maps}}) {
            auto path = findArena2File(dir, name);
            if (!path) return std::unexpected(path.error());
            auto archive = BsaArchive::load(*path);
            if (!archive) return std::unexpected(archive.error());
            *target = std::move(*archive);
        }
        auto climatePath = findArena2File(dir, "CLIMATE.PAK");
        if (!climatePath) return std::unexpected(climatePath.error());
        auto climateBytes = readFile(*climatePath);
        if (!climateBytes) return std::unexpected(climateBytes.error());
        auto climate = PakMap::fromBytes(ByteView(*climateBytes));
        if (!climate) return fail("CLIMATE.PAK: " + climate.error().message);
        a.climate = std::move(*climate);
        return a;
    }
};

} // namespace de::formats
