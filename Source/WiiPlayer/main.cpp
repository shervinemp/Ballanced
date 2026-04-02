#include <stdio.h>
#include <unistd.h>
#include "GameConfig.h"
#include "GamePlayer.h"

#ifdef WII
#include <gccore.h>
#include <fat.h>
#include <wiiuse/wpad.h>
#include <isfs.h>

void PowerCallback(s32 chan) {
    exit(0);
}
#endif

int main(int argc, char** argv) {
#ifdef WII
    // Initialize the Wii video and hardware subsystems
    VIDEO_Init();
    WPAD_Init();
    ISFS_Initialize();

    // Configure video mode based on hardware settings
    GXRModeObj* rmode = VIDEO_GetPreferredMode(NULL);
    if (CONF_GetAspectRatio() == CONF_ASPECT_16_9) {
        rmode->viWidth = 678;
    }
    if (CONF_GetProgressiveScan() > 0 && VIDEO_HaveComponentCable()) {
        rmode->viTVMode = VI_PROG;
    }
    VIDEO_Configure(rmode);

    // Set callback for graceful exit via console power button
    WPAD_SetPowerButtonCallback(PowerCallback);

    // Ensure the default allocator does not leak indiscriminately into MEM2.
    // Instead of forcing the primary heap entirely into MEM2 via SYS_SetArena1Hi(SYS_GetArena1Lo()),
    // we let MEM1 handle core execution and physics buffers naturally,
    // and manually configure libogc to expose MEM2 via mem2_malloc() / memalign()
    // or through Virtools' CKArena logic for large asset loads.

    // Initialize the FAT filesystem (SD Card / USB)
    // To support asynchronous file loading smoothly and prevent frame drops mid-game,
    // ensure file read operations within the engine are wrapped using libogc LWP threads.
    if (!fatInitDefault()) {
        printf("FAT Init Failed! Please insert an SD Card.\n");
        return -1;
    }

    // Create ISFS save directory if it doesn't exist
    // 00010000 = standard games, 5242414C = 'RBAL' (Custom Title ID for Ballance)
    ISFS_CreateDir("/title/00010000/5242414C", 0, 3, 3, 3);
    ISFS_CreateDir("/title/00010000/5242414C/data", 0, 3, 3, 3);

    // Change the working directory to the standard homebrew path
    // This allows the engine to find the 'Textures' and 'Sounds' folders natively
    chdir("sd:/apps/ballance/");
#endif

    // Run engine
    CGameConfig config;
    CGamePlayer player;

    if (!player.Init(config, NULL))
        return -1;

    if (!player.Load()) {
        player.Shutdown();
        return -1;
    }

    player.Run();

    player.Shutdown();

#ifdef WII
    ISFS_Deinitialize();
#endif
    return 0;
}
