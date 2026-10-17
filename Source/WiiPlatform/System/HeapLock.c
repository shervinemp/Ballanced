// The heap lock for newlib's malloc, free and realloc.
//
// libogc locks the heap with a kernel mutex. Its unlock walks every thread in
// case a waiter should run first, and both halves turn the FPU off, so the
// next floating point instruction after each allocation traps to turn it back
// on. Loading a level allocates hundreds of thousands of physics objects, and
// in Dolphin the longest frame of a level load lost a seventh of its time to
// this lock. Nothing in the allocator waits and the console has one core, so
// masking external interrupts while the heap is in use keeps every other
// thread out just as well. The linker sends newlib's calls here (--wrap, see
// Source/WiiPlayer/CMakeLists.txt).
//
// Link this file into each executable, like MainStack.c.

#include <reent.h>

#define MSR_EE 0x8000u

static unsigned int s_Depth;
static unsigned int s_Enabled; // MSR[EE] when the outermost lock was taken

void __wrap___malloc_lock(struct _reent *r)
{
    (void)r;
    unsigned int msr;
    __asm__ __volatile__("mfmsr %0" : "=r"(msr));
    __asm__ __volatile__("mtmsr %0" : : "r"(msr & ~MSR_EE) : "memory");
    if (s_Depth++ == 0)
        s_Enabled = msr & MSR_EE;
}

void __wrap___malloc_unlock(struct _reent *r)
{
    (void)r;
    if (--s_Depth != 0)
        return;
    unsigned int msr;
    __asm__ __volatile__("mfmsr %0" : "=r"(msr));
    __asm__ __volatile__("mtmsr %0" : : "r"(msr | s_Enabled) : "memory");
}
