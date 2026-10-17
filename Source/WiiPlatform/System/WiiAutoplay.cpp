#include "WiiAutoplay.h"

#include <string.h>

namespace
{
    u8 g_Keys[wiiautoplay::kKeyCount];
    bool g_KeysHeld = false;
    bool g_GamePlaying = false;
}

void wiiautoplay::SetKeys(const u8 *keys)
{
    g_KeysHeld = keys != NULL;
    if (keys)
        memcpy(g_Keys, keys, sizeof(g_Keys));
}

const u8 *wiiautoplay::GetKeys()
{
    return g_KeysHeld ? g_Keys : NULL;
}

void wiiautoplay::SetGamePlaying(bool playing)
{
    g_GamePlaying = playing;
}

bool wiiautoplay::IsGamePlaying()
{
    return g_GamePlaying;
}
