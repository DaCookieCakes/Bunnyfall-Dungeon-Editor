#pragma once

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

namespace de {

/// Thrown by ByteView on any read outside its bytes. Parsers catch it at their public boundary and turn it into an
/// Error, so malformed input never reads out of bounds and never escapes as an exception.
class OutOfBounds : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

/// A read-only, bounds-checked view over little-endian bytes. Offsets are relative to the start of the view.
class ByteView {
public:
    ByteView() = default;
    explicit ByteView(std::span<const std::uint8_t> bytes) : bytes_(bytes) {}

    [[nodiscard]] std::size_t size() const { return bytes_.size(); }
    [[nodiscard]] bool contains(std::size_t offset, std::size_t length) const {
        return offset <= bytes_.size() && length <= bytes_.size() - offset;
    }

    [[nodiscard]] ByteView sub(std::size_t offset, std::size_t length) const {
        require(offset, length);
        return ByteView(bytes_.subspan(offset, length));
    }

    [[nodiscard]] std::uint8_t u8(std::size_t offset) const { return read<std::uint8_t>(offset); }
    [[nodiscard]] std::int8_t i8(std::size_t offset) const { return read<std::int8_t>(offset); }
    [[nodiscard]] std::uint16_t u16(std::size_t offset) const { return read<std::uint16_t>(offset); }
    [[nodiscard]] std::int16_t i16(std::size_t offset) const { return read<std::int16_t>(offset); }
    [[nodiscard]] std::uint32_t u32(std::size_t offset) const { return read<std::uint32_t>(offset); }
    [[nodiscard]] std::int32_t i32(std::size_t offset) const { return read<std::int32_t>(offset); }

    /// A fixed-width text field: the bytes up to the first NUL (or the whole field), as Latin-1 kept byte for byte.
    [[nodiscard]] std::string fixedString(std::size_t offset, std::size_t width) const {
        require(offset, width);
        const auto field = bytes_.subspan(offset, width);
        const auto end = std::find(field.begin(), field.end(), std::uint8_t{0});
        return std::string(field.begin(), end);
    }

    [[nodiscard]] std::span<const std::uint8_t> bytes() const { return bytes_; }

private:
    void require(std::size_t offset, std::size_t length) const {
        if (!contains(offset, length)) {
            throw OutOfBounds("read of " + std::to_string(length) + " bytes at " + std::to_string(offset) +
                              " outside " + std::to_string(bytes_.size()) + " bytes");
        }
    }

    template <typename T>
    [[nodiscard]] T read(std::size_t offset) const {
        static_assert(std::endian::native == std::endian::little, "big-endian hosts need byte swapping here");
        require(offset, sizeof(T));
        T value;
        std::memcpy(&value, bytes_.data() + offset, sizeof(T));
        return value;
    }

    std::span<const std::uint8_t> bytes_;
};

} // namespace de
