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

The window is laid out like Hammer: a menu bar and toolbar, docked panels (Browse and Models left, Outliner/View and Edit
right, Project below) and four views in the middle: **3D**, **Top**, **Front** and **Side**. Panels can be closed,
re-docked or torn off; **View > Reset layout** puts them back. **Shift+Z** (or a view's Maximise button) makes the
view under the pointer fill the area and back again.

- 3D: right-drag to look, WASD to move, Q/E down and up, Shift for speed, wheel changes speed, middle-drag slides.
- 2D views (wireframe, with a grid whose blue lines are block edges): right- or middle-drag pans, the wheel zooms about
  the pointer, **Home** fits them to the dungeon. Moving a selection (**G**) in a 2D view keeps its height (Top) or
  depth (Front/Side).
- Left-click picks a model, flat or marker in any view. **Double-click** (or **Alt+Enter**) opens its **Properties**,
  a floating window like Hammer's Object Properties that then follows the selection: a **General** tab (what it is,
  where, and its settings) and a **Triggers** tab (its logic, marked `*` when it has some). Dock it anywhere if you
  prefer. **Help > Keys and mouse** lists the rest.

### Editing a block

Select anything in a block and press **Edit this block** in Properties (or open a block from the Blocks tab and
press **Edit** in the Edit panel). Then:

- **Place:** pick an entry in the Edit panel's palette (enemies, treasure, quest markers, enter/start markers, scenery,
  lights) and click a floor, wall or ceiling. **Fixed enemy** and **Scenery** first pop up a picker (an enemy list
  with a filter, a picture grid). It stays armed for the next click; right-click or Esc stops.
- **Select** with a left-click; double-click for Properties, whose General tab edits what it is (position, enemy
  type, gender, passive, slot, scenery picture, light radius; models turn in 90-degree steps).
- **Several at once:** Shift+click adds, Ctrl+click toggles, a left-drag on empty space draws a box (Shift/Ctrl
  likewise), **Ctrl+A** takes everything shown. Moving, turning, nudging, duplicating and deleting act on all of the
  edited block's selected objects, each as one undo step.
- **Handles** on the selection, in every view: drag an arrow to move along X, Y or Z, the square to move freely in the
  view's plane, or drag a selected object itself. **R** (or the toolbar) switches to the turning
  rings, which turn the selection about its middle in steps of the toolbar's turn snap: in 3D one per axis (X red, Y
  green, Z blue), in Top, Front and Side the one facing the view. Properties also has quarter turns about each axis
  (Turn, Tip, Roll). Right-click or Esc cancels.
- **Hiding:** the toolbar's **Show** menu hides kinds (architecture, doors, props, scenery, enemies, treasure, quest
  markers, start/enter, other markers, lights); **H** hides the selection, **I** shows only the selection's block,
  **U** shows everything again. Hidden things cannot be picked or placed on.
- **Models** (left, under Browse): every model classic dungeons use, as pictures, most used first, by kind
  (architecture, doors, props). Click one, then click in a view to place it (a tinted preview follows the pointer;
  **[** and **]** turn it). Empty space works too: the grid's level in 3D and Top, the view's middle in Front and
  Side, so a new block can be built from nothing. **Piece snap** (toolbar) keeps architecture and doors on the grid
  classic dungeons use (64 units across, 128 up, quarter turns). "Unused variants" also lists same-shape pieces in
  textures no classic block uses.
- **Texture variants:** a selected model shows the same piece in its other textures in Properties (General); click one to swap
  (every selected piece of that shape swaps). With several selected, **Texture set** buttons retexture all of them.
- **Prefabs** (the Models panel's last tab): name a selection and **Save selection**, from any block, classic or
  your own. They are files in `prefabs/` beside the project; place them like models. Action links inside a prefab
  (a lever and its door) follow the copies.
- **Outliner** (right): everything in the edited (or selected) block, by kind, with a filter and a
  show/hide box per object. Click selects, double-click (or **F** in a view) frames it.
- **Logic** (Properties, Triggers tab): what a selected object does, in words, as the game runs it ("When the player uses
  it: turns 90° about Y over 0.45 s; back again the next time"), with the text a text action shows, its door lock,
  what it sets off and what sets it off (click either to go there), the whole chain, and warnings for things that
  won't work (a lock action on something that isn't a door, a link to nothing, an object nothing ever sets off).
  The views draw the links as arrows: cyan for a chain, dashed magenta for a teleport, grey where the chain stops,
  a red cross for a link to nothing. The selection's chains are bright. The toolbar's **Links** button (or **L**)
  cycles all, only the chains the selection is part of, or none.
- **Editing logic** (the Triggers tab, on an object of the edited block): pick an **Action** (move, turn, swing open,
  lock/unlock, teleport, a message or riddle, hurt, drain magicka, cast a spell, set a quest variable, or just pass
  it on), when it is **Set off** (used, stood on, walked into, hit, ...), and its values: direction, distance and
  time for a move; axis, angle and time for a turn; the message from a list of the game's own. **Link to...** then
  click the next object in any view (Esc or right-click cancels); **Unlink**; **Take out of chain** joins what set
  it off to what it sets off. **Preview** plays the chain's moves, turns and swings in the views, forward and back.
  Every change is one undo step. A chain fires all at once, as in the game.
- **Right-click** (without dragging) on anything in a view opens a menu: Properties, Frame, Select its whole chain,
  and on the edited block quick setups. On a door: **Lever that opens it...** / **Pressure plate that opens it...**
  (place the classic lever 61028 or plate 74087; it is linked, and the door opens and unlocks, as one undo step),
  **Opened by...** (click anything to make it open the door), and **Starting lock** (16 levels; 20 and up are magic:
  no picking or bashing, so the Triggers tab warns when nothing opens one). On anything else: when used / stood on
  sets off..., set off by..., a secret wall that slides up or swings open when used, a teleporter, a trap. On empty
  space: place a lever or a plate. A door's lock is also in its Triggers tab.
- **G** moves the selection with the mouse (left-click puts it down, right-click or Esc cancels). Arrows nudge it
  (Shift: 64 units), PageUp/PageDown move it up and down. **Del** deletes, **Ctrl+D** duplicates,
  **Ctrl+Z / Ctrl+Y** undo and redo, **Ctrl+S** saves.

Saving writes the block as a patch into the patch folder: `--patches DIR`, by default
`../Bunnyfall/vendor/bunnyfall-dungeons/WorldDataPatches`. Blocks with a saved patch open patched, so editing carries
on where it stopped; a block edited back to the original has its patch removed. A classic block is shared: the Edit
panel says how many dungeons show the change.

### Building your own models

**File > New model** (or **New model** in the Models panel's **Custom** tab) starts a piece of your own: a room one
cell across and two high, like corridor 67000. The views then show the model alone, with its origin (the point a
block places it by) at the cross of coloured lines. It opens in a **Model editor** tab beside **Viewports**: pick
Viewports to look at the dungeon meanwhile (each tab keeps its own camera; the model waits in its tab until you close
it with its x or Close). **Right-click** a face, a point or empty space for what you can do there: push out, cut a
door, arch or window (its outline shows as you point at it), slice here, take or lay a texture, fit, flip, join,
weld, onto the grid, add a box. The **Model** panel (where Edit was) holds the tools and their numbers:

- **Pick** points (**1**), edges (**2**) or faces (**3**); **Tab** goes round them. Click, Shift+click adds,
  Ctrl+click toggles, drag on empty space for a box, **Ctrl+A** all. A box takes edges and faces by their middles; in
  the 2D views a dot marks each middle, which is also how an edge-on face is picked.
- **Edges**: **Grow out** adds a face from each edge across the face beside it (more floor, more wall), **Grow
  up/down** straight up or down, each by the distance under Edit, leaving the new far edges selected to grow again.
  **J** with two edges of a face cuts it between their middles; **Split edge** puts a point in each middle;
  **Weld** collapses each edge to a point; **Del** removes an edge by joining the two faces either side (undoing a
  cut or a slice; only where they lie flat together).
- **Move**: drag a selected point or face (in 3D across the floor; hold Shift for up and down), or drag an axis arrow.
  The corner you grab lands on the **grid** (its step is in the panel; **[** and **]** make it smaller and bigger), or with **snap to points** on any point near
  the pointer. **Show reference** draws a classic model in blue lines to build to and snap to; none of it is copied.
  Arrows and PgUp/PgDn move by a grid step; Move, Turn and Mirror in the panel take exact values.
- **Shapes**: a box between two corners, seen from inside (a room) or outside (a solid), with any sides left off, or
  a flight of stairs across the same box.
- **Edit**: **Push out** (**X**) moves faces back behind themselves joined to where they were, so a room's wall
  becomes a corridor; **Pull in** (**Shift+X**) grows a solid. **Join** (**J**) cuts a face between two picked points,
  or fills a face between three or more. **Flip** (**N**) turns faces round (a face is seen from one side). Split edge,
  Weld, Copy (**Ctrl+D**), Delete (**Del**), Merge close points.
- **Cuts**: **Cut opening** takes a doorway, window or trapdoor out of the selected faces: its width, height, how
  far up from the face's bottom and across from its middle, drawn on them in cyan before you cut (Door, Arch and
  Window presets). The wall becomes the pieces round the hole, its texture unbroken; with a **reveal depth** the hole
  is lined that deep behind the face (sides, a sill in the floor texture, a lintel in the ceiling's), so a doorway
  through a thick wall or into a corridor behind is closed in. **Slice** cuts the selected faces in two along a grid
  line (X, Y or Z =; **here** takes the selection's middle). Both put the new corners into the neighbouring faces'
  edges, so nothing cracks.
- **Texture**: any archive in your ARENA2, from a list with a filter (number or name) and arrows to step through:
  the six dungeon sets first (each dungeon's texture table swaps them, as it does classic walls), then the ones
  classic pieces use, most used first, then the rest. Click a picture to lay it on the selected faces. **Texture alignment**: units per texel (classic: 2),
  offset, turn; **Fit** puts one copy across each face, **Classic** lines faces up on the world.
- The kind (architecture, action door, prop) is the description blocks give it: pieces snap to the classic grid, and
  DOR makes an action door.

Every change goes into the project at once (**Ctrl+Z / Ctrl+Y** undo inside the model; save with the project).
**Close** brings the dungeon back. The model is then in the Custom tab (and in its kind's tab), placed like any
other; **Edit model** in Properties opens it again. Export writes each model to the mod's `Models/<id>.json`, which
Bunnyfall loads (`src/world/editorModels.js`). Daggerfall Unity cannot load it.

### Making your own dungeons

The **Project** panel holds your own dungeons and blocks, saved in `projects/bunnyfall-dungeons.project.json`
(`--project FILE` for another). Nothing there touches Daggerfall's own dungeons.

- **Blocks** tab: make a block as a copy of a classic one (`N0000071`) or from nothing, with a role (normal, wet, quest,
  mausoleum, border cap) and a label. **Open** shows it alone; **Edit this block** in Properties edits it as
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
DIR` makes a block and a dungeon, caps it, opens it and exports to DIR; `--check-model DIR` builds a model (a wall
pushed into a corridor, dragged, retextured, undone and redone), places it in a block and exports to DIR. All three
combine with `--screenshot`, and `--grid`
shows the sizing grid. `--select OFFSET` selects (and frames) the object at that offset, for screenshots of its
Properties.

## dftool

```sh
./build/dftool <ARENA2> stats              # counts and grid measurements over all dungeon blocks and dungeons
./build/dftool <ARENA2> block N0000071     # one block's model table and objects
./build/dftool <ARENA2> dungeon Daggerfall # a dungeon's block grid (name or locationId)
./build/dftool <ARENA2> model 67000        # a model's size and textures
./build/dftool <ARENA2> snap               # the grid classic architecture sits on
./build/dftool <ARENA2> actions            # how classic blocks use triggers, actions, links and door locks
```

World-data patches (what ships to Bunnyfall, never Daggerfall's own data):

```sh
./build/dftool <ARENA2> export-block N0000071 n71.json            # classic block as JSON, to edit (do not ship)
./build/dftool <ARENA2> make-patch N0000071 n71.json n71.patch.json
./build/dftool <ARENA2> rebuild-patch n71.patch.json               # checks it rebuilds to its sha256
```

`tools/dfprobe.py` is the Python probe the design numbers were first measured with.
