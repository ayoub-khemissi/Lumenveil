#pragma once

#include <d3d9.h>

#include <array>
#include <cstdint>

enum class ProfileSection : int
{
    Render,
    ShaftMemory,
    March,
    Lamps,
    Effects,
    LampSurfaces,
    LampBlur,
    LampHalos,
    WetSurfaces,
    Composite,
    Water,
    LiquidCopies,
    OpaqueHook,
    BeforePresent,
    Present,
    ClientDraws,
    Count
};

constexpr int kProfileSections = static_cast<int>(ProfileSection::Count);
constexpr int kProfileRing = 6;
constexpr int kProfileMaxPairs = 48;
constexpr double kProfileSummarySeconds = 60.0;

struct ProfileReport
{
    unsigned frames;
    unsigned gpuFrames;
    unsigned gpuDropped;
    double frameMs;
    double longestFrameMs;
    double cpuMs[kProfileSections];
    double gpuMs[kProfileSections];
    double gpuFrameMs;
};

inline const char* ProfileSectionName(ProfileSection s)
{
    static const char* const names[kProfileSections] = {
        "render",        "shaft memory",  "march",       "lamps",          "effects",
        "lamp light",    "lamp blur",     "lamp halos",  "wet surfaces",   "composite",
        "water",         "liquid copies", "opaque hook", "before Present", "Present",
        "client draw calls",
    };
    const int i = static_cast<int>(s);
    return i >= 0 && i < kProfileSections ? names[i] : "?";
}

class FrameProfile
{
public:
    void Configure(bool enabled);
    bool Enabled() const { return m_enabled; }
    void Begin(IDirect3DDevice9* dev, ProfileSection s);
    void End(IDirect3DDevice9* dev, ProfileSection s);
    void AddCpuTicks(ProfileSection s, long long ticks);
    void FrameDone(IDirect3DDevice9* dev);
    void ReleaseQueries();
    void Report(ProfileReport& out, bool reset);
    static long long Now();

private:
    struct Pair
    {
        int section = 0;
        IDirect3DQuery9* begin = nullptr;
        IDirect3DQuery9* end = nullptr;
    };
    struct Slot
    {
        IDirect3DQuery9* disjoint = nullptr;
        IDirect3DQuery9* frequency = nullptr;
        IDirect3DQuery9* frameBegin = nullptr;
        IDirect3DQuery9* frameEnd = nullptr;
        std::array<Pair, kProfileMaxPairs> pairs{};
        int used = 0;
        bool open = false;
        bool pending = false;
    };
    struct Window
    {
        unsigned frames = 0;
        unsigned gpuFrames = 0;
        unsigned gpuDropped = 0;
        long long frameTicks = 0;
        long long longestFrameTicks = 0;
        std::array<long long, kProfileSections> cpuTicks{};
        std::array<double, kProfileSections> gpuSeconds{};
        double gpuFrameSeconds = 0.0;
    };

    bool EnsureQueries(IDirect3DDevice9* dev);
    void OpenSlot();
    bool Poll(Slot& slot);
    void LogWindow();

    bool m_enabled = false;
    bool m_queriesFailed = false;
    bool m_queriesMade = false;
    std::array<Slot, kProfileRing> m_ring{};
    int m_current = 0;
    std::array<long long, kProfileSections> m_cpuStart{};
    std::array<int, kProfileSections> m_openPair{};
    long long m_lastFrameEnd = 0;
    long long m_windowStart = 0;
    Window m_window;
    Window m_report;
};
