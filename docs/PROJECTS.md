# Your own dungeons and mods

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
- **View in 3D** shows the dungeon; **Export to Bunnyfall** writes it into the project's mod folder (below): blocks
  copied from classic ones as patches, blocks made from nothing whole, each dungeon as a new place. A re-export
  removes the files of deleted dungeons and blocks.

The sizing grid (View panel, **Grid**) shows block edges, 128-unit room cells and the doorway sockets, at a height
you set (or the floor below you, or the selection's level).

## The mod

A project exports as a mod of its own: a folder of Bunnyfall's `vendor/`, set in the Project panel's **Mod** tab.

- **Folder**: the mod's id in Bunnyfall (a-z, 0-9 and -). The default, and what older projects use, is
  `bunnyfall-dungeons`. Edited classic blocks are saved into the same folder's `WorldDataPatches/`.
- **Author** and **Description**: the mod's switch in Bunnyfall (Settings > Gameplay > Dungeons) reads "*project
  name* by *author*", with the description as its note.
- **Id range** (0-7): where the project's block names, location ids and model ids come from, so two mods never take
  each other's. A new folder picks the lowest range no other editor mod uses; once the project has made a block,
  dungeon or model the range is fixed. The tab lists the other editor mods in `vendor/` and their ranges.

The export writes `dungeon-editor.mod.json` (the title, author, description and range) and a `README.md` (unless the
folder has one of its own) beside the world data. Bunnyfall gives every folder with that manifest a switch, a
Features row and an online room key (`src/systems/editorMods.js`; Bunnyfall change 4 in
[BUNNYFALL_CHANGES.md](BUNNYFALL_CHANGES.md)). The export refuses a folder holding files the editor did not write
(another mod), and an id range another editor mod uses; it warns when two mods ship the same block file (Bunnyfall
loads only one) and when the Bunnyfall checkout is too old to load a mod of another name.

**File > New project...** starts another mod: a new project file, named for its file (`crypts-of-hope.project.json`
is "Crypts Of Hope", exporting to `vendor/crypts-of-hope`, in the lowest free id range). **Open project...** changes to
another, **Save project as...** keeps a copy under a new name and goes on in it (the copy still exports to the same
folder until you give it its own). Changing project asks first when something is unsaved, closes the model editor
and any block edit, and reads the views again; if they showed the old project's blocks, Castle Daggerfall opens. The
editor remembers the project for the next start. `--project FILE` picks one for a single run; `--mod DIR` exports
somewhere else entirely.
