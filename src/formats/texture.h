#pragma once

#include "core/byte_view.h"
#include "core/error.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace de::formats {

/// A 256-colour palette. Texture archives use ART_PAL.COL.
struct Palette {
    std::array<std::array<std::uint8_t, 3>, 256> rgb{};

    /// .PAL (768 bytes) or .COL (776 bytes: an 8-byte header first).
    static Result<Palette> fromBytes(ByteView bytes);
    static Result<Palette> load(const std::filesystem::path& path);
};

/// One decoded frame: palette indices, row 0 at the top.
struct IndexedImage {
    int width = 0, height = 0;
    std::vector<std::uint8_t> pixels;
};

/// RGBA8, row 0 at the top. Palette index 0 becomes transparent when `transparentZero` is set (flats), else opaque.
std::vector<std::uint8_t> toRgba(const IndexedImage& image, const Palette& palette, bool transparentZero);

/// A TEXTURE.nnn archive: records, each with one or more frames.
class TextureArchive {
public:
    struct Record {
        std::int16_t offsetX = 0, offsetY = 0;
        std::int16_t width = 0, height = 0;
        std::int16_t compression = 0;
        std::uint32_t position = 0;     ///< Start of the record in the file.
        std::uint32_t dataOffset = 0;   ///< Relative to `position`.
        std::uint16_t frameCount = 0;
        std::int16_t scaleX = 0, scaleY = 0;
    };

    /// `archive` is the nnn of TEXTURE.nnn; 0 and 1 are the solid-colour archives.
    static Result<TextureArchive> fromBytes(std::vector<std::uint8_t> bytes, int archive);
    static Result<TextureArchive> load(const std::filesystem::path& arena2Dir, int archive);

    [[nodiscard]] int archive() const { return archive_; }
    [[nodiscard]] const std::string& description() const { return description_; }
    [[nodiscard]] std::size_t recordCount() const { return records_.size(); }
    [[nodiscard]] const Record& record(std::size_t index) const { return records_.at(index); }

    /// Decodes one frame of one record.
    [[nodiscard]] Result<IndexedImage> frame(std::size_t record, std::size_t frame = 0) const;

    /// The archive's file name, e.g. "TEXTURE.120".
    static std::string fileName(int archive);

private:
    Result<IndexedImage> readSolid(std::size_t record) const;
    Result<IndexedImage> readUncompressed(const Record& r, std::size_t frame) const;
    Result<IndexedImage> readRle(const Record& r, std::size_t frame) const;

    std::vector<std::uint8_t> bytes_;
    int archive_ = 0;
    std::string description_;
    std::vector<Record> records_;
};

} // namespace de::formats
