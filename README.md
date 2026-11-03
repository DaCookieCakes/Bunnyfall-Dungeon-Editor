# Bunnyfall Dungeon Editor

A C++ snap-builder for Bunnyfall dungeons. See `docs/DESIGN.md` for what it produces and how it is put together.

## Build

Needs CMake 3.28+, a C++23 compiler (GCC 14+ or Clang 18+) and Ninja (optional). The application also needs SDL3 and
libepoxy (`pacman -S sdl3 libepoxy`); configure with `-DDE_BUILD_APP=OFF` to build only the libraries, tools and
tests. doctest and Dear ImGui are fetched on first configure, pinned by hash.

```sh
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure
```

Data tests read the ARENA2 directory from `DE_ARENA2`. CTest defaults it to `../DFData/ARENA2`; pass
`-DDE_ARENA2=/path/to/ARENA2` to configure another. Without the data, those tests are skipped.

## The editor (currently the viewer)

```sh
./build/dungeon_editor                         # opens Castle Daggerfall
./build/dungeon_editor --dungeon "Crypts of"   # first dungeon whose name contains this (or a location id)
./build/dungeon_editor --block N0000071        # one block on its own
```

Right-drag to look, WASD to move, Q/E down and up, Shift for speed, mouse wheel to change speed. Left-click picks a
model, flat or marker and shows it in the Inspector. ARENA2 comes from `--arena2`, else `DE_ARENA2`, else
`../DFData/ARENA2`.

`--screenshot out.png [--size WxH] [--top] [--camera x,y,z,yaw,pitch]` draws one frame offscreen and exits;
`--frames N` runs the whole app hidden for N frames (a smoke test).

## dftool

```sh
./build/dftool <ARENA2> stats              # counts and grid measurements over all dungeon blocks and dungeons
./build/dftool <ARENA2> block N0000071     # one block's model table and objects
./build/dftool <ARENA2> dungeon Daggerfall # a dungeon's block grid (name or locationId)
./build/dftool <ARENA2> model 67000        # a model's size and textures
```

World-data patches (what ships to Bunnyfall, never Daggerfall's own data):

```sh
./build/dftool <ARENA2> export-block N0000071 n71.json            # classic block as JSON, to edit (do not ship)
./build/dftool <ARENA2> make-patch N0000071 n71.json n71.patch.json
./build/dftool <ARENA2> rebuild-patch n71.patch.json               # checks it rebuilds to its sha256
```

`tools/dfprobe.py` is the Python probe the design numbers were first measured with.
