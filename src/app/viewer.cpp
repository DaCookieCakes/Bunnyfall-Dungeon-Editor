#include "app/viewer.h"

#include "core/file.h"

#include <imgui.h>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <format>
#include <numbers>

namespace de::app {

namespace {

constexpr double kEyeHeight = 24;   // block units above a start marker's centre
constexpr int kEditorFlatArchive = 199;
constexpr int kStartMarkerRecord = 10;   // where a player walking in through the entrance arrives
constexpr int kEnterMarkerRecord = 8;    // where a new game (or any start inside the dungeon) puts the player

std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

const formats::RdbObject& objectOf(const scene::Scene& scene, const scene::ObjectRef& ref) {
    return scene.blocks[ref.block].data.roots[ref.root].objects[ref.object];
}

const char* actionName(int flag) {
    switch (flag) {
        case 0x01: return "Translation";
        case 0x02: return "+X";
        case 0x03: return "-X";
        case 0x04: return "+Y";
        case 0x05: return "-Y";
        case 0x06: return "+Z";
        case 0x07: return "-Z";
        case 0x08: return "Rotation";
        case 0x09: return "Cast spell";
        case 0x0b: return "Show text";
        case 0x0c: return "Show text with input";
        case 0x0e: return "Teleport";
        case 0x10: return "Lock door";
        case 0x11: return "Unlock door";
        case 0x12: return "Open door";
        case 0x14: return "Close door";
        case 0x15: case 0x16: case 0x17: case 0x18: case 0x19: return "Hurt";
        case 0x1a: return "Poison";
        case 0x1c: return "Drain magicka";
        case 0x1d: return "Dialogue";
        case 0x1e: return "Activate";
        case 0x1f: return "Set global variable";
        default: return "(not a defined action)";
    }
}

} // namespace

Result<std::unique_ptr<Viewer>> Viewer::create(const formats::Arena2& arena2) {
    auto catalog = catalog::Catalog::build(arena2);
    if (!catalog) return std::unexpected(catalog.error());
    auto dungeons = formats::readDungeons(arena2.maps);
    if (!dungeons) return std::unexpected(dungeons.error());
    auto palettePath = findArena2File(arena2.dir, "ART_PAL.COL");
    if (!palettePath) return std::unexpected(palettePath.error());
    auto palette = formats::Palette::load(*palettePath);
    if (!palette) return std::unexpected(palette.error());

    auto locations = formats::readLocations(arena2.maps);
    if (!locations) return std::unexpected(locations.error());

    std::unique_ptr<Viewer> v(new Viewer(arena2, std::move(*catalog), std::move(*dungeons), *palette));
    v->world_ = std::make_unique<project::World>(project::World{arena2, v->catalog_, std::move(*locations)});
    auto renderer = render::Renderer::create(v->textures_, v->models_);
    if (!renderer) return std::unexpected(renderer.error());
    v->renderer_.emplace(std::move(*renderer));
    return v;
}

Viewer::Viewer(const formats::Arena2& arena2, catalog::Catalog catalog, std::vector<formats::DungeonRecord> dungeons,
               formats::Palette palette)
    : arena2_(arena2), catalog_(std::move(catalog)), dungeons_(std::move(dungeons)), textures_(arena2.dir, palette),
      models_(arena2.arch3d) {
}

std::optional<std::size_t> Viewer::findDungeon(const std::string& key) const {
    unsigned id = 0;
    const bool numeric = std::from_chars(key.data(), key.data() + key.size(), id).ec == std::errc{};
    const auto want = lower(key);
    for (std::size_t i = 0; i < dungeons_.size(); ++i) {
        if (numeric ? dungeons_[i].locationId == id : lower(dungeons_[i].name).find(want) != std::string::npos) return i;
    }
    return std::nullopt;
}

void Viewer::setScene(scene::Scene scene) {
    endEdit();
    scene_ = std::move(scene);
    selected_.reset();
    cameraToStart();
}

scene::BlockSource Viewer::blockSource() const {
    return [this](const std::string& name) -> Result<formats::RdbBlock> {
        if (const auto* own = project_.block(name)) return own->data;   // the project's own block
        const auto bsaIndex = arena2_.blocks.findByName(name);
        if (!bsaIndex) return fail("no block " + name);
        if (patches_) {   // a classic block with a saved patch
            auto patched = patches_->load(arena2_.blocks, *bsaIndex);
            if (!patched) return std::unexpected(patched.error());
            if (*patched) return std::move(**patched);
        }
        return formats::readClassicRdbBlock(arena2_.blocks, *bsaIndex);
    };
}

Result<void> Viewer::openDungeon(std::size_t index) {
    if (index >= dungeons_.size()) return fail("no dungeon " + std::to_string(index));
    if (edit_ && edit_->dirty()) return fail("Save or discard the edits to " + edit_->name() + " first.");
    auto scene = scene::dungeonScene(dungeons_[index], arena2_.blocks, arena2_.climate, blockSource());
    if (!scene) return std::unexpected(scene.error());
    setScene(std::move(*scene));
    const auto& t = scene_.textures.archives;
    status_ = std::format("{}: {} blocks, {} models, {} flats\ndungeon type {}, climate {}, textures {} {} {} {} {} {}",
                          scene_.title, scene_.blocks.size(), scene_.models.size(), scene_.flats.size(),
                          dungeons_[index].map.dungeonType, scene_.worldClimate, t[0], t[1], t[2], t[3], t[4], t[5]);
    return {};
}

Result<void> Viewer::openBlock(const std::string& name) {
    const auto full = name.ends_with(".RDB") ? name : name + ".RDB";
    if (isProjectBlock(full)) return openProjectBlock(full);
    const auto index = arena2_.blocks.findByName(full);
    if (!index) return fail("no block " + full);
    if (edit_ && edit_->dirty()) return fail("Save or discard the edits to " + edit_->name() + " first.");
    auto block = blockSource()(full);
    if (!block) return std::unexpected(block.error());
    setScene(scene::blockScene(full, std::move(*block), *index));
    status_ = std::format("{}: {} models, {} flats", full, scene_.models.size(), scene_.flats.size());
    return {};
}

bool Viewer::cameraToMarker(int record) {
    for (const auto& flat : scene_.flats) {
        if (flat.archive == kEditorFlatArchive && flat.record == record && scene_.blocks[flat.ref.block].isStartingBlock) {
            camera_.position = {flat.position.x, flat.position.y + kEyeHeight, flat.position.z};
            camera_.yaw = 0;
            camera_.pitch = 0;
            return true;
        }
    }
    return false;
}

void Viewer::cameraToStart() {
    // As the game decides a start inside the dungeon: the enter marker, else the start marker.
    if (!cameraToMarker(kEnterMarkerRecord) && !cameraToMarker(kStartMarkerRecord)) cameraToTop();
}

void Viewer::cameraToEntrance() {
    if (!cameraToMarker(kStartMarkerRecord)) cameraToTop();
}

void Viewer::cameraToTop() {
    if (scene_.blocks.empty()) return;
    double minX = 1e300, maxX = -1e300, minZ = 1e300, maxZ = -1e300;
    for (const auto& b : scene_.blocks) {
        minX = std::min(minX, b.origin.x);
        maxX = std::max(maxX, b.origin.x + catalog::kRdbBlockSide);
        minZ = std::min(minZ, b.origin.z);
        maxZ = std::max(maxZ, b.origin.z + catalog::kRdbBlockSide);
    }
    const double span = std::max(maxX - minX, maxZ - minZ);
    const catalog::Vec3 centre{(minX + maxX) / 2, 0, (minZ + maxZ) / 2};
    camera_.position = {centre.x, span * 0.9 + 2000, centre.z - span * 0.35};
    camera_.lookAt(centre);
}

std::optional<scene::Hit> Viewer::pickAt(double x, double y, scene::PickOptions options) {
    const auto ray = camera_.rayThrough(x, y, viewWidth_, viewHeight_);
    const scene::MeshLookup meshes = [this](std::uint32_t id) -> const geometry::ModelMesh* {
        const auto* m = models_.get(id);
        return m ? &m->mesh : nullptr;
    };
    const scene::FlatSizeLookup sizes = [this](int a, int r) { return textures_.flatSize(a, r); };
    return scene::pick(scene_, ray, meshes, sizes, options);
}

scene::PickOptions Viewer::selectOptions() const {
    return {.flats = drawOptions_.flats,
            .markers = drawOptions_.markers,
            .models = true,
            .ignore = std::nullopt,
            .lightSize = drawOptions_.lights ? render::kLightGizmoSize : 0};
}

void Viewer::select(std::optional<scene::Hit> hit) {
    if (!hit) {
        selected_.reset();
        return;
    }
    switch (hit->kind) {
        case scene::Hit::Kind::Model: selected_ = scene_.models[hit->index].ref; break;
        case scene::Hit::Kind::Flat: selected_ = scene_.flats[hit->index].ref; break;
        case scene::Hit::Kind::Light: selected_ = scene_.lights[hit->index].ref; break;
    }
    selectedDistance_ = hit->distance;
}

std::optional<scene::Hit> Viewer::selectionHit() const {
    if (!selected_) return std::nullopt;
    if (const auto i = scene::modelIndexOf(scene_, *selected_)) return scene::Hit{scene::Hit::Kind::Model, *i, 0, {}};
    if (const auto i = scene::flatIndexOf(scene_, *selected_)) return scene::Hit{scene::Hit::Kind::Flat, *i, 0, {}};
    for (std::size_t i = 0; i < scene_.lights.size(); ++i) {
        if (scene_.lights[i].ref == *selected_) return scene::Hit{scene::Hit::Kind::Light, i, 0, {}};
    }
    return std::nullopt;
}

void Viewer::handleEvent(const SDL_Event& event, SDL_Window* window) {
    const auto& io = ImGui::GetIO();
    switch (event.type) {
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            if (io.WantCaptureMouse) break;
            if (event.button.button == SDL_BUTTON_RIGHT) {
                if (grabbing_) {
                    endGrab(false);   // right-click cancels a move
                    break;
                }
                if (placing_) {
                    placing_.reset();   // and stops placing
                    break;
                }
                looking_ = true;
                SDL_SetWindowRelativeMouseMode(window, true);
            } else if (event.button.button == SDL_BUTTON_LEFT) {
                if (grabbing_) endGrab(true);
                else if (placing_) placeAt(event.button.x, event.button.y);
                else select(pickAt(event.button.x, event.button.y, selectOptions()));
            }
            break;
        case SDL_EVENT_KEY_DOWN:
            if (io.WantCaptureKeyboard) break;
            handleEditKey(event.key);
            break;
        case SDL_EVENT_MOUSE_BUTTON_UP:
            if (event.button.button == SDL_BUTTON_RIGHT && looking_) {
                looking_ = false;
                SDL_SetWindowRelativeMouseMode(window, false);
            }
            break;
        case SDL_EVENT_MOUSE_MOTION:
            if (!looking_) {
                mouseX_ = event.motion.x;
                mouseY_ = event.motion.y;
                if (grabbing_) grabTo(mouseX_, mouseY_);
            }
            if (looking_) {
                constexpr double kTurn = 0.0035;   // radians per pixel
                camera_.yaw += event.motion.xrel * kTurn;
                camera_.pitch = std::clamp(camera_.pitch - event.motion.yrel * kTurn, -1.55, 1.55);
            }
            break;
        case SDL_EVENT_MOUSE_WHEEL:
            if (io.WantCaptureMouse) break;
            speed_ = std::clamp(speed_ * std::pow(1.2, event.wheel.y), 50.0, 20000.0);
            break;
        default: break;
    }
}

void Viewer::update(double seconds) {
    if (ImGui::GetIO().WantCaptureKeyboard) return;
    const bool* keys = SDL_GetKeyboardState(nullptr);
    if (keys[SDL_SCANCODE_LCTRL] || keys[SDL_SCANCODE_RCTRL]) return;   // Ctrl+letter is a shortcut, not a move
    const auto f = camera_.forward(), r = camera_.right();
    double dx = 0, dy = 0, dz = 0;
    auto add = [&](catalog::Vec3 v, double s) {
        dx += v.x * s;
        dy += v.y * s;
        dz += v.z * s;
    };
    if (keys[SDL_SCANCODE_W]) add(f, 1);
    if (keys[SDL_SCANCODE_S]) add(f, -1);
    if (keys[SDL_SCANCODE_D]) add(r, 1);
    if (keys[SDL_SCANCODE_A]) add(r, -1);
    if (keys[SDL_SCANCODE_E] || keys[SDL_SCANCODE_SPACE]) dy += 1;
    if (keys[SDL_SCANCODE_Q]) dy -= 1;
    const double boost = keys[SDL_SCANCODE_LSHIFT] ? 4 : 1;
    const double step = speed_ * boost * seconds;
    camera_.position = {camera_.position.x + dx * step, camera_.position.y + dy * step,
                        camera_.position.z + dz * step};
}

std::vector<std::string> Viewer::problems() const {
    auto all = textures_.problems();
    all.insert(all.end(), models_.problems().begin(), models_.problems().end());
    return all;
}

void Viewer::drawScene(int pixelWidth, int pixelHeight) {
    renderer_->draw(scene_, camera_, pixelWidth, pixelHeight, drawOptions_, selectionHit());
}

void Viewer::drawUi() {
    ImGui::DockSpaceOverViewport(0, nullptr, ImGuiDockNodeFlags_PassthruCentralNode);
    drawBrowser();
    drawInspector();
    drawViewPanel();
    drawEditPanel();
    drawProjectPanel();
}

void Viewer::drawBrowser() {
    ImGui::SetNextWindowPos({10, 10}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize({330, 520}, ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Browse")) {
        ImGui::End();
        return;
    }
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##filter", "filter by name", filter_, sizeof filter_);
    const auto want = lower(filter_);
    if (ImGui::BeginTabBar("browse")) {
        if (ImGui::BeginTabItem("Dungeons")) {
            std::vector<std::size_t> shown;
            for (std::size_t i = 0; i < dungeons_.size(); ++i) {
                if (want.empty() || lower(dungeons_[i].name).find(want) != std::string::npos) shown.push_back(i);
            }
            ImGui::TextDisabled("%zu of %zu", shown.size(), dungeons_.size());
            if (ImGui::BeginChild("dungeons")) {
                ImGuiListClipper clipper;
                clipper.Begin(static_cast<int>(shown.size()));
                while (clipper.Step()) {
                    for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                        const auto i = shown[static_cast<std::size_t>(row)];
                        const auto& d = dungeons_[i];
                        const auto label = std::format("{}##d{}", d.name, i);
                        if (ImGui::Selectable(label.c_str(), scene_.title == d.name)) {
                            if (auto r = openDungeon(i); !r) status_ = r.error().message;
                        }
                        if (ImGui::IsItemHovered()) {
                            ImGui::SetTooltip("region %d, location %u, %zu blocks", d.regionIndex, d.locationId,
                                              d.blocks.size());
                        }
                    }
                }
            }
            ImGui::EndChild();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Blocks")) {
            if (ImGui::BeginChild("blocks")) {
                for (const auto& b : catalog_.blocks()) {
                    if (!want.empty() && lower(b.name).find(lower(want)) == std::string::npos) continue;
                    const auto label = std::format("{}  {}", b.name, formats::roleName(b.role));
                    if (ImGui::Selectable(label.c_str(), scene_.title == b.name)) {
                        if (auto r = openBlock(b.name); !r) status_ = r.error().message;
                    }
                }
            }
            ImGui::EndChild();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::End();
}

void Viewer::drawInspector() {
    ImGui::SetNextWindowPos({10, 540}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize({330, 320}, ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Inspector")) {
        ImGui::End();
        return;
    }
    if (!selected_) {
        ImGui::TextDisabled("Left-click something in the view.");
        ImGui::End();
        return;
    }
    if (editing(*selected_)) {
        drawEditInspector(*selectedSlot());
        ImGui::End();
        return;
    }
    const auto& ref = *selected_;
    const auto& block = scene_.blocks[ref.block];
    const auto& obj = objectOf(scene_, ref);
    ImGui::Text("%s%s", block.name.c_str(), block.isStartingBlock ? "  (starting block)" : "");
    ImGui::Text("object %zu.%zu at offset %d", ref.root, ref.object, obj.position);
    ImGui::Text("file position (%d, %d, %d)", obj.x, obj.y, obj.z);
    ImGui::Text("distance %.0f units (%.1f m)", selectedDistance_, selectedDistance_ / catalog::kBlockUnitsPerMetre);
    ImGui::Separator();

    if (const auto* m = obj.model()) {
        const auto& refEntry = block.data.modelRefs[m->modelIndex];
        const auto* info = catalog_.model(refEntry.modelIdNum);
        ImGui::Text("model %u  \"%s\"  %s", refEntry.modelIdNum, refEntry.description.c_str(),
                    info ? std::string(catalog::kindName(info->kind)).c_str() : "?");
        if (info) {
            const auto s = info->bounds.size();
            ImGui::Text("size %.0f x %.0f x %.0f units", s.x, s.y, s.z);
            ImGui::Text("used %zu times in classic blocks", info->uses);
        }
        ImGui::Text("rotation (%d, %d, %d)  = y %.1f deg", m->xRotation, m->yRotation, m->zRotation,
                    -catalog::rotationDegrees(m->yRotation));
        if (m->triggerFlagStartingLock) {
            ImGui::Text("trigger/lock 0x%x  (starting lock %u)", m->triggerFlagStartingLock,
                        m->triggerFlagStartingLock >> 4);
        }
        if (m->hasAction()) {
            ImGui::Text("action: %s (flags %d)", actionName(m->action.flags), m->action.flags);
            ImGui::Text("  axis %d  duration %.2f s  magnitude %d", m->action.axis, m->action.duration / 20.0,
                        m->action.magnitude);
            if (m->action.nextObjectOffset >= 0) ImGui::Text("  next object at offset %d", m->action.nextObjectOffset);
        }
    } else if (const auto* f = obj.flat()) {
        if (f->textureArchive() == kEditorFlatArchive) {
            ImGui::Text("%s  (199/%d)", std::string(catalog::markerName(f->textureRecord())).c_str(),
                        f->textureRecord());
        } else {
            ImGui::Text("flat %d/%d", f->textureArchive(), f->textureRecord());
        }
        ImGui::Text("flags %u  magnitude %u  sound %u", f->flags, f->magnitude, f->soundIndex);
        ImGui::Text("faction/mobile id %u", f->factionOrMobileId());
        if (f->action) ImGui::Text("action byte %u, next object at offset %d", f->action, f->nextObjectOffset);
    } else if (const auto* l = obj.light()) {
        ImGui::Text("light, radius %u", l->radius);
    }
    if (block.bsaIndex != scene::kNoBsaIndex || isProjectBlock(block.name)) {
        ImGui::Separator();
        if (ImGui::Button("Edit this block")) beginEdit(ref.block);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Add, move and change things in %s", block.name.c_str());
    }
    ImGui::End();
}

std::optional<double> Viewer::floorBelow(catalog::Vec3 point) {
    const scene::MeshLookup meshes = [this](std::uint32_t id) -> const geometry::ModelMesh* {
        const auto* m = models_.get(id);
        return m ? &m->mesh : nullptr;
    };
    const scene::FlatSizeLookup sizes = [this](int a, int r) { return textures_.flatSize(a, r); };
    const auto hit = scene::pick(scene_, {point, {0, -1, 0}}, meshes, sizes,
                                 {.flats = false, .markers = false, .models = true, .ignore = std::nullopt, .lightSize = 0});
    if (!hit) return std::nullopt;
    return point.y - hit->distance;
}

void Viewer::showGrid(std::optional<double> height) {
    drawOptions_.grid = true;
    if (!height) height = floorBelow(camera_.position);
    if (height) drawOptions_.gridOptions.height = *height;
}

std::optional<double> Viewer::selectedBase() {
    const auto hit = selectionHit();
    if (!hit) return std::nullopt;
    switch (hit->kind) {
        case scene::Hit::Kind::Model: return scene_.models[hit->index].transform.translation.y;
        case scene::Hit::Kind::Flat: {
            const auto& f = scene_.flats[hit->index];
            return f.position.y - textures_.flatSize(f.archive, f.record).second / 2;
        }
        case scene::Hit::Kind::Light: return scene_.lights[hit->index].position.y - render::kLightGizmoSize / 2;
    }
    return std::nullopt;
}

void Viewer::drawViewPanel() {
    const auto* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos({viewport->Size.x - 290, 10}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize({280, 250}, ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("View")) {
        ImGui::End();
        return;
    }
    ImGui::Checkbox("Flats", &drawOptions_.flats);
    ImGui::SameLine();
    ImGui::Checkbox("Markers", &drawOptions_.markers);
    ImGui::SameLine();
    ImGui::Checkbox("Lights", &drawOptions_.lights);
    ImGui::Checkbox("Cull back faces", &drawOptions_.cullBackFaces);
    ImGui::SameLine();
    ImGui::Checkbox("Wireframe", &drawOptions_.wireframe);
    if (ImGui::Checkbox("Grid", &drawOptions_.grid) && drawOptions_.grid) {
        if (const auto y = floorBelow(camera_.position)) drawOptions_.gridOptions.height = *y;
    }
    if (drawOptions_.grid) {
        ImGui::SameLine();
        ImGui::Checkbox("through walls", &drawOptions_.gridOnTop);
        ImGui::SameLine();
        ImGui::Checkbox("sockets", &drawOptions_.gridOptions.sockets);
        int stepIndex = drawOptions_.gridOptions.cellStep == 0     ? 0
                        : drawOptions_.gridOptions.cellStep == 64  ? 1
                        : drawOptions_.gridOptions.cellStep == 128 ? 2
                                                                   : 3;
        const char* steps[] = {"blocks only", "64", "128", "256"};
        ImGui::SetNextItemWidth(110);
        if (ImGui::Combo("cells", &stepIndex, steps, 4)) {
            constexpr int values[] = {0, 64, 128, 256};
            drawOptions_.gridOptions.cellStep = values[stepIndex];
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("128 units is one room cell (3.2 m); a block is 2048 (16 cells).");
        float height = float(drawOptions_.gridOptions.height);
        ImGui::SetNextItemWidth(110);
        if (ImGui::DragFloat("height", &height, 4.0f)) drawOptions_.gridOptions.height = height;
        ImGui::SameLine();
        if (ImGui::SmallButton("floor below")) {
            if (const auto y = floorBelow(camera_.position)) drawOptions_.gridOptions.height = *y;
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(!selected_);
        if (ImGui::SmallButton("selection")) {
            if (const auto y = selectedBase()) drawOptions_.gridOptions.height = *y;
        }
        ImGui::EndDisabled();
    }
    if (ImGui::Button("Start")) cameraToStart();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Where a new game puts the player (the enter marker, else the start marker)");
    ImGui::SameLine();
    if (ImGui::Button("Entrance")) cameraToEntrance();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Where a player walking in through the door arrives (the start marker)");
    ImGui::SameLine();
    if (ImGui::Button("Top")) cameraToTop();
    ImGui::Separator();
    ImGui::TextWrapped("%s", status_.c_str());
    const auto& p = camera_.position;
    ImGui::Text("camera (%.0f, %.0f, %.0f)  yaw %.0f", p.x, p.y, p.z, camera_.yaw * 180 / std::numbers::pi);
    ImGui::Text("speed %.0f units/s (wheel)", speed_);
    ImGui::TextDisabled("Right-drag: look  WASD: move  Q/E: down/up  Shift: fast  Left-click: select");
    const auto problems = textures_.problems().size() + models_.problems().size();
    if (problems > 0 && ImGui::CollapsingHeader(std::format("{} problems", problems).c_str())) {
        for (const auto& p2 : textures_.problems()) ImGui::TextWrapped("%s", p2.c_str());
        for (const auto& p2 : models_.problems()) ImGui::TextWrapped("%s", p2.c_str());
    }
    ImGui::End();
}

} // namespace de::app
