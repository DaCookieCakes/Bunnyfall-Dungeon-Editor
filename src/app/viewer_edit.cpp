// The block editor: placing, moving and changing the things in one block, and saving them as a patch.

#include "app/viewer.h"

#include "edit/placement.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <format>

namespace de::app {

namespace {

constexpr std::int32_t kQuarterTurn = 512;   // file rotation units
constexpr int kBigStep = 64;                 // Shift + arrow keys

ImTextureRef textureRef(const render::GpuTexture& t) {
    return ImTextureRef(static_cast<ImTextureID>(static_cast<std::uintptr_t>(t.id)));
}

/// A preview of a texture fitted into a square of `side` pixels, keeping its shape.
void textureImage(const render::GpuTexture& t, float side) {
    const float w = float(std::max(t.width, 1)), h = float(std::max(t.height, 1));
    const float scale = side / std::max(w, h);
    ImGui::Image(textureRef(t), {w * scale, h * scale});
}

} // namespace

// ---- starting and stopping ----

void Viewer::beginEdit(std::size_t sceneBlock) {
    const auto& block = scene_.blocks[sceneBlock];
    if (edit_ && edit_->name() == block.name) {
        editBlock_ = sceneBlock;
        return;
    }
    if (edit_ && edit_->dirty()) {
        status_ = "Save or discard the edits to " + edit_->name() + " first.";
        return;
    }
    if (const auto* own = project_.block(block.name)) {
        // The project's own block: "original" is its template as the game has it, or empty.
        formats::RdbBlock original = project::emptyBlock();
        if (own->templateName) {
            const auto index = arena2_.blocks.findByName(*own->templateName);
            auto classic = index ? formats::readClassicRdbBlock(arena2_.blocks, *index)
                                 : Result<formats::RdbBlock>(fail("no template " + *own->templateName));
            if (!classic) {
                status_ = classic.error().message;
                return;
            }
            original = std::move(*classic);
        }
        edit_.emplace(block.name, scene::kNoBsaIndex, own->savePosition, std::move(original), block.data);
    } else {
        if (block.bsaIndex == scene::kNoBsaIndex) return;
        auto classic = formats::readClassicRdbBlock(arena2_.blocks, block.bsaIndex);
        if (!classic) {
            status_ = classic.error().message;
            return;
        }
        edit_.emplace(block.name, block.bsaIndex, arena2_.blocks.entry(block.bsaIndex).offset, std::move(*classic),
                      block.data);
    }
    editBlock_ = sceneBlock;
    status_ = "Editing " + block.name + ".";
}

void Viewer::endEdit() {
    edit_.reset();
    stopPlacing();
    grabbing_ = false;
    confirmDiscard_ = false;
}

void Viewer::discardEdit() {
    if (!edit_) return;
    const auto name = edit_->name();
    endEdit();
    auto saved = blockSource()(name);
    if (!saved) {
        status_ = saved.error().message;
        return;
    }
    for (auto& b : scene_.blocks) {
        if (b.name == name) b.data = *saved;
    }
    scene::rebuildInstances(scene_);
    ++sceneStamp_;
    clearSelection();
    status_ = "Discarded the unsaved changes to " + name + ".";
}

void Viewer::editChanged() {
    for (auto& b : scene_.blocks) {
        if (b.name == edit_->name()) b.data = edit_->block();
    }
    issuesStale_ = true;   // doorways may have changed
    scene::rebuildInstances(scene_);
    ++sceneStamp_;
    // Undo and redo can take objects away: forget what is no longer there.
    auto kept = selection_;
    std::erase_if(kept, [&](const scene::ObjectRef& r) { return editing(r) && !edit_->object({r.root, r.object}); });
    if (kept.size() != selection_.size()) setSelection(std::move(kept));
}

void Viewer::saveEdit() {
    if (!edit_) return;
    if (auto* own = project_.block(edit_->name())) {
        own->data = edit_->block();
        edit_->markSaved();
        markProjectChanged();
        saveProject();
        return;
    }
    if (!patches_) {
        status_ = "No patch folder: start the editor with --patches <folder>.";
        return;
    }
    if (modDir_ && !patchOverride_) {   // the patch lands in the mod's folder: Bunnyfall must know that folder as a mod
        if (auto r = project::ensureModManifest(project_, *modDir_); !r) {
            status_ = "Not saved: " + r.error().message;
            return;
        }
    }
    auto saved = patches_->save(arena2_.blocks, edit_->bsaIndex(), edit_->block());
    if (!saved) {
        status_ = "Not saved: " + saved.error().message;
        return;
    }
    edit_->markSaved();
    const auto file = patches_->fileFor(edit_->name()).filename().string();
    status_ = *saved == exporter::PatchFolder::Saved::Written
                  ? "Saved " + file + "."
                  : edit_->name() + " is back to the original, so " + file + " was removed.";
}

void Viewer::requestQuit() {
    if (hasUnsavedEdits()) confirmQuit_ = true;
    else quit_ = true;
}

bool Viewer::editing(const scene::ObjectRef& ref) const {
    return edit_ && ref.block < scene_.blocks.size() && scene_.blocks[ref.block].name == edit_->name();
}

std::optional<edit::ObjectSlot> Viewer::selectedSlot() const {
    const auto p = primary();
    if (!p || !editing(*p)) return std::nullopt;
    return edit::ObjectSlot{p->root, p->object};
}

// ---- where things go ----

std::optional<Viewer::SurfacePoint> Viewer::surfaceAt(double x, double y) {
    auto options = scene::PickOptions{.flats = false, .markers = false, .models = true, .ignore = std::nullopt, .lightSize = 0, .skip = {}};
    // Hidden things are not there to stand on, nor is what is being moved.
    options.skip = [this](scene::Hit::Kind kind, std::size_t i) {
        return isHidden(kind, i) || (grabbing_ && isSelected(refOf(kind, i)));
    };
    const auto hit = pickAt(x, y, options);
    if (!hit || hit->kind != scene::Hit::Kind::Model) return std::nullopt;
    const auto ray = rayAt(x, y);
    const catalog::Vec3 point{ray.origin.x + ray.direction.x * hit->distance, ray.origin.y + ray.direction.y * hit->distance,
                              ray.origin.z + ray.direction.z * hit->distance};
    return SurfacePoint{point, hit->normal, scene_.models[hit->index].ref.block};
}

std::pair<double, double> Viewer::entitySize(const formats::RdbObject& obj) {
    if (const auto* f = obj.flat()) return textures_.flatSize(f->textureArchive(), f->textureRecord());
    if (obj.light()) return {render::kLightGizmoSize, render::kLightGizmoSize};
    return {0, 0};   // a model stands on its own origin
}

std::array<std::int32_t, 3> Viewer::toFile(catalog::Vec3 world, std::size_t sceneBlock,
                                           std::optional<std::array<int, 3>> steps) const {
    const auto& origin = scene_.blocks[sceneBlock].origin;
    const auto s = steps.value_or(std::array<int, 3>{snap_, 1, snap_});
    return {edit::snapTo(world.x - origin.x, s[0]), edit::snapTo(-(world.y - origin.y), s[1]),
            edit::snapTo(world.z - origin.z, s[2])};
}

bool Viewer::isPiece(std::uint32_t modelId) const {
    const auto* info = catalog_.model(modelId);
    return info && (info->kind == catalog::ModelKind::Architecture || info->kind == catalog::ModelKind::Door ||
                    info->kind == catalog::ModelKind::ExitDoor);
}

bool Viewer::selectionHasPieces() const {
    if (!edit_) return false;
    for (const auto& s : selectedSlots()) {
        const auto* m = edit_->object(s)->model();
        if (m && isPiece(edit_->block().modelRefs[m->modelIndex].modelIdNum)) return true;
    }
    return false;
}

std::array<int, 3> Viewer::moveSteps(bool pieces) const {
    if (pieces && pieceSnap_) return {std::max(snap_, 64), 128, std::max(snap_, 64)};
    return {snap_, snap_, snap_};
}

void Viewer::placeAt(double x, double y) {
    if (!edit_ || !placingAnything()) return;
    const auto s = placementSurface(x, y);
    if (!s) {
        status_ = "Nothing there to place on, and outside every block.";
        return;
    }
    if (scene_.blocks[s->sceneBlock].name != edit_->name()) {
        status_ = "That is in " + scene_.blocks[s->sceneBlock].name + "; you are editing " + edit_->name() + ".";
        return;
    }
    if (placingPrefab_) {
        const auto at = prefabPlacement(x, y);
        if (!at) return;
        const auto& prefab = prefabs_[*placingPrefab_];
        const auto slots = project::placePrefab(*edit_, prefab, at->first, edit::modelYRotationFor(placeTurn_));
        if (!slots) {
            status_ = "This block has no model slots left for the prefab's models.";
            return;
        }
        editChanged();
        std::vector<scene::ObjectRef> refs;
        for (const auto& placed : *slots) refs.push_back({at->second, placed.root, placed.object});
        setSelection(std::move(refs));
        status_ = std::format("Placed \"{}\" ({} objects). Click again for another; [ ] turn, right-click or Esc stops.",
                              prefab.name, slots->size());
        return;
    }
    if (placingModel_) {
        const auto at = modelPlacement(x, y);
        const auto* info = catalog_.model(*placingModel_);
        if (!at || !info) return;
        // Already snapped (to the grid, or by vertex snap to a rim).
        const auto file = toFile(at->first.transform.translation, at->second, std::array<int, 3>{1, 1, 1});
        formats::RdbObject obj;
        obj.x = file[0];
        obj.y = file[1];
        obj.z = file[2];
        formats::RdbModel m;
        m.yRotation = placeTurn_;
        obj.resource = m;
        if (setup_) edit_->beginGesture();   // the model and its setup are one undo step
        const auto slot = edit_->addModel(*placingModel_, info->description, std::move(obj));
        if (!slot) {
            if (setup_) edit_->endGesture();
            status_ = "This block already uses 750 different models; no slot is free for another.";
            return;
        }
        if (setup_) {
            finishSetup({at->second, slot->root, slot->object});
            edit_->endGesture();
            stopPlacing();   // a setup places one
            editChanged();
            return;
        }
        editChanged();
        setSelection({scene::ObjectRef{at->second, slot->root, slot->object}});
        status_ = std::format("Placed model {} in {}. Click again for another; [ ] turn, right-click or Esc stops.",
                              *placingModel_, edit_->name());
        return;
    }
    auto obj = edit::makeEntity(*placing_, 0, 0, 0, sceneryArchive_, sceneryRecord_);
    if (*placing_ == edit::EntityKind::FixedEnemy) {
        edit::setFixedEnemyType(std::get<formats::RdbFlat>(obj.resource), enemyType_);
    }
    const auto [w, h] = entitySize(obj);
    const auto file = toFile(edit::restingCentre(s->point, s->normal, w, h), s->sceneBlock);
    obj.x = file[0];
    obj.y = file[1];
    obj.z = file[2];
    const auto slot = edit_->add(std::move(obj));
    editChanged();
    setSelection({scene::ObjectRef{s->sceneBlock, slot.root, slot.object}});
    status_ = std::format("Placed {} in {}. Click again to place another; right-click or Esc to stop.",
                          edit::entityInfo(*placing_).name, edit_->name());
}

// ---- moving ----

void Viewer::startGrab() {
    if (!edit_ || selectedSlots().empty() || drag_.kind != Drag::Kind::None) return;
    stopPlacing();
    beginTransform();
    grabBlock_ = movingBlock_;
    grabbing_ = true;
    status_ = "Moving: move the mouse over the dungeon, left-click to put it down, right-click or Esc to cancel.";
    grabTo(mouseX_, mouseY_);
}

void Viewer::grabTo(double x, double y) {
    if (!grabbing_ || moving_.empty()) return;
    // The first of the snapshot (the primary, when it is in this block) follows the pointer; the rest keep their
    // places around it.
    const auto& obj = moving_.front().start;
    catalog::Vec3 centre;
    if (views_[pointerView_].kind != ViewKind::Perspective) {
        // An orthographic view moves the object in its own plane, as in Hammer: the top view keeps its height, the
        // front and side views their depth.
        const auto& origin = scene_.blocks[grabBlock_].origin;
        const auto here = catalog::fromFilePosition(obj.x, obj.y, obj.z) + origin;
        const auto ray = rayAt(x, y);
        const auto f = ray.direction;
        const double along = (here.x - ray.origin.x) * f.x + (here.y - ray.origin.y) * f.y + (here.z - ray.origin.z) * f.z;
        centre = {ray.origin.x + f.x * along, ray.origin.y + f.y * along, ray.origin.z + f.z * along};
    } else {
        const auto s = surfaceAt(x, y);
        if (!s) return;
        const auto [w, h] = entitySize(obj);
        centre = obj.model() ? s->point : edit::restingCentre(s->point, s->normal, w, h);
    }
    // Measured from the block it was picked up in, wherever the pointer is.
    const auto pieces = selectionHasPieces();
    auto steps = moveSteps(pieces);
    if (!pieces && views_[pointerView_].kind == ViewKind::Perspective) steps[1] = 1;   // resting on what it is over
    const auto file = toFile(centre, grabBlock_, steps);
    const auto offset = vertexSnapOffset({file[0] - obj.x, file[1] - obj.y, file[2] - obj.z});
    const auto* now = edit_->object(moving_.front().slot);
    if (now && now->x == obj.x + offset[0] && now->y == obj.y + offset[1] && now->z == obj.z + offset[2]) return;
    applyTransform(offset, 0);
}

void Viewer::endGrab(bool keep) {
    if (!grabbing_) return;
    grabbing_ = false;
    endTransform(keep);
    status_ = keep ? "Moved." : "Move cancelled.";
}

void Viewer::nudge(int dx, int dy, int dz) {
    if (!edit_ || selectedSlots().empty()) return;
    beginTransform();
    applyTransform({dx, dy, dz}, 0);
    endTransform(true);
}

void Viewer::turnSelected(std::int32_t turn) {
    if (!edit_ || selectedSlots().empty()) return;
    beginTransform();
    applyTransform({0, 0, 0}, turn);
    endTransform(true);
}

void Viewer::deleteSelected() {
    if (!edit_) return;
    auto slots = selectedSlots();
    if (slots.empty()) return;
    // Last first, so the places of the rest stay right; one step of undo.
    std::ranges::sort(slots, [](const auto& a, const auto& b) { return std::pair(a.root, a.object) > std::pair(b.root, b.object); });
    edit_->beginGesture();
    for (const auto& s : slots) {
        edit_->remove(s);
        // Things hidden by hand in this block keep pointing at the same objects.
        std::vector<scene::ObjectRef> hidden(hidden_.begin(), hidden_.end());
        hidden_.clear();
        for (auto r : hidden) {
            if (editing(r) && r.root == s.root) {
                if (r.object == s.object) continue;
                if (r.object > s.object) --r.object;
            }
            hidden_.insert(r);
        }
    }
    edit_->endGesture();
    clearSelection();
    editChanged();
    status_ = std::format("Deleted {}.", slots.size());
}

void Viewer::duplicateSelected() {
    if (!edit_) return;
    const auto slots = selectedSlots();
    if (slots.empty()) return;
    const int step = snap_ > 1 ? snap_ : 16;
    const auto p = primary();
    const std::size_t block = p && editing(*p) ? p->block : editBlock_;
    std::vector<scene::ObjectRef> copies;
    edit_->beginGesture();
    for (const auto& s : slots) {
        if (const auto copy = edit_->duplicate(s, step, 0)) copies.push_back({block, copy->root, copy->object});
    }
    edit_->endGesture();
    editChanged();
    setSelection(std::move(copies));
    status_ = std::format("Duplicated {}.", selection_.size());
}

bool Viewer::handleEditKey(const SDL_KeyboardEvent& key) {
    const bool ctrl = (key.mod & SDL_KMOD_CTRL) != 0;
    const bool shift = (key.mod & SDL_KMOD_SHIFT) != 0;
    if (key.key == SDLK_Z && shift && !ctrl) {   // maximise the view under the pointer, or go back to four
        maximised_ = maximised_ ? std::nullopt : std::optional(pointerView_);
        return true;
    }
    if (key.key == SDLK_RETURN && (key.mod & SDL_KMOD_ALT) && !selection_.empty()) {   // Hammer's Alt+Enter
        openProperties(ImGui::GetIO().MousePos);
        return true;
    }
    if (key.key == SDLK_L && !(key.mod & (SDL_KMOD_CTRL | SDL_KMOD_ALT))) {
        cycleLinkMode();
        return true;
    }
    if (key.key == SDLK_HOME) {
        frameOrthoViews();
        return true;
    }
    if (key.key == SDLK_ESCAPE) {
        if (linkFrom_) linkFrom_.reset();
        else if (preview_) preview_.reset();
        else if (!moving_.empty() && !grabbing_) {   // a handle drag
            endTransform(false);
            drag_ = {};
        } else if (grabbing_) endGrab(false);
        else if (placingAnything()) stopPlacing();
        else clearSelection();
        return true;
    }
    if (drag_.kind != Drag::Kind::None) return false;
    if ((placingModel_ || placingPrefab_) && (key.key == SDLK_LEFTBRACKET || key.key == SDLK_RIGHTBRACKET)) {
        // Turn the model about to be placed: a quarter turn, or with Shift the turn snap.
        const std::int32_t step = shift ? std::max(turnSnap_, 1) : kQuarterTurn;
        const auto turn = edit::modelYRotationFor(key.key == SDLK_RIGHTBRACKET ? step : -step);
        placeTurn_ = ((placeTurn_ + turn) % catalog::kRotationUnitsPerTurn + catalog::kRotationUnitsPerTurn) %
                     catalog::kRotationUnitsPerTurn;
        return true;
    }
    if (ctrl && key.key == SDLK_A) {
        selectAll();
        return true;
    }
    if (!ctrl && !shift) {
        switch (key.key) {
            case SDLK_H: hideSelected(); return true;
            case SDLK_U: showAll(); return true;
            case SDLK_I: toggleIsolate(); return true;
            case SDLK_F: frameSelection(); return true;
            case SDLK_V:
                vertexSnap_ = !vertexSnap_;
                status_ = vertexSnap_ ? "Vertex snap on: models meet still models rim to rim." : "Vertex snap off.";
                return true;
            case SDLK_R:
                gizmoMode_ = gizmoMode_ == GizmoMode::Move ? GizmoMode::Turn : GizmoMode::Move;
                status_ = gizmoMode_ == GizmoMode::Move ? "Move handles." : "Turn handles (3D and Top views).";
                return true;
            default: break;
        }
    }
    if (!edit_ || grabbing_) return false;
    if (ctrl && key.key == SDLK_Z) {
        if (shift ? edit_->redo() : edit_->undo()) editChanged();
        return true;
    }
    if (ctrl && key.key == SDLK_Y) {
        if (edit_->redo()) editChanged();
        return true;
    }
    if (ctrl && key.key == SDLK_S) {
        saveEdit();
        return true;
    }
    if (ctrl && key.key == SDLK_D) {
        duplicateSelected();
        return true;
    }
    if (ctrl) return false;
    if (key.key == SDLK_DELETE || key.key == SDLK_BACKSPACE) {
        deleteSelected();
        return true;
    }
    if (key.key == SDLK_G) {
        startGrab();
        return true;
    }
    // Arrows move along whichever world axis is closest to the camera's own forward and right.
    const int step = shift ? kBigStep : (snap_ > 1 ? snap_ : 16);
    const auto f = camera_.forward();
    const bool alongX = std::abs(f.x) > std::abs(f.z);
    const int fx = alongX ? (f.x > 0 ? 1 : -1) : 0, fz = alongX ? 0 : (f.z > 0 ? 1 : -1);
    const int rx = fz, rz = -fx;   // right of forward, in this left-handed space
    switch (key.key) {
        case SDLK_UP: nudge(fx * step, 0, fz * step); return true;
        case SDLK_DOWN: nudge(-fx * step, 0, -fz * step); return true;
        case SDLK_RIGHT: nudge(rx * step, 0, rz * step); return true;
        case SDLK_LEFT: nudge(-rx * step, 0, -rz * step); return true;
        case SDLK_PAGEUP: nudge(0, -step, 0); return true;    // file Y points down
        case SDLK_PAGEDOWN: nudge(0, step, 0); return true;
        default: return false;
    }
}

// ---- the scripted check ----

Result<std::string> Viewer::checkEdit(const std::filesystem::path& patchDir) {
    std::string log;
    std::size_t start = 0;
    while (start < scene_.blocks.size() && !scene_.blocks[start].isStartingBlock) ++start;
    if (start == scene_.blocks.size()) return fail("no starting block");
    setPatchFolder(patchDir);
    beginEdit(start);
    if (!edit_) return fail("could not start editing: " + status_);
    log += "editing " + edit_->name() + "\n";
    const auto before = edit_->block().objectCount();

    // Points across the lower middle of the view: where the clicks land.
    const double cx = viewWidth_ / 2, cy = viewHeight_ * 0.62;
    int placed = 0;
    std::vector<scene::ObjectRef> placedRefs;
    for (std::size_t i = 0; i < edit::entityKinds().size(); ++i) {
        const auto& info = edit::entityKinds()[i];
        placing_ = info.kind;
        const double x = cx + (double(i) - 4) * viewWidth_ * 0.07;
        const auto count = edit_->block().objectCount();
        placeAt(x, cy);
        if (edit_->block().objectCount() != count + 1) return fail(std::format("placing {} failed: {}", info.name, status_));
        ++placed;
        placedRefs.push_back(*primary());
    }
    placing_.reset();
    log += std::format("placed {} entities ({} -> {} objects)\n", placed, before, edit_->block().objectCount());

    // A model from the palette (the most used prop), twice: the second shares the first's model slot.
    const catalog::ModelInfo* prop = nullptr;
    for (const auto& [id, info] : catalog_.models()) {
        if (info.kind == catalog::ModelKind::Prop && (!prop || info.uses > prop->uses)) prop = &info;
    }
    if (!prop) return fail("no props in the catalog");
    armModel(prop->id);
    const auto beforeModels = edit_->block().objectCount();
    placeAt(cx - viewWidth_ * 0.1, cy);
    placeAt(cx + viewWidth_ * 0.1, cy);
    stopPlacing();
    if (edit_->block().objectCount() != beforeModels + 2) return fail("placing a model: " + status_);
    const auto propRef = primary();
    if (!propRef) return fail("the placed model is not selected");
    const auto propSlot = edit_->modelSlot(prop->id);
    if (!propSlot) return fail("the placed model has no model slot");
    log += std::format("placed model {} (\"{}\") twice, model slot {}\n", prop->id, prop->description, *propSlot);

    // An architecture piece lands on the classic grid (piece snap).
    const catalog::ModelInfo* piece = nullptr;
    for (const auto& [id, info] : catalog_.models()) {
        if (info.kind == catalog::ModelKind::Architecture && (!piece || info.uses > piece->uses)) piece = &info;
    }
    armModel(piece->id);
    placeAt(cx, cy);
    stopPlacing();
    const auto pieceSlot = selectedSlot();
    const auto pieceRef = primary();
    if (!pieceSlot) return fail("placing a piece: " + status_);
    const auto* placedPiece = edit_->object(*pieceSlot);
    if (placedPiece->x % 64 || placedPiece->z % 64 || placedPiece->y % 128) {
        return fail(std::format("piece off the grid at ({}, {}, {})", placedPiece->x, placedPiece->y, placedPiece->z));
    }
    log += std::format("placed piece {} on the classic grid at ({}, {}, {})\n", piece->id, placedPiece->x, placedPiece->y,
                       placedPiece->z);

    // Its texture variants: the whole selection to another texture set, then back with undo.
    if (piece->twins.empty()) return fail("the most used piece has no texture variants");
    const auto* twin = catalog_.model(piece->twins.front());
    selectionToTextureSet(twin->textureArchive);
    const auto shown = edit_->block().modelRefs[edit_->object(*pieceSlot)->model()->modelIndex].modelIdNum;
    if (catalog_.model(shown)->textureArchive != twin->textureArchive) return fail("swapping textures: " + status_);
    edit_->undo();
    editChanged();
    log += std::format("piece {} swapped to textures {} (model {}) and back\n", piece->id, twin->textureArchive, shown);

    // A prefab of the piece and the last three entities, placed a quarter turned beside them, then undone.
    setSelection({placedRefs.end() - 3, placedRefs.end()});
    clickSelect(scene::Hit{scene::Hit::Kind::Model, *scene::modelIndexOf(scene_, *pieceRef), 0, {}}, true, false);
    savePrefabFromSelection("Check prefab");
    const auto made = std::ranges::find_if(prefabs_, [](const auto& p) { return p.name == "Check prefab"; });
    if (made == prefabs_.end()) return fail("saving a prefab: " + status_);
    armPrefab(static_cast<std::size_t>(made - prefabs_.begin()));
    placeTurn_ = 512;
    const auto beforePrefab = edit_->block().objectCount();
    placeAt(cx, cy);
    const auto prefabCount = made->objects().size();
    stopPlacing();
    placeTurn_ = 0;
    if (edit_->block().objectCount() != beforePrefab + prefabCount) return fail("placing the prefab: " + status_);
    edit_->undo();
    editChanged();
    std::filesystem::remove(made->file);
    reloadPrefabs();
    log += std::format("prefab of {} objects saved, placed turned, undone\n", prefabCount);
    setSelection({*pieceRef});

    // Move the last one with the grab code, then cancel one move and keep another.
    const auto slot = selectedSlot();
    if (!slot) return fail("nothing selected after placing");
    const auto origin = *edit_->object(*slot);
    mouseX_ = cx;
    mouseY_ = viewHeight_ * 0.75;
    startGrab();
    grabTo(cx + 40, viewHeight_ * 0.8);
    endGrab(false);
    if (edit_->object(*slot)->x != origin.x || edit_->object(*slot)->z != origin.z) return fail("cancelled move moved it");
    startGrab();
    grabTo(cx + 60, viewHeight_ * 0.8);
    endGrab(true);
    const auto moved = *edit_->object(*slot);
    log += std::format("moved ({}, {}, {}) -> ({}, {}, {})\n", origin.x, origin.y, origin.z, moved.x, moved.y, moved.z);

    // Undo the move, redo it.
    if (!edit_->undo()) return fail("nothing to undo");
    editChanged();
    if (edit_->object(*slot)->x != origin.x) return fail("undo did not put it back");
    if (!edit_->redo()) return fail("nothing to redo");
    editChanged();
    log += "undo and redo ok\n";

    // Vertex snap: a copy of corridor 67000 moved to near the end of the first goes exactly onto it, end to end.
    if (const auto* corridor = catalog_.model(67000)) {
        armModel(corridor->id);
        placeAt(cx, cy);
        stopPlacing();
        const auto first = selectedSlot();
        if (!first) return fail("placing corridor 67000: " + status_);
        const auto a = *edit_->object(*first);
        duplicateSelected();
        const auto copy = selectedSlot();
        if (!copy) return fail("copying the corridor");
        const auto b = *edit_->object(*copy);
        const auto size = corridor->bounds.size();
        const bool alongX = size.x >= size.z;
        const auto length = static_cast<std::int32_t>(std::lround(alongX ? size.x : size.z));
        vertexSnap_ = true;
        for (const int side : {1, -1}) {
            const std::array<std::int32_t, 3> want{a.x - b.x + (alongX ? side * length : 0), a.y - b.y,
                                                   a.z - b.z + (alongX ? 0 : side * length)};
            beginTransform();
            const auto got = vertexSnapOffset({want[0] + 9, want[1] + 5, want[2] - 7});   // near, not on
            endTransform(false);
            if (got != want) {
                vertexSnap_ = false;
                return fail(std::format("vertex snap put the copy at offset ({}, {}, {}), not ({}, {}, {})", got[0], got[1],
                                        got[2], want[0], want[1], want[2]));
            }
        }
        vertexSnap_ = false;
        beginTransform();
        const auto off = vertexSnapOffset({length + 9, 0, 0});
        endTransform(false);
        if (off != std::array<std::int32_t, 3>{length + 9, 0, 0}) return fail("vertex snap moved the copy while off");
        edit_->undo();   // the copy
        edit_->undo();   // the corridor
        editChanged();
        setSelection({*pieceRef});
        log += std::format("vertex snap put a copy of corridor 67000 end to end with it, both ends ({} long)\n", length);
    }

    // Several at once: the last three placed, moved and turned together, copied and deleted, then all undone.
    setSelection({placedRefs.end() - 3, placedRefs.end()});
    auto positions = [&] {
        std::vector<std::array<std::int32_t, 3>> out;
        for (const auto& s : selectedSlots()) {
            const auto* o = edit_->object(s);
            out.push_back({o->x, o->y, o->z});
        }
        return out;
    };
    const auto before3 = positions();
    if (before3.size() != 3) return fail("three selected, " + std::to_string(before3.size()) + " in the block");
    nudge(32, 0, 0);
    for (std::size_t i = 0; i < 3; ++i) {
        if (positions()[i][0] != before3[i][0] + 32) return fail("a nudge did not move all three");
    }
    auto middle = [](const std::vector<std::array<std::int32_t, 3>>& p) {
        double x = 0, z = 0;
        for (const auto& q : p) {
            x += q[0];
            z += q[2];
        }
        return std::pair(x / double(p.size()), z / double(p.size()));
    };
    const auto [mx, mz] = middle(positions());
    turnSelected(512);
    const auto [tx, tz] = middle(positions());
    if (std::abs(tx - mx) > 2 || std::abs(tz - mz) > 2) return fail("turning moved the middle of the selection");
    const auto objects = edit_->block().objectCount();
    duplicateSelected();
    if (edit_->block().objectCount() != objects + 3 || selection_.size() != 3) return fail("duplicating three");
    deleteSelected();
    if (edit_->block().objectCount() != objects || !selection_.empty()) return fail("deleting three");
    for (int i = 0; i < 4; ++i) {
        if (!edit_->undo()) return fail("undoing the group edits");
        editChanged();
    }
    setSelection({placedRefs.end() - 3, placedRefs.end()});
    if (positions() != before3) return fail("four undos did not put the three back");
    log += "three at once: nudged, turned about their middle, copied, deleted, undone\n";

    // A box over the whole 3D view, then hiding what it caught and showing it again.
    boxSelect(0, views_[0].min, {views_[0].min.x + views_[0].size.x, views_[0].min.y + views_[0].size.y}, false, false);
    const auto boxed = selection_.size();
    if (boxed == 0) return fail("a box over the whole view selected nothing");
    hideSelected();
    if (hidden_.size() != boxed) return fail("hiding the box");
    std::size_t drawn = 0;
    updateFlags();
    for (auto f : flags_.models) drawn += (f & render::InstanceFlags::kHidden) ? 0 : 1;
    showAll();
    log += std::format("box selected {}, hid them ({} models still drawn), showed all\n", boxed, drawn);
    // Logic: the prop becomes a lever that, used, sets off the piece, which slides up 128 units over 2 s.
    {
        auto lever = *edit_->object({propRef->root, propRef->object});
        logic::setAction(lever, logic::action::kActivate);
        logic::setTrigger(lever, logic::trigger::kUse);
        commitObject(*propRef, lever);
        auto door = *edit_->object({pieceRef->root, pieceRef->object});
        logic::setMove(door, {logic::Direction::Up, 128, 40});
        commitObject(*pieceRef, door);
        linkFrom_ = *propRef;
        finishLink(scene::Hit{scene::Hit::Kind::Model, *scene::modelIndexOf(scene_, *pieceRef), 0, {}});
        const auto& links = sceneLinks();
        const auto link = std::ranges::find_if(links, [&](const SceneLink& l) { return l.from == *propRef; });
        if (link == links.end() || link->to != *pieceRef || link->kind != logic::LinkKind::Chain) {
            return fail("linking the lever to the piece");
        }
        // The preview poses the piece half way up after 1 s, and puts it back after the frame.
        const auto pieceModel = *scene::modelIndexOf(scene_, *pieceRef);
        const auto restY = scene_.models[pieceModel].transform.translation.y;
        startPreview(*propRef);
        if (!preview_) return fail("no preview: " + status_);
        poseForPreview(1.0);
        const auto posedY = scene_.models[pieceModel].transform.translation.y;
        endPreviewPose();
        preview_.reset();
        if (std::abs(posedY - restY - 64) > 0.5) return fail(std::format("the preview put the piece at +{}", posedY - restY));
        if (scene_.models[pieceModel].transform.translation.y != restY) return fail("the preview pose stayed");
        takeOutOfChain(*pieceRef);
        if (logic::nodeOf(edit_->block(), *edit_->object({propRef->root, propRef->object}))->next != -1) {
            return fail("taking the piece out of the chain left the lever linked");
        }
        for (int i = 0; i < 4; ++i) {
            if (!edit_->undo()) return fail("undoing the logic edits");
        }
        editChanged();
        if (logic::nodeOf(edit_->block(), *edit_->object({propRef->root, propRef->object}))) {
            return fail("four undos left the lever an action");
        }
        for (int i = 0; i < 3; ++i) edit_->redo();   // back to the linked lever, which the save below keeps
        editChanged();
        if (std::ranges::none_of(sceneLinks(), [&](const SceneLink& l) { return l.from == *propRef && l.to == *pieceRef; })) {
            return fail("redo did not bring the link back");
        }
        log += "logic: lever (use) -> piece (moves up 128 over 2 s), linked by a click, previewed, taken out, undone\n";
    }
    // Doors: a lever that opens one (placed as one undo step), a magic lock, and the lock undone.
    {
        std::optional<scene::ObjectRef> doorRef;
        const auto& eb = edit_->block();
        for (std::size_t r = 0; r < eb.roots.size() && !doorRef; ++r) {
            for (std::size_t o = 0; o < eb.roots[r].objects.size(); ++o) {
                const auto* dm = eb.roots[r].objects[o].model();
                if (dm && logic::isActionDoor(eb, *dm)) {
                    doorRef = scene::ObjectRef{editBlock_, r, o};
                    break;
                }
            }
        }
        if (!doorRef) return fail("no door in the edited block");
        const auto steps = edit_->block().objectCount();
        armModel(61028);
        setup_ = PendingSetup{Setup::LeverOpensDoor, *doorRef};
        placeAt(cx, cy);
        if (setup_ || placingAnything()) return fail("the lever setup is still waiting: " + status_);
        if (edit_->block().objectCount() != steps + 1) return fail("the lever was not placed: " + status_);
        const auto lever = *primary();
        const auto leverNode = logic::nodeOf(edit_->block(), *edit_->object({lever.root, lever.object}));
        const auto& door = *edit_->object({doorRef->root, doorRef->object});
        const auto doorNode = logic::nodeOf(edit_->block(), door);
        if (!leverNode || leverNode->trigger != logic::trigger::kUse || leverNode->action != logic::action::kTurn ||
            leverNode->next != door.position) {
            return fail("the lever does not turn when used and set the door off");
        }
        if (!doorNode || doorNode->action != logic::action::kOpenDoor) return fail("the door was not made to open");
        const auto doorLabel = logic::objectLabel(edit_->block(), door);
        setDoorLock(*doorRef, 11);
        const auto lock = logic::startingLock(edit_->block(), *edit_->object({doorRef->root, doorRef->object}));
        if (!lock || lock->value != 25 || !lock->magic()) return fail("the door's lock");
        edit_->undo();   // the lock
        edit_->undo();   // the lever and the door's opening, together
        editChanged();
        if (edit_->block().objectCount() != steps) return fail("one undo did not take the lever setup away");
        edit_->redo();
        edit_->redo();
        editChanged();
        log += std::format("doors: a lever opens {} (one undo step), magic lock 25, undone and redone\n", doorLabel);
    }
    setSelection({*pieceRef});   // left selected, for a screenshot of its texture variants and logic
    focusInspectorFrames_ = 8;
    showProperties_ = true;

    // Save, read back through the same path the viewer opens dungeons with, and compare.
    saveEdit();
    if (edit_->dirty()) return fail("not saved: " + status_);
    auto reloaded = blockSource()(edit_->name());
    if (!reloaded) return fail("reload failed: " + reloaded.error().message);
    if (reloaded->objectCount() != edit_->block().objectCount()) return fail("the saved patch reloads differently");
    if (logic::linksOf(*reloaded).size() != logic::linksOf(edit_->block()).size()) {
        return fail("the saved patch reloads with different links");
    }
    log += "saved " + patches_->fileFor(edit_->name()).string() + " and reloaded it\n";
    return log;
}

// ---- panels ----

bool Viewer::enemyPicker(int& type) {
    ImGui::SetNextItemWidth(260);
    if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
    ImGui::InputTextWithHint("##filter", "filter (orc, mage, ...)", enemyFilter_, sizeof enemyFilter_);
    std::string want = enemyFilter_;
    for (auto& c : want) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    bool picked = false;
    if (ImGui::BeginChild("enemies", {260, 300}, ImGuiChildFlags_Borders)) {
        bool classes = false;
        ImGui::SeparatorText("Monsters");
        for (const auto& e : edit::enemyTypes()) {
            std::string name(e.name);
            for (auto& c : name) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (!want.empty() && name.find(want) == std::string::npos) continue;
            if (e.id >= 128 && !classes) {
                ImGui::SeparatorText("Classes");
                classes = true;
            }
            if (ImGui::Selectable(std::string(e.name).c_str(), e.id == type)) {
                type = e.id;
                picked = true;
            }
            if (e.id == type && ImGui::IsWindowAppearing()) ImGui::SetScrollHereY();
        }
    }
    ImGui::EndChild();
    return picked;
}

bool Viewer::sceneryPicker(int& archive, int& record) {
    ImGui::SetNextItemWidth(120);
    if (ImGui::InputInt("Archive", &archive)) {
        archive = std::clamp(archive, 0, 511);
        record = 0;
    }
    ImGui::SameLine();
    ImGui::TextDisabled("TEXTURE.%03d", archive);
    const int records = static_cast<int>(textures_.recordCount(archive));
    if (records == 0) {
        ImGui::TextDisabled("No pictures in this archive.");
        return false;
    }
    bool picked = false;
    // Six pictures a row, sized from the theme's own button padding and spacing, plus room for the scrollbar.
    const float cell = 48;
    const auto& style = ImGui::GetStyle();
    const float button = cell + 2 * style.FramePadding.x;
    const float width = 6 * button + 5 * style.ItemSpacing.x + 2 * style.WindowPadding.x + style.ScrollbarSize + 2;
    if (ImGui::BeginChild("records", {width, 300}, ImGuiChildFlags_Borders)) {
        const int perRow = std::max(1, static_cast<int>((ImGui::GetContentRegionAvail().x + style.ItemSpacing.x) /
                                                        (button + style.ItemSpacing.x)));
        for (int r = 0; r < records; ++r) {
            ImGui::PushID(r);
            const bool chosen = r == record;
            if (chosen) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
            if (ImGui::ImageButton("rec", textureRef(textures_.get(archive, r)), {cell, cell})) {
                record = r;
                picked = true;
            }
            if (chosen) ImGui::PopStyleColor();
            ImGui::SetItemTooltip("%d/%d", archive, r);
            if ((r + 1) % perRow != 0 && r + 1 < records) ImGui::SameLine();
            ImGui::PopID();
        }
    }
    ImGui::EndChild();
    return picked;
}

void Viewer::drawEditPanel() {
    const auto* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos({viewport->Size.x - 330, 270}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize({320, 600}, ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Edit", &showEdit_)) {
        ImGui::End();
        return;
    }
    if (!edit_) {
        ImGui::TextWrapped("Select something in a block and press \"Edit this block\" in Properties (double-click it), or right-click it.");
        if (scene_.blocks.size() == 1 &&
            (scene_.blocks[0].bsaIndex != scene::kNoBsaIndex || isProjectBlock(scene_.blocks[0].name))) {
            if (ImGui::Button(std::format("Edit {}", scene_.blocks[0].name).c_str())) beginEdit(0);
        }
        ImGui::Separator();
        ImGui::TextDisabled("Patches: %s", patches_ ? patches_->dir().string().c_str() : "(none set)");
        ImGui::End();
        return;
    }

    // What is being edited, and where it shows.
    const auto& name = edit_->name();
    const auto [classicUsers, projectUsers] = usersOf(name);
    const auto users = classicUsers + projectUsers;
    if (const auto* own = project_.block(name)) {
        ImGui::Text("Editing %s  (%s)", name.c_str(), own->label.c_str());
    } else {
        ImGui::Text("Editing %s", name.c_str());
    }
    if (classicUsers > 0 && users > 1) {
        ImGui::TextColored({1.0f, 0.75f, 0.3f, 1.0f}, "Used by %td dungeons: changes show in all of them.", users);
    } else if (users > 1) {
        ImGui::TextDisabled("Used by %td of the project's dungeons.", users);
    } else if (users == 1) {
        ImGui::TextDisabled("Used by 1 dungeon.");
    }
    if (edit_->dirty()) ImGui::TextColored({1.0f, 0.6f, 0.4f, 1.0f}, "Unsaved changes");
    else if (edit_->differsFromClassic()) ImGui::TextDisabled("Saved");
    else ImGui::TextDisabled("Same as the original");

    ImGui::BeginDisabled(!edit_->dirty());
    if (ImGui::Button("Save")) saveEdit();
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!edit_->canUndo());
    if (ImGui::Button("Undo")) {
        edit_->undo();
        editChanged();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!edit_->canRedo());
    if (ImGui::Button("Redo")) {
        edit_->redo();
        editChanged();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Stop editing")) {
        if (edit_->dirty()) confirmDiscard_ = true;
        else endEdit();
    }
    if (!edit_) {   // stopped just now
        ImGui::End();
        return;
    }

    int snapIndex = snap_ <= 1 ? 0 : snap_ == 8 ? 1 : snap_ == 16 ? 2 : snap_ == 32 ? 3 : 4;
    const char* snaps[] = {"off", "8", "16", "32", "64"};
    ImGui::SetNextItemWidth(90);
    if (ImGui::Combo("Snap (units)", &snapIndex, snaps, 5)) {
        constexpr int values[] = {1, 8, 16, 32, 64};
        snap_ = values[snapIndex];
    }

    // The palette. Fixed enemies and scenery ask what kind first.
    ImGui::SeparatorText("Place");
    for (const auto& info : edit::entityKinds()) {
        ImGui::PushID(static_cast<int>(info.kind));
        if (info.markerRecord >= 0) textureImage(textures_.get(edit::kEditorFlatArchive, info.markerRecord), 24);
        else if (info.kind == edit::EntityKind::Scenery) textureImage(textures_.get(sceneryArchive_, sceneryRecord_), 24);
        else ImGui::Dummy({24, 24});
        ImGui::SameLine();
        const bool armed = placing_ == info.kind;
        const bool asks = info.kind == edit::EntityKind::FixedEnemy || info.kind == edit::EntityKind::Scenery;
        std::string label(info.name);
        if (info.kind == edit::EntityKind::FixedEnemy) label += std::format(": {}", edit::enemyName(enemyType_));
        if (info.kind == edit::EntityKind::Scenery) label += std::format(": {}/{}", sceneryArchive_, sceneryRecord_);
        auto arm = [&] {
            if (grabbing_) endGrab(false);
            stopPlacing();
            placing_ = info.kind;
            status_ = std::format("Click a floor, wall or ceiling to place {}.", info.name);
        };
        if (ImGui::Selectable(label.c_str(), armed, 0, {0, 24})) {
            if (armed) placing_.reset();
            else if (asks) ImGui::OpenPopup("kind");
            else arm();
        }
        if (asks) ImGui::SetItemTooltip("Click to choose which, then click in a view to place it.");
        if (ImGui::BeginPopup("kind")) {
            const bool picked = info.kind == edit::EntityKind::FixedEnemy ? enemyPicker(enemyType_)
                                                                          : sceneryPicker(sceneryArchive_, sceneryRecord_);
            if (picked) {
                arm();
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        ImGui::PopID();
    }
    ImGui::Spacing();
    ImGui::TextDisabled("Double-click anything in a view for its properties.\nHelp > Keys and mouse lists the rest.");
    ImGui::End();
}

void Viewer::drawConfirmations() {
    if (pendingProject_) ImGui::OpenPopup("Change project?");
    if (ImGui::BeginPopupModal("Change project?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        const auto go = [this] {
            auto [purpose, file] = std::move(*pendingProject_);
            pendingProject_.reset();
            if (auto r = switchProject(std::move(file), purpose == ProjectDialog::New); !r) status_ = r.error().message;
        };
        ImGui::Text("%s has unsaved changes.", edit_ && edit_->dirty() ? edit_->name().c_str() : project_.name.c_str());
        if (ImGui::Button("Save and change")) {
            if (edit_ && edit_->dirty()) saveEdit();
            if (projectDirty_) saveProject();
            if (hasUnsavedEdits()) pendingProject_.reset();   // the save failed: its status says why
            else go();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Change without saving")) {
            if (edit_ && edit_->dirty()) discardEdit();
            projectDirty_ = false;
            go();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            pendingProject_.reset();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    if (confirmDiscard_) ImGui::OpenPopup("Discard changes?");
    if (ImGui::BeginPopupModal("Discard changes?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Discard the unsaved changes to %s?", edit_ ? edit_->name().c_str() : "the block");
        if (ImGui::Button("Discard")) {
            discardEdit();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Keep editing")) {
            confirmDiscard_ = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    if (confirmQuit_) ImGui::OpenPopup("Quit?");
    if (ImGui::BeginPopupModal("Quit?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("%s has unsaved changes.", edit_ ? edit_->name().c_str() : "A block");
        if (ImGui::Button("Save and quit")) {
            saveEdit();
            if (!hasUnsavedEdits()) quit_ = true;
            confirmQuit_ = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Quit without saving")) {
            quit_ = true;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            confirmQuit_ = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void Viewer::drawEditInspector(edit::ObjectSlot slot) {
    const auto* current = edit_->object(slot);
    if (!current) return;
    auto obj = *current;
    const auto kind = edit::kindOf(obj);
    bool changed = false;

    if (const auto* m = obj.model()) {
        const auto& ref = edit_->block().modelRefs[m->modelIndex];
        ImGui::Text("Model %u \"%s\"", ref.modelIdNum, ref.description.c_str());
    } else {
        ImGui::Text("%s", std::string(edit::entityInfo(kind).name).c_str());
    }
    ImGui::TextDisabled("in %s, save id %d", edit_->name().c_str(), obj.position);

    // Position, file units. Dragging is one undo step.
    int xyz[3] = {obj.x, obj.y, obj.z};
    if (ImGui::DragInt3("Position", xyz, 1.0f)) {
        obj.x = xyz[0];
        obj.y = xyz[1];
        obj.z = xyz[2];
        changed = true;
    }
    if (ImGui::IsItemActivated()) edit_->beginGesture();
    const bool positionDone = ImGui::IsItemDeactivated();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("File units (40 = 1 m). Y points down: smaller is higher.");

    if (auto* m = std::get_if<formats::RdbModel>(&obj.resource)) {
        if (ImGui::Button("Turn left")) turnSelected(-kQuarterTurn);
        ImGui::SameLine();
        if (ImGui::Button("Turn right")) turnSelected(kQuarterTurn);
        if (ImGui::IsItemHovered() && selection_.size() > 1) ImGui::SetTooltip("Turns the whole selection about its middle");
        ImGui::SameLine();
        ImGui::TextDisabled("%.0f deg", std::fmod(m->yRotation * 360.0 / 2048.0, 360.0));
        // Tipping and rolling: quarter turns about the world's X (east-west) and Z (north-south) axes.
        if (ImGui::Button("Tip -90")) turnSelectedAbout(0, -kQuarterTurn);
        ImGui::SameLine();
        if (ImGui::Button("Tip +90")) turnSelectedAbout(0, kQuarterTurn);
        ImGui::SetItemTooltip("About the east-west (X) axis: +90 tips the top northward");
        ImGui::SameLine();
        if (ImGui::Button("Roll -90")) turnSelectedAbout(2, -kQuarterTurn);
        ImGui::SameLine();
        if (ImGui::Button("Roll +90")) turnSelectedAbout(2, kQuarterTurn);
        ImGui::SetItemTooltip("About the north-south (Z) axis: +90 rolls east toward up");
        ImGui::TextDisabled("file rotation x %d  y %d  z %d  (2048 a turn)", m->xRotation, m->yRotation, m->zRotation);
    } else if (auto* f = std::get_if<formats::RdbFlat>(&obj.resource)) {
        if (kind == edit::EntityKind::FixedEnemy) {
            int type = edit::fixedEnemyType(*f);
            if (ImGui::Button(std::format("{}##enemy", edit::enemyName(type)).c_str(), {180, 0})) ImGui::OpenPopup("enemy");
            ImGui::SameLine();
            ImGui::TextUnformatted("Enemy");
            if (ImGui::BeginPopup("enemy")) {
                if (enemyPicker(type)) {
                    edit::setFixedEnemyType(*f, type);
                    changed = true;
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }
            int gender = static_cast<int>(edit::enemyGender(*f));
            const char* genders[] = {"Any", "Female", "Male"};
            if (ImGui::Combo("Gender", &gender, genders, 3)) {
                edit::setEnemyGender(*f, static_cast<edit::Gender>(gender));
                changed = true;
            }
            if (type <= 43) ImGui::TextDisabled("(gender is read for classes only)");
        }
        if (kind == edit::EntityKind::RandomEnemy) {
            int slotValue = edit::randomEnemySlot(*f);
            if (ImGui::InputInt("Slot", &slotValue)) {
                edit::setRandomEnemySlot(*f, slotValue);
                changed = true;
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Which of the dungeon's pre-rolled enemies stands here. 0 rolls one of 1-6.");
            }
        }
        if (kind == edit::EntityKind::FixedEnemy || kind == edit::EntityKind::RandomEnemy) {
            bool passive = edit::isPassive(*f);
            if (ImGui::Checkbox("Passive", &passive)) {
                edit::setPassive(*f, passive);
                changed = true;
            }
            ImGui::TextDisabled("Bunnyfall may swap an enemy too strong for the party.");
        }
        if (kind == edit::EntityKind::Scenery) {
            int archive = f->textureArchive(), record = f->textureRecord();
            if (ImGui::ImageButton("picture", textureRef(textures_.get(archive, record)), {64, 64})) {
                pickArchive_ = archive;   // the popup browses from here; the flat changes only when a picture is clicked
                pickRecord_ = record;
                ImGui::OpenPopup("scenery");
            }
            ImGui::SetItemTooltip("Click to choose another picture");
            ImGui::SameLine();
            ImGui::Text("Scenery %d/%d", archive, record);
            if (ImGui::BeginPopup("scenery")) {
                if (sceneryPicker(pickArchive_, pickRecord_)) {
                    edit::setFlatTexture(*f, pickArchive_, pickRecord_);
                    changed = true;
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }
        }
    } else if (auto* l = std::get_if<formats::RdbLight>(&obj.resource)) {
        int radius = l->radius;
        if (ImGui::DragInt("Radius", &radius, 1.0f, 1, 1000)) {
            l->radius = static_cast<std::uint16_t>(std::clamp(radius, 1, 65535));
            changed = true;
        }
        if (ImGui::IsItemActivated()) edit_->beginGesture();
        if (ImGui::IsItemDeactivated()) edit_->endGesture();
    }

    if (changed) {
        edit_->replace(slot, std::move(obj));
        editChanged();
    }
    if (positionDone) edit_->endGesture();

    ImGui::Separator();
    if (ImGui::Button("Move (G)")) startGrab();
    ImGui::SameLine();
    if (ImGui::Button("Duplicate")) duplicateSelected();
    ImGui::SameLine();
    if (ImGui::Button("Delete")) deleteSelected();
    drawTextureVariants();
}

} // namespace de::app
