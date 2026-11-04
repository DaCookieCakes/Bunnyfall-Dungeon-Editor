# Bunnyfall changes for the Dungeon Editor

Every change the editor needs in Bunnyfall, made or still to make, for one pull request once the editor is done. The
local Bunnyfall copy is not a fork, so these exist only there until then. Each change is tagged `BUNNYFALL` in the
code (`grep -rn BUNNYFALL src` finds them, alongside the fork's other tagged changes).

Before the PR: run `npx eslint` on the changed files, `node tools/l10nExtract.mjs`, and `npm run build`
(`npx vite build` in a copy that is not a git checkout).

## Made

### 1. The `bunnyfall-dungeons` mod (2026-11-02)

The editor's output loads as a vendored world-data mod, like Aquatic Sprites.

| File | Change |
|---|---|
| `src/systems/modSettings.js` | `MOD_SETTINGS['bunnyfall-dungeons']`: title "Bunnyfall Dungeons", author "Bunnyfall", one key `Enabled` (default on). New string `mods.bunnyfallDungeons.Enabled.description`. |
| `src/systems/online/onlineLane.js` | `ONLINE_ROOM_MOD_KEYS['bunnyfall-dungeons'] = { Enabled: true }`: online the room owns the switch, because the dungeons change walls, floors and foes every player meets. |
| `src/systems/features.js` | `modFeature('bunnyfall-dungeons', ..., 'world')`: its Features row ("Takes effect when the game next loads"). |
| `src/ui/settings/settingsMap.js` | `feat:mod-bunnyfall-dungeons` in the Dungeons section, after Aquatic Sprites. |
| `locales/en/Port_Strings.csv` | The new string, from `node tools/l10nExtract.mjs`. |
| `vendor/bunnyfall-dungeons/README.md` | What the folder is. The editor writes `WorldData/`, `WorldDataPatches/` and `.dungeon-editor-export.json` (its manifest) beside it. |

Offline the mod's files need Replace Game Artwork (Asset Injection), like every world-data mod; it is on by default.

### 2. Editor dungeons keep their layout (2026-11-03)

Places the editor makes are named `locationnew-bunnyfall-<number>-<slug>-<region>.json`. Bunnyfall marks them, and
treats them like the main-story dungeons: laid out as built.

| File | Change |
|---|---|
| `src/formats/worldData/worldDataReplacement.js` | `EDITOR_LOCATION_PREFIX = 'locationnew-bunnyfall-'`; in `getDFRegionAdditionalLocationData`, a location from such a file gets `dfLocation.editorDungeon = true`. |
| `src/world/smallerDungeons.js` | `useSmallerDungeon` answers false and `dungeonSizeFor` answers `'full'` for an editor dungeon: never rebuilt smaller, offline (Smaller Dungeons, medium size) or online (the world's seeded sizes). |
| `src/world/spawnedDungeons.js` | `spawnTemplates` leaves editor dungeons out: one designed place is never copied onto random map pixels. |

Checked under Node through Bunnyfall's own loader: the place joins its region, its custom blocks are served, it lays
out with its start marker, stays full size offline and online, and is no spawn template. Walking in was confirmed in
the game on 2026-11-03.

## Still to make

### 3. An explicit texture table per dungeon (planned)

A dungeon's wall, floor and sewer textures come from a table seeded by its location id and climate
(`world/dungeon/dungeonTextures.js`). The editor gives each placed piece one of the table's slots (texture "twins":
67000 / 67100 / 67200 are one corridor in sets 120 / 123 / 124), but which pictures fill the slots is the seed's.
To let a designer choose them: an optional field in the editor's location JSON (the six archives) that
`dungeonTextureTable` uses when present. DFU ignores the field. Not yet written.

### Later (separate work)

- **Custom geometry and textures** (milestone "B"): a loader for a mod's meshes and pictures, mapped onto model ids
  ARCH3D does not use, so the editor can ship new room shapes and texture images. Bunnyfall-only content.
