#include "project/project.h"

#include "export/dfu_json.h"
#include "mesh/mesh_json.h"

#include <algorithm>
#include <cstdio>
#include <format>
#include <fstream>

namespace de::project {

namespace {

using exporter::Json;

constexpr std::string_view kFormat = "bunnyfall-dungeon-editor-project/1";
constexpr std::string_view kRoleLetters = "NWSMB";

std::string makeBlockName(char letter, int number) {
    char text[32];
    std::snprintf(text, sizeof text, "%cBF%05d.RDB", letter, number);
    return text;
}

} // namespace

std::uint32_t ProjectDungeon::mapId() const {
    return (static_cast<std::uint32_t>(kFirstMapIdHigh + number) << 20) | static_cast<std::uint32_t>(pixelId());
}
int ProjectDungeon::globalNumber() const { return idRange * kDungeonsPerRange + number; }
std::uint16_t ProjectDungeon::exteriorLocationId() const { return static_cast<std::uint16_t>(kFirstLocationId + 2 * globalNumber()); }
std::uint16_t ProjectDungeon::dungeonLocationId() const { return static_cast<std::uint16_t>(kFirstLocationId + 2 * globalNumber() + 1); }

std::optional<std::string> modFolderProblem(std::string_view folder) {
    if (folder.empty()) return "a mod needs a folder name";
    if (folder.size() > 64) return "the folder name is longer than 64 letters";
    if (folder.front() == '-') return "the folder name cannot start with -";
    for (const char c : folder) {
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-')) {
            return "the folder name may only have a-z, 0-9 and -";
        }
    }
    return std::nullopt;
}

Result<void> Project::setIdRange(int range) {
    if (range < 0 || range >= kIdRanges) return fail(std::format("id ranges go from 0 to {}", kIdRanges - 1));
    if (range == mod.idRange) return {};
    if (numbersIssued()) return fail("the project has handed out numbers from its id range already");
    mod.idRange = range;
    return {};
}

const DungeonCell* ProjectDungeon::cellAt(int x, int z) const {
    const auto it = std::ranges::find_if(cells, [&](const DungeonCell& c) { return c.x == x && c.z == z; });
    return it == cells.end() ? nullptr : &*it;
}

const DungeonCell* ProjectDungeon::startingCell() const {
    const auto it = std::ranges::find_if(cells, &DungeonCell::isStartingBlock);
    return it == cells.end() ? nullptr : &*it;
}

formats::RdbBlock emptyBlock() {
    formats::RdbBlock b;
    b.width = b.height = 4;
    b.roots.resize(16);
    std::string unusedId;
    for (int i = 0; i < 5; ++i) unusedId += "\xEF\xBF\xBD";
    b.modelRefs.assign(formats::kRdbModelRefSlots, {unusedId, 0, std::string(formats::kUnusedModelDescription)});
    return b;
}

bool isProjectBlockName(std::string_view name) {
    return name.size() == 12 && kRoleLetters.find(name[0]) != std::string_view::npos && name.substr(1, 2) == "BF" &&
           std::all_of(name.begin() + 3, name.begin() + 8, [](char c) { return c >= '0' && c <= '9'; }) &&
           name.substr(8) == ".RDB";
}

CustomBlock* Project::block(std::string_view blockName) {
    const auto it = std::ranges::find(blocks_, blockName, &CustomBlock::name);
    return it == blocks_.end() ? nullptr : &*it;
}
const CustomBlock* Project::block(std::string_view blockName) const {
    const auto it = std::ranges::find(blocks_, blockName, &CustomBlock::name);
    return it == blocks_.end() ? nullptr : &*it;
}

ProjectDungeon* Project::dungeon(int number) {
    const auto it = std::ranges::find(dungeons_, number, &ProjectDungeon::number);
    return it == dungeons_.end() ? nullptr : &*it;
}

CustomModel* Project::model(std::uint32_t id) {
    const auto it = std::ranges::find(models_, id, &CustomModel::id);
    return it == models_.end() ? nullptr : &*it;
}
const CustomModel* Project::model(std::uint32_t id) const {
    const auto it = std::ranges::find(models_, id, &CustomModel::id);
    return it == models_.end() ? nullptr : &*it;
}

Result<CustomModel*> Project::addModel(std::string label, std::string description, mesh::Mesh mesh) {
    if (modelsIssued_ >= kModelsPerRange) return fail("the project has used all its model numbers");
    CustomModel m;
    m.id = kFirstCustomModelId + static_cast<std::uint32_t>(mod.idRange * kModelsPerRange + ++modelsIssued_);
    m.label = std::move(label);
    m.description = description.substr(0, 3);
    m.mesh = std::move(mesh);
    models_.push_back(std::move(m));
    return &models_.back();
}

bool Project::removeModel(std::uint32_t id) {
    return std::erase_if(models_, [&](const CustomModel& m) { return m.id == id; }) > 0;
}

Result<CustomBlock*> Project::addBlock(char letter, std::string label, std::optional<std::string> templateName,
                                       formats::RdbBlock data) {
    if (kRoleLetters.find(letter) == std::string_view::npos) return fail(std::string("no block role '") + letter + "'");
    if (blocksIssued_ >= kBlocksPerRange) return fail("the project has used all its block numbers");
    const int number = mod.idRange * kBlocksPerRange + ++blocksIssued_;
    CustomBlock b;
    b.name = makeBlockName(letter, number);
    b.label = std::move(label);
    b.templateName = std::move(templateName);
    b.savePosition = kFirstBlockSavePosition + static_cast<std::uint32_t>(number) * kBlockSaveStride;
    b.data = std::move(data);
    blocks_.push_back(std::move(b));
    return &blocks_.back();
}

Result<ProjectDungeon*> Project::addDungeon(std::string placeName) {
    if (dungeonsIssued_ >= kDungeonsPerRange) return fail("the project has used all its dungeon numbers (148)");
    ProjectDungeon d;
    d.number = dungeonsIssued_++;
    d.idRange = mod.idRange;
    d.name = std::move(placeName);
    dungeons_.push_back(std::move(d));
    return &dungeons_.back();
}

bool Project::removeBlock(std::string_view blockName) {
    return std::erase_if(blocks_, [&](const CustomBlock& b) { return b.name == blockName; }) > 0;
}

bool Project::removeDungeon(int number) {
    return std::erase_if(dungeons_, [&](const ProjectDungeon& d) { return d.number == number; }) > 0;
}

Result<void> Project::save(const std::filesystem::path& file) const {
    Json j = {{"format", kFormat},
              {"name", name},
              {"mod", {{"folder", mod.folder}, {"author", mod.author}, {"description", mod.description}, {"idRange", mod.idRange}}},
              {"blocksIssued", blocksIssued_},
              {"dungeonsIssued", dungeonsIssued_},
              {"modelsIssued", modelsIssued_}};
    Json blocks = Json::array();
    for (const auto& b : blocks_) {
        blocks.push_back({
            {"name", b.name},
            {"label", b.label},
            {"template", b.templateName ? Json(*b.templateName) : Json()},
            {"savePosition", b.savePosition},
            {"block", exporter::rdbBlockToJson(b.data, b.name, 0, b.savePosition)},
        });
    }
    j["blocks"] = std::move(blocks);
    Json dungeons = Json::array();
    for (const auto& d : dungeons_) {
        Json cells = Json::array();
        for (const auto& c : d.cells) cells.push_back({{"x", c.x}, {"z", c.z}, {"block", c.blockName}, {"start", c.isStartingBlock}});
        dungeons.push_back({
            {"number", d.number},
            {"name", d.name},
            {"region", d.regionIndex},
            {"pixel", {d.pixelX, d.pixelY}},
            {"dungeonType", d.dungeonType},
            {"locationType", static_cast<int>(d.locationType)},
            {"entrance", d.entrance},
            {"discovered", d.discovered},
            {"cells", std::move(cells)},
        });
    }
    j["dungeons"] = std::move(dungeons);
    Json models = Json::array();
    for (const auto& m : models_) {
        models.push_back({{"id", m.id}, {"label", m.label}, {"description", m.description}, {"mesh", mesh::meshToJson(m.mesh)}});
    }
    j["models"] = std::move(models);

    std::error_code ec;
    if (file.has_parent_path()) std::filesystem::create_directories(file.parent_path(), ec);
    const auto temp = file.string() + ".tmp";
    {
        std::ofstream out(temp);
        out << j.dump(1) << "\n";
        if (!out) return fail("cannot write " + temp);
    }
    std::filesystem::rename(temp, file, ec);
    if (ec) return fail("cannot replace " + file.string() + ": " + ec.message());
    return {};
}

Result<Project> Project::load(const std::filesystem::path& file) {
    std::ifstream in(file);
    if (!in) return fail("cannot open " + file.string());
    const auto j = Json::parse(in, nullptr, false);
    if (j.is_discarded() || !j.is_object()) return fail(file.string() + " is not JSON");
    if (j.value("format", "") != kFormat) return fail(file.string() + " is not a dungeon editor project");
    Project p;
    p.name = j.value("name", p.name);
    if (const auto m = j.value("mod", Json::object()); m.is_object()) {
        p.mod.folder = m.value("folder", p.mod.folder);
        p.mod.author = m.value("author", p.mod.author);
        p.mod.description = m.value("description", "");
        p.mod.idRange = m.value("idRange", 0);
    }
    if (const auto problem = modFolderProblem(p.mod.folder)) return fail(file.string() + ": " + *problem);
    if (p.mod.idRange < 0 || p.mod.idRange >= kIdRanges) return fail(file.string() + ": bad id range");
    const int range = p.mod.idRange;
    p.blocksIssued_ = j.value("blocksIssued", 0);
    p.dungeonsIssued_ = j.value("dungeonsIssued", 0);
    p.modelsIssued_ = j.value("modelsIssued", 0);
    for (const auto& b : j.value("blocks", Json::array())) {
        CustomBlock block;
        block.name = b.value("name", "");
        if (!isProjectBlockName(block.name)) return fail(file.string() + ": bad block name '" + block.name + "'");
        block.label = b.value("label", "");
        if (b.contains("template") && b["template"].is_string()) block.templateName = b["template"].get<std::string>();
        block.savePosition = b.value("savePosition", 0u);
        auto data = exporter::rdbBlockFromJson(b.value("block", Json::object()));
        if (!data) return fail(file.string() + ": " + block.name + ": " + data.error().message);
        block.data = std::move(*data);
        p.blocks_.push_back(std::move(block));
    }
    for (const auto& d : j.value("dungeons", Json::array())) {
        ProjectDungeon dungeon;
        dungeon.number = d.value("number", -1);
        if (dungeon.number < 0 || dungeon.number >= kMaxDungeons) return fail(file.string() + ": bad dungeon number");
        dungeon.idRange = range;
        dungeon.name = d.value("name", "");
        dungeon.regionIndex = d.value("region", 17);
        const auto pixel = d.value("pixel", Json::array({0, 0}));
        dungeon.pixelX = pixel.size() == 2 ? pixel[0].get<int>() : 0;
        dungeon.pixelY = pixel.size() == 2 ? pixel[1].get<int>() : 0;
        dungeon.dungeonType = d.value("dungeonType", 2);
        dungeon.locationType = static_cast<DungeonLocationType>(d.value("locationType", 4));
        dungeon.entrance = d.value("entrance", "DUNGAA00.RMB");
        dungeon.discovered = d.value("discovered", true);
        for (const auto& c : d.value("cells", Json::array())) {
            dungeon.cells.push_back({c.value("x", 0), c.value("z", 0), c.value("block", ""), c.value("start", false)});
        }
        p.dungeons_.push_back(std::move(dungeon));
    }
    for (const auto& m : j.value("models", Json::array())) {
        CustomModel model;
        model.id = m.value("id", 0u);
        if (!isCustomModelId(model.id)) return fail(file.string() + ": bad model id " + std::to_string(model.id));
        model.label = m.value("label", "");
        model.description = m.value("description", "C00").substr(0, 3);
        auto meshData = mesh::meshFromJson(m.value("mesh", Json::object()));
        if (!meshData) return fail(file.string() + ": model " + std::to_string(model.id) + ": " + meshData.error().message);
        model.mesh = std::move(*meshData);
        p.models_.push_back(std::move(model));
    }
    // Numbers are never handed out twice, even if the counters in the file were lost.
    for (const auto& b : p.blocks_) {
        const int n = std::stoi(b.name.substr(3, 5)) - range * kBlocksPerRange;
        if (n < 1 || n > kBlocksPerRange) return fail(file.string() + ": " + b.name + " is outside the project's id range");
        p.blocksIssued_ = std::max(p.blocksIssued_, n);
    }
    for (const auto& d : p.dungeons_) p.dungeonsIssued_ = std::max(p.dungeonsIssued_, d.number + 1);
    for (const auto& m : p.models_) {
        const int n = static_cast<int>(m.id - kFirstCustomModelId) - range * kModelsPerRange;
        if (n < 1 || n > kModelsPerRange) return fail(file.string() + ": model " + std::to_string(m.id) + " is outside the project's id range");
        p.modelsIssued_ = std::max(p.modelsIssued_, n);
    }
    return p;
}

} // namespace de::project
