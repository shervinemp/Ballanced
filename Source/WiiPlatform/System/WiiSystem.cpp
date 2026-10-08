#include "WiiSystem.h"

#include <fat.h>
#include <wiiuse/wpad.h>

#include <malloc.h>
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

        // Where MEM2 started before the heap reached it: libogc's sbrk moves the
        // heap from MEM1 to MEM2 once MEM1 is used up, and never moves back.
        void *g_Arena2Start = NULL;

        u64 g_GpuWait = 0;
        u64 g_VsyncWait = 0;

        const int kMaxOverlayItems = 64;
        OverlayItem g_Overlay[OVERLAY_LAYER_COUNT][kMaxOverlayItems];
        int g_OverlayCount[OVERLAY_LAYER_COUNT];

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
        g_Arena2Start = SYS_GetArena2Lo();
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

        // Go to black rather than leave the last frame up while the loader starts.
        VIDEO_SetBlack(TRUE);
        VIDEO_Flush();
        VIDEO_WaitVSync();

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

    // ------------------------------------------------------------------
    // HOME menu

    namespace
    {
        enum HomeButton
        {
            HOME_CLOSE,
            HOME_LOADER,
            HOME_WII_MENU,
            HOME_YES,
            HOME_NO,
            HOME_BUTTON_COUNT
        };

        enum HomeState
        {
            HOME_CLOSED,
            HOME_MAIN,
            HOME_CONFIRM,
            HOME_LEAVING // Closed; waits for the buttons to be let go
        };

        struct HomeMenu
        {
            HomeState State;
            int Focus;
            QuitAction Pending;
            int LeavingFrames;
        };

        HomeMenu g_Home = {HOME_CLOSED, HOME_LOADER, QUIT_NONE, 0};

        const u32 kDim = 0x000000A0;
        const u32 kBar = 0x2A2E36F0;
        const u32 kBarText = 0xFFFFFFFF;
        const u32 kButtonFill = 0xF4F6F8FF;
        const u32 kButtonBorder = 0xA8B0BCFF;
        const u32 kButtonText = 0x283038FF;
        const u32 kFocusFill = 0xD6F1FCFF;
        const u32 kFocusBorder = 0x2FB4E8FF;
        const u32 kDialogFill = 0xEEF1F4F8;
        const u32 kBatteryFull = 0x6CC24AFF;
        const u32 kBatteryEmpty = 0x50555EFF;
        const u32 kAbsent = 0x60656EFF;

        struct Box
        {
            float X0, Y0, X1, Y1;
        };

        Box HomeButtonBox(int button)
        {
            static const Box kBoxes[HOME_BUTTON_COUNT] = {
                {520.0f, 12.0f, 628.0f, 52.0f},   // Close
                {36.0f, 196.0f, 308.0f, 268.0f},  // Homebrew Channel
                {332.0f, 196.0f, 604.0f, 268.0f}, // Wii Menu
                {150.0f, 262.0f, 310.0f, 310.0f}, // Yes
                {330.0f, 262.0f, 490.0f, 310.0f}, // No
            };
            return kBoxes[button];
        }

        bool HomeButtonActive(int button)
        {
            if (g_Home.State == HOME_CONFIRM)
                return button == HOME_YES || button == HOME_NO;
            return button == HOME_CLOSE || button == HOME_LOADER || button == HOME_WII_MENU;
        }

        int HomeButtonAt(float x, float y)
        {
            for (int button = 0; button < HOME_BUTTON_COUNT; ++button)
            {
                const Box box = HomeButtonBox(button);
                if (HomeButtonActive(button) && x >= box.X0 && x < box.X1 && y >= box.Y0 && y < box.Y1)
                    return button;
            }
            return -1;
        }

        // D-Pad focus: Close sits above the two exits, Yes and No side by side.
        int HomeMove(int focus, int dx, int dy)
        {
            if (g_Home.State == HOME_CONFIRM)
                return dx < 0 ? HOME_YES : (dx > 0 ? HOME_NO : focus);
            if (dy < 0)
                return HOME_CLOSE;
            if (dy > 0 && focus == HOME_CLOSE)
                return HOME_WII_MENU;
            if (dx < 0 && focus != HOME_CLOSE)
                return HOME_LOADER;
            if (dx > 0 && focus != HOME_CLOSE)
                return HOME_WII_MENU;
            return focus;
        }

        OverlayItem HomeButtonItem(int button, const char *label, float scale)
        {
            const Box box = HomeButtonBox(button);
            const bool focused = g_Home.Focus == button;
            return MakeOverlayItem(box.X0, box.Y0, box.X1, box.Y1, focused ? kFocusFill : kButtonFill,
                                   focused ? kFocusBorder : kButtonBorder, label, kButtonText, scale);
        }

        void PublishHomeMenu()
        {
            OverlayItem items[48];
            int count = 0;
            items[count++] = MakeOverlayItem(0, 0, 640, 480, kDim, 0);
            items[count++] = MakeOverlayItem(0, 0, 640, 64, kBar, 0, "HOME Menu", kBarText, 2.0f);
            items[count++] = HomeButtonItem(HOME_CLOSE, "Close", 1.0f);
            items[count++] = HomeButtonItem(HOME_LOADER, "Homebrew Channel", 2.0f);
            items[count++] = HomeButtonItem(HOME_WII_MENU, "Wii Menu", 2.0f);

            // Wii Remote batteries, four bars each.
            items[count++] = MakeOverlayItem(0, 400, 640, 480, kBar, 0);
            for (int chan = 0; chan < 4; ++chan)
            {
                u32 type;
                const bool connected = WPAD_Probe(chan, &type) == WPAD_ERR_NONE;
                const float x = 64.0f + chan * 144.0f;
                char label[4] = {'P', (char)('1' + chan), '\0', '\0'};
                items[count++] = MakeOverlayItem(x, 424, x + 32, 456, 0, 0, label, connected ? kBarText : kAbsent);
                const int level = connected ? WPAD_BatteryLevel(chan) : 0;
                int bars = (level + 51) / 52;
                if (bars > 4)
                    bars = 4;
                for (int i = 0; i < 4; ++i)
                {
                    const float cx = x + 40.0f + i * 18.0f;
                    items[count++] = MakeOverlayItem(cx, 430, cx + 14, 450,
                                                     !connected ? 0 : (i < bars ? kBatteryFull : kBatteryEmpty),
                                                     connected ? 0 : kAbsent);
                }
            }

            if (g_Home.State == HOME_CONFIRM)
            {
                items[count++] = MakeOverlayItem(110, 150, 530, 330, kDialogFill, kButtonBorder);
                items[count++] = MakeOverlayItem(110, 168, 530, 200, 0, 0,
                                                 g_Home.Pending == QUIT_TO_MENU ? "Return to the Wii Menu?"
                                                                                : "Exit to the Homebrew Channel?",
                                                 kButtonText, 1.0f);
                items[count++] = MakeOverlayItem(110, 204, 530, 236, 0, 0, "Unsaved progress will be lost.",
                                                 kButtonText, 1.0f);
                items[count++] = HomeButtonItem(HOME_YES, "Yes", 2.0f);
                items[count++] = HomeButtonItem(HOME_NO, "No", 2.0f);
            }
            SetOverlay(OVERLAY_HOME_MENU, items, count);
        }
    }

    void OpenHomeMenu()
    {
        g_Home.State = HOME_MAIN;
        g_Home.Focus = HOME_CLOSE;
        g_Home.Pending = QUIT_NONE;
        g_Home.LeavingFrames = 0;
        PublishHomeMenu();
    }

    bool UpdateHomeMenu()
    {
        if (g_Home.State == HOME_CLOSED)
            return false;
        if (g_QuitRequest != QUIT_NONE)
        {
            g_Home.State = HOME_CLOSED;
            SetOverlay(OVERLAY_HOME_MENU, NULL, 0);
            return false;
        }

        ScanControllers();
        u32 down = 0;     // Wii Remote and Classic Controller buttons
        u32 held = 0;
        u32 padDown = 0;  // GameCube buttons
        u32 padHeld = 0;
        bool pointing = false;
        float px = 0.0f, py = 0.0f, angle = 0.0f;
        for (int chan = 0; chan < 4; ++chan)
        {
            down |= WPAD_ButtonsDown(chan);
            held |= WPAD_ButtonsHeld(chan);
            padDown |= PAD_ButtonsDown(chan);
            padHeld |= PAD_ButtonsHeld(chan);
            WPADData *data = WPAD_Data(chan);
            if (!pointing && data && data->ir.valid)
            {
                pointing = true;
                px = data->ir.x;
                py = data->ir.y;
                angle = data->ir.angle;
            }
        }

        if (g_Home.State == HOME_LEAVING)
        {
            // Let go of the button that closed the menu before the game sees the controllers again.
            if ((!held && !padHeld) || ++g_Home.LeavingFrames > 60)
            {
                g_Home.State = HOME_CLOSED;
                return false;
            }
            return true;
        }

        const int hovered = pointing ? HomeButtonAt(px, py) : -1;
        if (hovered >= 0)
            g_Home.Focus = hovered;
        SetPointer(pointing, px, py, angle);

        const int dx = ((down & (WPAD_BUTTON_RIGHT | WPAD_CLASSIC_BUTTON_RIGHT)) || (padDown & PAD_BUTTON_RIGHT)) ? 1
                     : ((down & (WPAD_BUTTON_LEFT | WPAD_CLASSIC_BUTTON_LEFT)) || (padDown & PAD_BUTTON_LEFT)) ? -1
                                                                                                             : 0;
        const int dy = ((down & (WPAD_BUTTON_DOWN | WPAD_CLASSIC_BUTTON_DOWN)) || (padDown & PAD_BUTTON_DOWN)) ? 1
                     : ((down & (WPAD_BUTTON_UP | WPAD_CLASSIC_BUTTON_UP)) || (padDown & PAD_BUTTON_UP)) ? -1
                                                                                                       : 0;
        if (dx || dy)
            g_Home.Focus = HomeMove(g_Home.Focus, dx, dy);

        const bool select = (down & (WPAD_BUTTON_A | WPAD_CLASSIC_BUTTON_A)) || (padDown & PAD_BUTTON_A);
        const bool back = (down & (WPAD_BUTTON_HOME | WPAD_BUTTON_B | WPAD_CLASSIC_BUTTON_HOME | WPAD_CLASSIC_BUTTON_B)) ||
                          (padDown & PAD_BUTTON_B) || ((padDown & PAD_BUTTON_START) && (padHeld & PAD_TRIGGER_Z));

        int chosen = -1;
        if (select && (!pointing || hovered >= 0))
            chosen = pointing ? hovered : g_Home.Focus;
        if (back)
            chosen = g_Home.State == HOME_CONFIRM ? HOME_NO : HOME_CLOSE;

        switch (chosen)
        {
        case HOME_CLOSE:
            g_Home.State = HOME_LEAVING;
            SetOverlay(OVERLAY_HOME_MENU, NULL, 0);
            SetPointer(false, 0.0f, 0.0f, 0.0f);
            return true;
        case HOME_LOADER:
        case HOME_WII_MENU:
            g_Home.State = HOME_CONFIRM;
            g_Home.Pending = chosen == HOME_LOADER ? QUIT_TO_LOADER : QUIT_TO_MENU;
            g_Home.Focus = HOME_NO;
            break;
        case HOME_YES:
            RequestQuit(g_Home.Pending);
            g_Home.State = HOME_CLOSED;
            SetOverlay(OVERLAY_HOME_MENU, NULL, 0);
            return false;
        case HOME_NO:
            g_Home.State = HOME_MAIN;
            g_Home.Focus = g_Home.Pending == QUIT_TO_MENU ? HOME_WII_MENU : HOME_LOADER;
            break;
        default:
            break;
        }
        PublishHomeMenu();
        return true;
    }

    void AddFrameWaits(u64 gpuTicks, u64 vsyncTicks)
    {
        g_GpuWait += gpuTicks;
        g_VsyncWait += vsyncTicks;
    }

    void TakeFrameWaits(u64 *gpuTicks, u64 *vsyncTicks)
    {
        if (gpuTicks)
            *gpuTicks = g_GpuWait;
        if (vsyncTicks)
            *vsyncTicks = g_VsyncWait;
        g_GpuWait = g_VsyncWait = 0;
    }

    void GetMemoryStatus(u32 *used, u32 *available)
    {
        const struct mallinfo info = mallinfo();
        const u32 arena2 = (u32)SYS_GetArena2Hi() - (u32)SYS_GetArena2Lo();
        const bool inMem2 = g_Arena2Start && SYS_GetArena2Lo() != g_Arena2Start;
        const u32 arena1 = inMem2 ? 0 : (u32)SYS_GetArena1Hi() - (u32)SYS_GetArena1Lo();
        // When the heap moves on to MEM2, malloc counts the addresses it
        // jumped over, from the end of the heap in MEM1, as memory in use.
        u32 inUse = (u32)info.uordblks;
        const u32 gap = inMem2 ? (u32)g_Arena2Start - (u32)SYS_GetArena1Lo() : 0;
        if (inUse > gap)
            inUse -= gap;
        if (used)
            *used = inUse;
        if (available)
            *available = (u32)info.fordblks + arena1 + arena2;
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

    void SetOverlay(OverlayLayer layer, const OverlayItem *items, int count)
    {
        if (layer < 0 || layer >= OVERLAY_LAYER_COUNT)
            return;
        if (count > kMaxOverlayItems)
            count = kMaxOverlayItems;
        if (count > 0 && items)
            memcpy(g_Overlay[layer], items, count * sizeof(OverlayItem));
        g_OverlayCount[layer] = items ? count : 0;
    }

    int GetOverlay(OverlayLayer layer, const OverlayItem **items)
    {
        if (layer < 0 || layer >= OVERLAY_LAYER_COUNT)
            return 0;
        if (items)
            *items = g_Overlay[layer];
        return g_OverlayCount[layer];
    }

    OverlayItem MakeOverlayItem(float x0, float y0, float x1, float y1, u32 fill, u32 border,
                                const char *text, u32 textColor, float textScale)
    {
        OverlayItem item;
        item.X0 = x0;
        item.Y0 = y0;
        item.X1 = x1;
        item.Y1 = y1;
        item.Fill = fill;
        item.Border = border;
        item.TextColor = textColor;
        item.TextScale = textScale;
        item.Text[0] = '\0';
        if (text)
        {
            strncpy(item.Text, text, sizeof(item.Text) - 1);
            item.Text[sizeof(item.Text) - 1] = '\0';
        }
        return item;
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
