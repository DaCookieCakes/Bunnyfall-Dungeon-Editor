#include "formats/texture.h"

#include "core/file.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>

namespace de::formats {

namespace {
constexpr int kSolidSize = 32;
constexpr std::size_t kRecordHeadersAt = 26;
constexpr std::size_t kRecordHeaderSize = 20;
constexpr std::int16_t kRecordRle = 0x1108;
constexpr std::int16_t kImageRle = 0x0108;
constexpr std::uint16_t kRowEncodingRle = 0x8000;
constexpr int kUncompressedStride = 256;
// Classic records are at most 256 on a side; this only stops corrupt headers from allocating absurd images.
constexpr int kMaxSide = 4096;
} // namespace

Result<Palette> Palette::fromBytes(ByteView bytes) {
    std::size_t header = 0;
    if (bytes.size() == 776) header = 8;
    else if (bytes.size() != 768) return fail("palette of " + std::to_string(bytes.size()) + " bytes");
    Palette p;
    for (std::size_t i = 0; i < 256; ++i) {
        for (std::size_t c = 0; c < 3; ++c) p.rgb[i][c] = bytes.u8(header + i * 3 + c);
    }
    return p;
}

Result<Palette> Palette::load(const std::filesystem::path& path) {
    auto bytes = readFile(path);
    if (!bytes) return std::unexpected(bytes.error());
    auto p = fromBytes(ByteView(*bytes));
    if (!p) return fail(path.filename().string() + ": " + p.error().message);
    return p;
}

std::vector<std::uint8_t> toRgba(const IndexedImage& image, const Palette& palette, bool transparentZero) {
    std::vector<std::uint8_t> out(image.pixels.size() * 4);
    for (std::size_t i = 0; i < image.pixels.size(); ++i) {
        const auto index = image.pixels[i];
        const auto& c = palette.rgb[index];
        out[i * 4 + 0] = c[0];
        out[i * 4 + 1] = c[1];
        out[i * 4 + 2] = c[2];
        out[i * 4 + 3] = (transparentZero && index == 0) ? 0 : 255;
    }
    return out;
}

std::string TextureArchive::fileName(int archive) {
    char name[16];
    std::snprintf(name, sizeof name, "TEXTURE.%03d", archive);
    return name;
}

std::vector<TextureArchiveEntry> listTextureArchives(const std::filesystem::path& arena2Dir) {
    std::vector<TextureArchiveEntry> out;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(arena2Dir, ec)) {
        const auto name = entry.path().filename().string();
        if (name.size() != 11 || name[7] != '.') continue;
        std::string head = name.substr(0, 7);
        for (auto& c : head) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        if (head != "TEXTURE" || !std::all_of(name.begin() + 8, name.end(), [](char c) { return c >= '0' && c <= '9'; })) continue;
        TextureArchiveEntry e;
        e.archive = std::stoi(name.substr(8));
        // The header: a record count (2 bytes) then a 24-character description.
        std::ifstream in(entry.path(), std::ios::binary);
        char header[26] = {};
        if (in.read(header, sizeof header)) {
            e.description.assign(header + 2, 24);
            const auto end = e.description.find_last_not_of(std::string_view(" \0", 2));
            e.description.erase(end == std::string::npos ? 0 : end + 1);
            for (auto& c : e.description) {
                if (static_cast<unsigned char>(c) < 32 || static_cast<unsigned char>(c) > 126) c = ' ';
            }
            const auto start = e.description.find_first_not_of(' ');
            e.description.erase(0, start == std::string::npos ? e.description.size() : start);
        }
        out.push_back(std::move(e));
    }
    std::ranges::sort(out, {}, &TextureArchiveEntry::archive);
    return out;
}

Result<TextureArchive> TextureArchive::load(const std::filesystem::path& arena2Dir, int archive) {
    const auto name = fileName(archive);
    auto path = findArena2File(arena2Dir, name);
    if (!path) return std::unexpected(path.error());
    auto bytes = readFile(*path);
    if (!bytes) return std::unexpected(bytes.error());
    auto t = fromBytes(std::move(*bytes), archive);
    if (!t) return fail(name + ": " + t.error().message);
    return t;
}

Result<TextureArchive> TextureArchive::fromBytes(std::vector<std::uint8_t> bytes, int archive) {
    TextureArchive t;
    t.bytes_ = std::move(bytes);
    t.archive_ = archive;
    const ByteView v(t.bytes_);
    try {
        const auto count = v.i16(0);
        if (count < 0) return fail("negative record count");
        t.description_ = v.fixedString(2, 24);
        while (!t.description_.empty() && t.description_.back() == ' ') t.description_.pop_back();
        t.description_.erase(0, t.description_.find_first_not_of(' ') == std::string::npos ? t.description_.size()
                                                                                         : t.description_.find_first_not_of(' '));
        t.records_.reserve(static_cast<std::size_t>(count));
        for (std::size_t i = 0; i < static_cast<std::size_t>(count); ++i) {
            const auto header = kRecordHeadersAt + i * kRecordHeaderSize;
            const auto pos = v.i32(header + 2);
            if (pos < 0) return fail("record " + std::to_string(i) + " has a negative position");
            const auto at = static_cast<std::size_t>(pos);
            Record r;
            r.position = static_cast<std::uint32_t>(at);
            r.offsetX = v.i16(at);
            r.offsetY = v.i16(at + 2);
            r.width = v.i16(at + 4);
            r.height = v.i16(at + 6);
            r.compression = v.i16(at + 8);
            r.dataOffset = v.u32(at + 14);
            r.frameCount = v.u16(at + 20);
            r.scaleX = v.i16(at + 24);
            r.scaleY = v.i16(at + 26);
            if (archive == 0 || archive == 1) r.width = r.height = kSolidSize;
            t.records_.push_back(r);
        }
    } catch (const OutOfBounds& e) {
        return fail(std::string("truncated: ") + e.what());
    }
    return t;
}

Result<IndexedImage> TextureArchive::frame(std::size_t record, std::size_t frame) const {
    if (record >= records_.size()) return fail("no record " + std::to_string(record));
    if (archive_ == 0 || archive_ == 1) return readSolid(record);
    const auto& r = records_[record];
    if (r.width <= 0 || r.height <= 0 || r.width > kMaxSide || r.height > kMaxSide) {
        return fail("record " + std::to_string(record) + " has size " + std::to_string(r.width) + "x" +
                    std::to_string(r.height));
    }
    if (frame >= r.frameCount) return fail("record " + std::to_string(record) + " has no frame " + std::to_string(frame));
    try {
        if (r.compression == kRecordRle || r.compression == kImageRle) return readRle(r, frame);
        return readUncompressed(r, frame);
    } catch (const OutOfBounds& e) {
        return fail("record " + std::to_string(record) + " truncated: " + e.what());
    }
}

Result<IndexedImage> TextureArchive::readSolid(std::size_t record) const {
    // TEXTURE.000 is palette index = record; TEXTURE.001 is 128 + record.
    const auto index = static_cast<std::uint8_t>((archive_ == 0 ? record : 128 + record) & 0xff);
    return IndexedImage{kSolidSize, kSolidSize, std::vector<std::uint8_t>(kSolidSize * kSolidSize, index)};
}

Result<IndexedImage> TextureArchive::readUncompressed(const Record& r, std::size_t frame) const {
    const ByteView v(bytes_);
    IndexedImage img{r.width, r.height, std::vector<std::uint8_t>(static_cast<std::size_t>(r.width * r.height))};
    const std::size_t start = std::size_t{r.position} + r.dataOffset;
    const auto w = static_cast<std::size_t>(r.width), h = static_cast<std::size_t>(r.height);

    if (r.frameCount == 1) {
        // Rows on a fixed 256-byte stride.
        for (std::size_t y = 0; y < h; ++y) {
            const auto row = v.sub(start + y * kUncompressedStride, w).bytes();
            std::copy(row.begin(), row.end(), img.pixels.begin() + static_cast<std::ptrdiff_t>(y * w));
        }
        return img;
    }

    // Animated: a frame offset table, then per frame its size and runs of (transparent count, opaque count, bytes).
    std::size_t src = start + static_cast<std::size_t>(v.i32(start + frame * 4));
    const auto cx = static_cast<std::size_t>(std::max<std::int16_t>(0, v.i16(src)));
    const auto cy = static_cast<std::size_t>(std::max<std::int16_t>(0, v.i16(src + 2)));
    src += 4;
    std::size_t dst = 0;
    for (std::size_t y = 0; y < cy; ++y) {
        std::size_t x = 0;
        while (x < cx) {
            const std::size_t clear = v.u8(src++);
            for (std::size_t end = x + clear; x < end; ++x) {
                if (dst < img.pixels.size()) img.pixels[dst] = 0;
                ++dst;
            }
            const std::size_t solid = v.u8(src++);
            for (std::size_t end = x + solid; x < end; ++x) {
                const auto px = v.u8(src++);
                if (dst < img.pixels.size()) img.pixels[dst] = px;
                ++dst;
            }
        }
    }
    return img;
}

Result<IndexedImage> TextureArchive::readRle(const Record& r, std::size_t frame) const {
    const ByteView v(bytes_);
    IndexedImage img{r.width, r.height, std::vector<std::uint8_t>(static_cast<std::size_t>(r.width * r.height))};
    const auto w = static_cast<std::size_t>(r.width), h = static_cast<std::size_t>(r.height);
    std::size_t rowHeaders = std::size_t{r.position} + r.dataOffset + h * frame * 4;
    std::size_t dst = 0;
    auto put = [&](std::uint8_t px) {
        if (dst >= img.pixels.size()) throw OutOfBounds("RLE row runs past the image");
        img.pixels[dst++] = px;
    };
    for (std::size_t y = 0; y < h; ++y, rowHeaders += 4) {
        const auto rowOffset = v.i16(rowHeaders);   // signed in the format; classic data never goes negative
        if (rowOffset < 0) throw OutOfBounds("negative RLE row offset");
        std::size_t src = std::size_t{r.position} + static_cast<std::size_t>(rowOffset);
        const auto encoding = v.u16(rowHeaders + 2);
        if (encoding != kRowEncodingRle) {
            const auto row = v.sub(src, w).bytes();
            for (const auto px : row) put(px);
            continue;
        }
        const std::size_t rowWidth = v.u16(src);
        src += 2;
        std::size_t done = 0;
        while (done < rowWidth) {
            const auto probe = v.i16(src);
            src += 2;
            if (probe < 0) {
                const auto px = v.u8(src++);
                for (int i = 0; i < -probe; ++i) put(px);
                done += static_cast<std::size_t>(-probe);
            } else if (probe > 0) {
                for (int i = 0; i < probe; ++i) put(v.u8(src++));
                done += static_cast<std::size_t>(probe);
            } else {
                break;   // a zero probe would never advance
            }
        }
    }
    return img;
}

} // namespace de::formats
