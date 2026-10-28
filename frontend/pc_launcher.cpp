// The PC all-games program's game list. See pc_launcher.h.
#ifdef SSR_PC_LAUNCHER
#include "pc_launcher.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>
#include <sys/stat.h>

#ifdef __APPLE__
#include <SDL.h>
#else
#include <SDL2/SDL.h>
#endif
#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#else
#include <unistd.h>
#endif

// (stb_image's code here, for the list's pictures: only JPEG and PNG.)
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#include "thirdparty/stb_image.h"
#undef STB_IMAGE_IMPLEMENTATION
#include "launcher.h"

// How many chunks the program's SoulSilver tables have (none in a build without them).
extern const uint32_t rc_num_chunks;

namespace PcLauncher
{
namespace
{
std::vector<std::string> g_args;
std::vector<char*> g_argv;
bool g_canChange = false, g_change = false;
std::string g_name;

bool Exists(const std::string& path)
{
    struct stat st;
    return stat(path.c_str(), &st) == 0;
}

void MakeDir(const std::string& path)
{
#ifdef _WIN32
    _mkdir(path.c_str());
#else
    mkdir(path.c_str(), 0777);
#endif
}

// The program's own folder, without the slash at its end. (On Windows the program is built
// with a manifest that makes the C library's file names UTF-8, so that folders with any
// letters in their names work.)
std::string ProgramDir()
{
    std::string dir = ".";
    if (char* base = SDL_GetBasePath())
    {
        dir = base;
        SDL_free(base);
    }
    while (dir.size() > 1 && (dir.back() == '/' || dir.back() == '\\')) dir.pop_back();
    return dir;
}

// The settings file's fullscreen and window lines, so that the list opens as the game last was.
bool ConfigFullscreen(const std::string& cfg, int& w, int& h)
{
    bool on = false;
    if (FILE* f = fopen(cfg.c_str(), "r"))
    {
        char line[256];
        while (fgets(line, sizeof line, f))
        {
            if (!strncmp(line, "fullscreen=", 11)) on = line[11] == '1';
            int ww = 0, wh = 0;
            if (sscanf(line, "window=%dx%d", &ww, &wh) == 2 && ww >= 320 && wh >= 240 && ww <= 16384 && wh <= 16384)
            {
                w = ww;
                h = wh;
            }
        }
        fclose(f);
    }
    return on;
}

// F11 in the list: the game then starts the same way (the settings file says so).
void SetConfigFullscreen(const std::string& cfg, bool on)
{
    std::vector<std::string> lines;
    bool found = false;
    if (FILE* f = fopen(cfg.c_str(), "r"))
    {
        char line[1024];
        while (fgets(line, sizeof line, f))
        {
            std::string l = line;
            if (!strncmp(line, "fullscreen=", 11))
            {
                l = std::string("fullscreen=") + (on ? "1" : "0") + "\n";
                found = true;
            }
            lines.push_back(l);
        }
        fclose(f);
    }
    if (!found) lines.push_back(std::string("fullscreen=") + (on ? "1" : "0") + "\n");
    if (FILE* f = fopen(cfg.c_str(), "w"))
    {
        for (const std::string& l : lines) fputs(l.c_str(), f);
        fclose(f);
    }
}

struct Folders
{
    std::string dir, roms, saves, funcs, cfg;
};

// Whether this program plays a game: SoulSilver and HeartGold need the program's own tables,
// the others their function table in data/funcs.
bool Playable(const Folders& fo, const Launcher::Game& g)
{
    if (!g.known) return false;
    if (g.code == "IPGE" || g.code == "IPKE") return rc_num_chunks > 0;
    return Exists(fo.funcs + "/" + g.code + ".txt");
}

// The ROMs in the roms folder (and any left beside the program), their saves in saves/.
std::vector<Launcher::Game> FindGames(const Folders& fo, std::map<std::string, std::vector<uint32_t>>& artCache)
{
    std::vector<Launcher::Game> games;
    for (const std::string& dir : { fo.roms, fo.dir })
    {
        DIR* d = opendir(dir.c_str());
        if (!d) continue;
        std::vector<std::string> names;
        while (dirent* e = readdir(d))
            if (Launcher::EndsWithNds(e->d_name)) names.push_back(e->d_name);
        closedir(d);
        std::sort(names.begin(), names.end());
        for (const std::string& n : names)
        {
            Launcher::Game g;
            if (!Launcher::ReadGame(dir + "/" + n, fo.saves, g)) continue;
            // A picture of the player's own (icons/IREO.jpg, or Black 2.jpg beside Black 2.nds),
            // read once.
            auto it = artCache.find(g.rom);
            if (it == artCache.end())
            {
                Launcher::FindArt(g, fo.dir);
                it = artCache.emplace(g.rom, g.art).first;
            }
            g.art = it->second;
            games.push_back(std::move(g));
        }
    }
    Launcher::Sort(games);
    return games;
}

// The list's look: near-black with white text and red details (the Switch's stays navy and
// gold, launcher.h's default).
Launcher::Theme PcTheme()
{
    Launcher::Theme t;
    t.bg = 0xFF0B0B0C;
    t.band = 0xFF141416;
    t.bandLine = 0xFFE0242C;
    t.title = 0xFFFFFFFF;
    t.tile = 0xFF1A1A1D;
    t.frame = 0xFFE0242C;
    t.name = 0xFFB8B8BC;
    t.nameSel = 0xFFFFFFFF;
    t.sub = 0xFF707076;
    t.subSel = 0xFFE0242C;
    t.bad = 0xFFE0242C;
    t.hint = 0xFF8A8A90;
    return t;
}

void OpenFolder(const std::string& path)
{
    std::string url = "file://";
    if (path.empty() || path[0] != '/') url += "/"; // (C:/... on Windows)
    for (char c : path) url += c == '\\' ? '/' : c;
#if SDL_VERSION_ATLEAST(2, 0, 14)
    if (SDL_OpenURL(url.c_str()) != 0) fprintf(stderr, "[launcher] could not open %s: %s\n", url.c_str(), SDL_GetError());
#endif
}

// The list in its own window until a game is chosen: its index in games (which is updated as
// ROMs come and go), or -1 when the window is closed.
int PickGame(const Folders& fo, std::vector<Launcher::Game>& games, int selected, bool& fullscreen, int winW, int winH)
{
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) != 0)
    {
        fprintf(stderr, "[launcher] SDL_Init: %s\n", SDL_GetError());
        return -1;
    }
    SDL_Window* window = SDL_CreateWindow("Pokemon DS Widescreen", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, winW, winH,
        SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI | (fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0));
    SDL_Renderer* renderer = window ? SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC) : nullptr;
    if (window && !renderer) renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    if (!renderer)
    {
        fprintf(stderr, "[launcher] no window: %s\n", SDL_GetError());
        if (window) SDL_DestroyWindow(window);
        SDL_Quit();
        return -1;
    }
    SDL_Texture* texture = nullptr;
    Canvas canvas;
    std::map<std::string, std::vector<uint32_t>> artCache;
    std::vector<SDL_GameController*> pads;
    int first = 0, result = -1, cols = 3;
    std::string note;
    // Held directions repeat, as on the Switch.
    int held = 0, heldDir = 0;
    Uint32 lastScan = SDL_GetTicks();
    bool quit = false;
    auto choose = [&]() {
        if (games.empty()) return;
        if (Playable(fo, games[selected]))
        {
            result = selected;
            quit = true;
        }
        else
            note = games[selected].known ? "THIS DOWNLOAD DOES NOT HAVE THIS GAME" : "THIS GAME IS NOT ONE THAT THIS PLAYS";
    };
    auto move = [&](int dir) { // 1 left, 2 right, 3 up, 4 down
        if (games.empty()) return;
        int to = selected;
        if (dir == 1) to = selected - 1;
        if (dir == 2) to = selected + 1;
        if (dir == 3) to = selected - cols;
        if (dir == 4) to = std::min(selected + cols, (int)games.size() - 1);
        if (to != selected && to >= 0 && to < (int)games.size())
        {
            selected = to;
            note.clear();
        }
    };
    while (!quit)
    {
        SDL_Event e;
        bool any = SDL_WaitEventTimeout(&e, 50);
        while (any)
        {
            switch (e.type)
            {
            case SDL_QUIT:
                quit = true;
                break;
            case SDL_CONTROLLERDEVICEADDED:
                if (SDL_GameController* p = SDL_GameControllerOpen(e.cdevice.which)) pads.push_back(p);
                break;
            case SDL_KEYDOWN:
                switch (e.key.keysym.sym)
                {
                case SDLK_LEFT: case SDLK_a: move(1); break;
                case SDLK_RIGHT: case SDLK_d: move(2); break;
                case SDLK_UP: case SDLK_w: move(3); break;
                case SDLK_DOWN: case SDLK_s: move(4); break;
                case SDLK_RETURN: case SDLK_KP_ENTER: case SDLK_SPACE: if (!e.key.repeat) choose(); break;
                case SDLK_ESCAPE: quit = true; break;
                case SDLK_o: OpenFolder(fo.roms); break;
                case SDLK_F11:
                    fullscreen = !fullscreen;
                    SDL_SetWindowFullscreen(window, fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
                    SetConfigFullscreen(fo.cfg, fullscreen);
                    break;
                default: break;
                }
                break;
            case SDL_CONTROLLERBUTTONDOWN:
                switch (e.cbutton.button)
                {
                case SDL_CONTROLLER_BUTTON_A: case SDL_CONTROLLER_BUTTON_START: choose(); break;
                case SDL_CONTROLLER_BUTTON_B: case SDL_CONTROLLER_BUTTON_BACK: quit = true; break;
                default: break;
                }
                break;
            case SDL_MOUSEMOTION:
            case SDL_MOUSEBUTTONDOWN:
            {
                // (Window points to the canvas's pixels, which are more on a high-DPI screen.)
                int ww = 1, wh = 1;
                SDL_GetWindowSize(window, &ww, &wh);
                const int mx = e.type == SDL_MOUSEMOTION ? e.motion.x : e.button.x;
                const int my = e.type == SDL_MOUSEMOTION ? e.motion.y : e.button.y;
                const int i = Launcher::TileAt(canvas, (int)games.size(), first, mx * canvas.w / ww, my * canvas.h / wh);
                if (i >= 0 && i != selected && (e.type == SDL_MOUSEBUTTONDOWN || (e.motion.xrel || e.motion.yrel)))
                {
                    selected = i;
                    note.clear();
                }
                if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT && i >= 0 && e.button.clicks >= 2) choose();
                break;
            }
            case SDL_MOUSEWHEEL:
                move(e.wheel.y > 0 ? 3 : 4);
                break;
            default:
                break;
            }
            any = SDL_PollEvent(&e);
        }
        if (quit) break;
        // Held directions on any controller (the D-pad or the left stick) repeat.
        int dir = 0;
        for (SDL_GameController* p : pads)
        {
            const int ax = SDL_GameControllerGetAxis(p, SDL_CONTROLLER_AXIS_LEFTX), ay = SDL_GameControllerGetAxis(p, SDL_CONTROLLER_AXIS_LEFTY);
            if (SDL_GameControllerGetButton(p, SDL_CONTROLLER_BUTTON_DPAD_LEFT) || ax < -16000) dir = 1;
            else if (SDL_GameControllerGetButton(p, SDL_CONTROLLER_BUTTON_DPAD_RIGHT) || ax > 16000) dir = 2;
            else if (SDL_GameControllerGetButton(p, SDL_CONTROLLER_BUTTON_DPAD_UP) || ay < -16000) dir = 3;
            else if (SDL_GameControllerGetButton(p, SDL_CONTROLLER_BUTTON_DPAD_DOWN) || ay > 16000) dir = 4;
            if (dir) break;
        }
        held = dir && dir == heldDir ? held + 1 : dir ? 1 : 0;
        heldDir = dir;
        if (held == 1 || (held > 8 && held % 3 == 0)) move(dir);

        // ROMs put in the folder (or taken out) show up by themselves.
        if (SDL_GetTicks() - lastScan > 1500)
        {
            lastScan = SDL_GetTicks();
            const std::string current = games.empty() ? std::string() : games[selected].rom;
            games = FindGames(fo, artCache);
            selected = 0;
            for (size_t i = 0; i < games.size(); i++)
                if (games[i].rom == current) selected = (int)i;
        }

        int ow = 1280, oh = 720;
        SDL_GetRendererOutputSize(renderer, &ow, &oh);
        if (ow != canvas.w || oh != canvas.h || !texture)
        {
            canvas.Resize(ow, oh);
            if (texture) SDL_DestroyTexture(texture);
            texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, ow, oh);
        }
        const std::string hint = games.empty() ? "O: OPEN THE ROMS FOLDER    ESC: QUIT" : "ENTER OR A: PLAY    O: ROMS FOLDER    ESC: QUIT";
        static const Launcher::Theme theme = PcTheme();
        cols = Launcher::Draw(canvas, games, selected, first, note, hint, theme);
        if (games.empty())
        {
            const int s = std::max(1, canvas.h / 240);
            const char* lines[] = { "NO GAMES FOUND", "",
                                    "PUT YOUR ROMS IN THE ROMS FOLDER BESIDE THIS PROGRAM.", "THEY SHOW UP HERE BY THEMSELVES.", "",
                                    "IT PLAYS POKEMON BLACK / WHITE / BLACK 2 / WHITE 2 (ENGLISH)",
                                    "SOULSILVER / HEARTGOLD (US) AND PLATINUM (ENGLISH)" };
            int y = canvas.h / 3;
            for (const char* l : lines)
            {
                const int ls = !strcmp(l, "NO GAMES FOUND") ? s + 1 : s;
                const int ts = Canvas::TextWidth(l, ls) <= canvas.w - 8 ? ls : std::max(1, ls - 1);
                canvas.Text((canvas.w - Canvas::TextWidth(l, ts)) / 2, y, l, ts, ls > s ? theme.title : theme.name);
                y += 12 * s;
            }
        }
        SDL_UpdateTexture(texture, nullptr, canvas.px.data(), canvas.w * 4);
        SDL_RenderClear(renderer);
        SDL_RenderCopy(renderer, texture, nullptr, nullptr);
        SDL_RenderPresent(renderer);
    }
    for (SDL_GameController* p : pads) SDL_GameControllerClose(p);
    if (texture) SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return result;
}
}

bool Start(int& argc, char**& argv)
{
    // With arguments (a ROM given by hand, the test harness) the program runs as it always has.
    if (argc >= 2) return true;
    Folders fo;
    fo.dir = ProgramDir();
    fo.roms = fo.dir + "/roms";
    fo.saves = fo.dir + "/saves";
    fo.funcs = fo.dir + "/data/funcs";
    fo.cfg = fo.dir + "/settings.cfg";
    MakeDir(fo.roms);
    MakeDir(fo.saves);

    // The log goes to game.log beside the program (the last game's is kept as game-previous.log:
    // QUIT starts the program again for the list, whose few lines would otherwise replace it).
    const std::string logPath = fo.dir + "/game.log";
    struct stat ls;
    if (stat(logPath.c_str(), &ls) == 0 && ls.st_size > 1024)
    {
        const std::string prev = fo.dir + "/game-previous.log";
        remove(prev.c_str());
        rename(logPath.c_str(), prev.c_str());
    }
    if (freopen(logPath.c_str(), "w", stderr)) setvbuf(stderr, nullptr, _IOLBF, 0);
    fprintf(stderr, "[launcher] folder %s\n", fo.dir.c_str());

    std::map<std::string, std::vector<uint32_t>> artCache;
    std::vector<Launcher::Game> games = FindGames(fo, artCache);
    // The list starts on the game played last.
    const std::string lastPath = fo.dir + "/last-game.txt";
    int chosen = 0;
    if (FILE* f = fopen(lastPath.c_str(), "r"))
    {
        char line[2048] = {};
        if (fgets(line, sizeof line, f))
        {
            std::string last = line;
            while (!last.empty() && (last.back() == '\n' || last.back() == '\r')) last.pop_back();
            for (size_t i = 0; i < games.size(); i++)
                if (games[i].rom == last) chosen = (int)i;
        }
        fclose(f);
    }
    int winW = 1280, winH = 720;
    bool fullscreen = ConfigFullscreen(fo.cfg, winW, winH);
    chosen = PickGame(fo, games, chosen, fullscreen, winW, winH);
    if (chosen < 0) return false;
    g_canChange = true;
    const Launcher::Game& game = games[chosen];
    if (const Launcher::KnownGame* k = Launcher::Known(game.code)) g_name = k->name;
    if (FILE* f = fopen(lastPath.c_str(), "w"))
    {
        fprintf(f, "%s\n", game.rom.c_str());
        fclose(f);
    }
    fprintf(stderr, "[launcher] %s (game code %s), save %s/%s\n", game.rom.c_str(), game.code.c_str(), fo.saves.c_str(),
        game.saveName.c_str());
    g_args = { argc > 0 && argv[0] ? argv[0] : "pokemon-ds-wide", game.rom, "--save", fo.saves + "/" + game.saveName,
               "--config", fo.cfg, "--jit" };
    // SoulSilver and HeartGold take their layouts from the program's own tables and must not be
    // given a function table (one switches to the Black games' layouts); the others take theirs.
    if (game.code != "IPGE" && game.code != "IPKE")
    {
        g_args.push_back("--funcs");
        g_args.push_back(fo.funcs + "/" + game.code + ".txt");
    }
    for (auto& a : g_args) g_argv.push_back(a.data());
    g_argv.push_back(nullptr);
    argc = (int)g_args.size();
    argv = g_argv.data();
    return true;
}

bool CanChangeGame() { return g_canChange; }
const char* GameName() { return g_name.c_str(); }
void ChangeGame() { g_change = true; }

void Finish()
{
    if (!g_change) return;
    fflush(stderr);
    // The program again, without arguments: the list.
#ifdef _WIN32
    wchar_t self[32768];
    const DWORD n = GetModuleFileNameW(nullptr, self, 32768);
    if (n == 0 || n >= 32768) return;
    std::wstring cmd = L"\"" + std::wstring(self) + L"\"";
    STARTUPINFOW si = {};
    si.cb = sizeof si;
    PROCESS_INFORMATION pi = {};
    if (CreateProcessW(self, &cmd[0], nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi))
    {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
#else
    char self[4096];
    const ssize_t n = readlink("/proc/self/exe", self, sizeof self - 1);
    if (n <= 0) return;
    self[n] = 0;
    execl(self, self, (char*)nullptr);
#endif
}
}
#endif
