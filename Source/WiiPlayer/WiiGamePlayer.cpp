#include "WiiGamePlayer.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "InterfaceManager.h"
#include "Logger.h"
#include "StaticPlugins.h"
#include "Utils.h"
#include "WiiSystem.h"

extern bool EditScript(CKLevel *level, const CGameConfig &config, const char *resolvedFile);

namespace
{
    // The engine wants a window handle; the "window" is the TV picture.
    int g_TvWindow;

    CKERROR LogRedirect(CKUICallbackStruct &cbStruct, void *userData)
    {
        (void)userData;
        if (cbStruct.Reason == CKUIM_OUTTOCONSOLE || cbStruct.Reason == CKUIM_OUTTOINFOBAR ||
            cbStruct.Reason == CKUIM_DEBUGMESSAGESEND)
        {
            static XString last = "";
            if (last.Compare(cbStruct.ConsoleString))
            {
                CLogger::Get().Info(cbStruct.ConsoleString);
                last = cbStruct.ConsoleString;
            }
        }
        return CK_OK;
    }

    bool IsRenderEngineName(const char *path)
    {
        const char *name = utils::FindLastPathSeparator(path);
        name = name ? name + 1 : path;
        if (strncasecmp(name, "lib", 3) == 0)
            name += 3;
        return strncasecmp(name, "CK2_3D", 6) == 0 && (name[6] == '\0' || name[6] == '.');
    }

    int FindRenderEngine(CKPluginManager *pluginManager)
    {
        const int count = pluginManager->GetPluginCount(CKPLUGIN_RENDERENGINE_DLL);
        for (int i = 0; i < count; ++i)
        {
            CKPluginEntry *entry = pluginManager->GetPluginInfo(CKPLUGIN_RENDERENGINE_DLL, i);
            CKPluginDll *dll = entry ? pluginManager->GetPluginDllInfo(entry->m_PluginDllIndex) : NULL;
            if (dll && IsRenderEngineName(dll->m_DllFileName.Str()))
                return i;
        }
        return -1;
    }

    bool AddPathIfMissing(CKPathManager *pathManager, int category, const char *path)
    {
        if (!path || !*path)
            return false;
        XString ckPath = path;
        if (pathManager->GetPathIndex(category, ckPath) >= 0)
            return true;
        return pathManager->AddPath(category, ckPath) >= 0;
    }

    void AddDirectoryPathIfExists(CKPathManager *pathManager, int category, const char *basePath, const char *relativePath)
    {
        XString path = utils::JoinPath(basePath, relativePath, true);
        if (!path.IsEmpty() && utils::DirectoryExists(path.CStr()))
            AddPathIfMissing(pathManager, category, path.CStr());
    }

    void RegisterCompositionPaths(CKPathManager *pathManager, const char *resolvedFile)
    {
        XString dir = utils::GetFileDirectory(resolvedFile, true);
        if (dir.IsEmpty())
            return;

        AddPathIfMissing(pathManager, DATA_PATH_IDX, dir.CStr());
        AddDirectoryPathIfExists(pathManager, DATA_PATH_IDX, dir.CStr(), "3D Entities");
        AddPathIfMissing(pathManager, SOUND_PATH_IDX, dir.CStr());
        AddDirectoryPathIfExists(pathManager, SOUND_PATH_IDX, dir.CStr(), "Sounds");
        AddPathIfMissing(pathManager, BITMAP_PATH_IDX, dir.CStr());
        AddDirectoryPathIfExists(pathManager, BITMAP_PATH_IDX, dir.CStr(), "Textures");

        // The game's own scripts read this variable to find their data.
        setenv("Gravity", dir.CStr(), 1);
    }
}

WiiGamePlayer::WiiGamePlayer()
    : m_GameInfo(NULL),
      m_Running(false),
      m_CKContext(NULL),
      m_RenderContext(NULL),
      m_RenderManager(NULL),
      m_TimeManager(NULL),
      m_MessageManager(NULL)
{
}

WiiGamePlayer::~WiiGamePlayer()
{
    Shutdown();
}

bool WiiGamePlayer::Init(const CGameConfig &config)
{
    m_Config = config;

    if (!InitEngine())
        return false;

    if (m_RenderManager->GetRenderDriverCount() == 0)
    {
        CLogger::Get().Error("No render driver found.");
        return false;
    }

    VxDriverDesc *driver = m_RenderManager->GetRenderDriverDescription(0);
    CLogger::Get().Info("Render driver: %s", driver ? driver->DriverDesc : "?");

    CKRECT rect = {0, 0, wiisystem::GetRenderWidth(), wiisystem::GetRenderHeight()};
    m_RenderContext = m_RenderManager->CreateRenderContext((WIN_HANDLE)&g_TvWindow, 0, &rect, FALSE, 32);
    if (!m_RenderContext)
    {
        CLogger::Get().Error("Failed to create the render context.");
        return false;
    }

    m_Running = true;
    return true;
}

bool WiiGamePlayer::InitEngine()
{
    if (CKStartUp() != CK_OK)
    {
        CLogger::Get().Error("CK engine cannot start up.");
        return false;
    }

    CKPluginManager *pluginManager = CKGetPluginManager();
    if (!RegisterStaticPlugins(pluginManager))
    {
        CLogger::Get().Error("Failed to register the built-in plugins.");
        return false;
    }

    const int renderEngine = FindRenderEngine(pluginManager);
    if (renderEngine < 0)
    {
        CLogger::Get().Error("The render engine is missing.");
        return false;
    }

#if CKVERSION == 0x13022002
    CKERROR res = CKCreateContext(&m_CKContext, (WIN_HANDLE)&g_TvWindow, renderEngine, 0);
#else
    CKERROR res = CKCreateContext(&m_CKContext, (WIN_HANDLE)&g_TvWindow, 0);
#endif
    if (res != CK_OK)
    {
        CLogger::Get().Error("Failed to create the CK context (%d).", res);
        return false;
    }

    m_CKContext->SetVirtoolsVersion(CK_VIRTOOLS_DEV, 0x2000043);
    m_CKContext->SetInterfaceMode(FALSE, LogRedirect, 0);

    return SetupManagers() && SetupPaths();
}

bool WiiGamePlayer::SetupManagers()
{
    m_RenderManager = m_CKContext->GetRenderManager();
    m_MessageManager = m_CKContext->GetMessageManager();
    m_TimeManager = m_CKContext->GetTimeManager();
    if (!m_RenderManager || !m_MessageManager || !m_TimeManager)
    {
        CLogger::Get().Error("A core manager is missing.");
        return false;
    }
    if (!m_CKContext->GetManagerByGuid(INPUT_MANAGER_GUID))
    {
        CLogger::Get().Error("The input manager is missing.");
        return false;
    }

    InterfaceManager *interfaceManager = InterfaceManager::GetManager(m_CKContext);
    if (interfaceManager)
        interfaceManager->SetPlayerCommandHandler(&WiiGamePlayer::HandlePlayerCommand, this);
    return true;
}

bool WiiGamePlayer::SetupPaths()
{
    CKPathManager *pm = m_CKContext->GetPathManager();
    if (!pm)
        return false;

    const struct
    {
        PathCategory Category;
        int Index;
    } paths[] = {
        {eDataPath, DATA_PATH_IDX},
        {eSoundPath, SOUND_PATH_IDX},
        {eBitmapPath, BITMAP_PATH_IDX},
    };
    for (size_t i = 0; i < sizeof(paths) / sizeof(paths[0]); ++i)
    {
        XString path;
        if (!m_Config.ResolvePath(paths[i].Category, path, true) || !utils::DirectoryExists(path.CStr()))
        {
            CLogger::Get().Warn("Missing data folder: %s", path.CStr());
            continue;
        }
        pm->AddPath(paths[i].Index, path);
        CLogger::Get().Debug("Path %d: %s", paths[i].Index, path.CStr());
    }
    return true;
}

bool WiiGamePlayer::Load(const char *filename)
{
    if (!m_CKContext)
        return false;

    if (!filename || !*filename)
        filename = m_Config.GetPath(eCmoPath);

    CKPathManager *pm = m_CKContext->GetPathManager();
    XString resolvedFile = filename;
    if (pm->ResolveFileName(resolvedFile, DATA_PATH_IDX) != CK_OK)
    {
        CLogger::Get().Error("Cannot find %s", filename);
        return false;
    }

    RegisterCompositionPaths(pm, resolvedFile.CStr());

    m_CKContext->Reset();
    m_CKContext->ClearAll();

    CKFile *file = m_CKContext->CreateCKFile();
    CKERROR res = file->OpenFile(resolvedFile.Str(), (CK_LOAD_FLAGS)(CK_LOAD_DEFAULT | CK_LOAD_CHECKDEPENDENCIES));
    if (res != CK_OK)
    {
        if (res == CKERR_PLUGINSMISSING)
        {
            const XClassArray<CKFilePluginDependencies> *missing = file->GetMissingPlugins();
            for (CKFilePluginDependencies *it = missing->Begin(); it != missing->End(); ++it)
            {
                for (int i = 0; i < it->m_Guids.Size(); ++i)
                {
                    if (!it->ValidGuids[i])
                        CLogger::Get().Error("Missing plugin GUID %x,%x", it->m_Guids[i].d1, it->m_Guids[i].d2);
                }
            }
        }
        m_CKContext->DeleteCKFile(file);
        CLogger::Get().Error("Failed to open %s (%d)", resolvedFile.CStr(), res);
        return false;
    }

    CKObjectArray *array = CreateCKObjectArray();
    res = file->LoadFileData(array);
    m_CKContext->DeleteCKFile(file);
    DeleteCKObjectArray(array);
    if (res != CK_OK)
    {
        CLogger::Get().Error("Failed to load %s (%d)", resolvedFile.CStr(), res);
        return false;
    }

    if (!FinishLoad(filename, resolvedFile.CStr()))
        return false;

    m_CKContext->Play();
    return true;
}

bool WiiGamePlayer::FinishLoad(const char *filename, const char *resolvedFile)
{
    InterfaceManager *im = InterfaceManager::GetManager(m_CKContext);
    if (im)
    {
        im->SetDriver(0);
        im->SetScreenMode(m_RenderManager->GetRenderDriverDescription(0) ? 0 : -1);
        im->SetRookie(m_Config.rookie);
        im->SetTaskSwitchEnabled(false);

        delete m_GameInfo;
        m_GameInfo = new CGameInfo;
        strcpy(m_GameInfo->path, ".");
        strncpy(m_GameInfo->fileName, filename, sizeof(m_GameInfo->fileName) - 1);
        m_GameInfo->fileName[sizeof(m_GameInfo->fileName) - 1] = '\0';
        im->SetGameInfo(m_GameInfo);
    }

    CKLevel *level = m_CKContext->GetCurrentLevel();
    if (!level)
    {
        CLogger::Get().Error("The composition has no level.");
        return false;
    }
    level->AddRenderContext(m_RenderContext, TRUE);

    // Look through the first camera until the composition picks one.
    const XObjectPointerArray cameras = m_CKContext->GetObjectListByType(CKCID_CAMERA, TRUE);
    if (cameras.Size() != 0)
        m_RenderContext->AttachViewpointToCamera((CKCamera *)cameras[0]);

    // Curves are editing aids.
    const int curveCount = m_CKContext->GetObjectsCountByClassID(CKCID_CURVE);
    CK_ID *curveIds = m_CKContext->GetObjectsListByClassID(CKCID_CURVE);
    for (int i = 0; i < curveCount; ++i)
    {
        CKMesh *mesh = ((CKCurve *)m_CKContext->GetObject(curveIds[i]))->GetCurrentMesh();
        if (mesh)
            mesh->Show(CKHIDE);
    }

    if (m_Config.applyHotfix && im && !EditScript(level, m_Config, resolvedFile))
        CLogger::Get().Warn("Failed to apply the script hotfixes.");

    level->LaunchScene(NULL);
    m_MessageManager->AddMessageType((CKSTRING)"OnClick");
    m_MessageManager->AddMessageType((CKSTRING)"OnDblClick");
    m_RenderContext->Render();
    return true;
}

void WiiGamePlayer::Run()
{
    while (Update())
        continue;
}

bool WiiGamePlayer::Update()
{
    if (wiisystem::GetQuitRequest() != wiisystem::QUIT_NONE)
        return false;
    if (wiisystem::ConsumeHomeMenuRequest())
        OpenHomeMenu();

    DrainPlayerCommands();
    if (!m_Running)
        return false;

    float beforeRender = 0.0f;
    float beforeProcess = 0.0f;
    m_TimeManager->GetTimeToWaitForLimits(beforeRender, beforeProcess);

    bool worked = false;
    if (beforeProcess <= 0.0f)
    {
        m_TimeManager->ResetChronos(FALSE, TRUE);
        m_CKContext->Process();
        worked = true;
    }
    if (beforeRender <= 0.0f)
    {
        m_TimeManager->ResetChronos(TRUE, FALSE);
        m_RenderContext->Render(); // Waits for the vertical blank
        worked = true;
    }
    if (!worked)
        usleep(1000);
    return true;
}

void WiiGamePlayer::OpenHomeMenu()
{
    m_CKContext->Pause();
    if (wiisystem::ShowHomeMenu() == wiisystem::QUIT_NONE)
        m_CKContext->Play();
    else
        m_Running = false;
}

void WiiGamePlayer::DrainPlayerCommands()
{
    InterfaceManager *im = m_CKContext ? InterfaceManager::GetManager(m_CKContext) : NULL;
    if (!im)
        return;
    TTPlayerCommand command;
    while (im->PollPlayerCommand(command))
        ExecutePlayerCommand(command);
}

int WiiGamePlayer::HandlePlayerCommand(InterfaceManager *manager, const TTPlayerCommand &command, void *userData)
{
    (void)manager;
    WiiGamePlayer *player = (WiiGamePlayer *)userData;
    return player ? player->ExecutePlayerCommand(command) : 0;
}

int WiiGamePlayer::ExecutePlayerCommand(const TTPlayerCommand &command)
{
    switch (command.type)
    {
    case TT_PLAYER_COMMAND_NO_GAMEINFO:
        CLogger::Get().Error("The composition raised an exception.");
        m_Running = false;
        wiisystem::RequestQuit(wiisystem::QUIT_TO_LOADER);
        return 1;

    case TT_PLAYER_COMMAND_CMO_RESTART:
        if (m_GameInfo && !Load(m_GameInfo->fileName))
            m_Running = false;
        return 1;

    case TT_PLAYER_COMMAND_CMO_LOAD:
        return Load(command.text.CStr()) ? 1 : 0;

    case TT_PLAYER_COMMAND_EXIT_TO_SYSTEM:
        m_Running = false;
        wiisystem::RequestQuit(wiisystem::QUIT_TO_LOADER);
        return 1;

    case TT_PLAYER_COMMAND_LIMIT_FPS:
        if (command.param0 > 0)
        {
            m_TimeManager->SetFrameRateLimit((float)command.param0);
            m_TimeManager->ChangeLimitOptions(CK_FRAMERATE_LIMIT);
        }
        return 1;

    case TT_PLAYER_COMMAND_EXIT_TO_TITLE:
    case TT_PLAYER_COMMAND_SCREEN_MODE_CHANGE:
    case TT_PLAYER_COMMAND_GO_FULLSCREEN:
    case TT_PLAYER_COMMAND_STOP_FULLSCREEN:
        // The TV picture has a single fixed mode.
        return 1;

    default:
        CLogger::Get().Warn("Unknown player command: %d", command.type);
        return 0;
    }
}

void WiiGamePlayer::Shutdown()
{
    if (m_CKContext)
    {
        m_CKContext->Reset();
        m_CKContext->ClearAll();
        if (m_RenderManager && m_RenderContext)
            m_RenderManager->DestroyRenderContext(m_RenderContext);
        m_RenderContext = NULL;

        CKCloseContext(m_CKContext);
        m_CKContext = NULL;
        CKShutdown();
    }
    delete m_GameInfo;
    m_GameInfo = NULL;
    m_RenderManager = NULL;
    m_TimeManager = NULL;
    m_MessageManager = NULL;
    m_Running = false;
}
