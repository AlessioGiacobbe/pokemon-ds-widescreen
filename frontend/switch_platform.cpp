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

namespace SwitchPlatform
{
namespace
{
std::vector<std::string> g_args;
std::vector<char*> g_argv;
bool g_boosted = false;

// Shows a message on the console's screen until + is pressed. Used only before SDL starts.
void ShowMessage(const std::string& text)
{
    fprintf(stderr, "%s\n", text.c_str());
    consoleInit(nullptr);
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    PadState pad;
    padInitializeDefault(&pad);
    printf("\n  ssrecomp\n\n%s\n\n  Press + to return to the Homebrew Menu.\n", text.c_str());
    while (appletMainLoop())
    {
        padUpdate(&pad);
        if (padGetButtonsDown(&pad) & HidNpadButton_Plus) break;
        consoleUpdate(nullptr);
    }
    consoleExit(nullptr);
}

bool EndsWithNds(const std::string& name)
{
    if (name.size() < 4) return false;
    std::string ext = name.substr(name.size() - 4);
    for (char& c : ext) c = (char)tolower((unsigned char)c);
    return ext == ".nds";
}

// rom.nds in the folder, or else the only .nds file there.
std::string FindRom(const std::string& dir, std::string& problem)
{
    struct stat st;
    if (stat((dir + "/rom.nds").c_str(), &st) == 0) return dir + "/rom.nds";
    std::vector<std::string> found;
    if (DIR* d = opendir(dir.c_str()))
    {
        while (dirent* e = readdir(d))
            if (EndsWithNds(e->d_name)) found.push_back(e->d_name);
        closedir(d);
    }
    if (found.size() == 1) return dir + "/" + found[0];
    if (found.empty())
        problem = "  No game found. Copy your Pokemon SoulSilver (US) ROM to\n  " + dir + "/rom.nds";
    else
        problem = "  More than one .nds file in\n  " + dir + "\n  Rename the one to play to rom.nds.";
    return "";
}
}

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

    // Messages go to game.log beside the game, as run.sh does on a PC, with the frame timing,
    // so that a log can be sent with a bug report.
    if (!freopen((dir + "/game.log").c_str(), "w", stderr)) freopen("game.log", "w", stderr);
    setvbuf(stderr, nullptr, _IOLBF, 0);
    setenv("SSR_PERF", "1", 0);
    fprintf(stderr, "[switch] started from %s\n", argc > 0 && argv[0] ? argv[0] : "?");

    // Started from the album (applet mode) a homebrew program only gets a few hundred MB of
    // memory; the game's code alone is larger. Started in place of a game (holding R) it gets
    // nearly all of it.
    const AppletType type = appletGetAppletType();
    fprintf(stderr, "[switch] applet type %d\n", (int)type);
    if (type != AppletType_Application && type != AppletType_SystemApplication)
    {
        ShowMessage("  This needs the console's full memory.\n"
                    "  Start it from a game instead of the album:\n"
                    "  hold R while starting any game, then choose ssrecomp.");
        return false;
    }
    u64 total = 0, used = 0;
    svcGetInfo(&total, InfoType_TotalMemorySize, CUR_PROCESS_HANDLE, 0);
    svcGetInfo(&used, InfoType_UsedMemorySize, CUR_PROCESS_HANDLE, 0);
    fprintf(stderr, "[switch] memory: %llu MB in use of %llu MB\n", (unsigned long long)(used >> 20),
        (unsigned long long)(total >> 20));

    // With arguments (e.g. sent with nxlink while testing) they are used as they are.
    if (argc >= 2) return true;

    std::string problem;
    std::string rom = FindRom(dir, problem);
    if (rom.empty())
    {
        ShowMessage(problem);
        return false;
    }
    g_args = { argc > 0 && argv[0] ? argv[0] : "ssrecomp", rom, "--save", dir + "/soulsilver.sav", "--fullscreen" };
    for (auto& a : g_args) g_argv.push_back(a.data());
    g_argv.push_back(nullptr);
    argc = (int)g_args.size();
    argv = g_argv.data();
    return true;
}

bool SetCpuBoost(bool on)
{
    // Boost mode needs system version 7.0.0 or later; on older ones the call fails and the
    // processor stays at its normal speed.
    const Result rc = appletSetCpuBoostMode(on ? ApmCpuBoostMode_FastLoad : ApmCpuBoostMode_Normal);
    if (R_FAILED(rc))
    {
        fprintf(stderr, "[switch] CPU boost %s failed: 0x%x\n", on ? "on" : "off", (unsigned)rc);
        return false;
    }
    g_boosted = on;
    fprintf(stderr, "[switch] CPU boost %s\n", on ? "on" : "off");
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

void Finish()
{
    if (g_boosted) SetCpuBoost(false);
    fflush(stderr);
}
}
#endif
