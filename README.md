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

On the first start a setup window asks for the ARENA2 folder, the Bunnyfall checkout and the project file, and saves
them in `~/.config/bunnyfall-dungeon-editor/config.json` (**File > Paths** changes them later). `--arena2` or
`DE_ARENA2` override the saved ARENA2.

The window is laid out like Hammer: a menu bar and toolbar, docked panels (Browse left, Properties/View and Edit
right, Project below) and four views in the middle: **3D**, **Top**, **Front** and **Side**. Panels can be closed,
re-docked or torn off; **View > Reset layout** puts them back. **Shift+Z** (or a view's Maximise button) makes the
view under the pointer fill the area and back again.

- 3D: right-drag to look, WASD to move, Q/E down and up, Shift for speed, wheel changes speed, middle-drag slides.
- 2D views (wireframe, with a grid whose blue lines are block edges): right- or middle-drag pans, the wheel zooms about
  the pointer, **Home** fits them to the dungeon. Moving a selection (**G**) in a 2D view keeps its height (Top) or
  depth (Front/Side).
- Left-click picks a model, flat or marker in any view and shows it in Properties. **Help > Keys and mouse** lists
  the rest.

### Editing a block

Select anything in a block and press **Edit this block** in the Inspector (or open a block from the Blocks tab and
press **Edit** in the Edit panel). Then:

- **Place:** pick an entry in the Edit panel's palette (enemies, treasure, quest markers, enter/start markers, scenery,
  lights) and click a floor, wall or ceiling. It stays armed for the next click; right-click or Esc stops.
- **Select** with a left-click; the Inspector edits what it is (position, enemy type, gender, passive, slot, scenery
  picture, light radius; models turn in 90-degree steps).
- **Several at once:** Shift+click adds, Ctrl+click toggles, a left-drag on empty space draws a box (Shift/Ctrl
  likewise), **Ctrl+A** takes everything shown. Moving, turning, nudging, duplicating and deleting act on all of the
  edited block's selected objects, each as one undo step.
- **Handles** on the selection, in every view: drag an arrow to move along X, Y or Z, the square to move freely in the
  view's plane, or drag a selected object itself. **R** (or the toolbar) switches to the turning ring (3D and Top
  views), which turns the selection about its middle in steps of the toolbar's turn snap. Right-click or Esc cancels.
- **Hiding:** the toolbar's **Show** menu hides kinds (architecture, doors, props, scenery, enemies, treasure, quest
  markers, start/enter, other markers, lights); **H** hides the selection, **I** shows only the selection's block,
  **U** shows everything again. Hidden things cannot be picked or placed on.
- **Models** (left, under Browse): every model classic dungeons use, as pictures, most used first, by kind
  (architecture, doors, props). Click one, then click in a view to place it (a tinted preview follows the pointer;
  **[** and **]** turn it). Empty space works too: the grid's level in 3D and Top, the view's middle in Front and
  Side, so a new block can be built from nothing. **Piece snap** (toolbar) keeps architecture and doors on the grid
  classic dungeons use (64 units across, 128 up, quarter turns). "Unused variants" also lists same-shape pieces in
  textures no classic block uses.
- **Texture variants:** a selected model shows the same piece in its other textures in Properties; click one to swap
  (every selected piece of that shape swaps). With several selected, **Texture set** buttons retexture all of them.
- **Prefabs** (the Models panel's last tab): name a selection and **Save selection**, from any block, classic or
  your own. They are files in `prefabs/` beside the project; place them like models. Action links inside a prefab
  (a lever and its door) follow the copies.
- **Outliner** (beside Properties): everything in the edited (or selected) block, by kind, with a filter and a
  show/hide box per object. Click selects, double-click (or **F** in a view) frames it.
- **G** moves the selection with the mouse (left-click puts it down, right-click or Esc cancels). Arrows nudge it
  (Shift: 64 units), PageUp/PageDown move it up and down. **Del** deletes, **Ctrl+D** duplicates,
  **Ctrl+Z / Ctrl+Y** undo and redo, **Ctrl+S** saves.

Saving writes the block as a patch into the patch folder: `--patches DIR`, by default
`../Bunnyfall/vendor/bunnyfall-dungeons/WorldDataPatches`. Blocks with a saved patch open patched, so editing carries
on where it stopped; a block edited back to the original has its patch removed. A classic block is shared: the Edit
panel says how many dungeons show the change.

### Making your own dungeons

The **Project** panel holds your own dungeons and blocks, saved in `projects/bunnyfall-dungeons.project.json`
(`--project FILE` for another). Nothing there touches Daggerfall's own dungeons.

- **Blocks** tab: make a block as a copy of a classic one (`N0000071`) or from nothing, with a role (normal, wet, quest,
  mausoleum, border cap) and a label. **Open** shows it alone; **Edit this block** in the Inspector edits it as
  above, and saving it goes into the project.
- **Dungeons** tab: **New dungeon**, then its place: name, region, map pixel (**Free spot nearby** finds empty land),
  dungeon type (which enemies and loot), map icon, entrance block, and whether it shows on the travel map.
- The grid: pick a block in **Place** and click empty cells (north is up). Right-click a cell to make it the start,
  replace it, remove it, or edit it in 3D. **Auto caps** closes every open side with border caps. Each edge shows
  its two doorways (green upper, orange lower); red means the neighbour's side does not match. Problems are listed
  under the grid; errors stop an export, warnings do not.
- **View in 3D** shows the dungeon; **Export to Bunnyfall** writes it into the mod folder (`--mod DIR`, by default
  `../Bunnyfall/vendor/bunnyfall-dungeons`): blocks copied from classic ones as patches, blocks made from nothing
  whole, each dungeon as a new place. A re-export removes the files of deleted dungeons and blocks.

The sizing grid (View panel, **Grid**) shows block edges, 128-unit room cells and the doorway sockets, at a height
you set (or the floor below you, or the selection's level).

### Checks without a screen

`--screenshot out.png [--size WxH] [--top] [--camera x,y,z,yaw,pitch] [--ui]` draws one frame offscreen (with the
panels, with `--ui`) and exits; `--frames N` runs the whole app hidden for N frames; `--check-edit DIR` runs the edit
tools as clicks would (places one of everything ahead, moves, undoes, saves to DIR and reloads); `--check-project
DIR` makes a block and a dungeon, caps it, opens it and exports to DIR. Both combine with `--screenshot`, and `--grid`
shows the sizing grid.

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
