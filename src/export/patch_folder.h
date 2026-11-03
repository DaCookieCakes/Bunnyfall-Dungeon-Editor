#pragma once

// A folder of block patches: Bunnyfall's vendor/<mod>/WorldDataPatches/, one `<NAME>.RDB.json` per edited block.

#include "core/error.h"
#include "formats/bsa.h"
#include "formats/rdb.h"

#include <filesystem>
#include <optional>

namespace de::exporter {

class PatchFolder {
public:
    explicit PatchFolder(std::filesystem::path dir) : dir_(std::move(dir)) {}

    [[nodiscard]] const std::filesystem::path& dir() const { return dir_; }
    [[nodiscard]] std::filesystem::path fileFor(const std::string& blockName) const { return dir_ / (blockName + ".json"); }
    [[nodiscard]] bool has(const std::string& blockName) const;

    /// The block a saved patch rebuilds, or nothing when there is no patch for it.
    Result<std::optional<formats::RdbBlock>> load(const formats::BsaArchive& blocks, std::size_t bsaIndex) const;

    enum class Saved { Written, Removed };
    /// Writes the patch that turns the classic block at `bsaIndex` into `block` (checked by rebuilding it first). A
    /// block that no longer differs from the classic one has its patch file removed instead.
    Result<Saved> save(const formats::BsaArchive& blocks, std::size_t bsaIndex, const formats::RdbBlock& block) const;

private:
    std::filesystem::path dir_;
};

} // namespace de::exporter
