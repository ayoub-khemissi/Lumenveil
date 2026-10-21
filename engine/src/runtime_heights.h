#pragma once

#include <d3d9.h>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace runtimeheight
{
constexpr int kMaxLevels = 8;
constexpr uint32_t kWorkingSize = 256;
constexpr uint32_t kSmallestSide = 4;

struct Weights
{
    int levels = 0;
    float band[kMaxLevels] = {};
};

const Weights& DefaultWeights();

struct Result
{
    int width = 0;
    int height = 0;
    std::vector<float> height01;
    bool cutout = false;
    float lumaSpread = 0.0f;
    float depthFactor = 0.0f;
};

bool Make(const uint8_t* rgba, int width, int height, bool hasAlpha, const Weights& weights, Result& out);
std::vector<uint8_t> L8Levels(const Result& result, unsigned* levelCount);
bool FormatDecodable(uint32_t format);
bool Decode(uint32_t format, uint32_t width, uint32_t height, const uint8_t* bits, uint32_t pitch,
            std::vector<uint8_t>& rgba, bool& hasAlpha);
uint32_t WorkingLevel(uint32_t width, uint32_t height, uint32_t levels);
}

constexpr size_t kRuntimeHeightCacheLimit = 384;
constexpr size_t kRuntimeHeightQueueLimit = 48;
constexpr unsigned kRuntimeHeightRequestsPerFrame = 2;
constexpr unsigned kRuntimeHeightUploadsPerFrame = 4;

struct RuntimeHeightStats
{
    unsigned requested;
    unsigned made;
    unsigned failed;
    unsigned cutouts;
    unsigned evicted;
    unsigned refused;
    double makeMilliseconds;
};

class RuntimeHeights
{
public:
    struct Ready
    {
        IDirect3DTexture9* texture;
        float depthFactor;
        unsigned size;
        bool cutout;
    };

    ~RuntimeHeights();
    const Ready* Find(IDirect3DDevice9* real, IDirect3DBaseTexture9* source, unsigned frame);
    void Frame(IDirect3DDevice9* real, unsigned frame);
    void Forget(IDirect3DBaseTexture9* source);
    void ReleaseAll();
    bool WaitIdle(unsigned milliseconds);
    size_t Cached() const { return m_ready.size(); }
    const RuntimeHeightStats& Stats() const { return m_stats; }

private:
    struct Job
    {
        uintptr_t key;
        unsigned ticket;
        uint32_t format;
        uint32_t width;
        uint32_t height;
        uint32_t pitch;
        std::vector<uint8_t> bits;
    };
    struct Done
    {
        uintptr_t key;
        unsigned ticket;
        bool ok;
        bool cutout;
        float depthFactor;
        uint32_t width;
        uint32_t height;
        unsigned levels;
        double milliseconds;
        std::vector<uint8_t> l8;
    };
    struct Shared
    {
        std::mutex lock;
        std::condition_variable wake;
        std::condition_variable idle;
        std::deque<Job> jobs;
        std::deque<Done> done;
        bool working = false;
        bool stop = false;
    };
    struct Slot
    {
        unsigned ticket;
        bool pending;
        bool failed;
        unsigned lastUsed;
        Ready ready;
    };

    void Start();
    static void Work(std::shared_ptr<Shared> shared);
    bool Request(IDirect3DBaseTexture9* source, unsigned ticket);
    void Collect(IDirect3DDevice9* real, unsigned limit);
    void Evict(unsigned frame);

    std::shared_ptr<Shared> m_shared;
    std::unordered_map<uintptr_t, Slot> m_ready;
    unsigned m_nextTicket = 1;
    unsigned m_requestFrame = 0;
    unsigned m_requestsThisFrame = 0;
    RuntimeHeightStats m_stats = {};
};
