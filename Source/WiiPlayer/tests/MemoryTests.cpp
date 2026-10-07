// How much heap the game gets: the Wii has 24 MB of MEM1 and 64 MB of MEM2,
// and libogc's heap moves on to MEM2 once MEM1 is used up. Ballance's levels,
// textures and sounds all live in this heap.

#include "TestFramework.h"

#include <stdlib.h>

#include "WiiSystem.h"

namespace
{
    const u32 kBlockSize = 256 * 1024;
    const int kMaxBlocks = 512; // 128 MB, more than the console has
}

void RunMemoryTests()
{
    wiitest::BeginSuite("Memory");

    u32 used = 0;
    u32 reported = 0;
    wiisystem::GetMemoryStatus(&used, &reported);

    // Claim the heap block by block until it runs out.
    static void *blocks[kMaxBlocks];
    int count = 0;
    while (count < kMaxBlocks && (blocks[count] = malloc(kBlockSize)) != NULL)
        ++count;
    // One large block must fit too: whole textures and sounds are allocated at once.
    for (int i = 0; i < count; ++i)
        free(blocks[i]);
    void *large = malloc(16 * 1024 * 1024);
    const bool largeFits = large != NULL;
    free(large);

    const u32 claimed = (u32)count * kBlockSize;
    wiitest::Log("  Heap: %u KB in use, %u KB reported free, %u KB claimed", (unsigned)(used / 1024),
                 (unsigned)(reported / 1024), (unsigned)(claimed / 1024));

    WT_CHECK(claimed >= 48u * 1024 * 1024, "only %u KB of heap could be claimed", (unsigned)(claimed / 1024));
    WT_CHECK(largeFits, "a 16 MB block did not fit");
    // The status the player logs must not promise memory that isn't there.
    WT_CHECK(reported + kBlockSize >= claimed && reported <= claimed + 2 * 1024 * 1024,
             "reported %u KB free, but %u KB could be claimed", (unsigned)(reported / 1024),
             (unsigned)(claimed / 1024));

    wiitest::EndSuite();
}
