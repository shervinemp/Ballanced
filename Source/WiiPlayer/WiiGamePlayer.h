#ifndef WIIGAMEPLAYER_H
#define WIIGAMEPLAYER_H

#include "CKAll.h"

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

private:
    bool InitEngine();
    bool SetupManagers();
    bool SetupPaths();
    bool FinishLoad(const char *filename, const char *resolvedFile);
    bool Update();
    void OpenHomeMenu();

    void DrainPlayerCommands();
    int ExecutePlayerCommand(const TTPlayerCommand &command);
    static int HandlePlayerCommand(InterfaceManager *manager, const TTPlayerCommand &command, void *userData);

    CGameConfig m_Config;
    CGameInfo *m_GameInfo;
    bool m_Running;

    CKContext *m_CKContext;
    CKRenderContext *m_RenderContext;
    CKRenderManager *m_RenderManager;
    CKTimeManager *m_TimeManager;
    CKMessageManager *m_MessageManager;
};

#endif // WIIGAMEPLAYER_H
