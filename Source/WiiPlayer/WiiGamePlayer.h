#ifndef WIIGAMEPLAYER_H
#define WIIGAMEPLAYER_H

#include "CKAll.h"

#include "AutoplayScript.h"
#include "GameConfig.h"

class CGameInfo;
class InterfaceManager;
struct TTPlayerCommand;

// Runs Ballance's composition on the Wii: the engine part of the desktop
// player (CGamePlayer) without its window and SDL event handling.
class WiiGamePlayer
{
public:
    WiiGamePlayer();
    ~WiiGamePlayer();

    bool Init(const CGameConfig &config);
    bool Load(const char *filename = NULL);
    void Run();
    void Shutdown();

    // Plays from a script (see AutoplayScript.h).
    bool LoadAutoplay(const char *path);

    // Frame time figures for Player.log: counting starts anew, or the figures
    // since then are logged.
    void StartFrameReport();
    void LogFrameReport();

    CKRenderContext *GetRenderContext() const { return m_RenderContext; }

private:
    bool InitEngine();
    bool SetupManagers();
    bool SetupPaths();
    bool LoadComposition(const char *filename, const char *resolvedFile);
    bool FinishLoad(const char *filename, const char *resolvedFile);
    bool Update();
    void OpenHomeMenu();
    void LogPerformance();
    void StartReport(unsigned long long now);

    void DrainPlayerCommands();
    int ExecutePlayerCommand(const TTPlayerCommand &command);
    static int HandlePlayerCommand(InterfaceManager *manager, const TTPlayerCommand &command, void *userData);

    CGameConfig m_Config;
    CGameInfo *m_GameInfo;
    bool m_Running;
    unsigned int m_FrameCount;
    unsigned int m_ReportFrame;
    unsigned long long m_LastReport; // Time base ticks
    // Since the last report
    unsigned long long m_ProcessTicks;
    unsigned long long m_RenderTicks;
    unsigned long long m_FrameStart;   // When the current frame began
    unsigned long long m_LongestFrame; // Ticks
    unsigned int m_Objects;
    unsigned int m_Triangles;

    CKContext *m_CKContext;
    CKRenderContext *m_RenderContext;
    CKRenderManager *m_RenderManager;
    CKTimeManager *m_TimeManager;
    CKMessageManager *m_MessageManager;

    AutoplayScript m_Autoplay;
};

#endif // WIIGAMEPLAYER_H
