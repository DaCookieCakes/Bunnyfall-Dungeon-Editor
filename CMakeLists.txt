cmake_minimum_required(VERSION 3.28)
project(BunnyfallDungeonEditor VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(DE_BUILD_TESTS "Build the test suite" ON)
option(DE_BUILD_TOOLS "Build command-line tools (dftool)" ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
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
)
target_link_libraries(de_formats PUBLIC de_core PRIVATE de_warnings)

# ---- catalog: what can be placed, measured from ARENA2 ----
add_library(de_catalog STATIC
  src/catalog/space.cpp
  src/catalog/catalog.cpp
)
target_link_libraries(de_catalog PUBLIC de_formats PRIVATE de_warnings)

if(DE_BUILD_TOOLS)
  add_executable(dftool tools/dftool/main.cpp)
  target_link_libraries(dftool PRIVATE de_catalog de_warnings)
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
  )
  target_link_libraries(de_tests PRIVATE de_catalog doctest::doctest de_warnings)

  # Data tests read ARENA2 from DE_ARENA2; default to the workspace copy beside this project.
  set(DE_ARENA2 "${CMAKE_SOURCE_DIR}/../DFData/ARENA2" CACHE PATH "ARENA2 directory for data tests")
  add_test(NAME de_tests COMMAND de_tests)
  set_tests_properties(de_tests PROPERTIES ENVIRONMENT "DE_ARENA2=${DE_ARENA2}")
endif()
