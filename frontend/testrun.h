// Test runs started from a file, for machines where the game cannot be started with
// arguments (the Switch): test.txt in the game's folder lists runs, each one a start of the
// game from a snapshot with scripted input, screenshots at chosen frames and a line of
// timings per frame, written to test-results/<run>/ with that run's game.log. The game
// then quits, and starts again for the next run (on the Switch by itself, see
// switch_platform.cpp; or by starting it again). When every run has been done, test.txt
// is renamed test-done.txt, so that the next start is an ordinary one.
//
//   # comment
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
// skipped, with the log it left.
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

inline std::vector<Run> Parse(const std::string& path, std::string& problems)
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
    bool more = false; // runs left after this one
};

// When dir/test.txt lists a run not yet done, its options are
// added to args (which already start the game: ROM, save, ...) and true is returned.
// Messages for the log go to notes.
inline bool Setup(const std::string& dir, std::vector<std::string>& args, State& st, std::string& notes)
{
    const std::string list = dir + "/test.txt";
    struct stat sb;
    if (stat(list.c_str(), &sb) != 0) return false;
    std::string problems;
    std::vector<Run> runs = Parse(list, problems);
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
            mkdir((results + "/" + r.name).c_str(), 0777);
            // (The caller has moved the log of the last start to game-last.log.)
            CopyFile(dir + "/game-last.log", results + "/" + r.name + "/game.log");
            notes += "[test] run " + r.name + " did not finish last time (its log is in test-results/" + r.name + ")\n";
            continue;
        }
        if (!chosen) chosen = &r;
        else left++;
    }
    if (!chosen)
    {
        // All done (the last one failed): an ordinary start.
        rename(list.c_str(), (dir + "/test-done.txt").c_str());
        notes += "[test] every run in test.txt has been done; it is now test-done.txt\n";
        return false;
    }
    st.active = true;
    st.dir = dir;
    st.name = chosen->name;
    st.results = results + "/" + chosen->name;
    st.more = left > 0;
    mkdir(st.results.c_str(), 0777);
    Note(progress, "started", st.name);
    // The game's own save and settings are left as they were.
    args.push_back("--no-save");
    args.push_back("--keep-settings");
    args.push_back("--relative");
    args.push_back("--shot-dir");
    args.push_back(st.results);
    args.push_back("--timing");
    args.push_back(st.results + "/timing.csv");
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

// At the end of a run: its log is copied beside its results. True if another run follows.
inline bool Finish(const State& st)
{
    if (!st.active) return false;
    fflush(stderr);
    CopyFile(st.dir + "/game.log", st.results + "/game.log");
    Note(st.dir + "/test-results/progress.txt", "done", st.name);
    if (!st.more)
    {
        rename((st.dir + "/test.txt").c_str(), (st.dir + "/test-done.txt").c_str());
        return false;
    }
    return true;
}
}
