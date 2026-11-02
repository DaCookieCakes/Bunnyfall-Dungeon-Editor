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
- New 3D models or textures. See §7.3 for why and what it would take.
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
- The first letter of a block's name gives its role, and the game reads it: `N` normal, `W` wet, `S` quest,
  `M` mausoleum, `B` border. Bunnyfall's "smaller dungeons" mode treats every `B` block as a cap and ignores the
  quest markers in it.

### 2.3 Block-to-block connection

Measured across N and W blocks, architecture crosses block edges mostly at **640 and 1408 units along an edge**.
These two points mirror each other (1408 = 2048 − 640), and together they are the large majority of crossings. A
smaller set crosses at 128, 1152, 1664 and 1920. Since blocks can't rotate, a standard block has to be usable on any
side. In practice that means **two doorway ports per side, at fixed positions**.

Design consequence: the editor detects ports from a block's geometry rather than hardcoding them, but treats 640 and
1408 as the standard ports. It warns when a neighbour has a port that isn't matched on the touching side.

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
| 8 | Enter marker | 10 |
| 10 | Start marker. Also carries the block's water level (`SoundIndex·−8`, 0 means none) and the castle flag (`Magnitude≠0`). | 167 |
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
catalog/     what can be placed: models with bounds, ports and descriptions; flats; enemy types;
             classic blocks as templates. Built from formats/, cached on disk.
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

Proposed libraries, to be confirmed (§10): SDL3 (window and input), Dear ImGui with docking (UI), OpenGL 3.3 core via
glad, glm, nlohmann/json, doctest.

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

## 7. Custom content

### 7.1 Prefabs (v1)

Pure editor data. On export they turn into ordinary objects, so they need nothing from Bunnyfall. Prefabs can be
shared between projects as files.

### 7.2 Custom enemies (later)

The editor can place `IsCustomData` enemies today, but they only work once Bunnyfall knows the id. The clean route is
a Bunnyfall module that reads enemy definitions from the mod's JSON (base career, name, sprite archive, stats) and
registers them. The editor would then write that JSON as well. Until that exists, the editor allows only classic ids
by default.

### 7.3 Custom models (later)

Bunnyfall only adds models through JS builders (`registerCustomModel`). Its POI system imports glTF, but it is built
for exteriors and goes through server approval. A data route for dungeon models would need a Bunnyfall loader for a
mod's mesh files, mapped onto model ids that ARCH3D doesn't use. Deferred until v1 ships.

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
- An unmatched port between neighbours (§2.3).
- A piece off the 64-unit grid or at a non-90° rotation (allowed, just flagged).
- A water enemy in a block with no water; a fixed enemy that will be downgraded at low party levels (§2.7).
- Quest markers only in `B` blocks; doors closer than 1.4 m (they will be merged).

---

## 9. Milestones

1. **Readers + catalog.** C++ versions of the BSA / ARCH3D / RDB / MAPS readers. A CLI that dumps a block and
   reproduces the `dfprobe.py` numbers. Tests against the real ARENA2.
   *Status (2026-11-02):* readers, `dftool` and tests are done; every classic block, dungeon and used model parses.
   Still to do: the catalog (model bounds, port detection, descriptions). Texture and palette readers moved to
   milestone 2, where the viewer needs them.
   *Known gaps, needed before milestone 3:* DFU's per-block repairs (`FixRdbData`, a handful of N/W blocks) and
   Bunnyfall's ARCH3D byte patches are not applied yet. A patch's base block must match what Bunnyfall reads, so both
   must be ported before patches are written.
2. **Viewer.** Render any classic dungeon with its texture table; fly camera; pick objects and inspect them.
3. **Round trip.** Import a classic block, export it as JSON, load it in Bunnyfall, and confirm it plays the same.
   This proves the export contract before any authoring UI exists.
4. **Block-level authoring.** Dungeon grid, place blocks, port checks, border auto-cap, new-location export.
5. **Piece-level authoring.** Snap grid, gizmos, undo/redo, prefabs, entity placement, validation panel.
6. **Logic graph.** Graph editor and compiler to action chains; door and lock settings.
7. **Quests.** Script editor with markers and places; quest-list and places-table export.
8. Later: custom enemies (§7.2), custom models (§7.3), the Bunnyfall runtime module (§5.3).

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
