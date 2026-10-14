// ssrecomp: widescreen, full-resolution rendering of the 3D scene.
//
// The DS draws its 3D scene 256x192 pixels large through a 4:3 view. For a wide display
// the frame's polygons are submitted a second time with the horizontal clip coordinate
// squeezed, so that what lies left and right of the original view survives clipping, and
// are then drawn by a separate rasteriser at the size of the output. The emulated state
// is not touched: the game sees exactly the polygon counts and results it would see
// without any of this.
#pragma once
#include <vector>
#include "types.h"

namespace melonDS
{
class GPU;
class GPU3D;

struct WideState
{
    GPU3D* Owner = nullptr;   // the console whose scene is drawn wide; nullptr turns it off
    int Width = 0, Height = 0; // size to draw at, set by the frontend
    float ViewWidth = 256;    // width of the view in DS pixels (256 = no extra width)

    // The image that belongs to the frame the 2D renderer has just output: XRGB, with
    // the top byte 0 where no polygon was drawn.
    std::vector<u32> Front;
    int FrontW = 0, FrontH = 0;
    u64 FrontSerial = 0;      // changes whenever Front does
};
extern WideState Wide;
}
