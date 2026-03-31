#include <stdio.h>
#include <unistd.h>
#include "GameConfig.h"
#include "GamePlayer.h"

#ifdef WII
#include <gccore.h>
#include <fat.h>


#endif

int main(int argc, char** argv) {
#ifdef WII
    // Initialize the Wii video and hardware subsystems
    VIDEO_Init();

    // Initialize the FAT filesystem (SD Card / USB)
    if (!fatInitDefault()) {
        printf("FAT Init Failed! Please insert an SD Card.\n");
        return -1;
    }

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

    return 0;
}
