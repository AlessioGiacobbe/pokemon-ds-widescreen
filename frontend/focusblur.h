#pragma once
// From Johto Online (github.com/nathanlogus/hgssrecomp, GPL-3.0 like this project).
// Focus blur over the widescreen world ("tilt-shift", as in Link's Awakening): a round
// area around the player stays sharp and the world softens towards the edges, which
// makes it look like a model. A quarter-size blurred copy of the world, opaque towards
// the edges and clear in the middle, is drawn over it (graphics card: alpha blending;
// the CPU compositor bakes it in with FocusBlurBake). The camera looks at the player, so
// the sharp area is centred on the picture. Text boxes and menus stay sharp.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

class FocusBlur
{
public:
    std::vector<uint32_t> Layer; // ARGB, W x H: blurred world, alpha = how much of it shows
    int W = 0, H = 0;

    // level 1: light (a large sharp area, soft edges), 2: strong.
    void Build(const uint32_t* px, int pw, int ph, int level)
    {
        const int cell = std::max(2, pw / 320);
        W = (pw + cell - 1) / cell;
        H = (ph + cell - 1) / cell;
        const size_t n = (size_t)W * H;
        const int step = std::max(1, cell / 2);
        // (Summed in whole numbers, a row at a time: on the Switch's processor this runs on
        // every new frame.)
        sums.assign(n * 4, 0);
        for (int y = 0; y < ph; y += step)
        {
            const uint32_t* row = px + (size_t)y * pw;
            uint32_t* o = &sums[(size_t)(y / cell) * W * 4];
            for (int x0 = 0; x0 < pw; x0 += cell, o += 4)
                for (int x = x0; x < std::min(pw, x0 + cell); x += step)
                {
                    const uint32_t c = row[x];
                    if (!(c >> 24)) { o[3] += 1 << 16; continue; } // nothing drawn: not mixed in
                    o[0] += (c >> 16) & 0xFF;
                    o[1] += (c >> 8) & 0xFF;
                    o[2] += c & 0xFF;
                    o[3] += 1 | (1 << 16); // drawn (low half) and counted (high half)
                }
        }
        rgb.resize(n * 4);
        for (size_t i = 0; i < n; i++)
        {
            const uint32_t* q = &sums[i * 4];
            const uint32_t count = q[3] >> 16;
            const float inv = count ? 1.0f / count : 0.0f;
            float* o = &rgb[i * 4];
            o[0] = q[0] * inv;
            o[1] = q[1] * inv;
            o[2] = q[2] * inv;
            o[3] = (q[3] & 0xFFFF) * inv;
        }
        // The quarter size is already soft; light adds a little, strong more.
        for (int k = 0; k < level; k++) Blur();

        // Sharp inside a circle (in units of the picture's height), fully blurred at the
        // far edge of the ramp.
        const float sharp = level >= 2 ? 0.28f : 0.45f, ramp = level >= 2 ? 0.42f : 0.5f;
        if (maskW != W || maskH != H || maskLevel != level)
        {
            // (The same for every picture of a size: made once.)
            mask.resize(n);
            for (int y = 0; y < H; y++)
                for (int x = 0; x < W; x++)
                {
                    const float dx = ((x + 0.5f) / W - 0.5f) * W / H, dy = (y + 0.5f) / H - 0.5f;
                    const float t = std::clamp((std::sqrt(dx * dx + dy * dy) - sharp) / ramp, 0.0f, 1.0f);
                    mask[(size_t)y * W + x] = (uint8_t)(255 * t * t * (3 - 2 * t));
                }
            maskW = W;
            maskH = H;
            maskLevel = level;
        }
        Layer.resize(n);
        for (size_t i = 0; i < n; i++)
        {
            const uint32_t a = mask[i];
            if (!a)
            {
                Layer[i] = 0;
                continue;
            }
            const float* c = &rgb[i * 4];
            // Back from premultiplied; where little is drawn the layer fades out.
            const float k = c[3] > 0.01f ? 1.0f / c[3] : 0.0f;
            const uint32_t al = (uint32_t)(a * std::min(1.0f, c[3]));
            Layer[i] = (al << 24) | ((uint32_t)std::min(255.0f, c[0] * k) << 16) |
                ((uint32_t)std::min(255.0f, c[1] * k) << 8) | (uint32_t)std::min(255.0f, c[2] * k);
        }
    }

private:
    std::vector<float> rgb, tmp;
    std::vector<uint32_t> sums;
    std::vector<uint8_t> mask;
    int maskW = 0, maskH = 0, maskLevel = 0;

    // Box blur of radius 1 (3 cells) on the four channels, along the rows and then the
    // columns; the edges repeat. (Whole rows at a time, which the compiler turns into vector
    // instructions.)
    void Blur()
    {
        tmp.resize(rgb.size());
        const float third = 1.0f / 3;
        const size_t rowLen = (size_t)W * 4;
        for (int y = 0; y < H; y++)
        {
            const float* in = rgb.data() + y * rowLen;
            float* out = tmp.data() + y * rowLen;
            for (int x = 0; x < W; x++)
            {
                const float* l = in + std::max(0, x - 1) * 4;
                const float* c = in + x * 4;
                const float* r = in + std::min(W - 1, x + 1) * 4;
                for (int k = 0; k < 4; k++) out[x * 4 + k] = (l[k] + c[k] + r[k]) * third;
            }
        }
        for (int y = 0; y < H; y++)
        {
            const float* u = tmp.data() + std::max(0, y - 1) * rowLen;
            const float* c = tmp.data() + y * rowLen;
            const float* d = tmp.data() + std::min(H - 1, y + 1) * rowLen;
            float* out = rgb.data() + y * rowLen;
            for (size_t i = 0; i < rowLen; i++) out[i] = (u[i] + c[i] + d[i]) * third;
        }
    }
};

// The CPU compositor's version of the blending: the layer stretched smoothly over the
// picture, mixed in by its alpha. (From Johto Online, like the rest of this file.)
inline void FocusBlurBake(const uint32_t* px, int pw, int ph, const FocusBlur& fb, std::vector<uint32_t>& out)
{
    out.resize((size_t)pw * ph);
    for (int y = 0; y < ph; y++)
    {
        const float fy = std::clamp((y + 0.5f) / ph * fb.H - 0.5f, 0.0f, (float)(fb.H - 1));
        const int y0 = (int)fy, y1 = std::min(fb.H - 1, y0 + 1);
        const float ay = fy - y0;
        for (int x = 0; x < pw; x++)
        {
            const float fx = std::clamp((x + 0.5f) / pw * fb.W - 0.5f, 0.0f, (float)(fb.W - 1));
            const int x0 = (int)fx, x1 = std::min(fb.W - 1, x0 + 1);
            const float ax = fx - x0;
            float v[4];
            for (int k = 0; k < 4; k++)
            {
                const int sh = 24 - 8 * k;
                auto ch = [&](int xx, int yy) { return (float)((fb.Layer[(size_t)yy * fb.W + xx] >> sh) & 0xFF); };
                v[k] = (ch(x0, y0) * (1 - ax) + ch(x1, y0) * ax) * (1 - ay) + (ch(x0, y1) * (1 - ax) + ch(x1, y1) * ax) * ay;
            }
            const uint32_t c = px[(size_t)y * pw + x];
            const float a = v[0] / 255;
            // (The top byte stays the picture's: where nothing was drawn is still known.)
            uint32_t o = c & 0xFF000000;
            for (int k = 0; k < 3; k++)
            {
                const int sh = 16 - 8 * k;
                const float base = (c >> 24) ? (float)((c >> sh) & 0xFF) : 0.0f;
                o |= (uint32_t)(base + (v[k + 1] - base) * a) << sh;
            }
            out[(size_t)y * pw + x] = o;
        }
    }
}
