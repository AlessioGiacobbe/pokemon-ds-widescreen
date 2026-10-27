#pragma once
// From Johto Online (github.com/nathanlogus/hgssrecomp, GPL-3.0 like this project).
// Void fill: where the widescreen world shows ground the game has not loaded (beyond the
// map blocks around the player, which the DS's own view never reaches), the empty pixels
// get the colours of the nearest drawn ground, blurred and dimmed like haze, instead of
// black. Empty pixels inside the DS's own view (the black around a room indoors) are the
// game's own look: when there are any, the picture is left as it is.
#include <algorithm>
#include <cstdint>
#include <vector>

struct VoidFill
{
    // Returns `px`, or a copy with the empty pixels (top byte 0) filled. dsFracX/Y: the
    // part of the picture's width/height, centred, that the DS's own view covers.
    const uint32_t* Apply(const uint32_t* px, int w, int h, float dsFracX, float dsFracY, uint64_t serial)
    {
        if (serial == lastSerial && w == lastW && h == lastH) return filled ? out.data() : px;
        lastSerial = serial;
        lastW = w;
        lastH = h;
        filled = false;
        if (w < Cell || h < Cell) return px;

        // First only whether anything is missing outside the DS's view and not inside it
        // (sampled; whole numbers, as it runs on every new frame and mostly finds nothing).
        const int dsX0 = (int)(w * (1 - dsFracX) / 2), dsX1 = w - dsX0;
        const int dsY0 = (int)(h * (1 - dsFracY) / 2), dsY1 = h - dsY0;
        int empty = 0, emptyInside = 0, inside = 0;
        for (int y = 1; y < h; y += 2)
        {
            const uint32_t* row = px + (size_t)y * w;
            const bool inRows = y >= dsY0 && y < dsY1;
            for (int x = 1; x < w; x += 2)
            {
                const bool none = !(row[x] >> 24);
                const bool in = inRows && x >= dsX0 && x < dsX1;
                empty += none;
                emptyInside += none & in;
                inside += in;
            }
        }
        // Nothing missing, or missing inside the DS's view (the game's own black).
        if (!empty || emptyInside * 100 > inside) return px;

        // A coarse grid of cells, each the average of its drawn pixels (sampled).
        const int sw = (w + Cell - 1) / Cell, sh = (h + Cell - 1) / Cell;
        Level& l0 = Prepare(0, sw, sh);
        sums.assign((size_t)sw * sh * 4, 0);
        for (int y = 1; y < h; y += 2)
        {
            const uint32_t* row = px + (size_t)y * w;
            uint32_t* o = &sums[(size_t)(y / Cell) * sw * 4];
            for (int x0 = 0; x0 < w; x0 += Cell, o += 4)
                for (int x = x0 + 1; x < std::min(w, x0 + Cell); x += 2)
                {
                    const uint32_t c = row[x];
                    if (!(c >> 24)) continue;
                    o[0] += (c >> 16) & 0xFF;
                    o[1] += (c >> 8) & 0xFF;
                    o[2] += c & 0xFF;
                    o[3]++;
                }
        }
        for (size_t i = 0; i < (size_t)sw * sh; i++)
        {
            const uint32_t* q = &sums[i * 4];
            float* p = &l0.v[i * 4];
            const float inv = q[3] ? 1.0f / q[3] : 0.0f;
            p[0] = q[0] * inv;
            p[1] = q[1] * inv;
            p[2] = q[2] * inv;
            p[3] = q[3] ? 1.0f : 0.0f;
        }
        // Nothing drawn at all (a black screen between maps).
        if (l0.Count() == 0) return px;

        // Push-pull: average the drawn cells down to coarser grids, then fill every empty
        // cell from the coarser grid above it, so the colours spread smoothly into the void.
        int levels = 1;
        while (lv[levels - 1].w > 1 || lv[levels - 1].h > 1)
        {
            const Level& f = lv[levels - 1];
            Level& c = Prepare(levels, (f.w + 1) / 2, (f.h + 1) / 2);
            for (int y = 0; y < c.h; y++)
                for (int x = 0; x < c.w; x++)
                {
                    float s[4] = {};
                    for (int dy = 0; dy < 2; dy++)
                        for (int dx = 0; dx < 2; dx++)
                        {
                            const int fx = std::min(f.w - 1, x * 2 + dx), fy = std::min(f.h - 1, y * 2 + dy);
                            const float* q = &f.v[((size_t)fy * f.w + fx) * 4];
                            for (int k = 0; k < 3; k++) s[k] += q[k] * q[3];
                            s[3] += q[3];
                        }
                    float* p = &c.v[((size_t)y * c.w + x) * 4];
                    for (int k = 0; k < 3; k++) p[k] = s[3] > 0 ? s[k] / s[3] : 0;
                    p[3] = std::min(1.0f, s[3]);
                }
            levels++;
        }
        for (int i = levels - 2; i >= 0; i--)
        {
            Level& f = lv[i];
            const Level& c = lv[i + 1];
            for (int y = 0; y < f.h; y++)
                for (int x = 0; x < f.w; x++)
                {
                    float* p = &f.v[((size_t)y * f.w + x) * 4];
                    if (p[3] >= 1) continue;
                    float s[3];
                    c.Sample((x + 0.5f) / 2 - 0.5f, (y + 0.5f) / 2 - 0.5f, s);
                    for (int k = 0; k < 3; k++) p[k] = p[k] * p[3] + s[k] * (1 - p[3]);
                    p[3] = 1;
                }
        }

        // The full picture: drawn pixels as they are, empty ones from the grid, dimmed.
        // (The grid's row for each picture row is made once, and only for rows with
        // something missing; along the row, the cells and weights come from a table.)
        out.assign(px, px + (size_t)w * h);
        xs.resize(w);
        for (int x = 0; x < w; x++)
        {
            const float fx = std::clamp((x + 0.5f) / Cell - 0.5f, 0.0f, (float)(sw - 1));
            xs[x].i = (int)fx;
            xs[x].f = fx - xs[x].i;
        }
        line.resize((size_t)sw * 3);
        for (int y = 0; y < h; y++)
        {
            uint32_t* row = &out[(size_t)y * w];
            bool any = false;
            for (int x = 0; x < w && !any; x++) any = !(row[x] >> 24);
            if (!any) continue;
            const float fy = std::clamp((y + 0.5f) / Cell - 0.5f, 0.0f, (float)(sh - 1));
            const int y0 = (int)fy, y1 = std::min(sh - 1, y0 + 1);
            const float ay = fy - y0;
            for (int cx = 0; cx < sw; cx++)
                for (int k = 0; k < 3; k++)
                    line[cx * 3 + k] = (l0.v[((size_t)y0 * sw + cx) * 4 + k] * (1 - ay) + l0.v[((size_t)y1 * sw + cx) * 4 + k] * ay) * Dim;
            for (int x = 0; x < w; x++)
            {
                if (row[x] >> 24) continue;
                const float* a = &line[xs[x].i * 3];
                const float* b = &line[std::min(sw - 1, xs[x].i + 1) * 3];
                const float f = xs[x].f;
                const uint32_t r = (uint32_t)(a[0] + (b[0] - a[0]) * f), g = (uint32_t)(a[1] + (b[1] - a[1]) * f),
                    bl = (uint32_t)(a[2] + (b[2] - a[2]) * f);
                row[x] = 0xFF000000 | (r << 16) | (g << 8) | bl;
            }
        }
        filled = true;
        return out.data();
    }

private:
    static constexpr int Cell = 8;        // picture pixels per grid cell
    static constexpr float Dim = 0.6f;    // brightness of the haze against the ground it continues

    struct Level
    {
        int w = 0, h = 0;
        std::vector<float> v; // r, g, b, weight per cell

        int Count() const
        {
            int n = 0;
            for (size_t i = 3; i < v.size(); i += 4) n += v[i] > 0;
            return n;
        }
        void Sample(float x, float y, float s[3]) const
        {
            x = std::clamp(x, 0.0f, (float)(w - 1));
            y = std::clamp(y, 0.0f, (float)(h - 1));
            const int x0 = (int)x, y0 = (int)y, x1 = std::min(w - 1, x0 + 1), y1 = std::min(h - 1, y0 + 1);
            const float fx = x - x0, fy = y - y0;
            const float* a = &v[((size_t)y0 * w + x0) * 4];
            const float* b = &v[((size_t)y0 * w + x1) * 4];
            const float* c = &v[((size_t)y1 * w + x0) * 4];
            const float* d = &v[((size_t)y1 * w + x1) * 4];
            for (int k = 0; k < 3; k++)
                s[k] = (a[k] * (1 - fx) + b[k] * fx) * (1 - fy) + (c[k] * (1 - fx) + d[k] * fx) * fy;
        }
    };
    // All levels exist from the start (a grid halves 24 times before it is one cell), so
    // that references to them stay valid while the next one is prepared.
    Level& Prepare(int i, int w, int h)
    {
        if (lv.empty()) lv.resize(24);
        lv[i].w = w;
        lv[i].h = h;
        lv[i].v.assign((size_t)w * h * 4, 0.0f);
        return lv[i];
    }

    std::vector<Level> lv;
    std::vector<uint32_t> out, sums;
    struct Tap { int i; float f; };
    std::vector<Tap> xs;
    std::vector<float> line;
    uint64_t lastSerial = ~0ull;
    int lastW = 0, lastH = 0;
    bool filled = false;
};
