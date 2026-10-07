// On-console tests for the Wii port. Run tests.dol from the SD card (or in
// Dolphin with the SD card folder synced); results go to the OSReport UART,
// to sd:/wiitests.log and to the screen. The rasterizer cases read the frame
// back from the EFB: in Dolphin, turn off "Skip EFB Access from CPU" and
// "Store EFB Copies to Texture Only" (Graphics > Hacks) so the reads see it.

#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "CKAll.h"
#include "StaticPlugins.h"
#include "TestFramework.h"
#include "VxWiiPlatform.h"
#include "WiiSystem.h"

namespace
{
    int g_TvWindow;

    int FindRenderEngine(CKPluginManager *pluginManager)
    {
        const int count = pluginManager->GetPluginCount(CKPLUGIN_RENDERENGINE_DLL);
        for (int i = 0; i < count; ++i)
        {
            CKPluginEntry *entry = pluginManager->GetPluginInfo(CKPLUGIN_RENDERENGINE_DLL, i);
            CKPluginDll *dll = entry ? pluginManager->GetPluginDllInfo(entry->m_PluginDllIndex) : NULL;
            const char *name = dll ? dll->m_DllFileName.CStr() : NULL;
            if (name && strstr(name, "CK2_3D"))
                return i;
        }
        return -1;
    }

    CKContext *CreateContext()
    {
        CKPluginManager *pluginManager = CKGetPluginManager();
        if (!RegisterStaticPlugins(pluginManager))
            return NULL;
        const int renderEngine = FindRenderEngine(pluginManager);
        CKContext *context = NULL;
#if CKVERSION == 0x13022002
        const CKERROR err = CKCreateContext(&context, (WIN_HANDLE)&g_TvWindow, renderEngine, 0);
#else
        (void)renderEngine;
        const CKERROR err = CKCreateContext(&context, (WIN_HANDLE)&g_TvWindow, 0);
#endif
        return err == CK_OK ? context : NULL;
    }
}

int main(int argc, char **argv)
{
    const bool storage = wiisystem::Init(argc, argv);
    VxWiiSetApplicationPath(wiisystem::GetGamePath());
    VxWiiSetDisplaySize(wiisystem::GetRenderWidth(), wiisystem::GetRenderHeight());

    if (storage)
        wiitest::OpenLog("sd:/wiitests.log");
    wiitest::Log("Ballance Wii tests");

    RunChunkTests();

    WT_CHECK(CKStartUp() == CK_OK, "CKStartUp");
    CKContext *context = CreateContext();
    WT_CHECK(context != NULL, "CK context");
    RunReaderTests();
    if (storage)
    {
        RunFileTests(context);
        RunSoundTests(context);
        RunFontTests(context);
        RunDatabaseTests(context);
    }
    else
    {
        wiitest::Log("No storage: file and sound tests skipped");
    }

    RunRasterizerTests();
    RunSceneTests(context);
    RunBehaviorTests(context);
    RunPhysicsTests(context);
    if (storage)
        BuildDemoComposition(context);

    char summary[160];
    snprintf(summary, sizeof(summary), "  %d checks, %d failed.", wiitest::CheckCount(), wiitest::FailureCount());
    wiitest::Log("RESULT: %s", wiitest::FailureCount() == 0 ? "PASS" : "FAIL");
    wiitest::Log("%s", summary + 2);
    wiitest::CloseLog();

    wiisystem::ShowMessage(wiitest::FailureCount() == 0 ? "Tests passed" : "Tests FAILED", summary);
    wiisystem::Exit();
    return 0;
}
