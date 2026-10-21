#include "runtime_heights.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <thread>

namespace runtimeheight
{
namespace
{
struct Plane
{
    int w = 0;
    int h = 0;
    std::vector<float> v;
    void Resize(int width, int height)
    {
        w = width;
        h = height;
        v.assign(static_cast<size_t>(width) * height, 0.0f);
    }
    float* Row(int y) { return v.data() + static_cast<size_t>(y) * w; }
    const float* Row(int y) const { return v.data() + static_cast<size_t>(y) * w; }
};

constexpr float kB0 = 1.0f / 16.0f;
constexpr float kB1 = 4.0f / 16.0f;
constexpr float kB2 = 6.0f / 16.0f;
constexpr int kTablePad = 2;

std::vector<int> WrapTable(int n)
{
    std::vector<int> t(static_cast<size_t>(n) + 2 * kTablePad + 2);
    for (int i = 0; i < static_cast<int>(t.size()); ++i)
    {
        int k = (i - kTablePad) % n;
        t[static_cast<size_t>(i)] = k < 0 ? k + n : k;
    }
    return t;
}

void Reduce(const Plane& in, Plane& out)
{
    const int sx = in.w > 1 ? 2 : 1;
    const int sy = in.h > 1 ? 2 : 1;
    const int ow = in.w / sx;
    const int oh = in.h / sy;
    const std::vector<int> wx = WrapTable(in.w);
    const std::vector<int> wy = WrapTable(in.h);
    Plane tmp;
    tmp.Resize(ow, in.h);
    for (int y = 0; y < in.h; ++y)
    {
        const float* r = in.Row(y);
        float* o = tmp.Row(y);
        for (int x = 0; x < ow; ++x)
        {
            const int c = x * sx;
            const int* t = wx.data() + c + kTablePad;
            o[x] = sx == 1 ? r[c] : kB0 * (r[t[-2]] + r[t[2]]) + kB1 * (r[t[-1]] + r[t[1]]) + kB2 * r[c];
        }
    }
    out.Resize(ow, oh);
    for (int y = 0; y < oh; ++y)
    {
        const int c = y * sy;
        float* o = out.Row(y);
        if (sy == 1)
        {
            std::memcpy(o, tmp.Row(c), sizeof(float) * ow);
            continue;
        }
        const int* t = wy.data() + c + kTablePad;
        const float* a = tmp.Row(t[-2]);
        const float* b = tmp.Row(t[-1]);
        const float* m = tmp.Row(c);
        const float* d = tmp.Row(t[1]);
        const float* e = tmp.Row(t[2]);
        for (int x = 0; x < ow; ++x)
            o[x] = kB0 * (a[x] + e[x]) + kB1 * (b[x] + d[x]) + kB2 * m[x];
    }
}

void ExpandLine(const float* in, int stride, float* out, int outStride, int n2, const std::vector<int>& t)
{
    for (int i = 0; i < n2; ++i)
    {
        const int j = i >> 1;
        const int* p = t.data() + j + kTablePad;
        const float v = (i & 1) == 0 ? 0.125f * (in[p[-1] * stride] + in[p[1] * stride]) + 0.75f * in[j * stride]
                                     : 0.5f * (in[j * stride] + in[p[1] * stride]);
        out[i * outStride] = v;
    }
}

void Expand(const Plane& in, int w2, int h2, Plane& out)
{
    const bool fx = w2 != in.w;
    const bool fy = h2 != in.h;
    const std::vector<int> wx = WrapTable(in.w);
    const std::vector<int> wy = WrapTable(in.h);
    Plane tmp;
    tmp.Resize(w2, in.h);
    for (int y = 0; y < in.h; ++y)
    {
        if (fx)
            ExpandLine(in.Row(y), 1, tmp.Row(y), 1, w2, wx);
        else
            std::memcpy(tmp.Row(y), in.Row(y), sizeof(float) * w2);
    }
    out.Resize(w2, h2);
    if (!fy)
    {
        out.v = tmp.v;
        return;
    }
    for (int x = 0; x < w2; ++x)
        ExpandLine(tmp.v.data() + x, w2, out.v.data() + x, w2, h2, wy);
}

float Smoothstep(float a, float b, float x)
{
    const float t = std::clamp((x - a) / (b - a), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

constexpr float kCutoutLow = 0.1f;
constexpr float kCutoutHigh = 0.9f;
constexpr float kCutoutMaxBetween = 0.15f;
constexpr float kCutoutMinHoles = 0.005f;
constexpr float kCutoutMinSurface = 0.1f;
constexpr float kPullWeightGain = 4.0f;
constexpr float kLowPercentile = 0.005f;
constexpr float kHighPercentile = 0.995f;
constexpr float kFlatSpread = 0.05f;
constexpr float kFullSpread = 0.2f;
constexpr float kSpreadFloor = 0.05f;

uint32_t Expand5(uint32_t v)
{
    return (v << 3) | (v >> 2);
}

uint32_t Expand6(uint32_t v)
{
    return (v << 2) | (v >> 4);
}

uint32_t Expand4(uint32_t v)
{
    return (v << 4) | v;
}

void Colour565(uint16_t c, uint8_t* out)
{
    out[0] = static_cast<uint8_t>(Expand5((c >> 11) & 31u));
    out[1] = static_cast<uint8_t>(Expand6((c >> 5) & 63u));
    out[2] = static_cast<uint8_t>(Expand5(c & 31u));
}

void DecodeColourBlock(const uint8_t* b, bool threeColour, uint8_t pal[4][4])
{
    const uint16_t c0 = static_cast<uint16_t>(b[0] | (b[1] << 8));
    const uint16_t c1 = static_cast<uint16_t>(b[2] | (b[3] << 8));
    Colour565(c0, pal[0]);
    Colour565(c1, pal[1]);
    pal[0][3] = pal[1][3] = 255;
    if (!threeColour || c0 > c1)
        for (int k = 0; k < 3; ++k)
        {
            pal[2][k] = static_cast<uint8_t>((2 * pal[0][k] + pal[1][k] + 1) / 3);
            pal[3][k] = static_cast<uint8_t>((pal[0][k] + 2 * pal[1][k] + 1) / 3);
        }
    else
        for (int k = 0; k < 3; ++k)
        {
            pal[2][k] = static_cast<uint8_t>((pal[0][k] + pal[1][k]) / 2);
            pal[3][k] = 0;
        }
    pal[2][3] = 255;
    pal[3][3] = threeColour && c0 <= c1 ? 0 : 255;
}

void DecodeDxt5Alpha(const uint8_t* b, uint8_t alpha[16])
{
    uint8_t a[8] = {b[0], b[1]};
    if (a[0] > a[1])
        for (int k = 1; k < 7; ++k)
            a[k + 1] = static_cast<uint8_t>(((7 - k) * a[0] + k * a[1] + 3) / 7);
    else
    {
        for (int k = 1; k < 5; ++k)
            a[k + 1] = static_cast<uint8_t>(((5 - k) * a[0] + k * a[1] + 2) / 5);
        a[6] = 0;
        a[7] = 255;
    }
    uint64_t bits = 0;
    for (int k = 0; k < 6; ++k)
        bits |= static_cast<uint64_t>(b[2 + k]) << (8 * k);
    for (int i = 0; i < 16; ++i)
        alpha[i] = a[(bits >> (3 * i)) & 7u];
}
}

const Weights& DefaultWeights()
{
    static const Weights w = {7, {0.62f, 0.82f, 0.81f, 0.90f, 0.80f, 0.76f, 0.67f}};
    return w;
}

bool Make(const uint8_t* rgba, int width, int height, bool hasAlpha, const Weights& weights, Result& out)
{
    if (!rgba || width < 1 || height < 1 || (width & (width - 1)) || (height & (height - 1)) || weights.levels < 1 ||
        weights.levels > kMaxLevels)
        return false;
    const size_t n = static_cast<size_t>(width) * height;
    out.width = width;
    out.height = height;
    std::vector<uint8_t> mask(n, 1);
    out.cutout = false;
    if (hasAlpha)
    {
        size_t low = 0;
        size_t high = 0;
        for (size_t i = 0; i < n; ++i)
        {
            const float a = rgba[4 * i + 3] / 255.0f;
            low += a <= kCutoutLow;
            high += a >= kCutoutHigh;
        }
        const float holes = static_cast<float>(low) / n;
        const float surface = static_cast<float>(high) / n;
        const float between = 1.0f - holes - surface;
        out.cutout = holes >= kCutoutMinHoles && surface >= kCutoutMinSurface && between <= holes &&
                     between <= kCutoutMaxBetween;
        if (out.cutout)
            for (size_t i = 0; i < n; ++i)
                mask[i] = rgba[4 * i + 3] >= 128 ? 1 : 0;
    }

    double sum = 0.0;
    double squares = 0.0;
    size_t counted = 0;
    for (size_t i = 0; i < n; ++i)
        if (mask[i])
        {
            const double l = (0.299 * rgba[4 * i] + 0.587 * rgba[4 * i + 1] + 0.114 * rgba[4 * i + 2]) / 255.0;
            sum += l;
            squares += l * l;
            ++counted;
        }
    const double mean = counted ? sum / counted : 0.0;
    const double sd = counted ? std::sqrt(std::max(squares / counted - mean * mean, 0.0)) : 0.0;
    out.lumaSpread = static_cast<float>(sd / (mean + kSpreadFloor));
    out.depthFactor = Smoothstep(kFlatSpread, kFullSpread, out.lumaSpread);

    std::vector<Plane> g(1);
    std::vector<Plane> m(1);
    g[0].Resize(width, height);
    m[0].Resize(width, height);
    for (size_t i = 0; i < n; ++i)
    {
        const float l = 0.2126f * (rgba[4 * i] / 255.0f) + 0.7152f * (rgba[4 * i + 1] / 255.0f) +
                        0.0722f * (rgba[4 * i + 2] / 255.0f);
        m[0].v[i] = mask[i];
        g[0].v[i] = mask[i] * l;
    }
    int levels = 0;
    while (levels < weights.levels && !(g[static_cast<size_t>(levels)].w == 1 && g[static_cast<size_t>(levels)].h == 1))
    {
        Plane next;
        Reduce(g[static_cast<size_t>(levels)], next);
        g.push_back(std::move(next));
        if (out.cutout)
        {
            Plane nextMask;
            Reduce(m[static_cast<size_t>(levels)], nextMask);
            m.push_back(std::move(nextMask));
        }
        ++levels;
    }
    Plane up;
    if (out.cutout)
    {
        for (int k = 0; k <= levels; ++k)
            for (size_t i = 0; i < g[static_cast<size_t>(k)].v.size(); ++i)
                g[static_cast<size_t>(k)].v[i] /= std::max(m[static_cast<size_t>(k)].v[i], 1e-6f);
        for (int k = levels - 1; k >= 0; --k)
        {
            Plane& gk = g[static_cast<size_t>(k)];
            Expand(g[static_cast<size_t>(k) + 1], gk.w, gk.h, up);
            for (size_t i = 0; i < up.v.size(); ++i)
            {
                const float a = std::clamp(m[static_cast<size_t>(k)].v[i] * kPullWeightGain, 0.0f, 1.0f);
                gk.v[i] = a * gk.v[i] + (1.0f - a) * up.v[i];
            }
        }
    }

    Plane result;
    for (int k = levels - 1; k >= 0; --k)
    {
        const Plane& gk = g[static_cast<size_t>(k)];
        Plane band;
        Expand(g[static_cast<size_t>(k) + 1], gk.w, gk.h, up);
        band.Resize(gk.w, gk.h);
        for (size_t i = 0; i < band.v.size(); ++i)
            band.v[i] = weights.band[k] * (gk.v[i] - up.v[i]);
        if (k < levels - 1)
        {
            Expand(result, band.w, band.h, up);
            for (size_t i = 0; i < band.v.size(); ++i)
                band.v[i] += up.v[i];
        }
        result = std::move(band);
    }
    if (levels == 0)
        result.Resize(width, height);

    std::vector<float> inside;
    inside.reserve(n);
    for (size_t i = 0; i < n; ++i)
        if (mask[i])
            inside.push_back(result.v[i]);
    if (inside.empty())
        inside = result.v;
    const auto order = [&](float p) {
        const size_t k = static_cast<size_t>(std::lround(p * static_cast<float>(inside.size() - 1)));
        std::nth_element(inside.begin(), inside.begin() + static_cast<std::ptrdiff_t>(k), inside.end());
        return inside[k];
    };
    const float lo = order(kLowPercentile);
    const float hi = order(kHighPercentile);
    out.height01.resize(n);
    if (hi - lo < 1e-6f)
        std::fill(out.height01.begin(), out.height01.end(), 1.0f);
    else
        for (size_t i = 0; i < n; ++i)
            out.height01[i] = std::clamp((result.v[i] - lo) / (hi - lo), 0.0f, 1.0f);
    return true;
}

std::vector<uint8_t> L8Levels(const Result& result, unsigned* levelCount)
{
    std::vector<uint8_t> bytes;
    std::vector<float> level = result.height01;
    int w = result.width;
    int h = result.height;
    unsigned count = 0;
    for (;;)
    {
        for (float v : level)
            bytes.push_back(static_cast<uint8_t>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f)));
        ++count;
        if (w == 1 && h == 1)
            break;
        const int sx = w > 1 ? 2 : 1;
        const int sy = h > 1 ? 2 : 1;
        const int w2 = w / sx;
        const int h2 = h / sy;
        std::vector<float> next(static_cast<size_t>(w2) * h2);
        for (int y = 0; y < h2; ++y)
            for (int x = 0; x < w2; ++x)
            {
                float s = 0.0f;
                for (int j = 0; j < sy; ++j)
                    for (int i = 0; i < sx; ++i)
                        s += level[static_cast<size_t>(y * sy + j) * w + x * sx + i];
                next[static_cast<size_t>(y) * w2 + x] = s / static_cast<float>(sx * sy);
            }
        level.swap(next);
        w = w2;
        h = h2;
    }
    if (levelCount)
        *levelCount = count;
    return bytes;
}

bool FormatDecodable(uint32_t format)
{
    switch (format)
    {
    case D3DFMT_DXT1:
    case D3DFMT_DXT3:
    case D3DFMT_DXT5:
    case D3DFMT_A8R8G8B8:
    case D3DFMT_X8R8G8B8:
    case D3DFMT_R5G6B5:
    case D3DFMT_X1R5G5B5:
    case D3DFMT_A1R5G5B5:
    case D3DFMT_A4R4G4B4:
        return true;
    default:
        return false;
    }
}

bool Decode(uint32_t format, uint32_t width, uint32_t height, const uint8_t* bits, uint32_t pitch,
            std::vector<uint8_t>& rgba, bool& hasAlpha)
{
    if (!bits || !FormatDecodable(format) || width == 0 || height == 0)
        return false;
    rgba.assign(static_cast<size_t>(width) * height * 4, 255);
    hasAlpha = format == D3DFMT_DXT3 || format == D3DFMT_DXT5 || format == D3DFMT_A8R8G8B8 ||
               format == D3DFMT_A1R5G5B5 || format == D3DFMT_A4R4G4B4 || format == D3DFMT_DXT1;
    const auto put = [&](uint32_t x, uint32_t y, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
        if (x >= width || y >= height)
            return;
        uint8_t* p = rgba.data() + (static_cast<size_t>(y) * width + x) * 4;
        p[0] = r;
        p[1] = g;
        p[2] = b;
        p[3] = a;
    };
    if (format == D3DFMT_DXT1 || format == D3DFMT_DXT3 || format == D3DFMT_DXT5)
    {
        if (width % 4 || height % 4)
            return false;
        const uint32_t blockBytes = format == D3DFMT_DXT1 ? 8u : 16u;
        bool anyClear = false;
        for (uint32_t by = 0; by < height / 4; ++by)
            for (uint32_t bx = 0; bx < width / 4; ++bx)
            {
                const uint8_t* b = bits + static_cast<size_t>(by) * pitch + static_cast<size_t>(bx) * blockBytes;
                uint8_t alpha[16];
                const uint8_t* colour = b;
                if (format == D3DFMT_DXT3)
                {
                    for (int i = 0; i < 16; ++i)
                        alpha[i] = static_cast<uint8_t>(Expand4((b[i / 2] >> (4 * (i & 1))) & 15u));
                    colour = b + 8;
                }
                else if (format == D3DFMT_DXT5)
                {
                    DecodeDxt5Alpha(b, alpha);
                    colour = b + 8;
                }
                uint8_t pal[4][4];
                DecodeColourBlock(colour, format == D3DFMT_DXT1, pal);
                const uint32_t idx = static_cast<uint32_t>(colour[4] | (colour[5] << 8) | (colour[6] << 16)) |
                                     (static_cast<uint32_t>(colour[7]) << 24);
                for (uint32_t i = 0; i < 16; ++i)
                {
                    const uint8_t* c = pal[(idx >> (2 * i)) & 3u];
                    const uint8_t a = format == D3DFMT_DXT1 ? c[3] : alpha[i];
                    anyClear = anyClear || a < 255;
                    put(bx * 4 + (i & 3), by * 4 + (i >> 2), c[0], c[1], c[2], a);
                }
            }
        if (format == D3DFMT_DXT1)
            hasAlpha = anyClear;
        return true;
    }
    for (uint32_t y = 0; y < height; ++y)
    {
        const uint8_t* row = bits + static_cast<size_t>(y) * pitch;
        for (uint32_t x = 0; x < width; ++x)
        {
            if (format == D3DFMT_A8R8G8B8 || format == D3DFMT_X8R8G8B8)
            {
                const uint8_t* p = row + 4 * x;
                put(x, y, p[2], p[1], p[0], format == D3DFMT_A8R8G8B8 ? p[3] : 255);
                continue;
            }
            const uint16_t v = static_cast<uint16_t>(row[2 * x] | (row[2 * x + 1] << 8));
            if (format == D3DFMT_R5G6B5)
            {
                uint8_t c[3];
                Colour565(v, c);
                put(x, y, c[0], c[1], c[2], 255);
            }
            else if (format == D3DFMT_A4R4G4B4)
                put(x, y, static_cast<uint8_t>(Expand4((v >> 8) & 15u)), static_cast<uint8_t>(Expand4((v >> 4) & 15u)),
                    static_cast<uint8_t>(Expand4(v & 15u)), static_cast<uint8_t>(Expand4(v >> 12)));
            else
                put(x, y, static_cast<uint8_t>(Expand5((v >> 10) & 31u)), static_cast<uint8_t>(Expand5((v >> 5) & 31u)),
                    static_cast<uint8_t>(Expand5(v & 31u)),
                    format == D3DFMT_A1R5G5B5 ? ((v >> 15) ? 255 : 0) : static_cast<uint8_t>(255));
        }
    }
    return true;
}

uint32_t WorkingLevel(uint32_t width, uint32_t height, uint32_t levels)
{
    uint32_t level = 0;
    while (level + 1 < levels && (width > kWorkingSize || height > kWorkingSize))
    {
        width = std::max(width / 2, 1u);
        height = std::max(height / 2, 1u);
        ++level;
    }
    return width > kWorkingSize || height > kWorkingSize ? UINT32_MAX : level;
}
}

RuntimeHeights::~RuntimeHeights()
{
    if (m_shared)
    {
        std::lock_guard<std::mutex> guard(m_shared->lock);
        m_shared->stop = true;
        m_shared->wake.notify_all();
    }
}

void RuntimeHeights::Start()
{
    if (m_shared)
        return;
    m_shared = std::make_shared<Shared>();
    std::thread(Work, m_shared).detach();
}

void RuntimeHeights::Work(std::shared_ptr<Shared> shared)
{
    for (;;)
    {
        Job job;
        {
            std::unique_lock<std::mutex> lock(shared->lock);
            shared->wake.wait(lock, [&] { return shared->stop || !shared->jobs.empty(); });
            if (shared->stop)
                return;
            job = std::move(shared->jobs.front());
            shared->jobs.pop_front();
            shared->working = true;
        }
        const auto started = std::chrono::steady_clock::now();
        Done done = {job.key, job.ticket, false, false, 0.0f, 0, 0, 0, 0.0, {}};
        std::vector<uint8_t> rgba;
        bool hasAlpha = false;
        runtimeheight::Result result;
        if (runtimeheight::Decode(job.format, job.width, job.height, job.bits.data(), job.pitch, rgba, hasAlpha) &&
            runtimeheight::Make(rgba.data(), static_cast<int>(job.width), static_cast<int>(job.height), hasAlpha,
                                runtimeheight::DefaultWeights(), result))
        {
            done.ok = true;
            done.cutout = result.cutout;
            done.depthFactor = result.depthFactor;
            done.width = job.width;
            done.height = job.height;
            done.l8 = runtimeheight::L8Levels(result, &done.levels);
        }
        done.milliseconds =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
        std::lock_guard<std::mutex> guard(shared->lock);
        shared->done.push_back(std::move(done));
        shared->working = false;
        if (shared->jobs.empty())
            shared->idle.notify_all();
    }
}

bool RuntimeHeights::Request(IDirect3DBaseTexture9* source, unsigned ticket)
{
    if (source->GetType() != D3DRTYPE_TEXTURE)
        return false;
    auto* tex = static_cast<IDirect3DTexture9*>(source);
    D3DSURFACE_DESC top = {};
    if (FAILED(tex->GetLevelDesc(0, &top)) || top.Pool != D3DPOOL_MANAGED ||
        !runtimeheight::FormatDecodable(static_cast<uint32_t>(top.Format)))
        return false;
    const uint32_t level = runtimeheight::WorkingLevel(top.Width, top.Height, tex->GetLevelCount());
    D3DSURFACE_DESC desc = {};
    if (level == UINT32_MAX || FAILED(tex->GetLevelDesc(level, &desc)) || desc.Width < runtimeheight::kSmallestSide ||
        desc.Height < runtimeheight::kSmallestSide || (desc.Width & (desc.Width - 1)) || (desc.Height & (desc.Height - 1)))
        return false;
    D3DLOCKED_RECT rect = {};
    if (FAILED(tex->LockRect(level, &rect, nullptr, D3DLOCK_READONLY)) || !rect.pBits)
        return false;
    const bool compressed = desc.Format == D3DFMT_DXT1 || desc.Format == D3DFMT_DXT3 || desc.Format == D3DFMT_DXT5;
    const uint32_t rows = compressed ? desc.Height / 4 : desc.Height;
    Job job = {reinterpret_cast<uintptr_t>(source), ticket, static_cast<uint32_t>(desc.Format), desc.Width,
               desc.Height, static_cast<uint32_t>(rect.Pitch), {}};
    job.bits.assign(static_cast<const uint8_t*>(rect.pBits),
                    static_cast<const uint8_t*>(rect.pBits) + static_cast<size_t>(rows) * rect.Pitch);
    tex->UnlockRect(level);
    Start();
    std::lock_guard<std::mutex> guard(m_shared->lock);
    if (m_shared->jobs.size() >= kRuntimeHeightQueueLimit)
        return false;
    m_shared->jobs.push_back(std::move(job));
    m_shared->wake.notify_one();
    return true;
}

const RuntimeHeights::Ready* RuntimeHeights::Find(IDirect3DDevice9* real, IDirect3DBaseTexture9* source,
                                                  unsigned frame)
{
    if (!source)
        return nullptr;
    const uintptr_t key = reinterpret_cast<uintptr_t>(source);
    auto it = m_ready.find(key);
    if (it != m_ready.end())
    {
        it->second.lastUsed = frame;
        if (it->second.pending && m_shared)
            Collect(real, kRuntimeHeightUploadsPerFrame);
        it = m_ready.find(key);
        if (it == m_ready.end())
            return nullptr;
        return it->second.ready.texture ? &it->second.ready : nullptr;
    }
    if (m_requestFrame != frame)
    {
        m_requestFrame = frame;
        m_requestsThisFrame = 0;
    }
    if (m_requestsThisFrame >= kRuntimeHeightRequestsPerFrame)
        return nullptr;
    ++m_requestsThisFrame;
    const unsigned ticket = m_nextTicket++;
    Slot slot = {ticket, true, false, frame, {nullptr, 0.0f, 0u, false}};
    if (Request(source, ticket))
        ++m_stats.requested;
    else
    {
        slot.pending = false;
        slot.failed = true;
        ++m_stats.refused;
    }
    m_ready[key] = slot;
    return nullptr;
}

void RuntimeHeights::Collect(IDirect3DDevice9* real, unsigned limit)
{
    if (!m_shared)
        return;
    std::deque<Done> finished;
    {
        std::lock_guard<std::mutex> guard(m_shared->lock);
        while (!m_shared->done.empty() && finished.size() < limit)
        {
            finished.push_back(std::move(m_shared->done.front()));
            m_shared->done.pop_front();
        }
    }
    for (Done& d : finished)
    {
        auto it = m_ready.find(d.key);
        if (it == m_ready.end() || it->second.ticket != d.ticket)
            continue;
        Slot& slot = it->second;
        slot.pending = false;
        m_stats.makeMilliseconds += d.milliseconds;
        IDirect3DTexture9* texture = nullptr;
        if (!d.ok || !real ||
            FAILED(real->CreateTexture(d.width, d.height, d.levels, 0, D3DFMT_L8, D3DPOOL_MANAGED, &texture, nullptr)))
        {
            slot.failed = true;
            ++m_stats.failed;
            continue;
        }
        size_t offset = 0;
        uint32_t w = d.width;
        uint32_t h = d.height;
        for (unsigned k = 0; k < d.levels; ++k)
        {
            D3DLOCKED_RECT rect = {};
            if (SUCCEEDED(texture->LockRect(k, &rect, nullptr, 0)))
            {
                for (uint32_t row = 0; row < h; ++row)
                    std::memcpy(static_cast<uint8_t*>(rect.pBits) + static_cast<size_t>(row) * rect.Pitch,
                                d.l8.data() + offset + static_cast<size_t>(row) * w, w);
                texture->UnlockRect(k);
            }
            offset += static_cast<size_t>(w) * h;
            w = std::max(w / 2, 1u);
            h = std::max(h / 2, 1u);
        }
        slot.ready = {texture, d.depthFactor, std::max(d.width, d.height), d.cutout};
        ++m_stats.made;
        if (d.cutout)
            ++m_stats.cutouts;
    }
}

void RuntimeHeights::Evict(unsigned frame)
{
    size_t live = 0;
    for (const auto& [key, slot] : m_ready)
        live += slot.ready.texture ? 1 : 0;
    while (live > kRuntimeHeightCacheLimit)
    {
        auto oldest = m_ready.end();
        for (auto it = m_ready.begin(); it != m_ready.end(); ++it)
            if (it->second.ready.texture && (oldest == m_ready.end() || it->second.lastUsed < oldest->second.lastUsed))
                oldest = it;
        if (oldest == m_ready.end() || oldest->second.lastUsed == frame)
            break;
        oldest->second.ready.texture->Release();
        m_ready.erase(oldest);
        ++m_stats.evicted;
        --live;
    }
}

void RuntimeHeights::Frame(IDirect3DDevice9* real, unsigned frame)
{
    Collect(real, kRuntimeHeightUploadsPerFrame);
    Evict(frame);
}

void RuntimeHeights::Forget(IDirect3DBaseTexture9* source)
{
    auto it = m_ready.find(reinterpret_cast<uintptr_t>(source));
    if (it == m_ready.end())
        return;
    if (it->second.ready.texture)
        it->second.ready.texture->Release();
    m_ready.erase(it);
}

void RuntimeHeights::ReleaseAll()
{
    for (auto& [key, slot] : m_ready)
        if (slot.ready.texture)
            slot.ready.texture->Release();
    m_ready.clear();
    if (m_shared)
    {
        std::lock_guard<std::mutex> guard(m_shared->lock);
        m_shared->jobs.clear();
        m_shared->done.clear();
    }
}

bool RuntimeHeights::WaitIdle(unsigned milliseconds)
{
    if (!m_shared)
        return true;
    std::unique_lock<std::mutex> lock(m_shared->lock);
    return m_shared->idle.wait_for(lock, std::chrono::milliseconds(milliseconds),
                                   [&] { return m_shared->jobs.empty() && !m_shared->working; });
}
