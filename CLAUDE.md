# Galaga Legions DX (Xbox 360 XBLA) - static recompilation

Hobby project: native PC port of Galaga Legions DX, built on the ReXGlue SDK (github.com/rexglue/rexglue-sdk,
BSD-3, Xenia-based: kernel/XAM, D3D12/Vulkan GPU, XMA audio, VFS, input). Windows, VS 2026 (v18), Clang 22.
Started 2026-10-22 as a copy of the finished Pac-Man CE port (same publisher/era, same toolchain). The Pac-Man code
and notes (rexglue/src/*.cpp, CLAUDE.md) are at commit 47d1596 of github.com/jedeyesniv-art/Pacman-CE-PC-Recomp,
and locally on branch pacman-history (`git show 47d1596:<path>`); this repo's published history starts fresh.
Game data comes from the owner's own console dump and is never committed or shared.
Goal: runs well and light (Pac-Man ended at ~7% CPU, locked 60 fps, also on the Steam Deck).

## Status
- 2026-10-22: RELEASED 1.0.0 (owner: "perfect play" on PC and Steam Deck). Public repo
  github.com/jedeyesniv-art/Galaga-Legions-DX-PC-Recomp (main pushed with fresh history; the full local history,
  Pac-Man included, is on the unpushed branch pacman-history), release v1.0.0 with the windows + symbols zips and
  the LGPL source archives (copied from the Pac-Man release: same SDK commit, same FFmpeg/libmspack revisions).
  Release zip tested unpacked fresh with only the STFS package in game\. To release again: bump CMake VERSION,
  package_portable.ps1 -Release, gh release create (dist\release-sources\ must hold the two archives).
- 2026-10-22: BOOTS AND RUNS: title menu renders correctly, attract demo plays (screenshots at 20/35 s), closes
  cleanly, no errors in the log; ~1.3 cores of CPU in the menu/demo (no spin-wait hooks yet). Not yet tested:
  real gameplay with a pad, audio, saves. Two fixes got it there (galaga_config.toml):
  1. setjmp/longjmp (0x821B0E70 / 0x821B0B50, MSVC CRT, found by Pac-Man's code pattern). Symptom: guest read of
     0x1A4 in sub_82175D78 (libjpeg read_markers in D3DX, cinfo null) after opening data\texture - a libjpeg
     error longjmp that didn't unwind.
  2. 27 vtable-only function starts without .pdata. Symptom: "[FATAL] Call to invalid or unregistered function
     at guest address 0x821C1B60". Found all at once by scanning .rdata for unregistered code pointers after a
     terminator; SEH scope tables (0x8205FF20..0x820603E0) and inline jump tables in leaf functions excluded.
  Expected log noise: NtCreateFile failures for data\config.ini, data\effect.txt, data\DemoData\Demo_MN.bin.
- 2026-10-22: owner played it: "felt and looked perfect", low CPU/GPU. Then the Pac-Man tooling and fixes were
  ported (see "Hooks" below). Measured (attract demo, 70 s): steady windows 60.0 fps with 0 slow frames; the only
  slow frames are startup and a ~180 ms scene load (game code busy, no emulator wait); no guest thread spins
  (busiest thread 5-9% of a core: no job-worker hooks needed, unlike Pac-Man); 1-3 access faults per frame;
  0 buffer-full waits. Per frame: swap -> fence wait (site 11, sub_8207C778) until the vblank -> the command-buffer
  interrupt ~16 ms after VdSwap (Pac-Man ~1-2 ms; frame pacing is fine, but worth a look if input latency feels off).
- - Debugging a crash: build with debug info (package_portable.ps1's flags), the dump lands in
  %LOCALAPPDATA%\CrashDumps; parse it (exception + stack scan for galaga.exe addresses), llvm-symbolizer
  --obj=galaga.exe 0x140000000+RVA gives sub_XXXXXXXX names. The PPCContext was not in the dump.

## Layout (C:\GalagaRecomp)
- Private\                  game data (git-ignored as private/): the owner's STFS package in
                            stfs\58410AF9\000D0000\, unpacked with Tools\stfs_extract.py (default.xex, data\, geui\,
                            xb360\, XBLA metadata), plus default.exe = PE basefile (xextool -b; ImageBase 0x82000000).
- rexglue\                  the ReXGlue project
    galaga_manifest.toml    project + entrypoint; includes galaga_config.toml
    galaga_config.toml      codegen config (setjmp/longjmp, function boundaries, mid-asm hooks) - empty so far
    src\galaga_app.h        ReXApp subclass: run defaults (see below)
    src\game_files.cpp      startup checks (game data present / right title) and save backups
    generated\rexglue.cmake SDK integration (tracked); generated\default\ = codegen output (ignored)
    out\                    build output (ignored)
- Tools\rexglue-sdk-src\    SDK source at nightly commit bd833a2a, branch "pacman" = + our patches
                            (patches\rexglue-sdk\, 0001-0010). THE SDK WE BUILD WITH. Its out\build\win-amd64 dir was
                            reconfigured here 2026-10-22 (install prefix C:/GalagaRecomp/Tools/rexglue-sdk-vk/win-amd64;
                            the command is under "SDK patches"); rebuild + install takes ~10 min.
- Tools\rexglue-sdk-vk\win-amd64\  its install (ignored; relocatable, only pkgconfig files name the old path).
- Tools\rexglue-sdk\win-amd64\  the prebuilt nightly 0.10.0.24: its bin\rexglue.exe CLI is the same version.
- Tools\XeXtool\            xorloser's xextool 6.3 (-l info, -b basefile).
- Tools\stfs_extract.py     unpack an STFS package (read-only packages; follows hash chains): <pkg> <out> [--list]
- Tools\ppcinspect.py       disassembler helper (capstone): modes fn / dis / words, on Private\default.exe
- Tools\package_portable.ps1  portable folder dist\GalagaLegionsDX\ (+ -Release zips); adapted from Pac-Man, untested.
- _pacman_leftovers\        (git-ignored) Pac-Man build outputs/logs moved aside during cleanup; owner may delete.

## Build environment
Scripted builds run inside vcvars64 (not Developer PowerShell - it defaults to x86):
- vcvars64.bat needs "%ProgramFiles(x86)%\Microsoft Visual Studio\Installer" (vswhere) on PATH. It prints
  VsDevCmd errors but the x64 env still works, so chain commands with "&", not "&&".
- In a one-line cmd /c chain %ERRORLEVEL% is expanded at parse time - don't trust it.
- The ReXGlue presets use clang/clang++ (GNU driver): put VS's LLVM bin on PATH,
  C:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\Llvm\x64\bin
- From Git Bash, prefix MSYS_NO_PATHCONV=1 to anything taking /flags, or they get mangled into paths.
- A failed configure (e.g. outside vcvars64: "CMAKE_RC_COMPILER not set") leaves the build dir broken - re-run the
  configure inside vcvars64. Switching a build dir to another SDK: configure with --fresh.

## Build and run
From rexglue\ (configure once):
  cmake --preset win-amd64-release -DCMAKE_PREFIX_PATH=C:/GalagaRecomp/Tools/rexglue-sdk-vk/win-amd64
  cmake --build --preset win-amd64-release        (codegen runs as part of the build)
Codegen alone (fast check of config changes): ..\Tools\rexglue-sdk\win-amd64\bin\rexglue.exe codegen galaga_manifest.toml
Run: rexglue\out\build\win-amd64-release\galaga.exe (no arguments needed; finds ..\..\..\..\Private)
  Useful options: --log_file=<path> --log_level=debug|trace (trace logs VFS opens), --fullscreen=false,
  --resolution_scale=1, --game_data_root=<dir or STFS package file>, --license_mask=<n>
Testing without the owner's screen: capture only the game window with PrintWindow (PW_RENDERFULLCONTENT,
DPI-aware process) at --resolution_scale=1 - never CopyFromScreen.

## Steady menus (src\steady_ui.cpp, --galaga_steady_ui, default on; owner's request 2026-10-22)
Owner: "strobing uncomfortably, very rapid" on the mode select / mission preview / result screens. Same in Xenia
Canary (measured), on Vulkan and D3D12, 1x and 3x, streaming blocks on/off: it is authored. Source: the shared
menu skin geui\XB360\Skin.geui (XUI-style binary layout) has looping timelines whose opacity keyframes alternate
every 2 frames: 1.0/0.5 (an additive glow over the preview, atlas 0x1D98B000: drawn alpha 0x81..0xFD) and full
0/1 blinks. Keyframe record: 00 01 | frame u16 | 00 00 | prop count 1-2 | 04 | opacity BE float | ...; a
timeline's records sit at a fixed stride. The app's file-read filter flattens every run of >8 keyframes <= 2
frames apart with opacity jumps > 0.2 to the run's average: 14 timelines (407 keyframes) in Skin.geui; fade-ins
and the short 1.0/0.3 blinks at frames 160-178 (3-frame steps) are kept. Verified with a per-draw probe: the glow
then holds alpha 0xB9. Left: a mild 4-frame shimmer (alpha 0xCE-0xFA) on one draw from atlas 0x129C3000 - not
from .geui opacity keys. Needs whole-file reads (Skin.geui is read at once) and SDK patch 0010 for package reads.
How it was found (reusable): the draw filter (SDK patch 0008) as a probe - per draw: texture fetch constant 0
(regs 0x4800-0x4805: base 0x4801 & ~0xFFF, size in 0x4802), blend 0x2201, surface 0x2000, vertex fetch constant 95
(reg 0x48BE: the sprite vertex buffer; vertex = x y z ARGB u v ...); group per frame and diff the alphas.
rand() is sub_8213EE50 (MSVC LCG; called once at boot - not used for effects).
Xenia Canary (via Xenia Manager in the owner's Downloads folder) runs the STFS package directly; --keyboard_mode=1
enables keys (A = ';', Start = X): a handy reference renderer.
Driving menus in tests: WScript.Shell AppActivate + SendKeys(" ") (Space = A in our keyboard binds); 3 presses
from boot (title, main menu "Play Game") reach Select Mode with the area preview.

## Championship Score Attack Tournament (main menu, 2nd item) - Live-only, left as is (2026-10-22)
Selecting it shows the game's own "must be signed into a gamer profile and connected to Xbox LIVE". Texts:
"Participate in Score Attack Tournament", "Score Attack Tournament Leaderboard", main-menu EventBoard "Entry
Ranking". The gate: XUserGetSigninState (thunk 0x8215A520) must be 2 (the SDK hard-codes 1, user_profile.h) and
XUserCheckPrivilege (sub_8215A530: fails with 0x4DD if state 1, else XamUserCheckPrivilege, which the SDK denies).
Experiment (reverted): hooks forcing both -> the mode at once calls XGIUserSetContextEx(0, 0x800A, 1) and
XUserReadStats (the tournament leaderboard) and crashes on a null read with no server reply. It is built around the
online event; offline play would need faked tournament data. Owner informed; not pursued.

## Testing with logs
rexglue\test-session.bat (also copied into the portable folder; not in the release zip): starts the game with
--galaga_frame_trace and --galaga_profile and writes rexglue\logs\galaga-<date_time>.log (git-ignored). Extra
arguments pass through. What the log has:
- every 10 s "frames: ..." (src\frame_stats.cpp, on by default, --galaga_frame_stats): fps, frames over 20 ms /
  under 10 ms, worst, "game behind by" (time lost: the game steps once per frame), vblank/command-buffer interrupt
  timing, buffer-full and fence waits, main-thread GPU-wait share, access faults, busiest threads.
- "frame trace" lines around each slow frame (max 80): V vblank, S VdSwap, C command-buffer irq, ( ) fence wait,
  [ ] buffer-full wait, Fn fence-wait call site n (frame_stats.cpp lists them), <[a,b,c] GPU wait with callers.
- "profile [...]" every 10 s: per thread (main, GPU commands, UI) leaf-module shares and hot spots as
  module+RVA; symbolize with llvm-symbolizer --obj=galaga.exe 0x140000000+RVA (needs that build's PDB).
- errors from diagnostics.cpp: any call to KeBugCheck(Ex) / XamLoaderTerminateTitle / RtlRaiseException
  (ExTerminateThread = normal thread end, info), unhandled host exceptions with a module+offset stack,
  "game main thread ended (...)" saying whether the window was closed.

## Hooks (galaga_config.toml; code ported from Pac-Man, addresses found for Galaga 2026-10-22)
D3D (XDK 2.0.20500; same code as Pac-Man's 4929 here, device fields +0x100: +0x2A10 -> +0x2B10):
- GPU wait poll sub_82087030 (low-priority-hint delay loop, then checks the GPU) -> GpuWaitPollHook at 0x82087078:
  spin briefly, then 250 us high-resolution sleeps (--galaga_gpu_wait_backoff).
- fence wait sub_8207C3D0 (slow path 0x8207C44C..0x8207C48C), command-buffer-full wait sub_8207BC88
  (0x8207BCC8..0x8207BD10): timed for frame stats. 14 fence-wait call sites hooked for the trace.
- ring-buffer setup sub_8207D750 (VdInitializeRingBuffer caller): CommandBufferSizeHook at 0x8207D85C (r28 size,
  r11 segments) -> 8 MB / 512 KB segments (--galaga_command_buffer_mb / --galaga_command_segment_kb).
- graphics interrupt callback sub_8207BFA8 (InterruptHook at entry); the only VdSwap call 0x82075290 in
  sub_82074FD0 (FrameHook).
Game:
- Achievements menu: sub_8215A858 (li r4,0; b XamShowAchievementsUI) -> AchievementsUiHook opens the F7 overlay
  (the game calls it several times while A is held; opening is idempotent). B closes it: AchievementsInputHook at
  0x820B7BA0, after the game's only pad read (XInputGetState wrapper sub_82159DA8 -> XamInputGetState, no
  keystroke reads; r27 user, r3 result, XINPUT_STATE at r1+0xA0). While open the game sees no buttons (like the
  console's system UI) and stays blanked until all buttons are released, so the closing B doesn't reach the menu.
  Tested 2026-10-22 with keys: menu -> overlay -> Escape -> back on the menu, Achievements still selected.
- Stop-function call sites (diagnostics): listed in galaga_config.toml / diagnostics.cpp, from a scan of every
  b/bl to the import thunks (generated code's ctx.lr misattributes tail calls - scan the binary).
How they were found: the SMT hint "mr r31,r31" (0x7FFFFB78) run marks the GPU poll; its callers are the waits;
the "lis rX,0x20" default 2 MB marks the ring setup; import thunk addresses from galaga_init.cpp.

## Run defaults (src\galaga_app.h) - each one was a lesson from Pac-Man
- Game data, first match: game\ next to the exe (unpacked, or the STFS package up to 3 folders deep, title ID
  58410AF9 at 0x360; mounted by SDK patch 0009), the exe's folder, the nearest private\ searching up.
- userdata\ next to the exe (saves + shader cache), fallback Documents\galaga. Saves backed up at startup.
- GPU: gpu_plugin xenos, Vulkan backend by default (--galaga_gpu_backend=d3d12|any). OnPreSetup loads the plugin
  itself so plugin cvars (draw_resolution_scale_x/_y, shared_memory_streaming_blocks, gpu_wait_reg_mem_sleep_us)
  exist and can be set with SetFlagByName (REXCVAR_DECLARE on plugin cvars fails to link).
- Render scale from the desktop (largest 16:9 area / 1280x720, rounded up). present_letterbox, fullscreen on,
  F11/Alt+Enter toggle (SDL re-sends the held key on a mode switch: ignore toggles within 500 ms).
- host_present_from_non_ui_thread = true and NO achievement toast dialog (CreateAchievementNotificationDialog
  returns nullptr). Pac-Man's big slowdown: any ImGui dialog makes the presenter paint on the UI thread at the
  display rate, and the game's swap then waits for it -> heavy frames miss vblank ("lag storms").
- license_mask = 1 (full game, not the XBLA trial).
- Keyboard: SDK MnkInputDriver turned on, every keybind_* set (SDK defaults are messy). Galaga's control set
  still to be decided (TODO in ApplyKeyboardControls).
- OnShutdown shuts input down while the window exists (SDK bug: heap corruption 0xC0000374 on exits not via the
  close button; docs\rexglue-issue-exit-crash.md).

## Game facts (from xextool / first codegen)
- Title ID 58410AF9, media ID 56C059CA, version 0.0.0.2, built 2011-05-02. XDK 2.0.20500 (D3D9, XAudio2,
  XMCORE, XONLINE). Image 0x82000000-0x82B80000 (resource 0x82B50000), code 0x82070000-0x8220A668,
  entry 0x82070A58. Big data section (0x82210000+).
- Save/restore helpers: savegprlr_14 0x8213BDC0 .. restgprlr_31 0x8213BE54; savefpr_14 0x8213D520 ..
  restfpr_31 0x8213D5B0; savevmx/restvmx 0x821B0620..
- D3D interrupt callback sub_8207BFA8 (context 0x40002000).
- Package: 846 entries; data\ (gamedata.bin 6.4 MB, gamedatabin.bin, fonts *.abc/png per region, DemoData\,
  texture\ 610 PNGs, *.xpr), geui\, xb360\.

## Bring-up checklist (what Pac-Man needed; redo the address-specific parts for Galaga)
1. Codegen config: after codegen, check for "has no blocks - generating stub" and analysis errors
   (UnresolvedCall). setjmp/longjmp (Pac-Man: MSVC CRT, setjmp_address/longjmp_address in config). Missing
   function starts (Pac-Man: 74 vtable-only methods without .pdata, found by diffing against XenonRecomp's list;
   beware SEH scope records mistaken for functions). [functions] keyed by hex address, { size = } or {}.
2. Get it running: fix crashes, missing/stubbed imports. diagnostics hooks on KeBugCheck(Ex),
   XamLoaderTerminateTitle, RtlRaiseException call sites help explain silent exits (Pac-Man diagnostics.cpp).
3. Performance - DONE for Galaga except as noted (see Hooks). Pac-Man's list, for reference:
   - job worker threads polling a control word with no delay, and D3D's GPU-wait poll loop -> mid-asm hooks that
     _mm_pause then sleep 250 us on a high-resolution waitable timer (Pac-Man 35% -> 7% CPU).
   - D3DRING_BUFFER_PARAMETERS at D3D device setup: bigger command buffer / segments (Pac-Man 8 MB, 512 KB
     segments) - mid-frame segment starts waited for the next vblank.
   - frame stats hooked on the game's VdSwap call (fps, frames > 20 ms, "game behind by").
   - deferred resource destroy if the game frees textures its in-flight frame uses.
   - build: -march=x86-64-v3 -ffp-contract=off on <name>_recomp (already in CMakeLists).
4. Presentation quirks (Pac-Man's playfield offset was a game-specific fix), controls, achievements menu
   (XamShowAchievementsUI is an SDK stub - Pac-Man hooked the game's wrapper to open the F7 overlay).
5. Release: package_portable.ps1, licences (rexglue\licenses\THIRD-PARTY-NOTICES.txt), LGPL source archives
   (FFmpeg, libmspack via git archive at the SDK's submodule revisions), leak check for game files, an original
   exe icon (no game artwork).

## SDK patches (patches\rexglue-sdk\, see its README)
0001 shared_memory_streaming_blocks, 0002 gpu_wait_reg_mem_sleep_us, 0003 input device refresh throttle,
0004 AllocRange free-page hints, 0005 SDL pad instance-id cache, 0006 streaming blocks with scaled resolve,
0007 file read filter (SetFileReadFilter), 0008 draw register filter (SetDrawRegisterFilter),
0009 STFS package as game_data_root, 0010 read filter for package files too. Re-apply to a fresh clone:
git -C Tools\rexglue-sdk-src am ..\..\patches\rexglue-sdk\*.patch (plus libmspack's symlinks replaced by copies).
SDK Vulkan build (inside vcvars64): cmake --preset win-amd64 -DREXGLUE_USE_VULKAN=ON -DCMAKE_INSTALL_PREFIX=...
plus "-DCMAKE_CXX_FLAGS_RELEASE=-O3 -DNDEBUG -gline-tables-only -gcodeview" (same for C) and
"-DCMAKE_SHARED_LINKER_FLAGS_RELEASE=-Wl,-debug -Wl,-opt:ref -Wl,-opt:icf" (same for EXE), then
cmake --build out/build/win-amd64 --config Release --target install --parallel.

## General lessons (from Pac-Man)
- Guest memory at 0xE0000000+ is mapped with an extra host offset: use memory()->TranslateVirtual, not
  membase + address.
- --vsync is guest vblank emulation (false = double game speed), not host vsync.
- The relwithdebinfo preset is very slow at loading; use Release for playing and timing.
- Windows writes crash dumps silently to %LOCALAPPDATA%\CrashDumps; symbolize with llvm-symbolizer + the PDBs
  (SDK PDBs in Tools\rexglue-sdk-src\out\win-amd64\Release).
- Expected log noise: missing SDL gamecontrollerdb.txt; NtCreateFile failures for optional language folders.
- Measure performance with A/B runs of two builds side by side; the owner's feel of gameplay is the final test.
