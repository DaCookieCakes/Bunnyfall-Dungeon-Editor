#pragma once

// TEXT.RSC: Daggerfall's numbered messages. Dungeon actions show some of them (§2.6 of DESIGN.md): "show text"
// shows record 8600 + index, "show text with input" 5400 + index, and a door's text 7700 + index.

#include "core/error.h"

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace de::formats {

class TextResource {
public:
    static Result<TextResource> load(const std::filesystem::path& path);
    static Result<TextResource> fromBytes(std::span<const std::uint8_t> bytes);

    /// A record as plain UTF-8: line and page breaks become '\n', layout codes are dropped, trailing breaks trimmed.
    [[nodiscard]] std::optional<std::string> text(std::uint16_t id) const;
    /// Every record id in [first, last], in order.
    [[nodiscard]] std::vector<std::uint16_t> idsBetween(std::uint16_t first, std::uint16_t last) const;

private:
    std::map<std::uint16_t, std::string> records_;
};

} // namespace de::formats
