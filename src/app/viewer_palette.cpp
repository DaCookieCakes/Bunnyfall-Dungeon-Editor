// The model palette: every model Daggerfall's dungeons use, as pictures, and placing them (with a preview under the
// pointer, on surfaces or in empty space).

#include "app/viewer.h"

#include "edit/placement.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cctype>
#include <format>
#include <map>
#include <set>

namespace de::app {

namespace {

constexpr const char* kKinds[] = {"Architecture", "Doors", "Props", "All", "Prefabs"};
constexpr int kPrefabTab = 4;

bool kindMatches(int filter, catalog::ModelKind kind) {
    switch (filter) {
        case 0: return kind == catalog::ModelKind::Architecture;
        case 1: return kind == catalog::ModelKind::Door || kind == catalog::ModelKind::ExitDoor;
        case 2: return kind == catalog::ModelKind::Prop;
        default: return true;
    }
}

std::string lowered(std::string_view s) {
    std::string out(s);
    for (auto& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

} // namespace

void Viewer::armModel(std::uint32_t id) {
    if (!edit_) {
        status_ = "Start editing a block first (Properties > Edit this block).";
        return;
    }
    if (grabbing_) endGrab(false);
    placing_.reset();
    placingModel_ = id;
    const auto* info = catalog_.model(id);
    status_ = std::format("Placing model {} ({}): click to put it down, [ and ] turn it, right-click or Esc stops.", id,
                          info ? info->description : "?");
}

std::optional<Viewer::SurfacePoint> Viewer::placementSurface(double x, double y) {
    if (auto s = surfaceAt(x, y)) return s;
    const auto ray = rayAt(x, y);
    const auto& v = views_[pointerView_];
    catalog::Vec3 point;
    if (v.kind == ViewKind::Front || v.kind == ViewKind::Side) {
        const auto f = ray.direction;
        const double t = (v.centre.x - ray.origin.x) * f.x + (v.centre.y - ray.origin.y) * f.y + (v.centre.z - ray.origin.z) * f.z;
        point = {ray.origin.x + f.x * t, ray.origin.y + f.y * t, ray.origin.z + f.z * t};
    } else {
        if (std::abs(ray.direction.y) < 1e-6) return std::nullopt;
        const double t = (drawOptions_.gridOptions.height - ray.origin.y) / ray.direction.y;
        if (t < 0) return std::nullopt;
        point = {ray.origin.x + ray.direction.x * t, drawOptions_.gridOptions.height, ray.origin.z + ray.direction.z * t};
    }
    for (std::size_t b = 0; b < scene_.blocks.size(); ++b) {
        const auto& o = scene_.blocks[b].origin;
        if (point.x >= o.x && point.x < o.x + catalog::kRdbBlockSide && point.z >= o.z && point.z < o.z + catalog::kRdbBlockSide) {
            return SurfacePoint{point, {0, 1, 0}, b};
        }
    }
    return std::nullopt;
}

std::optional<std::pair<scene::ModelInstance, std::size_t>> Viewer::modelPlacement(double x, double y) {
    if (!placingModel_) return std::nullopt;
    const auto* info = catalog_.model(*placingModel_);
    const auto s = placementSurface(x, y);
    if (!info || !s) return std::nullopt;
    const auto rotation = catalog::rdbModelRotation(0, placeTurn_, 0);
    const auto turned = catalog::transformBox(info->bounds, rotation, {0, 0, 0});
    const auto origin = edit::restingModelOrigin(s->point, s->normal, turned);
    // Snapped as the file will hold it, so the preview is where the click puts it.
    const auto file = toFile(origin, s->sceneBlock, isPiece(*placingModel_) ? std::optional(moveSteps(true)) : std::nullopt);
    const auto at = catalog::fromFilePosition(file[0], file[1], file[2]) + scene_.blocks[s->sceneBlock].origin;
    return std::pair{scene::ModelInstance{*placingModel_, {rotation, at}, {static_cast<std::size_t>(-1), 0, 0}}, s->sceneBlock};
}

void Viewer::selectionToModel(std::uint64_t shapeKey, std::uint32_t modelId) {
    const auto* target = catalog_.model(modelId);
    if (!edit_ || !target) return;
    edit_->beginGesture();
    int changed = 0;
    for (const auto& s : selectedSlots()) {
        const auto* m = edit_->object(s)->model();
        if (!m) continue;
        const auto* info = catalog_.model(edit_->block().modelRefs[m->modelIndex].modelIdNum);
        if (!info || info->shapeKey != shapeKey) continue;
        if (edit_->setModel(s, modelId, target->description)) ++changed;
    }
    edit_->endGesture();
    editChanged();
    status_ = std::format("{} piece{} now model {}.", changed, changed == 1 ? "" : "s", modelId);
}

void Viewer::selectionToTextureSet(int archive) {
    if (!edit_) return;
    edit_->beginGesture();
    int changed = 0, without = 0;
    for (const auto& s : selectedSlots()) {
        const auto* m = edit_->object(s)->model();
        if (!m) continue;
        const auto* info = catalog_.model(edit_->block().modelRefs[m->modelIndex].modelIdNum);
        if (!info || info->textureArchive == archive) continue;
        const catalog::ModelInfo* to = nullptr;
        for (const auto t : info->twins) {
            const auto* twin = catalog_.model(t);
            if (twin && twin->textureArchive == archive && (!to || twin->uses > to->uses)) to = twin;
        }
        if (!to) {
            ++without;
            continue;
        }
        if (edit_->setModel(s, to->id, to->description)) ++changed;
    }
    edit_->endGesture();
    editChanged();
    status_ = std::format("{} piece{} now in textures {}{}.", changed, changed == 1 ? "" : "s", archive,
                          without ? std::format(" ({} do not come in it)", without) : "");
}

void Viewer::drawTextureVariants() {
    const auto slot = selectedSlot();
    if (!edit_ || !slot) return;
    const auto* m = edit_->object(*slot)->model();
    if (!m) return;
    const auto* info = catalog_.model(edit_->block().modelRefs[m->modelIndex].modelIdNum);
    if (!info) return;

    if (!info->twins.empty()) {
        ImGui::SeparatorText("Texture variants");
        std::vector<const catalog::ModelInfo*> all{info};
        for (const auto t : info->twins) {
            if (const auto* twin = catalog_.model(t)) all.push_back(twin);
        }
        std::ranges::sort(all, [](const auto* a, const auto* b) { return a->id < b->id; });
        const float side = 56;
        const int perRow = std::max(1, int(ImGui::GetContentRegionAvail().x / (side + 14)));
        for (std::size_t i = 0; i < all.size(); ++i) {
            const auto* v = all[i];
            if (i % std::size_t(perRow)) ImGui::SameLine();
            ImGui::PushID(static_cast<int>(v->id));
            const bool current = v->id == info->id;
            if (current) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_HeaderActive));
            const GLuint tex = thumbnails_.get(v->id, v->bounds);
            const bool clicked = tex ? ImGui::ImageButton("v", ImTextureRef(static_cast<ImTextureID>(tex)), {side, side}, {0, 1}, {1, 0})
                                     : ImGui::Button(std::to_string(v->id).c_str(), {side + 8, side + 8});
            if (current) ImGui::PopStyleColor();
            if (clicked && !current) selectionToModel(info->shapeKey, v->id);
            ImGui::SetItemTooltip("%u: textures %d%s", v->id, v->textureArchive, v->uses ? "" : " (no classic block uses it)");
            ImGui::PopID();
        }
        if (selection_.size() > 1) ImGui::TextDisabled("Applies to every selected piece of this shape.");
    }

    // Texture sets the selected pieces come in, for retexturing a whole room at once.
    if (selection_.size() > 1) {
        std::map<int, int> sets;
        for (const auto& s : selectedSlots()) {
            const auto* sm = edit_->object(s)->model();
            if (!sm) continue;
            const auto* si = catalog_.model(edit_->block().modelRefs[sm->modelIndex].modelIdNum);
            if (!si) continue;
            std::set<int> mine{si->textureArchive};
            for (const auto t : si->twins) {
                if (const auto* twin = catalog_.model(t)) mine.insert(twin->textureArchive);
            }
            for (const int a : mine) ++sets[a];
        }
        if (sets.size() > 1) {
            ImGui::SeparatorText("Texture set for the selection");
            int n = 0;
            for (const auto& [archive, count] : sets) {
                if (n++ % 4) ImGui::SameLine();
                if (ImGui::Button(std::format("{}##set", archive).c_str(), {56, 0})) selectionToTextureSet(archive);
                ImGui::SetItemTooltip("%d of the selected pieces come in textures %d", count, archive);
            }
        }
    }
}

// ---- prefabs ----

void Viewer::reloadPrefabs() {
    stopPlacing();
    std::vector<std::string> problems;
    prefabs_ = project::loadPrefabFolder(prefabFolder(), problems);
    if (!problems.empty()) status_ = "Prefabs not loaded: " + problems.front();
}

void Viewer::armPrefab(std::size_t index) {
    if (!edit_) {
        status_ = "Start editing a block first (Properties > Edit this block).";
        return;
    }
    if (grabbing_) endGrab(false);
    stopPlacing();
    placingPrefab_ = index;
    placeTurn_ = 0;
    status_ = std::format("Placing \"{}\": click to put it down, [ and ] turn it, right-click or Esc stops.",
                          prefabs_[index].name);
}

void Viewer::savePrefabFromSelection(const std::string& name) {
    const auto p = primary();
    if (!p || name.empty()) return;
    const auto& block = scene_.blocks[p->block];
    const auto& data = editing(*p) ? edit_->block() : block.data;
    std::vector<edit::ObjectSlot> slots;
    double sx = 0, sz = 0, bottom = 1e300;
    bool pieces = false;
    for (const auto& r : selection_) {
        if (r.block != p->block) continue;   // one block's objects: the primary's
        const auto& obj = data.roots[r.root].objects[r.object];
        slots.push_back({r.root, r.object});
        sx += obj.x;
        sz += obj.z;
        // The bottom of what it is, editor space (Y up) in the block.
        const auto at = catalog::fromFilePosition(obj.x, obj.y, obj.z);
        double low = at.y;
        if (const auto* m = obj.model()) {
            const auto id = data.modelRefs[m->modelIndex].modelIdNum;
            if (const auto* info = catalog_.model(id)) {
                low = catalog::transformBox(info->bounds, catalog::rdbModelRotation(m->xRotation, m->yRotation, m->zRotation), at).min.y;
            }
            pieces = pieces || isPiece(id);
        } else if (const auto* f = obj.flat()) {
            low = at.y - textures_.flatSize(f->textureArchive(), f->textureRecord()).second / 2;
        } else {
            low = at.y - render::kLightGizmoSize / 2;
        }
        bottom = std::min(bottom, low);
    }
    if (slots.empty()) return;
    const double n = double(slots.size());
    // Pieces keep to the classic grid: anchor them on it, so the prefab lands on it too.
    const int across = pieces && pieceSnap_ ? 64 : 1, up = pieces && pieceSnap_ ? 128 : 1;
    const std::array<std::int32_t, 3> anchor{edit::snapTo(sx / n, across), edit::snapTo(-bottom, up), edit::snapTo(sz / n, across)};
    auto prefab = project::makePrefab(name, data, slots, anchor);
    const auto file = prefabFolder() / project::prefabFileName(name);
    if (auto r = project::savePrefab(prefab, file); !r) {
        status_ = "Prefab not saved: " + r.error().message;
        return;
    }
    reloadPrefabs();
    status_ = std::format("Saved prefab \"{}\" ({} objects) as {}.", name, slots.size(), file.string());
}

std::optional<std::pair<std::array<std::int32_t, 3>, std::size_t>> Viewer::prefabPlacement(double x, double y) {
    if (!placingPrefab_ || *placingPrefab_ >= prefabs_.size()) return std::nullopt;
    const auto s = placementSurface(x, y);
    if (!s) return std::nullopt;
    const auto& prefab = prefabs_[*placingPrefab_];
    bool pieces = false;
    for (const auto& o : prefab.objects()) pieces = pieces || isPiece(prefab.modelIdOf(o));
    const auto file = toFile(s->point, s->sceneBlock, pieces ? std::optional(moveSteps(true)) : std::nullopt);
    return std::pair{file, s->sceneBlock};
}

void Viewer::updateGhost() {
    ghostModels_.clear();
    ghostFlats_.clear();
    if (!placingModel_ && !placingPrefab_) return;
    const auto& v = views_[pointerView_];
    const auto& m = ImGui::GetIO().MousePos;
    if (!v.shown || m.x < v.min.x || m.y < v.min.y || m.x >= v.min.x + v.size.x || m.y >= v.min.y + v.size.y) return;
    constexpr std::size_t kNowhere = static_cast<std::size_t>(-1);
    if (placingModel_) {
        if (const auto at = modelPlacement(mouseX_, mouseY_)) ghostModels_.push_back(at->first);
        return;
    }
    const auto at = prefabPlacement(mouseX_, mouseY_);
    if (!at) return;
    const auto& prefab = prefabs_[*placingPrefab_];
    const auto& origin = scene_.blocks[at->second].origin;
    for (const auto& o : prefab.objects()) {
        const auto placed = edit::transformed(o, at->first, edit::modelYRotationFor(placeTurn_), 0, 0);
        const auto pos = catalog::fromFilePosition(placed.x, placed.y, placed.z) + origin;
        if (const auto* pm = placed.model()) {
            ghostModels_.push_back({prefab.modelIdOf(o), {catalog::rdbModelRotation(pm->xRotation, pm->yRotation, pm->zRotation), pos},
                                    {kNowhere, 0, 0}});
        } else if (const auto* f = placed.flat()) {
            scene::FlatInstance g;
            g.archive = f->textureArchive();
            g.record = f->textureRecord();
            g.position = pos;
            g.editorMarker = f->textureArchive() == edit::kEditorFlatArchive;
            g.ref = {kNowhere, 0, 0};
            ghostFlats_.push_back(g);
        }
    }
}

void Viewer::drawPrefabList() {
    ImGui::TextWrapped("Prefabs are sets of objects to place again as one. They are saved in %s.", prefabFolder().string().c_str());
    ImGui::SetNextItemWidth(-160);
    ImGui::InputTextWithHint("##prefabname", "name for the selection", prefabName_, sizeof prefabName_);
    ImGui::SameLine();
    ImGui::BeginDisabled(selection_.empty() || prefabName_[0] == 0);
    if (ImGui::Button("Save selection", {-1, 0})) {
        savePrefabFromSelection(prefabName_);
        prefabName_[0] = 0;
    }
    ImGui::EndDisabled();
    ImGui::SetItemTooltip("From any block, classic or your own: the objects selected in the last picked one's block");
    if (ImGui::SmallButton("Reload")) reloadPrefabs();
    if (!edit_) {
        ImGui::SameLine();
        ImGui::TextDisabled("Edit a block to place them.");
    }
    ImGui::Separator();
    if (prefabs_.empty()) {
        ImGui::TextDisabled("None yet. Select a set piece (a table and chairs, a shrine...) in any dungeon, name it\n"
                            "and press Save selection.");
        return;
    }
    if (ImGui::BeginChild("prefabs", {0, 0}, ImGuiChildFlags_Borders)) {
        std::optional<std::size_t> remove;
        for (std::size_t i = 0; i < prefabs_.size(); ++i) {
            const auto& p = prefabs_[i];
            ImGui::PushID(static_cast<int>(i));
            const bool armed = placingPrefab_ == i;
            std::size_t models = 0;
            for (const auto& o : p.objects()) models += o.model() ? 1u : 0u;
            if (ImGui::Selectable(std::format("{}   ({} models, {} other)", p.name, models, p.objects().size() - models).c_str(), armed)) {
                if (armed) stopPlacing();
                else armPrefab(i);
            }
            if (ImGui::BeginPopupContextItem()) {
                if (ImGui::MenuItem("Delete the prefab file")) remove = i;
                ImGui::EndPopup();
            }
            ImGui::SetItemTooltip("%s\nRight-click to delete", p.file.string().c_str());
            ImGui::PopID();
        }
        if (remove) {
            std::error_code ec;
            std::filesystem::remove(prefabs_[*remove].file, ec);
            status_ = ec ? "Not deleted: " + ec.message() : "Deleted prefab \"" + prefabs_[*remove].name + "\".";
            reloadPrefabs();
        }
    }
    ImGui::EndChild();
}

void Viewer::drawModelPalette() {
    if (const auto* browse = ImGui::FindWindowByName("Browse"); browse && browse->DockId) {
        ImGui::SetNextWindowDockID(browse->DockId, ImGuiCond_FirstUseEver);   // first open: a tab beside Browse
    }
    if (!ImGui::Begin("Models", &showModels_)) {
        ImGui::End();
        return;
    }
    if (ImGui::BeginTabBar("kinds", ImGuiTabBarFlags_FittingPolicyShrink)) {
        for (int k = 0; k < 5; ++k) {
            if (ImGui::BeginTabItem(kKinds[k])) {
                paletteKind_ = k;
                ImGui::EndTabItem();
            }
        }
        ImGui::EndTabBar();
    }
    if (paletteKind_ == kPrefabTab) {
        drawPrefabList();
        ImGui::End();
        return;
    }
    ImGui::SetNextItemWidth(-90);
    ImGui::InputTextWithHint("##modelfilter", "id or description (C00, DOR...)", modelFilter_, sizeof modelFilter_);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1);
    ImGui::SliderFloat("##thumb", &thumbSize_, 40, 128, "%.0f px");
    ImGui::Checkbox("Unused variants", &showUnusedModels_);
    ImGui::SetItemTooltip("Also list pieces no classic block uses: the same shapes in other textures");
    if (!edit_) ImGui::TextDisabled("Edit a block to place models.");

    // Most used first: the pieces classic dungeons are built from come to the top.
    const auto want = lowered(modelFilter_);
    std::vector<const catalog::ModelInfo*> shown;
    for (const auto& [id, info] : catalog_.models()) {
        if (!kindMatches(paletteKind_, info.kind)) continue;
        if (info.uses == 0 && !showUnusedModels_) continue;
        if (!want.empty() && std::to_string(id).find(want) == std::string::npos &&
            lowered(info.description).find(want) == std::string::npos) {
            continue;
        }
        shown.push_back(&info);
    }
    std::ranges::sort(shown, [](const auto* a, const auto* b) { return a->uses != b->uses ? a->uses > b->uses : a->id < b->id; });
    ImGui::TextDisabled("%zu models", shown.size());

    if (ImGui::BeginChild("models", {0, 0}, ImGuiChildFlags_Borders)) {
        const float pad = ImGui::GetStyle().FramePadding.x * 2 + ImGui::GetStyle().ItemSpacing.x;
        const int perRow = std::max(1, int((ImGui::GetContentRegionAvail().x + ImGui::GetStyle().ItemSpacing.x) / (thumbSize_ + pad)));
        const int rows = int((shown.size() + std::size_t(perRow) - 1) / std::size_t(perRow));
        ImGuiListClipper clipper;
        clipper.Begin(rows, thumbSize_ + ImGui::GetStyle().FramePadding.y * 2 + ImGui::GetStyle().ItemSpacing.y);
        while (clipper.Step()) {
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                for (int col = 0; col < perRow; ++col) {
                    const auto i = std::size_t(row * perRow + col);
                    if (i >= shown.size()) break;
                    const auto* m = shown[i];
                    if (col) ImGui::SameLine();
                    ImGui::PushID(static_cast<int>(m->id));
                    const bool armed = placingModel_ == m->id;
                    if (armed) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_HeaderActive));
                    const GLuint tex = thumbnails_.get(m->id, m->bounds);
                    bool clicked = false;
                    if (tex) {
                        clicked = ImGui::ImageButton("m", ImTextureRef(static_cast<ImTextureID>(tex)), {thumbSize_, thumbSize_},
                                                     {0, 1}, {1, 0});
                    } else {
                        clicked = ImGui::Button(std::to_string(m->id).c_str(),
                                                {thumbSize_ + ImGui::GetStyle().FramePadding.x * 2,
                                                 thumbSize_ + ImGui::GetStyle().FramePadding.y * 2});
                    }
                    if (armed) ImGui::PopStyleColor();
                    if (clicked) {
                        if (armed) stopPlacing();
                        else armModel(m->id);
                    }
                    if (ImGui::BeginItemTooltip()) {
                        const auto s = m->bounds.size();
                        ImGui::Text("%u  \"%s\"  %s", m->id, m->description.c_str(), std::string(catalog::kindName(m->kind)).c_str());
                        ImGui::Text("%.0f x %.0f x %.0f units (%.1f x %.1f x %.1f m)", s.x, s.y, s.z,
                                    s.x / catalog::kBlockUnitsPerMetre, s.y / catalog::kBlockUnitsPerMetre,
                                    s.z / catalog::kBlockUnitsPerMetre);
                        ImGui::TextDisabled("used %zu times in classic blocks; textures %d; %zu other texture%s", m->uses,
                                            m->textureArchive, m->twins.size(), m->twins.size() == 1 ? "" : "s");
                        ImGui::EndTooltip();
                    }
                    ImGui::PopID();
                }
            }
        }
    }
    ImGui::EndChild();
    ImGui::End();
}

} // namespace de::app
