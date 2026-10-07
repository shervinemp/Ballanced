// Sounds through the Wii sound manager and ASND: a WAV file on the storage
// device, loaded whole or streamed, plays, pauses, resumes, loops and stops.

#include "TestFramework.h"

#include "CKAll.h"

#include <gccore.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

namespace
{
    const char *kPath = "sd:/wiitest.wav";
    const int kRate = 22050;

    void PutLE16(FILE *fp, int v)
    {
        fputc(v & 0xFF, fp);
        fputc((v >> 8) & 0xFF, fp);
    }

    void PutLE32(FILE *fp, CKDWORD v)
    {
        PutLE16(fp, (int)(v & 0xFFFF));
        PutLE16(fp, (int)(v >> 16));
    }

    // One second of a 440 Hz tone, 16-bit mono.
    bool WriteTone()
    {
        FILE *fp = fopen(kPath, "wb");
        if (!fp)
            return false;
        const int samples = kRate;
        fwrite("RIFF", 1, 4, fp);
        PutLE32(fp, 36 + samples * 2);
        fwrite("WAVEfmt ", 1, 8, fp);
        PutLE32(fp, 16);
        PutLE16(fp, 1);
        PutLE16(fp, 1);
        PutLE32(fp, kRate);
        PutLE32(fp, kRate * 2);
        PutLE16(fp, 2);
        PutLE16(fp, 16);
        fwrite("data", 1, 4, fp);
        PutLE32(fp, samples * 2);
        for (int i = 0; i < samples; ++i)
            PutLE16(fp, (int)(8000.0f * sinf(2.0f * PI * 440.0f * i / kRate)));
        fclose(fp);
        return true;
    }

    void WaitFrames(CKContext *context, int frames)
    {
        CKSoundManager *manager = (CKSoundManager *)context->GetManagerByGuid(SOUND_MANAGER_GUID);
        for (int i = 0; i < frames; ++i)
        {
            VIDEO_WaitVSync();
            // Streams are refilled when the manager runs after each frame.
            if (manager)
                manager->PostProcess();
        }
    }

    void TestSound(CKContext *context, CKBOOL streamed)
    {
        const char *mode = streamed ? "streamed" : "loaded";
        CKWaveSound *sound = (CKWaveSound *)context->CreateObject(CKCID_WAVESOUND, (CKSTRING) "WiiTestSound");
        sound->SetType(CK_WAVESOUND_BACKGROUND);
        const CKERROR err = sound->Create(streamed, (CKSTRING)kPath);
        WT_CHECK(err == CK_OK, "%s sound created (%d)", mode, err);
        if (err != CK_OK)
        {
            context->DestroyObject(sound);
            return;
        }
        CKWaveFormat format;
        memset(&format, 0, sizeof(format));
        sound->GetSoundFormat(format);
        WT_CHECK(format.nSamplesPerSec == kRate && format.wBitsPerSample == 16 && format.nChannels == 1,
                 "%s format %d Hz %d bits %d ch", mode, (int)format.nSamplesPerSec, format.wBitsPerSample,
                 format.nChannels);
        const int length = sound->GetSoundLength();
        WT_CHECK(length > 950 && length < 1050, "%s length %d ms", mode, length);

        sound->Play();
        WT_CHECK(sound->IsPlaying(), "%s sound plays", mode);
        WaitFrames(context, 15);
        const CKDWORD played = sound->GetPlayPosition();
        WT_CHECK(played > 0, "%s play position advances (%u)", mode, played);

        sound->Pause();
        WT_CHECK(sound->IsPaused(), "%s sound pauses", mode);
        const CKDWORD paused = sound->GetPlayPosition();
        WaitFrames(context, 10);
        const CKDWORD stillPaused = sound->GetPlayPosition();
        WT_CHECK(stillPaused == paused, "%s position holds while paused (%u, %u)", mode, paused, stillPaused);

        sound->Resume();
        WaitFrames(context, 10);
        WT_CHECK(sound->IsPlaying(), "%s sound resumes", mode);

        // The tone ends after a second, unless it loops.
        if (!streamed)
        {
            sound->Stop();
            sound->SetLoopMode(TRUE);
            sound->Play();
            WaitFrames(context, 80);
            WT_CHECK(sound->IsPlaying(), "%s looping sound still plays after its length", mode);
            sound->SetLoopMode(FALSE);
        }
        sound->Stop();
        WaitFrames(context, 2);
        WT_CHECK(!sound->IsPlaying(), "%s sound stops", mode);
        context->DestroyObject(sound);
    }
}

void RunSoundTests(CKContext *context)
{
    wiitest::BeginSuite("Sound");
    if (!WT_CHECK(context && context->GetManagerByGuid(SOUND_MANAGER_GUID), "sound manager"))
    {
        wiitest::EndSuite();
        return;
    }
    if (!WT_CHECK(WriteTone(), "tone written"))
    {
        wiitest::EndSuite();
        return;
    }
    TestSound(context, FALSE);
    TestSound(context, TRUE);
    remove(kPath);
    wiitest::EndSuite();
}
