#include "frame_profile.h"

#include "log.h"

#include <windows.h>

#include <algorithm>

namespace
{
long long Frequency()
{
    static const long long frequency = [] {
        LARGE_INTEGER f = {};
        QueryPerformanceFrequency(&f);
        return f.QuadPart > 0 ? f.QuadPart : 1;
    }();
    return frequency;
}

double TicksToMs(long long ticks)
{
    return static_cast<double>(ticks) * 1000.0 / static_cast<double>(Frequency());
}

void Release(IDirect3DQuery9*& q)
{
    if (q)
        q->Release();
    q = nullptr;
}

bool Make(IDirect3DDevice9* dev, D3DQUERYTYPE type, IDirect3DQuery9*& q)
{
    return SUCCEEDED(dev->CreateQuery(type, &q)) && q;
}

template <typename T>
HRESULT Read(IDirect3DQuery9* q, T& value)
{
    return q->GetData(&value, sizeof(value), 0);
}
}

long long FrameProfile::Now()
{
    LARGE_INTEGER t = {};
    QueryPerformanceCounter(&t);
    return t.QuadPart;
}

void FrameProfile::Configure(bool enabled)
{
    if (enabled == m_enabled)
        return;
    m_enabled = enabled;
    m_cpuStart.fill(0);
    m_openPair.fill(-1);
    m_lastFrameEnd = 0;
    m_windowStart = 0;
    m_window = {};
    if (!enabled)
        ReleaseQueries();
    VF_LOG_INFO("perf: frame profile %s", enabled ? "on (PerfLog=1), a summary every 60 s" : "off");
}

bool FrameProfile::EnsureQueries(IDirect3DDevice9* dev)
{
    if (m_queriesMade)
        return true;
    if (m_queriesFailed)
        return false;
    bool ok = true;
    for (Slot& slot : m_ring)
    {
        ok = ok && Make(dev, D3DQUERYTYPE_TIMESTAMPDISJOINT, slot.disjoint) &&
             Make(dev, D3DQUERYTYPE_TIMESTAMPFREQ, slot.frequency) &&
             Make(dev, D3DQUERYTYPE_TIMESTAMP, slot.frameBegin) && Make(dev, D3DQUERYTYPE_TIMESTAMP, slot.frameEnd);
        for (Pair& p : slot.pairs)
            ok = ok && Make(dev, D3DQUERYTYPE_TIMESTAMP, p.begin) && Make(dev, D3DQUERYTYPE_TIMESTAMP, p.end);
    }
    if (!ok)
    {
        ReleaseQueries();
        m_queriesFailed = true;
        VF_LOG_INFO("perf: GPU timestamp queries unavailable; main-thread times only");
        return false;
    }
    m_queriesMade = true;
    m_current = 0;
    return true;
}

void FrameProfile::ReleaseQueries()
{
    for (Slot& slot : m_ring)
    {
        Release(slot.disjoint);
        Release(slot.frequency);
        Release(slot.frameBegin);
        Release(slot.frameEnd);
        for (Pair& p : slot.pairs)
        {
            Release(p.begin);
            Release(p.end);
        }
        slot.used = 0;
        slot.open = false;
        slot.pending = false;
    }
    m_queriesMade = false;
    m_openPair.fill(-1);
}

void FrameProfile::OpenSlot()
{
    Slot& slot = m_ring[m_current];
    slot.disjoint->Issue(D3DISSUE_BEGIN);
    slot.frameBegin->Issue(D3DISSUE_END);
    slot.used = 0;
    slot.open = true;
    slot.pending = false;
}

void FrameProfile::Begin(IDirect3DDevice9* dev, ProfileSection s)
{
    if (!m_enabled)
        return;
    const int i = static_cast<int>(s);
    m_cpuStart[i] = Now();
    m_openPair[i] = -1;
    (void)dev;
    if (!m_queriesMade)
        return;
    Slot& slot = m_ring[m_current];
    if (!slot.open || slot.used >= kProfileMaxPairs)
        return;
    Pair& p = slot.pairs[slot.used];
    p.section = i;
    p.begin->Issue(D3DISSUE_END);
    m_openPair[i] = slot.used++;
}

void FrameProfile::End(IDirect3DDevice9* dev, ProfileSection s)
{
    (void)dev;
    if (!m_enabled)
        return;
    const int i = static_cast<int>(s);
    if (m_cpuStart[i] == 0)
        return;
    AddCpuTicks(s, Now() - m_cpuStart[i]);
    m_cpuStart[i] = 0;
    const int pair = m_openPair[i];
    m_openPair[i] = -1;
    if (pair < 0 || !m_queriesMade)
        return;
    Slot& slot = m_ring[m_current];
    if (slot.open && pair < slot.used)
        slot.pairs[pair].end->Issue(D3DISSUE_END);
}

void FrameProfile::AddCpuTicks(ProfileSection s, long long ticks)
{
    if (!m_enabled || ticks < 0)
        return;
    const int i = static_cast<int>(s);
    m_window.cpuTicks[i] += ticks;
    m_report.cpuTicks[i] += ticks;
}

bool FrameProfile::Poll(Slot& slot)
{
    BOOL disjoint = FALSE;
    UINT64 frequency = 0;
    UINT64 frameBegin = 0;
    UINT64 frameEnd = 0;
    HRESULT hr = Read(slot.disjoint, disjoint);
    if (hr == S_OK)
        hr = Read(slot.frequency, frequency);
    if (hr == S_OK)
        hr = Read(slot.frameBegin, frameBegin);
    if (hr == S_OK)
        hr = Read(slot.frameEnd, frameEnd);
    std::array<double, kProfileSections> seconds{};
    for (int k = 0; hr == S_OK && k < slot.used; ++k)
    {
        UINT64 a = 0;
        UINT64 b = 0;
        hr = Read(slot.pairs[k].begin, a);
        if (hr == S_OK)
            hr = Read(slot.pairs[k].end, b);
        if (hr == S_OK && b >= a && frequency > 0)
            seconds[slot.pairs[k].section] += static_cast<double>(b - a) / static_cast<double>(frequency);
    }
    if (hr == S_FALSE)
        return false;
    slot.pending = false;
    if (hr != S_OK || disjoint || frequency == 0 || frameEnd < frameBegin)
    {
        ++m_window.gpuDropped;
        ++m_report.gpuDropped;
        return true;
    }
    const double span = static_cast<double>(frameEnd - frameBegin) / static_cast<double>(frequency);
    for (Window* w : {&m_window, &m_report})
    {
        ++w->gpuFrames;
        w->gpuFrameSeconds += span;
        for (int k = 0; k < kProfileSections; ++k)
            w->gpuSeconds[k] += seconds[k];
    }
    return true;
}

void FrameProfile::FrameDone(IDirect3DDevice9* dev)
{
    if (!m_enabled)
        return;
    const long long now = Now();
    if (m_lastFrameEnd != 0)
    {
        const long long ticks = now - m_lastFrameEnd;
        for (Window* w : {&m_window, &m_report})
        {
            ++w->frames;
            w->frameTicks += ticks;
            w->longestFrameTicks = std::max(w->longestFrameTicks, ticks);
        }
    }
    m_lastFrameEnd = now;
    if (m_windowStart == 0)
        m_windowStart = now;

    if (EnsureQueries(dev))
    {
        Slot& slot = m_ring[m_current];
        if (slot.open)
        {
            slot.frameEnd->Issue(D3DISSUE_END);
            slot.disjoint->Issue(D3DISSUE_END);
            slot.frequency->Issue(D3DISSUE_END);
            slot.open = false;
            slot.pending = true;
        }
        for (Slot& s : m_ring)
            if (s.pending)
                Poll(s);
        m_current = (m_current + 1) % kProfileRing;
        Slot& next = m_ring[m_current];
        if (next.pending)
        {
            next.pending = false;
            ++m_window.gpuDropped;
            ++m_report.gpuDropped;
        }
        OpenSlot();
    }

    if (TicksToMs(now - m_windowStart) > kProfileSummarySeconds * 1000.0)
    {
        LogWindow();
        m_window = {};
        m_windowStart = now;
    }
}

void FrameProfile::LogWindow()
{
    const Window& w = m_window;
    if (w.frames == 0)
        return;
    auto cpu = [&](ProfileSection s) { return TicksToMs(w.cpuTicks[static_cast<int>(s)]) / w.frames; };
    auto gpu = [&](ProfileSection s) {
        return w.gpuFrames ? w.gpuSeconds[static_cast<int>(s)] * 1000.0 / w.gpuFrames : 0.0;
    };
    const double frame = TicksToMs(w.frameTicks) / w.frames;
    const double ours = cpu(ProfileSection::Render) + cpu(ProfileSection::Water) +
                        cpu(ProfileSection::LiquidCopies) + cpu(ProfileSection::OpaqueHook) +
                        cpu(ProfileSection::BeforePresent);
    const double present = cpu(ProfileSection::Present);
    VF_LOG_INFO("perf: %u frames, %.1f fps, %.2f ms a frame (longest %.1f); the client %.2f ms (its draw calls %.2f), "
                "the fog %.2f, Present %.2f",
                w.frames, frame > 0.0 ? 1000.0 / frame : 0.0, frame, TicksToMs(w.longestFrameTicks),
                frame - ours - present, cpu(ProfileSection::ClientDraws), ours, present);
    VF_LOG_INFO("perf: main thread, ms a frame: render %.2f (shaft memory %.2f, march %.2f, lamps %.2f with effects "
                "%.2f, light %.2f, blur %.2f, halos %.2f, wet surfaces %.2f, composite %.2f), water %.2f, liquid "
                "copies %.2f, opaque hook %.2f, before Present %.2f",
                cpu(ProfileSection::Render), cpu(ProfileSection::ShaftMemory), cpu(ProfileSection::March),
                cpu(ProfileSection::Lamps), cpu(ProfileSection::Effects), cpu(ProfileSection::LampSurfaces),
                cpu(ProfileSection::LampBlur), cpu(ProfileSection::LampHalos), cpu(ProfileSection::WetSurfaces),
                cpu(ProfileSection::Composite), cpu(ProfileSection::Water), cpu(ProfileSection::LiquidCopies),
                cpu(ProfileSection::OpaqueHook), cpu(ProfileSection::BeforePresent));
    VF_LOG_INFO("perf: GPU, ms a frame (%u frames timed, %u dropped): render %.2f (shaft memory %.2f, march %.2f, "
                "lamps %.2f with effects %.2f, light %.2f, blur %.2f, halos %.2f, wet surfaces %.2f, composite %.2f), "
                "water %.2f, liquid copies %.2f, opaque hook %.2f, before Present %.2f; the frame's span %.2f",
                w.gpuFrames, w.gpuDropped, gpu(ProfileSection::Render), gpu(ProfileSection::ShaftMemory),
                gpu(ProfileSection::March), gpu(ProfileSection::Lamps), gpu(ProfileSection::Effects),
                gpu(ProfileSection::LampSurfaces), gpu(ProfileSection::LampBlur), gpu(ProfileSection::LampHalos),
                gpu(ProfileSection::WetSurfaces), gpu(ProfileSection::Composite), gpu(ProfileSection::Water),
                gpu(ProfileSection::LiquidCopies), gpu(ProfileSection::OpaqueHook), gpu(ProfileSection::BeforePresent),
                w.gpuFrames ? w.gpuFrameSeconds * 1000.0 / w.gpuFrames : 0.0);
    const double lampParts = cpu(ProfileSection::Effects) + cpu(ProfileSection::LampUnits) +
                             cpu(ProfileSection::LampSelect) + cpu(ProfileSection::LampUpload) +
                             cpu(ProfileSection::LampSurfaces) + cpu(ProfileSection::LampBlur) +
                             cpu(ProfileSection::LampHalos);
    VF_LOG_INFO("perf: lamps on the main thread, ms a frame: world copy %.2f, effects %.2f, units %.2f, "
                "selection %.2f, upload %.2f (table %.2f, tile build %.2f, tile lock %.2f, tile write %.2f), light "
                "%.2f, blur %.2f, halos %.2f, the rest %.2f",
                cpu(ProfileSection::WorldCopy), cpu(ProfileSection::Effects), cpu(ProfileSection::LampUnits),
                cpu(ProfileSection::LampSelect), cpu(ProfileSection::LampUpload), cpu(ProfileSection::LampTable),
                cpu(ProfileSection::LampTileBuild), cpu(ProfileSection::LampTileLock),
                cpu(ProfileSection::LampTileWrite), cpu(ProfileSection::LampSurfaces),
                cpu(ProfileSection::LampBlur), cpu(ProfileSection::LampHalos),
                cpu(ProfileSection::Lamps) - cpu(ProfileSection::WorldCopy) - lampParts);
}

void FrameProfile::Report(ProfileReport& out, bool reset)
{
    const Window& w = m_report;
    out = {};
    out.frames = w.frames;
    out.gpuFrames = w.gpuFrames;
    out.gpuDropped = w.gpuDropped;
    out.frameMs = w.frames ? TicksToMs(w.frameTicks) / w.frames : 0.0;
    out.longestFrameMs = TicksToMs(w.longestFrameTicks);
    for (int k = 0; k < kProfileSections; ++k)
    {
        out.cpuMs[k] = w.frames ? TicksToMs(w.cpuTicks[k]) / w.frames : 0.0;
        out.gpuMs[k] = w.gpuFrames ? w.gpuSeconds[k] * 1000.0 / w.gpuFrames : 0.0;
    }
    out.gpuFrameMs = w.gpuFrames ? w.gpuFrameSeconds * 1000.0 / w.gpuFrames : 0.0;
    if (reset)
        m_report = {};
}
