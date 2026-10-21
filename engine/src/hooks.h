#pragma once

bool InstallEngineHooks();

void InstallFarClipHooks();
void RequestFarClipRefresh();
void RefreshFarClipNow();
int FarClipRefreshRequests();

void InstallWaterHooks();

struct FogFrameStatus
{
    bool drawn;
    const char* reason;
};

FogFrameStatus LastFogFrameStatus();
