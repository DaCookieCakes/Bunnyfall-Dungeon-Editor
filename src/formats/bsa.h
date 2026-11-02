#pragma once

#include "core/byte_view.h"
#include "core/error.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace de::formats {

/// A BSA archive (BLOCKS.BSA, ARCH3D.BSA, MAPS.BSA, ...). Records are stored back to back after a 4-byte header, and
/// the directory sits at the end of the file: either names (14-byte field + size) or numeric ids (id + size).
class BsaArchive {
public:
    enum class DirectoryType : std::uint16_t { Names = 0x0100, Numbers = 0x0200 };

    struct Entry {
        std::string name;      ///< Names directories; for Numbers, the id in decimal.
        std::uint32_t id = 0;  ///< Numbers directories only.
        std::uint32_t offset = 0;  ///< Byte offset of the record in the file.
        std::uint32_t size = 0;
    };

    static Result<BsaArchive> load(const std::filesystem::path& path);
    static Result<BsaArchive> fromBytes(std::vector<std::uint8_t> bytes);

    [[nodiscard]] DirectoryType directoryType() const { return type_; }
    [[nodiscard]] std::size_t count() const { return entries_.size(); }
    [[nodiscard]] const Entry& entry(std::size_t index) const { return entries_.at(index); }
    [[nodiscard]] const std::vector<Entry>& entries() const { return entries_; }

    /// The record's bytes, as a view into the archive (valid while the archive lives).
    [[nodiscard]] ByteView record(std::size_t index) const;

    [[nodiscard]] std::optional<std::size_t> findByName(std::string_view name) const;
    [[nodiscard]] std::optional<std::size_t> findById(std::uint32_t id) const;

    /// Total size of the archive file in bytes.
    [[nodiscard]] std::size_t fileSize() const { return bytes_.size(); }

private:
    std::vector<std::uint8_t> bytes_;
    DirectoryType type_ = DirectoryType::Names;
    std::vector<Entry> entries_;
};

} // namespace de::formats
