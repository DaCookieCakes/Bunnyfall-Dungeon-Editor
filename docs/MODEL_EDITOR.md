# The model editor

**File > New model** (or **New model** in the Models panel's **Custom** tab) starts a piece of your own: a room one
cell across and two high, like corridor 67000. The views then show the model alone, with its origin (the point a
block places it by) at the cross of coloured lines. It opens in a **Model editor** tab beside **Viewports**: pick
Viewports to look at the dungeon meanwhile (each tab keeps its own camera; the model waits in its tab until you close
it with its x or Close). **Right-click** a face, a point or empty space for what you can do there: push out, cut a
door, arch or window (its outline shows as you point at it), slice here, take or lay a texture, fit, flip, join,
weld, onto the grid, add a box. With something picked, a right-click anywhere else opens the selection's menu (empty
space's items are in its **Empty space** submenu). The **Model** panel (where Edit was) holds the tools and their numbers:

- **Pick** points (**1**), edges (**2**) or faces (**3**); **Tab** goes round them. Click, Shift+click adds,
  Ctrl+click toggles, drag on empty space for a box, **Ctrl+A** all. A box takes edges and faces by their middles; in
  the 2D views a dot marks each middle, which is also how an edge-on face is picked.
- **Handles** (**R** goes round them, or the panel): **Move** arrows, **Turn** rings about X, Y and Z (15-degree
  steps), **Scale** arms with square ends along one axis, the middle square all three (the size lands on the grid).
  Shift while dragging: no snapping. The Edit section takes typed turns (about X, Y or Z) and scale factors.
- **G** picks the selection up to move with the pointer (a click puts it down, right-click or Esc cancels); **G**
  again **slides** it instead, each point along one of its own edges toward the pointer and never past that edge's
  other end: a point along an edge, a wall along the floor and ceiling edges leading away from it. The slide snaps
  to the grid along the edge (Shift: free); G once more goes back to moving freely.
- **Extrude** (**E**, or the right-click menu) in all three pick kinds: faces are copied with new sides joining them
  to where they were, edges each get a new face to a copy of the edge, points do the same for the edges between
  them (a lone point is copied on its own, to fill faces to with J). The copies follow the pointer like G; a click
  puts them down, right-click or Esc cancels (nothing is made, and a click without moving makes nothing either). In
  3D, floors and ceilings pull up and down and everything else across; Shift+E the other way round.
- **Along normals** (**Shift+N**, or the panel) makes moving and extruding follow the way the geometry faces rather than
  X, Y and Z, for anything at an angle. **E**, **G**, dragging the selection and the Move handle (now one blue arrow)
  all go along the normals. One face moves square to itself. Selected faces at an angle each move the full distance and
  stay joined, so a roof grows evenly thicker with its ridge rising to match. Edges and points use the faces they are
  on; loose ones have no normal and stay put. The **distance** snaps to the grid (Shift: free), so corners of a sloping
  face may leave the grid. If the normal points straight at a view, use another view.
- **Edges**: **Grow out** adds a face from each edge across the face beside it (more floor, more wall), **Grow
  up/down** straight up or down, each by the distance under Edit, leaving the new far edges selected to grow again.
  **J** with two edges of a face cuts it between their middles; **Split edge** puts a point in each middle;
  **Weld** collapses each edge to a point; **Del** removes an edge by joining the two faces either side (undoing a
  cut or a slice; only where they lie flat together).
- **Move**: drag a selected point or face (in 3D across the floor; hold Shift for up and down), or drag an axis arrow.
  The corner you grab lands on the **grid** (its step is in the panel; **[** and **]** make it smaller and bigger), or with **snap to points** on any point near
  the pointer. With **weld** on (the default), a corner put down on another point becomes one with it. **Show reference** draws a classic model in blue lines to build to and snap to; none of it is copied.
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
- **Wrapping a texture** (like Hammer's **Alt+right-click**): pick a face (**3**), then Alt+right-click the next one. It
  takes the selected face's texture, with the turn, offset and (on shallow slopes) scale worked out so the picture
  carries on over the edge between them, as if the first face were folded flat onto the second. The clicked face then becomes the
  selection, so you can keep clicking round a room. A floor carried up a wall is turned to keep running the way it was
  going, which can read 90 degrees on the wall; round four walls it stays level, with a seam only where the last meets
  the first (unless the room's perimeter is a whole number of pictures). With several selected, it carries on from the
  one beside the clicked face. Faces with no edge in common line up where their planes would meet. A slope within about
  45 degrees of flat is textured from above, so one that also has a turned texture skews it and can only meet exactly at
  the edge's middle; all other combinations meet along the whole edge. One Ctrl+Z undoes it.
- The kind (architecture, action door, prop) is the description blocks give it: pieces snap to the classic grid, and
  DOR makes an action door.

Every change goes into the project at once (**Ctrl+Z / Ctrl+Y** undo inside the model; save with the project).
**Close** brings the dungeon back. The model is then in the Custom tab (and in its kind's tab), placed like any
other; **Edit model** in Properties opens it again. Export writes each model to the mod's `Models/<id>.json` ([PROJECTS.md](PROJECTS.md)), which
Bunnyfall loads (`src/world/editorModels.js`). Daggerfall Unity cannot load it.
