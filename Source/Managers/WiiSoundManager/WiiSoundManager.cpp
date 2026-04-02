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
#ifdef WII
    memset(m_ActiveVoices, 0, sizeof(m_ActiveVoices));
#endif
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

void WiiSoundManager::Update3DSettings(void *source, CK_SOUNDMANAGER_CAPS settingsoptions, CKWaveSound3DSettings &settings, CKBOOL set) {
#ifdef WII
    if (!source) return;
    WiiSoundSource* src = (WiiSoundSource*)source;
    if (src->voice_id < 0) return;

    // A complete 3D spatialization needs listener coordinates and distance attenuation algorithms.
    // For a basic implementation, we extract distance from properties if provided,
    // or we can calculate a pseudo pan from X coordinates if known.
    // For now, hook into ASND_SetVoiceVolume and ASND_SetVoicePan
    // using simplified values based on typical 3D settings.

    // Update volume based on distance (stubbed mapping from 3D coords, needs listener global vars to be exact)
    // If we have settings we can approximate panning and volume.
    // settings.Position (VxVector) holds the sound position.
    // We would compare this to a global Listener vector updated in UpdateListenerSettings.

    // Extract sound position
    VxVector soundPos = settings.m_Position;

    // Dynamically fetch the listener position and orientation from the active camera
    VxVector listenerPos(0.0f, 0.0f, 0.0f);
    VxVector listenerRight(1.0f, 0.0f, 0.0f);

    CKRenderManager* rm = m_Context->GetRenderManager();
    if (rm && rm->GetRenderContextCount() > 0) {
        CKRenderContext* rc = rm->GetRenderContext(0);
        if (rc) {
            CKCamera* cam = rc->GetAttachedCamera();
            if (cam) {
                cam->GetPosition(&listenerPos, NULL);
                VxVector dir, up;
                cam->GetOrientation(&dir, &up, &listenerRight, NULL);
            }
        }
    }

    // Calculate distance
    VxVector diff = soundPos - listenerPos;
    float dist = diff.Magnitude();

    // Calculate volume attenuation using a logarithmic distance curve
    float minD = settings.m_MinDistance > 0.1f ? settings.m_MinDistance : 1.0f;
    float maxD = settings.m_MaxDistance > minD ? settings.m_MaxDistance : 1000.0f;

    float volScale = 1.0f;
    if (dist > minD) {
        if (dist >= maxD) {
            volScale = 0.0f;
        } else {
            // Inverse distance (logarithmic-style attenuation)
            // Roll-off factor models real-world physics
            float rolloff = 1.0f;
            volScale = minD / (minD + rolloff * (dist - minD));

            // Clamp to 0.0 at max distance to fully mute distant objects
            float maxVolScale = minD / (minD + rolloff * (maxD - minD));
            volScale = (volScale - maxVolScale) / (1.0f - maxVolScale);
            if (volScale < 0.0f) volScale = 0.0f;
        }
    }

    int baseVol = (int)(src->volume * volScale);
    if (baseVol < 0) baseVol = 0;
    if (baseVol > 255) baseVol = 255;

    // Calculate pan: dot product of listener's right vector and the normalized direction to the sound
    float dotRight = 0.0f; // Center default
    if (dist > 0.001f) {
        VxVector dir = diff / dist;
        dotRight = DotProduct(dir, listenerRight); // Range [-1.0, 1.0]
    }

    // Convert pan to left/right volume balance
    // -1.0 means full left (left = baseVol, right = 0)
    //  1.0 means full right (left = 0, right = baseVol)
    int leftVol = baseVol;
    int rightVol = baseVol;

    if (dotRight < 0.0f) {
        // Panned left: reduce right volume
        rightVol = (int)(baseVol * (1.0f + dotRight));
    } else if (dotRight > 0.0f) {
        // Panned right: reduce left volume
        leftVol = (int)(baseVol * (1.0f - dotRight));
    }

    ASND_SetVoiceVolume(src->voice_id, leftVol, rightVol);
#endif
}

void WiiSoundManager::UpdateListenerSettings(CK_SOUNDMANAGER_CAPS settingsoptions, CKListenerSettings &settings, CKBOOL set) {
}
CKBOOL WiiSoundManager::IsInitialized() { return m_Initialized; }

void WiiSoundManager::InternalPause(void *source) {
    if (!source) return;
    WiiSoundSource* src = (WiiSoundSource*)source;
    src->is_playing = FALSE;
#ifdef WII
    if (src->voice_id >= 0 && src->voice_id < 16) {
        if (m_ActiveVoices[src->voice_id] == src) {
            ASND_StopVoice(src->voice_id);
            m_ActiveVoices[src->voice_id] = NULL;
        }
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

    // Hardware voice management: Priority Queue
    // Find an empty voice slot first
    int assigned_voice = -1;
    for (int i = 0; i < 16; i++) {
        if (!m_ActiveVoices[i] || ASND_StatusVoice(i) == SND_UNUSED) {
            assigned_voice = i;
            break;
        }
    }

    // If no empty slot, cull the quietest sound
    if (assigned_voice == -1) {
        int lowest_vol = 256;
        int target_cull = 0;
        for (int i = 0; i < 16; i++) {
            if (m_ActiveVoices[i] && m_ActiveVoices[i]->volume < lowest_vol) {
                lowest_vol = m_ActiveVoices[i]->volume;
                target_cull = i;
            }
        }
        // Force stop the culled voice
        ASND_StopVoice(target_cull);
        if (m_ActiveVoices[target_cull]) {
            m_ActiveVoices[target_cull]->voice_id = -1;
            m_ActiveVoices[target_cull]->is_playing = FALSE;
        }
        assigned_voice = target_cull;
    }

    src->voice_id = assigned_voice;
    m_ActiveVoices[assigned_voice] = src;

    int format = VOICE_STEREO_16BIT;
    if (src->format.nChannels == 1) {
        if (src->format.wBitsPerSample == 8) format = VOICE_MONO_8BIT;
        else format = VOICE_MONO_16BIT;
    } else {
        if (src->format.wBitsPerSample == 8) format = VOICE_STEREO_8BIT;
        else format = VOICE_STEREO_16BIT;
    }

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
