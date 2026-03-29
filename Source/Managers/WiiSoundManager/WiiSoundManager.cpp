#include "WiiSoundManager.h"
#include <string.h>

CKERROR CreateWiiSoundManager(CKContext* context, CKBaseManager** res) {
    *res = new WiiSoundManager(context);
    return CK_OK;
}

CKERROR RemoveWiiSoundManager(CKContext* context, CKBaseManager* res) {
    delete (WiiSoundManager*)res;
    return CK_OK;
}

WiiSoundManager::WiiSoundManager(CKContext* Context) : CKSoundManager(Context, "WiiSoundManager") {
    m_Initialized = FALSE;
    m_NextVoiceID = 0;
}

WiiSoundManager::~WiiSoundManager() {}

CKERROR WiiSoundManager::OnCKInit() {
#ifdef WII
    ASND_Init();
    ASND_Pause(0);
#endif
    m_Initialized = TRUE;
    return CK_OK;
}

CKERROR WiiSoundManager::OnCKEnd() {
#ifdef WII
    ASND_End();
#endif
    m_Initialized = FALSE;
    return CK_OK;
}

CK_SOUNDMANAGER_CAPS WiiSoundManager::GetCaps() {
    return (CK_SOUNDMANAGER_CAPS)(CK_SOUNDMANAGER_ALL);
}

void *WiiSoundManager::CreateSource(CK_WAVESOUND_TYPE flags, CKWaveFormat *wf, CKDWORD bytes, CKBOOL streamed) {
    WiiSoundSource* src = new WiiSoundSource();
    src->voice_id = -1; // Assigned dynamically on play

    src->byte_size = bytes;
    src->sample_rate = wf ? wf->nSamplesPerSec : 44100;
    src->volume = 255;
    src->is_playing = FALSE;
    src->is_looping = FALSE;
    src->type = flags;

    if (wf) {
        src->format = *wf;
    } else {
        memset(&src->format, 0, sizeof(CKWaveFormat));
    }

#ifdef WII
    // The allocated pointer must be 32-byte aligned for ASND / DMA reads
    src->pcm_data = (short*)memalign(32, bytes);
#else
    src->pcm_data = (short*)malloc(bytes);
#endif
    memset(src->pcm_data, 0, bytes);

    return src;
}

void *WiiSoundManager::DuplicateSource(void *source) {
    if (!source) return NULL;
    WiiSoundSource* orig_src = (WiiSoundSource*)source;
    WiiSoundSource* src = (WiiSoundSource*)CreateSource(orig_src->type, &orig_src->format, orig_src->byte_size, FALSE);
    if (src && src->pcm_data && orig_src->pcm_data) {
        memcpy(src->pcm_data, orig_src->pcm_data, orig_src->byte_size);
    }
    return src;
}

void WiiSoundManager::ReleaseSource(void *source) {
    if (!source) return;
    WiiSoundSource* src = (WiiSoundSource*)source;
#ifdef WII
    ASND_StopVoice(src->voice_id);
    free(src->pcm_data);
#else
    free(src->pcm_data);
#endif
    delete src;
}

void WiiSoundManager::Play(CKWaveSound *w, void *source, CKBOOL loop) {
    InternalPlay(source, loop);
}

void WiiSoundManager::Pause(CKWaveSound *w, void *source) {
    InternalPause(source);
}

void WiiSoundManager::SetPlayPosition(void *source, int pos) {}
int WiiSoundManager::GetPlayPosition(void *source) { return 0; }

CKBOOL WiiSoundManager::IsPlaying(void *source) {
    if (!source) return FALSE;
    WiiSoundSource* src = (WiiSoundSource*)source;
    return src->is_playing;
}

CKERROR WiiSoundManager::SetWaveFormat(void *source, CKWaveFormat &wf) {
    if (!source) return CKERR_INVALIDPARAMETER;
    WiiSoundSource* src = (WiiSoundSource*)source;
    src->format = wf;
    src->sample_rate = wf.nSamplesPerSec;
    return CK_OK;
}

CKERROR WiiSoundManager::GetWaveFormat(void *source, CKWaveFormat &wf) {
    if (!source) return CKERR_INVALIDPARAMETER;
    WiiSoundSource* src = (WiiSoundSource*)source;
    wf = src->format;
    return CK_OK;
}

int WiiSoundManager::GetWaveSize(void *source) {
    if (!source) return 0;
    WiiSoundSource* src = (WiiSoundSource*)source;
    return src->byte_size;
}

CKERROR WiiSoundManager::Lock(void *source, CKDWORD WriteCursor, CKDWORD NumBytes, void **AudioPtr1, CKDWORD *dwAudioBytes1, void **pvAudioPtr2, CKDWORD *dwAudioBytes2, CK_WAVESOUND_LOCKMODE dwFlags) {
    if (!source || !AudioPtr1 || !dwAudioBytes1) return CKERR_INVALIDPARAMETER;
    WiiSoundSource* src = (WiiSoundSource*)source;

    // Direct buffer write
    *AudioPtr1 = (void*)((char*)src->pcm_data + WriteCursor);
    *dwAudioBytes1 = NumBytes;

    if (pvAudioPtr2) *pvAudioPtr2 = NULL;
    if (dwAudioBytes2) *dwAudioBytes2 = 0;

    return CK_OK;
}

CKERROR WiiSoundManager::Unlock(void *source, void *AudioPtr1, CKDWORD NumBytes1, void *AudioPtr2, CKDWORD dwAudioBytes2) {
    // Data is directly manipulated in lock pointer due to memalign
    return CK_OK;
}

void WiiSoundManager::SetType(void *source, CK_WAVESOUND_TYPE type) {
    if (!source) return;
    WiiSoundSource* src = (WiiSoundSource*)source;
    src->type = type;
}

CK_WAVESOUND_TYPE WiiSoundManager::GetType(void *source) {
    if (!source) return (CK_WAVESOUND_TYPE)0;
    WiiSoundSource* src = (WiiSoundSource*)source;
    return src->type;
}

void WiiSoundManager::UpdateSettings(void *source, CK_SOUNDMANAGER_CAPS settingsoptions, CKWaveSoundSettings &settings, CKBOOL set) {}
void WiiSoundManager::Update3DSettings(void *source, CK_SOUNDMANAGER_CAPS settingsoptions, CKWaveSound3DSettings &settings, CKBOOL set) {}
void WiiSoundManager::UpdateListenerSettings(CK_SOUNDMANAGER_CAPS settingsoptions, CKListenerSettings &settings, CKBOOL set) {}
CKBOOL WiiSoundManager::IsInitialized() { return m_Initialized; }

void WiiSoundManager::InternalPause(void *source) {
    if (!source) return;
    WiiSoundSource* src = (WiiSoundSource*)source;
    src->is_playing = FALSE;
#ifdef WII
    if (src->voice_id >= 0) {
        ASND_StopVoice(src->voice_id);
        src->voice_id = -1;
    }
#endif
}

void WiiSoundManager::InternalPlay(void *source, CKBOOL loop) {
    if (!source) return;
    WiiSoundSource* src = (WiiSoundSource*)source;
    src->is_playing = TRUE;
    src->is_looping = loop;

#ifdef WII
    DCFlushRange(src->pcm_data, src->byte_size);

    src->voice_id = m_NextVoiceID++;
    if (m_NextVoiceID > 15) m_NextVoiceID = 0;

    ASND_StopVoice(src->voice_id);

    // Simplistic mapping, typically dr_wav decodes to 16bit Stereo. Format checks would ideally be deeper.
    int format = VOICE_STEREO_16BIT;

    ASND_SetVoice(
        src->voice_id,
        format,
        src->sample_rate,
        0,
        src->pcm_data,
        src->byte_size,
        src->volume,
        src->volume,
        NULL
    );
#endif
}
