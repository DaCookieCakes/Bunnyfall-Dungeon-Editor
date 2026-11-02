#pragma once

#include "core/file.h"
#include "formats/bsa.h"

#include <filesystem>

namespace de::formats {

/// The ARENA2 archives the dungeon editor reads. Opening loads them whole (about 80 MB).
struct Arena2 {
    std::filesystem::path dir;
    BsaArchive blocks;   ///< BLOCKS.BSA
    BsaArchive arch3d;   ///< ARCH3D.BSA
    BsaArchive maps;     ///< MAPS.BSA

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
        return a;
    }
};

} // namespace de::formats
