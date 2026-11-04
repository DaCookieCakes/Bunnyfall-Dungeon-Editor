#include "project/prefab.h"

#include "edit/transform.h"
#include "export/dfu_json.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <map>
#include <sstream>

namespace de::project {

namespace {

constexpr const char* kFormat = "dfe-prefab/1";

std::int32_t linkOf(const formats::RdbObject& o) {
    if (const auto* m = o.model(); m && m->hasAction()) return m->action.nextObjectOffset;
    if (const auto* f = o.flat(); f && f->action) return f->nextObjectOffset;
    return -1;
}

void setLink(formats::RdbObject& o, std::int32_t to) {
    if (auto* m = std::get_if<formats::RdbModel>(&o.resource); m && m->hasAction()) m->action.nextObjectOffset = to;
    if (auto* f = std::get_if<formats::RdbFlat>(&o.resource); f && f->action) f->nextObjectOffset = to;
}

} // namespace

std::uint32_t Prefab::modelIdOf(const formats::RdbObject& obj) const {
    const auto* m = obj.model();
    return m && m->modelIndex < block.modelRefs.size() ? block.modelRefs[m->modelIndex].modelIdNum : 0;
}

Prefab makePrefab(std::string name, const formats::RdbBlock& source, const std::vector<edit::ObjectSlot>& slots,
                  std::array<std::int32_t, 3> anchor) {
    Prefab p;
    p.name = std::move(name);
    p.block.width = p.block.height = 1;
    p.block.roots.resize(1);
    p.block.roots[0].offset = 0;
    std::map<std::uint16_t, std::uint16_t> refs;   // source model slot -> prefab model slot
    std::vector<std::int32_t> positions;
    for (const auto& s : slots) {
        if (s.root >= source.roots.size() || s.object >= source.roots[s.root].objects.size()) continue;
        auto obj = source.roots[s.root].objects[s.object];
        obj.x -= anchor[0];
        obj.y -= anchor[1];
        obj.z -= anchor[2];
        if (auto* m = std::get_if<formats::RdbModel>(&obj.resource)) {
            auto [it, fresh] = refs.try_emplace(m->modelIndex, static_cast<std::uint16_t>(p.block.modelRefs.size()));
            if (fresh) p.block.modelRefs.push_back(source.modelRefs[m->modelIndex]);
            m->modelIndex = it->second;
        }
        positions.push_back(obj.position);
        p.block.roots[0].objects.push_back(std::move(obj));
    }
    // Links to objects left behind go.
    for (auto& o : p.block.roots[0].objects) {
        const auto to = linkOf(o);
        if (to >= 0 && std::ranges::find(positions, to) == positions.end()) setLink(o, -1);
    }
    edit::relinkActions(p.block);
    return p;
}

Result<void> savePrefab(const Prefab& prefab, const std::filesystem::path& file) {
    exporter::Json j{{"format", kFormat},
                     {"name", prefab.name},
                     {"block", exporter::rdbBlockToJson(prefab.block, "PREFAB.RDB", 0, 0)}};
    std::error_code ec;
    std::filesystem::create_directories(file.parent_path(), ec);
    std::ofstream out(file, std::ios::binary);
    if (!out) return fail("cannot write " + file.string());
    out << j.dump(1, '\t') << '\n';
    if (!out) return fail("cannot write " + file.string());
    return {};
}

Result<Prefab> loadPrefab(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) return fail("cannot read " + file.string());
    std::stringstream text;
    text << in.rdbuf();
    const auto j = exporter::Json::parse(text.str(), nullptr, false);
    if (j.is_discarded() || !j.is_object()) return fail(file.filename().string() + ": not JSON");
    if (j.value("format", "") != kFormat) return fail(file.filename().string() + ": not a " + kFormat + " prefab");
    auto block = exporter::rdbBlockFromJson(j.value("block", exporter::Json::object()));
    if (!block) return fail(file.filename().string() + ": " + block.error().message);
    if (block->roots.empty()) return fail(file.filename().string() + ": no objects");
    Prefab p;
    p.name = j.value("name", file.stem().stem().string());
    p.block = std::move(*block);
    // Everything in one root, as made.
    for (std::size_t r = 1; r < p.block.roots.size(); ++r) {
        for (auto& o : p.block.roots[r].objects) p.block.roots[0].objects.push_back(std::move(o));
    }
    p.block.roots.resize(1);
    p.file = file;
    return p;
}

std::vector<Prefab> loadPrefabFolder(const std::filesystem::path& dir, std::vector<std::string>& problems) {
    std::vector<Prefab> out;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
        const auto name = entry.path().filename().string();
        if (!name.ends_with(".prefab.json")) continue;
        auto p = loadPrefab(entry.path());
        if (p) out.push_back(std::move(*p));
        else problems.push_back(p.error().message);
    }
    std::ranges::sort(out, [](const auto& a, const auto& b) { return a.name < b.name; });
    return out;
}

std::string prefabFileName(std::string_view name) {
    std::string out;
    for (const char c : name) {
        if (std::isalnum(static_cast<unsigned char>(c))) out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        else if (!out.empty() && out.back() != '-') out += '-';
    }
    while (!out.empty() && out.back() == '-') out.pop_back();
    return (out.empty() ? "prefab" : out) + ".prefab.json";
}

std::optional<std::vector<edit::ObjectSlot>> placePrefab(edit::BlockEdit& edit, const Prefab& prefab,
                                                         std::array<std::int32_t, 3> at, std::int32_t turn) {
    // Room for its models first, so a full block refuses before anything changes.
    std::size_t newModels = 0;
    for (const auto& ref : prefab.block.modelRefs) {
        if (ref.modelIdNum && !edit.modelSlot(ref.modelIdNum)) ++newModels;
    }
    std::size_t used = 0;
    for (const auto& ref : edit.block().modelRefs) {
        if (ref.description != formats::kUnusedModelDescription && !(ref.modelId.empty() && ref.modelIdNum == 0)) ++used;
    }
    if (used + newModels > formats::kRdbModelRefSlots) return std::nullopt;

    edit.beginGesture();
    std::vector<edit::ObjectSlot> slots;
    std::map<std::int32_t, std::int32_t> moved;   // prefab position -> block position
    for (const auto& original : prefab.objects()) {
        auto obj = edit::transformed(original, at, turn, 0, 0);
        std::optional<edit::ObjectSlot> slot;
        if (obj.model()) {
            const auto& ref = prefab.block.modelRefs[obj.model()->modelIndex];
            slot = edit.addModel(ref.modelIdNum, ref.description, std::move(obj));
        } else {
            slot = edit.add(std::move(obj));
        }
        if (!slot) {
            edit.cancelGesture();
            return std::nullopt;
        }
        moved[original.position] = edit.object(*slot)->position;
        slots.push_back(*slot);
    }
    // Their links, to each other's new positions.
    for (std::size_t i = 0; i < slots.size(); ++i) {
        const auto to = linkOf(prefab.objects()[i]);
        if (to < 0) continue;
        auto obj = *edit.object(slots[i]);
        const auto it = moved.find(to);
        setLink(obj, it == moved.end() ? -1 : it->second);
        edit.replace(slots[i], std::move(obj));
    }
    edit.endGesture();
    return slots;
}

} // namespace de::project
