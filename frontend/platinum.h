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
    // Whether a weather with particles ran within the last `frames` frames.
    bool Weather(const FuncWatch& fw, unsigned frames) const
    {
        for (int f : weather)
            if (fw.Ran(f, frames)) return true;
        return false;
    }

    // The names of Platinum's application layouts (pickSceneBw2), which stay up while the
    // application fades out and change at once between two of them with both screens black.
    static bool AppScene(const char* n)
    {
        static const char* const names[] = { "party", "bag", "summary", "options", "journal", "move-reminder",
            "pokedex", "trainer-card", "pc-box", "map", "naming" };
        for (const char* m : names)
            if (!strcmp(n, m)) return true;
        return false;
    }

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
    int saving = -1;         // the save being written (several seconds, nothing else running)
    // The weathers with particles (sprites over the world): each one's task, every frame while
    // it lasts (pokeplatinum's src/overlay005/ov5_021D5EB8.c, its table of weathers: rain, heavy
    // rain, thunderstorm, snow, heavy snow, blizzard, ashfall, sandstorm, hail, spirits and two
    // more).
    std::vector<int> weather;

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
        saving = find("SaveData_CardSave_Main");
        for (const char* w : { "ov5_021D7658", "ov5_021D9FF8", "ov5_021D9F0C", "ov5_021D79F0", "ov5_021D7E54", "ov5_021D8FF8",
                               "ov5_021DA244", "ov5_021D8D08", "ov5_021D9464", "ov5_021DA8A0", "ov5_021D823C", "ov5_021D8948" })
            weather.push_back(find(w));
        fprintf(stderr, "[platinum] field %d, menu %d, battle %d\n", field, startMenu, battle);
    }
};
