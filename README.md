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

### Editing a block

Select anything in a block and press **Edit this block** in the Inspector (or open a block from the Blocks tab and
press **Edit** in the Edit panel). Then:

- **Place:** pick an entry in the Edit panel's palette (enemies, treasure, quest markers, enter/start markers, scenery,
  lights) and click a floor, wall or ceiling. It stays armed for the next click; right-click or Esc stops.
- **Select** with a left-click; the Inspector edits what it is (position, enemy type, gender, passive, slot, scenery
  picture, light radius; models turn in 90-degree steps).
- **G** moves the selection with the mouse (left-click puts it down, right-click or Esc cancels). Arrows nudge it
  (Shift: 64 units), PageUp/PageDown move it up and down. **Del** deletes, **Ctrl+D** duplicates,
  **Ctrl+Z / Ctrl+Y** undo and redo, **Ctrl+S** saves.

Saving writes the block as a patch into the patch folder: `--patches DIR`, by default
`../Bunnyfall/vendor/bunnyfall-dungeons/WorldDataPatches`. Blocks with a saved patch open patched, so editing carries
on where it stopped; a block edited back to the original has its patch removed. A classic block is shared: the Edit
panel says how many dungeons show the change.

### Checks without a screen

`--screenshot out.png [--size WxH] [--top] [--camera x,y,z,yaw,pitch] [--ui]` draws one frame offscreen (with the
panels, with `--ui`) and exits; `--frames N` runs the whole app hidden for N frames; `--check-edit DIR` runs the edit
tools as clicks would (places one of everything ahead, moves, undoes, saves to DIR and reloads), and combines with
`--screenshot`.

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
