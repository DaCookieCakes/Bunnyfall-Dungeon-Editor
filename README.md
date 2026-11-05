# Bunnyfall Dungeon Editor

A C++ snap-builder for Bunnyfall dungeons, laid out like Hammer. Edit classic blocks or build your own from
Daggerfall's pieces, make models of your own, wire up doors, levers and traps, lay blocks out into new dungeons on the
world map, and export the lot as a Bunnyfall mod.

| Guide | What is in it |
|---|---|
| [docs/EDITING.md](docs/EDITING.md) | The window and views, picking, placing, snapping, moving, logic and right-click setups |
| [docs/MODEL_EDITOR.md](docs/MODEL_EDITOR.md) | Building models of your own: pick modes, extrude, cuts, textures |
| [docs/PROJECTS.md](docs/PROJECTS.md) | Your own blocks and dungeons, and the mod a project exports as |
| [docs/TOOLS.md](docs/TOOLS.md) | Headless checks and screenshots, and `dftool` |
| [docs/DESIGN.md](docs/DESIGN.md) | What the editor produces and how it is put together |
| [docs/BUNNYFALL_CHANGES.md](docs/BUNNYFALL_CHANGES.md) | Every change the editor needs in Bunnyfall, for the PR |

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

## Run

```sh
./build/dungeon_editor                         # opens Castle Daggerfall
./build/dungeon_editor --dungeon "Crypts of"   # first dungeon whose name contains this (or a location id)
./build/dungeon_editor --block N0000071        # one block on its own
```

On the first start a setup window asks for the ARENA2 folder, the Bunnyfall checkout and the project file, and saves
them in `~/.config/bunnyfall-dungeon-editor/config.json` (**File > Paths** changes them later). `--arena2` or
`DE_ARENA2` override the saved ARENA2, `--project FILE` the project.

**Help > Keys and mouse** lists every key. The short version: right-drag looks round in 3D (WASD moves), left-click
picks, double-click opens Properties, right-click opens a menu of what can be done there, **G** moves, **R** turns,
**Ctrl+Z** undoes.

## Where things go

- **The project** (`projects/bunnyfall-dungeons.project.json` by default) holds your own blocks, dungeons and
  models. Nothing in it touches Daggerfall's own data.
- **The mod**: the project exports into Bunnyfall's `vendor/<folder>/`, a mod of its own with its own switch in
  Bunnyfall's settings. The folder, author and id range are in the Project panel's **Mod** tab
  ([PROJECTS.md](docs/PROJECTS.md#the-mod)); `bunnyfall-dungeons` is the default. `--mod DIR` exports elsewhere.
- **Edited classic blocks** are saved as patches in the mod's `WorldDataPatches/` (`--patches DIR` for another
  folder). They ship as changes only: Bunnyfall rebuilds them from the player's own BLOCKS.BSA.
- **Prefabs** are files in `prefabs/` beside the project.
