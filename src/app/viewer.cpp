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
constexpr int kStartMarkerRecord = 10;

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

    std::unique_ptr<Viewer> v(new Viewer(arena2, std::move(*catalog), std::move(*dungeons), *palette));
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
    scene_ = std::move(scene);
    selection_.reset();
    cameraToStart();
}

Result<void> Viewer::openDungeon(std::size_t index) {
    if (index >= dungeons_.size()) return fail("no dungeon " + std::to_string(index));
    auto scene = scene::dungeonScene(dungeons_[index], arena2_.blocks, arena2_.climate);
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
    const auto index = arena2_.blocks.findByName(full);
    if (!index) return fail("no block " + full);
    auto block = formats::readClassicRdbBlock(arena2_.blocks, *index);
    if (!block) return std::unexpected(block.error());
    setScene(scene::blockScene(full, std::move(*block)));
    status_ = std::format("{}: {} models, {} flats", full, scene_.models.size(), scene_.flats.size());
    return {};
}

void Viewer::cameraToStart() {
    for (const auto& flat : scene_.flats) {
        if (flat.archive == kEditorFlatArchive && flat.record == kStartMarkerRecord &&
            scene_.blocks[flat.ref.block].isStartingBlock) {
            camera_.position = {flat.position.x, flat.position.y + kEyeHeight, flat.position.z};
            camera_.yaw = 0;
            camera_.pitch = 0;
            return;
        }
    }
    cameraToTop();
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

void Viewer::pickAt(double x, double y) {
    const auto ray = camera_.rayThrough(x, y, viewWidth_, viewHeight_);
    const scene::MeshLookup meshes = [this](std::uint32_t id) -> const geometry::ModelMesh* {
        const auto* m = models_.get(id);
        return m ? &m->mesh : nullptr;
    };
    const scene::FlatSizeLookup sizes = [this](int a, int r) { return textures_.flatSize(a, r); };
    selection_ = scene::pick(scene_, ray, meshes, sizes, {drawOptions_.flats, drawOptions_.markers});
}

void Viewer::handleEvent(const SDL_Event& event, SDL_Window* window) {
    const auto& io = ImGui::GetIO();
    switch (event.type) {
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            if (io.WantCaptureMouse) break;
            if (event.button.button == SDL_BUTTON_RIGHT) {
                looking_ = true;
                SDL_SetWindowRelativeMouseMode(window, true);
            } else if (event.button.button == SDL_BUTTON_LEFT) {
                pickAt(event.button.x, event.button.y);
            }
            break;
        case SDL_EVENT_MOUSE_BUTTON_UP:
            if (event.button.button == SDL_BUTTON_RIGHT && looking_) {
                looking_ = false;
                SDL_SetWindowRelativeMouseMode(window, false);
            }
            break;
        case SDL_EVENT_MOUSE_MOTION:
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
    if (keys[SDL_SCANCODE_Q] || keys[SDL_SCANCODE_LCTRL]) dy -= 1;
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
    renderer_->draw(scene_, camera_, pixelWidth, pixelHeight, drawOptions_, selection_);
}

void Viewer::drawUi() {
    ImGui::DockSpaceOverViewport(0, nullptr, ImGuiDockNodeFlags_PassthruCentralNode);
    drawBrowser();
    drawInspector();
    drawViewPanel();
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
    if (!selection_) {
        ImGui::TextDisabled("Left-click something in the view.");
        ImGui::End();
        return;
    }
    const auto& ref = selection_->kind == scene::Hit::Kind::Model ? scene_.models[selection_->index].ref
                                                                    : scene_.flats[selection_->index].ref;
    const auto& block = scene_.blocks[ref.block];
    const auto& obj = objectOf(scene_, ref);
    ImGui::Text("%s%s", block.name.c_str(), block.isStartingBlock ? "  (starting block)" : "");
    ImGui::Text("object %zu.%zu at offset %d", ref.root, ref.object, obj.position);
    ImGui::Text("file position (%d, %d, %d)", obj.x, obj.y, obj.z);
    ImGui::Text("distance %.0f units (%.1f m)", selection_->distance, selection_->distance / catalog::kBlockUnitsPerMetre);
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
    }
    ImGui::End();
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
    ImGui::Checkbox("Cull back faces", &drawOptions_.cullBackFaces);
    ImGui::SameLine();
    ImGui::Checkbox("Wireframe", &drawOptions_.wireframe);
    if (ImGui::Button("Start")) cameraToStart();
    ImGui::SameLine();
    if (ImGui::Button("Top")) cameraToTop();
    ImGui::Separator();
    ImGui::TextWrapped("%s", status_.c_str());
    const auto& p = camera_.position;
    ImGui::Text("camera (%.0f, %.0f, %.0f)  yaw %.0f", p.x, p.y, p.z, camera_.yaw * 180 / std::numbers::pi);
    ImGui::Text("speed %.0f units/s (wheel)", speed_);
    ImGui::TextDisabled("Right-drag: look  WASD: move  Q/E: down/up  Shift: fast");
    const auto problems = textures_.problems().size() + models_.problems().size();
    if (problems > 0 && ImGui::CollapsingHeader(std::format("{} problems", problems).c_str())) {
        for (const auto& p2 : textures_.problems()) ImGui::TextWrapped("%s", p2.c_str());
        for (const auto& p2 : models_.problems()) ImGui::TextWrapped("%s", p2.c_str());
    }
    ImGui::End();
}

} // namespace de::app
