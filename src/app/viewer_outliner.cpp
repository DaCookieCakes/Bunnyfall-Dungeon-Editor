// The outliner: everything in a block as a list, to find, select, hide and frame things; and framing the selection.

#include "app/viewer.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cctype>
#include <format>

namespace de::app {

namespace {

std::string lowerCase(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

} // namespace

std::optional<std::pair<scene::Hit::Kind, std::size_t>> Viewer::instanceOf(const scene::ObjectRef& ref) const {
    if (const auto i = scene::modelIndexOf(scene_, ref)) return std::pair{scene::Hit::Kind::Model, *i};
    if (const auto i = scene::flatIndexOf(scene_, ref)) return std::pair{scene::Hit::Kind::Flat, *i};
    for (std::size_t i = 0; i < scene_.lights.size(); ++i) {
        if (scene_.lights[i].ref == ref) return std::pair{scene::Hit::Kind::Light, i};
    }
    return std::nullopt;
}

void Viewer::frameSelection() {
    if (selection_.empty()) return;
    catalog::Vec3 lo{1e300, 1e300, 1e300}, hi{-1e300, -1e300, -1e300};
    for (const auto& r : selection_) {
        const auto& obj = scene_.blocks[r.block].data.roots[r.root].objects[r.object];
        const auto p = catalog::fromFilePosition(obj.x, obj.y, obj.z) + scene_.blocks[r.block].origin;
        lo = {std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z)};
        hi = {std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z)};
    }
    const catalog::Vec3 c{(lo.x + hi.x) / 2, (lo.y + hi.y) / 2, (lo.z + hi.z) / 2};
    const double span = std::max({hi.x - lo.x, hi.y - lo.y, hi.z - lo.z, 200.0});
    for (auto& v : views_) {
        if (v.kind == ViewKind::Perspective) continue;
        v.centre = c;
        v.halfHeight = std::clamp(span, 150.0, 40000.0);
    }
    // The 3D camera backs off along its own view until the whole of it fits.
    const auto f = camera_.forward();
    const double distance = span / std::tan(camera_.fovY / 2) * 0.8 + 100;
    camera_.position = {c.x - f.x * distance, c.y - f.y * distance, c.z - f.z * distance};
}

void Viewer::drawOutliner() {
    if (const auto* props = ImGui::FindWindowByName("Properties"); props && props->DockId) {
        ImGui::SetNextWindowDockID(props->DockId, ImGuiCond_FirstUseEver);   // first open: a tab beside Properties
    }
    if (!ImGui::Begin("Outliner", &showOutliner_)) {
        ImGui::End();
        return;
    }
    // Which block: the edited one, else the selection's.
    std::optional<std::size_t> block;
    if (const auto p = primary()) block = p->block;
    if (edit_ && (!block || !editing({*block, 0, 0}))) block = editBlock_;
    if (!block || *block >= scene_.blocks.size()) {
        ImGui::TextDisabled("Select something, or edit a block, to list what is in its block.");
        ImGui::End();
        return;
    }
    const auto& b = scene_.blocks[*block];
    const auto& data = editing({*block, 0, 0}) ? edit_->block() : b.data;
    ImGui::Text("%s%s", b.name.c_str(), editing({*block, 0, 0}) ? "  (editing)" : "");

    ImGui::SetNextItemWidth(-150);
    ImGui::InputTextWithHint("##outlinerfilter", "filter (id, C00, Enemy...)", outlinerFilter_, sizeof outlinerFilter_);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1);
    const char* current = outlinerKind_ < 0 ? "All kinds" : categoryName(static_cast<Category>(outlinerKind_));
    if (ImGui::BeginCombo("##outlinerkind", current)) {
        if (ImGui::Selectable("All kinds", outlinerKind_ < 0)) outlinerKind_ = -1;
        for (std::size_t c = 0; c < kCategories; ++c) {
            if (ImGui::Selectable(categoryName(static_cast<Category>(c)), outlinerKind_ == int(c))) outlinerKind_ = int(c);
        }
        ImGui::EndCombo();
    }

    // The rows: every object of the block that is drawn, with its kind and a name.
    struct Row {
        scene::ObjectRef ref;
        scene::Hit::Kind kind;
        std::size_t instance;
        Category category;
        std::string label;
    };
    std::vector<Row> rows;
    const auto want = lowerCase(outlinerFilter_);
    for (std::size_t r = 0; r < data.roots.size(); ++r) {
        for (std::size_t o = 0; o < data.roots[r].objects.size(); ++o) {
            const scene::ObjectRef ref{*block, r, o};
            const auto inst = instanceOf(ref);
            if (!inst) continue;   // not drawn (an unknown model)
            const auto category = categoryOf(inst->first, inst->second);
            if (outlinerKind_ >= 0 && int(category) != outlinerKind_) continue;
            const auto& obj = data.roots[r].objects[o];
            std::string label;
            if (const auto* m = obj.model()) {
                const auto& ref2 = data.modelRefs[m->modelIndex];
                label = std::format("{} {}", ref2.modelIdNum, ref2.description);
            } else if (const auto* f = obj.flat()) {
                label = f->textureArchive() == edit::kEditorFlatArchive
                            ? std::string(catalog::markerName(f->textureRecord()))
                            : std::format("flat {}/{}", f->textureArchive(), f->textureRecord());
            } else if (const auto* l = obj.light()) {
                label = std::format("light r{}", l->radius);
            }
            const auto kindText = std::string(categoryName(category));
            if (!want.empty() && lowerCase(label).find(want) == std::string::npos &&
                lowerCase(kindText).find(want) == std::string::npos) {
                continue;
            }
            rows.push_back({ref, inst->first, inst->second, category, std::move(label)});
        }
    }
    std::ranges::stable_sort(rows, [](const Row& a, const Row& b2) { return a.category < b2.category; });
    ImGui::TextDisabled("%zu shown  (click selects, Shift adds, Ctrl toggles, double-click frames)", rows.size());

    const auto flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable;
    if (ImGui::BeginTable("outliner", 4, flags)) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFrameHeight());
        ImGui::TableSetupColumn("Kind", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("What", ImGuiTableColumnFlags_WidthStretch, 1.3f);
        ImGui::TableSetupColumn("Position", ImGuiTableColumnFlags_WidthStretch, 1.2f);
        ImGui::TableHeadersRow();
        ImGuiListClipper clipper;
        clipper.Begin(int(rows.size()));
        while (clipper.Step()) {
            for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
                const auto& row = rows[std::size_t(i)];
                ImGui::TableNextRow();
                ImGui::PushID(i);
                ImGui::TableNextColumn();
                bool shownHere = !hidden_.contains(row.ref);
                if (ImGui::Checkbox("##show", &shownHere)) {
                    if (shownHere) hidden_.erase(row.ref);
                    else hidden_.insert(row.ref);
                }
                ImGui::TableNextColumn();
                const bool kindHidden = !shown_[std::size_t(row.category)];
                if (kindHidden) ImGui::TextDisabled("%s", categoryName(row.category));
                else ImGui::TextUnformatted(categoryName(row.category));
                if (kindHidden) ImGui::SetItemTooltip("This kind is hidden (Show menu)");
                ImGui::TableNextColumn();
                if (ImGui::Selectable(row.label.c_str(), isSelected(row.ref),
                                      ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick)) {
                    const auto& io = ImGui::GetIO();
                    clickSelect(scene::Hit{row.kind, row.instance, 0, {}}, io.KeyShift, io.KeyCtrl);
                    if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                        setSelection({row.ref});
                        frameSelection();
                    }
                }
                ImGui::TableNextColumn();
                const auto& obj = data.roots[row.ref.root].objects[row.ref.object];
                ImGui::TextDisabled("%d, %d, %d", obj.x, obj.y, obj.z);
                ImGui::PopID();
            }
        }
        ImGui::EndTable();
    }
    ImGui::End();
}

} // namespace de::app
