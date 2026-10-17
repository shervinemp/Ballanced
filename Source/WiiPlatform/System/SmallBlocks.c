// Small blocks for malloc and free.
//
// newlib's allocator searches and splits its free lists on each malloc and
// merges blocks on each free, and loading a level makes hundreds of thousands
// of small allocations (physics shapes, engine objects, strings). Blocks of up to 256 bytes now come from
// 64 KB chunks of one size each: a free list per size, or the next unused
// block of the size's current chunk. A table of who owns each 64 KB of MEM1
// and MEM2 tells free() which blocks are small. Chunks are cut from 512 KB
// arenas taken from newlib (whose 64 KB-aligned allocations are slow) and are
// never given back, so the memory a size has needed stays with it.
//
// The linker sends newlib's allocation functions here (--wrap, see
// Source/WiiPlayer/CMakeLists.txt). Every entry point that could otherwise pass
// a small block to newlib code reading its block headers (calloc, realloc,
// memalign, malloc_usable_size) is wrapped too, and newlib's own memalign and
// realloc only ever ask malloc for large blocks.
//
// Link this file into each executable, like MainStack.c.

#include <errno.h>
#include <reent.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

void *__real__malloc_r(struct _reent *r, size_t size);
void __real__free_r(struct _reent *r, void *block);
void *__real__realloc_r(struct _reent *r, void *block, size_t size);
void *__real__memalign_r(struct _reent *r, size_t alignment, size_t size);
size_t __real__malloc_usable_size_r(struct _reent *r, void *block);

#define MSR_EE 0x8000u
#define CHUNK_SHIFT 16
#define CHUNK_SIZE (1u << CHUNK_SHIFT)
#define ARENA_SIZE (8u * CHUNK_SIZE)
#define MAX_SMALL 256
#define MAX_ALIGNMENT 32

static const uint16_t kClassSizes[] = {8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256};
#define CLASS_COUNT (sizeof(kClassSizes) / sizeof(kClassSizes[0]))

// Size class + 1 of each 64 KB of MEM1 and MEM2, 0 where newlib owns it.
static uint8_t s_Mem1Owners[0x01800000u >> CHUNK_SHIFT];
static uint8_t s_Mem2Owners[0x04000000u >> CHUNK_SHIFT];

static unsigned char *s_ArenaNext;
static unsigned char *s_ArenaEnd;
static void *s_FreeLists[CLASS_COUNT];
static unsigned char *s_Next[CLASS_COUNT];
static unsigned char *s_End[CLASS_COUNT];

static inline uint8_t *Owner(const void *block)
{
    const uintptr_t address = (uintptr_t)block;
    if (address - 0x80000000u < 0x01800000u)
        return &s_Mem1Owners[(address - 0x80000000u) >> CHUNK_SHIFT];
    if (address - 0x90000000u < 0x04000000u)
        return &s_Mem2Owners[(address - 0x90000000u) >> CHUNK_SHIFT];
    return NULL;
}

static inline unsigned int SizeClass(size_t size)
{
    if (size <= 64)
        return (unsigned int)((size + 7) >> 3) - 1;    // 8..64 by 8
    if (size <= 128)
        return 8 + (unsigned int)((size - 65) >> 4);   // 80..128 by 16
    return 12 + (unsigned int)((size - 129) >> 5);     // 160..256 by 32
}

// Interrupts off while the lists change: the console has one core, and
// nothing here waits (as in HeapLock.c).
static inline unsigned int Lock(void)
{
    unsigned int msr;
    __asm__ __volatile__("mfmsr %0" : "=r"(msr));
    __asm__ __volatile__("mtmsr %0" : : "r"(msr & ~MSR_EE) : "memory");
    return msr & MSR_EE;
}

static inline void Unlock(unsigned int enabled)
{
    unsigned int msr;
    __asm__ __volatile__("mfmsr %0" : "=r"(msr));
    __asm__ __volatile__("mtmsr %0" : : "r"(msr | enabled) : "memory");
}

static void *AllocateSmall(struct _reent *r, unsigned int sizeClass)
{
    const unsigned int enabled = Lock();
    void *block = s_FreeLists[sizeClass];
    if (block)
    {
        s_FreeLists[sizeClass] = *(void **)block;
        Unlock(enabled);
        return block;
    }
    const unsigned int size = kClassSizes[sizeClass];
    if (s_Next[sizeClass] + size > s_End[sizeClass])
    {
        if (s_ArenaNext == s_ArenaEnd)
        {
            unsigned char *arena = (unsigned char *)__real__memalign_r(r, CHUNK_SIZE, ARENA_SIZE);
            if (!arena || !Owner(arena) || !Owner(arena + ARENA_SIZE - 1))
            {
                if (arena)
                    __real__free_r(r, arena);
                Unlock(enabled);
                return NULL;
            }
            s_ArenaNext = arena;
            s_ArenaEnd = arena + ARENA_SIZE;
        }
        unsigned char *chunk = s_ArenaNext;
        s_ArenaNext += CHUNK_SIZE;
        *Owner(chunk) = (uint8_t)(sizeClass + 1);
        s_Next[sizeClass] = chunk;
        s_End[sizeClass] = chunk + CHUNK_SIZE;
    }
    block = s_Next[sizeClass];
    s_Next[sizeClass] += size;
    Unlock(enabled);
    return block;
}

void *__wrap__malloc_r(struct _reent *r, size_t size)
{
    if (size - 1 < MAX_SMALL)
    {
        void *block = AllocateSmall(r, SizeClass(size));
        if (block)
            return block;
    }
    return __real__malloc_r(r, size);
}

void __wrap__free_r(struct _reent *r, void *block)
{
    const uint8_t *owner = block ? Owner(block) : NULL;
    if (owner && *owner)
    {
        const unsigned int sizeClass = *owner - 1u;
        const unsigned int enabled = Lock();
        *(void **)block = s_FreeLists[sizeClass];
        s_FreeLists[sizeClass] = block;
        Unlock(enabled);
        return;
    }
    __real__free_r(r, block);
}

size_t __wrap__malloc_usable_size_r(struct _reent *r, void *block)
{
    const uint8_t *owner = block ? Owner(block) : NULL;
    if (owner && *owner)
        return kClassSizes[*owner - 1u];
    return __real__malloc_usable_size_r(r, block);
}

void *__wrap__calloc_r(struct _reent *r, size_t count, size_t size)
{
    if (size != 0 && count > (size_t)-1 / size)
    {
        r->_errno = ENOMEM;
        return NULL;
    }
    const size_t total = count * size;
    void *block = __wrap__malloc_r(r, total);
    if (block)
        memset(block, 0, total);
    return block;
}

void *__wrap__realloc_r(struct _reent *r, void *block, size_t size)
{
    if (!block)
        return __wrap__malloc_r(r, size);
    if (size == 0)
    {
        __wrap__free_r(r, block);
        return NULL;
    }
    const uint8_t *owner = Owner(block);
    const size_t old = (owner && *owner) ? kClassSizes[*owner - 1u] : 0;
    if (size <= old)
        return block;
    if (old != 0 || size <= MAX_SMALL)
    {
        // Into or out of the small blocks: copy.
        const size_t available = old != 0 ? old : __real__malloc_usable_size_r(r, block);
        void *moved = __wrap__malloc_r(r, size);
        if (!moved)
            return NULL;
        memcpy(moved, block, available < size ? available : size);
        __wrap__free_r(r, block);
        return moved;
    }
    // Large to large: newlib's realloc only asks malloc for large blocks.
    return __real__realloc_r(r, block, size);
}

void *__wrap__memalign_r(struct _reent *r, size_t alignment, size_t size)
{
    if (alignment <= MAX_ALIGNMENT && size - 1 < MAX_SMALL)
    {
        // Blocks are aligned to their size class within 64 KB chunks, so a
        // size rounded up to the alignment lands in a class it divides.
        const size_t mask = alignment > 8 ? alignment - 1 : 7;
        const size_t rounded = (size + mask) & ~mask;
        if (rounded <= MAX_SMALL)
        {
            const unsigned int sizeClass = SizeClass(rounded);
            if ((kClassSizes[sizeClass] & mask) == 0)
            {
                void *block = AllocateSmall(r, sizeClass);
                if (block)
                    return block;
            }
        }
    }
    // newlib's memalign asks malloc for size + alignment + a header: keep that
    // above the small blocks, whose headers it would read.
    return __real__memalign_r(r, alignment, size > MAX_SMALL ? size : MAX_SMALL + 1);
}
