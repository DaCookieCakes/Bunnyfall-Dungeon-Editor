// Dungeon logic in the viewer: link arrows in every view and the Logic part of the Properties panel.

#include "app/viewer.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <deque>
#include <format>
#include <unordered_map>

namespace de::app {

namespace {

constexpr ImU32 kChainColour = IM_COL32(70, 225, 255, 255);   // cyan: clear of the orange selection and the wire colours
constexpr ImU32 kTeleportColour = IM_COL32(255, 90, 230, 255);
constexpr ImU32 kStopColour = IM_COL32(150, 150, 150, 255);
constexpr ImU32 kMissingColour = IM_COL32(240, 70, 60, 255);
constexpr ImVec4 kWarningText{1.0f, 0.78f, 0.3f, 1.0f};

ImVec2 add(ImVec2 a, ImVec2 b) { return {a.x + b.x, a.y + b.y}; }

ImU32 withAlpha(ImU32 c, int alpha) { return (c & ~IM_COL32_A_MASK) | (ImU32(alpha) << IM_COL32_A_SHIFT); }

ImU32 colourOf(logic::LinkKind kind) {
    switch (kind) {
        case logic::LinkKind::Chain: return kChainColour;
        case logic::LinkKind::Teleport: return kTeleportColour;
        case logic::LinkKind::Ignored: return kStopColour;
        case logic::LinkKind::Missing: return kMissingColour;
    }
    return kChainColour;
}

void arrowHead(ImDrawList* list, ImVec2 from, ImVec2 to, ImU32 colour, float size) {
    const float dx = to.x - from.x, dy = to.y - from.y;
    const float len = std::sqrt(dx * dx + dy * dy);
    if (len < 1) return;
    const float ux = dx / len, uy = dy / len;
    const ImVec2 tip{to.x - ux * 4, to.y - uy * 4};
    list->AddTriangleFilled(tip, {tip.x - ux * size - uy * size * 0.5f, tip.y - uy * size + ux * size * 0.5f},
                            {tip.x - ux * size + uy * size * 0.5f, tip.y - uy * size - ux * size * 0.5f}, colour);
}

void dashedLine(ImDrawList* list, ImVec2 a, ImVec2 b, ImU32 colour, float thickness) {
    const float dx = b.x - a.x, dy = b.y - a.y;
    const float len = std::sqrt(dx * dx + dy * dy);
    const int dashes = std::max(1, int(len / 8));
    for (int i = 0; i < dashes; i += 2) {
        const float t0 = float(i) / float(dashes), t1 = std::min(1.0f, float(i + 1) / float(dashes));
        list->AddLine({a.x + dx * t0, a.y + dy * t0}, {a.x + dx * t1, a.y + dy * t1}, colour, thickness);
    }
}

} // namespace

catalog::Vec3 Viewer::objectCentre(const scene::ObjectRef& ref) const {
    if (const auto inst = instanceOf(ref)) {
        switch (inst->first) {
            case scene::Hit::Kind::Model: {
                const auto& m = scene_.models[inst->second];
                if (const auto* info = catalog_.model(m.modelId)) {
                    const auto& b = info->bounds;
                    return m.transform.apply({(b.min.x + b.max.x) / 2, (b.min.y + b.max.y) / 2, (b.min.z + b.max.z) / 2});
                }
                return m.transform.translation;
            }
            case scene::Hit::Kind::Flat: return scene_.flats[inst->second].position;
            case scene::Hit::Kind::Light: return scene_.lights[inst->second].position;
        }
    }
    const auto& obj = scene_.blocks[ref.block].data.roots[ref.root].objects[ref.object];
    return catalog::fromFilePosition(obj.x, obj.y, obj.z) + scene_.blocks[ref.block].origin;
}

const std::vector<Viewer::SceneLink>& Viewer::sceneLinks() {
    if (linksStamp_ == sceneStamp_) return links_;
    linksStamp_ = sceneStamp_;
    links_.clear();
    // Instances by object, built once (instanceOf scans).
    std::unordered_map<scene::ObjectRef, std::pair<scene::Hit::Kind, std::size_t>, RefHash> instances;
    for (std::size_t i = 0; i < scene_.models.size(); ++i) instances[scene_.models[i].ref] = {scene::Hit::Kind::Model, i};
    for (std::size_t i = 0; i < scene_.flats.size(); ++i) instances[scene_.flats[i].ref] = {scene::Hit::Kind::Flat, i};
    for (std::size_t i = 0; i < scene_.lights.size(); ++i) instances[scene_.lights[i].ref] = {scene::Hit::Kind::Light, i};
    auto instance = [&](const scene::ObjectRef& r) -> std::optional<std::pair<scene::Hit::Kind, std::size_t>> {
        const auto it = instances.find(r);
        if (it == instances.end()) return std::nullopt;
        return it->second;
    };
    for (std::size_t b = 0; b < scene_.blocks.size(); ++b) {
        for (const auto& link : logic::linksOf(scene_.blocks[b].data)) {
            SceneLink s;
            s.from = {b, link.from.root, link.from.object};
            s.to = {b, link.to.root, link.to.object};
            s.kind = link.kind;
            s.a = objectCentre(s.from);
            s.b = link.kind == logic::LinkKind::Missing ? s.a : objectCentre(s.to);
            s.fromInstance = instance(s.from);
            if (link.kind != logic::LinkKind::Missing) s.toInstance = instance(s.to);
            links_.push_back(s);
        }
    }
    return links_;
}

std::unordered_set<scene::ObjectRef, Viewer::RefHash> Viewer::selectedChains() {
    std::unordered_set<scene::ObjectRef, RefHash> out;
    if (selection_.empty()) return out;
    const auto& links = sceneLinks();
    std::deque<scene::ObjectRef> todo(selection_.begin(), selection_.end());
    for (const auto& r : selection_) out.insert(r);
    constexpr std::size_t kLimit = 400;
    while (!todo.empty() && out.size() < kLimit) {
        const auto at = todo.front();
        todo.pop_front();
        for (const auto& l : links) {
            if (l.kind == logic::LinkKind::Missing) continue;
            if (l.from == at && out.insert(l.to).second) todo.push_back(l.to);
            if (l.to == at && out.insert(l.from).second) todo.push_back(l.from);
        }
    }
    return out;
}

void Viewer::drawLogicLinks(std::size_t index) {
    if (linkMode_ == LinkMode::Off) return;
    const auto& links = sceneLinks();
    if (links.empty()) return;
    const auto chains = selectedChains();
    const auto& v = views_[index];
    auto* list = ImGui::GetWindowDrawList();
    list->PushClipRect(v.min, add(v.min, v.size), true);
    auto hiddenEnd = [&](const auto& inst) { return inst && isHidden(inst->first, inst->second); };
    for (const auto& l : links) {
        const bool lit = chains.contains(l.from);
        if (linkMode_ == LinkMode::Selection && !lit) continue;
        if (hiddenEnd(l.fromInstance) || hiddenEnd(l.toInstance)) continue;
        const auto p = toScreen(index, l.a);
        if (!p) continue;
        const ImU32 colour = withAlpha(colourOf(l.kind), lit ? 255 : 175);
        const float thickness = lit ? 2.5f : 1.5f;
        if (l.kind == logic::LinkKind::Missing) {
            const float r = lit ? 6.0f : 4.0f;
            list->AddLine({p->x - r, p->y - r}, {p->x + r, p->y + r}, colour, thickness);
            list->AddLine({p->x - r, p->y + r}, {p->x + r, p->y - r}, colour, thickness);
            continue;
        }
        const auto q = toScreen(index, l.b);
        if (!q) continue;
        const float dx = q->x - p->x, dy = q->y - p->y;
        if (dx * dx + dy * dy < 9) {   // one above the other in this view
            list->AddCircle(*p, lit ? 7.0f : 5.0f, colour, 12, thickness);
            continue;
        }
        if (l.kind == logic::LinkKind::Teleport) dashedLine(list, *p, *q, colour, thickness);
        else list->AddLine(*p, *q, colour, thickness);
        arrowHead(list, *p, *q, colour, lit ? 11.0f : 8.0f);
        list->AddCircleFilled(*p, lit ? 3.5f : 2.5f, colour);
    }
    list->PopClipRect();
}

bool Viewer::selectAtOffset(std::int32_t offset) {
    for (std::size_t b = 0; b < scene_.blocks.size(); ++b) {
        if (const auto slot = logic::findPosition(scene_.blocks[b].data, offset)) {
            selectAndFrame({b, slot->root, slot->object});
            focusInspectorFrames_ = 8;   // past the frames that build the docked layout
            return true;
        }
    }
    return false;
}

void Viewer::selectAndFrame(const scene::ObjectRef& ref) {
    setSelection({ref});
    frameSelection();
}

void Viewer::drawLogicSection(const scene::ObjectRef& ref) {
    const auto& block = scene_.blocks[ref.block].data;
    const auto& obj = block.roots[ref.root].objects[ref.object];
    const auto* m = obj.model();
    const bool door = m && logic::isActionDoor(block, *m);
    const auto node = logic::nodeOf(block, obj);

    std::vector<const SceneLink*> incoming;
    const SceneLink* outgoing = nullptr;
    for (const auto& l : sceneLinks()) {
        if (l.kind != logic::LinkKind::Missing && l.to == ref) incoming.push_back(&l);
        if (l.from == ref) outgoing = &l;
    }
    const bool skippedRecord = m && m->hasAction() && m->action.flags == 0;
    if (!node && !door && incoming.empty() && !skippedRecord) return;

    ImGui::Separator();
    if (!ImGui::CollapsingHeader("Logic", ImGuiTreeNodeFlags_DefaultOpen)) return;
    ImGui::PushID("logic");
    auto warn = [](const std::string& text) {
        ImGui::PushStyleColor(ImGuiCol_Text, kWarningText);
        ImGui::TextWrapped("%s", text.c_str());
        ImGui::PopStyleColor();
    };
    auto linkButton = [&](const char* prefix, const scene::ObjectRef& target, int id) {
        const auto& t = block.roots[target.root].objects[target.object];
        const auto label = std::format("{} {}##{}", prefix, logic::objectLabel(block, t), id);
        if (ImGui::SmallButton(label.c_str())) selectAndFrame(target);
        ImGui::SetItemTooltip("Select it and frame it (offset %d)", t.position);
    };

    if (door) {
        if (const auto lock = logic::startingLock(block, obj)) {
            ImGui::Text("Door, locked: level %d%s", lock->value, lock->magic() ? " (magic: can't be picked or bashed)" : "");
        } else {
            ImGui::TextUnformatted("Door, not locked");
        }
    }
    if (node) {
        const auto d = logic::describe(*node, door, texts_ ? &*texts_ : nullptr);
        ImGui::TextColored(ImGui::GetStyleColorVec4(ImGuiCol_CheckMark), "%s:", d.when.c_str());
        ImGui::Indent();
        ImGui::TextWrapped("%s", d.does.c_str());
        if (!d.detail.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            ImGui::TextWrapped("%s", d.detail.c_str());
            ImGui::PopStyleColor();
        }
        ImGui::Unindent();
        for (const auto& w : d.warnings) warn(w);
        if (node->trigger == logic::trigger::kNone && incoming.empty()) {
            warn("Nothing sets this off, so it never happens.");
        }
        if (outgoing) {
            switch (outgoing->kind) {
                case logic::LinkKind::Chain: linkButton("Also sets off ->", outgoing->to, 0); break;
                case logic::LinkKind::Teleport: linkButton("Lands on ->", outgoing->to, 0); break;
                case logic::LinkKind::Ignored:
                    linkButton("Links to ->", outgoing->to, 0);
                    warn("That object has no action, so the chain stops there.");
                    break;
                case logic::LinkKind::Missing:
                    warn(std::format("Links to offset {}, but nothing in the block is there.", node->next));
                    break;
            }
        }
    } else if (skippedRecord) {
        ImGui::TextDisabled("Has an action record of kind 0, which the game skips (and its link with it).");
    }
    if (!incoming.empty()) {
        ImGui::TextDisabled("Set off by:");
        for (std::size_t i = 0; i < incoming.size(); ++i) {
            const auto* l = incoming[i];
            linkButton(l->kind == logic::LinkKind::Teleport ? "<- teleport from" : "<-", l->from, int(i) + 1);
        }
        if (!node && std::ranges::any_of(incoming, [](const SceneLink* l) { return l->kind != logic::LinkKind::Teleport; })) {
            warn("This has no action itself, so a chain stops here.");
        }
    }

    // The whole chain, from its first object, when it is longer than a pair.
    std::optional<edit::ObjectSlot> head{edit::ObjectSlot{ref.root, ref.object}};
    {
        std::unordered_set<scene::ObjectRef, RefHash> seen{ref};
        scene::ObjectRef at = ref;
        for (;;) {
            const auto prev = std::ranges::find_if(sceneLinks(), [&](const SceneLink& l) {
                return l.kind != logic::LinkKind::Missing && l.to == at;
            });
            if (prev == sceneLinks().end() || !seen.insert(prev->from).second) break;
            at = prev->from;
        }
        head = edit::ObjectSlot{at.root, at.object};
    }
    const auto chain = logic::chainFrom(block, *head);
    if (chain.size() > 2) {
        if (ImGui::TreeNode("chain", "The whole chain (%zu)", chain.size())) {
            for (std::size_t i = 0; i < chain.size(); ++i) {
                const scene::ObjectRef r{ref.block, chain[i].slot.root, chain[i].slot.object};
                const auto& o = block.roots[r.root].objects[r.object];
                std::string what = chain[i].node ? std::string(logic::actionName(chain[i].node->action)) : "no action";
                const auto label = std::format("{}. {}: {}##c{}", i + 1, logic::objectLabel(block, o), what, i);
                if (ImGui::Selectable(label.c_str(), r == ref)) selectAndFrame(r);
            }
            ImGui::TreePop();
        }
    }

    if (ImGui::TreeNode("raw", "Raw values")) {
        ImGui::Text("offset %d", obj.position);
        if (m) {
            ImGui::Text("trigger/lock field 0x%x", m->triggerFlagStartingLock);
            if (m->hasAction()) {
                ImGui::Text("action %d  axis %d  duration %d  magnitude %d", m->action.flags, m->action.axis,
                            m->action.duration, m->action.magnitude);
                ImGui::Text("next %d  sound %d", m->action.nextObjectOffset, m->soundIndex);
            }
        } else if (const auto* f = obj.flat()) {
            ImGui::Text("action %u  flags (trigger) %u  magnitude %u", f->action, f->flags, f->magnitude);
            ImGui::Text("next %d  sound %u", f->nextObjectOffset, f->soundIndex);
        }
        ImGui::TreePop();
    }
    ImGui::PopID();
}

} // namespace de::app
