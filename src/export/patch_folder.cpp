#include "export/patch_folder.h"

#include "export/world_data_patch.h"

#include <fstream>

namespace de::exporter {

bool PatchFolder::has(const std::string& blockName) const {
    std::error_code ec;
    return std::filesystem::is_regular_file(fileFor(blockName), ec);
}

Result<std::optional<formats::RdbBlock>> PatchFolder::load(const formats::BsaArchive& blocks, std::size_t bsaIndex) const {
    if (bsaIndex >= blocks.count()) return fail("no block record " + std::to_string(bsaIndex));
    const auto& name = blocks.entry(bsaIndex).name;
    if (!has(name)) return std::optional<formats::RdbBlock>{};
    std::ifstream in(fileFor(name));
    const auto patch = Json::parse(in, nullptr, false);
    if (patch.is_discarded()) return fail(fileFor(name).string() + " is not JSON");
    auto json = rebuildBlockPatch(patch, blocks);
    if (!json) return fail(fileFor(name).filename().string() + ": " + json.error().message);
    auto block = rdbBlockFromJson(*json);
    if (!block) return fail(fileFor(name).filename().string() + ": " + block.error().message);
    return std::optional(std::move(*block));
}

Result<PatchFolder::Saved> PatchFolder::save(const formats::BsaArchive& blocks, std::size_t bsaIndex,
                                             const formats::RdbBlock& block) const {
    if (bsaIndex >= blocks.count()) return fail("no block record " + std::to_string(bsaIndex));
    const auto& entry = blocks.entry(bsaIndex);
    const auto json = rdbBlockToJson(block, entry.name, bsaIndex, entry.offset);
    auto patch = makeBlockPatch(blocks, bsaIndex, json, entry.name + ".json");
    if (!patch) return std::unexpected(patch.error());

    std::error_code ec;
    if ((*patch)["ops"].empty()) {
        std::filesystem::remove(fileFor(entry.name), ec);
        if (ec) return fail("cannot remove " + fileFor(entry.name).string() + ": " + ec.message());
        return Saved::Removed;
    }
    std::filesystem::create_directories(dir_, ec);
    if (ec) return fail("cannot create " + dir_.string() + ": " + ec.message());
    // Written beside the target and renamed over it, so a failed write never leaves half a patch for the game.
    const auto target = fileFor(entry.name);
    const auto temp = target.string() + ".tmp";
    {
        std::ofstream out(temp);
        out << patch->dump(1) << "\n";
        if (!out) return fail("cannot write " + temp);
    }
    std::filesystem::rename(temp, target, ec);
    if (ec) return fail("cannot replace " + target.string() + ": " + ec.message());
    return Saved::Written;
}

} // namespace de::exporter
