# GTA 2 Recompiled — milestones

Targets: Windows, Linux, Android. Renderer: Vulkan 1.1 baseline. Toolkit: winrecomp
(own toolkit for Win32 PE games; x86 lifter taken from xboxrecomp option-c; no Ghidra).

## M0 — Groundwork (DONE 2026-11-25)
- [x] Unpack the installer (InstallShield overlay -> data1.hdr/data2.cab -> unshield)
- [x] Analyse gta2.exe: imports, renderer DLLs d3ddll.dll (gbh_*) + Dmavideo.dll (Vid_*)
- [x] Create the winrecomp toolkit: lifter from option-c, Win32 PE front end
- [x] Full lift: 25,390 functions, 0 failed, 32 unresolved

## M1 — Clean lift
- [ ] Clear out data decoded as code (arpl/iretd/outsb hits), write gta2 game.toml
- [ ] Resolve the 32 unresolved indirect targets, set up icall feedback
- [ ] Mark jump tables / switch tables, check SEH/CRT parts (startup code, _initterm table at .data 0x57c00c)
- [ ] Gen into gta2_recomp/gen, stable partitions

## M2 — Runtime core (winrecomp/runtime)
- [ ] Guest memory: map the PE image at 0x400000 (.text/.rdata/.data/.bss), heap, stack
- [ ] Import binding: IAT slots -> native handlers (216 imports, DLL!Func table)
- [ ] Dispatcher for indirect calls / callbacks (WndProc, threads, timers)
- [ ] CMake build of the generated C (GCC/Clang on Linux, MSVC/clang-cl on Windows, NDK)
- [ ] First start up to WinMain, log + crash report with guest address

## M3 — Win32 API layer (on SDL3)
- [ ] KERNEL32: files (paths, case-insensitive), memory, threads/sync, time, ini/registry
- [ ] USER32/GDI32: window, message loop, WndProc, cursor, MessageBox
- [ ] ADVAPI32 (registry -> ini), SHELL32, ole32, VERSION, WINMM (timeGetTime, timers)
- [ ] Settings that gta2 manager.exe writes -> our own settings menu

## M4 — Renderer Vulkan 1.1
- [ ] Native replacement for Dmavideo.dll (Vid_*: mode, flip, surfaces, gamma)
- [ ] Native replacement for d3ddll.dll (gbh_*: textures, palettes, polygons, lights, DrawLine)
- [ ] Vulkan 1.1 backend (swapchain, pipelines, texture cache), no 1.2+ dependency
- [ ] Offscreen game image at RENDER RESOLUTION, final pass to the swapchain (ASPECT RATIO 4:3/16:9 as option-c/p)
- [ ] Slang shaders (RetroArch) via librashader, shader packs from media/shaders (option-c feature)
- [ ] V-Sync, FRAME RATE 30/60/120 (PERFORMANCE sub-menu)
- [ ] Android: landscape, preTransform identity

## M5 — Audio, video, input
- [ ] Miles (mss32): samples, streams, 3D sound, MSSDS3DH -> SDL3 audio
- [ ] Bink (binkw32): intros/movies (decoder or skip)
- [ ] DirectInput 8: keyboard, gamepad (SDL3), controller layout
- [ ] Android: touch controls

## M6 — Playable on Linux
- [ ] Frontend/menu, missions load, saves (POSIX-safe writes)
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
