# Writes a copy of a GLSL shader with #defines after its first line (the #version), for a
# variant of it: uam, which compiles the Switch's shaders, has no option to define anything.
#   cmake -DIN=<shader> -DOUT=<copy> -DDEFINE=<name>[,<name>...] -P glsl_define.cmake
file(READ "${IN}" text)
string(FIND "${text}" "\n" eol)
math(EXPR rest "${eol} + 1")
string(SUBSTRING "${text}" 0 ${rest} first)
string(SUBSTRING "${text}" ${rest} -1 body)
string(REPLACE "," ";" names "${DEFINE}")
set(defines "")
foreach(name ${names})
    string(APPEND defines "#define ${name} 1\n")
endforeach()
file(WRITE "${OUT}" "${first}${defines}#line 2\n${body}")
