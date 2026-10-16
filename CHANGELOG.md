# Changelog

## v0.2.6 — Controller fixes (prerelease)

**Downloads:** the same three setup programs; use these instead of the earlier
ones and run the setup again to rebuild the game.

### Fixed

- **Controllers not working** (#8). Only the first controller found was used;
  on Windows that is often a virtual or idle device (from Steam, DS4Windows,
  vJoy, a racing wheel...), and then nothing pressed on any controller worked.
  Every connected controller now works at once.
- **Generic USB pads** that SDL does not know the layout of were ignored. They
  are now read with the usual layout: the first buttons as A, B, X, Y, then L,
  R, Select, Start, with the D-pad and the left stick. A device whose stick
  rests at one end (some keyboards and mice report themselves as joysticks) has
  its stick ignored, so the player does not walk on their own.
- The controller now also works while the game window is not focused, as when
  it was started from the setup window.

### Tested

- On Linux with a real Xbox Series controller and virtual ones (an Xbox 360
  pad, a generic USB pad, one with a stuck stick). Compiled for Windows, not yet
  tried with a controller there.

## v0.2.5 — Widescreen battles

**Downloads:** the same three setup programs; use these instead of the earlier
ones and run the setup again to rebuild the game (and the APK, on Android).

### New

- **Battles fill the wide screen** instead of having stretched sides. The
  battle scene is drawn wider than the DS screen: the background and the
  platforms continue past its edges, the HP boxes move out to the sides of the
  view, and the wild Pokémon slides over to the right as your own Pokémon is on
  the left. The trainer throwing the Poké Ball walks all the way off screen.
- Move effects stay with the Pokémon they belong to, and the message box is
  left where it is.

### Fixed

- **The battle strip vanished for a moment** between choosing FIGHT and the
  moves appearing, showing the message box under it.

### Known limitations

- Tried on 16:9 only; wider phone screens (20:9) are untested in battles.
- A move effect crossing the gap between the two Pokémon can look stretched
  there for a moment.

## v0.2.4 — Colour fixes

### Fixed

- **Washed-out colours on Windows** (#5): the game asked SDL for its OpenGL ES
  renderer, which on Windows (seen with AMD graphics) can convert every pixel
  as if it were linear light, so the whole picture looked pale. Windows now uses
  SDL's default, Direct3D.
- **The game was always at night.** The DS clock started at midnight on
  1 January 2000 on every launch, so the overworld always had night lighting.
  It now follows the computer's clock, as on a real DS, including after loading
  a quick save.

## v0.2.3 — Android (experimental)

**Downloads:** the same three setup programs; use these instead of the earlier
ones. There is no APK to download: the Android version contains the game's code,
so it is built from your own ROM on your PC, like the PC version.

### New

- **Build for Android** in the setup program (`--android` without a window).
  It downloads Google's Android NDK and SDL2 (once, checked against their
  published checksums), compiles the game for 64-bit ARM Android 7.0+, and makes
  a signed APK to copy to the phone or handheld and install. No Java or Android
  Studio is needed: the APK is packed and signed by `tools/android/apk.py`.
- **On Android:** full screen in landscape; on-screen buttons (D-pad, A/B/X/Y,
  L/R, Start, Select, Menu), hidden while a controller is being used and back at
  a touch; fingers on the picture touch the game's touch screen, several at
  once; Back opens the pause menu; the save is written when the app goes to the
  background; messages and frame timings go to `log.txt` in the app's storage.
- **Tested in Google's Android emulator** (Android 14): installs, updates over
  itself, loads a save, plays with the on-screen buttons and touch, pause menu.
  **Not yet tried on a real phone or handheld**: reports welcome, with
  `log.txt` if something is wrong.

## v0.2.2 — Windows fixes

**Downloads:** the same three setup programs as before; use these instead of the
earlier ones. Already built? Start the new setup program and press **Build**
once: only the changed files are recompiled and your save is kept.

### Fixed

- **Windows: progress was not saved** (#2). Only the first save ever reached
  `soulsilver.sav`; later ones were left in `soulsilver.sav.tmp`, because
  Windows does not let a file be renamed over an existing one. Saves now replace
  the file. If you lost progress this way, see the note below.
- **Windows: the build stopped with "junk at end of line"** in `rc_images.S`
  (#3). The generated assembly ended with a Linux-only line.
- **Windows: "Could not find a package configuration file provided by SDL2"**
  (#3). The setup program now uses MSYS2's CMake ahead of any other copy on the
  PATH, tells CMake where MSYS2's libraries are, and says when SDL2, libpng or
  zlib are missing.
- **Screen jumping around while saving** (#1). During "Saving a lot of
  data…" the layout fell back to the X menu's (the save card beside the
  overworld) and the picture flipped between widescreen and 4:3, because the
  game hardly runs anything else while it writes. The save screen now stays as
  it is until the save is written.
- **Clearer install instructions** in the setup program (#4): it says to open a
  terminal, has a button to copy the commands, and on SteamOS (Steam Deck)
  gives the container steps, as there programs cannot be installed directly.

### Getting back lost progress on Windows (#2)

Close the game, open `%LOCALAPPDATA%\ssrecomp`, rename `soulsilver.sav` to
`soulsilver.sav.old`, then rename `soulsilver.sav.tmp` to `soulsilver.sav`. The
`.tmp` file holds your latest save.

### Tested

- The whole game cross-compiled for Windows with MinGW and run under Wine:
  identical to the Linux build frame for frame, and saving in the game replaces
  the save file. Still not tried on a real Windows PC.

## v0.2.1 — 64-bit ARM Linux

**Downloads:** `ssrecomp-setup-linux-x86_64`,
`ssrecomp-setup-linux-aarch64.AppImage` (64-bit ARM Linux: Raspberry Pi 4/5
with a 64-bit OS, other arm64 boards and laptops) and
`ssrecomp-setup-windows-x86_64.exe` are setup programs, not the game: start
one, choose your own Pokémon SoulSilver (US) ROM, press Build, then Play. Use
these instead of the v0.2.0 ones.

### New

- **Setup program for 64-bit ARM Linux**, as an AppImage
  (`tools/make_appimage.sh`). The game compiles for arm64; cross-compiled and
  run under emulation it is identical to the x86 build frame for frame. Not yet
  tried on real ARM hardware.

### Fixed

- **The game would not compile on ARM, and would crash on older x86 CPUs**: the
  widescreen renderer was always built with AVX2 instructions. They are now
  used only on x86 CPUs that have them; nothing changes on those.
- The recompiler failed when its output folder was on another drive.

### Known limitations

- Windows and ARM builds not yet tried on real hardware (Windows under Wine,
  ARM under emulation).
- See v0.2.0 and v0.1.0 for the rest.

## v0.2.0 — build from your own ROM

**Downloads:** `ssrecomp-setup-linux-x86_64` and
`ssrecomp-setup-windows-x86_64.exe` are setup programs, not the game: start
one, choose your own Pokémon SoulSilver (US) ROM, press Build, then Play. A
compiler and a few libraries are needed; the program says which and how to get
them. A ready-built game cannot be offered, as it would contain the game's code.

### New

- **Setup programs for Linux and Windows**: one file each, made from this
  release's source (`tools/package_setup.py`). They carry the project's source
  and build the game in a folder of their own.
- **Setup window** (`ssrecomp_setup.py`): pick your ROM, press Build, press
  Play. It checks the ROM is the right game, says what is missing to compile
  and how to install it, picks the number of compile jobs from your memory, and
  shows the progress. `python3 ssrecomp_setup.py --rom <file>` does the same
  without a window.
- **No decompilation needed to play.** The code is recompiled straight from
  your ROM (`tools/ndsrom.py` reads and decompresses it); what the
  decompilation used to add comes from `layouts/soulsilver-us.json`, which holds
  addresses and checksums only. The result is identical to building from the
  decompilation. `setup.sh` remains for working on the game's source.
- **Windows (untested):** the code compiles with MinGW, and the setup window
  supports MSYS2 and copies the needed DLLs next to the game. Not yet tried on
  a Windows machine.

### Known limitations

- Tested on Linux only (Ubuntu 24.04 in a container on Bazzite).
- Only Pokémon SoulSilver (US).
- See v0.1.0 for the rest.

## v0.1.0 — first preview

The first public version. Playable from the start of a new game through the
early routes, towns and gyms; the rest of the game has not been played through
with it yet.

**No ready-made download.** A built program would contain the game's code, so
releases are source only: run `./setup.sh` with your own copy of Pokémon
SoulSilver (US) as described in the README.

### What's in it

- **Static recompilation** of the game's ARM9 code from pret/pokeheartgold's
  build output, running on the melonDS core. Checked against melonDS's
  interpreter instruction by instruction (`--lockstep`).
- **One screen, no switching.** Each situation has its own layout that takes the
  useful parts of both DS screens: the overworld with dialogue, the X menu beside
  the overworld, the bag, party, Pokédex, Pokégear, PC, shops, the intro and the
  starter choice.
- **Widescreen overworld:** the 3D world is drawn across the whole 16:9 view at
  a higher resolution (540–1440 lines, "WORLD DETAIL" in the pause menu); trees
  and buildings at the sides are drawn instead of popping in.
- **Battles across the whole screen**, with the touch-screen buttons as a strip
  along the bottom: FIGHT / BAG / POKéMON / RUN and the four moves, chosen with
  left and right, A to pick, B to go back.
- **60 fps:** in-between frames for the 3D scene, which the game updates 30
  times a second.
- **Controller first:** everything is reachable with buttons; a pause menu (Guide
  button or Esc) holds the settings, save states and fast forward.
- **Runs well:** about 9 ms of a 16.7 ms frame at 2560×1440 on a 6-core desktop
  CPU; the picture is composed on the graphics card.
- **`setup.sh`** installs what is missing, builds the game with the
  decompilation, checks it against the retail ROM's SHA-1 and compiles
  everything in one step.

### Known limitations

- Linux only so far (tested on Ubuntu 24.04 in a container on Bazzite).
  Windows, macOS and Android builds do not exist yet.
- Only Pokémon SoulSilver (US). HeartGold and other regions are not supported.
- Later parts of the game, and some screens (radio, contests, the Pokéathlon,
  wireless features), have not been checked and may show a generic two-screen
  layout.
- The widescreen picture has no edge-marking outlines; battles, menus and the
  intro stay 4:3 apart from the battle sides.
- The recompiled code is not yet faster than melonDS's own interpreter; making
  it so is future work.
