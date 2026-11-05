# Tools and headless checks

## The editor without a screen

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
