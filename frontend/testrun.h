// Test runs started from a file, for machines where the game cannot be started with
// arguments (the Switch): test.txt in the game's folder lists runs, each one a start of the
// game from a snapshot with scripted input, screenshots at chosen frames and a line of
// timings per frame, written to test-results/<run>/ with that run's game.log. The game
// then quits; the next start does the next run (or, with "restart yes", the Switch build
// starts itself again at once, see switch_platform.cpp). When every run has been done,
// test.txt is renamed test-done.txt, so that the next start is an ordinary one.
//
//   # comment
//   restart yes               start again by itself after each run (default: one run a start)
//   run bridge-720            a new run, named for its folder in test-results
//   state bridge.st           snapshot to start from (in the game's folder; the pause menu's
//                             SAVE STATE writes <save>.state)
//   frames 600                how long to run, counted from the snapshot
//   shots 60,300,599          frames to take pictures of, counted from the snapshot
//   input walk.txt            input script (as --input, frames counted from the snapshot)
//   set wide_lines=720        a setting for this run only (as in ssrecomp.cfg)
//   arg --no-wide             any other option, as on the command line
//
// test-results/progress.txt records each run as it starts and ends: a run that started and
// never ended (it crashed or hung, and the console was restarted) is marked as failed and
// skipped. Each run's log is written in its folder from the start (the Switch cannot copy
// a file that is still open for writing), so a run that crashed keeps what it wrote.
#pragma once
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <vector>
#include <sys/stat.h>

namespace TestRun
{
struct Run
{
    std::string name;
    std::vector<std::string> args;
};

inline std::vector<Run> Parse(const std::string& path, std::string& problems, bool* restart = nullptr)
{
    std::vector<Run> runs;
    std::ifstream f(path);
    std::string line;
    int n = 0;
    while (std::getline(f, line))
    {
        n++;
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t')) line.pop_back();
        size_t start = line.find_first_not_of(" \t");
        if (start == std::string::npos || line[start] == '#') continue;
        line = line.substr(start);
        const size_t sp = line.find_first_of(" \t");
        const std::string key = line.substr(0, sp);
        std::string value = sp == std::string::npos ? "" : line.substr(line.find_first_not_of(" \t", sp));
        if (key == "restart")
        {
            if (restart) *restart = value == "yes" || value == "1";
            continue;
        }
        if (key == "run")
        {
            Run r;
            r.name = value.empty() ? "run" + std::to_string(runs.size() + 1) : value;
            // (A folder name.)
            for (char& c : r.name)
                if (c == '/' || c == '\\' || c == ':' || c == ' ') c = '_';
            runs.push_back(r);
            continue;
        }
        if (runs.empty()) runs.push_back({ "run1", {} });
        std::vector<std::string>& a = runs.back().args;
        if (key == "state") { a.push_back("--state-load"); a.push_back(value); }
        else if (key == "frames") { a.push_back("--frames"); a.push_back(value); }
        else if (key == "shots") { a.push_back("--shot-at"); a.push_back(value); }
        else if (key == "input") { a.push_back("--input"); a.push_back(value); }
        else if (key == "set") { a.push_back("--set"); a.push_back(value); }
        else if (key == "arg")
        {
            std::istringstream ss(value);
            std::string tok;
            while (ss >> tok) a.push_back(tok);
        }
        else problems += "test.txt line " + std::to_string(n) + ": unknown '" + key + "'\n";
    }
    return runs;
}

// What progress.txt says of each run: "started", "done" or "failed".
inline void ReadProgress(const std::string& path, std::set<std::string>& started, std::set<std::string>& finished)
{
    std::ifstream f(path);
    std::string what, name;
    while (f >> what >> name)
    {
        if (what == "started") started.insert(name);
        else finished.insert(name); // done, failed
    }
}

inline void Note(const std::string& path, const char* what, const std::string& name)
{
    if (FILE* f = fopen(path.c_str(), "a"))
    {
        fprintf(f, "%s %s\n", what, name.c_str());
        fclose(f);
    }
}

// A rename onto a file that is already there fails on the Switch's SD card: it goes first.
inline void Replace(const std::string& from, const std::string& to)
{
    remove(to.c_str());
    rename(from.c_str(), to.c_str());
}

inline bool CopyFile(const std::string& from, const std::string& to)
{
    FILE* in = fopen(from.c_str(), "rb");
    if (!in) return false;
    FILE* out = fopen(to.c_str(), "wb");
    if (!out)
    {
        fclose(in);
        return false;
    }
    char buf[16384];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) fwrite(buf, 1, n, out);
    fclose(in);
    fclose(out);
    return true;
}

struct State
{
    bool active = false;
    std::string dir, name, results;
    bool more = false;    // runs left after this one
    bool restart = false; // start again at once for the next
};

// Before the log is opened: when dir/test.txt lists a run not yet done, its options are
// added to args and true is returned; the log is then st.results + "/game.log". Messages for
// the log go to notes.
inline bool Setup(const std::string& dir, std::vector<std::string>& args, State& st, std::string& notes)
{
    const std::string list = dir + "/test.txt";
    struct stat sb;
    if (stat(list.c_str(), &sb) != 0) return false;
    std::string problems;
    bool restart = false;
    std::vector<Run> runs = Parse(list, problems, &restart);
    notes += problems;
    const std::string results = dir + "/test-results";
    mkdir(results.c_str(), 0777);
    const std::string progress = results + "/progress.txt";
    std::set<std::string> started, finished;
    ReadProgress(progress, started, finished);
    const Run* chosen = nullptr;
    size_t left = 0;
    for (const Run& r : runs)
    {
        if (finished.count(r.name)) continue;
        if (started.count(r.name))
        {
            // Started and never finished: what it left in the log is kept with it.
            Note(progress, "failed", r.name);
            finished.insert(r.name);
            notes += "[test] run " + r.name + " did not finish last time (its log is in test-results/" + r.name + ")\n";
            continue;
        }
        if (!chosen) chosen = &r;
        else left++;
    }
    if (!chosen)
    {
        // All done (the last one failed): an ordinary start.
        Replace(list, dir + "/test-done.txt");
        notes += "[test] every run in test.txt has been done; it is now test-done.txt\n";
        return false;
    }
    // A snapshot that is not there yet (test.txt copied before SAVE STATE was used): an
    // ordinary start, so that it can be made; the runs wait until it is there.
    for (size_t i = 0; i + 1 < chosen->args.size(); i++)
    {
        if (chosen->args[i] != "--state-load") continue;
        const std::string& f = chosen->args[i + 1];
        const std::string path = f.find(':') == std::string::npos && f[0] != '/' ? dir + "/" + f : f;
        if (stat(path.c_str(), &sb) != 0)
        {
            notes += "[test] " + f + " is not there yet (the pause menu's SAVE STATE makes it): an ordinary start; "
                "the test runs start once it is there\n";
            return false;
        }
    }
    st.active = true;
    st.dir = dir;
    st.name = chosen->name;
    st.results = results + "/" + chosen->name;
    st.more = left > 0;
    st.restart = restart;
    mkdir(st.results.c_str(), 0777);
    Note(progress, "started", st.name);
    // The game's own save and settings are left as they were.
    args.push_back("--no-save");
    args.push_back("--keep-settings");
    args.push_back("--test-run");
    args.push_back("--relative");
    args.push_back("--shot-dir");
    args.push_back(st.results);
    args.push_back("--timing");
    args.push_back(st.results + "/timing.csv");
    // Pictures put together by the processor (merged.h, the same picture as the GPU's): reading
    // the screen back took 12 seconds a picture on the Switch.
    args.push_back("--compose");
    args.push_back("1280x720");
    args.push_back("--no-window-shots");
    for (size_t i = 0; i < chosen->args.size(); i++)
    {
        std::string a = chosen->args[i];
        // Files named in test.txt are in the game's folder.
        if (i > 0 && (chosen->args[i - 1] == "--state-load" || chosen->args[i - 1] == "--input") && a.find(':') == std::string::npos &&
            a[0] != '/')
            a = dir + "/" + a;
        args.push_back(a);
    }
    notes += "[test] run " + st.name + (st.more ? " (more to come)" : " (the last)") + ":";
    for (const std::string& a : chosen->args) notes += " " + a;
    notes += "\n";
    return true;
}

// At the end of a run. True if the program should start again at once for the next one.
inline bool Finish(const State& st)
{
    if (!st.active) return false;
    fflush(stderr);
    Note(st.dir + "/test-results/progress.txt", "done", st.name);
    if (!st.more)
    {
        Replace(st.dir + "/test.txt", st.dir + "/test-done.txt");
        return false;
    }
    return st.restart;
}
}
