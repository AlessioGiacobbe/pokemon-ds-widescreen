# SDL2 for the Windows build of the PC all-games program (tools/pc/build_all.sh): from SDL's own
# MinGW development package, of which only the headers, the import library, SDL2main and
# SDL2.dll are used. SSR_SDL2_ROOT is its x86_64-w64-mingw32 folder.
if(NOT SSR_SDL2_ROOT OR NOT EXISTS "${SSR_SDL2_ROOT}/include/SDL2/SDL.h")
    message(FATAL_ERROR "SSR_SDL2_ROOT must be the x86_64-w64-mingw32 folder of SDL2's MinGW development package")
endif()
set(SDL2_INCLUDE_DIRS "${SSR_SDL2_ROOT}/include;${SSR_SDL2_ROOT}/include/SDL2")
set(SDL2_LIBRARIES SDL2::SDL2)
if(NOT TARGET SDL2::SDL2)
    add_library(SDL2::SDL2 SHARED IMPORTED)
    set_target_properties(SDL2::SDL2 PROPERTIES
        IMPORTED_IMPLIB "${SSR_SDL2_ROOT}/lib/libSDL2.dll.a"
        IMPORTED_LOCATION "${SSR_SDL2_ROOT}/bin/SDL2.dll"
        INTERFACE_INCLUDE_DIRECTORIES "${SDL2_INCLUDE_DIRS}")
    # WinMain, which calls the program's main (renamed SDL_main by SDL.h); libmingw32 first, as
    # sdl2-config gives them, and SDL2 again after it (CMake lists a library only once).
    add_library(SDL2::SDL2main INTERFACE IMPORTED)
    set_target_properties(SDL2::SDL2main PROPERTIES
        INTERFACE_LINK_LIBRARIES "mingw32;${SSR_SDL2_ROOT}/lib/libSDL2main.a;${SSR_SDL2_ROOT}/lib/libSDL2.dll.a")
endif()
set(SDL2_FOUND TRUE)
