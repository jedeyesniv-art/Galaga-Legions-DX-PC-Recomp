# Galaga Legions DX: native PC port

A native Windows build of **Galaga Legions DX** (Xbox 360 Live Arcade, 2011), made by statically recompiling the
game's PowerPC code to x86-64 with the [ReXGlue SDK](https://github.com/rexglue/rexglue-sdk) (built on Xenia's
kernel, GPU and audio emulation). It is the sibling of the
[Pac-Man Championship Edition port](https://github.com/jedeyesniv-art/Pacman-CE-PC-Recomp) and reuses everything
learned there. A hobby project, not affiliated with or endorsed by Bandai Namco or Microsoft.

**No game files are included.** You need your own copy of the game, dumped from your own Xbox 360.

## Features

- Full-speed, full-game play at a steady 60 fps, light on CPU and GPU, on Windows 10/11 and the Steam Deck
  (one build for both; Vulkan by default, Direct3D 12 selectable).
- **Steady menus**: the original menu skin flashes parts of the mode select, mission preview and result screens
  on and off 15 times a second. This port shows them steadily at the same brightness (switchable back).
- Renders at your screen's resolution (3x on 4K) with the picture never stretched; fullscreen toggle (F11 /
  Alt+Enter).
- Plays straight from the game's package file as dumped from the console (STFS), or from unpacked files.
- Xbox controller and keyboard; achievements recorded locally (the in-game Achievements menu opens the list; B
  closes it); automatic save backups.
- Not available: the Championship Score Attack Tournament and leaderboards, which need Xbox Live.

## Playing

Download the release zip, unpack it anywhere, and put your copy of the game in its `game\` folder: the package
file from your console (a file with a long hexadecimal name, folders and all) or the unpacked files. Run
`galaga.exe`. On a Steam Deck, add `galaga.exe` as a non-Steam game and run it with Proton. The included
`README.txt` has the controls and options.

## Building from source

Requirements: Windows, Visual Studio 2026 with the C++ workload and its Clang/LLVM tools, CMake, Python 3, and
your own copy of the game.

1. Build the ReXGlue SDK at the pinned commit with this project's patches and the Vulkan backend:
   see [patches/rexglue-sdk/README.md](patches/rexglue-sdk/README.md).
2. Put your game files in `private\` (git-ignored). From the console's package file:
   `python Tools\stfs_extract.py <package> private`, then make the PE basefile for analysis with xextool
   (`xextool -b private\default.exe private\default.xex`).
3. In a Visual Studio x64 developer prompt (`vcvars64.bat`) with Visual Studio's LLVM `bin` folder on `PATH`,
   from `rexglue\`:

   ```
   cmake --preset win-amd64-release -DCMAKE_PREFIX_PATH=<SDK install dir>
   cmake --build --preset win-amd64-release
   ```

   The recompiler runs as part of the build and generates the game code into `rexglue\generated\default\`
   (git-ignored: it is derived from the game).
4. `rexglue\out\build\win-amd64-release\galaga.exe` runs with the game files in `private\`.
   `Tools\package_portable.ps1` builds the portable folder in `dist\` (`-Release` also writes the release zips).

## Repository layout

- `rexglue\`: the project: recompiler configuration (`galaga_config.toml`: setjmp/longjmp, function starts the
  analysis misses, mid-asm hooks) and the host code in `src\` (startup and defaults, the GPU-wait and command
  buffer fixes, frame statistics and diagnostics, steady menus, the achievements overlay).
- `patches\rexglue-sdk\`: the SDK patches this build needs.
- `Tools\`: `stfs_extract.py` (unpack the console package), `ppcinspect.py` (disassembler helper),
  `package_portable.ps1` (portable folder and release zips).
- `CLAUDE.md`: detailed technical notes: what was found in the game's code and why each change was made.

## Licence

This project's own code is released under [MIT No Attribution](LICENSE). The release binaries include
third-party software under its own licences; see `licenses\THIRD-PARTY-NOTICES.txt` in the release (from
`rexglue\licenses\`). The source code of the LGPL parts (FFmpeg, libmspack) at the exact revisions used is
attached to each release.

Galaga Legions DX is © Bandai Namco Entertainment Inc.
