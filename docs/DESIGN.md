# Bunnyfall Dungeon Editor — Design

Status: draft 1 (2026-11-02). Nothing is built yet. This document fixes what the editor must produce and how it is
structured before any code is written.

## 1. Purpose

A desktop C++ editor for building Bunnyfall dungeons by snapping pieces together, with room for custom content and
scripted sequences. Its output is content Bunnyfall already knows how to load. No engine change is needed to *play*
an exported dungeon. A few small Bunnyfall-side changes are needed to *ship* one; see §6.4.

### Goals

- Build dungeons at two scales: blocks on the dungeon grid, and architecture pieces inside a block. Both snap.
- Place and configure enemies, treasure, NPCs, lights, doors and markers with game-accurate previews.
- Author scripted behaviour (moving platforms, levers, traps, teleports, text) as a node graph, plus quest scripts.
- Export files Bunnyfall loads unchanged, and that should also load in Daggerfall Unity, since it is the same format.
- Read the player's own ARENA2. The editor never ships or exports Daggerfall's data.

### Non-goals (for v1)

- Editing town (RMB) blocks or exteriors beyond picking an entrance.
- New textures. New 3D models are in since milestone 8 (Bunnyfall only, §7.3).
- A gameplay simulator. Preview is visual; testing happens in Bunnyfall.

### Code rule

Bunnyfall's JavaScript is a reference for **formats and rules only**. Its structure, naming and file layout are not a
model for this codebase.

---

## 2. Facts the design rests on

Everything here was either read from Bunnyfall's loaders or measured from ARENA2 with `tools/dfprobe.py`. Classic
data contains 187 RDB blocks, 10,237 ARCH3D models and 4,232 dungeons.

### 2.1 Units and axes

| Thing | Value |
|---|---|
| Block / object position unit | 1 unit = 0.025 m |
| ARCH3D mesh point unit | 1/256 of a block unit, so mesh points ÷ 256 give block units |
| Rotation unit | 2048 = 360°, so 512 = 90° (`ROTATION_DIVISOR = 2048/360`) |
| Vertical axis | Daggerfall Y points **down**: negative Y is up. Flats are placed at `(X, −Y, Z)`. |
| Model matrix order | `T · Rz · Rx · Ry` (not the usual TRS order) |

### 2.2 The dungeon (location) level

- A dungeon is a list of `{X, Z, BlockName, IsStartingBlock}`. Each block is 2048 × 2048 units (51.2 m) at
  `(X·2048, 0, Z·2048)`.
- **Blocks cannot be rotated.** The location format has no rotation field.
- **The starting block is at (0,0) in all 4,232 classic dungeons**, and is never a border block. It starts with N
  (2,279 dungeons), W (1,167), M (774) or S (12).
- Size: 5 to 22 blocks (most often 8). Grid extents are at most 5 × 7.
- **Interior blocks are closed on all four sides**: in 45,891 of 45,920 cases, each side of a non-border block touches
  another block. Border blocks (prefix B) cap the open sides. There are 15 border blocks, about 6.8 per dungeon, and
  each is used on all four sides about equally, so caps are direction-agnostic.
- **Border caps carry a dead-end stub on every side**, and a dungeon uses only the side facing it. The other stubs
  are sealed and unreachable; the game never shows them, but a free camera does (Privateer's Hold: 134 central
  pieces and 37 facing-side cap pieces reachable from the start, 115 cap pieces sealed off). The editor should
  dim or hide geometry not connected to the start, and its own caps should only build the side they need.
- The first letter of a block's name gives its role, and the game reads it: `N` normal, `W` wet, `S` quest,
  `M` mausoleum, `B` border. Bunnyfall's "smaller dungeons" mode treats every `B` block as a cap and ignores the
  quest markers in it.

### 2.3 Block-to-block connection: the socket standard

Measured with the catalog's port detection (§4), which cuts each placed piece's real faces with the four edge
planes:

- **Every edge has two sockets**, the same on all four sides:
  - the **upper socket** at 640 along the edge, floor 1280 (editor Y);
  - the **lower socket** at 1408 along the edge, floor 512.
- Each socket is a doorway **50 units (1.25 m) wide**.
- **All 138 normal, wet and border blocks open all eight sockets.** This is how classic dungeons connect: any block
  fits next to any other, on any side, with no rotation.
- Quest (S) blocks open 13 of 40 completely, and mausoleum (M) blocks none. They are special-built and usually sit
  where fewer connections are needed.
- 41 blocks also have architecture crossing an edge somewhere else, mostly wide rooms and caves (122 to 512 units)
  reaching into the neighbouring block's empty space. These are **overhangs**, not connections.

Design consequence: the dungeon-level snap is "every block offers the 8 standard sockets". Validation checks that
facing sockets are both open or both absent (§8). An overhang is allowed, but warns when it collides with the
neighbour's geometry.

Model classification feeding this: architecture is the families C (corridors, junctions, stairs), R (large rooms),
O (odd-shaped rooms), three-digit descriptions (rooms named by size in 128-unit cells less one: `000` is one cell,
`012` two by three), and any other model at least 96 units in every direction. With that, port detection agrees with
counting every model in the block.

### 2.4 Inside a block

| Measure | Value |
|---|---|
| Architecture pieces (model refs described `C??`) | 12,411 of 22,961 models |
| X/Z on a multiple of 64 units (1.6 m) | 95.6% |
| X/Z on a multiple of 128 units (3.2 m) | 77.7% (the rest are almost all at +64) |
| Y on a multiple of 256 (6.4 m, one storey) | 86.9%; on 128: 95.2% |
| Y-rotation on a multiple of 512 (90°) | almost all; non-zero X/Z rotation is rare (~1%) |
| Most-used piece, 67000 (corridor) | 128 × 128 × 256 units = 3.2 × 3.2 × 6.4 m |
| Pieces past the 0–2048 bounds | yes, up to z = 2712. The block is not a hard boundary. |
| Objects per block | 21 to 391 (median 220) |
| Model references used per block | at most 102 (format limit: 750) |
| Object roots | 4×4 (182 blocks) or 8×8 (5 blocks). Roots are **not** spatial buckets (only 4% match a 512 cell). |

Design consequence: piece snapping uses a 64-unit grid with 128 as the default step, 128/256 for vertical steps, and
90° turns. Free placement stays available for props.

### 2.5 Object kinds inside an RDB block

Models (22,961), flats (12,238) and lights (4,268).

**Model**: an index into the block's model reference list (an ARCH3D id plus a 3-letter description), a rotation and
an optional action record.
- Descriptions `DOR`, `DDR`, `NEW` and `CAV` make it an action door. Its starting lock is
  `TriggerFlag_StartingLock >> 4`, indexed into a 16-entry lock table.
- Model 70300 (`EXT`) is the exit door. It only appears in the starting block.
- Doors less than 1.4 m apart are merged into one.

**Flat**: a texture archive and record. Archive 199 holds the editor markers:

| 199 record | Meaning | Classic count |
|---|---|---|
| 8 | Enter marker: where a start *inside* the dungeon puts the player (a new game, a load, a respawn, a quest teleport). Wins over the start marker for those. | 10 |
| 10 | Start marker: where a player walking in through the entrance arrives, and the fallback when there is no enter marker. Also carries the block's water level (`SoundIndex·−8`, 0 means none) and the castle flag (`Magnitude≠0`). | 167 |
| 11 | Quest spawn marker | 144 |
| 15 | Random enemy. `Flags` = encounter slot, 0 means reroll. | 2,195 |
| 16 | Fixed enemy. `FactionOrMobileId & 0xFF` = enemy type. Action 99 = passive. Flags: 1 = female, 2 = male. | 369 |
| 18 | Quest item marker | 140 |
| 19 | Random treasure pile | 703 |
| 17, 20 | Not read by Bunnyfall. Keep them on import. | 76 / 671 |

Other archives are scenery or NPCs. Archive 216 is fixed treasure. The NPC flat archives become static NPCs.

**Light**: a radius. The common values are 100, 75, 40, 45, 80 and 50.

### 2.6 Actions (the built-in scripting)

Each action has a flag, an axis, a duration (`/20` = seconds), a magnitude and a `NextObjectOffset` link.

The trigger is read differently for the two object kinds:
- **Models:** the *whole* `TriggerFlag_StartingLock` value, used only if it exactly equals a defined trigger
  (otherwise None). The same field holds the door's starting lock (`>> 4`), so **a model can't have both a starting
  lock and a trigger**. The logic compiler enforces this.
- **Flats:** the trigger is the flat's `Flags` field. The flat's `Magnitude` is also used as its axis (a DFU quirk the
  exporter has to reproduce).

Classic usage of model actions: Rotation 403, chain-only nodes (flag 0) 326, Translation 295, undefined 99 (treated as
None) 120, OpenDoor 63, Teleport 59, Activate 56, LockDoor 40, CastSpell 28.

How the game runs them (Bunnyfall's `rdbLayout.js` and `actionSystem.js`, measured 2026-11-04; `dftool actions` and
`src/logic/` follow it):
- A model takes part only when its action kind is not 0, a flat when its action byte is not 0. The 326 classic
  records of kind 0 are skipped, links and all. Editor markers (199) can be linked to (a teleport lands on one) but
  never act. A link of 0 means none (flats store it so).
- **A chain fires at once**: an object sets off its next object *before* its own action. The one exception is "show
  text with input", whose next fires only on a right answer (five riddles have answers).
- A teleport's next object is both where the player lands and, if it acts, set off too.
- Moves and turns toggle: forward the first time, back the next. Flats get no duration (they jump). The ±X/Y/Z kinds
  always take 2.5 s and go axis × 8 units. LID and WHE models, and TRP with axis 13, turn a fixed amount.
- Door verbs act only on the object itself. "Open door" (and "close door" off a door) turns *anything* into a
  lockless swinging door (−90° over 1.5 s): the classic secret walls and trapdoors. Lock/unlock on a non-door does
  nothing.
- Text actions show TEXT.RSC records: 10 exist for "show text" (8600 + index), 5 riddles (5400 + index), 13 door
  texts (7700 + index). New messages need a Bunnyfall change (BUNNYFALL_CHANGES.md).
- The sound index plays on every activation, and doubles as the text record, the spell, and the cap of Hurt21.
- Classic data: 1,368 acting objects in 112 of 187 blocks; 637 chain links, 81 teleports, 7 into objects that don't
  act; most chains are one or two objects (the longest is 32). 432 of 2,475 action doors start locked (30 magic).

Action flags: Translation, ±X/Y/Z moves, Rotation, CastSpell, ShowText, ShowTextWithInput, Teleport, Lock / Unlock /
Open / Close door, Hurt 21–25, Poison, DrainMagicka, Dialogue, Activate, SetGlobalVar. Trigger types: Collision 1/3/9,
Direct 2/6, Attack 5, MultiTrigger 8, Door 10.

Chains link by **an object's `Position` field**. That field is also part of save identity:
`LoadID = blockPosition + objectPosition`.

### 2.7 Enemies

- Random markers draw from `EncounterTables[DungeonType]` (19 dungeon types) and from table 19 for markers under
  water. Both are seeded by the location's `LocationId` and banded by player level.
- **Bunnyfall swaps out a fixed enemy that is too strong for the party's level**, replacing it with a level-appropriate
  pick (`markerTooStrong`). The editor shows this to designers; it isn't a bug.
- Water species are skipped in blocks with no water.
- `IsCustomData` flats take all 16 bits of `FactionOrMobileId` as the enemy type. A custom id only means something
  once a career is registered on the Bunnyfall side.

### 2.8 How Bunnyfall loads it

`vendor/<mod>/WorldData/*.json` goes onto the world-data door (`src/scenes/modWorldData.js`).

| File | Effect |
|---|---|
| `<NAME>.RDB.json` | A block. A new name gets an index past BLOCKS.BSA. |
| `locationnew-<name>-<regionIndex>.json` | A new location in a region |
| `location-<region>-<index>.json` | Replaces a classic location |

Offline, these files load only when Asset Injection is on and the mod's switch is enabled. Online they always load.
JSON field names are defined by `Bunnyfall/src/formats/worldData/worldDataJson.js` (writer) and
`worldDataReplacement.js` (reader).

**A JSON block's `Position` becomes its block position in `LoadID`.** Two custom blocks that both leave it at 0 will
share LoadIDs for the same object positions, which breaks saved enemy and door state. See §6.2.

---

## 3. Concepts

The editor's own model, which is independent of the RDB layout:

| Concept | What it is |
|---|---|
| **Project** | One mod: its name and id, its dungeons, its blocks, its prefabs and its quests. Saved as an editor project file. |
| **Dungeon** | A location: name, region, map position, dungeon type, exterior entrance, and a grid of block instances. |
| **Block** | An RDB block being authored: pieces, entities and logic. It has a role (N/W/S/M/B). It can be a new block, or an imported classic block used as a template. |
| **Piece** | An instance of an ARCH3D model placed in a block: transform, plus door and lock settings if it is a door. |
| **Prefab** | A saved group of pieces, entities and logic, placed as one. This is the editor's "custom block". |
| **Entity** | Anything that becomes a flat or light: enemy marker, treasure, NPC, scenery, light, start/enter/quest markers. |
| **Logic graph** | Per block: trigger and action nodes wired together. Compiled to action records. |
| **Quest** | A DFU quest script, authored as text with editor help (place and marker pickers). |

A **block rotation** in the editor (turning a whole block 90°) is allowed for authoring, but because the location
format can't rotate blocks, a rotated instance is baked into its own exported block. The editor names it and says so.

---

## 4. Architecture

C++23 (for `std::expected`), CMake. Linux is the primary platform; avoid anything that rules out Windows.

```
core/        no dependencies: math (fixed-point-aware), ids, result/error types, logging
formats/     ARENA2 readers: BSA, ARCH3D, BLOCKS (RDB), MAPS (dungeon section), TEXTURE.*, PAL/COL,
             MONSTER/encounter tables. Read-only, no rendering or UI includes.
catalog/     what can be placed: models with bounds, kinds and geometry; block summaries with ports and sockets;
             later flats and enemy types. Built from formats/ at start-up (about 40 ms; no cache needed).
model/       the editor document (§3), plus commands with undo/redo. Holds no file-format knowledge.
snap/        grid, port matching and rotation rules (§2.3, §2.4). Pure functions over model/ types.
logic/       the logic graph and its compiler to action chains (§5).
validate/    rules that read model/ and produce diagnostics (§8).
export/      model/ → DFU World Data JSON and quest text; ID allocation (§6.2).
import/      classic RDB / World Data JSON → model/ (round trip).
render/      OpenGL viewport: meshes, the texture table, flats as billboards, gizmos, overlays.
app/         window, panels and tools: the only layer that knows the UI library.
tests/       unit tests per library; golden-file tests for export.
```

Dependencies only go downward (`app → render/model/... → formats → core`). `formats/`, `model/`, `export/` and
`validate/` build and test without a window, so a command-line exporter and validator come for free.

Libraries: SDL3 (window and input), Dear ImGui with docking (UI, fetched and pinned), OpenGL 3.3 core loaded through
libepoxy, doctest (fetched and pinned), nlohmann/json (fetched and pinned; its default sorted objects make a compact
dump the canonical form patches are hashed over). glm turned out to be unnecessary; the
little matrix maths there is lives in `render/camera.h` and `catalog/space.h`.

---

## 5. Scripting

Three tiers, cheapest first.

1. **Logic graph → RDB action chains.** Nodes are triggers (on touch, on use, on attack, on door) and actions (move,
   rotate, teleport, text, text-with-answer, hurt, cast spell, lock/unlock/open/close door, activate). The compiler
   lowers a graph to linked action records, allocates `Position` values and wires `NextObjectOffset`. It rejects
   graphs the format can't express (branches, more than one outgoing link, a trigger on a locked door) with a clear message. This runs everywhere,
   with no Bunnyfall changes.
2. **Quest scripts.** Text in DFU's QRC/QBN format, with editor help: a list of the dungeon's quest markers, place
   names and foe types, plus a syntax check. Quests are exported to `Quests/` and the mod's quest list. The editor also
   writes the `Quests-Places.txt` row a permanent place needs.
3. **Beyond the format** (cutscenes, conditions, variables across blocks). This needs a small runtime module on the
   Bunnyfall side that reads a sidecar JSON the editor writes. It is out of scope for v1, but the logic graph's node
   set should be designed so these nodes can be added later without changing existing ones.

---

## 6. Export contract

### 6.1 Output tree

```
<modname>/
  <modname>.dfmod.json, modsettings.json   (shape copied from vendor/roleplay-realism)
  WorldData/
    <BLOCK>.RDB.json                       one per authored or baked block
    locationnew-<name>-<region>.json       one per new dungeon
  Quests/
    QuestList-<modname>.txt, <QUEST>.txt
```

The output folder is dropped into `Bunnyfall/vendor/<modname>/`.

### 6.2 Identity and stability

These must not change once a mod is released, because Bunnyfall's saves and online sync key on them:

- **Block names.** 8 characters + `.RDB`, keeping the role letter first. A project-level prefix keeps them from
  colliding with classic blocks and other mods.
- **Block `Position`.** Allocated per block from a project range above every classic BLOCKS.BSA offset (the file is
  32,884,997 bytes). The allocation is stored in the project and never reused.
- **Object `Position`.** Allocated per object, stored on the object, and never renumbered on re-export. Deleting an
  object retires its number.
- **`LocationId` and `MapId`.** Allocated once per dungeon and stored.

The exporter is deterministic: the same project produces byte-identical JSON. This is what the golden-file tests check.

### 6.3 Mapping

| Editor | RDB |
|---|---|
| Piece | Model object + model reference entry. References are deduplicated, max 750 per block. |
| Door piece | Model with `DOR`/`DDR` description and lock bits |
| Enemy (random / fixed / custom) | Flat 199/15, 199/16, 199/16 with `IsCustomData` |
| Start / enter / quest markers, treasure | Flat 199/10, 8, 11, 18, 19. Water level and castle flag go on the first start marker. |
| Light | Light object |
| Logic graph | Action records on models and flats, linked by `Position` |
| Rotated block instance | A new baked block |

Objects are written into a 4×4 root list. The distribution doesn't matter to the game (§2.4), so the exporter puts
them in a fixed, deterministic order.

### 6.4 Bunnyfall-side integration (small, one-time)

1. Load the mod's quests: `src/scenes/quest/questData.js` lists each mod's quest folder by hand, so the mod needs one
   added line.
2. Online: add the mod to `ONLINE_ROOM_MOD_KEYS` (`src/systems/online/onlineLane.js`) so every player in a room has the
   same dungeon.
3. Decide whether authored dungeons opt out of "smaller dungeons" and the medium size (`src/world/smallerDungeons.js`).
   Otherwise a 9-block design can be cut down to a 5-block plus shape.
4. Custom enemy ids: a career registration per id (`registerCustomCareerTemplate`). That is JS for now; see §7.2.

---

### 6.5 New places and new blocks (milestone 4)

Measured against Bunnyfall's loader (`worldDataReplacement.js`) and a classic location serialised by its own
`locationToDfuJson` (Privateer's Hold):

- **A new place** is `WorldData/locationnew-bunnyfall-<slug>-<regionIndex>.json`. Bunnyfall adds it to the region at
  load, after the classic locations, and refuses it if its map id or its name is already the region's.
  The `bunnyfall-` part is the editor's convention: Bunnyfall marks such places as editor dungeons, so they are never
  rebuilt smaller (offline settings or online sizes).
- **Map id** = `(1900 + n) << 20 | pixel`, where `pixel = y * 1000 + x` and `n` is the dungeon's number in the
  project. Classic high parts go up to 1832, and DFU reads map ids as signed 32-bit, so 1900-2047 (148 dungeons) is the
  editor's range. `ExteriorData.MapId` is the pixel part alone.
- **Location ids**: classic ones go up to 51000 and Bunnyfall's arena undercroft is 55398. Editor places take
  `60000 + 2n` for the exterior and `60001 + 2n` for the dungeon, which names its exterior (`ExteriorLocationId`).
  The dungeon's id seeds its texture table and random enemies.
- **Position**: header `X = x * 32768 + 16384`, `Y = (499 - y) * 32768 + 16384` (world units; the map pixel is
  `X / 32768`, `499 - Y / 32768`); `Longitude = x * 128 + 64`, `Latitude = (499 - y) * 128 + 64`. The pixel must
  be inside the region, on land, and hold no other location. `Politic` = region + 128.
- **The PAK maps sit one pixel west of the map table.** Read at `x + 1`, POLITIC.PAK puts 15,248 of the 15,251
  classic locations in their own region (14,960 at `x`); Bunnyfall's terrain reads CLIMATE.PAK at `x + 1` too. So the
  editor checks region and water at `x + 1`, and writes the climate of the land there.
- **Climate**: the world climate of the land at the pixel (CLIMATE.PAK at `x + 1`) with the settings
  `GetWorldClimateSettings` derives from it (ground, nature and sky archives, people). Bunnyfall reads only `WorldClimate`; the rest is for DFU.
- **Exterior**: a 1x1 grid naming one classic entrance block, chosen by the designer. Classic dungeons use 136 of them,
  mostly `DUNGAA00`-`DUNGAA18` and the `RUINAA` ruins.
- **New blocks** named in the dungeon get indices past BLOCKS.BSA and are fetched as `<NAME>.json`. A patch that
  rebuilds under the new name counts, so a block started from a classic one ships as a patch (§10.2); one built from
  empty ships whole. New block names keep the role letter first and are `<letter>BF<5 digits>.RDB`. Each new block
  takes its own `Position` (the save-identity base) from 40,000,000 up, past BLOCKS.BSA's 32,884,997 bytes.

## 7. Custom content

### 7.1 Prefabs (v1)

Pure editor data. On export they turn into ordinary objects, so they need nothing from Bunnyfall. Prefabs can be
shared between projects as files.

Two kinds:
- **Furniture and set pieces** dropped into a level as one thing: a bench, a table with chairs and candles, a camp
  (bedroll, fire, crates), an altar, a shelf of bones. A prefab is any mix of models, flats and lights with their offsets
  and turns, placed, moved and turned as a group, and broken apart when needed.
- **Whole blocks** made once and reused across dungeons (milestone 4).

A starter set should ship with the editor, cut from things classic blocks already arrange (furniture props are the
X / T / M / B description families, §2.4), plus "save selection as prefab".

### 7.2 Custom enemies (later)

The editor can place `IsCustomData` enemies today, but they only work once Bunnyfall knows the id. The clean route is
a Bunnyfall module that reads enemy definitions from the mod's JSON (base career, name, sprite archive, stats) and
registers them. The editor would then write that JSON as well. Until that exists, the editor allows only classic ids
by default.

### 7.3 Custom models (milestone 8)

Bunnyfall's custom model registry (`world/customModels.js`) is asked before ARCH3D everywhere a model is built, and
takes dfMeshToModel's shape. The editor writes that shape as data, and one small Bunnyfall module registers it
(BUNNYFALL_CHANGES.md 3). These models are Bunnyfall-only: DFU draws nothing for an id it does not have.

- **The editor's form** (`src/mesh/`, kept in the project file): points and polygon faces in editor space. Each
  face has a texture (archive, record) and a Hammer-style alignment: projected onto its plane along axes that ignore
  its shape (walls run right and down as seen from the front, floors along X and Z), at a scale in units per texel,
  with an offset and a turn. Neighbouring faces line up. Classic density, measured on the corridors and rooms, is 2
  units per texel (a 64-texel picture over a 128-unit cell).
- **Winding**: a face's points go round it so the fan's `cross(b - a, c - a)` points to the side it is seen from, as
  DFU's ARCH3D triangles do in editor space (measured on every triangle of five classic models).
- **Ids**: from 7,000,001, issued once by the project (save identity, as block names are). Classic ids stay under
  100,000; Bunnyfall's own registrations reach 864,111.
- **Export**: `Models/<id>.json`: triangles in metres (editor space × 0.025 is Unity's frame), unit normals, UVs as
  DFU makes them (`u / width`, `-(v / height)`, with the base archive's size), and sub-meshes by texture. Texture
  numbers are kept, so a face in a dungeon set (119, 120, 122, 123, 124, 168) is swapped by the dungeon's table
  like a classic wall.
- **No Daggerfall geometry is copied.** A classic model can be shown as a reference (lines) to build to and snap to;
  nothing of it goes into the model.

---

## 8. Validation

Run before every export. Errors block export; warnings don't.

**Errors**
- No starting block, the starting block isn't at grid (0,0), or it is a `B` block.
- No start marker in the starting block, or more than one exit door (70300) or one outside the starting block.
- A non-`B` block with an open side (§2.2).
- Duplicate block names; Position or LoadID collisions inside the project or with classic data.
- More than 750 model references in a block; a logic link to a missing object; an unknown action or trigger value.
- A custom enemy id with no definition, when custom enemies are disabled.

**Warnings**
- A socket open on one side of a block seam and closed on the other (§2.3).
- An overhang (architecture crossing an edge outside a socket) that collides with the neighbour's geometry.
- A piece off the 64-unit grid or at a non-90° rotation (allowed, just flagged).
- A water enemy in a block with no water; a fixed enemy that will be downgraded at low party levels (§2.7).
- Quest markers only in `B` blocks; doors closer than 1.4 m (they will be merged).

---

## 9. Milestones

1. **Readers + catalog.** C++ versions of the BSA / ARCH3D / RDB / MAPS readers. A CLI that dumps a block and
   reproduces the `dfprobe.py` numbers. Tests against the real ARENA2.
   *Status (2026-11-02): done.* Readers, catalog (model kinds and geometry, port detection, the socket standard in
   §2.3), `dftool` and tests. Every classic block, dungeon and used model parses. Texture and palette readers moved to
   milestone 2, where the viewer needs them. Sorting the remaining "prop" description families (X, B, T, M, ...)
   into palette groups is left for milestone 5.
   *Known gaps, needed before milestone 3:* DFU's per-block repairs (`FixRdbData`, a handful of N/W blocks) and
   Bunnyfall's ARCH3D byte patches are not applied yet. A patch's base block must match what Bunnyfall reads, so both
   must be ported before patches are written.
2. **Viewer.** Render any classic dungeon with its texture table; fly camera; pick objects and inspect them.
   *Status (2026-11-02): done.* Texture, palette and CLIMATE.PAK readers; ARCH3D decomposition ported from Daggerfall
   Unity (corners, fitted UVs, packed UVs; all 1,043 dungeon models build); dungeons linked to their map table row
   (dungeon type, map id, climate); the classic texture table; flats and editor markers as billboards; picking and the
   inspector; screenshot and hidden smoke-test modes. Lighting is the editor's own (a soft camera light), not the
   game's, and the "randomised" texture-table settings (modes 1-4) are not offered yet.
3. **Round trip.** Import a classic block, export it as JSON, load it in Bunnyfall, and confirm it plays the same.
   This proves the export contract before any authoring UI exists.
   *Status (2026-11-02): export side done, in-game check pending.* Daggerfall Unity's seven classic block repairs
   (FixRdbData) are applied wherever blocks are read; text fields decode as Bunnyfall decodes them (lossy UTF-8,
   trailing NULs only); RDB blocks serialise to World Data JSON; canonical form, sha256, patch ops and the diff are
   ported. Proof against shipped data: Aquatic Sprites' three RDB patches rebuild to the author's sha256, and the
   editor's own patches of the same blocks come out with identical ops. `dftool export-block / make-patch /
   rebuild-patch` drive it by hand. The Bunnyfall side is in (local copy, 2026-11-02): the `bunnyfall-dungeons`
   mod (modSettings.js, room-owned in onlineLane.js, a Features row, the Dungeons settings section) and a test patch,
   `vendor/bunnyfall-dungeons/WorldDataPatches/S0000999.RDB.json` (six braziers round Privateer's Hold's start).
   Left: seeing it in the game.
   Bunnyfall's ARCH3D byte patches are still unported (they change how a few models draw, not what is exported).
4. **Block-level authoring.** Dungeon grid, place blocks, port checks, border auto-cap, new-location export.
   *Status (2026-11-03): done, the in-game walk-in pending.* The project file (`src/project/project.*`: custom blocks
   and dungeons, every identity number issued once); export to the mod folder (`world_export.*`: checks, new-place
   JSON, block patches over templates or whole blocks, a manifest so re-exports tidy up); the Project panel with the
   dungeon grid, doorway ticks, auto caps and the problem list (`src/app/viewer_project.cpp`); the sizing grid in the
   3D view. Bunnyfall marks `locationnew-bunnyfall-*` places as editor dungeons: never rebuilt smaller (offline or
   online) and never cloned as spawned dungeons. Checked through Bunnyfall's own loader under Node: the place joins
   its region, the custom block is served, the dungeon lays out with its start marker, and it stays full size.
5. **Piece-level authoring.** Snap grid, gizmos, undo/redo, prefabs, entity placement, validation panel.
   *Status (2026-11-03): first part done, ahead of milestone 4.* Editing one classic block in the viewer: placing
   markers, enemies (random and fixed, with type, gender, passive, slot), treasure, scenery and lights on the surface
   under the pointer; selecting, moving (grab, nudge, typed positions), turning models, duplicating, deleting; undo and
   redo; saving as a patch to Bunnyfall's mod folder and reopening it. New objects take save identities from 900000 up
   and keep them. `src/edit/` holds the rules (no UI), `src/app/viewer_edit.cpp` the panels. Not yet: placing
   architecture pieces, a model palette, prefabs (benches and other set pieces, §7.1), rotation gizmos, validation,
   and editing blocks other than classic ones (new blocks come with milestone 4). The controls need a proper pass:
   the first version is keyboard-heavy and placement-only.
   *Step 1 (2026-11-03): done.* First-launch setup window and saved paths (`src/app/config.*`, `setup.*`); a
   Hammer-style dark layout (`viewer_layout.cpp`, `theme.cpp`): menu bar, toolbar, status bar, dock space with a
   default layout; four views (`viewer_views.cpp`), 3D plus orthographic Top/Front/Side drawn wireframe into their own
   render targets, each maximisable (Shift+Z), 2D pan, zoom about the pointer and a grid.
   *Step 2 (2026-11-03): done.* Multi-select (Shift/Ctrl clicks, box drags, select all), group move/turn/nudge/
   duplicate/delete as single undo steps (`src/edit/transform.*`: snapshot-based, turns about the group's middle),
   move arrows, free-move square and a turning ring with snap in every view (`viewer_select.cpp`), dragging a selected
   object moves the selection, hiding by kind / by hand / isolating a block (hidden things are neither drawn nor
   picked). Not yet: hiding what the player cannot reach (needs a walk of the doorways).
   *Step 3, the piece kit (2026-11-03): done.* Model palette with rendered thumbnails (`render/thumbnails.*`,
   `viewer_palette.cpp`), placing models with a preview, into empty space too (new blocks), new model references taken
   in the block's 750 slots (`BlockEdit::addModel`/`setModel`). Piece snap from measured classic data (`dftool snap`:
   of 15,783 architecture pieces 96% sit on a 64-unit grid across, 94% on 128 up, 99.9% on quarter turns). Texture
   twins: models of identical shape (points and faces) in other texture sets, including 826 ARCH3D models no classic
   block uses (1,181 of 1,460 architecture models have one); swapped singly or as a set for a selection. Prefabs
   (`project/prefab.*`): a selection saved as a one-root RDB block in World Data JSON under `prefabs/`, anchored at the
   middle of its bottom, placed turned and snapped with its internal action links remapped. Outliner
   (`viewer_outliner.cpp`) and framing the selection (F).
6. **Logic graph.** Graph editor and compiler to action chains; door and lock settings.
   *Plan (2026-11-04):* chains can't branch, so instead of a free node canvas: (1) see the logic that is there,
   (2) edit chains (trigger and action pickers, link by clicking, preview of moves), (3) doors and locks with common
   set-ups, (4) a reachability check (entrance to exit and every quest marker), replacing the dropped "hide what the
   player can't reach" view filter.
   *Step 1 (2026-11-04): done.* `src/logic/action.*` reads nodes and links as the game does and describes them in
   words; `formats/text_rsc.*` reads TEXT.RSC; `dftool actions` measures classic use (§2.6). In the viewer
   (`viewer_logic.cpp`): a Logic part in Properties (what it does, its text, door lock, links both ways, the whole
   chain, warnings) and link arrows in every view.
   *Step 2 (2026-11-04): done.* `src/logic/edit.*`: setting an action (a model's record is made on demand at
   Position + 16), trigger, link and values, with moves and turns in editor terms (directions in editor space;
   flats can only use the fixed moves) and the motion a preview plays (`motionOf`, `turnAt`, checked against
   Bunnyfall's `trs`). In the viewer: the editable Logic part, link by clicking, take out of chain, and the preview
   (forward, a pause, back; posed for the frame only). `--check-edit` makes a lever, links it to a piece by a click,
   previews, undoes and saves the link in its patch.
   *UI pass (2026-11-04):* Properties is a floating window opened by a double-click (or Alt+Enter), like Hammer's
   Object Properties, with General and Triggers tabs; the Edit panel keeps only the Place list, whose fixed enemy and
   scenery entries pop up a picker first.
   *Step 3 (2026-11-04): done.* Doors and locks. A starting-lock picker (the 16 values; it replaces the door's own
   trigger, which shares the field) and a warning for a magic lock nothing opens. Bunnyfall's OpenDoor also unlocks,
   so a lever opens a locked door. A right-click menu in the views (a right-click that neither looks nor pans):
   lever or plate that opens a door (the classic 61028 L0Z, used, turns 45° about Z over 0.6 s; 74087 TSB, stood on,
   passes it on; placed, linked and the door set as one undo step), "opened by" / "set off by" (link picked
   backwards), secret wall (slides up its own height) and secret door (swings), teleporter, trap, and placing a lever
   or plate on empty space.
7. **Quests.** Script editor with markers and places; quest-list and places-table export.
   *Deferred (2026-11-04).* Research done, nothing built. What Bunnyfall does (`src/scenes/quest/questData.js`,
   `src/systems/quest/place.js`, `questLists.js`, `rrInstall.js`):
   - Quests are DFU QRC/QBN text. A quest names a fixed dungeon only through a Quests-Places row:
     `Name, <locationId + 1>, 0x0001, -1` (p1 = location id + 1 means its dungeon; p2 = `0xfaNN` picks quest
     marker NN). Markers are numbered per kind (spawn 199/11, item 199/18) in the dungeon's block order, then root and
     object order, so the editor can show each marker's number.
   - Quests are offered through `QuestList-<name>.txt` rows (group, membership, minReq, flag).
   - Bunnyfall loads a mod's quests only through code written for that mod: a glob line in `questData.js`,
     `registerQuestList(name, isOn)`, and `addIntoQuestTables({places: [...]})`. So quests need one Bunnyfall change
     (load `vendor/bunnyfall-dungeons/Quests/`, register its list, add its places rows from a file the editor writes).
   - No debug command to start a quest by name was found; testing would go through a quest list group.
   Plan when picked up: (1) quests in the project and their export plus that change, (2) pickers for places, markers,
   foes, items, (3) a syntax check.
8. **Custom models ("B").** The model editor (§7.3).
   *Stage 1 (2026-11-04): done.* `src/mesh/` (the mesh, triangulation with ear clipping, texture alignment, shapes:
   box, quad, stairs; extrude, fill, cut, split edge, weld, merge, remove, flip, copy, move/turn/mirror; undo; both
   JSON forms), custom models in the project file, catalog and caches (placed, snapped and port-checked like classic
   ones), export, checks (a block placing a model the project lost is an error; faces that are not flat, have no area
   or repeat a point are warnings). In the viewer (`viewer_mesh.cpp`): File > New model or the Models panel's Custom
   tab opens the model alone in the four views; points or faces picked by click or box, moved by dragging (corners
   snap to the grid or to a point near the pointer, the model's own or a reference's) or along an axis handle; a
   texture picker over the dungeon sets and any archive, and alignment fields (fit, classic, turn). `--check-model`
   runs it all as clicks would. Bunnyfall's loader is in and checked under Node; not yet seen in the game.
   *Cuts (2026-11-04):* `cutOpening` (a rectangle in the face's own across/up terms; the face becomes the pieces round
   it, clipped from the face so any flat face works; an optional reveal lines the hole behind the face) and
   `sliceFaces` (a grid-line knife). New corners go into every edge they lie on (no T-junction cracks); faces with
   points along an edge are ear-clipped, so no triangle has zero area.
   *UI pass (2026-11-04):* the model editor is its own views tab beside Viewports (each keeps its camera; the tab
   shown decides the mode), with a right-click menu on faces, points and empty space. In the dungeon, the turn handle
   has a ring per axis (`edit::turnedAbout`: a world turn composed with the model's rotation and written back as
   Daggerfall's three angles, `catalog::rdbRotationOf`), and Properties has Tip and Roll quarter turns.
   *Edges (2026-11-04):* a third pick kind (1 points, 2 edges, 3 faces): `edgesOf`, `extendEdges` (grow a face from
   an edge, wound to match the face beside it), `outwardOf`, `dissolveEdge` (two coplanar faces into one),
   `connectEdges` (cut a face between two edges' middles).
   *Stage 2 (next):* the designer's own texture images (PNG into stand-in archives, `addVendorTextures`).
9. Later: custom enemies (§7.2), the Bunnyfall runtime module (§5.3).

---

## 10. Decisions and open questions

### Decided (2026-11-02)

1. **UI stack:** SDL3 + Dear ImGui (docking) + OpenGL 3.3 core, as proposed in §4.
2. **No original game content ships. Blocks export in one of two ways, chosen automatically:**
   - **Built from scratch:** exported whole as `<NAME>.RDB.json`. It holds only the project's own placements, which
     refer to ARCH3D model ids and texture numbers. It contains no Daggerfall bytes; every mod does the same.
   - **Started from a classic block** (imported as a template, or containing pieces copied from one): exported as a
     patch in Bunnyfall's `dfe-worlddata-patch/1` format (`Bunnyfall/src/formats/worldData/worldDataPatch.js`),
     saved in `vendor/<mod>/WorldDataPatches/`. The patch names the classic block it starts from
     (`base: {kind:'block', block, index}`), the file it rebuilds (the project's new block name, with an op that sets
     `Name`), the `sha256` of the result, and the edit ops. Bunnyfall rebuilds the block from the player's own
     BLOCKS.BSA at load.
   - The block's origin is recorded in the project, so the choice is never a guess. The UI shows it on the block
     ("from N0000012 → ships as patch").
   - Implementation needs in C++: `blockToDfuJson` (classic block → DFU JSON, field for field), the op diff, and
     `canonicalJson` (sorted keys, `JSON.stringify` values) + SHA-256, to match Bunnyfall's hash. RDB JSON is integers
     and strings only, so the canonical form is safe to reproduce. Golden tests: a patch the editor writes must
     rebuild to the same sha256 as Bunnyfall's own `tools/worldDataPatch.mjs`.
3. **Exterior entrance:** the designer chooses it per dungeon, from the classic dungeon-entrance RMB blocks
   (catalogue still to build, milestone 4).

4. **Smaller dungeons:** authored dungeons are **always exempt** from Bunnyfall's smaller and medium dungeon modes,
   like the main-story dungeons. Bunnyfall side: a small change in `src/world/smallerDungeons.js` that recognises the
   editor's dungeons (§6.4.3). Editor side: nothing beyond marking the dungeon as authored.

### Open

5. **Records 199/17 and 199/20.** Their meaning is unknown. Keep them on import; don't offer them in the palette until
   identified.
