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
    PakMap politic;      ///< POLITIC.PAK: each map pixel's region + 128 (64 for water)

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
        for (auto [name, target] : {std::pair{"CLIMATE.PAK", &a.climate}, std::pair{"POLITIC.PAK", &a.politic}}) {
            auto path = findArena2File(dir, name);
            if (!path) return std::unexpected(path.error());
            auto bytes = readFile(*path);
            if (!bytes) return std::unexpected(bytes.error());
            auto pak = PakMap::fromBytes(ByteView(*bytes));
            if (!pak) return fail(std::string(name) + ": " + pak.error().message);
            *target = std::move(*pak);
        }
        return a;
    }
};

} // namespace de::formats
