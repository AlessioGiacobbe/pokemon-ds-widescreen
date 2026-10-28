// Nintendo Switch (homebrew, libnx) parts of the frontend. See switch_platform.h.
#ifdef __SWITCH__
#include "switch_platform.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include <switch.h>

#include "testrun.h"
// (stb_image's code here, for the game list's pictures: only JPEG and PNG.)
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#include "thirdparty/stb_image.h"
#undef STB_IMAGE_IMPLEMENTATION
#include "launcher.h"

// How many chunks the program's SoulSilver tables have: none in a build for the Black games only.
extern const uint32_t rc_num_chunks;

namespace SwitchPlatform
{
namespace
{
std::vector<std::string> g_args;
std::vector<char*> g_argv;
bool g_boosted = false;
// A test run from test.txt (testrun.h), and the .nro to start again for the next one.
TestRun::State g_test;
std::string g_self;

// Shows a message on the console's screen until + is pressed. Used only before SDL starts.
void ShowMessage(const std::string& text)
{
    fprintf(stderr, "%s\n", text.c_str());
    consoleInit(nullptr);
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    PadState pad;
    padInitializeDefault(&pad);
    printf("\n  Pokemon DS widescreen\n\n%s\n\n  Press + to return to the Homebrew Menu.\n", text.c_str());
    while (appletMainLoop())
    {
        padUpdate(&pad);
        if (padGetButtonsDown(&pad) & HidNpadButton_Plus) break;
        consoleUpdate(nullptr);
    }
    consoleExit(nullptr);
}

// The program's romfs (the Black games' function tables), mounted once.
bool RomfsReady()
{
    static const bool ready = R_SUCCEEDED(romfsInit());
    return ready;
}

// Whether this build plays a game: SoulSilver and HeartGold need the program's own tables, the
// Black games and Platinum a function table (the player's own funcs.txt beside the game, or the
// build's).
bool Playable(const Launcher::Game& g)
{
    if (!g.known) return false;
    if (g.code == "IPGE" || g.code == "IPKE") return rc_num_chunks > 0;
    struct stat st;
    if (stat((g.folder + "/funcs.txt").c_str(), &st) == 0) return true;
    return RomfsReady() && stat(("romfs:/funcs/" + g.code + ".txt").c_str(), &st) == 0;
}

// The game list (launcher.h) on the screen until a game is chosen: its index, or -1 for +.
// Shown with libnx's framebuffer, before the game's own window exists.
int PickGame(const std::vector<Launcher::Game>& games, int selected)
{
    Framebuffer fb;
    if (R_FAILED(framebufferCreate(&fb, nwindowGetDefault(), 1280, 720, PIXEL_FORMAT_RGBA_8888, 2))) return selected;
    framebufferMakeLinear(&fb);
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    PadState pad;
    padInitializeDefault(&pad);
    Canvas canvas;
    canvas.Resize(1280, 720);
    int first = 0, held = 0, result = -1, cols = 3;
    std::string note;
    while (appletMainLoop())
    {
        padUpdate(&pad);
        const u64 down = padGetButtonsDown(&pad), now = padGetButtons(&pad);
        // The arrows (or a stick) move through the grid, and keep moving while held.
        const bool up = now & HidNpadButton_AnyUp, dn = now & HidNpadButton_AnyDown;
        const bool lf = now & HidNpadButton_AnyLeft, rt = now & HidNpadButton_AnyRight;
        held = (up || dn || lf || rt) ? held + 1 : 0;
        const bool step = held == 1 || (held > 20 && held % 6 == 0);
        int to = selected;
        if (step && lf) to = selected - 1;
        if (step && rt) to = selected + 1;
        if (step && up) to = selected - cols;
        if (step && dn) to = std::min(selected + cols, (int)games.size() - 1);
        if (to != selected && to >= 0 && to < (int)games.size())
        {
            selected = to;
            note.clear();
        }
        if (down & HidNpadButton_A)
        {
            if (Playable(games[selected]))
            {
                result = selected;
                break;
            }
            note = games[selected].known ? "THIS DOWNLOAD DOES NOT HAVE THIS GAME" : "THIS GAME IS NOT ONE THAT THIS PLAYS";
        }
        if (down & (HidNpadButton_Plus | HidNpadButton_B)) break;
        cols = Launcher::Draw(canvas, games, selected, first, note);
        u32 stride;
        u32* out = (u32*)framebufferBegin(&fb, &stride);
        for (int y = 0; y < 720; y++)
        {
            const uint32_t* src = &canvas.px[(size_t)y * 1280];
            u32* dst = out + (size_t)y * (stride / 4);
            // (ARGB to the framebuffer's RGBA bytes.)
            for (int x = 0; x < 1280; x++)
            {
                const uint32_t p = src[x];
                dst[x] = 0xFF000000u | (p & 0xFF) << 16 | (p & 0xFF00) | (p >> 16 & 0xFF);
            }
        }
        framebufferEnd(&fb);
    }
    framebufferClose(&fb);
    return result;
}

// The games to choose from: rom.nds, or the ROMs in the program's folder (and its roms/ folder);
// in the all-games program also those in the other folders of /switch (the one-game programs'
// folders), each played with the save in its own folder, so that the two kinds of download share
// a game's save.
std::vector<Launcher::Game> FindGames(const std::string& dir, bool& ownFolder)
{
    std::vector<Launcher::Game> games;
    Launcher::Game g;
    ownFolder = true;
    if (Launcher::ReadGame(dir + "/rom.nds", dir, g)) return { g };
#ifndef SSR_ALL_GAMES
    // (A one-game program: its own folder only, as always.)
    Launcher::ScanFolder(dir, games, false);
#else
    Launcher::ScanFolder(dir, games);
    {
        const size_t own = games.size();
        if (DIR* d = opendir("sdmc:/switch"))
        {
            while (dirent* e = readdir(d))
            {
                const std::string folder = std::string("sdmc:/switch/") + e->d_name;
                if (e->d_name[0] == '.' || folder == dir) continue;
                struct stat st;
                if (stat(folder.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) Launcher::ScanFolder(folder, games);
            }
            closedir(d);
        }
        ownFolder = own > 0;
    }
#endif
    Launcher::Sort(games);
    // The same game in two folders (two ROMs, two saves): each says where it is.
    for (size_t i = 0; i < games.size(); i++)
        for (size_t j = 0; j < games.size(); j++)
            if (i != j && games[i].code == games[j].code && games[i].folder != dir)
            {
                // (pokemon-black2-wide: IN BLACK2)
                std::string name = games[i].folder.substr(games[i].folder.find_last_of('/') + 1);
                if (name.rfind("pokemon-", 0) == 0) name = name.substr(8);
                if (name.size() > 5 && name.compare(name.size() - 5, 5, "-wide") == 0) name.resize(name.size() - 5);
                games[i].where = "IN " + name.substr(0, 14);
            }
    return games;
}

// Set when the game list was shown: the pause menu can then go back to it.
bool g_canChange = false, g_changeGame = false;
}

// Set while a start with the JIT (and with its fast memory) has not yet run for a few
// seconds (see JitStarted).
static std::string g_jitMarker, g_fastMemMarker;

bool Start(int& argc, char**& argv)
{
    // The folder the .nro was started from (the Homebrew Menu passes its path), or the usual one.
    std::string dir = "sdmc:/switch/ssrecomp";
    if (argc > 0 && argv[0] && strncmp(argv[0], "sdmc:/", 6) == 0)
    {
        std::string self = argv[0];
        size_t slash = self.find_last_of('/');
        if (slash != std::string::npos && slash > 5) dir = self.substr(0, slash);
    }
    mkdir(dir.c_str(), 0777);
    chdir(dir.c_str());
    if (argc > 0 && argv[0]) g_self = argv[0];
    // A test run (testrun.h) from test.txt, if there is one to do: its log goes to its folder.
    std::vector<std::string> testArgs;
    std::string testNotes;
    // (Not when started with arguments, which are then used as they are.)
    const bool testRun = argc < 2 && TestRun::Setup(dir, testArgs, g_test, testNotes);
    const std::string logPath = testRun ? g_test.results + "/game.log" : dir + "/game.log";

    // Messages go to game.log beside the game, as run.sh does on a PC, with the frame timing,
    // so that a log can be sent with a bug report. The last session's log is kept as
    // game-previous.log: QUIT to the game list starts the program again, which would otherwise
    // replace the log of the game just played with the list's few lines. (Only a log longer
    // than the list's is kept, so going to the list twice keeps the game's.)
    if (!testRun)
    {
        struct stat ls;
        if (stat(logPath.c_str(), &ls) == 0 && ls.st_size > 1024)
        {
            const std::string prev = dir + "/game-previous.log";
            remove(prev.c_str());
            rename(logPath.c_str(), prev.c_str());
        }
    }
    if (!freopen(logPath.c_str(), "w", stderr)) freopen("game.log", "w", stderr);
    setvbuf(stderr, nullptr, _IOLBF, 0);
    setenv("SSR_PERF", "1", 0);
    fprintf(stderr, "[switch] started from %s\n", argc > 0 && argv[0] ? argv[0] : "?");
    fputs(testNotes.c_str(), stderr);

    // Started from the album (applet mode) a homebrew program only gets a few hundred MB of
    // memory; the game's code alone is larger. Started in place of a game (holding R) it gets
    // nearly all of it.
    const AppletType type = appletGetAppletType();
    fprintf(stderr, "[switch] applet type %d\n", (int)type);
    if (type != AppletType_Application && type != AppletType_SystemApplication)
    {
        ShowMessage("  This needs the console's full memory.\n"
                    "  Start it from a game instead of the album:\n"
                    "  hold R while starting any game, then choose this game\n"
                    "  (or start it from its forwarder on the HOME Menu).");
        return false;
    }
    u64 total = 0, used = 0;
    svcGetInfo(&total, InfoType_TotalMemorySize, CUR_PROCESS_HANDLE, 0);
    svcGetInfo(&used, InfoType_UsedMemorySize, CUR_PROCESS_HANDLE, 0);
    fprintf(stderr, "[switch] memory: %llu MB in use of %llu MB\n", (unsigned long long)(used >> 20),
        (unsigned long long)(total >> 20));

    // With arguments (e.g. sent with nxlink while testing) they are used as they are.
    if (argc >= 2) return true;

    // The game: the only one there is, or the one chosen from the list (launcher.h).
    bool ownFolder = true;
    std::vector<Launcher::Game> games = FindGames(dir, ownFolder);
#ifdef SSR_ALL_GAMES
    // The all-games download comes with an empty roms folder; made here too, in case it was lost
    // in copying, so that the message below points somewhere that is there.
    mkdir((dir + "/roms").c_str(), 0777);
    const std::string romsDir = dir + "/roms/";
#else
    const std::string romsDir = dir + "/";
#endif
    if (games.empty())
    {
        ShowMessage("  No game found. Copy your ROMs (Pokemon Black, White, Black 2 or White 2 in\n"
                    "  English; SoulSilver or HeartGold US; Platinum in English) into\n  " + romsDir);
        return false;
    }
    int chosen = 0;
#ifndef SSR_ALL_GAMES
    // The one-game programs have no game list (only the all-games download does).
    if (games.size() > 1)
    {
        ShowMessage("  More than one .nds file in\n  " + dir + "\n  Rename the one to play to rom.nds.");
        return false;
    }
#else
    if (games.size() > 1)
    {
        // The list starts on the game played last.
        const std::string lastPath = dir + "/last-game.txt";
        std::string last;
        if (FILE* f = fopen(lastPath.c_str(), "r"))
        {
            char line[768] = {};
            if (fgets(line, sizeof(line), f)) last = line;
            fclose(f);
            while (!last.empty() && (last.back() == '\n' || last.back() == '\r')) last.pop_back();
        }
        for (size_t i = 0; i < games.size(); i++)
            if (games[i].rom == last) chosen = (int)i;
        if (testRun)
        {
            // (A test run, test.txt, plays the game played last without the list: no one is there.)
            fprintf(stderr, "[switch] test run: %s, without the game list\n", games[chosen].rom.c_str());
        }
        else
        {
            // Each game's picture, if the player has one (Launcher::FindArt).
            for (Launcher::Game& g : games) Launcher::FindArt(g, dir);
            // A game with no picture of its own takes one another copy of it has (the icon of its
            // one-game program, say).
            for (Launcher::Game& g : games)
                for (const Launcher::Game& other : games)
                    if (g.art.empty() && !other.art.empty() && other.code == g.code) g.art = other.art;
            chosen = PickGame(games, chosen);
            if (chosen < 0) return false;
            if (FILE* f = fopen(lastPath.c_str(), "w"))
            {
                fprintf(f, "%s\n", games[chosen].rom.c_str());
                fclose(f);
            }
            g_canChange = true;
        }
    }
#endif
    const Launcher::Game& game = games[chosen];
    const std::string rom = game.rom, gameDir = game.folder;
    char code[5] = {};
    memcpy(code, game.code.data(), std::min<size_t>(4, game.code.size()));
    // SoulSilver and HeartGold (US): the same program, layouts from its own tables.
    const bool heartSilver = !strcmp(code, "IPGE") || !strcmp(code, "IPKE");
    // The save is named after the game (SoulSilver's name is the one earlier builds used), in
    // the game's own folder.
    const std::string saveName = game.saveName;
    fprintf(stderr, "[switch] %s (game code %s), save %s/%s%s\n", rom.c_str(), code[0] ? code : "?", gameDir.c_str(),
        saveName.c_str(), ownFolder ? "" : " (from another folder)");
    // The settings: settings.cfg beside the game (ssrecomp.cfg before; an old one is renamed).
    const std::string cfgPath = dir + "/settings.cfg", oldCfg = dir + "/ssrecomp.cfg";
    struct stat cst;
    if (stat(cfgPath.c_str(), &cst) != 0 && stat(oldCfg.c_str(), &cst) == 0) rename(oldCfg.c_str(), cfgPath.c_str());
    g_args = { argc > 0 && argv[0] ? argv[0] : "pokemon-ds-wide", rom, "--save", gameDir + "/" + saveName, "--fullscreen",
               "--config", cfgPath };
    // A game that is not recompiled (Black, White, Black 2, White 2) comes with its function table, made on the
    // PC by tools/bw2/funcmap.py or tools/bw1/funcmap.py; its layouts follow the functions that
    // run. SoulSilver and HeartGold need none (their layouts come from their own tables, built into the program)
    // and must not be given one: a function table switches to the Black games' layouts.
    // A funcs.txt beside the game is used first (made from the player's ROM by
    // tools/switch/build_game.sh); otherwise the shared build's own, one per version in its
    // romfs (tools/switch/build_game.sh bw), whose overlay bytes are read from the ROM.
    struct stat st;
    std::string funcsPath;
    if (!heartSilver)
    {
        const std::string own = gameDir + "/funcs.txt", builtIn = std::string("romfs:/funcs/") + code + ".txt";
        if (stat(own.c_str(), &st) == 0) funcsPath = own;
        else if (RomfsReady() && stat("romfs:/funcs", &st) == 0)
        {
            if (stat(builtIn.c_str(), &st) == 0) funcsPath = builtIn;
            else
            {
                ShowMessage(std::string("  ") + rom.substr(rom.find_last_of('/') + 1) + " is a game this program does not know\n"
                            "  (game code " + (code[0] ? code : "?") + ").\n\n"
                            "  It plays the English versions of:\n"
                            "    Pokemon Black 2 (IREO), Pokemon White 2 (IRDO), Pokemon Black (IRBO)\n"
                            "    and Pokemon White (IRAO), US and Europe;\n"
                            "    Pokemon SoulSilver (IPGE) and Pokemon HeartGold (IPKE), US, with the\n"
                            "    SoulSilver and HeartGold download;\n"
                            "    Pokemon Platinum (CPUE), with the Platinum download.\n"
                            "  Other languages put the game's code in other places.");
                return false;
            }
        }
    }
    const bool funcs = !funcsPath.empty();
    if (funcs)
    {
        fprintf(stderr, "[switch] function table %s\n", funcsPath.c_str());
        g_args.push_back("--funcs");
        g_args.push_back(funcsPath);
    }
    // Black and Black 2 run on melonDS's JIT or its interpreter; so do SoulSilver and HeartGold in a build
    // with the JIT (tools/switch/build.sh with SSR_JIT=ON: about 1.7 times as fast as its
    // recompiled code; a build without the JIT runs the recompiled code, given neither option).
#ifdef JIT_ENABLED
    if (funcs || heartSilver)
    {
        // melonDS's JIT (code translated for the Switch's processor as it runs), unless the
        // L is held while the game starts, or the last start with the JIT never got going (a
        // file made before the start and removed a few seconds into the game says so).
        // Fast memory (the DS's memory mapped where the JIT's code reads it directly, see
        // melonDS/src/ARMJIT_Memory.cpp) likewise: if a start with it did not get going, the
        // next start goes without it, and the one after tries it again.
        bool jit = true, fastMem = true;
        if (FILE* f = fopen(cfgPath.c_str(), "r"))
        {
            char line[128];
            while (fgets(line, sizeof(line), f))
            {
                // (FAST JIT always: "jit=" and "fastmem=" from older versions' EMULATION
                // menu are not read any more, so no one is left on the slower choices.)
                // The widescreen drawing threads on core 0 too (melonDS/src/SsrThreads.h).
                if (!strncmp(line, "wide_core0=", 11) && line[11] == '0') setenv("SSR_WIDE_CORE0", "0", 1);
            }
            fclose(f);
        }
        PadState pad;
        padConfigureInput(1, HidNpadStyleSet_NpadStandard);
        padInitializeDefault(&pad);
        padUpdate(&pad);
        if (jit && (padGetButtons(&pad) & HidNpadButton_L))
        {
            jit = false;
            fprintf(stderr, "[switch] L held: the interpreter this time\n");
        }
        const std::string marker = dir + "/jit-starting", fastMemMarker = dir + "/fastmem-starting";
        if (stat(fastMemMarker.c_str(), &st) == 0)
        {
            // (Fast memory is the likelier cause, so the JIT stays on this time.)
            remove(fastMemMarker.c_str());
            remove(marker.c_str());
            if (jit && fastMem)
            {
                fastMem = false;
                fprintf(stderr, "[switch] the last start with fast memory did not get going: "
                    "the JIT without it this time\n");
            }
        }
        if (jit && stat(marker.c_str(), &st) == 0)
        {
            jit = false;
            remove(marker.c_str());
            fprintf(stderr, "[switch] the last start with the JIT did not get going: the interpreter this time\n");
        }
        if (jit)
        {
            if (FILE* f = fopen(marker.c_str(), "wb")) fclose(f);
            g_jitMarker = marker;
            if (fastMem)
            {
                if (FILE* f = fopen(fastMemMarker.c_str(), "wb")) fclose(f);
                g_fastMemMarker = fastMemMarker;
            }
        }
        // (Read by the core, melonDS/src/ARMJIT_Memory.cpp, and by MakeConsole.)
        if (!fastMem) setenv("SSR_JIT_NOFASTMEM", "1", 1);
        g_args.push_back(jit ? "--jit" : "--interp");
    }
#else
    if (funcs) g_args.push_back("--interp");
#endif
    if (testRun)
    {
        g_args.insert(g_args.end(), testArgs.begin(), testArgs.end());
        // Each frame is shown alongside the next one's emulation, as in play (see
        // "pipelined" in main.cpp), pictures or not.
        setenv("SSR_PIPELINE", "1", 1);
    }
    for (auto& a : g_args) g_argv.push_back(a.data());
    g_argv.push_back(nullptr);
    argc = (int)g_args.size();
    argv = g_argv.data();
    return true;
}

static bool DirectBoost(bool on);

bool CanChangeGame() { return g_canChange && envHasNextLoad(); }
void ChangeGame() { g_changeGame = true; }

bool SetCpuBoost(bool on)
{
    // Set the speeds directly, as sys-clk does, when the system allows it; otherwise ask for
    // boost mode, which needs system version 7.0.0 or later (on older ones the call fails
    // and the processor stays at its normal speed).
    if (on && DirectBoost(true))
    {
        g_boosted = true;
        return true;
    }
    if (!on) DirectBoost(false);
    const Result rc = appletSetCpuBoostMode(on ? ApmCpuBoostMode_FastLoad : ApmCpuBoostMode_Normal);
    if (R_FAILED(rc))
    {
        fprintf(stderr, "[switch] CPU boost %s failed: 0x%x\n", on ? "on" : "off", (unsigned)rc);
        if (!on) g_boosted = false;
        return !on;
    }
    g_boosted = on;
    fprintf(stderr, "[switch] CPU boost %s (system boost mode)\n", on ? "on" : "off");
    return true;
}

bool LocalTime(int& year, int& month, int& day, int& hour, int& minute, int& second)
{
    // newlib's localtime() knows no time zone here and gives UTC; the game lights the world
    // by the hour, so the console's own zone is asked for.
    u64 now = 0;
    if (R_FAILED(timeGetCurrentTime(TimeType_UserSystemClock, &now))) return false;
    TimeCalendarTime cal;
    TimeCalendarAdditionalInfo info;
    if (R_FAILED(timeToCalendarTimeWithMyRule(now, &cal, &info))) return false;
    year = cal.year;
    month = cal.month;
    day = cal.day;
    hour = cal.hour;
    minute = cal.minute;
    second = cal.second;
    return true;
}

// The clock services, opened on first use (false if the game may not use them).
static ClkrstSession g_cpuClock, g_gpuClock, g_memClock;
static bool OpenClocks()
{
    static bool tried = false, ok = false;
    if (!tried)
    {
        tried = true;
        if (hosversionAtLeast(8, 0, 0) && R_SUCCEEDED(clkrstInitialize()))
            ok = R_SUCCEEDED(clkrstOpenSession(&g_cpuClock, PcvModuleId_CpuBus, 3)) &&
                 R_SUCCEEDED(clkrstOpenSession(&g_gpuClock, PcvModuleId_GPU, 3)) &&
                 R_SUCCEEDED(clkrstOpenSession(&g_memClock, PcvModuleId_EMC, 3));
        if (!ok) fprintf(stderr, "[switch] clock rates cannot be read\n");
    }
    return ok;
}

// Boost mode is the system's to grant, and it takes it away for 10-20 seconds every minute
// or so (measured: the processor at 1020 MHz in each slow spell, with the chip at 35C), in
// SoulSilver too; asking for it again only shortened the slow spells. sys-clk, which sets the
// speeds itself and checks them every 0.3 seconds, had none. So the same is done here: the
// processor at 1785 MHz (boost mode's speed) and the memory at 1600 MHz (its docked speed;
// handheld mode runs it at 1331 and the emulation uses memory constantly), checked five times
// a second and set again if the system has lowered them. The graphics chip keeps its own
// speed (boost mode would turn it down to 76 MHz). When boost is turned off or the game ends,
// both go back to the speeds they had.
static bool g_direct = false;
static u32 g_cpuNormal = 0, g_memNormal = 0;
static constexpr u32 BoostCpuHz = 1785000000u, BoostMemHz = 1600000000u;

static bool DirectBoost(bool on)
{
    if (!on)
    {
        if (g_direct)
        {
            clkrstSetClockRate(&g_cpuClock, g_cpuNormal);
            clkrstSetClockRate(&g_memClock, g_memNormal);
            fprintf(stderr, "[switch] CPU boost off: processor %u MHz, memory %u MHz again\n", g_cpuNormal / 1000000, g_memNormal / 1000000);
        }
        g_direct = false;
        return true;
    }
    if (!OpenClocks()) return false;
    u32 cpu = 0, mem = 0;
    if (R_FAILED(clkrstGetClockRate(&g_cpuClock, &cpu)) || R_FAILED(clkrstGetClockRate(&g_memClock, &mem))) return false;
    if (!g_direct)
    {
        g_cpuNormal = cpu;
        g_memNormal = mem;
    }
    const Result rc1 = clkrstSetClockRate(&g_cpuClock, BoostCpuHz);
    const Result rc2 = clkrstSetClockRate(&g_memClock, BoostMemHz);
    u32 nowCpu = 0, nowMem = 0;
    clkrstGetClockRate(&g_cpuClock, &nowCpu);
    clkrstGetClockRate(&g_memClock, &nowMem);
    if (R_FAILED(rc1) || nowCpu < BoostCpuHz - 15000000u)
    {
        fprintf(stderr, "[switch] the processor speed cannot be set (0x%x); using the system's boost mode\n", (unsigned)rc1);
        if (R_SUCCEEDED(rc2)) clkrstSetClockRate(&g_memClock, g_memNormal);
        return false;
    }
    g_direct = true;
    fprintf(stderr, "[switch] CPU boost on: processor %u MHz (was %u), memory %u MHz (was %u)\n", nowCpu / 1000000,
        g_cpuNormal / 1000000, nowMem / 1000000, g_memNormal / 1000000);
    return true;
}

void KeepBoost(uint32_t frame)
{
    static int events = 0;
    if (!g_boosted || frame % 12 != 0 || !OpenClocks()) return;
    u32 cpu = 0, mem = 0;
    if (R_FAILED(clkrstGetClockRate(&g_cpuClock, &cpu)) || R_FAILED(clkrstGetClockRate(&g_memClock, &mem))) return;
    const bool cpuLow = cpu < BoostCpuHz - 15000000u, memLow = g_direct && mem < BoostMemHz - 15000000u;
    if (!cpuLow && !memLow) return;
    events++;
    if (events <= 10 || events % 50 == 0)
        fprintf(stderr, "[switch] the system lowered the %s to %u MHz (time %d); set back\n", cpuLow ? "processor" : "memory",
            (cpuLow ? cpu : mem) / 1000000, events);
    if (g_direct)
    {
        if (cpuLow) clkrstSetClockRate(&g_cpuClock, BoostCpuHz);
        if (memLow) clkrstSetClockRate(&g_memClock, BoostMemHz);
    }
    else
    {
        appletSetCpuBoostMode(ApmCpuBoostMode_Normal);
        appletSetCpuBoostMode(ApmCpuBoostMode_FastLoad);
    }
}

static Thread g_emuThread;

bool StartThread(void (*entry)(void*), void* arg)
{
    if (R_FAILED(threadCreate(&g_emuThread, entry, arg, nullptr, 0x400000, 0x2C, 0))) return false;
    if (R_FAILED(threadStart(&g_emuThread)))
    {
        threadClose(&g_emuThread);
        return false;
    }
    return true;
}

void JoinThread()
{
    threadWaitForExit(&g_emuThread);
    threadClose(&g_emuThread);
}

void MoveMainThread()
{
    const Result rc = svcSetThreadCoreMask(CUR_THREAD_HANDLE, 2, 1 << 2);
    fprintf(stderr, "[switch] main thread to core 2: 0x%x\n", (unsigned)rc);
}

void JitStarted()
{
    if (!g_fastMemMarker.empty())
    {
        remove(g_fastMemMarker.c_str());
        g_fastMemMarker.clear();
    }
    if (g_jitMarker.empty()) return;
    remove(g_jitMarker.c_str());
    g_jitMarker.clear();
    fprintf(stderr, "[switch] the JIT is running\n");
}

std::string Status()
{
    // Opened on first use; a service the game may not use just leaves its part out.
    static bool tried = false, ts = false, tsSessions = false;
    static TsSession soc, pcb;
    const bool clk = OpenClocks();
    ClkrstSession& cpu = g_cpuClock;
    ClkrstSession& gpu = g_gpuClock;
    ClkrstSession& emc = g_memClock;
    if (!tried)
    {
        tried = true;
        if (R_SUCCEEDED(tsInitialize()))
        {
            ts = true;
            if (hosversionAtLeast(10, 0, 0))
                tsSessions = R_SUCCEEDED(tsOpenSession(&soc, TsDeviceCode_LocationExternal)) &&
                             R_SUCCEEDED(tsOpenSession(&pcb, TsDeviceCode_LocationInternal));
        }
        if (!ts) fprintf(stderr, "[switch] temperatures cannot be read\n");
    }
    char buf[128];
    int n = 0;
    buf[0] = 0;
    u32 c = 0, g = 0, m = 0;
    if (clk && R_SUCCEEDED(clkrstGetClockRate(&cpu, &c)) && R_SUCCEEDED(clkrstGetClockRate(&gpu, &g)) &&
        R_SUCCEEDED(clkrstGetClockRate(&emc, &m)))
        n += snprintf(buf + n, sizeof(buf) - n, "cpu %u gpu %u mem %u MHz", c / 1000000, g / 1000000, m / 1000000);
    float socC = 0, pcbC = 0;
    bool haveTemp = false;
    if (tsSessions)
        haveTemp = R_SUCCEEDED(tsSessionGetTemperature(&soc, &socC)) && R_SUCCEEDED(tsSessionGetTemperature(&pcb, &pcbC));
    else if (ts)
    {
        s32 a = 0, b = 0;
        haveTemp = R_SUCCEEDED(tsGetTemperatureMilliC(TsLocation_External, &a)) &&
                   R_SUCCEEDED(tsGetTemperatureMilliC(TsLocation_Internal, &b));
        socC = a / 1000.0f;
        pcbC = b / 1000.0f;
    }
    if (haveTemp)
        n += snprintf(buf + n, sizeof(buf) - n, "%schip %.0fC board %.0fC", n ? ", " : "", socC, pcbC);
    return buf;
}

void ForceExit()
{
    fflush(stderr);
    svcExitProcess();
    for (;;) {}
}

void Finish()
{
    JitStarted(); // (an ordinary exit: the JIT did start)
    if (g_boosted) SetCpuBoost(false);
    // QUIT in the pause menu, for a game chosen from the game list: the Homebrew Menu's loader
    // starts the program again, which shows the list.
    if (g_changeGame && !g_self.empty())
    {
        const std::string cmd = g_self.find(' ') == std::string::npos ? g_self : "\"" + g_self + "\"";
        const Result rc = envSetNextLoad(g_self.c_str(), cmd.c_str());
        fprintf(stderr, "[switch] back to the game list: 0x%x\n", (unsigned)rc);
    }
    fflush(stderr);
    if (TestRun::Finish(g_test))
    {
        // The next run: the Homebrew Menu's loader starts the .nro again once this one ends
        // (if it cannot, starting it again by hand does the same).
        if (!g_self.empty() && envHasNextLoad())
        {
            const std::string cmd = g_self.find(' ') == std::string::npos ? g_self : "\"" + g_self + "\"";
            const Result rc = envSetNextLoad(g_self.c_str(), cmd.c_str());
            fprintf(stderr, "[test] starting again for the next run: 0x%x\n", (unsigned)rc);
        }
        else fprintf(stderr, "[test] start the game again for the next run\n");
        fflush(stderr);
    }
}
}
#endif
