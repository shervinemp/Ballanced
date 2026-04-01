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
    VxVector soundPos = settings.Position;

    // We need the listener position. We'll use a static variable updated in UpdateListenerSettings
    extern VxVector g_ListenerPos;
    extern VxVector g_ListenerRight; // For panning

    // Calculate distance
    VxVector diff = soundPos - g_ListenerPos;
    float dist = diff.Magnitude();

    // Calculate volume attenuation (simple linear fallback)
    // You can use settings.MinDistance and settings.MaxDistance for a proper curve
    float minD = settings.MinDistance > 0.1f ? settings.MinDistance : 1.0f;
    float maxD = settings.MaxDistance > minD ? settings.MaxDistance : 1000.0f;

    float volScale = 1.0f;
    if (dist > minD) {
        if (dist >= maxD) {
            volScale = 0.0f;
        } else {
            // Linear attenuation between min and max
            volScale = 1.0f - ((dist - minD) / (maxD - minD));
        }
    }

    int vol = (int)(src->volume * volScale);
    if (vol < 0) vol = 0;
    if (vol > 255) vol = 255;

    ASND_SetVoiceVolume(src->voice_id, vol, vol);

    // Calculate pan: dot product of listener's right vector and the normalized direction to the sound
    int pan = 127; // Center default
    if (dist > 0.001f) {
        VxVector dir = diff / dist;
        float dotRight = DotProduct(dir, g_ListenerRight); // Range [-1.0, 1.0]
        // Map [-1.0, 1.0] to [0, 255] where -1 is Left (0) and 1 is Right (255)
        pan = (int)((dotRight + 1.0f) * 127.5f);
        if (pan < 0) pan = 0;
        if (pan > 255) pan = 255;
    }

    ASND_SetVoicePan(src->voice_id, pan);
#endif
}

#ifdef WII
VxVector g_ListenerPos(0.0f, 0.0f, 0.0f);
VxVector g_ListenerRight(1.0f, 0.0f, 0.0f);
#endif

void WiiSoundManager::UpdateListenerSettings(CK_SOUNDMANAGER_CAPS settingsoptions, CKListenerSettings &settings, CKBOOL set) {
#ifdef WII
    // Store global listener coordinates (settings.Position, settings.Orientation) here
    // to be used by Update3DSettings for volume distance scaling and panning.
    g_ListenerPos = settings.Position;
    // Orientation is a matrix or quaternion, but we can simplify by extracting the Right vector from the Orientation matrix (typically the first row)
    // Assuming settings.Orientation is a VxMatrix or similar where row 0 is Right vector
    // VxVector right(settings.Orientation[0][0], settings.Orientation[0][1], settings.Orientation[0][2]);
    // g_ListenerRight = Normalize(right);
#endif
}
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
