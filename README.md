# Pokémon DS games on the Nintendo Switch

**Your favourite DS Pokémon games on a modded Switch: one screen, widescreen,
720p, 60 fps.** No more squinting at two tiny stacked DS screens.

![Black 2: cycling along Castelia City's waterfront in widescreen, with the in-between frames on](docs/screenshots/black2-castelia-bike-60fps.gif)

| Game | Status | Download |
|---|---|---|
| **Pokémon Black 2 and White 2** | ✅ out now | [pokemon-black2-white2-wide-switch.zip](../../releases/tag/bw2-switch-v1) |
| **Pokémon SoulSilver and HeartGold** | ✅ out now | [pokemon-soulsilver-heartgold-wide-switch.zip](../../releases/tag/soulsilver-heartgold-v1) |
| **Pokémon Black and White** | ✅ out now | [pokemon-black-white-wide-switch.zip](../../releases/tag/black-white-switch-v1) |
| **Pokémon Platinum** | ✅ out now | [pokemon-platinum-wide-switch.zip](../../releases/tag/platinum-v1) |
| **All seven games in one** (one program, choose the game when it starts) | ✅ out now | [pokemon-ds-wide-all-switch.zip](../../releases/tag/all-games-v2) |
| **Pokémon Omega Ruby** | 🛠️ in the works (its own project) | |

**Which one?** The **all seven games** download is one program: start it and
pick the game from a list. The other downloads are one program per game, if
you only want one or two. Every download is free of the games themselves: you
bring your own ROMs, and the program reads what it needs from them when the
game starts.

## What is it?

A Switch homebrew app that runs the game on the melonDS emulator core (built
in: there is no need to install melonDS or anything else), plus a layer that
knows what the game is doing (which screen, which menu, which
battle) and rearranges it into one proper widescreen picture. It is **not a
native port** (those are the dream, see [What's next](#whats-next)); it's the
DS game, presented the way it should look on a Switch.

## Why not just play it in melonDS?

| | melonDS | this |
|---|---|---|
| Screens | two 256×192 DS screens, stacked or side by side | one 16:9 picture, the touch screen's buttons tucked beside the action |
| 3D world | 4:3 at DS resolution | 16:9 at 720p, drawn by the Switch's graphics chip, skies and scenery carried to the edges |
| Frame rate | the game's own 30 fps 3D | 60 fps, with in-between frames |

## Installing a separate download

(For the all seven games download, see
[All seven games in one program](#all-seven-games-in-one-program) instead.)

1. Download the game's zip (table above) and unzip it to the root of the SD
   card. It makes a folder for each game in `/switch/`; a game whose ROM you
   don't put in its folder just isn't used.
2. Copy your own ROM into the game's folder, as the only `.nds` file there.
3. **Hold R while starting any game** to open the Homebrew Menu, then pick the
   game (started from the album there is not enough memory). **QUIT** in the
   pause menu (+ and − together) saves and goes back to the Homebrew Menu.

Each game keeps its save in its own folder (named below). To carry on from an
emulator or a flash cart, copy your save in under that name.

**Recommended: start each game from the HOME Menu.** With
[sphaira](https://github.com/ITotalJustice/sphaira) (a Homebrew Menu you can
use instead of the usual one) you can install a forwarder for each game: an
icon on the Switch's HOME Menu that starts it directly, with no holding R. In
sphaira's Homebrew list, select the game (for example `pokemon-black2-wide`, or
**Pokemon DS Widescreen** for the all seven games download) and choose to install
a forwarder. Do it once per game; keep the game's folder
where it is, as the forwarder starts the `.nro` in it. Each game then has its
own entry, so sys-clk (below) can have its own settings for it.

---

## All seven games in one program

![The game list: Black, White, Black 2, White 2, HeartGold, SoulSilver and Platinum in a grid on black, Black 2 chosen in red with its save found](docs/screenshots/all-games-launcher.png)

`pokemon-ds-wide-all-switch.zip` is one program for all seven games: Black,
White, Black 2, White 2, HeartGold, SoulSilver and Platinum. It opens on a grid
of your games, each showing whether it has a save yet; pick one and it starts.
The separate downloads above are still there if you only want one or two.

**Installing**

1. Unzip it to the root of the SD card. It makes `/switch/pokemon-ds-wide/`
   with an empty `roms` folder inside.
2. Copy your ROMs into `/switch/pokemon-ds-wide/roms/` (any names).
3. Hold R while starting any game, pick **Pokemon DS Widescreen**, then the
   game.

**Using the list:** the D-pad or a stick moves between games, **A** plays,
**+** goes back to the Homebrew Menu. It opens on the game you played last.
**QUIT** in the pause menu (+ and − together) saves and comes back to the list
(started from a forwarder it closes instead; start it again to pick another).
With only one game found there is no list: it starts straight away.

**Saves:** each game keeps its save under its own name (`black2.sav`,
`white2.sav`, `black.sav`, `white.sav`, `soulsilver.sav`, `heartgold.sav`,
`platinum.sav`):
in `/switch/pokemon-ds-wide/` for a ROM in `roms/`, or in a separate
download's folder for a ROM there.

**Already using the separate downloads?** Leave them as they are: the list
also shows the games in their folders and plays each with the save already
there, so you don't need to move anything. If the same game is in both places
it is listed twice, each copy with its own save, and the label under it says
which folder it is in.

**The pictures:** each game shows the icon from its ROM, or the HOME Menu icon
of its separate program when that is installed. For your own, put a square
JPEG or PNG in `/switch/pokemon-ds-wide/icons/` named after the game's code:
`IRBO` Black, `IRAO` White, `IREO` Black 2, `IRDO` White 2, `IPKE` HeartGold,
`IPGE` SoulSilver, `CPUE` Platinum (e.g. `icons/IREO.jpg`).

The pause-menu settings are shared by all the games in this program (the
separate programs keep their own).

---

## Pokémon Black 2 and White 2

| | |
|---|---|
| ![Black 2: Black Kyurem against a wild Crustle, with the game's FIGHT, BAG, RUN and POKÉMON panel beside the battle](docs/screenshots/black2-black-kyurem-battle.jpg) | ![White 2: White Kyurem against a wild Tropius, with the FIGHT panel beside the battle](docs/screenshots/white2-white-kyurem-battle.jpg) |
| ![Black 2: the view from Aspertia's lookout, sky and mountains carried to the edges](docs/screenshots/black2-aspertia-lookout.jpg) | ![Black 2: Castelia City's pier](docs/screenshots/black2-castelia-pier.jpg) |

- **Download:** [`pokemon-black2-white2-wide-switch.zip`](../../releases/tag/bw2-switch-v1),
  folders `/switch/pokemon-black2-wide/` and `/switch/pokemon-white2-wide/`.
- **ROMs:** any English copy (US or Europe), trimmed or not, including the
  "NDSi Enhanced" one. Other languages put the game's code elsewhere: the
  program says so instead of starting.
- **Saves:** `black2.sav` and `white2.sav` (`tools/bw2/convert_save.py` turns
  `.dsv` and Action Replay saves into the raw 512 KB save).
- **What you get:** battles (single, double, triple, rotation) with the HP boxes
  at the edges of the wide view and the battle buttons beside them; menus, the
  bag and the save screen laid out for one screen; and more of the map loaded
  around you, so the wide view never runs out of land.

## Pokémon SoulSilver and HeartGold

![SoulSilver: a wild Meowth appears and Lugia is sent out, in widescreen](docs/screenshots/soulsilver-lugia-battle.gif)

| | |
|---|---|
| ![HeartGold: Totodile against a wild Pidgey on Route 29, with FIGHT, BAG, RUN, POKÉMON and the party beside the battle](docs/screenshots/heartgold-totodile-battle.jpg) | ![SoulSilver: Lugia following you along Route 7](docs/screenshots/soulsilver-lugia-following.jpg) |

- **Download:** [`pokemon-soulsilver-heartgold-wide-switch.zip`](../../releases/tag/soulsilver-heartgold-v1),
  folders `/switch/pokemon-soulsilver-wide/` and `/switch/pokemon-heartgold-wide/`.
- **ROMs:** the English US versions. Other versions start, but without the
  one-screen layouts.
- **Saves:** `soulsilver.sav` and `heartgold.sav` (the same format, so either
  game reads the other's).
- **What you get:** Johto and Kanto in widescreen, with your partner Pokémon
  walking behind you; battles with their buttons beside the action; menus laid
  out for one screen. Where this project started: the
  original ssrecomp was SoulSilver on the PC.

## Pokémon Black and White

| | |
|---|---|
| ![Black: Reshiram against a wild Mantyke, with the game's FIGHT, BAG, RUN and POKÉMON panel beside the battle](docs/screenshots/black-reshiram-battle.jpg) | ![White: Zekrom in battle, with the FIGHT panel beside it](docs/screenshots/white-zekrom-battle.jpg) |
| ![Black: a double battle, Reshiram and Excadrill against a wild Pelipper and Mantyke, the HP boxes down the edges](docs/screenshots/black-reshiram-double.jpg) | ![Black: Nimbasa City in widescreen](docs/screenshots/black-nimbasa.jpg) |

- **Download:** [`pokemon-black-white-wide-switch.zip`](../../releases/tag/black-white-switch-v1),
  folders `/switch/pokemon-black-wide/` and `/switch/pokemon-white-wide/`.
- **ROMs:** any English copy (US or Europe), trimmed or not.
- **Saves:** `black.sav` and `white.sav` (the same format, so either game reads
  the other's; `tools/bw2/convert_save.py` turns `.dsv` and Action Replay saves
  into the raw 512 KB save).
- **What you get:** Unova's first adventure with the same treatment as Black 2:
  battles (single, double, triple, rotation) with the HP boxes at the edges of
  the wide view and the battle buttons beside them; its own layouts for the
  start menu, the X menu, the save screen, the Badge Case and the Pokédex; and
  the C-Gear's question at the start answered for you.

---

## Pokémon Platinum

| | |
|---|---|
| ![Platinum: Infernape against a wild Machoke, with FIGHT, BAG, RUN and POKÉMON beside the battle](docs/screenshots/platinum-infernape-battle.jpg) | ![Platinum: Snowpoint City in the snow, in widescreen](docs/screenshots/platinum-snowpoint.jpg) |

- **Download:** [`pokemon-platinum-wide-switch.zip`](../../releases/tag/platinum-v1),
  folder `/switch/pokemon-platinum-wide/`.
- **ROM:** the English US version (game code `CPUE`), trimmed or not.
- **Save:** `platinum.sav`.
- **What you get:** Sinnoh in widescreen, the snow and rain reaching the edges
  of the view; battles with their buttons and moves beside the action; the X
  menu at the right edge of the view, and the Bag, party, Pokédex, map and
  other menus laid out for one screen.

---

## Recommended settings

**sys-clk** (recommended for full speed), handheld. With a forwarder (above),
start the game, open sys-clk's overlay and set these for that game's own
profile: they then apply whenever you play it, and only then. Without one, set
them for the game you start the Homebrew Menu from, or as the global profile.

| | |
|---|---|
| CPU | 1785 MHz |
| GPU | 460 MHz (the highest of the console's own handheld speeds; no need to go higher) |
| Memory | 1600 MHz |

Without sys-clk the games run at the console's normal speeds and drop frames
in busy places.

**Pause menu** (+ and − together). Black 2 and White 2 already start with
what is recommended, and every game always uses the one wide screen:

| Setting | Recommended | What it does |
|---|---|---|
| 60 FPS | ON | in-between frames: the games draw their 3D at 30 fps |
| BATTLE BUTTONS | PANEL | Black 2, White 2, Black and White: the game's own FIGHT, BAG, RUN and POKÉMON panel beside the battle (Black and White also have STRIP: large buttons in the message box) |
| WORLD DETAIL | 720 | lines the wide world is drawn with (540 if you want headroom) |
| LOWER DETAIL WHEN BUSY | ON | fewer lines for a moment instead of slowing down |
| WORLD ON GPU | ON | the world drawn by the graphics chip |

The games run on melonDS's FAST JIT. If a start ever fails with it, the next
one falls back by itself (hold L while starting for the interpreter).

## Under the hood

The widescreen world is drawn by the Switch's graphics chip (deko3D), so Black
2 runs at (or a whisker under) 60 fps at 720p in towns and battles, measured on
a Switch OLED in handheld mode. The full list of changes is in
[CHANGELOG.md](CHANGELOG.md).

## Known limits

- Black, White, Black 2 and White 2: English (US/Europe) versions only. SoulSilver,
  HeartGold and Platinum: US versions only.
- Platinum: the Pokétch (the watch on the touch screen) isn't shown during
  normal play; its apps are one press of the screen-swap button away.
- SoulSilver and HeartGold: at map borders (Route 29 and New Bark Town, Celadon
  City and Route 16), parts of the neighbouring map pop in and out at the sides
  of the wide view. Loading more of the map, as Black 2 and White 2 do, should
  fix it.
- Switch only for now. Found a bug? See [Reporting a bug](#reporting-a-bug).

## What's next

- **Pokémon Omega Ruby**: in the works, as its own project.
- **Native Switch ports**: the dream, hopefully for all of them. Optimistic,
  but we're aiming for it.

## Reporting a bug

Open a **[bug report](https://github.com/mamesj/pokemon-ds-widescreen-switch/issues/new?template=bug_report.yml)**
(you need to be signed in to GitHub). The form asks for the game, your ROM's
version and the **`game.log`** file, which says what the program did:

1. **Find it:** `game.log` is in the game's folder on the SD card:
   `/switch/pokemon-ds-wide/` for the all-games download, otherwise the game's
   own folder (e.g. `/switch/pokemon-black2-wide/`). It is written again each
   time the game starts, so copy it straight after the problem.
2. **Get it onto a computer:** put the SD card in a card reader; or, leaving
   it in the Switch, use Hekate's **Tools > USB Tools > SD Card** (or DBI's MTP
   mode) to see the card on the computer over USB; or an FTP app such as ftpd
   over Wi-Fi.
3. **Add it to the form:** drag the file into the `game.log` box.

A photo or short video of the screen helps too. Please never attach your ROM.
A white or black screen when a game starts usually means the ROM isn't one the
program plays: an English copy of Black, White, Black 2 or White 2 (US or
Europe), or the English US copy of SoulSilver or HeartGold, unchanged (no
patches, translations, randomizers or trimming).

## Support

Tips are welcome but never expected:
[buy me a coffee](https://buymeacoffee.com/mamesj) or
[PayPal](https://paypal.me/mamesj).

## Credits and licence

A fork of [ClickingMaster/ssrecomp](https://github.com/ClickingMaster/ssrecomp)
(Pokémon SoulSilver recompiled for single-screen devices), which gave this its
single-screen and widescreen view; [its README](docs/original-ssrecomp-README.md)
covers that project. Function names come from the
[pokebw2](https://github.com/fuddlesworth/pokebw2) and
[aletheia](https://github.com/Project-Holon/aletheia) decompilations (only their
symbol files); SoulSilver's and HeartGold's addresses come from ssrecomp's own
SoulSilver layout. The DS is emulated by [melonDS](https://melonds.kuribo64.net).

GPL-3.0-or-later (see `LICENSE`), as required by melonDS. Not affiliated with
or endorsed by Nintendo, Game Freak, The Pokémon Company or the melonDS
project; Pokémon is a trademark of its owners.

---

Built with the help of [Claude](https://claude.ai), Anthropic's AI.
