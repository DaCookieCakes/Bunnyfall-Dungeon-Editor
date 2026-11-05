#!/usr/bin/env bash
# A Linux build that runs on other people's machines: built on an older base (CI uses Ubuntu 24.04, glibc 2.39), with
# SDL3 and libepoxy beside the programs in lib/ (found through an $ORIGIN rpath) and the C++ runtime linked in.
# OpenGL, X11/Wayland and glibc are the system's, as they must be.
#
#   tools/package-linux.sh [version]      -> $WORK/dist/BunnyfallDungeonEditor-<version>-linux-x64.tar.gz
#
# WORK (default build-linux/ in the repo) holds SDL's source and both builds. Needs a C++23 compiler (CC/CXX), CMake,
# Ninja, pkg-config, libepoxy's headers and SDL3's build dependencies (the workflow lists them).

set -euo pipefail

version="${1:-dev}"
root="$(cd "$(dirname "$0")/.." && pwd)"
work="${WORK:-$root/build-linux}"
sdl_tag="release-3.4.18"   # the SDL the editor is developed against
mkdir -p "$work"

# SDL3, shared, from source: Ubuntu 24.04 has no SDL3 package.
if [[ ! -e "$work/sdl3/lib/libSDL3.so" ]]; then
  [[ -d "$work/sdl-src" ]] || git clone --quiet --depth 1 --branch "$sdl_tag" https://github.com/libsdl-org/SDL.git "$work/sdl-src"
  cmake -S "$work/sdl-src" -B "$work/sdl-build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$work/sdl3" -DCMAKE_INSTALL_LIBDIR=lib \
    -DSDL_SHARED=ON -DSDL_STATIC=OFF -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF -DSDL_INSTALL_TESTS=OFF
  cmake --build "$work/sdl-build"
  cmake --install "$work/sdl-build"
fi

# The editor, finding its libraries in lib/ beside it.
cmake -S "$root" -B "$work/editor" -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="$work/sdl3" \
  -DCMAKE_BUILD_WITH_INSTALL_RPATH=ON -DCMAKE_INSTALL_RPATH='$ORIGIN/lib' \
  -DCMAKE_EXE_LINKER_FLAGS="-static-libstdc++ -static-libgcc"
cmake --build "$work/editor"
# Without ARENA2 the data tests skip themselves.
ctest --test-dir "$work/editor" --output-on-failure

# The package.
name="BunnyfallDungeonEditor-${version}-linux-x64"
dist="$work/dist/$name"
rm -rf "$dist" "$work/dist/$name.tar.gz"
mkdir -p "$dist/lib" "$dist/licenses"
cp "$work/editor/dungeon_editor" "$work/editor/dftool" "$dist/"
strip "$dist/dungeon_editor" "$dist/dftool"
cp -L "$work/sdl3/lib/libSDL3.so.0" "$dist/lib/"
epoxy="$(ldd "$work/editor/dungeon_editor" | awk '/libepoxy/ {print $3}')"
[[ -n "$epoxy" ]] || { echo "libepoxy not found in ldd output" >&2; exit 1; }
cp -L "$epoxy" "$dist/lib/"
cp "$root/README.md" "$dist/"
cp -r "$root/docs" "$dist/"
cp "$work/sdl-src/LICENSE.txt" "$dist/licenses/SDL3-LICENSE.txt"
[[ -f /usr/share/doc/libepoxy0/copyright ]] && cp /usr/share/doc/libepoxy0/copyright "$dist/licenses/libepoxy-copyright"
imgui="$(find "$work/editor/_deps" -maxdepth 2 -name LICENSE.txt -path '*imgui*' | head -1)"
json="$(find "$work/editor/_deps" -maxdepth 2 -name LICENSE.MIT -path '*json*' | head -1)"
[[ -n "$imgui" ]] && cp "$imgui" "$dist/licenses/imgui-LICENSE.txt"
[[ -n "$json" ]] && cp "$json" "$dist/licenses/nlohmann-json-LICENSE.MIT"

# What it still needs from the system, for the log.
echo "needs from the system:"
ldd "$dist/dungeon_editor" | awk '{print "  " $1}' | grep -v -e 'lib/libSDL3' -e 'lib/libepoxy' || true

tar -C "$work/dist" -czf "$work/dist/$name.tar.gz" "$name"
echo "$work/dist/$name.tar.gz"
