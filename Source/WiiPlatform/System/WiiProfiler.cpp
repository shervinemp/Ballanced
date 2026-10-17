#include "WiiProfiler.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <tuxedo/thread.h>
#include <tuxedo/tick.h>

namespace
{
    const u32 kHeaderWords = 4; // thread, phase, pc, lr
    const u32 kSampleWords = kHeaderWords + wiiprofiler::kDepth;

    KTickTask s_Task;
    u32 *volatile s_Samples = NULL;
    volatile u32 s_Count = 0;
    u32 s_Capacity = 0;
    u32 s_IntervalUs = 0;
    volatile bool s_Running = false;
    volatile u32 s_Phase = wiiprofiler::PHASE_OTHER;
    volatile u32 s_Frame = 0;

    // Cached MEM1 or MEM2, where stacks live.
    inline bool IsStackAddress(u32 address)
    {
        return (address >= 0x80000000u && address < 0x81800000u) || (address >= 0x90000000u && address < 0x94000000u);
    }

    inline bool IsCodeAddress(u32 address)
    {
        return address >= 0x80000000u && address < 0x81800000u && (address & 3) == 0;
    }

    // Runs in the decrementer interrupt, which saved the interrupted thread's
    // registers in its context. Integer code only: the FPU belongs to the thread.
    void Sample(KTickTask *task)
    {
        (void)task;
        const u32 index = s_Count;
        if (index >= s_Capacity)
            return;
        KThread *thread = KThreadGetSelf();
        u32 *sample = s_Samples + index * kSampleWords;
        sample[0] = (u32)thread;
        sample[1] = s_Frame << 8 | s_Phase;
        sample[2] = thread ? thread->ctx.pc : 0;
        sample[3] = thread ? thread->ctx.lr : 0;
        u32 depth = 0;
        if (thread)
        {
            // EABI frames: word 0 links to the caller's frame, word 1 of that
            // frame holds the return address saved by the callee.
            u32 frame = thread->ctx.gpr[1];
            while (depth < wiiprofiler::kDepth && IsStackAddress(frame) && (frame & 7) == 0)
            {
                const u32 caller = *(const u32 *)frame;
                if (caller <= frame || !IsStackAddress(caller) || (caller & 7) != 0)
                    break;
                const u32 ret = *(const u32 *)(caller + 4);
                if (!IsCodeAddress(ret))
                    break;
                sample[kHeaderWords + depth++] = ret;
                frame = caller;
            }
        }
        while (depth < wiiprofiler::kDepth)
            sample[kHeaderWords + depth++] = 0;
        s_Count = index + 1;
    }
}

bool wiiprofiler::Start(u32 intervalUs, u32 capacity)
{
    if (s_Running || intervalUs == 0 || capacity == 0)
        return false;
    free(s_Samples);
    s_Samples = (u32 *)malloc((size_t)capacity * kSampleWords * sizeof(u32));
    if (!s_Samples)
        return false;
    s_Capacity = capacity;
    s_IntervalUs = intervalUs;
    s_Count = 0;
    s_Running = true;
    const u64 ticks = PPCUsToTicks(intervalUs);
    KTickTaskStart(&s_Task, Sample, ticks, ticks);
    return true;
}

void wiiprofiler::Stop()
{
    if (!s_Running)
        return;
    KTickTaskStop(&s_Task);
    s_Running = false;
}

bool wiiprofiler::IsRunning()
{
    return s_Running;
}

u32 wiiprofiler::GetSampleCount()
{
    return s_Count;
}

bool wiiprofiler::Write(const char *path)
{
    Stop();
    if (!s_Samples)
        return false;
    bool ok = false;
    FILE *file = fopen(path, "wb");
    if (file)
    {
        const u32 header[5] = {0x57505246u /* WPRF */, 2, s_Count, kSampleWords, s_IntervalUs};
        ok = fwrite(header, sizeof(header), 1, file) == 1 &&
             (s_Count == 0 || fwrite(s_Samples, s_Count * kSampleWords * sizeof(u32), 1, file) == 1);
        ok = fclose(file) == 0 && ok;
    }
    free(s_Samples);
    s_Samples = NULL;
    s_Count = s_Capacity = 0;
    return ok;
}

void wiiprofiler::SetPhase(Phase phase)
{
    s_Phase = phase;
}

wiiprofiler::Phase wiiprofiler::GetPhase()
{
    return (Phase)s_Phase;
}

void wiiprofiler::NextFrame()
{
    s_Frame = (s_Frame + 1) & 0xFFFFFF;
}
