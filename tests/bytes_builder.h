#pragma once

#include <cstdint>
#include <cstring>
#include <string_view>
#include <vector>

namespace de::test {

/// Builds little-endian test inputs.
class Bytes {
public:
    std::vector<std::uint8_t> data;

    Bytes& u8(std::uint8_t v) { data.push_back(v); return *this; }
    Bytes& u16(std::uint16_t v) { return raw(&v, 2); }
    Bytes& i16(std::int16_t v) { return raw(&v, 2); }
    Bytes& u32(std::uint32_t v) { return raw(&v, 4); }
    Bytes& i32(std::int32_t v) { return raw(&v, 4); }
    Bytes& text(std::string_view s, std::size_t width) {
        for (std::size_t i = 0; i < width; ++i) data.push_back(i < s.size() ? static_cast<std::uint8_t>(s[i]) : 0);
        return *this;
    }
    Bytes& zeros(std::size_t n) { data.insert(data.end(), n, 0); return *this; }
    [[nodiscard]] std::size_t size() const { return data.size(); }

    /// Overwrite 4 bytes at `at` (for offsets known only later).
    void patchI32(std::size_t at, std::int32_t v) { std::memcpy(data.data() + at, &v, 4); }
    void patchU32(std::size_t at, std::uint32_t v) { std::memcpy(data.data() + at, &v, 4); }

private:
    Bytes& raw(const void* p, std::size_t n) {
        const auto* b = static_cast<const std::uint8_t*>(p);
        data.insert(data.end(), b, b + n);
        return *this;
    }
};

} // namespace de::test
