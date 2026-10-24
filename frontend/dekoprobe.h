// ssrecomp: a first test of deko3D, the Switch's own graphics API (what melonDS's Switch port
// drew with), before the widescreen world is drawn with it. Run with --deko-probe (on the
// Switch from test.txt: "arg --deko-probe") once the window is up: it makes a deko3D device
// beside SDL's OpenGL, draws a picture with a shader that reads a uniform buffer and a
// storage buffer, reads it back two ways (drawn into a GPU image and copied, or drawn straight
// into memory the processor reads through its cache), checks it and logs the times, then
// releases everything and the game goes on as usual. Only in Switch builds with deko3d and
// uam (devkitPro's switch-dev); SSR_DEKO3D says so.
#pragma once

namespace DekoProbe
{
void Run(int w, int h);
}
