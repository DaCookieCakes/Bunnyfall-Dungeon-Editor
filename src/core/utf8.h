#pragma once

#include <cstdint>
#include <span>
#include <string>

namespace de {

/// Appends one code point as UTF-8.
inline void appendUtf8(std::string& out, std::uint32_t cp) {
    if (cp < 0x80) {
        out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

/// Decodes bytes as UTF-8 the way a browser's TextDecoder does (the WHATWG algorithm): every invalid sequence
/// becomes U+FFFD, the result is always valid UTF-8. Bunnyfall reads Daggerfall's text fields this way, so text the
/// editor writes back for Bunnyfall must be decoded the same.
inline std::string decodeUtf8Lossy(std::span<const std::uint8_t> bytes) {
    constexpr std::uint32_t kReplacement = 0xFFFD;
    std::string out;
    out.reserve(bytes.size());
    std::uint32_t cp = 0;
    int needed = 0, seen = 0;
    std::uint8_t lower = 0x80, upper = 0xBF;
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        const std::uint8_t b = bytes[i];
        if (needed == 0) {
            if (b <= 0x7F) {
                out.push_back(static_cast<char>(b));
            } else if (b >= 0xC2 && b <= 0xDF) {
                needed = 1;
                cp = b & 0x1Fu;
            } else if (b >= 0xE0 && b <= 0xEF) {
                if (b == 0xE0) lower = 0xA0;
                if (b == 0xED) upper = 0x9F;
                needed = 2;
                cp = b & 0x0Fu;
            } else if (b >= 0xF0 && b <= 0xF4) {
                if (b == 0xF0) lower = 0x90;
                if (b == 0xF4) upper = 0x8F;
                needed = 3;
                cp = b & 0x07u;
            } else {
                appendUtf8(out, kReplacement);
            }
            continue;
        }
        if (b < lower || b > upper) {
            // The sequence ends early: one replacement, and this byte starts afresh.
            cp = 0;
            needed = seen = 0;
            lower = 0x80;
            upper = 0xBF;
            appendUtf8(out, kReplacement);
            --i;
            continue;
        }
        lower = 0x80;
        upper = 0xBF;
        cp = (cp << 6) | (b & 0x3Fu);
        if (++seen == needed) {
            appendUtf8(out, cp);
            cp = 0;
            needed = seen = 0;
        }
    }
    if (needed != 0) appendUtf8(out, kReplacement);
    return out;
}

} // namespace de
