#ifndef WIISOUNDMANAGER_H
#define WIISOUNDMANAGER_H

#include "CKAll.h"

#ifdef WII
#include <asndlib.h>
#include <malloc.h>
#include <ogc/machine/processor.h>
#endif

// Wii-specific sound source descriptor
struct WiiSoundSource {
    int voice_id;
    short* pcm_data;
    int byte_size;
    int sample_rate;
    int volume;
    CKBOOL is_playing;
    CKBOOL is_looping;
    CK_WAVESOUND_TYPE type;
    CKWaveFormat format;
};

class WiiSoundManager : public CKSoundManager {
public:
    WiiSoundManager(CKContext* Context);
    virtual ~WiiSoundManager();

    // Base Manager Hooks
    virtual CKERROR OnCKInit();
    virtual CKERROR OnCKEnd();

    // CKSoundManager Requirements
    virtual CK_SOUNDMANAGER_CAPS GetCaps();
    virtual void *CreateSource(CK_WAVESOUND_TYPE flags, CKWaveFormat *wf, CKDWORD bytes, CKBOOL streamed);
    virtual void *DuplicateSource(void *source);
    virtual void ReleaseSource(void *source);
    virtual void Play(CKWaveSound *w, void *source, CKBOOL loop);
    virtual void Pause(CKWaveSound *w, void *source);
    virtual void SetPlayPosition(void *source, int pos);
    virtual int GetPlayPosition(void *source);
    virtual CKBOOL IsPlaying(void *source);
    virtual CKERROR SetWaveFormat(void *source, CKWaveFormat &wf);
    virtual CKERROR GetWaveFormat(void *source, CKWaveFormat &wf);
    virtual int GetWaveSize(void *source);
    virtual CKERROR Lock(void *source, CKDWORD WriteCursor, CKDWORD NumBytes, void **AudioPtr1, CKDWORD *dwAudioBytes1, void **pvAudioPtr2, CKDWORD *dwAudioBytes2, CK_WAVESOUND_LOCKMODE dwFlags);
    virtual CKERROR Unlock(void *source, void *AudioPtr1, CKDWORD NumBytes1, void *AudioPtr2, CKDWORD dwAudioBytes2);
    virtual void SetType(void *source, CK_WAVESOUND_TYPE type);
    virtual CK_WAVESOUND_TYPE GetType(void *source);
    virtual void UpdateSettings(void *source, CK_SOUNDMANAGER_CAPS settingsoptions, CKWaveSoundSettings &settings, CKBOOL set = TRUE);
    virtual void Update3DSettings(void *source, CK_SOUNDMANAGER_CAPS settingsoptions, CKWaveSound3DSettings &settings, CKBOOL set = TRUE);
    virtual void UpdateListenerSettings(CK_SOUNDMANAGER_CAPS settingsoptions, CKListenerSettings &settings, CKBOOL set = TRUE);
    virtual CKBOOL IsInitialized();

protected:
    virtual void InternalPause(void *source);
    virtual void InternalPlay(void *source, CKBOOL loop = FALSE);

private:
    CKBOOL m_Initialized;
    int m_NextVoiceID;
};

#endif // WIISOUNDMANAGER_H
