#pragma once
// From Johto Online (github.com/nathanlogus/hgssrecomp, GPL-3.0 like this project).
// Night effects over the widescreen world: a glow around lit windows and lamps, warm
// light pooling around them, fireflies over grass and woods, and deeper night colours
// with a vignette. They are worked out at a quarter of the picture's size, as two layers
// the compositor blends over the 3D world (graphics card: multiply, then add; the CPU
// compositor bakes them in with NightFxBake), so their cost does not grow with the
// picture. Text boxes and menus are drawn over them and stay as they are.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

// How much of the night effects to show at this time of the game's clock: all of them
// from 20:00 to 4:00, coming in through the evening and going at dawn.
inline float NightStrength(int hour, int minute)
{
    const float t = hour + minute / 60.0f;
    if (t >= 20 || t < 4) return 1;
    if (t >= 17) return 0.25f + 0.75f * (t - 17) / 3;
    if (t < 6) return 1 - (t - 4) / 2;
    return 0;
}

struct NightFly
{
    float u, v;       // place in the wide picture, 0..1
    float brightness; // 0..1
};

struct NightLight
{
    float u, v;   // the lamp's foot in the wide picture, 0..1
    float ru, rv; // half the width and height of the oval its light makes on the ground
    float hu, hv; // its head
};

class NightFx
{
public:
    std::vector<uint32_t> Add, Mul; // ARGB layers of W x H
    int W = 0, H = 0;

    // Works out the layers for a wide picture `px` (top byte 0 where nothing was drawn).
    // The night is a light map in Mul: a dim blue ambient, brought back up to warm lamp
    // light around lamps and lit windows, so the ground there shows its own colours
    // (light added on top would look like fog). Add holds only the glow of the lights.
    void Build(const uint32_t* px, int pw, int ph, float strength, const std::vector<NightFly>& flies,
        const std::vector<NightLight>& lamps)
    {
        const int cell = std::max(2, pw / 320);
        W = (pw + cell - 1) / cell;
        H = (ph + cell - 1) / cell;
        const size_t n = (size_t)W * H;
        const int step = std::max(1, cell / 2); // 2 x 2 samples per cell: lights span several cells
        light.assign(n * 3, 0.0f);
        weight.assign(n, 0.0f);

        // Lights: bright, warm pixels (lit windows, lamps, fire), averaged per cell. (A row
        // at a time; most of the picture is not a light, which an integer test tells.)
        bool anyLight = false;
        for (int cy = 0; cy < H; cy++)
        for (int y = cy * cell; y < std::min(ph, cy * cell + cell); y += step)
        {
            const uint32_t* row = px + (size_t)y * pw;
            float* lo = &light[(size_t)cy * W * 3];
            float* wo = &weight[(size_t)cy * W];
            for (int x0 = 0; x0 < pw; x0 += cell, lo += 3, wo++)
                for (int x = x0; x < std::min(pw, x0 + cell); x += step)
                {
                    const uint32_t c = row[x];
                    const int ir = (c >> 16) & 0xFF, ig = (c >> 8) & 0xFF, ib = c & 0xFF;
                    if (!(c >> 24) || ir - ib <= 20 || ir * 3 + ig * 6 + ib <= 1400) continue;
                    const float pr = (float)ir, pg = (float)ig, pb = (float)ib;
                    const float lum = pr * 0.3f + pg * 0.6f + pb * 0.1f, warm = pr - pb;
                    const float l = std::clamp((lum - 140) / 80, 0.0f, 1.0f) * std::clamp((warm - 20) / 40, 0.0f, 1.0f);
                    lo[0] += pr * l; lo[1] += pg * l; lo[2] += pb * l; wo[0] += l;
                    anyLight |= l > 0;
                }
        }
        if (anyLight)
        {
            // (Averages over the samples a cell has: all of them but at the right and bottom
            // edges.)
            for (int cy = 0; cy < H; cy++)
                for (int cx = 0; cx < W; cx++)
                {
                    const int sy = (std::min(ph, cy * cell + cell) - cy * cell + step - 1) / step;
                    const int sx = (std::min(pw, cx * cell + cell) - cx * cell + step - 1) / step;
                    const float inv = 1.0f / (sx * sy);
                    const size_t i = (size_t)cy * W + cx;
                    light[i * 3] *= inv; light[i * 3 + 1] *= inv; light[i * 3 + 2] *= inv;
                    weight[i] *= inv;
                }
        }

        if (!anyLight && lamps.empty())
        {
            // No lights at all (most routes): the night colours alone, the same for every
            // picture of a size and strength, and the fireflies.
            PlainMul(n, strength);
            Mul = plainMul;
            Add.assign(n, 0xFF000000);
            AddFlies(flies, strength);
            return;
        }

        // Glow: the lights blurred a little; pools: blurred a lot and shifted down a
        // little, as lamps light the ground below and in front of them.
        // (Both at half the grid's size: the glow is blurred as much as at full size, by half
        // the radius, and read back between cells.)
        const int PW = (W + 1) / 2, PH = (H + 1) / 2;
        pool.assign((size_t)PW * PH * 3, 0.0f);
        for (int y = 0; y < H; y++)
        {
            const float* in = &light[(size_t)y * W * 3];
            float* out = &pool[(size_t)(y / 2) * PW * 3];
            for (int x = 0; x < W; x++)
                for (int k = 0; k < 3; k++) out[(x >> 1) * 3 + k] += in[x * 3 + k] * 0.25f;
        }
        glow = pool;
        Blur(glow, PW, PH, 3, 1);
        Blur(glow, PW, PH, 3, 1);
        for (int k = 0; k < 3; k++) Blur(pool, PW, PH, 3, 3);
        Blur(weight, W, H, 1, 2);

        PlainMul(n, strength);

        // Lamp light on the ground: a bright oval around each lamp's foot with a soft
        // edge, and a faint wider halo.
        lamp.assign(n, 0.0f);
        for (const NightLight& l : lamps)
        {
            const float cx = l.u * W - 0.5f, cy = l.v * H - 0.5f;
            const float rx = std::max(1.0f, l.ru * W), ry = std::max(1.0f, l.rv * H);
            for (int y = std::max(0, (int)(cy - ry * 1.6f)); y <= std::min(H - 1, (int)(cy + ry * 1.6f)); y++)
                for (int x = std::max(0, (int)(cx - rx * 1.6f)); x <= std::min(W - 1, (int)(cx + rx * 1.6f)); x++)
                {
                    const float dx = (x - cx) / rx, dy = (y - cy) / ry, d = std::sqrt(dx * dx + dy * dy);
                    const float core = 1 - Smooth(0.25f, 1.0f, d);
                    lamp[(size_t)y * W + x] += core * 0.9f + 0.2f * std::max(0.0f, 1 - d / 1.6f);
                }
        }

        Add.assign(n, 0xFF000000);
        Mul.assign(n, 0xFFFFFFFF);
        const int shift = std::max(1, H / 60);
        const float warm[3] = { 1.0f, 0.84f, 0.58f }; // lamp light, as a share of daylight
        const float glowK[3] = { strength * 1.2f * 1.1f, strength * 1.2f, strength * 1.2f * 0.8f };
        const float spillK[3] = { 3.0f / 255, 3.0f / 255, 3.0f / 255 * 0.7f };
        for (int y = 0; y < H; y++)
        {
            const float* poolRow = &pool[(size_t)(std::max(0, y - shift) / 2) * PW * 3];
            const float gy = std::min((float)(PH - 1), std::max(0.0f, (y - 0.5f) * 0.5f));
            const int gy0 = (int)gy;
            const float ay = gy - gy0;
            const float* glowRow0 = &glow[(size_t)gy0 * PW * 3];
            const float* glowRow1 = &glow[(size_t)std::min(PH - 1, gy0 + 1) * PW * 3];
            for (int x = 0; x < W; x++)
            {
                const size_t i = (size_t)y * W + x;
                float g[3] = {};
                const int gxi = std::min(PW - 1, std::max(0, (x - 1) >> 1));
                const float* q0 = glowRow0 + gxi * 3;
                const float* q1 = glowRow1 + gxi * 3;
                const int more = gxi + 1 < PW ? 3 : 0;
                // (Most cells are far from any light: nothing to add there.)
                if (q0[0] + q0[1] + q0[2] + q0[more] + q0[more + 1] + q0[more + 2] + q1[0] + q1[1] + q1[2] + q1[more] +
                    q1[more + 1] + q1[more + 2] > 0.05f)
                {
                    // (Between the half-size glow's cells.)
                    const float gx = std::min((float)(PW - 1), std::max(0.0f, (x - 0.5f) * 0.5f));
                    const int x0 = (int)gx, x1 = std::min(PW - 1, x0 + 1);
                    const float ax = gx - x0;
                    const float* r0 = glowRow0 + x0 * 3;
                    const float* r0b = glowRow0 + x1 * 3;
                    const float* r1 = glowRow1 + x0 * 3;
                    const float* r1b = glowRow1 + x1 * 3;
                    for (int k = 0; k < 3; k++)
                        g[k] = (r0[k] + (r0b[k] - r0[k]) * ax) * (1 - ay) + (r1[k] + (r1b[k] - r1[k]) * ax) * ay;
                }
                Add[i] = 0xFF000000 | (Clamp8(g[0] * glowK[0]) << 16) | (Clamp8(g[1] * glowK[1]) << 8) | Clamp8(g[2] * glowK[2]);

                const float* sp = poolRow + (x >> 1) * 3;
                if (weight[i] < 1e-4f && lamp[i] < 1e-4f && sp[0] + sp[1] + sp[2] < 0.01f)
                {
                    Mul[i] = plainMul[i];
                    continue;
                }
                // Night: a dim blue ambient, a little darker towards the edges; the lamps'
                // ovals and the light spilling below lit windows bring it back up to warm
                // light. Lit windows and lamps themselves keep their own colours.
                const float vig = vignette[i];
                const float keep = std::min(1.0f, std::max(0.0f, weight[i] * 6));
                const float ambient[3] = { 0.40f * vig, 0.45f * vig, 0.70f * (0.85f + 0.15f * vig) };
                const float lit = std::min(1.0f, lamp[i]);
                const float amount = strength * (1 - keep);
                uint32_t mc = 0xFF000000;
                for (int k = 0; k < 3; k++)
                {
                    const float target = std::min(1.0f, ambient[k] + (warm[k] - ambient[k]) * lit + sp[k] * spillK[k]);
                    mc |= Clamp8((1 + (target - 1) * amount) * 255) << (16 - 8 * k);
                }
                Mul[i] = mc;
            }
        }

        // Lamp heads: a small warm glow.
        for (const NightLight& l : lamps)
        {
            const float hx = l.hu * W - 0.5f, hy = l.hv * H - 0.5f, r = std::max(1.2f, l.ru * W * 0.2f);
            for (int y = std::max(0, (int)(hy - 2.5f * r)); y <= std::min(H - 1, (int)(hy + 2.5f * r)); y++)
                for (int x = std::max(0, (int)(hx - 2.5f * r)); x <= std::min(W - 1, (int)(hx + 2.5f * r)); x++)
                {
                    const float d2 = ((x - hx) * (x - hx) + (y - hy) * (y - hy)) / (r * r);
                    const float k = strength * std::exp(-d2);
                    uint32_t& c = Add[(size_t)y * W + x];
                    const uint32_t cr = std::min(255u, ((c >> 16) & 0xFF) + (uint32_t)(120 * k));
                    const uint32_t cg = std::min(255u, ((c >> 8) & 0xFF) + (uint32_t)(100 * k));
                    const uint32_t cb = std::min(255u, (c & 0xFF) + (uint32_t)(60 * k));
                    c = 0xFF000000 | (cr << 16) | (cg << 8) | cb;
                }
        }

        AddFlies(flies, strength);
    }

private:
    std::vector<float> light, weight, glow, pool, vignette, lamp;

    // The night colours alone (no lights near), per cell: the same for every picture of a
    // size and strength.
    void PlainMul(size_t n, float strength)
    {
        if (plainW == W && plainH == H && plainStrength == strength) return;
        Vignette(n);
        plainMul.resize(n);
        for (size_t i = 0; i < n; i++)
        {
            const float vig = vignette[i];
            const float ambient[3] = { 0.40f * vig, 0.45f * vig, 0.70f * (0.85f + 0.15f * vig) };
            uint32_t mc = 0xFF000000;
            for (int k = 0; k < 3; k++) mc |= Clamp8((1 + (ambient[k] - 1) * strength) * 255) << (16 - 8 * k);
            plainMul[i] = mc;
        }
        plainW = W;
        plainH = H;
        plainStrength = strength;
    }
    void Vignette(size_t n)
    {
        if (vignette.size() == n && vigW == W) return;
        vignette.resize(n);
        vigW = W;
        for (int y = 0; y < H; y++)
            for (int x = 0; x < W; x++)
            {
                const float ex = (x + 0.5f) / W * 2 - 1, ey = (y + 0.5f) / H * 2 - 1;
                vignette[(size_t)y * W + x] = 1 - 0.2f * Smooth(0.6f, 1.3f, std::sqrt(ex * ex * 0.8f + ey * ey));
            }
    }
    // Fireflies: small soft dots, yellow-green, added on top.
    void AddFlies(const std::vector<NightFly>& flies, float strength)
    {
        for (const NightFly& f : flies)
        {
            const float fx = f.u * W - 0.5f, fy = f.v * H - 0.5f;
            const int x0 = (int)std::floor(fx), y0 = (int)std::floor(fy);
            for (int dy = -1; dy <= 2; dy++)
                for (int dx = -1; dx <= 2; dx++)
                {
                    const int x = x0 + dx, y = y0 + dy;
                    if (x < 0 || y < 0 || x >= W || y >= H) continue;
                    const float dist2 = (x - fx) * (x - fx) + (y - fy) * (y - fy);
                    const float k = f.brightness * strength * std::exp(-dist2 * 1.3f);
                    uint32_t& c = Add[(size_t)y * W + x];
                    const uint32_t r = std::min(255u, ((c >> 16) & 0xFF) + (uint32_t)(200 * k));
                    const uint32_t g = std::min(255u, ((c >> 8) & 0xFF) + (uint32_t)(255 * k));
                    const uint32_t b = std::min(255u, (c & 0xFF) + (uint32_t)(110 * k));
                    c = 0xFF000000 | (r << 16) | (g << 8) | b;
                }
        }
    }
    std::vector<uint32_t> plainMul;
    int plainW = 0, plainH = 0, vigW = 0;
    float plainStrength = -1;

    static uint32_t Clamp8(float v) { return (uint32_t)std::min(255.0f, std::max(0.0f, v)); }
    static float Smooth(float a, float b, float x)
    {
        const float t = std::clamp((x - a) / (b - a), 0.0f, 1.0f);
        return t * t * (3 - 2 * t);
    }

    // Box blur of a w x h grid of `channels` floats per cell, radius r, along the rows
    // and then the columns, with running sums; the edges repeat. (The columns a whole row at
    // a time, which the compiler turns into vector instructions.)
    void Blur(std::vector<float>& v, int w, int h, int channels, int r)
    {
        tmp.resize(v.size());
        const float scale = 1.0f / (2 * r + 1);
        const size_t rowLen = (size_t)w * channels;
        pad.resize((size_t)(w + 2 * r + 1) * channels);
        for (int y = 0; y < h; y++)
        {
            // The row with its edge cells repeated r times on each side, then no edges to test.
            const float* in = v.data() + y * rowLen;
            float* out = tmp.data() + y * rowLen;
            for (int i = 0; i < r; i++)
                for (int c = 0; c < channels; c++)
                {
                    pad[(size_t)i * channels + c] = in[c];
                    pad[(size_t)(r + w + i) * channels + c] = in[(w - 1) * channels + c];
                }
            pad[(size_t)(2 * r + w) * channels] = 0; // (read past the end once, unused)
            std::copy(in, in + rowLen, pad.begin() + (size_t)r * channels);
            float sum[4] = {};
            for (int i = 0; i < 2 * r + 1; i++)
                for (int c = 0; c < channels; c++) sum[c] += pad[(size_t)i * channels + c];
            const float* lead = pad.data() + (size_t)(2 * r + 1) * channels;
            const float* tail = pad.data();
            for (int x = 0; x < w; x++, lead += channels, tail += channels, out += channels)
                for (int c = 0; c < channels; c++)
                {
                    out[c] = sum[c] * scale;
                    sum[c] += (x + 1 < w ? lead[c] : 0) - tail[c];
                }
        }
        acc.assign(rowLen, 0.0f);
        for (int i = -r; i <= r; i++)
        {
            const float* row = tmp.data() + std::clamp(i, 0, h - 1) * rowLen;
            for (size_t k = 0; k < rowLen; k++) acc[k] += row[k];
        }
        for (int y = 0; y < h; y++)
        {
            float* out = v.data() + y * rowLen;
            for (size_t k = 0; k < rowLen; k++) out[k] = acc[k] * scale;
            const float* add = tmp.data() + std::min(h - 1, y + r + 1) * rowLen;
            const float* sub = tmp.data() + std::max(0, y - r) * rowLen;
            for (size_t k = 0; k < rowLen; k++) acc[k] += add[k] - sub[k];
        }
    }
    std::vector<float> tmp, acc, pad;
};

// The CPU compositor's version of the blending the graphics card does: the picture
// multiplied by Mul and then Add added, both stretched smoothly over it.
inline void NightFxBake(const uint32_t* px, int pw, int ph, const NightFx& fx, std::vector<uint32_t>& out)
{
    out.resize((size_t)pw * ph);
    auto sample = [&](const std::vector<uint32_t>& layer, float u, float v, float c[3]) {
        const float x = std::clamp(u * fx.W - 0.5f, 0.0f, (float)(fx.W - 1)), y = std::clamp(v * fx.H - 0.5f, 0.0f, (float)(fx.H - 1));
        const int x0 = (int)x, y0 = (int)y, x1 = std::min(fx.W - 1, x0 + 1), y1 = std::min(fx.H - 1, y0 + 1);
        const float ax = x - x0, ay = y - y0;
        for (int k = 0; k < 3; k++)
        {
            const int sh = 16 - 8 * k;
            auto ch = [&](int xx, int yy) { return (float)((layer[(size_t)yy * fx.W + xx] >> sh) & 0xFF); };
            c[k] = (ch(x0, y0) * (1 - ax) + ch(x1, y0) * ax) * (1 - ay) + (ch(x0, y1) * (1 - ax) + ch(x1, y1) * ax) * ay;
        }
    };
    for (int y = 0; y < ph; y++)
        for (int x = 0; x < pw; x++)
        {
            const uint32_t c = px[(size_t)y * pw + x];
            float m[3], a[3];
            const float u = (x + 0.5f) / pw, v = (y + 0.5f) / ph;
            sample(fx.Mul, u, v, m);
            sample(fx.Add, u, v, a);
            uint32_t o = 0xFF000000;
            for (int k = 0; k < 3; k++)
            {
                const int sh = 16 - 8 * k;
                const float base = (c >> 24) ? (float)((c >> sh) & 0xFF) : 0.0f;
                o |= (uint32_t)std::min(255.0f, base * m[k] / 255 + a[k]) << sh;
            }
            out[(size_t)y * pw + x] = o;
        }
}
