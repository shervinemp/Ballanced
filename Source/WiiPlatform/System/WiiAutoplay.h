#ifndef BALLANCE_WII_AUTOPLAY_H
#define BALLANCE_WII_AUTOPLAY_H

#include <gctypes.h>

// State an autoplay script (see WiiPlayer/WiiAutoplay.h) shares with the input
// manager, which links against this library rather than the player.
namespace wiiautoplay
{
    enum
    {
        kKeyCount = 256
    };

    // Keys the script holds down, by DirectInput scan code (nonzero = held);
    // NULL releases them. The input manager presses them like a keyboard.
    void SetKeys(const u8 *keys);
    const u8 *GetKeys();

    // Whether the game is in play (listening for its camera keys), as the
    // input manager sees it each frame.
    void SetGamePlaying(bool playing);
    bool IsGamePlaying();
}

#endif // BALLANCE_WII_AUTOPLAY_H
