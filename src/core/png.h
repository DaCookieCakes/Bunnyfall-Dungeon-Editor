#pragma once

// A minimal PNG writer: RGBA8, uncompressed deflate blocks. Large files, no dependencies; for screenshots.

#include "core/error.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <vector>

namespace de {

namespace png_detail {

inline std::uint32_t crc32(std::span<const std::uint8_t> data, std::uint32_t crc = 0) {
    static const auto table = [] {
        std::array<std::uint32_t, 256> t{};
        for (std::uint32_t n = 0; n < 256; ++n) {
            std::uint32_t c = n;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xedb88320u ^ (c >> 1) : c >> 1;
            t[n] = c;
        }
        return t;
    }();
    crc = ~crc;
    for (const auto b : data) crc = table[(crc ^ b) & 0xff] ^ (crc >> 8);
    return ~crc;
}

inline void be32(std::vector<std::uint8_t>& out, std::uint32_t v) {
    for (int s = 24; s >= 0; s -= 8) out.push_back(static_cast<std::uint8_t>(v >> s));
}

inline void chunk(std::vector<std::uint8_t>& out, const char* type, const std::vector<std::uint8_t>& data) {
    be32(out, static_cast<std::uint32_t>(data.size()));
    std::vector<std::uint8_t> typed(type, type + 4);
    typed.insert(typed.end(), data.begin(), data.end());
    out.insert(out.end(), typed.begin(), typed.end());
    be32(out, crc32(typed));
}

} // namespace png_detail

/// Writes `rgba` (width * height * 4 bytes, top row first) as a PNG.
inline Result<void> writePng(const std::filesystem::path& path, int width, int height,
                             std::span<const std::uint8_t> rgba) {
    using namespace png_detail;
    if (width <= 0 || height <= 0 || rgba.size() != std::size_t(width) * std::size_t(height) * 4) {
        return fail("writePng: size does not match the pixels");
    }
    std::vector<std::uint8_t> raw;   // each row: filter byte 0, then the pixels
    raw.reserve(std::size_t(height) * (std::size_t(width) * 4 + 1));
    for (int y = 0; y < height; ++y) {
        raw.push_back(0);
        const auto row = rgba.subspan(std::size_t(y) * std::size_t(width) * 4, std::size_t(width) * 4);
        raw.insert(raw.end(), row.begin(), row.end());
    }

    std::vector<std::uint8_t> z{0x78, 0x01};   // zlib header, no compression
    for (std::size_t at = 0; at < raw.size() || at == 0;) {
        const std::size_t n = std::min<std::size_t>(65535, raw.size() - at);
        const bool last = at + n == raw.size();
        z.push_back(last ? 1 : 0);
        z.push_back(static_cast<std::uint8_t>(n));
        z.push_back(static_cast<std::uint8_t>(n >> 8));
        z.push_back(static_cast<std::uint8_t>(~n));
        z.push_back(static_cast<std::uint8_t>(~n >> 8));
        z.insert(z.end(), raw.begin() + static_cast<std::ptrdiff_t>(at), raw.begin() + static_cast<std::ptrdiff_t>(at + n));
        at += n;
        if (last) break;
    }
    std::uint32_t a = 1, b = 0;   // adler32
    for (const auto byte : raw) {
        a = (a + byte) % 65521;
        b = (b + a) % 65521;
    }
    be32(z, (b << 16) | a);

    std::vector<std::uint8_t> out{0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
    std::vector<std::uint8_t> header;
    be32(header, static_cast<std::uint32_t>(width));
    be32(header, static_cast<std::uint32_t>(height));
    header.insert(header.end(), {8, 6, 0, 0, 0});   // 8-bit RGBA
    chunk(out, "IHDR", header);
    chunk(out, "IDAT", z);
    chunk(out, "IEND", {});

    std::ofstream file(path, std::ios::binary);
    if (!file) return fail("cannot write " + path.string());
    file.write(reinterpret_cast<const char*>(out.data()), static_cast<std::streamsize>(out.size()));
    if (!file) return fail("cannot write " + path.string());
    return {};
}

} // namespace de
