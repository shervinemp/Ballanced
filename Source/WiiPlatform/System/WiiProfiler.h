#ifndef BALLANCE_WII_PROFILER_H
#define BALLANCE_WII_PROFILER_H

#include <gctypes.h>

// Statistical CPU profiler for the console and Dolphin.
//
// A timer interrupt records where the interrupted thread is: its program
// counter, link register and the return addresses on its stack, plus a phase
// the game sets (logic, drawing, ...). Source/WiiPlayer/tools/wii_profile.py
// turns the samples into self and total time per function using boot.elf's
// symbols. Sampling costs a few microseconds an interrupt and touches no
// floating point.
namespace wiiprofiler
{
    enum Phase
    {
        PHASE_OTHER = 0,
        PHASE_LOGIC,   // CKContext::Process
        PHASE_DRAW,    // CKRenderContext::Render up to the GPU wait
        PHASE_GPU,     // Waiting for the GPU to finish the frame
        PHASE_VSYNC,   // Waiting for the TV
        PHASE_LOAD,    // Loading a composition
    };

    // Samples every intervalUs microseconds into a buffer of capacity samples
    // (each 4 * (4 + kDepth) bytes). False when sampling already runs or there
    // is no memory for the buffer.
    bool Start(u32 intervalUs, u32 capacity);
    void Stop();
    bool IsRunning();
    u32 GetSampleCount();

    // Writes the samples taken since Start and frees them. File layout (big
    // endian words): "WPRF", version, sample count, words per sample, interval
    // in microseconds, then per sample: thread, frame number << 8 | phase, pc,
    // lr and kDepth return addresses (0 past the end of the chain).
    bool Write(const char *path);

    void SetPhase(Phase phase);
    Phase GetPhase();

    // Counts frames, so a profile can single out the long ones.
    void NextFrame();

    enum
    {
        kDepth = 28
    };
}

#endif // BALLANCE_WII_PROFILER_H
