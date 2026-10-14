# Changelog

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
