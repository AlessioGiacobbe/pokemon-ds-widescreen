# Writes a C header holding a file's bytes (a compiled shader) as an array.
#   cmake -DIN=<file> -DOUT=<header> -DNAME=<array name> -P embed.cmake
file(READ "${IN}" hex HEX)
string(LENGTH "${hex}" len)
math(EXPR size "${len} / 2")
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," bytes "${hex}")
file(WRITE "${OUT}" "// Made from ${IN} by tools/switch/embed.cmake.\n#pragma once\nalignas(256) static const unsigned char ${NAME}[${size}] = {\n${bytes}\n};\n")
