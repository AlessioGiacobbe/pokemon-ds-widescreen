cmake_minimum_required(VERSION 3.16)
project(ssrecomp LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

# melonDS core is the hardware runtime; its own frontend, JIT and GL renderer are not used.
set(BUILD_QT_SDL OFF CACHE BOOL "" FORCE)
set(ENABLE_OGLRENDERER OFF CACHE BOOL "" FORCE)
set(ENABLE_JIT OFF CACHE BOOL "" FORCE)
set(ENABLE_GDBSTUB OFF CACHE BOOL "" FORCE)
set(ENABLE_LTO_RELEASE OFF CACHE BOOL "" FORCE)
add_subdirectory(melonDS EXCLUDE_FROM_ALL)

find_package(SDL2 REQUIRED)
find_package(PNG REQUIRED)

enable_language(ASM)

# Recompiled game code, produced by tools/recomp.py.
include(${CMAKE_CURRENT_SOURCE_DIR}/gen/sources.cmake)
add_library(gamecode OBJECT ${RC_GENERATED_SOURCES})
target_include_directories(gamecode PRIVATE melonDS/src runtime gen)
# Generated code is huge and uniform; -O1 keeps build times reasonable.
target_compile_options(gamecode PRIVATE -O1 -g0 -fno-exceptions -fno-rtti -Wa,-I${CMAKE_CURRENT_SOURCE_DIR}/gen)

add_executable(ssrecomp
    frontend/main.cpp
    frontend/platform.cpp
    runtime/recomp_rt.cpp
    gen/apps_table.cpp
    $<TARGET_OBJECTS:gamecode>)
target_include_directories(ssrecomp PRIVATE melonDS/src frontend runtime ${SDL2_INCLUDE_DIRS})
target_link_libraries(ssrecomp PRIVATE core ${SDL2_LIBRARIES} PNG::PNG pthread)
