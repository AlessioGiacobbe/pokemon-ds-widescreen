// Pokemon Platinum (English, CPUE): which of its screens is up, for its layouts.
//
// Platinum runs like Black 2 (melonDS's JIT, layouts that follow the game's own functions),
// with a function table made by tools/platinum/funcmap.py from a build of the pokeplatinum
// decompilation. Its screens are Gen 4's, much like SoulSilver's: the overworld and its menus
// on the top screen (the Poketch on the touch screen beside it), battles with their commands
// on the touch screen. pickSceneBw2 in main.cpp picks the scenes from the functions below.
#pragma once
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "funcwatch.h"

struct PtFuncs
{
    bool on = false;
    int field = -1;          // the overworld, every frame
    int startMenu = -1;      // the X menu (and the applications started from it)
    int dex = -1, party = -1, bag = -1, trainerCard = -1, options = -1, summary = -1;
    int townMap = -1, box = -1, naming = -1, title = -1, mainMenu = -1, intro = -1;
    int starter = -1, journal = -1, moveReminder = -1, evolution = -1, eggHatch = -1;
    int hallOfFame = -1, credits = -1;
    int battle = -1;         // any battle
    int battleInput = -1;    // the battle waiting for a command or a move on the touch screen
    int battleBag = -1, battleParty = -1; // the Bag and the party in a battle (touch screen)

    // romPath: the game being started; names: the functions the layouts watch (Platinum's are
    // added to it when the game is Platinum).
    void Init(const std::string& romPath, const FuncWatch& fw, std::vector<std::string>& names)
    {
        char code[5] = {};
        if (FILE* f = fopen(romPath.c_str(), "rb"))
        {
            if (fseek(f, 0x0C, SEEK_SET) != 0 || fread(code, 1, 4, f) != 4) code[0] = 0;
            fclose(f);
        }
        on = !strcmp(code, "CPUE");
        if (!on) return;
        auto find = [&](const char* name) {
            names.push_back(name);
            return fw.Find(name);
        };
        field = find("FieldMap_Main");
        startMenu = find("StartMenu_Main");
        dex = find("PokedexMain_Main");
        party = find("PartyMenu_Main");
        bag = find("BagApplication_Main");
        trainerCard = find("TrainerCaseApp_Main");
        options = find("OptionsMenu_Main");
        summary = find("PokemonSummaryScreen_Main");
        townMap = find("TownMap_Main");
        box = find("BoxAppMan_Main");
        naming = find("NamingScreen_Main");
        title = find("TitleScreen_Main");
        mainMenu = find("MainMenu_Main");
        intro = find("RowanIntro_Main");
        starter = find("ChooseStarter_Main");
        journal = find("JournalController_Main");
        moveReminder = find("MoveReminder_Main");
        evolution = find("Evolution_Main");
        eggHatch = find("EggHatch_Main");
        hallOfFame = find("HallOfFameManager_Main");
        credits = find("EndCreditsApp_Main");
        battle = find("Battle_Main");
        battleInput = find("BattleSystem_MenuInput");
        battleBag = find("BattleBagTask_Tick");
        battleParty = find("BattlePartyTask_Tick");
        fprintf(stderr, "[platinum] field %d, menu %d, battle %d\n", field, startMenu, battle);
    }
};
