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
#ifdef JIT_ENABLED
        // melonDS's JIT (code translated for the Switch's processor as it runs), unless the
        // settings say otherwise (EMULATION in the pause menu, "jit=0" in ssrecomp.cfg), L is
        // held while the game starts, or the last start with the JIT never got going (a file
        // made before the start and removed a few seconds into the game says so).
        // Fast memory (the DS's memory mapped where the JIT's code reads it directly, see
        // melonDS/src/ARMJIT_Memory.cpp) likewise, from "fastmem=0"; and if a start with it did
        // not get going, it is turned off in the settings (it can go back on in the menu).
        bool jit = true, fastMem = true;
        const std::string cfgPath = dir + "/ssrecomp.cfg";
        if (FILE* f = fopen(cfgPath.c_str(), "r"))
        {
            char line[128];
            while (fgets(line, sizeof(line), f))
            {
                if (!strncmp(line, "jit=", 4)) jit = line[4] != '0';
                if (!strncmp(line, "fastmem=", 8)) fastMem = line[8] != '0';
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
                if (FILE* f = fopen(cfgPath.c_str(), "a"))
                {
                    fputs("fastmem=0\n", f);
                    fclose(f);
                }
                fprintf(stderr, "[switch] the last start with fast memory did not get going: turned off "
                    "(EMULATION in the pause menu turns it back on)\n");
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
#else
        g_args.push_back("--interp");
#endif
    }
    for (auto& a : g_args) g_argv.push_back(a.data());
    g_argv.push_back(nullptr);
    argc = (int)g_args.size();
    argv = g_argv.data();
    return true;
}

static bool DirectBoost(bool on);

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

void Finish()
{
    JitStarted(); // (an ordinary exit: the JIT did start)
    if (g_boosted) SetCpuBoost(false);
    fflush(stderr);
}
}
#endif
