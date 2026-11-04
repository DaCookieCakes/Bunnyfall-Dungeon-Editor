// Bunnyfall Dungeon Editor - currently the viewer (milestone 2).
//
//   dungeon_editor [--arena2 DIR] [--dungeon NAME|ID | --block NAME]
//                  [--screenshot OUT.png [--size WxH] [--top] [--camera x,y,z,yawDeg,pitchDeg]]
//                  [--frames N] [--mod DIR] [--patches DIR] [--project FILE]
//
// ARENA2 comes from --arena2, else DE_ARENA2, else ../DFData/ARENA2. With --screenshot the view is drawn once,
// offscreen, saved and the program exits. --frames N runs the normal loop, panels and all, in a hidden window for N
// frames and exits: a smoke test of the whole application without showing anything.
// --mod is the mod folder the project exports to (default: ../Bunnyfall/vendor/bunnyfall-dungeons, when
// ../Bunnyfall/vendor exists); edited classic blocks are saved in its WorldDataPatches (or --patches). --project is the
// project file (default: projects/bunnyfall-dungeons.project.json).

#include "app/viewer.h"
#include "core/png.h"

#include <SDL3/SDL.h>
#include <epoxy/gl.h>
#include <imgui.h>
#include <imgui_impl_opengl3.h>
#include <imgui_impl_sdl3.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <numbers>
#include <print>
#include <string>
#include <vector>

namespace {

struct Options {
    std::string arena2;
    std::string dungeon, block;
    std::string screenshot;
    int width = 1600, height = 900;
    bool top = false;
    std::vector<double> camera;
    int frames = 0;   // > 0: run this many frames hidden, then exit
    std::string patches;
    std::string checkEdit;      // a folder: run the scripted edit check, saving its patch there
    std::string checkProject;   // a folder: run the scripted project check, exporting there
    std::string mod;
    std::string project = "projects/bunnyfall-dungeons.project.json";
    bool ui = false;            // draw the panels into the screenshot too
    bool grid = false;          // show the sizing grid
};

bool parse(int argc, char** argv, Options& o) {
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&]() -> const char* { return i + 1 < argc ? argv[++i] : nullptr; };
        const char* v = nullptr;
        if (a == "--arena2" && (v = next())) o.arena2 = v;
        else if (a == "--dungeon" && (v = next())) o.dungeon = v;
        else if (a == "--block" && (v = next())) o.block = v;
        else if (a == "--screenshot" && (v = next())) o.screenshot = v;
        else if (a == "--size" && (v = next())) std::sscanf(v, "%dx%d", &o.width, &o.height);
        else if (a == "--top") o.top = true;
        else if (a == "--ui") o.ui = true;
        else if (a == "--grid") o.grid = true;
        else if (a == "--frames" && (v = next())) o.frames = std::atoi(v);
        else if (a == "--patches" && (v = next())) o.patches = v;
        else if (a == "--check-edit" && (v = next())) o.checkEdit = v;
        else if (a == "--check-project" && (v = next())) o.checkProject = v;
        else if (a == "--mod" && (v = next())) o.mod = v;
        else if (a == "--project" && (v = next())) o.project = v;
        else if (a == "--camera" && (v = next())) {
            double x, y, z, yaw, pitch;
            if (std::sscanf(v, "%lf,%lf,%lf,%lf,%lf", &x, &y, &z, &yaw, &pitch) != 5) return false;
            o.camera = {x, y, z, yaw, pitch};
        } else return false;
    }
    if (o.arena2.empty()) {
        const char* env = std::getenv("DE_ARENA2");
        o.arena2 = env ? env : "../DFData/ARENA2";
    }
    return true;
}

int fatal(const std::string& message) {
    std::println(stderr, "error: {}", message);
    return 1;
}

/// Draws the 3D view once into an offscreen framebuffer and writes it out.
int screenshot(de::app::Viewer& viewer, const Options& o) {
    GLuint fbo = 0, colour = 0, depth = 0;
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glGenRenderbuffers(1, &colour);
    glBindRenderbuffer(GL_RENDERBUFFER, colour);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, o.width, o.height);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, colour);
    glGenRenderbuffers(1, &depth);
    glBindRenderbuffer(GL_RENDERBUFFER, depth);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, o.width, o.height);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depth);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) return fatal("offscreen framebuffer incomplete");

    viewer.setViewSize(o.width, o.height);
    if (o.ui) {
        // Two frames: the first lays the windows out, the second is the one drawn.
        for (int i = 0; i < 2; ++i) {
            ImGui_ImplOpenGL3_NewFrame();
            ImGui_ImplSDL3_NewFrame();
            ImGui::NewFrame();
            viewer.drawUi();
            ImGui::Render();
        }
    }
    viewer.drawScene(o.width, o.height);
    if (o.ui) ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(o.width) * static_cast<std::size_t>(o.height) * 4);
    glReadPixels(0, 0, o.width, o.height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    // OpenGL reads bottom row first; PNG wants the top first.
    std::vector<std::uint8_t> flipped(pixels.size());
    const std::size_t row = static_cast<std::size_t>(o.width) * 4;
    for (std::size_t y = 0; y < static_cast<std::size_t>(o.height); ++y) {
        std::copy_n(pixels.begin() + static_cast<std::ptrdiff_t>((static_cast<std::size_t>(o.height) - 1 - y) * row),
                    row, flipped.begin() + static_cast<std::ptrdiff_t>(y * row));
    }
    glDeleteRenderbuffers(1, &colour);
    glDeleteRenderbuffers(1, &depth);
    glDeleteFramebuffers(1, &fbo);
    for (const auto& problem : viewer.problems()) std::println(stderr, "problem: {}", problem);
    if (auto r = de::writePng(o.screenshot, o.width, o.height, flipped); !r) return fatal(r.error().message);
    std::println("wrote {}", o.screenshot);
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    Options o;
    if (!parse(argc, argv, o)) {
            std::println(stderr, "usage: dungeon_editor [--arena2 DIR] [--dungeon NAME|ID | --block NAME] "
                             "[--screenshot OUT.png [--size WxH] [--top] [--camera x,y,z,yawDeg,pitchDeg]] "
                             "[--frames N] [--mod DIR] [--patches DIR] [--project FILE] [--check-edit DIR] "
                             "[--check-project DIR] [--ui] [--grid]");
        return 2;
    }
    auto arena2 = de::formats::Arena2::open(o.arena2);
    if (!arena2) return fatal(arena2.error().message + " (set --arena2 or DE_ARENA2)");

    if (!SDL_Init(SDL_INIT_VIDEO)) return fatal(std::string("SDL: ") + SDL_GetError());
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    const bool offscreen = !o.screenshot.empty() || o.frames > 0 || !o.checkEdit.empty() || !o.checkProject.empty();
    SDL_WindowFlags flags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
    if (offscreen) flags |= SDL_WINDOW_HIDDEN;
    SDL_Window* window = SDL_CreateWindow("Bunnyfall Dungeon Editor", o.width, o.height, flags);
    if (!window) return fatal(std::string("window: ") + SDL_GetError());
    SDL_GLContext gl = SDL_GL_CreateContext(window);
    if (!gl) return fatal(std::string("OpenGL 3.3 context: ") + SDL_GetError());
    SDL_GL_MakeCurrent(window, gl);
    SDL_GL_SetSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    ImGui::StyleColorsDark();
    ImGui_ImplSDL3_InitForOpenGL(window, gl);
    ImGui_ImplOpenGL3_Init("#version 330 core");

    int status = 0;
    {
        auto viewer = de::app::Viewer::create(*arena2);
        if (!viewer) return fatal(viewer.error().message);
        auto& v = **viewer;
        std::string mod = o.mod;
        if (mod.empty() && std::filesystem::is_directory("../Bunnyfall/vendor")) mod = "../Bunnyfall/vendor/bunnyfall-dungeons";
        if (!mod.empty()) v.setModFolder(std::filesystem::absolute(mod).lexically_normal());
        if (!o.patches.empty()) v.setPatchFolder(std::filesystem::absolute(o.patches).lexically_normal());
        if (auto r = v.setProjectFile(std::filesystem::absolute(o.project).lexically_normal()); !r) {
            return fatal("project: " + r.error().message);
        }
        if (!offscreen) {
            std::println("mod folder: {}", mod.empty() ? "(none)" : std::filesystem::absolute(mod).lexically_normal().string());
            std::println("project: {}", std::filesystem::absolute(o.project).lexically_normal().string());
        }

        de::Result<void> opened{};
        if (!o.block.empty()) {
            opened = v.openBlock(o.block);
        } else {
            const auto index = v.findDungeon(o.dungeon.empty() ? "Daggerfall" : o.dungeon);
            if (!index) return fatal("no dungeon matches '" + o.dungeon + "'");
            opened = v.openDungeon(*index);
        }
        if (!opened) return fatal(opened.error().message);
        if (o.top) v.cameraToTop();
        if (o.camera.size() == 5) {
            auto& c = v.camera();
            c.position = {o.camera[0], o.camera[1], o.camera[2]};
            c.yaw = o.camera[3] * std::numbers::pi / 180;
            c.pitch = o.camera[4] * std::numbers::pi / 180;
        }

        if (o.grid) v.showGrid();
        if (!o.checkProject.empty()) {
            v.setViewSize(o.width, o.height);
            auto log = v.checkProject(o.checkProject);
            if (!log) return fatal("project check: " + log.error().message);
            std::print("{}", *log);
        }
        if (!o.checkEdit.empty()) {
            v.setViewSize(o.width, o.height);
            auto log = v.checkEdit(o.checkEdit);
            if (!log) return fatal("edit check: " + log.error().message);
            std::print("{}", *log);
        }
        if (!o.screenshot.empty()) {
            status = screenshot(v, o);
        } else if (o.checkEdit.empty() && o.checkProject.empty()) {
            int frame = 0;
            Uint64 last = SDL_GetTicksNS();
            while (!v.wantsQuit() && (o.frames <= 0 || frame++ < o.frames)) {
                SDL_Event event;
                while (SDL_PollEvent(&event)) {
                    ImGui_ImplSDL3_ProcessEvent(&event);
                    // Closing asks first when there are unsaved edits.
                    if (event.type == SDL_EVENT_QUIT) v.requestQuit();
                    if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && event.window.windowID == SDL_GetWindowID(window)) {
                        v.requestQuit();
                    }
                    v.handleEvent(event, window);
                }
                const Uint64 now = SDL_GetTicksNS();
                v.update(double(now - last) / 1e9);
                last = now;

                ImGui_ImplOpenGL3_NewFrame();
                ImGui_ImplSDL3_NewFrame();
                ImGui::NewFrame();
                v.drawUi();
                ImGui::Render();

                int pw = 0, ph = 0, lw = 0, lh = 0;
                SDL_GetWindowSizeInPixels(window, &pw, &ph);
                SDL_GetWindowSize(window, &lw, &lh);
                v.setViewSize(lw, lh);
                v.drawScene(pw, ph);
                ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
                SDL_GL_SwapWindow(window);
            }
        }
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    SDL_GL_DestroyContext(gl);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return status;
}
