#include "app/setup.h"

#include <epoxy/gl.h>
#include <imgui.h>
#include <imgui_impl_opengl3.h>
#include <imgui_impl_sdl3.h>

#include <mutex>
#include <optional>
#include <string>

namespace de::app {

namespace {

/// A folder picked in SDL's dialog, handed over from whichever thread the dialog answers on.
struct PickedFolder {
    std::mutex lock;
    std::optional<std::string> path;
    int field = -1;   // which field asked
};
PickedFolder g_picked;

void SDLCALL folderPicked(void* userdata, const char* const* files, int) {
    if (!files || !files[0]) return;   // cancelled, or an error
    std::scoped_lock guard(g_picked.lock);
    g_picked.path = files[0];
    g_picked.field = static_cast<int>(reinterpret_cast<std::intptr_t>(userdata));
}

void browseFolder(SDL_Window* window, int field, const std::filesystem::path& start) {
    const std::string location = start.empty() ? "" : start.string();
    SDL_ShowOpenFolderDialog(folderPicked, reinterpret_cast<void*>(static_cast<std::intptr_t>(field)), window,
                             location.empty() ? nullptr : location.c_str(), false);
}

void browseFile(SDL_Window* window, int field, const std::filesystem::path& start) {
    static const SDL_DialogFileFilter filter{"Dungeon editor project", "json"};
    const std::string location = start.empty() ? "" : start.string();
    SDL_ShowSaveFileDialog(folderPicked, reinterpret_cast<void*>(static_cast<std::intptr_t>(field)), window, &filter, 1,
                           location.empty() ? nullptr : location.c_str());
}

bool pathField(const char* label, std::filesystem::path& path, int field, SDL_Window* window, bool isFile) {
    std::string text = path.string();
    char buffer[1024] = {};
    text.copy(buffer, sizeof buffer - 1);
    ImGui::SetNextItemWidth(-110);
    bool changed = ImGui::InputText(label, buffer, sizeof buffer);
    if (changed) path = buffer;
    ImGui::SameLine();
    ImGui::PushID(field);
    if (ImGui::Button("Browse...")) {
        if (isFile) browseFile(window, field, path.parent_path());
        else browseFolder(window, field, path);
    }
    ImGui::PopID();
    std::scoped_lock guard(g_picked.lock);
    if (g_picked.path && g_picked.field == field) {
        path = *g_picked.path;
        if (isFile && path.extension() != ".json") path += ".json";
        g_picked.path.reset();
        changed = true;
    }
    return changed;
}

void verdict(const std::optional<std::string>& problem, bool required) {
    if (!problem) ImGui::TextColored({0.45f, 0.9f, 0.5f, 1.0f}, "Found.");
    else ImGui::TextColored(required ? ImVec4{1.0f, 0.45f, 0.4f, 1.0f} : ImVec4{1.0f, 0.8f, 0.35f, 1.0f}, "%s", problem->c_str());
}

/// The fields themselves; answers true when the designer chose to go on (and the paths will do).
bool drawFields(SDL_Window* window, Config& config) {
    ImGui::TextWrapped("Daggerfall's data (the ARENA2 folder). The editor reads it and never changes it.");
    pathField("##arena2", config.arena2, 0, window, false);
    const auto arena2 = arena2Problem(config.arena2);
    verdict(arena2, true);
    ImGui::Spacing();
    ImGui::TextWrapped("Your Bunnyfall checkout. Dungeons are exported into its vendor/bunnyfall-dungeons folder.");
    pathField("##bunnyfall", config.bunnyfall, 1, window, false);
    verdict(bunnyfallProblem(config.bunnyfall), false);
    ImGui::Spacing();
    ImGui::TextWrapped("The project file: your own dungeons and blocks (made if it does not exist).");
    pathField("##project", config.project, 2, window, true);
    ImGui::Spacing();
    ImGui::BeginDisabled(arena2.has_value() || config.project.empty());
    const bool go = ImGui::Button("Save and continue", {200, 0});
    ImGui::EndDisabled();
    return go;
}

} // namespace

bool runSetup(SDL_Window* window, Config& config) {
    bool done = false, quit = false;
    while (!done && !quit) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL3_ProcessEvent(&event);
            if (event.type == SDL_EVENT_QUIT || event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) quit = true;
        }
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
        const auto* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Always, {0.5f, 0.5f});
        ImGui::SetNextWindowSize({720, 0});
        ImGui::Begin("Set up the Bunnyfall Dungeon Editor", nullptr,
                     ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
        if (drawFields(window, config)) {
            if (auto r = config.save(); !r) {
                ImGui::OpenPopup("not saved");
            } else {
                done = true;
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Quit")) quit = true;
        ImGui::TextDisabled("Saved in %s", Config::file().string().c_str());
        if (ImGui::BeginPopup("not saved")) {
            ImGui::Text("Could not save %s.", Config::file().string().c_str());
            ImGui::EndPopup();
        }
        ImGui::End();
        ImGui::Render();
        int w = 0, h = 0;
        SDL_GetWindowSizeInPixels(window, &w, &h);
        glViewport(0, 0, w, h);
        glClearColor(0.08f, 0.08f, 0.09f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(window);
    }
    return done;
}

bool drawPathsWindow(SDL_Window* window, Config& config, bool* open) {
    if (!*open) return false;
    bool saved = false;
    ImGui::SetNextWindowSize({720, 0}, ImGuiCond_Appearing);
    if (ImGui::Begin("Paths", open, ImGuiWindowFlags_NoCollapse)) {
        if (drawFields(window, config)) {
            saved = config.save().has_value();
            if (saved) *open = false;
        }
        ImGui::TextDisabled("Changes take effect the next time the editor starts.");
    }
    ImGui::End();
    return saved;
}

} // namespace de::app
