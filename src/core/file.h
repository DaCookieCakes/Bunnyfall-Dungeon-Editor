#pragma once

#include "core/error.h"

#include <cctype>
#include <cstdint>
#include <string>
#include <string_view>
#include <filesystem>
#include <fstream>
#include <vector>

namespace de {

/// The whole file, or an error naming it.
inline Result<std::vector<std::uint8_t>> readFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) return fail("cannot open " + path.string());
    const auto end = in.tellg();
    if (end < 0) return fail("cannot size " + path.string());
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(end));
    in.seekg(0);
    if (!bytes.empty() && !in.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()))) {
        return fail("cannot read " + path.string());
    }
    return bytes;
}

/// ARENA2 file names are upper case on disk in most copies, but not all. Finds `name` in `dir` ignoring case.
inline Result<std::filesystem::path> findArena2File(const std::filesystem::path& dir, std::string_view name) {
    std::error_code ec;
    const auto exact = dir / name;
    if (std::filesystem::exists(exact, ec)) return exact;
    auto lower = [](std::string s) {
        for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
    };
    const auto want = lower(std::string(name));
    for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
        if (lower(entry.path().filename().string()) == want) return entry.path();
    }
    return fail(std::string(name) + " not found in " + dir.string());
}

} // namespace de
