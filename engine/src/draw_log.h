#pragma once

#include "call_watch.h"

#include <d3d9.h>

#include <string>

enum class DrawPhase : int
{
    BeforeWorld,
    World,
    OpaqueM2,
    AfterOpaqueM2,
    Liquid,
    AfterLiquid,
    Fog,
    ScreenEffects,
    Interface,
    Count
};

enum class DrawCallKind : int
{
    Primitive,
    IndexedPrimitive,
    PrimitiveUp,
    IndexedPrimitiveUp,
    Clear
};

struct DrawCall
{
    DrawCallKind kind;
    D3DPRIMITIVETYPE type;
    UINT primitives;
    UINT vertices;
    UINT start;
    INT baseVertex;
    UINT upStride;
};

struct DrawLogStats
{
    unsigned captures;
    unsigned rows;
    unsigned draws;
    unsigned d3dQueries;
    unsigned framesWatched;
    unsigned framesDiscarded;
    unsigned filesWritten;
    unsigned timerReads;
    unsigned constantValues;
};

constexpr int kDrawLogOff = 0;
constexpr int kDrawLogOnKey = 1;
constexpr int kDrawLogOnce = 2;

void DrawLogSetDirectory(const std::string& directory);
std::string DrawLogCsvPath();
std::string DrawLogSideFolder();
void DrawLogArm();
void DrawLogCancel();
DrawLogStats DrawLogStatistics();
void DrawLogResetStatistics();
const std::string& DrawLogLastSummary();

void DrawLogMark(DrawPhase phase);
void DrawLogBeforePresent();
void DrawLogAfterPresent(IDirect3DDevice9* real);

void DrawLogVertexShader(IDirect3DVertexShader9* shader);
void DrawLogPixelShader(IDirect3DPixelShader9* shader);
void DrawLogConstants(bool pixel, UINT start, const float* data, UINT count);
void DrawLogTransform(D3DTRANSFORMSTATETYPE state);
void DrawLogFvf(DWORD fvf);
void DrawLogDeclaration(IDirect3DVertexDeclaration9* declaration);
void DrawLogStream(UINT index, IDirect3DVertexBuffer9* buffer, UINT offset, UINT stride);
void DrawLogIndices(IDirect3DIndexBuffer9* buffer);
void DrawLogRenderState(D3DRENDERSTATETYPE state, DWORD value);
void DrawLogRenderTarget(DWORD index, IDirect3DSurface9* surface);
void DrawLogDepthStencil(IDirect3DSurface9* surface);
void DrawLogTexture(DWORD stage, IDirect3DBaseTexture9* texture);
void DrawLogViewport(const D3DVIEWPORT9* viewport);
void DrawLogStateCall();
long long DrawLogDrawBegins();
void DrawLogDrawEnds(const DrawCall& call, long long begin);

unsigned long long BytecodeHash(const void* data, size_t size);
