POKEMON DS WIDESCREEN FOR PC
============================

The Pokemon DS games on one widescreen display, with the two DS screens merged into one view,
the world drawn out to the sides of a 16:9 screen, 60 fps and controller support.
It plays your own copies of:

  Pokemon Black, White, Black 2 and White 2 (English, US or Europe)
  Pokemon SoulSilver and HeartGold (US)
  Pokemon Platinum (US)

Nothing of the games comes with this download: you need your own ROMs (.nds files).


GETTING STARTED
---------------

1. Unpack the download anywhere you like (not inside Program Files).
2. Put your ROMs in the roms folder. Any file names are fine.
3. Start Pokemon DS Launcher.exe (Windows) or pokemon-ds-wide (Linux).
4. Pick a game from the list: arrow keys or a controller's D-pad, then Enter or A.
   You can also double-click a game. Press O in the list to open the roms folder, and H (or
   a controller's Y) for the settings: the Esc menu's options, each explained.

Windows may warn that the program is from an unknown publisher the first time: choose
"More info", then "Run anyway".
On Linux, SDL2 must be installed (Ubuntu and Debian: sudo apt install libsdl2-2.0-0).


PICTURE SETTINGS (Esc menu)
---------------------------

  WORLD ON GPU       ON: the widescreen world is drawn by your graphics card (OpenGL 4.3 or
                     newer). Turn it OFF if the world looks wrong or the game will not start.
  3D RESOLUTION      how sharp the widescreen 3D world is: 720P, 1080P, 1440P (2K) or
                     2160P (4K), lower for slow PCs. The 2D sprites and text stay the DS's
                     own pixels. With WORLD ON GPU, 1080P or 1440P suits most PCs; 2160P needs
                     a good graphics card.
  LOWER RES WHEN BUSY      lowers the 3D RESOLUTION for a moment when the PC cannot keep up.
  BORDERLESS FULLSCREEN    fills your screen at its own resolution, as a borderless window,
                     so switching to other windows is instant. F11 does the same.


WHERE THINGS ARE KEPT
---------------------

  roms/           your ROMs
  icons/          pictures for the game list, if you want them: a JPEG or PNG named by the
                  game's code (IREO.jpg Black 2, IRDO White 2, IRBO Black, IRAO White,
                  IPKE HeartGold, IPGE SoulSilver, CPUE Platinum). Without one, the list shows
                  the game's own small DS icon. A picture named like the ROM (Black 2.jpg beside
                  Black 2.nds) works too.
  saves/          your saved games, one per game (black2.sav, soulsilver.sav, ...)
  settings.cfg    your settings
  game.log        what happened in the last session (send it with a bug report)

To carry on a game from another emulator, put its .sav file in the saves folder with the name
above (for example saves/platinum.sav). Back your saves up before you do.


CONTROLS
--------

Controller (Xbox, PlayStation, Switch Pro and others): plays as a DS, with the touch screen's
buttons shown in the view. Hold the right trigger to fast forward (up to 4x).

Keyboard:
  Arrow keys       D-pad
  X / Z            A / B
  S / A            X / Y
  Q / W            L / R
  Enter            START
  Backspace        SELECT
  Mouse            touch screen (click to touch)
  Esc              menu (settings, save state, QUIT back to the game list)
  F11              fullscreen
  Tab              next screen layout
  F1               show the other DS screen
  Left Shift       fast forward while held


SOURCE AND LICENCE
------------------

This program is free software under the GNU General Public License version 3 (LICENSE.txt),
built on melonDS. Its source code:
  https://github.com/mamesj/pokemon-ds-widescreen-switch
SDL2 (SDL2.dll on Windows) is under the zlib licence (LICENSE-SDL2.txt).
Pokemon is a trademark of Nintendo, Creatures and GAME FREAK. This project is not affiliated
with or endorsed by them.
