// The PC all-games program (Windows and Linux): the game list when it starts without
// arguments, like the Switch's all-games download (switch_platform.cpp). The player puts their
// ROMs in the roms folder beside the program; the list (launcher.h) shows them and starts the
// chosen one with its save, settings and function table.
#pragma once
#ifdef SSR_PC_LAUNCHER

namespace PcLauncher
{
// Called first in main(). With arguments it does nothing (the program runs as ever). Without
// any, it sends the log to game.log beside the program, shows the game list until a game is
// chosen and sets up the arguments for it. False when the player closed the list.
bool Start(int& argc, char**& argv);

// Whether the game was chosen from the list, so that QUIT in the pause menu goes back to it;
// ChangeGame asks for that, and Finish (last thing in main) then starts the program again.
bool CanChangeGame();
// The chosen game's name (e.g. "Pokemon Black 2"), for the window's title; empty without the list.
const char* GameName();
void ChangeGame();
void Finish();
}
#endif
