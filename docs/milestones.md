# GTA 2 Recompiled — milestones

Targets: Windows, Linux, Android. Renderer: Vulkan 1.1 baseline. Toolkit: winrecomp
(own toolkit for Win32 PE games; x86 lifter taken from xboxrecomp option-c; no Ghidra).

## M0 — Groundwork (DONE 2026-11-25)
- [x] Unpack the installer (InstallShield overlay -> data1.hdr/data2.cab -> unshield)
- [x] Analyse gta2.exe: imports, renderer DLLs d3ddll.dll (gbh_*) + Dmavideo.dll (Vid_*)
- [x] Create the winrecomp toolkit: lifter from option-c, Win32 PE front end
- [x] Full lift: 25,390 functions, 0 failed, 32 unresolved

## M1 — Clean lift
- [x] Clear out data decoded as code (arpl/iretd/outsb hits), write gta2 game.toml
- [x] Resolve the 32 unresolved indirect targets, set up icall feedback
- [x] Mark jump tables / switch tables, check SEH/CRT parts (startup code, _initterm table at .data 0x57c00c)
- [x] Gen into gta2_recomp/gen, stable partitions

## M2 — Runtime core (winrecomp/runtime)
- Note: the CMake build is Linux-tested; Windows (M7) and NDK (M8) builds come with those milestones
- [x] Guest memory: map the PE image at 0x400000 (.text/.rdata/.data/.bss), heap, stack
- [x] Import binding: IAT slots -> native handlers (216 imports, DLL!Func table)
- [x] Dispatcher for indirect calls / callbacks (WndProc, threads, timers)
- [ ] CMake build of the generated C (GCC/Clang on Linux, MSVC/clang-cl on Windows, NDK)
- [x] First start up to WinMain, log + crash report with guest address

## M3 — Win32 API layer (on SDL3)
- [x] KERNEL32: files (paths, case-insensitive), memory, threads/sync, time, ini/registry
- [x] USER32/GDI32: window, message loop, WndProc, cursor, MessageBox
- [x] ADVAPI32 (registry -> ini), SHELL32, ole32, VERSION, WINMM (timeGetTime, timers)
- [ ] Settings that gta2 manager.exe writes -> our own settings menu

## M4 — Renderer Vulkan 1.1
- Status 2026-11-26: main menu + in-game render correctly (HUD, lighting, sprites); original runs at 30 FPS
- [x] Native replacement for Dmavideo.dll (Vid_*: mode, flip, surfaces, gamma)
- [x] Native replacement for d3ddll.dll (gbh_*: textures, palettes, polygons, lights, DrawLine)
- [x] Vulkan 1.1 backend (swapchain, pipelines, texture cache), no 1.2+ dependency
- [x] Offscreen game image at RENDER RESOLUTION, final pass to the swapchain (ASPECT RATIO 4:3/16:9/16:10/21:9 as option-c/p: real wider view of the city, never stretched; frontend pillarboxed)
- [x] Slang shaders (RetroArch) via librashader (built from source, Vulkan only); shader packs + menu with M6
- [x] Real widescreen 16:9/16:10/21:9 (Hor+, all cameras, HUD anchored; 4:3 byte-identical to the original); verify pager/mission texts + fast-car zoom-out in M6
- [x] No busy-wait: menu + game loop sleep to their deadline (100 % -> ~3 % of a core), timing exact (30.3 Hz)
- [x] V-Sync (WR_VSYNC; menu line with M6)
- [ ] FRAME RATE 60/120: moved behind M6 (own item below) - logic is frame-locked at 30 Hz, needs interpolated extra frames
- [ ] Android: landscape, preTransform identity

## M4b — FRAME RATE 60/120 (after M6)
- [ ] Keep the 30 Hz logic tick; render interpolated frames in between (camera + object positions/angles lerped, restored after the draw)
- [ ] Make the render path re-runnable within a tick (sprite lists are cleared after each draw, 0x004C4D6E)
- [ ] Per-tick animations / HUD stay per tick; FRAME RATE line in the PERFORMANCE sub-menu

## M5 — Audio, video, input
- [ ] Miles (mss32): samples, streams, 3D sound, MSSDS3DH -> SDL3 audio
- [ ] Bink (binkw32): intros/movies (decoder or skip)
- [ ] DirectInput 8: keyboard, gamepad (SDL3), controller layout
- [ ] Android: touch controls

## M6 — Playable on Linux
- [ ] Frontend/menu, missions load, saves (POSIX-safe writes)
- [ ] RECOMP OPTIONS and CHEATS lines in the game's own options menu (CHEATS: the game's cheats as ON/OFF switches, one-shot cheats as actions; maintainer 2026-11-26)
- [ ] Play through all three districts, fix crashes
- [ ] Performance: constant 60 FPS, game logic speed correct

## M7 — Windows build (PAUSED: done on a Windows PC)
- [ ] MSVC/clang build, Win95 launcher, settings menu (option-p RECOMP layout)
- [ ] Test on Windows

## M8 — Android build
- [ ] APK (ARM64), Vista launcher (Android standard), game data import
- [ ] Back menu in the fixed order, graphics driver (Turnip) row
- [ ] Test on the Odin 2

## After M8 (maintainer 2026-11-26)
- [ ] Own milestone: complete translation of ALL story content and UI (game texts + our menus/launcher) into EN, FR, DE, IT, ES, JA, ZH, RU, PL; own text renderer + font (Latin-Ext, Cyrillic, CJK); shipped as a translations pak next to the launcher (like ZOE-subtitles.pak); LANGUAGE in the RECOMP menu
- [ ] Controller rumble: explosions make the controller rumble
- [ ] Custom soundtracks

## M9 — Multiplayer (optional)
- [ ] DirectPlay (DPLAYX) -> own network layer (LAN/IP)

## M10 — Release
- [ ] Repos (main + -Android), README, AI note, .recomp.json, recomplauncher.json
- [ ] Linux AppImage (Win2000 launcher), Windows zip, Android APK
- [ ] Release notes per version, link to the Android version
