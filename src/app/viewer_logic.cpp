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
    if (linkFrom_ && pointerView_ == index) {   // the link being picked follows the pointer
        if (const auto p = toScreen(index, objectCentre(*linkFrom_))) {
            const auto mouse = ImGui::GetIO().MousePos;
            auto* list = ImGui::GetWindowDrawList();
            list->PushClipRect(views_[index].min, add(views_[index].min, views_[index].size), true);
            dashedLine(list, *p, mouse, kChainColour, 2.0f);
            if (linkReverse_) arrowHead(list, mouse, *p, kChainColour, 11.0f);   // it will point into this one
            else arrowHead(list, *p, mouse, kChainColour, 11.0f);
            list->AddText({mouse.x + 12, mouse.y + 4}, kChainColour, linkReverse_ ? "set off by?" : "next object?");
            list->PopClipRect();
        }
    }
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
            showProperties_ = true;
            showTriggersTab_ = true;
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
    const bool editable = editing(ref) && logic::canAct(obj);
    if (!node && !door && incoming.empty() && !skippedRecord && !editable) {
        ImGui::TextDisabled(logic::canAct(obj) ? "No action, and nothing sets it off.\nEdit its block to give it one."
                                               : "Markers and lights can't act (a teleport can land on one).");
        return;
    }
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

    if (door && !editable) {
        if (const auto lock = logic::startingLock(block, obj)) {
            ImGui::Text("Door, locked: level %d%s", lock->value, lock->magic() ? " (magic: can't be picked or bashed)" : "");
        } else {
            ImGui::TextUnformatted("Door, not locked");
        }
    }
    if (door) {
        const auto lock = logic::startingLock(block, obj);
        const bool opened = node && (node->action == logic::action::kOpenDoor || node->action == logic::action::kUnlockDoor) &&
                            !incoming.empty();
        if (lock && lock->magic() && !opened) {
            warn("A magic lock, and nothing here opens or unlocks it: only a spell gets the player through.");
        }
    }
    if (editable) {
        drawLogicEditor(ref, incoming);
        if (node) ImGui::SeparatorText("In the game");
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
        if (ImGui::SmallButton(preview_ ? "Stop preview" : "Preview")) {
            if (preview_) preview_.reset();
            else startPreview(ref);
        }
        ImGui::SetItemTooltip("Play what happens when this is set off: its chain's moves, turns and swings,\n"
                              "forward and then back (as the game does the next time). Esc stops it.");
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

// ---- editing ----

namespace {

struct ActionChoice {
    std::uint8_t kind;
    const char* label;
    const char* help;
    bool models, flats;
};
constexpr ActionChoice kActionChoices[] = {
    {logic::action::kNone, "No action", "Takes the action away (and its link).", true, true},
    {logic::action::kMove, "Move", "Slides it, then back the next time.", true, true},
    {logic::action::kTurn, "Turn", "Turns it about its own axis, then back the next time.", true, false},
    {logic::action::kOpenDoor, "Swing open", "A door opens itself; anything else becomes a lockless swinging door "
                                             "(secret walls, trapdoors).", true, false},
    {logic::action::kCloseDoor, "Close door", "A door closes itself.", true, false},
    {logic::action::kLockDoor, "Lock door", "A door locks itself (level 16 if it had no lock).", true, false},
    {logic::action::kUnlockDoor, "Unlock door", "A door unlocks itself.", true, false},
    {logic::action::kTeleport, "Teleport", "Moves the player to the next object.", true, true},
    {logic::action::kShowText, "Show text", "Shows one of the game's messages.", true, true},
    {logic::action::kShowTextWithInput, "Ask a riddle", "Asks one of the game's riddles; the chain goes on only on a "
                                                       "right answer.", true, true},
    {logic::action::kDoorText, "Door text", "Shows a message the first time (castle guards use these).", true, true},
    {logic::action::kHurt22, "Hurt", "Hurts the player: an amount times their level.", true, true},
    {logic::action::kDrainMagicka, "Drain magicka", "Takes magicka from the player.", true, true},
    {logic::action::kCastSpell, "Cast spell", "Casts a spell at the player (then only every 22nd time).", true, true},
    {logic::action::kSetGlobalVar, "Set global variable", "Sets one of the 64 quest variables.", true, true},
    {logic::action::kActivate, "Pass it on", "Does nothing itself; sets off the next object. For levers, plates.",
     true, true},
};

struct TriggerChoice {
    std::uint8_t trigger;
    const char* label;
};
constexpr TriggerChoice kTriggerChoices[] = {
    {logic::trigger::kNone, "Only when set off by another"},
    {logic::trigger::kUse, "Used"},
    {logic::trigger::kWalkOn, "Stood on"},
    {logic::trigger::kWalkInto, "Walked into"},
    {logic::trigger::kAttack, "Hit"},
    {logic::trigger::kUseOrBump, "Used or walked into"},
    {logic::trigger::kUseHitOrBump, "Used, hit or walked into"},
    {logic::trigger::kDoor, "Opened or closed (doors)"},
};

constexpr const char* kDirections[] = {"Up", "Down", "+X", "-X", "+Z", "-Z"};
constexpr const char* kTurnAxes[] = {"X", "Y (upright)", "Z"};

bool usesIndex(std::uint8_t kind) {
    return kind == logic::action::kShowText || kind == logic::action::kShowTextWithInput ||
           kind == logic::action::kDoorText || kind == logic::action::kCastSpell || kind == logic::action::kHurt21;
}

} // namespace

void Viewer::commitObject(const scene::ObjectRef& ref, formats::RdbObject obj) {
    if (!edit_ || !editing(ref)) return;
    edit_->replace({ref.root, ref.object}, std::move(obj));
    editChanged();
}

void Viewer::drawLogicEditor(const scene::ObjectRef& ref, const std::vector<const SceneLink*>& incoming) {
    const auto* current = edit_->object({ref.root, ref.object});
    if (!current) return;
    auto obj = *current;
    const auto& block = edit_->block();
    const auto* m = obj.model();
    const bool door = m && logic::isActionDoor(block, *m);
    const bool flat = obj.flat() != nullptr;
    const auto node = logic::nodeOf(block, obj);
    bool changed = false, endGesture = false;
    auto gesture = [&] {   // a dragged value is one undo step, ended after its last change is in
        if (ImGui::IsItemActivated()) edit_->beginGesture();
        if (ImGui::IsItemDeactivated()) endGesture = true;
    };
    const float width = std::max(120.0f, ImGui::GetContentRegionAvail().x * 0.6f);

    // What it does.
    const std::uint8_t raw = node ? node->rawAction : 0;
    std::string current_label = "No action";
    for (const auto& c : kActionChoices) {
        if (c.kind == raw) current_label = c.label;
    }
    if (node && (raw >= logic::action::kPlusX && raw <= logic::action::kMinusZ)) current_label = "Move";
    else if (node && !std::ranges::any_of(kActionChoices, [&](const ActionChoice& c) { return c.kind == raw; })) {
        current_label = std::format("{} ({})", logic::actionName(node->action), raw);
    }
    ImGui::SetNextItemWidth(width);
    if (ImGui::BeginCombo("Action", current_label.c_str())) {
        for (const auto& c : kActionChoices) {
            if (flat ? !c.flats : !c.models) continue;
            if (ImGui::Selectable(c.label, c.kind == raw)) {
                if (c.kind == logic::action::kMove) {
                    if (!node || !logic::moveOf(*node)) logic::setMove(obj, {logic::Direction::Up, 64, 40});
                } else if (c.kind == logic::action::kTurn) {
                    if (!node || !logic::turnOf(*node)) logic::setTurn(obj, {logic::TurnAxis::Y, 90, 30});
                } else {
                    logic::setAction(obj, c.kind);
                    // Values that make sense straight away.
                    switch (c.kind) {
                        case logic::action::kShowText: logic::setIndex(obj, 4); break;
                        case logic::action::kShowTextWithInput: logic::setIndex(obj, 4); break;
                        case logic::action::kDoorText: logic::setIndex(obj, 5); logic::setAxis(obj, 0); break;
                        case logic::action::kHurt22: case logic::action::kDrainMagicka: logic::setAxis(obj, 2); break;
                        case logic::action::kSetGlobalVar: logic::setAxis(obj, 0); break;
                        default: break;
                    }
                    if (!usesIndex(c.kind) && !node) logic::setIndex(obj, 0);
                }
                changed = true;
            }
            ImGui::SetItemTooltip("%s", c.help);
        }
        ImGui::EndCombo();
    }

    if (door) {
        const auto lockNow = logic::startingLock(block, obj);
        const int index = lockNow ? lockNow->index : 0;
        auto lockLabel = [](int i) {
            const int v = logic::kLockValues[i];
            if (v == 0) return std::string("Not locked");
            return std::format("Level {}{}", v, v >= logic::kMagicLock ? " (magic)" : "");
        };
        ImGui::SetNextItemWidth(width);
        if (ImGui::BeginCombo("Starting lock", lockLabel(index).c_str())) {
            for (int i = 0; i < 16; ++i) {
                if (ImGui::Selectable(lockLabel(i).c_str(), i == index)) {
                    if (auto* mm = std::get_if<formats::RdbModel>(&obj.resource)) {
                        mm->triggerFlagStartingLock = static_cast<std::uint32_t>(i) << 4;   // replaces a trigger
                    }
                    changed = true;
                }
            }
            ImGui::EndCombo();
        }
        ImGui::SetItemTooltip("Picking and bashing work below 20; magic locks need a spell or something that opens it.\n"
                              "A lock replaces the door's own trigger (they share a field).");
    }
    if (node) {
        // When.
        const auto lock = logic::startingLock(block, obj);
        if (lock) {
            ImGui::TextDisabled("Set off: only by another object, or by opening it.");
            ImGui::TextDisabled("(A locked door can't have its own trigger: they share a field.)");
        } else {
            const char* label = "(other)";
            for (const auto& t : kTriggerChoices) {
                if (t.trigger == node->trigger) label = t.label;
            }
            ImGui::SetNextItemWidth(width);
            if (ImGui::BeginCombo("Set off when", label)) {
                for (const auto& t : kTriggerChoices) {
                    if (t.trigger == logic::trigger::kDoor && !door) continue;
                    if (ImGui::Selectable(t.label, t.trigger == node->trigger)) {
                        logic::setTrigger(obj, t.trigger);
                        changed = true;
                    }
                }
                ImGui::EndCombo();
            }
        }

        // How: the values this action reads.
        const std::uint8_t kind = node->action;
        if (const auto move = logic::moveOf(*node)) {
            auto mv = *move;
            bool moved = false;
            int dir = static_cast<int>(mv.direction);
            ImGui::SetNextItemWidth(width);
            if (ImGui::Combo("Direction", &dir, kDirections, 6)) {
                mv.direction = static_cast<logic::Direction>(dir);
                moved = true;
            }
            ImGui::SetNextItemWidth(width);
            if (ImGui::DragInt("Distance", &mv.units, flat ? 8.0f : 4.0f, flat ? 8 : 0, flat ? 2040 : 8192, "%d units")) {
                moved = true;
            }
            gesture();
            ImGui::SameLine();
            ImGui::TextDisabled("%.1f m", mv.units / 40.0);
            if (flat) {
                ImGui::TextDisabled("A flat moves in steps of 8 units, always over 2.5 s.");
            } else {
                float secs = float(mv.duration) / 20.0f;
                ImGui::SetNextItemWidth(width);
                if (ImGui::DragFloat("Time", &secs, 0.05f, 0.0f, 60.0f, "%.2f s")) {
                    mv.duration = static_cast<std::uint32_t>(std::lround(std::max(0.0f, secs) * 20));
                    moved = true;
                }
                gesture();
            }
            if (moved) {
                logic::setMove(obj, mv);
                changed = true;
            }
        } else if (kind == logic::action::kTurn) {
            auto turn = logic::turnOf(*node).value_or(logic::Turn{logic::TurnAxis::Y, 0, node->duration});
            int axis = static_cast<int>(turn.axis);
            float degrees = float(turn.degrees), secs = float(turn.duration) / 20.0f;
            bool turned = false;
            ImGui::SetNextItemWidth(width);
            if (ImGui::Combo("About", &axis, kTurnAxes, 3)) turned = true;
            ImGui::SetNextItemWidth(width);
            if (ImGui::DragFloat("Angle", &degrees, 1.0f, -360.0f, 360.0f, "%.1f deg")) turned = true;
            gesture();
            ImGui::SetNextItemWidth(width);
            if (ImGui::DragFloat("Time", &secs, 0.05f, 0.0f, 60.0f, "%.2f s")) turned = true;
            gesture();
            if (node->description == "LID" || node->description == "WHE") {
                ImGui::TextDisabled("%s models always turn their own fixed way.", node->description.c_str());
            }
            if (turned) {
                logic::setTurn(obj, {static_cast<logic::TurnAxis>(axis), degrees,
                                     static_cast<std::uint32_t>(std::lround(std::max(0.0f, secs) * 20))});
                changed = true;
            }
        } else if (kind == logic::action::kShowText || kind == logic::action::kShowTextWithInput ||
                   kind == logic::action::kDoorText) {
            const std::uint16_t base = kind == logic::action::kShowText            ? logic::kShowTextBase
                                       : kind == logic::action::kShowTextWithInput ? logic::kShowTextWithInputBase
                                                                                   : logic::kDoorTextBase;
            const auto id = static_cast<std::uint16_t>(base + node->index);
            auto preview = [&](std::uint16_t i) {
                auto t = texts_ ? texts_->text(i).value_or("(missing)") : std::string("(no TEXT.RSC)");
                std::ranges::replace(t, '\n', ' ');
                if (t.size() > 60) t = t.substr(0, 57) + "...";
                return std::format("{}: {}", i, t);
            };
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
            if (ImGui::BeginCombo("##text", preview(id).c_str())) {
                if (texts_) {
                    for (const auto i : texts_->idsBetween(std::uint16_t(base + 1), std::uint16_t(base + 99))) {
                        if (ImGui::Selectable(preview(i).c_str(), i == id)) {
                            logic::setIndex(obj, static_cast<std::uint8_t>(i - base));
                            changed = true;
                        }
                        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", texts_->text(i)->c_str());
                    }
                }
                ImGui::EndCombo();
            }
            ImGui::TextDisabled("Only the game's own messages for now (new ones need a Bunnyfall change).");
            if (kind == logic::action::kDoorText) {
                bool hostile = node->axis > 5;
                if (ImGui::Checkbox("Using it again makes people hostile", &hostile)) {
                    logic::setAxis(obj, hostile ? 6 : 0);
                    changed = true;
                }
            }
        } else if (kind == logic::action::kHurt22 || kind == logic::action::kDrainMagicka ||
                   kind == logic::action::kSetGlobalVar || (kind >= 0x16 && kind <= logic::action::kHurt25)) {
            int amount = static_cast<int>(node->axis);
            const char* label = kind == logic::action::kSetGlobalVar ? "Variable" : kind == logic::action::kDrainMagicka
                                                                                      ? "Magicka"
                                                                                      : "Damage x level";
            ImGui::SetNextItemWidth(width);
            if (ImGui::InputInt(label, &amount)) {
                logic::setAxis(obj, static_cast<std::uint32_t>(std::clamp(amount, 0, kind == logic::action::kSetGlobalVar ? 63 : 255)));
                changed = true;
            }
        } else if (kind == logic::action::kCastSpell) {
            int spell = node->index;
            ImGui::SetNextItemWidth(width);
            if (ImGui::InputInt("Spell", &spell)) {
                logic::setIndex(obj, static_cast<std::uint8_t>(std::clamp(spell, 0, 255)));
                changed = true;
            }
            ImGui::SetItemTooltip("A spell number from SPELLS.STD.");
        }
        if (!usesIndex(kind)) {
            int sound = node->index;
            ImGui::SetNextItemWidth(width);
            if (ImGui::InputInt("Sound", &sound)) {
                logic::setIndex(obj, static_cast<std::uint8_t>(std::clamp(sound, 0, 255)));
                changed = true;
            }
            ImGui::SetItemTooltip("Played each time it is set off. 0: none.");
        }

        // The next object.
        if (linkFrom_ == ref) {
            ImGui::TextColored(kWarningText, "Click the next object in a view (Esc or right-click cancels).");
        } else {
            const char* linkLabel = kind == logic::action::kTeleport ? "Pick where it lands" : "Link to...";
            if (ImGui::SmallButton(linkLabel)) linkFrom_ = ref;
            ImGui::SetItemTooltip("Then click the object it sets off, in any view. It must be in this block.");
            if (node->next > 0) {
                ImGui::SameLine();
                if (ImGui::SmallButton("Unlink")) {
                    logic::setNext(obj, -1);
                    changed = true;
                }
            }
        }
        if (!incoming.empty() || node->next > 0) {
            ImGui::SameLine();
            if (ImGui::SmallButton("Take out of chain")) {
                if (endGesture) edit_->endGesture();
                takeOutOfChain(ref);
                return;   // the block changed under `obj`
            }
            ImGui::SetItemTooltip("What sets this off will set off its next object instead.");
        }
    } else if (!incoming.empty()) {
        ImGui::TextDisabled("Give it an action to carry the chain on.");
    }
    if (changed) commitObject(ref, std::move(obj));
    if (endGesture) edit_->endGesture();
}

void Viewer::finishLink(std::optional<scene::Hit> hit) {
    const auto from = *linkFrom_;
    const bool reverse = linkReverse_;
    linkFrom_.reset();
    linkReverse_ = false;
    if (!hit) return;
    const auto to = refOf(hit->kind, hit->index);
    if (!editing(to)) {
        status_ = "A link stays inside one block: pick something in " + edit_->name() + ".";
        return;
    }
    if (to.root == from.root && to.object == from.object) return;
    if (reverse) {   // `to` is what sets `from` off: give it an action if it has none, and a door its opening
        const auto* sourceNow = edit_->object({to.root, to.object});
        if (!sourceNow || !logic::canAct(*sourceNow)) {
            status_ = "Markers and lights can't set anything off.";
            return;
        }
        auto source = *sourceNow;
        auto target = *edit_->object({from.root, from.object});
        const auto& block = edit_->block();
        edit_->beginGesture();
        if (!logic::nodeOf(block, source)) {
            logic::setAction(source, logic::action::kActivate);
            if (!logic::startingLock(block, source)) logic::setTrigger(source, logic::trigger::kUse);
        }
        logic::setNext(source, target.position);
        const auto* tm = target.model();
        if (tm && logic::isActionDoor(block, *tm)) {
            const auto node = logic::nodeOf(block, target);
            if (!node || node->action != logic::action::kOpenDoor) {
                logic::setAction(target, logic::action::kOpenDoor);
                edit_->replace({from.root, from.object}, std::move(target));
            }
        }
        edit_->replace({to.root, to.object}, std::move(source));
        edit_->endGesture();
        editChanged();
        setSelection({from});
        return;
    }
    const auto* src = edit_->object({from.root, from.object});
    const auto* target = edit_->object({to.root, to.object});
    if (!src || !target) return;
    auto obj = *src;
    logic::setNext(obj, target->position);
    commitObject(from, std::move(obj));
    setSelection({from});
}

void Viewer::takeOutOfChain(const scene::ObjectRef& ref) {
    const auto* self = edit_->object({ref.root, ref.object});
    if (!self) return;
    const auto node = logic::nodeOf(edit_->block(), *self);
    const std::int32_t next = node ? node->next : -1;
    const std::int32_t position = self->position;
    edit_->beginGesture();
    const auto& block = edit_->block();
    for (std::size_t r = 0; r < block.roots.size(); ++r) {
        for (std::size_t o = 0; o < block.roots[r].objects.size(); ++o) {
            const auto& other = edit_->block().roots[r].objects[o];
            const auto n = logic::nodeOf(edit_->block(), other);
            if (!n || n->next != position || other.position == position) continue;
            auto changed = other;
            logic::setNext(changed, next);
            edit_->replace({r, o}, std::move(changed));
        }
    }
    auto changedSelf = *edit_->object({ref.root, ref.object});
    logic::setNext(changedSelf, -1);
    edit_->replace({ref.root, ref.object}, std::move(changedSelf));
    edit_->endGesture();
    editChanged();
}

// ---- quick setups ----

namespace {

constexpr std::uint32_t kLeverModel = 61028;   // the classic wall lever (L0Z): used, it turns 45 degrees
constexpr std::uint32_t kPlateModel = 74087;   // the classic floor plate (TSB): stood on, it passes it on
constexpr std::uint32_t kLeverTurnTime = 12;   // 0.6 s, as classic levers

} // namespace

void Viewer::finishSetup(const scene::ObjectRef& placed) {
    const auto setup = *setup_;
    setup_.reset();
    const auto* doorNow = edit_->object({setup.door.root, setup.door.object});
    if (!doorNow) return;
    auto door = *doorNow;
    auto made = *edit_->object({placed.root, placed.object});
    if (setup.kind == Setup::LeverOpensDoor) {
        logic::setTurn(made, {logic::TurnAxis::Z, 45, kLeverTurnTime});
        logic::setTrigger(made, logic::trigger::kUse);
    } else {
        logic::setAction(made, logic::action::kActivate);
        logic::setTrigger(made, logic::trigger::kWalkOn);
    }
    logic::setNext(made, door.position);
    edit_->replace({placed.root, placed.object}, std::move(made));
    const auto node = logic::nodeOf(edit_->block(), door);
    if (!node || node->action != logic::action::kOpenDoor) {
        logic::setAction(door, logic::action::kOpenDoor);   // opening also unlocks
        edit_->replace({setup.door.root, setup.door.object}, std::move(door));
    }
    setSelection({placed});
    status_ = setup.kind == Setup::LeverOpensDoor ? "The lever opens the door (and unlocks it)."
                                                  : "Standing on the plate opens the door (and unlocks it).";
}

void Viewer::setDoorLock(const scene::ObjectRef& ref, int lockIndex) {
    auto obj = *edit_->object({ref.root, ref.object});
    if (auto* m = std::get_if<formats::RdbModel>(&obj.resource)) {
        m->triggerFlagStartingLock = static_cast<std::uint32_t>(lockIndex) << 4;
        commitObject(ref, std::move(obj));
    }
}

void Viewer::applyQuickSetup(const scene::ObjectRef& ref, QuickSetup kind) {
    const auto* now = edit_->object({ref.root, ref.object});
    if (!now || !logic::canAct(*now)) return;
    auto obj = *now;
    const auto node = logic::nodeOf(edit_->block(), obj);
    bool pick = false;
    switch (kind) {
        case QuickSetup::UsedSetsOff:
        case QuickSetup::PlateSetsOff:
            if (!node) logic::setAction(obj, logic::action::kActivate);
            logic::setTrigger(obj, kind == QuickSetup::UsedSetsOff ? logic::trigger::kUse : logic::trigger::kWalkOn);
            pick = true;
            break;
        case QuickSetup::SlidesUp: {
            int height = 128;
            if (const auto* m = obj.model()) {
                if (const auto* info = catalog_.model(edit_->block().modelRefs[m->modelIndex].modelIdNum)) {
                    height = std::max(8, static_cast<int>(std::lround(info->bounds.size().y)));
                }
            }
            logic::setMove(obj, {logic::Direction::Up, height, 40});
            logic::setTrigger(obj, logic::trigger::kUse);
            break;
        }
        case QuickSetup::SwingsOpen:
            logic::setAction(obj, logic::action::kOpenDoor);
            logic::setTrigger(obj, logic::trigger::kUse);
            break;
        case QuickSetup::Teleporter:
            logic::setAction(obj, logic::action::kTeleport);
            logic::setTrigger(obj, logic::trigger::kWalkInto);
            pick = true;
            break;
        case QuickSetup::TrapHurts:
            logic::setAction(obj, logic::action::kHurt22);
            logic::setAxis(obj, 2);
            logic::setTrigger(obj, logic::trigger::kWalkOn);
            break;
    }
    commitObject(ref, std::move(obj));
    setSelection({ref});
    if (pick) {
        linkFrom_ = ref;
        linkReverse_ = false;
        status_ = kind == QuickSetup::Teleporter ? "Now click where it takes the player." : "Now click what it sets off.";
    }
}

void Viewer::drawViewContextMenu() {
    if (!context_) return;
    if (!context_->ref) {
        ImGui::TextDisabled("Nothing here");
        if (edit_) {
            ImGui::Separator();
            if (ImGui::MenuItem("Place a lever")) armModel(kLeverModel);
            if (ImGui::MenuItem("Place a pressure plate")) armModel(kPlateModel);
        }
        return;
    }
    const auto ref = *context_->ref;
    if (ref.block >= scene_.blocks.size()) return;
    const auto& block = scene_.blocks[ref.block].data;
    if (ref.root >= block.roots.size() || ref.object >= block.roots[ref.root].objects.size()) return;
    const auto& obj = block.roots[ref.root].objects[ref.object];
    ImGui::TextColored(ImGui::GetStyleColorVec4(ImGuiCol_CheckMark), "%s", logic::objectLabel(block, obj).c_str());
    ImGui::Separator();
    if (ImGui::MenuItem("Properties", "Alt+Enter")) openProperties(context_->at);
    if (ImGui::MenuItem("Frame", "F")) frameSelection();
    const bool linked = std::ranges::any_of(sceneLinks(), [&](const SceneLink& l) { return l.from == ref || l.to == ref; });
    if (linked && ImGui::MenuItem("Select its whole chain")) {
        setSelection({ref});
        const auto chain = selectedChains();
        std::vector<scene::ObjectRef> refs(chain.begin(), chain.end());
        std::erase(refs, ref);
        refs.push_back(ref);
        setSelection(std::move(refs));
    }
    if (!editing(ref)) {
        const auto& sb = scene_.blocks[ref.block];
        if (sb.bsaIndex != scene::kNoBsaIndex || isProjectBlock(sb.name)) {
            ImGui::Separator();
            if (ImGui::MenuItem("Edit this block")) beginEdit(ref.block);
        }
        return;
    }
    if (!logic::canAct(obj)) return;
    const auto* m = obj.model();
    const bool door = m && logic::isActionDoor(block, *m);
    const auto node = logic::nodeOf(block, obj);

    ImGui::SeparatorText("Triggers");
    if (node) {
        if (ImGui::MenuItem("Link to...")) {
            linkFrom_ = ref;
            linkReverse_ = false;
        }
        if (ImGui::MenuItem("Take out of chain")) takeOutOfChain(ref);
        if (ImGui::MenuItem("Remove its action")) {
            auto o = obj;
            logic::setAction(o, 0);
            commitObject(ref, std::move(o));
        }
    }
    if (door) {
        if (ImGui::MenuItem("Lever that opens it...")) {
            armModel(kLeverModel);
            setup_ = PendingSetup{Setup::LeverOpensDoor, ref};
            status_ = "Click a wall to put the lever on ([ and ] turn it).";
        }
        if (ImGui::MenuItem("Pressure plate that opens it...")) {
            armModel(kPlateModel);
            setup_ = PendingSetup{Setup::PlateOpensDoor, ref};
            status_ = "Click a floor to put the plate on.";
        }
        if (ImGui::MenuItem("Opened by... (pick it)")) {
            linkFrom_ = ref;
            linkReverse_ = true;
        }
        ImGui::SetItemTooltip("Click anything in the block: it will open this door when used (or as it is set off).");
        if (ImGui::BeginMenu("Starting lock")) {
            const auto lock = logic::startingLock(block, obj);
            for (int i = 0; i < 16; ++i) {
                const int v = logic::kLockValues[i];
                const auto label = v == 0 ? std::string("Not locked")
                                          : std::format("Level {}{}", v, v >= logic::kMagicLock ? " (magic)" : "");
                if (ImGui::MenuItem(label.c_str(), nullptr, (lock ? lock->index : 0) == i)) setDoorLock(ref, i);
            }
            ImGui::EndMenu();
        }
        return;
    }
    if (ImGui::MenuItem("When used, sets off... (pick it)")) applyQuickSetup(ref, QuickSetup::UsedSetsOff);
    if (ImGui::MenuItem("When stood on, sets off... (pick it)")) applyQuickSetup(ref, QuickSetup::PlateSetsOff);
    if (ImGui::MenuItem("Set off by... (pick it)")) {
        linkFrom_ = ref;
        linkReverse_ = true;
    }
    if (m) {
        if (ImGui::MenuItem("Secret wall: slides up when used")) applyQuickSetup(ref, QuickSetup::SlidesUp);
        if (ImGui::MenuItem("Secret door: swings open when used")) applyQuickSetup(ref, QuickSetup::SwingsOpen);
    }
    if (ImGui::MenuItem("Teleporter: walking into it goes to... (pick it)")) applyQuickSetup(ref, QuickSetup::Teleporter);
    if (ImGui::MenuItem("Trap: hurts whoever stands on it")) applyQuickSetup(ref, QuickSetup::TrapHurts);
}

// ---- preview ----

void Viewer::startPreview(const scene::ObjectRef& ref) {
    const auto& block = scene_.blocks[ref.block].data;
    Preview p;
    p.stamp = sceneStamp_;
    for (const auto& step : logic::chainFrom(block, {ref.root, ref.object})) {
        if (!step.node) continue;
        const auto& o = block.roots[step.slot.root].objects[step.slot.object];
        const bool door = o.model() && logic::isActionDoor(block, *o.model());
        const auto motion = logic::motionOf(*step.node, door);
        if (!motion) continue;
        p.items.push_back({instanceOf({ref.block, step.slot.root, step.slot.object}), *motion});
        p.length = std::max(p.length, motion->seconds);
    }
    if (p.items.empty()) {
        status_ = "Nothing in this chain moves or turns.";
        return;
    }
    preview_ = std::move(p);
}

void Viewer::poseForPreview(double seconds) {
    posedModels_.clear();
    posedFlats_.clear();
    if (!preview_) return;
    if (preview_->stamp != sceneStamp_) {   // the block changed: what was previewed is gone
        preview_.reset();
        return;
    }
    constexpr double kHold = 0.75;
    auto& p = *preview_;
    p.time += seconds;
    const double length = std::max(p.length, 0.25);
    const double back = length + kHold;
    if (p.time > back + length + kHold) {
        preview_.reset();
        return;
    }
    for (const auto& item : p.items) {
        if (!item.instance) continue;
        const double s = item.motion.seconds;
        double t = 0;
        if (p.time < back) t = s > 0 ? std::min(1.0, p.time / s) : 1.0;
        else t = 1.0 - (s > 0 ? std::min(1.0, (p.time - back) / s) : 1.0);
        const auto& mv = item.motion.move;
        const catalog::Vec3 offset{mv.x * t, mv.y * t, mv.z * t};
        if (item.instance->first == scene::Hit::Kind::Model) {
            auto& inst = scene_.models[item.instance->second];
            posedModels_.emplace_back(item.instance->second, inst.transform);
            inst.transform.rotation = inst.transform.rotation * logic::turnAt(item.motion, t);
            inst.transform.translation = inst.transform.translation + offset;
        } else if (item.instance->first == scene::Hit::Kind::Flat) {
            auto& inst = scene_.flats[item.instance->second];
            posedFlats_.emplace_back(item.instance->second, inst.position);
            inst.position = inst.position + offset;
        }
    }
}

void Viewer::endPreviewPose() {
    for (const auto& [i, t] : posedModels_) scene_.models[i].transform = t;
    for (const auto& [i, p] : posedFlats_) scene_.flats[i].position = p;
    posedModels_.clear();
    posedFlats_.clear();
}

} // namespace de::app
