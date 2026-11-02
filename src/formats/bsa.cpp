#include "formats/bsa.h"

#include "core/file.h"

#include <algorithm>

namespace de::formats {

namespace {
constexpr std::size_t kHeaderSize = 4;
constexpr std::size_t kNameEntrySize = 18;
constexpr std::size_t kNameFieldSize = 14;
constexpr std::size_t kNumberEntrySize = 8;
} // namespace

Result<BsaArchive> BsaArchive::load(const std::filesystem::path& path) {
    auto bytes = readFile(path);
    if (!bytes) return std::unexpected(bytes.error());
    auto archive = fromBytes(std::move(*bytes));
    if (!archive) return fail(path.filename().string() + ": " + archive.error().message);
    return archive;
}

Result<BsaArchive> BsaArchive::fromBytes(std::vector<std::uint8_t> bytes) {
    BsaArchive archive;
    archive.bytes_ = std::move(bytes);
    const ByteView view(archive.bytes_);
    try {
        const auto count = view.i16(0);
        if (count < 0) return fail("negative record count");
        const auto rawType = view.u16(2);
        std::size_t entrySize = 0;
        if (rawType == static_cast<std::uint16_t>(DirectoryType::Names)) {
            archive.type_ = DirectoryType::Names;
            entrySize = kNameEntrySize;
        } else if (rawType == static_cast<std::uint16_t>(DirectoryType::Numbers)) {
            archive.type_ = DirectoryType::Numbers;
            entrySize = kNumberEntrySize;
        } else {
            return fail("unknown directory type " + std::to_string(rawType));
        }

        const auto directorySize = entrySize * static_cast<std::size_t>(count);
        if (directorySize + kHeaderSize > view.size()) return fail("directory larger than the file");
        const auto directoryStart = view.size() - directorySize;

        archive.entries_.reserve(static_cast<std::size_t>(count));
        std::size_t recordOffset = kHeaderSize;
        for (std::size_t i = 0; i < static_cast<std::size_t>(count); ++i) {
            const auto at = directoryStart + i * entrySize;
            Entry entry;
            std::int32_t size = 0;
            if (archive.type_ == DirectoryType::Names) {
                entry.name = view.fixedString(at, kNameFieldSize);
                size = view.i32(at + kNameFieldSize);
            } else {
                entry.id = view.u32(at);
                entry.name = std::to_string(entry.id);
                size = view.i32(at + 4);
            }
            if (size < 0) return fail("record " + std::to_string(i) + " has a negative size");
            if (recordOffset + static_cast<std::size_t>(size) > directoryStart) {
                return fail("record " + std::to_string(i) + " runs into the directory");
            }
            entry.offset = static_cast<std::uint32_t>(recordOffset);
            entry.size = static_cast<std::uint32_t>(size);
            recordOffset += static_cast<std::size_t>(size);
            archive.entries_.push_back(std::move(entry));
        }
    } catch (const OutOfBounds& e) {
        return fail(std::string("truncated: ") + e.what());
    }
    return archive;
}

ByteView BsaArchive::record(std::size_t index) const {
    const auto& e = entries_.at(index);
    return ByteView(bytes_).sub(e.offset, e.size);
}

std::optional<std::size_t> BsaArchive::findByName(std::string_view name) const {
    const auto it = std::ranges::find(entries_, name, &Entry::name);
    if (it == entries_.end()) return std::nullopt;
    return static_cast<std::size_t>(it - entries_.begin());
}

std::optional<std::size_t> BsaArchive::findById(std::uint32_t id) const {
    if (type_ != DirectoryType::Numbers) return std::nullopt;
    const auto it = std::ranges::find(entries_, id, &Entry::id);
    if (it == entries_.end()) return std::nullopt;
    return static_cast<std::size_t>(it - entries_.begin());
}

} // namespace de::formats
