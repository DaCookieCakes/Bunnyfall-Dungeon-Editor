# Bunnyfall Dungeon Editor

A C++ snap-builder for Bunnyfall dungeons. See `docs/DESIGN.md` for what it produces and how it is put together.

## Build

Needs CMake 3.28+, a C++23 compiler (GCC 14+ or Clang 18+) and Ninja (optional). Tests fetch doctest on first
configure.

```sh
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure
```

Data tests read the ARENA2 directory from `DE_ARENA2`. CTest defaults it to `../DFData/ARENA2`; pass
`-DDE_ARENA2=/path/to/ARENA2` to configure another. Without the data, those tests are skipped.

## dftool

```sh
./build/dftool <ARENA2> stats              # counts and grid measurements over all dungeon blocks and dungeons
./build/dftool <ARENA2> block N0000071     # one block's model table and objects
./build/dftool <ARENA2> dungeon Daggerfall # a dungeon's block grid (name or locationId)
./build/dftool <ARENA2> model 67000        # a model's size and textures
```

`tools/dfprobe.py` is the Python probe the design numbers were first measured with.
