# The HOME Menu entries' program (frontend/switch_forwarder.cpp installs one per game): sphaira's
# homebrew loader (https://github.com/ITotalJustice/sphaira, hbl/ at 681f1b2a7ae8941cfd9afe2ad6dcd79cd3ddc5f7; GPL-3.0, based on
# nx-hbloader, ISC: nx-hbloader.LICENSE.md), which starts the .nro named in its romfs
# (nextNroPath) with the arguments there (nextArgv). Built by tools/switch/build_forwarder.sh into
# the all-games program's romfs as forwarder/main and forwarder/main.npdm.
#
#   cmake -S tools/switch/forwarder -B <build> -G Ninja -DCMAKE_TOOLCHAIN_FILE=$DEVKITPRO/cmake/Switch.cmake -DSSR_FORWARDER_OUT=<folder>
cmake_minimum_required(VERSION 3.13)
project(hbl VERSION 3.0.0 LANGUAGES ASM C)

add_executable(hbl source/main.c source/trampoline.s)
set_target_properties(hbl PROPERTIES C_STANDARD 11 C_EXTENSIONS ON)
target_link_options(hbl PRIVATE -Wl,-wrap,exit)
target_compile_definitions(hbl PRIVATE VERSION="${CMAKE_PROJECT_VERSION}")
target_link_libraries(hbl PRIVATE nx)

find_program(SSR_ELF2NSO NAMES elf2nso HINTS "${DEVKITPRO}/tools/bin" REQUIRED)
find_program(SSR_NPDMTOOL NAMES npdmtool HINTS "${DEVKITPRO}/tools/bin" REQUIRED)
set(SSR_FORWARDER_OUT "${CMAKE_CURRENT_BINARY_DIR}/exefs" CACHE PATH "where main and main.npdm go")
file(MAKE_DIRECTORY "${SSR_FORWARDER_OUT}")
add_custom_command(OUTPUT "${SSR_FORWARDER_OUT}/main"
    COMMAND "${SSR_ELF2NSO}" "$<TARGET_FILE:hbl>" "${SSR_FORWARDER_OUT}/main"
    DEPENDS hbl VERBATIM)
add_custom_command(OUTPUT "${SSR_FORWARDER_OUT}/main.npdm"
    COMMAND "${SSR_NPDMTOOL}" "${CMAKE_CURRENT_SOURCE_DIR}/hbl.json" "${SSR_FORWARDER_OUT}/main.npdm"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/hbl.json" VERBATIM)
add_custom_target(exefs ALL DEPENDS "${SSR_FORWARDER_OUT}/main" "${SSR_FORWARDER_OUT}/main.npdm")
