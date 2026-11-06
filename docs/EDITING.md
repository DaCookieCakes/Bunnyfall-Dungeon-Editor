# Editing dungeons

The dungeon side of the editor: looking round, picking, and editing a block's pieces, markers and logic. For
building models of your own see [MODEL_EDITOR.md](MODEL_EDITOR.md); for your own dungeons and mods,
[PROJECTS.md](PROJECTS.md).

## The window

The window is laid out like Hammer: a menu bar and toolbar, docked panels (Browse and Models left, Outliner/View and Edit
right, Project below) and four views in the middle: **3D**, **Top**, **Front** and **Side**. Panels can be closed,
re-docked or torn off; **View > Reset layout** puts them back. **Shift+Z** (or a view's Maximise button) makes the
view under the pointer fill the area and back again.

- 3D: right-drag to look, WASD to move, Shift for speed, wheel changes speed, middle-drag slides.
- 2D views (wireframe, with a grid whose blue lines are block edges): right- or middle-drag pans, the wheel zooms about
  the pointer, **Home** fits them to the dungeon. Moving a selection (**G**) in a 2D view keeps its height (Top) or
  depth (Front/Side).
- Left-click picks a model, flat or marker in any view. **Double-click** (or **Alt+Enter**) opens its **Properties**,
  a floating window like Hammer's Object Properties that then follows the selection: a **General** tab (what it is,
  where, and its settings) and a **Triggers** tab (its logic, marked `*` when it has some). Dock it anywhere if you
  prefer. **Help > Keys and mouse** lists the rest.

## Editing a block

Select anything in a block and press **Edit this block** in Properties (or open a block from the Blocks tab and
press **Edit** in the Edit panel). Then:

- **Place:** pick an entry in the Edit panel's palette (enemies, treasure, quest markers, enter/start markers, people,
  scenery, lights) and click a floor, wall or ceiling. **Fixed enemy**, **Person** and **Scenery** first pop up a picker (an enemy list
  with a filter, a picture grid). It stays armed for the next click; right-click or Esc stops.
- **People:** **Person** in the palette places a flat from one of the game's people archives (a picture grid, as for
  scenery). The game makes any such flat someone to talk to. In Properties (General), **Talk** gives them a **Name** and
  up to 8 lines of up to 240 characters: each press of Talk says the next line, then starts over, as a person in
  Bunnyfall's POI editor does. There are no answers to choose from. No lines means they talk as Daggerfall's people do;
  no name means the game names them. This works on classic people in a block you edit too, but a quest's person speaks
  for the quest first, and a Show Text action on them runs instead (Properties warns). The words need change 7 in
  [BUNNYFALL_CHANGES.md](BUNNYFALL_CHANGES.md); Daggerfall Unity should ignore them and use its normal talk window.
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
  (architecture, doors, props); "Unused variants" also lists same-shape pieces in textures no classic block uses.
  Click one, then click in a view to place it (a tinted preview follows the pointer; **[** and **]** turn it). Empty
  space works too: the grid's level in 3D and Top, the view's middle in Front and Side, so a new block can be built
  from nothing.
- **Snapping:** **Piece snap** (toolbar) keeps architecture and doors on the grid classic dungeons use (64 units
  across, 128 up, quarter turns). **Vertex snap** (toolbar, or **V**) puts a model being placed or moved (G, or
  dragged) straight onto a model already there where their open ends match point for point: a corridor's mouth on
  the next corridor's, a cave tile's side on its neighbour's. The nearest such fit within reach of the pointer wins
  and its points are marked in yellow; with no fit, the grid snaps as usual.
- **Texture variants:** a selected model shows the same piece in its other textures in Properties (General); click
  one to swap (every selected piece of that shape swaps). With several selected, **Texture set** buttons retexture
  all of them.
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

Saving writes the block as a patch into the project's mod folder, `WorldDataPatches/` (see
[PROJECTS.md](PROJECTS.md#the-mod); `--patches DIR` puts them elsewhere). Blocks with a saved patch open patched, so editing carries
on where it stopped; a block edited back to the original has its patch removed. A classic block is shared: the Edit
panel says how many dungeons show the change.
