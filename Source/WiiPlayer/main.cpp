#include <stdio.h>

#include "GameConfig.h"
#include "Logger.h"
#include "Utils.h"
#include "VxWiiPlatform.h"
#include "WiiGamePlayer.h"
#include "WiiPhysicsShapes.h"
#include "WiiSystem.h"

namespace
{
    void LogPhysicsShapes(const char *text)
    {
        CLogger::Get().Info("%s", text);
    }

    unsigned int FreeMemory()
    {
        u32 available = 0;
        wiisystem::GetMemoryStatus(NULL, &available);
        return available;
    }

    // Collision surfaces built while loading stay in memory, and in Wii/Cache/
    // for the next start, so loading a level again does not build them again.
    // [Wii] PhysicsCache = 0 (off), 1 (memory) or 2 (memory and cache files);
    // PhysicsCacheMemory = megabytes of unused surfaces kept (the level before)
    // while more than 16 MB are free; PhysicsCacheVerify and PhysicsCacheLog
    // check and list the surfaces.
    void ConfigurePhysicsShapes(bool haveIni, const char *iniPath, const char *gamePath)
    {
        int mode = wiiphysicsshapes::STORAGE;
        int megabytes = 8;
        bool verify = false, log = false;
        if (haveIni)
        {
            utils::IniGetInteger("Wii", "PhysicsCache", mode, iniPath);
            utils::IniGetInteger("Wii", "PhysicsCacheMemory", megabytes, iniPath);
            utils::IniGetBoolean("Wii", "PhysicsCacheVerify", verify, iniPath);
            utils::IniGetBoolean("Wii", "PhysicsCacheLog", log, iniPath);
        }
        static char folder[256];
        snprintf(folder, sizeof(folder), "%sWii/Cache/", gamePath);
        wiiphysicsshapes::Settings settings;
        settings.CacheMode = mode <= 0 ? wiiphysicsshapes::OFF
                             : mode == 1 ? wiiphysicsshapes::MEMORY
                                         : wiiphysicsshapes::STORAGE;
        settings.MemoryBudget = (unsigned int)(megabytes > 0 ? megabytes : 0) << 20;
        settings.MemoryReserve = 16u << 20;
        settings.FreeMemory = FreeMemory;
        settings.Folder = folder;
        settings.Verify = verify;
        settings.LogShapes = log;
        settings.Log = LogPhysicsShapes;
        wiiphysicsshapes::Configure(settings);
    }

    // Ballance's language for the console's: 0 German, 1 English, 2 Spanish,
    // 3 Italian, 4 French. Other languages get English.
    int ConsoleLanguage()
    {
        switch (CONF_GetLanguage())
        {
        case CONF_LANG_GERMAN: return 0;
        case CONF_LANG_SPANISH: return 2;
        case CONF_LANG_ITALIAN: return 3;
        case CONF_LANG_FRENCH: return 4;
        default: return 1;
        }
    }
}

int main(int argc, char **argv)
{
    const bool storage = wiisystem::Init(argc, argv);
    const char *gamePath = wiisystem::GetGamePath();
    VxWiiSetApplicationPath(gamePath);
    VxWiiSetDisplaySize(wiisystem::GetRenderWidth(), wiisystem::GetRenderHeight());
    VxWiiSetPixelAspect(wiisystem::IsWidescreen() ? 4.0f / 3.0f : 1.0f);

    if (!storage)
    {
        wiisystem::ShowMessage("No SD card or USB storage device was found.",
                               "  Insert the SD card or USB drive that holds Ballance and try again.");
        wiisystem::Exit();
    }

    CGameConfig config;
    config.SetRuntimeBasePath(gamePath);
    // Player.ini can still choose another language.
    config.langId = ConsoleLanguage();
    config.LoadFromIni();
    XString iniPath;
    const bool haveIni = config.ResolvePath(eConfigPath, iniPath);
    // Tilt controls are on unless Player.ini turns them off.
    bool tilt = true;
    if (haveIni && utils::IniGetBoolean("Wii", "TiltControls", tilt, iniPath.CStr()))
        wiisystem::SetTiltControls(tilt);
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
    ConfigurePhysicsShapes(haveIni, iniPath.CStr(), gamePath);

    XString cmoPath;
    if (!config.ResolvePath(eCmoPath, cmoPath) || !utils::FileOrDirectoryExists(cmoPath.CStr()))
    {
        char message[512];
        snprintf(message, sizeof(message),
                 "  Copy the contents of your Ballance installation (base.cmo,\n"
                 "  Database.tdb and the 3D Entities, Sounds, Sounds_low,\n"
                 "  Textures and Text folders) to %s next to boot.dol.", gamePath);
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
            // A script can play the game for benchmarks (AutoplayScript.h).
            char autoplay[256];
            if (haveIni && utils::IniGetString("Wii", "Autoplay", autoplay, sizeof(autoplay), iniPath.CStr()) &&
                autoplay[0])
                player.LoadAutoplay(utils::JoinPath(gamePath, autoplay).CStr());
            player.Run();
        }
        player.Shutdown();
    }
    // Finish writing the shape cache while the storage device is mounted.
    wiiphysicsshapes::Shutdown();

    if (!config.SaveToIni() ||
        (haveIni && !utils::IniSetBoolean("Wii", "TiltControls", wiisystem::GetTiltControls(), iniPath.CStr())))
        CLogger::Get().Warn("Could not save Player.ini.");
    CLogger::Get().Close();

    wiisystem::Exit();
    return 0;
}
