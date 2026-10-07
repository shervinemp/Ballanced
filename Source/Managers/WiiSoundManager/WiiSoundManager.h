#ifndef WIISOUNDMANAGER_H
#define WIISOUNDMANAGER_H

#include "CKAll.h"

class WiiSoundManager;

/**
 * One Virtools sound buffer, played on an ASND hardware voice.
 *
 * Whole sounds keep their PCM in a 32-byte aligned buffer. Streamed sounds
 * (music) use the buffer as a ring that loops on the voice while CKWaveSound
 * refills it ahead of the play cursor, like a DirectSound streaming buffer.
 */
struct WiiSoundSource
{
    WiiSoundManager *Manager;
    CK_WAVESOUND_TYPE Type;
    CKWaveFormat Format;
    CKWaveSoundSettings Settings;
    CKWaveSound3DSettings Settings3D;

    CKBYTE *Buffer;        // 32-byte aligned, padded with silence
    CKDWORD Size;          // Bytes the engine sees (GetWaveSize)
    CKDWORD Capacity;      // Allocated bytes
    CKBOOL Streamed;
    CKDWORD WritePos;

    int Voice;             // ASND voice, -1 when none
    CKBOOL Playing;
    CKBOOL Paused;
    CKBOOL Looping;

    // Play cursor bookkeeping: ASND reports output ticks, not source frames.
    double StartFrame;     // Source frame where the voice started
    double PlayedFrames;   // Source frames played since then
    CKDWORD LastTicks;
    float Pitch;           // Source frames per output frame relative to the base rate

    // Final per-channel volumes (0..1) before the global gain.
    float LeftGain;
    float RightGain;
};

class WiiSoundManager : public CKSoundManager
{
    friend struct WiiSoundSource;

public:
    WiiSoundManager(CKContext *context);
    ~WiiSoundManager() override;

    CK_SOUNDMANAGER_CAPS GetCaps() override;

    void *CreateSource(CK_WAVESOUND_TYPE type, CKWaveFormat *wf, CKDWORD bytes, CKBOOL streamed) override;
    void *DuplicateSource(void *source) override;
    void ReleaseSource(void *source) override;

    void Play(CKWaveSound *ws, void *source, CKBOOL loop) override;
    void Pause(CKWaveSound *ws, void *source) override;
    void SetPlayPosition(void *source, int pos) override;
    int GetPlayPosition(void *source) override;
    CKBOOL IsPlaying(void *source) override;

    CKERROR SetWaveFormat(void *source, CKWaveFormat &wf) override;
    CKERROR GetWaveFormat(void *source, CKWaveFormat &wf) override;
    int GetWaveSize(void *source) override;

    CKERROR Lock(void *source, CKDWORD writeCursor, CKDWORD numBytes,
                 void **audioPtr1, CKDWORD *audioBytes1,
                 void **audioPtr2, CKDWORD *audioBytes2,
                 CK_WAVESOUND_LOCKMODE flags) override;
    CKERROR Unlock(void *source, void *audioPtr1, CKDWORD numBytes1, void *audioPtr2, CKDWORD audioBytes2) override;

    void SetType(void *source, CK_WAVESOUND_TYPE type) override;
    CK_WAVESOUND_TYPE GetType(void *source) override;

    void UpdateSettings(void *source, CK_SOUNDMANAGER_CAPS options, CKWaveSoundSettings &settings, CKBOOL set = TRUE) override;
    void Update3DSettings(void *source, CK_SOUNDMANAGER_CAPS options, CKWaveSound3DSettings &settings, CKBOOL set = TRUE) override;
    void UpdateListenerSettings(CK_SOUNDMANAGER_CAPS options, CKListenerSettings &settings, CKBOOL set = TRUE) override;

    CKBOOL IsInitialized() override;

    CKERROR OnCKInit() override;
    CKERROR OnCKEnd() override;
    CKERROR OnCKReset() override;
    CKERROR OnCKPause() override;
    CKERROR OnCKPlay() override;
    CKERROR PostClearAll() override;
    CKERROR PostProcess() override;
    CKERROR PreLaunchScene(CKScene *oldScene, CKScene *newScene) override;
    CKERROR SequenceToBeDeleted(CK_ID *objids, int count) override;

    CKDWORD GetValidFunctionsMask() override
    {
        return CKSoundManager::GetValidFunctionsMask() |
               CKMANAGER_FUNC_OnCKInit | CKMANAGER_FUNC_OnCKEnd | CKMANAGER_FUNC_OnCKReset |
               CKMANAGER_FUNC_OnCKPause | CKMANAGER_FUNC_OnCKPlay | CKMANAGER_FUNC_PostClearAll |
               CKMANAGER_FUNC_PostProcess | CKMANAGER_FUNC_OnSequenceToBeDeleted |
               CKMANAGER_FUNC_PreLaunchScene;
    }

protected:
    void InternalPause(void *source) override;
    void InternalPlay(void *source, CKBOOL loop = FALSE) override;

private:
    WiiSoundSource *ResolveSource(CKWaveSound *ws, void *source);
    void StartVoice(WiiSoundSource *source);
    void StopVoice(WiiSoundSource *source);
    int AllocateVoice(WiiSoundSource *requester);
    void UpdatePlayedFrames(WiiSoundSource *source);
    void ApplyVoiceParameters(WiiSoundSource *source);
    void ApplySettings(WiiSoundSource *source);
    void Apply3DSettings(WiiSoundSource *source);
    void Refresh3DSources();
    void PositionSource(WiiSoundSource *source, CK3dEntity *ent, const VxVector &position,
                        const VxVector &direction, VxVector &oldPosition, float deltaTime);
    void StopAllPlayingSounds();

    CKBOOL m_Initialized;
    CKListenerSettings m_ListenerSettings;
    XObjectArray m_SoundsPlaying;
    WiiSoundSource *m_Voices[16];

    VxVector m_ListenerPosition;
    VxVector m_ListenerVelocity;
    VxVector m_ListenerRight;
    VxVector m_ListenerUp;
    VxVector m_ListenerForward;
};

#endif // WIISOUNDMANAGER_H
