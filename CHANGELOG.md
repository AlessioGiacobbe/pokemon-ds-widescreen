# Changelog

## Unreleased

- **Pokémon White (English, US/Europe).** The same program and layouts as Black: White is the
  same game with its code a few bytes apart, so its function table is Black's at White's
  addresses (`tools/bw1/white_shifts.txt`, found by `tools/bw1/match_white.py`;
  `tools/bw1/funcmap.py --white` makes the table). On the Switch it gets `white.sav`, and the
  messages for a missing or unknown game list it.
- **Black: double, triple and rotation battles laid out for the wide view.** The foes' HP boxes
  stay down the left edge of the view and yours down the right edge, where Black draws them
  (they were cut by the single battle's places), and the battle buttons sit in the message
  box's band so they don't cover your boxes. Choosing a move's target shows the target buttons
  there too (it showed none). Black's table now also knows the target menu, the level-up
  window and the evolution scene by Black 2's names. Headless tests: `--state-at` counts from
  the loaded state under `--relative`, like the other frame options.
- **SoulSilver and HeartGold (English, US) as a download, on the fast JIT.** The program holds
  none of the games' code: its tables have addresses only, and the overlay code the game
  watch compares is read from your ROM when the game starts. HeartGold is told apart by its
  game code; its few moved functions come from `tools/soulsilver/heartgold-us.json`. All of
  SoulSilver's layouts work without the recompiled code, about 1.7 times as fast. The new
  game's first menu is skipped again (also in HeartGold). Battles' widened picture is made
  beside the next frame's emulation. A bot for long test runs: `tools/soulsilver/bot/`.
  On the Switch, HeartGold gets `heartgold.sav` and the JIT like SoulSilver, and the message
  for a game the program doesn't know lists both.
- **The bars beside an evolution or a battle take the screen's outermost colour that isn't
  black** (from its last four columns), so a scene that leaves a black column at the screen's
  edge (SoulSilver's evolution) no longer gets a black bar on that side.
- **3D drawing commands are no longer lost (Black 2, White 2 and the other games on the fast
  JIT).** When the console's 3D command queue was full, a burst of commands from the fast JIT
  could overflow the small overflow store melonDS keeps, and from then on every command read
  its neighbour's numbers. Fixed: White 2's overworld not drawn after a trainer battle (so
  White 2 now has the wider map too), triple and double battles' backgrounds going black for
  a few seconds after a Pokémon faints, and the summary screen's Pokémon sometimes missing.
  Save states made before keep working and fall back in step at once.
- **Black 2 and White 2: double, triple and rotation battles' HP boxes against the edges of the
  wide view,** the foes' in the upper left and yours in the lower right, as in single battles
  (they kept the DS's places in the middle). The boxes stay put while the screen goes black
  into an evolution, instead of the next scene showing through where they were.
- **Switch: a shorter pause menu.** The game always shows the one wide screen, so SCREEN LAYOUT
  and LARGE SCREEN are gone (an older saved layout is ignored). CPU BOOST is gone: the system
  kept taking its boost mode away and the game dropped frames; sys-clk sets the clocks
  instead (see README). EMULATION is gone: FAST JIT always, still falling back by itself for
  one start if a start with it fails, and the interpreter while L is held. BUTTONS, which
  had nothing to choose on the Switch, is gone too.
- **Pokemon White 2 (US/Europe, English):** plays like Black 2, with the same layouts, wide
  view and battle screens (its function table comes from the pokebw2 decompilation's White 2
  symbols), and the wider map.
- **Switch, Black, Black 2 and White 2 start at WORLD DETAIL 720 with 60 FPS on** (with WORLD
  ON GPU, about 16 ms a frame), instead of 288 lines and 60 FPS off, which suited the world
  drawn by the processor. Settings you have already changed are kept.
- **Switch, Black 2 and White 2 without building:** the program can carry the function tables
  for Black 2, White 2 and Black itself and read the few bytes it needs from your ROM when
  the game starts, so one ready-made `.nro` plays any English copy of those games
  (`tools/switch/build_game.sh bw`). A ROM it doesn't know (another language) gets a message
  saying which versions it plays.
- **Switch: one command per game.** `tools/switch/build_game.sh soulsilver|black2|black
  your-rom.nds` does every step once the tools are installed: the function tables, the
  build (with devkitPro's Docker image when devkitPro is not installed) and a folder laid out
  like the SD card (see README, "Nintendo Switch").
- **SoulSilver on the Switch's JIT:** built without the recompiled code
  (`tools/soulsilver/tables_only.py`), it runs on melonDS's JIT, about 1.7 times as fast on
  the Switch, and builds in minutes; its layouts learn which functions and overlays run from
  the emulator instead. In a wide battle the bottom band shows the message box on its left
  and the touch screen's buttons on its right.

- **Nintendo Switch (experimental):** a homebrew `.nro` for consoles with custom
  firmware, built on the PC from your ROM with `tools/switch/build.sh` (see README,
  "Nintendo Switch"). The pause menu's FULLSCREEN is CPU BOOST there, the
  widescreen world starts at 1.5 times the DS's sharpness (WORLD DETAIL 288) and
  60 FPS starts off. The buttons follow the Switch's own labels (A is the right
  button). Quitting saves and closes cleanly (each step is written to `game.log`).
  CPU BOOST now sets the speeds itself, as sys-clk does, instead of asking for the
  system's boost mode (which the system took away for 10-20 seconds every minute or
  so): processor 1785 MHz, memory 1600 MHz (its docked speed), checked five times a
  second, and put back to normal when it is turned off or the game ends.
  Games without recompiled code (the experimental Black 2 build) run on melonDS's
  JIT on the Switch (EMULATION in the pause menu; hold L while starting for the
  interpreter, which is also used by itself after a start where the JIT failed).

- **WORLD ON GPU (Switch, pause menu):** the widescreen world drawn by the Switch's
  graphics chip through deko3D, its own graphics API, instead of the processor, into the
  same picture. On the Skyarrow Bridge at WORLD DETAIL 720 a frame went from about 34 ms
  (30 fps) to about 16 ms (60 fps). On by default; if the game does not get 600 frames
  into it, it is off at the next start. With it, the DS's own 3D picture is drawn by the graphics chip
  too (`ds3d_gpu=0` keeps that on the processor), and the texture memory is only sent to
  the graphics chip when it has changed. On a PC the same
  drawing runs through OpenGL in headless tests (`--wide-gpu`), to compare with the
  processor's picture: they differ only along the edges of polygons.
  LOWER DETAIL WHEN BUSY works with it too: where the graphics chip spends more than 11 ms
  of each frame on the world (a battle at 720 lines), the world is drawn with fewer lines
  until it fits, and back up when there is room.
  Where the game draws its world every other frame (Black 2's cities) and the graphics
  chip has not finished a picture in time, the last one stays on show for a frame instead
  of the game waiting for it; and the graphics chip shades fewer hidden pixels.
  The graphics chip now keeps its pictures compressed and draws the world straight into
  the frame it shows, and each DS texture is decoded once (when the game changes it)
  rather than again for every pixel (that decoding was over half of its drawing time).
- SoulSilver: a new game's first menu, which is on the touch screen alone ("Please touch any
  topic you'd like to know more about"), is answered NO INFO NEEDED by itself as it comes up;
  pressing A had chosen the controls explanation again and again.
- Black 2 battles: the level-up window (the list of stats) moves with your HP box instead of
  being cut through by it; in double, triple and rotation battles the buttons sit in the
  message box's band (as BATTLE BUTTONS' PANEL), below all your HP boxes instead of over the
  lowest one, and choosing a move's target, which was not shown at all, is.
- Black 2: with the widescreen world on, the game loads more of its map beside you (3 of its
  32-tile chunks across instead of 2), so the wide view no longer comes to the end of the
  land where the DS's own screen could not see (beside the cliff at Aspertia's lookout). The
  DS has no room for that in its 4 MB, so Black 2 now gets 8 MB, as the DS's development units
  have, with the game's memory moved into the upper half (as it is given more on a DSi). Save
  states made before keep the game's own map until the game is next started.
- Black 2: where a sky or a distant layer of the view (Castelia zoomed out; the sky,
  mountains, haze and far trees from a lookout) ends a little short of the wide view's edge
  with nothing beside it, it is continued to the edge, its picture carried on, instead of
  leaving a strip of flat colour. The start menu's grey
  circle no longer runs on below the screen as a straight band.

- Switch: quitting no longer hangs when closing the sound never returns (seen at
  "[exit] audio"): after three seconds the program ends without it.
- Black 2: the world no longer flashes up for a frame between CONTINUE and the season's
  screen (the widescreen world is left out while the game shows no 3D). The start menu is
  drawn with sharp pixels and fills the height of a wide view: its list and the saved
  game's card go on below the screens, drawn from what the game has ready there, so the
  last entry is no longer cut in half.
- **Pokemon Black (experimental):** the same program runs Black (US/Europe) like Black 2, from
  a function table made from the aletheia decompilation (`tools/bw1/`), with Black's own
  layouts where its screens differ (`frontend/bw1.h`): the start menu (the saved game's card
  beside the whole list, drawn on below the screen), the X menu, single battles' HP boxes, the
  Badge Case, the Pokedex registration after a catch and the save screen (as Black 2's).
- Black 2: in a single battle on the wide view the HP boxes are back where the game has them,
  the opponent's in the upper left and yours in the lower right (just above the battle's
  buttons), each against the edge of the view, instead of both along the top.
- **BATTLE BUTTONS (pause menu, Black and Black 2):** how a battle's touch-screen buttons are
  shown beside the wide view: CORNER (the panel in the lower right corner, as before; Black 2's
  default), PANEL (the touch screen's panel as tall as the message box) or, in Black, STRIP
  (its default: the buttons large in the message box's band, the one the cursor is on framed).
- Black 2: saving from the X menu has a layout of its own. The question, YES / NO and the
  messages that follow are in a message box along the bottom of the overworld, and the
  saved game's card (with the progress of the save) is a panel in the upper right corner.
  Before, all of that was on the hidden touch screen, and while the save was being
  written both screens popped up side by side for a second or two.
- **Test runs on the Switch:** a `test.txt` in the game's folder lists runs (a snapshot to
  start from, how many frames, which frames to take pictures of, an input script, settings
  for the run only). Each start of the game does the next run, writes the pictures (BMP), a
  line of timings per frame (`timing.csv`) and its `game.log` to `test-results/<run>/`, and
  quits; it then starts again by itself for the next one. The save and the settings are left
  alone. When all are done, `test.txt` becomes `test-done.txt`. On the PC the same options
  exist: `--relative`, `--timing <file.csv>`, `--set key=value`, `--keep-settings`. See
  `frontend/testrun.h` for the format.

- **3D ON GPU** (pause menu, Linux; "3D ON GPU (TEST)" on the Switch): the 3D scene, the
  widescreen world included, is drawn by the graphics chip through OpenGL instead of the
  processor, on the thread that shows the frames. Off by default. If it was on and the game did
  not get 600 frames into it (it hung a Switch once), it is off at the next start.
- When the game runs slower than a DS (a slow device), the sound is stretched to fill the
  time, at its own pitch, instead of breaking up into crackles. At full speed it plays
  exactly as before.
- Fixed: a zoomed or rotated background could drift and smear into streaks when parts
  of it stayed the same from one frame to the next.
- The setup program for macOS can now check for updates and update itself from
  the setup window and with `--update`, matching Linux and Windows.
- A single-screen build of the decompilation (the one-screen game) is recognised when it
  starts and shown as the screen the game itself has chosen, with no merging or
  widescreen changes. The choice is not saved in the settings.

## v0.4.2 — Faster widescreen

**Downloads:** the same setup programs as before; update from the setup window
and run the setup again to rebuild the game (your save is kept). Android
players: press **Build for Android** again and install the new app over the old
one.

- **The widescreen 3D picture takes about half the processor time it did.** It is
  drawn several pixels at a time (SIMD: AVX2 or SSE on PCs, NEON on phones), and
  its working memory now stays in the processor's cache. On PCs it also uses fewer
  threads (about one per core), which leaves more room for the game itself. The
  picture is the same, apart from the odd pixel along the edge of a polygon.
- The widescreen battle view looks for the stats window in one pass instead of four.

## v0.4.1 — Android controllers and update notice

**Downloads:** the same setup programs as before; update from the setup window
and run the setup again to rebuild the game (your save is kept). Android
players: press **Build for Android** again and install the new app over the old
one.

- **Android: controllers such as the Backbone One work.** SDL's own USB drivers
  took over some pads (the Backbone One, PlayStation pads) from Android's
  input, and lost them when they could not open them themselves. All controllers
  now come through Android, which knows them.
- **Android: the app says when there is a new version.** At start it asks
  GitHub for the newest release; when that is newer, a message at the top of the
  screen says to build the app again on the PC (the app holds your own game, so
  it cannot download itself). It is the only thing the app uses the internet for.
- **Lower world detail for slower devices.** WORLD DETAIL in the pause menu now
  also offers 384, 288 and 192 (the DS's own sharpness) as well as 540 to 1440;
  fewer lines draw faster. Left and right go down and up through the choices.
- **FPS counter.** FPS COUNTER in the pause menu shows the frames shown per
  second in the top right corner.

## v0.4.0 — Faster again, cleaner menus

**Downloads:** the setup programs for Linux (x86_64 and arm64), Windows and now
macOS (Apple Silicon and Intel; the Mac app is unsigned, so open it with
right-click → Open the first time). Use the setup window's update button or
download the new one, then run the setup again to rebuild the game (your save
is kept).

- **The bag shows just the bag.** The item description box from the other
  screen is no longer drawn below it.
- **A tidier Pokémon summary.** The Pokémon's name, item and markings sit in
  their own column and the information from both screens is shown side by side, each
  in a rounded card with its contents centred, and no borders cut off.
- **Menus stay on one layout.** Opening the bag, party, Pokégear and the other
  menus from the start menu no longer shows the top screen and then both screens
  before the menu's own view, and closing them no longer falls back to the plain
  bottom screen while they fade out. The view now follows each menu from the
  moment the game starts loading it until it has closed (the game's start and
  end functions of each menu are watched as well as its main one).
- **Faster again: about 30% in the overworld and 20% in battles** (measured
  headless on a 6-core desktop; slower machines such as the Steam Deck and
  phones gain the most). The bottom screen and anything that does not move are
  no longer redrawn every frame, the CPUs switch less often, waits for the game
  card and the touch screen are skipped, and more instructions are recompiled.
  Nothing else changes: in testing, pictures, sound and memory came out the
  same, apart from a one-line difference in a few frames of battle transitions.
- `run.sh` writes `game.log` next to it, as the setup window's Play button does.
- The log lists every controller SDL sees (name, USB IDs, whether its layout is
  known), the SDL version, and the Steam and container settings that can hide a
  controller, for reports like #8.

## v0.3.0 — Faster, updates, logs

**Downloads:** the same three setup programs; use these instead of the earlier
ones and run the setup again to rebuild the game (your save is kept). From this
version on, the setup window can update itself.

### Faster

- **The recompiled game code is about 6–9% faster than melonDS's interpreter**
  (before it was about 16% slower). The common instructions are now plain C++
  instead of calls into the interpreter, and memory accesses and jumps take a
  shorter path. Every instruction was checked against the interpreter
  (`--lockstep`) over 16,000 frames of booting, walking and battling.

### Fixed

- **White strips beside the screen as a battle opens.** The widescreen view showed
  the half of the opening's background, and the 3D pieces, that the game keeps off
  the screen; a row the screen shows in one colour is now that colour beside it.
- **The trainer's legs over the black bar** as he slides out after throwing the
  Poké Ball: beside the screen he is now behind the message box, as on the screen.
- **Fades at the start and end of a battle** reached the sides of the view a frame
  before the screen, so the picture faded unevenly. The sides now use the palettes
  and brightness the screen was drawn with.
- **The stats after a level up** were cut in two, the top half moved to the right
  with the wild Pokémon's side. Windows like it now stay where the game puts them.

### Added

- **Logs for bug reports.** Started from the setup window, the game's messages now
  go to `game.log` (new each time) and a build's output to `build.log`; **Open logs
  folder** opens the folder with both. On Android `log.txt` moved to
  `Android/data/io.github.clickingmaster.ssrecomp/files`, where a file manager or a
  PC can reach it. The renderer in use is now always in the log.

- **Updates from the setup window.** At start it checks for a newer release (shown
  next to the version number); **Update** downloads the setup program for your
  system, checks it against the release's `SHA256SUMS.txt`, puts it in place of
  the old one and restarts. Then press **Build** once to update the game; the
  save and settings are kept. (This version still has to be downloaded by hand.)
- **Issue forms** on GitHub ask for the version, system and log.

- **Portable mode** (#10): with a file `portable.txt` next to the setup program,
  everything is kept in a folder `ssrecomp-data` beside it instead of
  `%LOCALAPPDATA%` (Windows) or `~/.local/share` (Linux). The ROM's location is
  remembered relative to that folder, so it still works on another PC or drive
  letter.

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
