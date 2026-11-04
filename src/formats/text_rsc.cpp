#include "formats/text_rsc.h"

#include "core/file.h"
#include "core/utf8.h"

namespace de::formats {

namespace {

constexpr std::uint16_t kLastEntry = 0xFFFF;
constexpr std::uint8_t kEndOfRecord = 0xFE;
constexpr std::uint8_t kPosition = 0xFB;   // followed by two position bytes

std::string plain(std::span<const std::uint8_t> bytes) {
    std::string out;
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        const auto b = bytes[i];
        if (b == kPosition) {
            i += 2;
        } else if (b == 0xFC || b == 0xFD || b == 0xFF) {   // centre / end of line / end of page
            out.push_back('\n');
        } else if (b >= 0x20 && b < 0xF0) {
            appendUtf8(out, b);   // Latin-1, which is what the game's fonts draw
        }
    }
    while (!out.empty() && (out.back() == '\n' || out.back() == ' ')) out.pop_back();
    return out;
}

} // namespace

Result<TextResource> TextResource::load(const std::filesystem::path& path) {
    auto bytes = readFile(path);
    if (!bytes) return std::unexpected(bytes.error());
    auto r = fromBytes(*bytes);
    if (!r) return fail(path.filename().string() + ": " + r.error().message);
    return r;
}

Result<TextResource> TextResource::fromBytes(std::span<const std::uint8_t> bytes) {
    auto u16 = [&](std::size_t at) { return static_cast<std::uint16_t>(bytes[at] | (bytes[at + 1] << 8)); };
    auto u32 = [&](std::size_t at) {
        return static_cast<std::uint32_t>(bytes[at] | (bytes[at + 1] << 8) | (bytes[at + 2] << 16)) |
               (static_cast<std::uint32_t>(bytes[at + 3]) << 24);
    };
    if (bytes.size() < 2) return fail("too short");
    const std::size_t headerEnd = u16(0);
    if (headerEnd > bytes.size()) return fail("header runs past the end");
    TextResource out;
    for (std::size_t at = 2; at + 6 <= headerEnd; at += 6) {
        const auto id = u16(at);
        if (id == kLastEntry) break;
        const std::size_t start = u32(at + 2);
        if (start > bytes.size()) return fail("record " + std::to_string(id) + " starts past the end");
        std::size_t end = start;
        while (end < bytes.size() && bytes[end] != kEndOfRecord) ++end;
        out.records_[id] = plain(bytes.subspan(start, end - start));
    }
    return out;
}

std::optional<std::string> TextResource::text(std::uint16_t id) const {
    const auto it = records_.find(id);
    if (it == records_.end()) return std::nullopt;
    return it->second;
}

std::vector<std::uint16_t> TextResource::idsBetween(std::uint16_t first, std::uint16_t last) const {
    std::vector<std::uint16_t> out;
    for (auto it = records_.lower_bound(first); it != records_.end() && it->first <= last; ++it) out.push_back(it->first);
    return out;
}

} // namespace de::formats
