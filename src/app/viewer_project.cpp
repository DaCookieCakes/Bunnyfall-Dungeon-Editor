// The project: the designer's own dungeons and blocks, the dungeon grid, and the export to Bunnyfall.

#include "app/viewer.h"

#include "formats/climate.h"

#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <array>
#include <format>
#include <utility>

namespace de::app {

namespace {

constexpr std::string_view kRoleLetters = "NWSMB";
constexpr std::array<const char*, 5> kRoleNames = {"Normal", "Wet", "Quest", "Mausoleum", "Border cap"};
constexpr std::array<const char*, 19> kDungeonTypes = {
    "Crypt",       "Orc stronghold", "Human stronghold", "Prison",       "Desecrated temple", "Mine",
    "Natural cave", "Coven",         "Vampire haunt",    "Laboratory",   "Harpy nest",        "Ruined castle",
    "Spider nest", "Giant stronghold", "Dragon's den",   "Barbarian stronghold", "Volcanic caves", "Scorpion nest",
    "Cemetery"};
constexpr std::array<std::pair<project::DungeonLocationType, const char*>, 4> kLocationTypes = {{
    {project::DungeonLocationType::Labyrinth, "Labyrinth"},
    {project::DungeonLocationType::Keep, "Keep"},
    {project::DungeonLocationType::Ruin, "Ruin"},
    {project::DungeonLocationType::Graveyard, "Graveyard"},
}};
constexpr int kBorderCapCount = 15;   // B0000000 .. B0000014: all of them open all eight sockets

/// A role's colour on the grid.
ImU32 roleColour(formats::RdbRole role, bool own) {
    const auto a = own ? 255 : 190;
    switch (role) {
        case formats::RdbRole::Normal: return IM_COL32(60, 90, 150, a);
        case formats::RdbRole::Wet: return IM_COL32(40, 120, 125, a);
        case formats::RdbRole::Quest: return IM_COL32(110, 70, 140, a);
        case formats::RdbRole::Mausoleum: return IM_COL32(120, 90, 55, a);
        case formats::RdbRole::Border: return IM_COL32(75, 75, 80, a);
        case formats::RdbRole::Unknown: break;
    }
    return IM_COL32(120, 40, 40, a);
}

/// A block's name short enough for a grid cell: a classic one without its zeros (N0000071 -> N71), the project's own
/// by its label.
std::string cellLabel(const std::string& name, const project::CustomBlock* own) {
    if (own) return own->label.size() > 8 ? own->label.substr(0, 7) + "." : own->label;
    const auto stem = name.substr(0, name.find('.'));
    if (stem.size() < 2) return stem;
    const auto digits = stem.find_first_not_of('0', 1);
    return stem.substr(0, 1) + (digits == std::string::npos ? "0" : stem.substr(digits));
}

/// Text input bound to a std::string.
bool inputString(const char* label, std::string& value, std::size_t capacity = 64) {
    std::vector<char> buffer(capacity, '\0');
    std::copy_n(value.begin(), std::min(value.size(), capacity - 1), buffer.begin());
    if (!ImGui::InputText(label, buffer.data(), buffer.size())) return false;
    value = buffer.data();
    return true;
}

} // namespace

// ---- files ----

void Viewer::setModFolder(std::filesystem::path dir) {
    modOverride_ = std::move(dir);
    applyModFolder();
}

void Viewer::applyModFolder() {
    std::optional<std::filesystem::path> dir = modOverride_;
    if (!dir && config_ && !bunnyfallProblem(config_->bunnyfall) && !project::modFolderProblem(project_.mod.folder)) {
        dir = config_->modFolder(project_.mod.folder);
    }
    modDir_ = dir;
    if (patchOverride_) {
        patches_.emplace(*patchOverride_);
    } else if (dir) {
        patches_.emplace(*dir / "WorldDataPatches");
    } else {
        patches_.reset();
    }
}

Result<void> Viewer::setProjectFile(std::filesystem::path file) {
    projectFile_ = std::move(file);
    if (std::filesystem::exists(projectFile_)) {
        auto loaded = project::Project::load(projectFile_);
        if (!loaded) return std::unexpected(loaded.error());
        project_ = std::move(*loaded);
    } else {
        project_ = {};
    }
    projectDirty_ = false;
    issuesStale_ = true;
    if (!project_.dungeons().empty()) currentDungeon_ = project_.dungeons().front().number;
    applyModFolder();
    registerCustomModels();
    reloadPrefabs();
    return {};
}

void Viewer::saveProject() {
    if (projectFile_.empty()) {
        status_ = "No project file: start the editor with --project <file>.";
        return;
    }
    if (auto r = project_.save(projectFile_); !r) {
        status_ = "Project not saved: " + r.error().message;
        return;
    }
    projectDirty_ = false;
    status_ = "Saved the project to " + projectFile_.filename().string() + ".";
}

namespace {

/// "crypts-of-hope.project.json" -> "Crypts Of Hope".
std::string nameFromFile(const std::filesystem::path& file) {
    auto stem = file.stem().string();
    if (stem.ends_with(".project")) stem.resize(stem.size() - 8);
    std::string name;
    bool start = true;
    for (const char c : stem) {
        if (c == '-' || c == '_' || c == ' ' || c == '.') {
            if (!name.empty() && name.back() != ' ') name.push_back(' ');
            start = true;
        } else {
            name.push_back(start ? static_cast<char>(std::toupper(static_cast<unsigned char>(c))) : c);
            start = false;
        }
    }
    while (!name.empty() && name.back() == ' ') name.pop_back();
    return name.empty() ? "My Dungeons" : name;
}

} // namespace

void Viewer::projectFilePicked(ProjectDialog purpose, std::filesystem::path file) {
    file = std::filesystem::absolute(file).lexically_normal();
    if (purpose == ProjectDialog::SaveAs) {
        saveProjectAs(std::move(file));
        return;
    }
    if (purpose == ProjectDialog::New && std::filesystem::exists(file)) {
        status_ = file.filename().string() + " is there already: File > Open project opens it.";
        return;
    }
    if (purpose == ProjectDialog::Open && file == projectFile_) {
        status_ = file.filename().string() + " is the project already open.";
        return;
    }
    if (hasUnsavedEdits()) {   // ask first (drawConfirmations)
        pendingProject_ = {purpose, std::move(file)};
        return;
    }
    if (auto r = switchProject(std::move(file), purpose == ProjectDialog::New); !r) status_ = r.error().message;
}

void Viewer::saveProjectAs(std::filesystem::path file) {
    if (auto r = project_.save(file); !r) {
        status_ = "Project not saved: " + r.error().message;
        return;
    }
    projectFile_ = std::move(file);
    projectDirty_ = false;
    if (config_) {
        config_->project = projectFile_;
        (void)config_->save();
    }
    status_ = std::format("Saved the project as {}. It still exports to vendor/{}: give it a folder of its own in the "
                          "Mod tab if it is to be a mod of its own.",
                          projectFile_.filename().string(), project_.mod.folder);
}

Result<void> Viewer::switchProject(std::filesystem::path file, bool fresh) {
    if (fresh) {
        project::Project p;
        p.name = nameFromFile(file);
        p.mod.author = project_.mod.author;   // most likely the same person
        p.mod.folder = project::modFolderFor(p.name);
        if (config_ && !bunnyfallProblem(config_->bunnyfall)) {
            const auto vendor = config_->bunnyfall / "vendor";
            const auto base = p.mod.folder;
            for (int n = 2; std::filesystem::exists(vendor / p.mod.folder); ++n) p.mod.folder = std::format("{}-{}", base, n);
            if (const auto range = project::freeIdRange(vendor, p.mod.folder)) (void)p.setIdRange(*range);
        }
        if (auto r = p.save(file); !r) return std::unexpected(r.error());
    } else if (auto loaded = project::Project::load(file); !loaded) {
        return std::unexpected(loaded.error());
    }

    // Leave the old project.
    closeModelEditor();
    endEdit();
    std::vector<std::uint32_t> oldModels;
    for (const auto& m : project_.models()) oldModels.push_back(m.id);
    currentDungeon_ = -1;
    if (auto r = setProjectFile(file); !r) return r;
    for (const auto id : oldModels) {
        if (!project_.model(id)) syncCustomModel(id);   // gone from the catalog and the drawn models
    }
    if (config_) {
        config_->project = projectFile_;
        (void)config_->save();
    }
    refreshOtherMods();

    // What is on screen, read again: the patches are the new mod folder's, the project blocks the new project's.
    bool readable = true;
    for (auto& b : scene_.blocks) {
        auto data = blockSource()(b.name);
        if (!data) {
            readable = false;
            break;
        }
        b.data = std::move(*data);
    }
    if (readable) {
        scene::rebuildInstances(scene_);
        ++sceneStamp_;
        clearSelection();
    } else if (const auto daggerfall = findDungeon("Daggerfall")) {
        if (auto r = openDungeon(*daggerfall); !r) return r;
    }
    status_ = std::format("{} {} (exports to {}).", fresh ? "Made the new project" : "Opened", projectFile_.filename().string(),
                          modDir_ ? modDir_->string() : "nowhere yet: set Bunnyfall in File > Paths");
    return {};
}

void Viewer::exportProject() {
    if (!modDir_) {
        status_ = "No mod folder: set the Bunnyfall checkout in File > Paths (or start with --mod <folder>).";
        return;
    }
    if (edit_ && edit_->dirty() && isProjectBlock(edit_->name())) saveEdit();   // what is on screen is what ships
    if (projectDirty_) saveProject();
    auto report = project::exportProject(project_, *world_, *modDir_);
    if (!report) {
        status_ = "Export failed: " + report.error().message;
        return;
    }
    issues_ = report->issues;
    issuesStale_ = false;
    refreshOtherMods();
    if (report->hasErrors()) {
        status_ = "Not exported: fix the errors listed in the Project panel first.";
        return;
    }
    status_ = std::format("Exported {} files to {}{}.", report->written.size(), modDir_->string(),
                          report->removed.empty() ? "" : std::format(" and removed {} old ones", report->removed.size()));
}

// ---- opening ----

Result<void> Viewer::openProjectDungeon(int number) {
    const auto* d = project_.dungeon(number);
    if (!d) return fail("no project dungeon " + std::to_string(number));
    if (d->cells.empty()) return fail(d->name + " has no blocks yet.");
    if (edit_ && edit_->dirty()) return fail("Save or discard the edits to " + edit_->name() + " first.");
    std::vector<formats::DungeonBlockRef> refs;
    for (const auto& c : d->cells) {
        refs.push_back({static_cast<std::int8_t>(c.x), static_cast<std::int8_t>(c.z), 0, c.blockName, c.isStartingBlock});
    }
    const int climate = formats::terrainClimateAt(arena2_.climate, d->pixelX, d->pixelY);
    auto scene = scene::assembleDungeon(d->name, refs, arena2_.blocks, blockSource(),
                                        scene::TextureTable::classic(d->dungeonLocationId(), climate), climate);
    if (!scene) return std::unexpected(scene.error());
    setScene(std::move(*scene));
    status_ = std::format("{}: {} blocks (project)", d->name, d->cells.size());
    return {};
}

Result<void> Viewer::openProjectBlock(const std::string& name) {
    const auto* own = project_.block(name);
    if (!own) return fail("no project block " + name);
    if (edit_ && edit_->dirty()) return fail("Save or discard the edits to " + edit_->name() + " first.");
    setScene(scene::blockScene(name, own->data));
    status_ = std::format("{} ({}): {} objects", name, own->label, own->data.objectCount());
    return {};
}

void Viewer::editCell(int dungeonNumber, int x, int z) {
    if (auto r = openProjectDungeon(dungeonNumber); !r) {
        status_ = r.error().message;
        return;
    }
    const catalog::Vec3 origin{double(x) * catalog::kRdbBlockSide, 0, double(z) * catalog::kRdbBlockSide};
    for (std::size_t i = 0; i < scene_.blocks.size(); ++i) {
        if (scene_.blocks[i].origin.x != origin.x || scene_.blocks[i].origin.z != origin.z) continue;
        beginEdit(i);
        const double half = catalog::kRdbBlockSide / 2.0;
        camera_.position = {origin.x + half, 2600, origin.z - 900};
        camera_.lookAt({origin.x + half, 600, origin.z + half});
        drawOptions_.grid = true;
        return;
    }
}

// ---- helpers ----

std::pair<std::ptrdiff_t, std::ptrdiff_t> Viewer::usersOf(const std::string& name) const {
    const auto classic = std::ranges::count_if(dungeons_, [&](const auto& d) {
        return std::ranges::any_of(d.blocks, [&](const auto& b) { return b.blockName == name; });
    });
    const auto own = std::ranges::count_if(project_.dungeons(), [&](const auto& d) {
        return std::ranges::any_of(d.cells, [&](const auto& c) { return c.blockName == name; });
    });
    return {classic, own};
}

std::optional<std::pair<int, int>> Viewer::freePixel(int region, int nearX, int nearY) const {
    auto free = [&](int x, int y) {
        if (formats::regionAtPixel(arena2_.politic, x, y) != region) return false;
        if (formats::terrainClimateAt(arena2_.climate, x, y) == formats::kClimateOcean) return false;
        if (std::ranges::any_of(world_->locations, [&](const auto& l) { return l.pixelX() == x && l.pixelY() == y; })) return false;
        return std::ranges::none_of(project_.dungeons(), [&](const auto& d) { return d.pixelX == x && d.pixelY == y; });
    };
    if (free(nearX, nearY)) return std::pair{nearX, nearY};
    for (int r = 1; r < 300; ++r) {
        for (int dy = -r; dy <= r; ++dy)
            for (int dx = -r; dx <= r; ++dx) {
                if (std::max(std::abs(dx), std::abs(dy)) != r) continue;   // the ring at distance r only
                if (free(nearX + dx, nearY + dy)) return std::pair{nearX + dx, nearY + dy};
            }
    }
    return std::nullopt;
}

int Viewer::addCaps(project::ProjectDungeon& d) {
    int added = 0;
    const auto cells = d.cells;   // caps are added for the blocks there now, not for each other
    for (const auto& c : cells) {
        if (formats::rdbRoleFromName(c.blockName) == formats::RdbRole::Border) continue;
        for (auto [dx, dz] : {std::pair{1, 0}, std::pair{-1, 0}, std::pair{0, 1}, std::pair{0, -1}}) {
            const int x = c.x + dx, z = c.z + dz;
            if (d.cellAt(x, z)) continue;
            const int pick = ((x * 7 + z * 13) % kBorderCapCount + kBorderCapCount) % kBorderCapCount;
            d.cells.push_back({x, z, std::format("B{:07d}.RDB", pick), false});
            ++added;
        }
    }
    return added;
}

void Viewer::refreshOtherMods() {
    otherMods_ = modDir_ ? project::editorModsIn(modDir_->parent_path()) : std::vector<project::EditorMod>{};
    modIssues_ = modDir_ ? project::checkModFolder(project_, *modDir_) : std::vector<project::Issue>{};
}

void Viewer::drawModSettings() {
    ImGui::TextWrapped("The project exports as a mod of its own: a folder of Bunnyfall's vendor/. Bunnyfall gives it a "
                       "switch under Settings > Gameplay > Dungeons, titled with the project's name.");
    ImGui::Spacing();
    auto& mod = project_.mod;
    ImGui::SetNextItemWidth(260);
    std::string folder = mod.folder;
    inputString("Folder", folder);
    if (folder != mod.folder) {
        mod.folder = folder;
        markProjectChanged();
    }
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        applyModFolder();
        // A project that has numbered nothing yet moves to a range no other mod uses.
        if (modDir_ && !project_.numbersIssued()) {
            if (const auto free = project::freeIdRange(modDir_->parent_path(), mod.folder)) (void)project_.setIdRange(*free);
        }
        refreshOtherMods();
        issuesStale_ = true;
    }
    if (const auto problem = project::modFolderProblem(mod.folder)) {
        ImGui::SameLine();
        ImGui::TextColored({1.0f, 0.45f, 0.4f, 1.0f}, "%s", problem->c_str());
    }
    ImGui::SetNextItemWidth(260);
    if (inputString("Author", mod.author)) markProjectChanged();
    std::vector<char> buffer(512, '\0');
    std::copy_n(mod.description.begin(), std::min(mod.description.size(), buffer.size() - 1), buffer.begin());
    if (ImGui::InputTextMultiline("Description", buffer.data(), buffer.size(), {420, 60})) {
        mod.description = buffer.data();
        markProjectChanged();
    }
    ImGui::SetItemTooltip("The switch's note in Bunnyfall's settings. Empty: \"Dungeons built with the Bunnyfall Dungeon Editor.\"");

    // The id range.
    ImGui::Spacing();
    ImGui::BeginDisabled(project_.numbersIssued());
    int range = mod.idRange;
    ImGui::SetNextItemWidth(120);
    if (ImGui::Combo("Id range", &range, "0\0" "1\0" "2\0" "3\0" "4\0" "5\0" "6\0" "7\0")) {
        if (auto r = project_.setIdRange(range); !r) status_ = r.error().message;
        else markProjectChanged();
        refreshOtherMods();
        issuesStale_ = true;
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::TextDisabled(project_.numbersIssued() ? "(fixed: the project has numbered blocks, dungeons or models)"
                                                 : "(fixed once the first block, dungeon or model is made)");
    ImGui::TextWrapped("Block names, location ids and model ids come from it, so two mods with different ranges never "
                       "take each other's.");

    // Where it goes, and who else is there.
    ImGui::Spacing();
    ImGui::SeparatorText("In Bunnyfall");
    if (!modDir_) {
        ImGui::TextColored({1.0f, 0.8f, 0.35f, 1.0f}, "No Bunnyfall checkout: set one in File > Paths to export.");
        return;
    }
    ImGui::TextDisabled("%s", modDir_->string().c_str());
    if (modOverride_) ImGui::TextDisabled("(from --mod)");
    if (otherMods_.empty()) {
        ImGui::TextDisabled("No other editor mods in vendor/ yet.");
    } else if (ImGui::BeginTable("mods", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingFixedFit)) {
        ImGui::TableSetupColumn("Folder");
        ImGui::TableSetupColumn("Title");
        ImGui::TableSetupColumn("Range");
        ImGui::TableHeadersRow();
        for (const auto& m : otherMods_) {
            const bool self = m.folder == mod.folder;
            const bool clash = !self && m.idRange == mod.idRange;
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::Text("%s%s", m.folder.c_str(), self ? " (this one, as last exported)" : "");
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(m.title.c_str());
            ImGui::TableNextColumn();
            if (clash) ImGui::TextColored({1.0f, 0.45f, 0.4f, 1.0f}, "%d (the same!)", m.idRange);
            else ImGui::Text("%d", m.idRange);
        }
        ImGui::EndTable();
    }
    for (const auto& issue : modIssues_) {
        const bool error = issue.severity == project::Issue::Severity::Error;
        ImGui::TextColored(error ? ImVec4{1.0f, 0.45f, 0.4f, 1.0f} : ImVec4{1.0f, 0.8f, 0.35f, 1.0f}, "%s", issue.text.c_str());
    }
}

void Viewer::refreshIssues() {
    if (!issuesStale_) return;
    issues_.clear();
    if (const auto* d = project_.dungeon(currentDungeon_)) issues_ = project::checkDungeon(project_, *d, *world_);
    issuesStale_ = false;
}

// ---- panels ----

void Viewer::drawProjectPanel() {
    const auto* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos({350, viewport->Size.y - 470}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize({700, 460}, ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Project", &showProject_)) {
        ImGui::End();
        return;
    }
    ImGui::SetNextItemWidth(220);
    if (inputString("Project name", project_.name)) markProjectChanged();
    ImGui::SetItemTooltip("Also the mod's title in Bunnyfall's settings");
    ImGui::SameLine();
    if (ImGui::Button(projectDirty_ ? "Save project*" : "Save project")) saveProject();
    ImGui::SameLine();
    if (ImGui::Button("Export to Bunnyfall")) exportProject();
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Checks every dungeon, then writes the blocks and places into %s",
                          modDir_ ? modDir_->string().c_str() : "(no mod folder)");
    }
    ImGui::TextDisabled("%s", projectFile_.empty() ? "(no project file)" : projectFile_.string().c_str());

    if (ImGui::BeginTabBar("project")) {
        if (ImGui::BeginTabItem("Dungeons")) {
            // The list.
            ImGui::BeginChild("dungeon list", {170, 0}, ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeX);
            if (ImGui::Button("New dungeon", {-1, 0})) {
                auto d = project_.addDungeon(std::format("New Dungeon {}", project_.dungeonsIssued() + 1));
                if (!d) {
                    status_ = d.error().message;
                } else {
                    if (const auto pixel = freePixel(17, 109, 158)) std::tie((*d)->pixelX, (*d)->pixelY) = *pixel;
                    (*d)->cells = {{0, 0, "N0000071.RDB", true}};
                    currentDungeon_ = (*d)->number;
                    markProjectChanged();
                    issuesStale_ = true;
                }
            }
            for (const auto& d : project_.dungeons()) {
                if (ImGui::Selectable(std::format("{}##{}", d.name, d.number).c_str(), currentDungeon_ == d.number)) {
                    currentDungeon_ = d.number;
                    issuesStale_ = true;
                }
            }
            ImGui::EndChild();
            ImGui::SameLine();

            // The chosen one.
            ImGui::BeginChild("dungeon");
            if (auto* d = project_.dungeon(currentDungeon_)) {
                drawDungeonSettings(*d);
                drawGrid(*d);
            } else {
                ImGui::TextDisabled("Make a new dungeon, or pick one from the list.");
            }
            ImGui::EndChild();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Blocks")) {
            drawProjectBlocks();
            ImGui::EndTabItem();
        }
        const bool modTab = ImGui::BeginTabItem("Mod");
        if (ImGui::IsItemActivated() || (modTab && ImGui::IsWindowAppearing())) {
            refreshOtherMods();
        }
        if (modTab) {
            drawModSettings();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::End();

    if (confirmDeleteDungeon_) ImGui::OpenPopup("Delete dungeon?");
    if (ImGui::BeginPopupModal("Delete dungeon?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        const auto* d = project_.dungeon(currentDungeon_);
        ImGui::Text("Delete %s from the project?", d ? d->name.c_str() : "the dungeon");
        ImGui::TextDisabled("Its number is not given out again. The next export removes its file.");
        if (ImGui::Button("Delete")) {
            project_.removeDungeon(currentDungeon_);
            currentDungeon_ = project_.dungeons().empty() ? -1 : project_.dungeons().front().number;
            markProjectChanged();
            issuesStale_ = true;
            confirmDeleteDungeon_ = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Keep it")) {
            confirmDeleteDungeon_ = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void Viewer::drawDungeonSettings(project::ProjectDungeon& d) {
    bool changed = false;
    ImGui::SetNextItemWidth(220);
    changed |= inputString("Name", d.name);
    ImGui::SameLine();
    if (ImGui::Button("View in 3D")) {
        if (auto r = openProjectDungeon(d.number); !r) status_ = r.error().message;
    }
    ImGui::SameLine();
    if (ImGui::Button("Delete")) confirmDeleteDungeon_ = true;

    ImGui::SetNextItemWidth(220);
    if (ImGui::BeginCombo("Region", std::string(formats::regionName(d.regionIndex)).c_str())) {
        for (int r = 0; r < formats::kRegionCount; ++r) {
            if (ImGui::Selectable(std::string(formats::regionName(r)).c_str(), r == d.regionIndex)) {
                d.regionIndex = r;
                changed = true;
            }
        }
        ImGui::EndCombo();
    }
    int pixel[2] = {d.pixelX, d.pixelY};
    ImGui::SetNextItemWidth(220);
    if (ImGui::InputInt2("Map pixel", pixel)) {
        d.pixelX = std::clamp(pixel[0], 0, 999);
        d.pixelY = std::clamp(pixel[1], 0, 499);
        changed = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Free spot nearby")) {
        if (const auto free = freePixel(d.regionIndex, d.pixelX, d.pixelY)) {
            std::tie(d.pixelX, d.pixelY) = *free;
            changed = true;
        } else {
            status_ = "No free land in that region.";
        }
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("The nearest land pixel in the region with no place on it.");

    ImGui::SetNextItemWidth(220);
    if (ImGui::Combo("Dungeon type", &d.dungeonType, kDungeonTypes.data(), static_cast<int>(kDungeonTypes.size()))) changed = true;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Picks which enemies random enemy markers draw, and the loot.");
    int typeIndex = static_cast<int>(std::ranges::find(kLocationTypes, d.locationType, &std::pair<project::DungeonLocationType, const char*>::first) - kLocationTypes.begin());
    const char* typeNames[] = {kLocationTypes[0].second, kLocationTypes[1].second, kLocationTypes[2].second, kLocationTypes[3].second};
    ImGui::SetNextItemWidth(220);
    if (ImGui::Combo("Map icon", &typeIndex, typeNames, 4)) {
        d.locationType = kLocationTypes[static_cast<std::size_t>(typeIndex)].first;
        changed = true;
    }

    if (entranceBlocks_.empty()) {
        for (const auto& e : arena2_.blocks.entries()) {
            if (e.name.ends_with(".RMB") && (e.name.starts_with("DUNG") || e.name.starts_with("RUIN") || e.name.starts_with("GRVY"))) {
                entranceBlocks_.push_back(e.name);
            }
        }
    }
    ImGui::SetNextItemWidth(220);
    if (ImGui::BeginCombo("Entrance", d.entrance.c_str())) {
        for (const auto& name : entranceBlocks_) {
            if (ImGui::Selectable(name.c_str(), name == d.entrance)) {
                d.entrance = name;
                changed = true;
            }
        }
        ImGui::EndCombo();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("The classic surface block with the way in (DUNGAA: dungeon mouths, RUINAA: ruins).");
    changed |= ImGui::Checkbox("Shown on the travel map", &d.discovered);

    if (changed) {
        markProjectChanged();
        issuesStale_ = true;
    }
}

void Viewer::drawGrid(project::ProjectDungeon& d) {
    ImGui::SeparatorText("Blocks");

    // What a click on an empty cell puts there.
    ImGui::SetNextItemWidth(220);
    const auto* own = project_.block(gridBlock_);
    const auto label = own ? std::format("{} ({})", gridBlock_, own->label) : gridBlock_;
    if (ImGui::BeginCombo("Place", label.c_str(), ImGuiComboFlags_HeightLarge)) {
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextWithHint("##gridfilter", "filter", gridFilter_, sizeof gridFilter_);
        const std::string filter = gridFilter_;
        for (const auto& b : project_.blocks()) {
            if (!filter.empty() && b.name.find(filter) == std::string::npos && b.label.find(filter) == std::string::npos) continue;
            if (ImGui::Selectable(std::format("{}  {}  (yours)", b.name, b.label).c_str(), b.name == gridBlock_)) gridBlock_ = b.name;
        }
        for (const auto& b : catalog_.blocks()) {
            if (!filter.empty() && b.name.find(filter) == std::string::npos) continue;
            if (ImGui::Selectable(std::format("{}  {}", b.name, formats::roleName(b.role)).c_str(), b.name == gridBlock_)) gridBlock_ = b.name;
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (ImGui::Button("Auto caps")) {
        const int added = addCaps(d);
        status_ = std::format("Added {} border caps.", added);
        if (added) {
            markProjectChanged();
            issuesStale_ = true;
        }
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Closes every open side of a non-cap block with a classic border cap.");

    // The canvas: north (+z) up, one square per block.
    int minX = -2, maxX = 2, minZ = -2, maxZ = 2;
    for (const auto& c : d.cells) {
        minX = std::min(minX, c.x - 1);
        maxX = std::max(maxX, c.x + 1);
        minZ = std::min(minZ, c.z - 1);
        maxZ = std::max(maxZ, c.z + 1);
    }
    const float cell = 60;
    const ImVec2 size{float(maxX - minX + 1) * cell, float(maxZ - minZ + 1) * cell};
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("grid", size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    const bool leftClick = ImGui::IsItemClicked(ImGuiMouseButton_Left);
    const bool rightClick = ImGui::IsItemClicked(ImGuiMouseButton_Right);
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    auto* draw = ImGui::GetWindowDrawList();
    auto corner = [&](int x, int z) {
        return ImVec2{origin.x + float(x - minX) * cell, origin.y + float(maxZ - z) * cell};
    };
    const auto colourOpen = [](std::size_t socket) { return socket == 0 ? IM_COL32(100, 255, 115, 255) : IM_COL32(255, 155, 50, 255); };

    for (int z = maxZ; z >= minZ; --z) {
        for (int x = minX; x <= maxX; ++x) {
            const ImVec2 a = corner(x, z), b{a.x + cell - 2, a.y + cell - 2};
            const auto* c = d.cellAt(x, z);
            if (!c) {
                draw->AddRect(a, b, IM_COL32(90, 90, 100, 120));
                continue;
            }
            draw->AddRectFilled(a, b, roleColour(formats::rdbRoleFromName(c->blockName), isProjectBlock(c->blockName)));
            if (c->isStartingBlock) draw->AddRect(a, b, IM_COL32(255, 220, 60, 255), 0.0f, 0, 3.0f);
            const auto shortName = cellLabel(c->blockName, project_.block(c->blockName));
            draw->AddText({a.x + 9, a.y + 8}, IM_COL32(235, 235, 240, 255), shortName.c_str());
            if (c->isStartingBlock) draw->AddText({a.x + 9, a.y + cell - 24}, IM_COL32(255, 220, 60, 255), "start");

            // Doorways: two ticks per edge, coloured when open, red where the neighbour's side disagrees.
            const auto sockets = project::socketsOf(project_, *world_, c->blockName);
            if (!sockets) continue;
            struct EdgeInfo { catalog::Edge edge; int dx, dz; catalog::Edge facing; };
            constexpr EdgeInfo edges[] = {{catalog::Edge::West, -1, 0, catalog::Edge::East},
                                          {catalog::Edge::East, 1, 0, catalog::Edge::West},
                                          {catalog::Edge::South, 0, -1, catalog::Edge::North},
                                          {catalog::Edge::North, 0, 1, catalog::Edge::South}};
            for (const auto& e : edges) {
                const auto* other = d.cellAt(x + e.dx, z + e.dz);
                const auto theirs = other ? project::socketsOf(project_, *world_, other->blockName) : std::nullopt;
                for (std::size_t s = 0; s < 2; ++s) {
                    const bool open = (*sockets)[static_cast<std::size_t>(e.edge)][s];
                    const bool mismatch = theirs && (*theirs)[static_cast<std::size_t>(e.facing)][s] != open;
                    if (!open && !mismatch) continue;
                    const ImU32 colour = mismatch ? IM_COL32(255, 60, 60, 255) : colourOpen(s);
                    const float t = float(catalog::kStandardSockets[s].along) / catalog::kRdbBlockSide;
                    ImVec2 p0, p1;
                    switch (e.edge) {
                        case catalog::Edge::West: p0 = {a.x, b.y - t * (cell - 2) - 3}; p1 = {a.x + 6, p0.y + 6}; break;
                        case catalog::Edge::East: p0 = {b.x - 6, b.y - t * (cell - 2) - 3}; p1 = {b.x, p0.y + 6}; break;
                        case catalog::Edge::South: p0 = {a.x + t * (cell - 2) - 3, b.y - 6}; p1 = {p0.x + 6, b.y}; break;
                        case catalog::Edge::North: p0 = {a.x + t * (cell - 2) - 3, a.y}; p1 = {p0.x + 6, a.y + 6}; break;
                    }
                    draw->AddRectFilled(p0, p1, colour);
                }
            }
        }
    }
    // Axis labels: the start cell's coordinates.


    const int hx = minX + static_cast<int>((mouse.x - origin.x) / cell);
    const int hz = maxZ - static_cast<int>((mouse.y - origin.y) / cell);
    if (ImGui::IsItemHovered()) {
        if (const auto* c = d.cellAt(hx, hz)) {
            const auto* ownBlock = project_.block(c->blockName);
            ImGui::SetTooltip("(%d, %d) %s%s%s\nRight-click for more", hx, hz, c->blockName.c_str(),
                              ownBlock ? "  " : "", ownBlock ? ownBlock->label.c_str() : "");
        } else {
            ImGui::SetTooltip("(%d, %d) empty: click to place %s", hx, hz, gridBlock_.c_str());
        }
    }
    if (leftClick && !d.cellAt(hx, hz)) {
        d.cells.push_back({hx, hz, gridBlock_, d.cells.empty()});
        markProjectChanged();
        issuesStale_ = true;
    }
    if (rightClick && d.cellAt(hx, hz)) {
        gridMenuCell_ = std::pair{hx, hz};
        ImGui::OpenPopup("cell menu");
    }
    if (ImGui::BeginPopup("cell menu")) {
        if (gridMenuCell_) {
            const auto [cx, cz] = *gridMenuCell_;
            auto it = std::ranges::find_if(d.cells, [&](const auto& c) { return c.x == cx && c.z == cz; });
            if (it != d.cells.end()) {
                ImGui::TextDisabled("(%d, %d) %s", cx, cz, it->blockName.c_str());
                if (ImGui::MenuItem("Make it the starting block", nullptr, it->isStartingBlock)) {
                    for (auto& c : d.cells) c.isStartingBlock = false;
                    it->isStartingBlock = true;
                    markProjectChanged();
                    issuesStale_ = true;
                }
                if (ImGui::MenuItem(std::format("Replace with {}", gridBlock_).c_str(), nullptr, false, gridBlock_ != it->blockName)) {
                    it->blockName = gridBlock_;
                    markProjectChanged();
                    issuesStale_ = true;
                }
                if (ImGui::MenuItem("Edit this block in 3D")) editCell(d.number, cx, cz);
                if (ImGui::MenuItem("Remove")) {
                    d.cells.erase(it);
                    markProjectChanged();
                    issuesStale_ = true;
                }
            }
        }
        ImGui::EndPopup();
    }

    // What is wrong.
    refreshIssues();
    ImGui::SeparatorText(issues_.empty() ? "No problems" : std::format("{} problems", issues_.size()).c_str());
    for (const auto& issue : issues_) {
        const bool error = issue.severity == project::Issue::Severity::Error;
        ImGui::TextColored(error ? ImVec4{1.0f, 0.4f, 0.35f, 1.0f} : ImVec4{1.0f, 0.8f, 0.35f, 1.0f}, "%s %s",
                           error ? "Error:" : "Warning:", issue.text.c_str());
    }
}

void Viewer::drawProjectBlocks() {
    ImGui::SeparatorText("New block");
    int role = newBlockRole_;
    ImGui::SetNextItemWidth(130);
    if (ImGui::Combo("Role", &role, kRoleNames.data(), static_cast<int>(kRoleNames.size()))) newBlockRole_ = role;
    ImGui::SameLine();
    ImGui::SetNextItemWidth(110);
    ImGui::InputTextWithHint("Copy of", "empty", newBlockTemplate_, sizeof newBlockTemplate_);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("A classic block to start from (N0000071), or leave empty to start from nothing.");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(160);
    ImGui::InputTextWithHint("Label", "a name for it", newBlockLabel_, sizeof newBlockLabel_);
    ImGui::SameLine();
    if (ImGui::Button("Create")) {
        std::string templateName = newBlockTemplate_;
        std::optional<std::string> base;
        formats::RdbBlock data = project::emptyBlock();
        if (!templateName.empty()) {
            if (!templateName.ends_with(".RDB")) templateName += ".RDB";
            const auto index = arena2_.blocks.findByName(templateName);
            auto classic = index ? formats::readClassicRdbBlock(arena2_.blocks, *index)
                                 : Result<formats::RdbBlock>(fail("no classic block " + templateName));
            if (!classic) {
                status_ = classic.error().message;
                return;
            }
            data = std::move(*classic);
            base = templateName;
        }
        auto made = project_.addBlock(kRoleLetters[static_cast<std::size_t>(newBlockRole_)],
                                      newBlockLabel_[0] ? newBlockLabel_ : "untitled", base, std::move(data));
        if (!made) {
            status_ = made.error().message;
        } else {
            gridBlock_ = (*made)->name;
            status_ = std::format("Made {}{}.", (*made)->name, base ? " from " + *base : " from nothing");
            markProjectChanged();
        }
    }

    ImGui::SeparatorText("Your blocks");
    if (project_.blocks().empty()) ImGui::TextDisabled("None yet.");
    std::optional<std::string> remove;
    if (ImGui::BeginTable("blocks", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit)) {
        ImGui::TableSetupColumn("Name");
        ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("From");
        ImGui::TableSetupColumn("Used");
        ImGui::TableSetupColumn("");
        ImGui::TableHeadersRow();
        for (auto& b : project_.blocks()) {
            ImGui::PushID(b.name.c_str());
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(b.name.c_str());
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-1);
            if (inputString("##label", b.label)) markProjectChanged();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(b.templateName ? b.templateName->c_str() : "nothing");
            ImGui::TableNextColumn();
            const auto users = usersOf(b.name).second;
            ImGui::Text("%td", users);
            ImGui::TableNextColumn();
            if (ImGui::SmallButton("Open")) {
                if (auto r = openProjectBlock(b.name); !r) status_ = r.error().message;
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Use")) gridBlock_ = b.name;
            ImGui::SameLine();
            ImGui::BeginDisabled(users > 0);
            if (ImGui::SmallButton("Delete")) remove = b.name;
            ImGui::EndDisabled();
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if (remove) {
        if (edit_ && edit_->name() == *remove) endEdit();
        project_.removeBlock(*remove);
        markProjectChanged();
    }
}

// ---- the scripted check ----

Result<std::string> Viewer::checkProject(const std::filesystem::path& modDir) {
    std::string log;
    setModFolder(modDir);
    if (auto r = setProjectFile(modDir / "check.project.json"); !r) return std::unexpected(r.error());

    const auto index = arena2_.blocks.findByName("N0000071.RDB");
    auto classic = formats::readClassicRdbBlock(arena2_.blocks, *index);
    if (!classic) return std::unexpected(classic.error());
    auto hall = project_.addBlock('N', "Great hall", "N0000071.RDB", std::move(*classic));
    if (!hall) return std::unexpected(hall.error());
    const auto hallName = (*hall)->name;
    log += "made block " + hallName + " from N0000071.RDB\n";

    auto made = project_.addDungeon("The Burrow");
    if (!made) return std::unexpected(made.error());
    auto& d = **made;
    const auto pixel = freePixel(17, 109, 158);
    if (!pixel) return fail("no free pixel");
    std::tie(d.pixelX, d.pixelY) = *pixel;
    d.cells = {{0, 0, hallName, true}};
    currentDungeon_ = d.number;
    log += std::format("made dungeon {} at map pixel ({}, {})\n", d.name, d.pixelX, d.pixelY);

    log += std::format("auto caps added {}\n", addCaps(d));
    issuesStale_ = true;
    refreshIssues();
    log += std::format("{} problems\n", issues_.size());
    for (const auto& i : issues_) log += "  " + i.text + "\n";

    if (auto r = openProjectDungeon(d.number); !r) return fail("open: " + r.error().message);
    log += std::format("opened in 3D: {} blocks, {} models\n", scene_.blocks.size(), scene_.models.size());

    saveProject();
    if (projectDirty_) return fail("project not saved: " + status_);
    exportProject();
    log += status_ + "\n";
    if (!std::filesystem::exists(modDir / "WorldData" / project::locationFileName(d))) return fail("no place file written");

    // File > New project, then Open the first one again; the editor's saved settings are left alone.
    struct Restore {
        Config*& slot;
        Config* saved;
        ~Restore() { slot = saved; }
    } restore{config_, std::exchange(config_, nullptr)};
    const auto first = projectFile_;
    const auto second = modDir / "crypts-of-hope.project.json";
    std::filesystem::remove(second);
    if (auto r = switchProject(second, true); !r) return fail("new project: " + r.error().message);
    if (project_.name != "Crypts Of Hope" || project_.mod.folder != "crypts-of-hope" || !project_.dungeons().empty() ||
        !std::filesystem::exists(second)) {
        return fail(std::format("new project: '{}', folder '{}', {} dungeons", project_.name, project_.mod.folder,
                                project_.dungeons().size()));
    }
    log += std::format("new project {} (mod folder {}); the scene held the old project's block, so {} opened\n",
                       project_.name, project_.mod.folder, scene_.title);
    if (auto r = switchProject(first, false); !r) return fail("open project: " + r.error().message);
    if (project_.dungeons().size() != 1 || !project_.block(hallName)) return fail("the first project did not come back");
    log += "opened " + first.filename().string() + " again: its dungeon and block are back\n";
    return log;
}

} // namespace de::app
