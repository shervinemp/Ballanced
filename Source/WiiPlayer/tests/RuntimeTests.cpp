// The console runtime linked into each executable: memcpy and memset
// (FastMemory.c), the heap lock (HeapLock.c) and VxMath's directory listings
// (VxWindowFunctionsOgc.cpp).

#include "TestFramework.h"

#include <malloc.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <ogc/lwp_watchdog.h>
#include <ogc/system.h>

#include "VxWindowFunctions.h"

namespace
{
    // Byte by byte, out of the compiler's reach.
    void ReferenceCopy(volatile unsigned char *dst, const volatile unsigned char *src, size_t n)
    {
        for (size_t i = 0; i < n; ++i)
            dst[i] = src[i];
    }

    void ReferenceFill(volatile unsigned char *dst, unsigned char value, size_t n)
    {
        for (size_t i = 0; i < n; ++i)
            dst[i] = value;
    }

    bool ReferenceEqual(const volatile unsigned char *a, const volatile unsigned char *b, size_t n)
    {
        for (size_t i = 0; i < n; ++i)
        {
            if (a[i] != b[i])
                return false;
        }
        return true;
    }

    void FillPattern(unsigned char *p, size_t n, unsigned int seed)
    {
        for (size_t i = 0; i < n; ++i)
        {
            seed = seed * 1103515245u + 12345u;
            p[i] = (unsigned char)(seed >> 16);
        }
    }

    // memcpy and memset against byte loops, every size up to 300 bytes at
    // every alignment of source and destination within a cache line, inside
    // guard bytes. Into uncached memory they only have to finish: dcbz there
    // raises an alignment exception, and Dolphin does not read back byte
    // stores through the uncached view reliably, even from a byte loop.
    void TestCopies(bool uncached)
    {
        const size_t kArea = 1024;
        unsigned char *bufferA = (unsigned char *)memalign(32, kArea);
        unsigned char *bufferB = (unsigned char *)memalign(32, kArea);
        unsigned char *expected = (unsigned char *)memalign(32, kArea);
        if (!bufferA || !bufferB || !expected)
        {
            WT_CHECK(false, "test buffers");
            free(bufferA);
            free(bufferB);
            free(expected);
            return;
        }
        unsigned char *source = bufferA;
        unsigned char *target = bufferB;
        if (uncached)
        {
            DCFlushRange(bufferB, kArea);
            target = (unsigned char *)MEM_K0_TO_K1(bufferB);
            for (int size = 0; size <= 300; size += 7)
            {
                for (int offset = 0; offset < 32; offset += 3)
                {
                    memcpy(target + 64 + offset, source + offset, size);
                    memset(target + 64 + offset, size, size);
                }
            }
            memset(target, 0, kArea);
            memcpy(target, source, kArea);
            WT_CHECK(true, "memcpy and memset into uncached memory");
            DCInvalidateRange(bufferB, kArea);
            free(bufferA);
            free(bufferB);
            free(expected);
            return;
        }

        int copyFailures = 0, fillFailures = 0;
        char firstFailure[96] = "";
        for (size_t size = 0; size <= 300; ++size)
        {
            for (int sourceOffset = 0; sourceOffset < 32; sourceOffset += (size > 64 ? 3 : 1))
            {
                for (int targetOffset = 0; targetOffset < 32; targetOffset += (size > 64 ? 5 : 1))
                {
                    // The expected bytes come from byte loops alone.
                    FillPattern(source, kArea, (unsigned)(size * 977 + sourceOffset));
                    FillPattern(target, kArea, (unsigned)(size * 131 + targetOffset));
                    ReferenceCopy(expected, target, kArea);
                    ReferenceCopy(expected + 64 + targetOffset, source + 64 + sourceOffset, size);
                    memcpy(target + 64 + targetOffset, source + 64 + sourceOffset, size);
                    if (!ReferenceEqual(target, expected, kArea) && copyFailures++ == 0 && fillFailures == 0)
                        snprintf(firstFailure, sizeof(firstFailure), "memcpy %u bytes, offsets %d -> %d",
                                 (unsigned)size, sourceOffset, targetOffset);

                    const unsigned char value = (unsigned char)(size * 7 + targetOffset);
                    ReferenceCopy(expected, target, kArea);
                    ReferenceFill(expected + 64 + targetOffset, value, size);
                    memset(target + 64 + targetOffset, value, size);
                    if (!ReferenceEqual(target, expected, kArea) && fillFailures++ == 0 && copyFailures == 0)
                        snprintf(firstFailure, sizeof(firstFailure), "memset %u bytes, offset %d, value %u",
                                 (unsigned)size, targetOffset, value);
                }
            }
        }
        WT_CHECK(copyFailures == 0 && fillFailures == 0, "memcpy and memset match byte loops (%d, %d failed; %s)",
                 copyFailures, fillFailures, firstFailure);

        // Large blocks, and zero fills, which take the dcbz path.
        FillPattern(source, kArea, 99);
        ReferenceFill(target, 0x5A, kArea);
        memcpy(target + 1, source + 7, kArea - 64);
        WT_CHECK(ReferenceEqual(target + 1, source + 7, kArea - 64) && target[0] == 0x5A && target[kArea - 63] == 0x5A,
                 "large misaligned copy");
        ReferenceFill(target, 0x5A, kArea);
        memset(target + 3, 0, kArea - 70);
        bool zero = target[2] == 0x5A && target[kArea - 67] == 0x5A;
        for (size_t i = 3; i < kArea - 67; ++i)
            zero = zero && target[i] == 0;
        WT_CHECK(zero, "large zero fill");

        free(bufferA);
        free(bufferB);
        free(expected);
    }

    // Copies whose areas overlap: forward by a word, as older code expects of
    // memcpy, without dcbz clearing bytes not yet read.
    void TestOverlap()
    {
        unsigned char *buffer = (unsigned char *)memalign(32, 512);
        unsigned char *expected = (unsigned char *)memalign(32, 512);
        FillPattern(buffer, 512, 5);
        memcpy(expected, buffer, 512);
        ReferenceCopy(expected, expected + 4, 400);
        memcpy(buffer, buffer + 4, 400);
        WT_CHECK(memcmp(buffer, expected, 512) == 0, "overlapping copy towards lower addresses");
        free(buffer);
        free(expected);
    }

    unsigned int Msr()
    {
        unsigned int msr;
        __asm__ __volatile__("mfmsr %0" : "=r"(msr));
        return msr;
    }

    void TestHeapLock()
    {
        // Interrupts come back on, and the FPU stays on, after the allocator.
        const unsigned int before = Msr();
        void *blocks[64];
        for (int i = 0; i < 64; ++i)
            blocks[i] = malloc(16 + i * 24);
        for (int i = 0; i < 64; i += 2)
            free(blocks[i]);
        void *grown = realloc(blocks[1], 4096);
        const unsigned int after = Msr();
        WT_CHECK((after & 0x8000) == (before & 0x8000), "malloc and free leave interrupts as they were (MSR %08x -> %08x)",
                 before, after);
        WT_CHECK((after & 0x2000) == (before & 0x2000), "malloc and free leave the FPU on");
        WT_CHECK(grown != NULL, "realloc");
        free(grown);
        for (int i = 3; i < 64; i += 2)
            free(blocks[i]);

        // How long allocations take, for the log.
        static void *timed[4000];
        const u64 start = gettime();
        for (int i = 0; i < 4000; ++i)
            timed[i] = malloc(16 + (i & 7) * 24);
        const u64 allocated = gettime();
        for (int i = 0; i < 4000; ++i)
            free(timed[i]);
        const u64 freed = gettime();
        wiitest::Log("  malloc: %u ns, free: %u ns", (unsigned)(ticks_to_nanosecs(allocated - start) / 4000),
                     (unsigned)(ticks_to_nanosecs(freed - allocated) / 4000));
    }

    // Small blocks (SmallBlocks.c) next to newlib's large ones.
    void TestSmallBlocks()
    {
        bool ok = true;
        void *blocks[300];
        for (int i = 0; i < 300; ++i)
        {
            const size_t size = 1 + i;
            blocks[i] = malloc(size);
            ok = ok && blocks[i] && ((uintptr_t)blocks[i] & 7) == 0 && malloc_usable_size(blocks[i]) >= size;
            if (blocks[i])
                memset(blocks[i], i & 0xFF, size);
        }
        for (int i = 0; i < 300 && ok; ++i)
        {
            const unsigned char *p = (const unsigned char *)blocks[i];
            for (int j = 0; j <= i; ++j)
                ok = ok && p[j] == (unsigned char)(i & 0xFF);
        }
        WT_CHECK(ok, "blocks of 1 to 300 bytes are aligned, large enough and separate");
        for (int i = 0; i < 300; i += 2)
            free(blocks[i]);
        for (int i = 0; i < 300; i += 2)
            blocks[i] = malloc(1 + i);
        for (int i = 0; i < 300; ++i)
            free(blocks[i]);

        // realloc within, into, out of and between the small blocks keeps the contents.
        unsigned char *p = (unsigned char *)malloc(10);
        for (int i = 0; i < 10; ++i)
            p[i] = (unsigned char)(i + 1);
        p = (unsigned char *)realloc(p, 6);
        bool kept = p != NULL;
        for (int i = 0; kept && i < 6; ++i)
            kept = p[i] == i + 1;
        p = (unsigned char *)realloc(p, 200);
        for (int i = 0; kept && i < 6; ++i)
            kept = p[i] == i + 1;
        memset(p + 6, 0x77, 194);
        p = (unsigned char *)realloc(p, 5000);
        for (int i = 0; kept && i < 6; ++i)
            kept = p[i] == i + 1;
        for (int i = 6; kept && i < 200; ++i)
            kept = p[i] == 0x77;
        p = (unsigned char *)realloc(p, 40);
        for (int i = 0; kept && i < 6; ++i)
            kept = p[i] == i + 1;
        for (int i = 6; kept && i < 40; ++i)
            kept = p[i] == 0x77;
        WT_CHECK(kept, "realloc keeps the contents between block sizes");
        WT_CHECK(realloc(p, 0) == NULL, "realloc to 0 frees");

        unsigned char *zeroed = (unsigned char *)calloc(13, 9);
        bool zero = zeroed != NULL;
        for (int i = 0; zero && i < 117; ++i)
            zero = zeroed[i] == 0;
        WT_CHECK(zero, "calloc clears small blocks");
        free(zeroed);
        volatile size_t huge = 0x10000; // Out of the compiler's sight, which warns about it
        WT_CHECK(calloc(huge, huge) == NULL, "calloc refuses an overflowing size");

        bool aligned = true;
        void *alignedBlocks[64];
        for (int i = 0; i < 64; ++i)
        {
            const size_t alignment = (size_t)8 << (i % 4); // 8, 16, 32, 64
            alignedBlocks[i] = memalign(alignment, 1 + i * 5);
            aligned = aligned && alignedBlocks[i] && ((uintptr_t)alignedBlocks[i] & (alignment - 1)) == 0;
            if (alignedBlocks[i])
                memset(alignedBlocks[i], 0xEE, 1 + i * 5);
        }
        for (int i = 0; i < 64; ++i)
            free(alignedBlocks[i]);
        WT_CHECK(aligned, "memalign of small blocks");

        void *large = malloc(100000);
        void *small = malloc(24);
        WT_CHECK(large && small && malloc_usable_size(large) >= 100000, "large blocks still come from newlib");
        free(small);
        free(large);
    }

    struct ListResult
    {
        int Files;
        bool SawFile;
        bool SawFolder;
    };

    XBOOL CountEntry(const VxDirectoryEntry *entry, void *user)
    {
        ListResult *result = (ListResult *)user;
        if (!entry->IsDirectory)
            ++result->Files;
        if (!entry->IsDirectory && strcmp(entry->Name.CStr(), "Cached.txt") == 0)
            result->SawFile = true;
        if (entry->IsDirectory && strcmp(entry->Name.CStr(), "Sub") == 0)
            result->SawFolder = true;
        return TRUE;
    }

    // Existence checks and listings come from cached listings, which must see
    // files the program creates, deletes and renames.
    void TestDirectoryCache()
    {
        const char *folder = "sd:/wiitests-dircache";
        if (!VxDirectoryExists("sd:/"))
        {
            wiitest::Log("  No SD card: directory cache checks skipped");
            return;
        }
        unlink("sd:/wiitests-dircache/Cached.txt");
        unlink("sd:/wiitests-dircache/Renamed.txt");
        rmdir("sd:/wiitests-dircache/Sub");
        rmdir(folder);

        WT_CHECK(!VxDirectoryExists(folder), "folder absent before the test");
        WT_CHECK(!VxFileExists("sd:/wiitests-dircache/Cached.txt"), "file absent before the folder exists");
        WT_CHECK(VxMakeDirectory(folder), "make folder");
        WT_CHECK(VxDirectoryExists(folder) && VxDirectoryExists("sd:/wiitests-dircache/"), "new folder found");
        WT_CHECK(!VxFileExists(folder), "a folder is not a file");

        WT_CHECK(!VxFileExists("sd:/wiitests-dircache/Cached.txt"), "file absent before it is written");
        FILE *file = fopen("sd:/wiitests-dircache/Cached.txt", "wb");
        WT_CHECK(file != NULL, "create file");
        if (file)
        {
            fputs("cached", file);
            fclose(file);
        }
        WT_CHECK(VxFileExists("sd:/wiitests-dircache/Cached.txt"), "new file found");
        WT_CHECK(VxFileExists("sd:/wiitests-dircache\\Cached.txt"), "found with a backslash");
        WT_CHECK(VxFileExists("sd:/wiitests-dircache/CACHED.TXT") == VxFileExists("sd:/wiitests-dircache/cached.txt"),
                 "other case answered by the file system");
        WT_CHECK(!VxDirectoryExists("sd:/wiitests-dircache/Cached.txt"), "a file is not a folder");
        WT_CHECK(!VxFileExists("sd:/wiitests-dircache/Missing.txt"), "missing file");

        WT_CHECK(VxMakeDirectory("sd:/wiitests-dircache/Sub"), "make subfolder");
        ListResult listed = {0, false, false};
        WT_CHECK(VxListDirectory(folder, "*", TRUE, CountEntry, &listed) && listed.Files == 1 && listed.SawFile &&
                     listed.SawFolder,
                 "listing shows the new file and folder (%d files)", listed.Files);
        ListResult masked = {0, false, false};
        VxListDirectory(folder, "*.TXT", FALSE, CountEntry, &masked);
        WT_CHECK(masked.Files == 1 && !masked.SawFolder, "listing with a mask and no folders");

        WT_CHECK(rename("sd:/wiitests-dircache/Cached.txt", "sd:/wiitests-dircache/Renamed.txt") == 0, "rename");
        WT_CHECK(!VxFileExists("sd:/wiitests-dircache/Cached.txt") &&
                     VxFileExists("sd:/wiitests-dircache/Renamed.txt"),
                 "renamed file found under its new name only");
        WT_CHECK(VxDeleteFile("sd:/wiitests-dircache/Renamed.txt"), "delete");
        WT_CHECK(!VxFileExists("sd:/wiitests-dircache/Renamed.txt"), "deleted file gone");
        rmdir("sd:/wiitests-dircache/Sub");
        WT_CHECK(!VxDirectoryExists("sd:/wiitests-dircache/Sub"), "removed folder gone");
        rmdir(folder);
        WT_CHECK(!VxDirectoryExists(folder), "test folder removed");
    }
}

void RunRuntimeTests()
{
    wiitest::BeginSuite("Runtime");

    TestCopies(false);
    TestCopies(true);
    TestOverlap();

    // Speed, for the log: 64-byte matrices and a 64 KB block.
    static unsigned char from[65536] __attribute__((aligned(32)));
    static unsigned char to[65536] __attribute__((aligned(32)));
    u64 start = gettime();
    for (int i = 0; i < 20000; ++i)
        memcpy(to + (i & 255) * 64, from + ((i * 7) & 255) * 64, 64);
    const u64 small = gettime() - start;
    start = gettime();
    for (int i = 0; i < 50; ++i)
        memcpy(to + (i & 1), from + 3, sizeof(from) - 4);
    const u64 large = gettime() - start;
    wiitest::Log("  memcpy: 64 bytes in %u ns, 64 KB misaligned in %u us",
                 (unsigned)(ticks_to_nanosecs(small) / 20000), (unsigned)(ticks_to_microsecs(large) / 50));

    TestHeapLock();
    TestSmallBlocks();
    TestDirectoryCache();

    wiitest::EndSuite();
}
