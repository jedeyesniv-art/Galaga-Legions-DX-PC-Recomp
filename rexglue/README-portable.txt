Galaga Legions DX - native PC build, version 1.0.0
==================================================

Setup
  Put your own copy of the game in the game\ folder, either way:
    - the game's package file as dumped from your console (one file with a long hexadecimal name, often
      inside 58410AF9\000D0000\ folders - copy those along, or just the file), or
    - the unpacked files: game\default.xex, game\data\..., etc.
  Then run galaga.exe. No installation is needed; the whole folder can be moved or copied.

Files
  galaga.exe, *.dll   the game and its runtime (Visual C++ runtime included)
  game\               your game files (not included)
  userdata\           created on first run: saves and the shader cache. Delete it to start fresh.
  userdata\save-backups\  automatic copies of your saves, made at startup whenever they changed (the
                      newest 10 are kept). To restore one, copy its contents back into userdata\.
  licenses\           licences: this port (MIT-0) and the third-party software it uses
                      (start with THIRD-PARTY-NOTICES.txt)

Requirements
  64-bit Windows 10 or 11 with a Vulkan-capable GPU (Direct3D 12 also works, see the options below),
  or a Steam Deck: add galaga.exe as a non-Steam game and run it with Proton.

Display
  The game renders at your main monitor's resolution, scaled up from 1280x720 by a whole number. The picture
  is never stretched. The game starts fullscreen; F11 or Alt+Enter switches to a window.

Options (on the command line, e.g. galaga.exe --fullscreen=false)
  --fullscreen=false                         start in a window
  --galaga_gpu_backend=vulkan|d3d12          graphics API (default: Vulkan)
  --resolution_scale=<1-7>                   render scale (default: chosen from the desktop resolution)
  --log_file=galaga.log --log_level=debug   write a log
  --license_mask=<n>                         licence bits (the default, 1, is the full game)
  --game_data_root=<dir or file>             use game files from another folder, or a package file
  --galaga_steady_ui=false                   keep the menus' original rapid flashing (see below)

Steady menus
  Some menu screens (mode select, mission previews, results) originally flash parts of the picture on and off
  15 times a second. This build shows those parts steadily instead, at the same average brightness, which is
  much easier on the eyes. --galaga_steady_ui=false restores the original flashing.

Achievements
  Achievements are recorded on this PC. Choose Achievements on the title screen (or press F7 at any time) to
  see them; B (Escape on the keyboard) or F7 closes the list.

Reporting a problem
  Run test-session.bat instead of galaga.exe: it writes a detailed log to the logs folder.
