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
    // The save is named after the game (SoulSilver's name is the one earlier builds used).
    char code[5] = {};
    if (FILE* f = fopen(rom.c_str(), "rb"))
    {
        if (fseek(f, 0x0C, SEEK_SET) != 0 || fread(code, 1, 4, f) != 4) code[0] = 0;
        fclose(f);
    }
    std::string saveName = "game.sav";
    if (!strcmp(code, "IPGE")) saveName = "soulsilver.sav";
    else if (!strcmp(code, "IREO")) saveName = "black2.sav";
    else if (!strcmp(code, "IRBO")) saveName = "black.sav";
    fprintf(stderr, "[switch] game code %s, save %s\n", code[0] ? code : "?", saveName.c_str());
    g_args = { argc > 0 && argv[0] ? argv[0] : "ssrecomp", rom, "--save", dir + "/" + saveName, "--fullscreen" };
    // A game that is not recompiled (Black 2) comes with its function table, made on the PC
    // by tools/bw2/funcmap.py; its layouts follow the functions that run.
    struct stat st;
    if (stat((dir + "/funcs.txt").c_str(), &st) == 0)
    {
        g_args.push_back("--funcs");
        g_args.push_back(dir + "/funcs.txt");
        g_args.push_back("--interp");
    }
    for (auto& a : g_args) g_argv.push_back(a.data());
    g_argv.push_back(nullptr);
    argc = (int)g_args.size();
    argv = g_argv.data();
    return true;
}

static void UnforceClock();

bool SetCpuBoost(bool on)
{
    if (!on) UnforceClock();
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
// or so (measured: the processor at 1020 MHz in each slow spell, with the chip at 35C).
// So the speed is checked twice a second: when it has dropped, boost mode is asked for
// again, and if that does not bring it back, the processor is set to the boost speed
// directly, as sys-clk does.
static bool g_clockForced = false;
void KeepBoost(uint32_t frame)
{
    static int lowChecks = 0, events = 0;
    if (!g_boosted || frame % 30 != 0 || !OpenClocks()) return;
    u32 hz = 0;
    if (R_FAILED(clkrstGetClockRate(&g_cpuClock, &hz))) return;
    if (hz >= 1700000000u)
    {
        lowChecks = 0;
        return;
    }
    events++;
    const bool say = events <= 10 || events % 50 == 0;
    if (lowChecks++ == 0)
    {
        appletSetCpuBoostMode(ApmCpuBoostMode_Normal);
        appletSetCpuBoostMode(ApmCpuBoostMode_FastLoad);
        if (say) fprintf(stderr, "[switch] processor dropped to %u MHz (time %d): boost mode asked for again\n", hz / 1000000, events);
    }
    else
    {
        const Result rc = clkrstSetClockRate(&g_cpuClock, 1785000000u);
        if (R_SUCCEEDED(rc)) g_clockForced = true;
        if (say) fprintf(stderr, "[switch] processor still at %u MHz: set to 1785 MHz directly (%s)\n", hz / 1000000,
            R_SUCCEEDED(rc) ? "done" : "refused");
    }
}

static void UnforceClock()
{
    // A speed set directly stays set: back to the normal one before boost mode ends.
    if (g_clockForced) clkrstSetClockRate(&g_cpuClock, 1020000000u);
    g_clockForced = false;
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

void Finish()
{
    if (g_boosted) SetCpuBoost(false);
    fflush(stderr);
}
}
#endif
