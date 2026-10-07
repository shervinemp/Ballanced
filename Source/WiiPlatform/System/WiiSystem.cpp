#include "WiiSystem.h"

#include <fat.h>
#include <wiiuse/wpad.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

namespace wiisystem
{
    namespace
    {
        // The game renders a 640x480 picture everywhere; PAL 50 Hz modes stretch
        // it vertically when the frame buffer is copied out.
        const int kRenderWidth = 640;
        const int kRenderHeight = 480;

        GXRModeObj *g_VideoMode = NULL;
        bool g_Widescreen = false;
        bool g_StorageMounted = false;
        char g_GamePath[256] = "";
        volatile QuitAction g_QuitRequest = QUIT_NONE;
        bool g_HomeMenuRequested = false;

        bool g_PointerVisible = false;
        float g_PointerX = 0.0f;
        float g_PointerY = 0.0f;
        float g_PointerAngle = 0.0f;

        // Button callbacks run in interrupt context: only record the request.
        void OnPowerButton()
        {
            g_QuitRequest = QUIT_POWER_OFF;
        }

        void OnResetButton(u32 irq, void *ctx)
        {
            (void)irq;
            (void)ctx;
            if (g_QuitRequest == QUIT_NONE)
                g_QuitRequest = QUIT_TO_LOADER;
        }

        void OnWiimotePowerButton(s32 channel)
        {
            (void)channel;
            g_QuitRequest = QUIT_POWER_OFF;
        }

        bool FileExists(const char *path)
        {
            struct stat st;
            return stat(path, &st) == 0 && S_ISREG(st.st_mode);
        }

        bool IsGameFolder(const char *folder)
        {
            char path[300];
            snprintf(path, sizeof(path), "%sbase.cmo", folder);
            return FileExists(path);
        }

        bool UseGameFolder(const char *folder)
        {
            if (!folder || !*folder || strlen(folder) + 1 >= sizeof(g_GamePath))
                return false;

            char candidate[sizeof(g_GamePath)];
            strcpy(candidate, folder);
            const size_t length = strlen(candidate);
            if (candidate[length - 1] != '/')
                strcat(candidate, "/");

            if (!IsGameFolder(candidate))
                return false;
            strcpy(g_GamePath, candidate);
            return true;
        }

        void LocateGameFolder(int argc, char **argv)
        {
            // The Homebrew Channel passes the DOL path, e.g. "sd:/apps/ballance/boot.dol".
            if (argc > 0 && argv && argv[0])
            {
                char folder[sizeof(g_GamePath)];
                strncpy(folder, argv[0], sizeof(folder) - 1);
                folder[sizeof(folder) - 1] = '\0';
                char *slash = strrchr(folder, '/');
                if (slash)
                {
                    slash[1] = '\0';
                    if (UseGameFolder(folder))
                        return;
                }
            }

            static const char *const kCandidates[] = {
                "sd:/apps/ballance/",
                "usb:/apps/ballance/",
                "sd:/ballance/",
                "usb:/ballance/",
            };
            for (size_t i = 0; i < sizeof(kCandidates) / sizeof(kCandidates[0]); ++i)
            {
                if (UseGameFolder(kCandidates[i]))
                    return;
            }

            // Nothing found: keep the default so error messages name a real place.
            strcpy(g_GamePath, "sd:/apps/ballance/");
        }

        void ScanControllers()
        {
            WPAD_ScanPads();
            PAD_ScanPads();
        }

        bool AnyButtonPressed()
        {
            for (int chan = 0; chan < 4; ++chan)
            {
                if (WPAD_ButtonsDown(chan) || PAD_ButtonsDown(chan))
                    return true;
            }
            return false;
        }
    }

    bool Init(int argc, char **argv)
    {
        VIDEO_Init();
        WPAD_Init();
        PAD_Init();
        // Log output goes to the OSReport UART: Dolphin shows it in its log
        // window, and on a console the write is ignored.
        SYS_STDIO_Report(true);

        SYS_SetPowerCallback(OnPowerButton);
        SYS_SetResetCallback(OnResetButton);
        WPAD_SetPowerButtonCallback(OnWiimotePowerButton);

        g_VideoMode = VIDEO_GetPreferredMode(NULL);
        g_Widescreen = CONF_GetAspectRatio() == CONF_ASPECT_16_9;
        if (g_Widescreen)
        {
            // Fill the width of a 16:9 set; the rasterizer widens the projection to match.
            g_VideoMode->viWidth = 678;
            g_VideoMode->viXOrigin = (VI_MAX_WIDTH_NTSC - 678) / 2;
        }

        g_StorageMounted = fatInitDefault();
        if (g_StorageMounted)
        {
            LocateGameFolder(argc, argv);
            chdir(g_GamePath);
        }
        else
        {
            strcpy(g_GamePath, "sd:/apps/ballance/");
        }
        return g_StorageMounted;
    }

    GXRModeObj *GetVideoMode()
    {
        return g_VideoMode;
    }

    int GetRenderWidth()
    {
        return kRenderWidth;
    }

    int GetRenderHeight()
    {
        return kRenderHeight;
    }

    bool IsWidescreen()
    {
        return g_Widescreen;
    }

    const char *GetGamePath()
    {
        return g_GamePath;
    }

    QuitAction GetQuitRequest()
    {
        return g_QuitRequest;
    }

    void RequestQuit(QuitAction action)
    {
        // A power-off request always wins.
        if (g_QuitRequest != QUIT_POWER_OFF)
            g_QuitRequest = action;
    }

    void Exit()
    {
        const QuitAction action = g_QuitRequest;

        // Flush pending writes (saved scores, settings, logs) before leaving.
        fflush(NULL);
        if (g_StorageMounted)
        {
            fatUnmount("sd:");
            fatUnmount("usb:");
        }
        WPAD_Shutdown();

        switch (action)
        {
        case QUIT_POWER_OFF:
            SYS_ResetSystem(SYS_POWEROFF, 0, 0);
            break;
        case QUIT_TO_MENU:
            SYS_ResetSystem(SYS_RETURNTOMENU, 0, 0);
            break;
        default:
            break;
        }
        // Returns to the loader (the Homebrew Channel) when one is present.
        exit(0);
    }

    void RequestHomeMenu()
    {
        g_HomeMenuRequested = true;
    }

    bool ConsumeHomeMenuRequest()
    {
        const bool requested = g_HomeMenuRequested;
        g_HomeMenuRequested = false;
        return requested;
    }

    QuitAction ShowHomeMenu()
    {
        GXRModeObj *mode = g_VideoMode ? g_VideoMode : VIDEO_GetPreferredMode(NULL);
        static void *s_MenuFrameBuffer = NULL;
        if (!s_MenuFrameBuffer)
            s_MenuFrameBuffer = MEM_K0_TO_K1(SYS_AllocateFramebuffer(mode));

        console_init(s_MenuFrameBuffer, 20, 20, mode->fbWidth, mode->xfbHeight,
                     mode->fbWidth * VI_DISPLAY_PIX_SZ);
        VIDEO_SetNextFramebuffer(s_MenuFrameBuffer);
        VIDEO_Flush();

        printf("\x1b[2J\x1b[4;0H");
        printf("        HOME Menu\n\n\n");
        printf("        A / HOME    Return to the game\n\n");
        printf("        +           Exit to the Homebrew Channel\n\n");
        printf("        -           Exit to the Wii Menu\n");

        // Let go of HOME before reading the choice.
        for (int frame = 0; frame < 15; ++frame)
        {
            ScanControllers();
            VIDEO_WaitVSync();
        }

        QuitAction choice = QUIT_NONE;
        while (g_QuitRequest == QUIT_NONE)
        {
            ScanControllers();
            u32 wiiButtons = 0;
            u32 padButtons = 0;
            for (int chan = 0; chan < 4; ++chan)
            {
                wiiButtons |= WPAD_ButtonsDown(chan);
                padButtons |= PAD_ButtonsDown(chan);
            }
            if (wiiButtons & (WPAD_BUTTON_A | WPAD_BUTTON_HOME | WPAD_CLASSIC_BUTTON_A | WPAD_CLASSIC_BUTTON_HOME) ||
                padButtons & (PAD_BUTTON_A | PAD_BUTTON_START))
                break;
            if (wiiButtons & (WPAD_BUTTON_PLUS | WPAD_CLASSIC_BUTTON_PLUS) || padButtons & PAD_BUTTON_X)
            {
                choice = QUIT_TO_LOADER;
                break;
            }
            if (wiiButtons & (WPAD_BUTTON_MINUS | WPAD_CLASSIC_BUTTON_MINUS) || padButtons & PAD_BUTTON_Y)
            {
                choice = QUIT_TO_MENU;
                break;
            }
            VIDEO_WaitVSync();
        }

        if (choice != QUIT_NONE)
            RequestQuit(choice);
        if (g_QuitRequest != QUIT_NONE)
            return g_QuitRequest;

        // Swallow the button that closed the menu so the game doesn't see it.
        for (int frame = 0; frame < 10; ++frame)
        {
            ScanControllers();
            VIDEO_WaitVSync();
        }
        // console_init took over stdout; send the log back to the UART.
        SYS_STDIO_Report(true);
        return QUIT_NONE;
    }

    void SetPointer(bool visible, float x, float y, float angle)
    {
        g_PointerVisible = visible;
        g_PointerX = x;
        g_PointerY = y;
        g_PointerAngle = angle;
    }

    bool GetPointer(float *x, float *y, float *angle)
    {
        if (x)
            *x = g_PointerX;
        if (y)
            *y = g_PointerY;
        if (angle)
            *angle = g_PointerAngle;
        return g_PointerVisible;
    }

    void ShowMessage(const char *title, const char *message)
    {
        GXRModeObj *mode = g_VideoMode ? g_VideoMode : VIDEO_GetPreferredMode(NULL);
        static void *s_ConsoleFrameBuffer = NULL;
        if (!s_ConsoleFrameBuffer)
            s_ConsoleFrameBuffer = MEM_K0_TO_K1(SYS_AllocateFramebuffer(mode));

        VIDEO_Configure(mode);
        console_init(s_ConsoleFrameBuffer, 20, 20, mode->fbWidth, mode->xfbHeight,
                     mode->fbWidth * VI_DISPLAY_PIX_SZ);
        VIDEO_SetNextFramebuffer(s_ConsoleFrameBuffer);
        VIDEO_SetBlack(FALSE);
        VIDEO_Flush();
        VIDEO_WaitVSync();

        // Clear the screen and move to the top-left corner.
        printf("\x1b[2J\x1b[2;0H");
        printf("  Ballance\n\n");
        printf("  %s\n\n", title ? title : "");
        printf("%s\n\n", message ? message : "");
        printf("  Press any button to exit.\n");

        // Wait for the press to be released first so an earlier press doesn't count.
        for (int frame = 0; frame < 30; ++frame)
        {
            ScanControllers();
            VIDEO_WaitVSync();
        }
        while (g_QuitRequest == QUIT_NONE)
        {
            ScanControllers();
            if (AnyButtonPressed())
                break;
            VIDEO_WaitVSync();
        }
    }
}
