#include <stdio.h>

#include "GameConfig.h"
#include "Logger.h"
#include "Utils.h"
#include "VxWiiPlatform.h"
#include "WiiGamePlayer.h"
#include "WiiSystem.h"

int main(int argc, char **argv)
{
    const bool storage = wiisystem::Init(argc, argv);
    const char *gamePath = wiisystem::GetGamePath();
    VxWiiSetApplicationPath(gamePath);
    VxWiiSetDisplaySize(wiisystem::GetRenderWidth(), wiisystem::GetRenderHeight());

    if (!storage)
    {
        wiisystem::ShowMessage("No SD card or USB storage device was found.",
                               "  Insert the SD card or USB drive that holds Ballance and try again.");
        wiisystem::Exit();
    }

    CGameConfig config;
    config.SetRuntimeBasePath(gamePath);
    config.LoadFromIni();
    // The TV picture has one mode; desktop window settings do not apply.
    config.driver = 0;
    config.width = wiisystem::GetRenderWidth();
    config.height = wiisystem::GetRenderHeight();
    config.bpp = 32;
    config.fullscreen = false;
    config.manualSetup = false;

    XString logPath;
    if (config.ResolvePath(eLogPath, logPath))
        CLogger::Get().Open(logPath.CStr(), config.logMode == eLogOverwrite,
                            config.verbose ? CLogger::LEVEL_DEBUG : CLogger::LEVEL_INFO);
    CLogger::Get().Info("Ballance for Wii starting from %s", gamePath);

    XString cmoPath;
    if (!config.ResolvePath(eCmoPath, cmoPath) || !utils::FileOrDirectoryExists(cmoPath.CStr()))
    {
        char message[512];
        snprintf(message, sizeof(message),
                 "  Copy the contents of your Ballance installation (base.cmo,\n"
                 "  Database.tdb and the 3D Entities, Sounds, Textures and Text\n"
                 "  folders) to %s next to boot.dol.", gamePath);
        wiisystem::ShowMessage("The Ballance game files were not found.", message);
        // Close the log while the storage device is still mounted.
        CLogger::Get().Close();
        wiisystem::Exit();
    }

    {
        WiiGamePlayer player;
        if (!player.Init(config))
        {
            CLogger::Get().Error("Failed to start the engine.");
            wiisystem::ShowMessage("Ballance could not start.", "  Details were written to Player.log.");
        }
        else if (!player.Load())
        {
            CLogger::Get().Error("Failed to load the game.");
            wiisystem::ShowMessage("The game files could not be loaded.", "  Details were written to Player.log.");
        }
        else
        {
            player.Run();
        }
        player.Shutdown();
    }

    if (!config.SaveToIni())
        CLogger::Get().Warn("Could not save Player.ini.");
    CLogger::Get().Close();

    wiisystem::Exit();
    return 0;
}
