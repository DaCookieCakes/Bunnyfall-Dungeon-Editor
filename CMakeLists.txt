cmake_minimum_required(VERSION 3.28)
project(BunnyfallDungeonEditor VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(DE_BUILD_TESTS "Build the test suite" ON)
option(DE_BUILD_TOOLS "Build command-line tools (dftool)" ON)
option(DE_BUILD_APP "Build the editor application (needs SDL3 and libepoxy)" ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

# MinGW (the Windows build): std::print's console half lives in libstdc++exp.
if(MINGW)
  link_libraries(stdc++exp)
endif()

# Warnings for our own targets only.
add_library(de_warnings INTERFACE)
if(MSVC)
  target_compile_options(de_warnings INTERFACE /W4 /permissive-)
else()
  target_compile_options(de_warnings INTERFACE -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion)
endif()

# ---- core: no dependencies ----
add_library(de_core INTERFACE)
target_include_directories(de_core INTERFACE src)

# ---- formats: ARENA2 readers, read-only ----
add_library(de_formats STATIC
  src/formats/bsa.cpp
  src/formats/rdb.cpp
  src/formats/arch3d.cpp
  src/formats/maps.cpp
  src/formats/texture.cpp
  src/formats/climate.cpp
  src/formats/text_rsc.cpp
)
target_link_libraries(de_formats PUBLIC de_core PRIVATE de_warnings)

# ---- catalog: what can be placed, measured from ARENA2 ----
add_library(de_catalog STATIC
  src/catalog/space.cpp
  src/catalog/catalog.cpp
)
target_link_libraries(de_catalog PUBLIC de_formats PRIVATE de_warnings)

# ---- geometry: drawable meshes from ARCH3D, no GPU ----
add_library(de_geometry STATIC src/geometry/model_mesh.cpp)
target_link_libraries(de_geometry PUBLIC de_catalog PRIVATE de_warnings)

# ---- scene: placed instances of a block or a dungeon, and picking; no GPU ----
add_library(de_scene STATIC src/scene/dungeon_scene.cpp src/scene/picking.cpp src/scene/texture_table.cpp
  src/scene/grid.cpp)
target_link_libraries(de_scene PUBLIC de_geometry PRIVATE de_warnings)

# ---- export: World Data JSON and patches for Bunnyfall ----
include(FetchContent)
# nlohmann/json is a single header; fetched and pinned, its own build not run.
FetchContent_Declare(nlohmann_json
  URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
  URL_HASH SHA256=42f6e95cad6ec532fd372391373363b62a14af6d771056dbfc86160e6dfff7aa
  DOWNLOAD_EXTRACT_TIMESTAMP ON
  SOURCE_SUBDIR do-not-build)
FetchContent_MakeAvailable(nlohmann_json)
add_library(nlohmann_json_header INTERFACE)
target_include_directories(nlohmann_json_header SYSTEM INTERFACE "${nlohmann_json_SOURCE_DIR}/single_include")

# ---- mesh: the custom models the model editor builds; no GPU, no UI ----
add_library(de_mesh STATIC src/mesh/mesh.cpp src/mesh/ops.cpp src/mesh/mesh_edit.cpp src/mesh/mesh_json.cpp)
target_link_libraries(de_mesh PUBLIC de_geometry nlohmann_json_header PRIVATE de_warnings)

add_library(de_export STATIC src/export/dfu_json.cpp src/export/world_data_patch.cpp src/export/patch_folder.cpp)
target_link_libraries(de_export PUBLIC de_formats nlohmann_json_header PRIVATE de_warnings)

# ---- edit: changing a block (entities, undo, save identities); no GPU, no UI ----
add_library(de_edit STATIC src/edit/entities.cpp src/edit/block_edit.cpp src/edit/transform.cpp src/edit/vertex_snap.cpp)
target_link_libraries(de_edit PUBLIC de_catalog PRIVATE de_warnings)

# ---- logic: dungeon actions as the game reads them; no GPU, no UI ----
add_library(de_logic STATIC src/logic/action.cpp src/logic/edit.cpp)
target_link_libraries(de_logic PUBLIC de_edit de_catalog PRIVATE de_warnings)

# ---- project: the editor's own file of blocks and dungeons ----

add_library(de_project STATIC src/project/project.cpp src/project/world_export.cpp src/project/prefab.cpp)
target_link_libraries(de_project PUBLIC de_export de_edit de_catalog de_mesh PRIVATE de_warnings)

# ---- the application: window, renderer, UI ----
if(DE_BUILD_APP)
  find_package(SDL3 REQUIRED CONFIG)
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(EPOXY REQUIRED IMPORTED_TARGET epoxy)
  include(FetchContent)
  FetchContent_Declare(imgui
    URL https://github.com/ocornut/imgui/archive/refs/tags/v1.93.0-docking.tar.gz
    URL_HASH SHA256=746c265540bd3aa109570f29492ef4d8f5b421dd9fd0049c35ca35f21496f1a5
    DOWNLOAD_EXTRACT_TIMESTAMP ON
    SOURCE_SUBDIR do-not-build)
  FetchContent_MakeAvailable(imgui)
  add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp)
  target_include_directories(imgui SYSTEM PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
  target_link_libraries(imgui PUBLIC SDL3::SDL3)

  add_library(de_render STATIC
    src/render/shader.cpp
    src/render/texture_cache.cpp
    src/render/model_cache.cpp
    src/render/renderer.cpp
    src/render/render_target.cpp
    src/render/thumbnails.cpp
  )
  target_link_libraries(de_render PUBLIC de_geometry de_scene PkgConfig::EPOXY PRIVATE de_warnings)

  add_executable(dungeon_editor
    src/app/main.cpp
    src/app/viewer.cpp
    src/app/viewer_edit.cpp
    src/app/viewer_project.cpp
    src/app/viewer_views.cpp
    src/app/viewer_select.cpp
    src/app/viewer_snap.cpp
    src/app/viewer_palette.cpp
    src/app/viewer_outliner.cpp
    src/app/viewer_logic.cpp
    src/app/viewer_layout.cpp
    src/app/viewer_mesh.cpp
    src/app/theme.cpp
    src/app/config.cpp
    src/app/setup.cpp
  )
  # Windows: a windowed program, no console behind it (SDL_main.h supplies its WinMain).
  set_target_properties(dungeon_editor PROPERTIES WIN32_EXECUTABLE TRUE)
  target_link_libraries(dungeon_editor PRIVATE de_render de_catalog de_edit de_logic de_export de_project imgui SDL3::SDL3
                        nlohmann_json_header
                        de_warnings)
endif()

if(DE_BUILD_TOOLS)
  add_executable(dftool tools/dftool/main.cpp)
  target_link_libraries(dftool PRIVATE de_catalog de_export de_logic de_warnings)
endif()

if(DE_BUILD_TESTS)
  enable_testing()
  include(FetchContent)
  find_package(doctest 2.4 QUIET)
  if(NOT doctest_FOUND)
    # doctest is a single header. Its own CMakeLists predates CMake 4's minimum, so it is fetched but not built
    # (SOURCE_SUBDIR points nowhere) and the target is made here.
    FetchContent_Declare(doctest
      URL https://github.com/doctest/doctest/archive/refs/tags/v2.4.11.tar.gz
      URL_HASH SHA256=632ed2c05a7f53fa961381497bf8069093f0d6628c5f26286161fbd32a560186
      DOWNLOAD_EXTRACT_TIMESTAMP ON
      SOURCE_SUBDIR do-not-build)
    FetchContent_MakeAvailable(doctest)
    add_library(doctest_header INTERFACE)
    target_include_directories(doctest_header SYSTEM INTERFACE "${doctest_SOURCE_DIR}")
    add_library(doctest::doctest ALIAS doctest_header)
  endif()

  add_executable(de_tests
    tests/test_main.cpp
    tests/byte_view_test.cpp
    tests/bsa_test.cpp
    tests/arena2_test.cpp
    tests/space_test.cpp
    tests/catalog_test.cpp
    tests/texture_test.cpp
    tests/model_mesh_test.cpp
    tests/scene_test.cpp
    tests/picking_test.cpp
    tests/texture_table_test.cpp
    tests/camera_test.cpp
    tests/export_test.cpp
    tests/edit_test.cpp
    tests/prefab_test.cpp
    tests/transform_test.cpp
    tests/logic_test.cpp
    tests/grid_test.cpp
    tests/project_test.cpp
    tests/world_export_test.cpp
    tests/mesh_test.cpp
    tests/vertex_snap_test.cpp
  )
  target_link_libraries(de_tests PRIVATE de_catalog de_geometry de_scene de_export de_edit de_logic de_project de_mesh doctest::doctest
                        de_warnings)

  # Data tests read ARENA2 from DE_ARENA2; default to the workspace copy beside this project.
  set(DE_ARENA2 "${CMAKE_SOURCE_DIR}/../DFData/ARENA2" CACHE PATH "ARENA2 directory for data tests")
  add_test(NAME de_tests COMMAND de_tests)
  # Patch tests also read Bunnyfall's vendored world-data patches.
  set(DE_BUNNYFALL "${CMAKE_SOURCE_DIR}/../Bunnyfall" CACHE PATH "Bunnyfall checkout for tests against its vendored data")
  set_tests_properties(de_tests PROPERTIES ENVIRONMENT "DE_ARENA2=${DE_ARENA2};DE_BUNNYFALL=${DE_BUNNYFALL}")
endif()
