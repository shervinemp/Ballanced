#include "WiiSoundManager.h"

#include <asndlib.h>
#include <gccore.h>
#include <malloc.h>

#include <math.h>
#include <string.h>

namespace
{
    const float kPi = 3.14159265358979323846f;
    const float kSpeedOfSound = 343.3f;
    const float kMinDopplerRatio = 0.125f;
    const float kMaxDopplerRatio = 8.0f;
    const double kOutputRate = 48000.0; // ASND mixes at 48 kHz
    const CKDWORD kAlignment = 32;      // ASND buffers: 32-byte aligned and padded

    WiiSoundSource *g_Voices[MAX_SND_VOICES]; // Read by the looping callback

    float Clamp(float value, float minValue, float maxValue)
    {
        return value < minValue ? minValue : (value > maxValue ? maxValue : value);
    }

    CKDWORD AlignDown(CKDWORD value)
    {
        return value & ~(kAlignment - 1);
    }

    CKDWORD AlignUp(CKDWORD value)
    {
        return (value + kAlignment - 1) & ~(kAlignment - 1);
    }

    CKDWORD FrameBytes(const CKWaveFormat &format)
    {
        if (format.nBlockAlign > 0)
            return format.nBlockAlign;
        const CKDWORD bytes = (CKDWORD)format.nChannels * (format.wBitsPerSample / 8);
        return bytes > 0 ? bytes : 1;
    }

    // Sound readers (dr_wav) decode 16-bit samples to the host byte order, which
    // is big-endian here; 8-bit WAV samples stay unsigned.
    int VoiceFormat(const CKWaveFormat &format)
    {
        const bool stereo = format.nChannels >= 2;
        if (format.wBitsPerSample == 8)
            return stereo ? VOICE_STEREO_8BIT_U : VOICE_MONO_8BIT_U;
        return stereo ? VOICE_STEREO_16BIT_BE : VOICE_MONO_16BIT_BE;
    }

    CKBYTE SilenceByte(const CKWaveFormat &format)
    {
        return format.wBitsPerSample == 8 ? 0x80 : 0x00;
    }

    // Loops must cover whole ASND blocks; a whole sound may be padded with silence.
    CKDWORD LoopBytes(const WiiSoundSource *source)
    {
        const CKDWORD bytes = AlignDown(source->Size);
        return bytes > 0 ? bytes : kAlignment;
    }

    void FlushRange(const void *pointer, CKDWORD bytes)
    {
        if (!pointer || bytes == 0)
            return;
        const uintptr_t start = (uintptr_t)pointer & ~(uintptr_t)(kAlignment - 1);
        const uintptr_t end = ((uintptr_t)pointer + bytes + kAlignment - 1) & ~(uintptr_t)(kAlignment - 1);
        DCFlushRange((void *)start, (u32)(end - start));
    }

    // Restarts a looping voice that began mid-buffer from the buffer start.
    void LoopVoiceCallback(s32 voice)
    {
        if (voice < 0 || voice >= MAX_SND_VOICES)
            return;
        WiiSoundSource *source = g_Voices[voice];
        if (source && source->Looping && source->Buffer)
            ASND_AddVoice(voice, source->Buffer, LoopBytes(source));
    }

    void NormalizeOrDefault(VxVector &value, float x, float y, float z)
    {
        const float length = value.SquareMagnitude();
        if (length > 0.000001f)
            value *= 1.0f / sqrtf(length);
        else
            value.Set(x, y, z);
    }

    float DirectionToPan(const VxVector &direction)
    {
        const float length = sqrtf(direction.x * direction.x + direction.z * direction.z);
        if (length <= 0.000001f)
            return 0.0f;
        return Clamp(direction.x / length, -1.0f, 1.0f);
    }

    void ConstantPowerPan(float pan, float &leftGain, float &rightGain)
    {
        pan = Clamp(pan, -1.0f, 1.0f);
        leftGain = sqrtf(0.5f * (1.0f - pan));
        rightGain = sqrtf(0.5f * (1.0f + pan));
    }
}

// ---------------------------------------------------------------------------
// Construction

WiiSoundManager::WiiSoundManager(CKContext *context) : CKSoundManager(context, (CKSTRING)"Wii Sound Manager")
{
    m_Initialized = FALSE;
    memset(m_Voices, 0, sizeof(m_Voices));
    m_ListenerPosition.Set(0.0f, 0.0f, 0.0f);
    m_ListenerVelocity.Set(0.0f, 0.0f, 0.0f);
    m_ListenerRight.Set(1.0f, 0.0f, 0.0f);
    m_ListenerUp.Set(0.0f, 1.0f, 0.0f);
    m_ListenerForward.Set(0.0f, 0.0f, 1.0f);
    m_Context->RegisterNewManager(this);
}

WiiSoundManager::~WiiSoundManager()
{
    if (m_Initialized)
    {
        ReleaseMinions();
        ASND_End();
        m_Initialized = FALSE;
    }
}

CK_SOUNDMANAGER_CAPS WiiSoundManager::GetCaps()
{
    CKDWORD caps = CK_WAVESOUND_SETTINGS_ALL | CK_WAVESOUND_3DSETTINGS_ALL | CK_LISTENERSETTINGS_ALL |
                   CK_WAVESOUND_3DSETTINGS_DISTANCEFACTOR | CK_WAVESOUND_3DSETTINGS_DOPPLERFACTOR;
    caps &= ~(CK_WAVESOUND_SETTINGS_EQUALIZATION | CK_WAVESOUND_SETTINGS_PRIORITY | CK_LISTENERSETTINGS_EQ |
              CK_LISTENERSETTINGS_PRIORITY | CK_SOUNDMANAGER_ONFLYTYPE);
    return (CK_SOUNDMANAGER_CAPS)caps;
}

CKBOOL WiiSoundManager::IsInitialized()
{
    return m_Initialized;
}

// ---------------------------------------------------------------------------
// Sources

void *WiiSoundManager::CreateSource(CK_WAVESOUND_TYPE type, CKWaveFormat *wf, CKDWORD bytes, CKBOOL streamed)
{
    if (!wf || bytes == 0 || !m_Initialized)
        return NULL;

    WiiSoundSource *source = new WiiSoundSource();
    memset(source, 0, sizeof(WiiSoundSource));
    source->Manager = this;
    source->Type = type;
    source->Format = *wf;
    source->Settings = CKWaveSoundSettings();
    source->Settings3D = CKWaveSound3DSettings();
    source->Streamed = streamed;
    // A streaming ring loops on the voice as a whole, so it must be block sized.
    source->Size = streamed ? (AlignDown(bytes) > 0 ? AlignDown(bytes) : kAlignment) : bytes;
    source->Capacity = AlignUp(source->Size) + kAlignment;
    source->Voice = -1;
    source->Pitch = 1.0f;
    source->LeftGain = 1.0f;
    source->RightGain = 1.0f;

    source->Buffer = (CKBYTE *)memalign(kAlignment, source->Capacity);
    if (!source->Buffer)
    {
        delete source;
        return NULL;
    }
    memset(source->Buffer, SilenceByte(source->Format), source->Capacity);

    ApplySettings(source);
    return source;
}

void *WiiSoundManager::DuplicateSource(void *source)
{
    WiiSoundSource *original = (WiiSoundSource *)source;
    if (!original)
        return NULL;

    WiiSoundSource *copy = (WiiSoundSource *)CreateSource(original->Type, &original->Format, original->Size, original->Streamed);
    if (!copy)
        return NULL;

    memcpy(copy->Buffer, original->Buffer, original->Capacity < copy->Capacity ? original->Capacity : copy->Capacity);
    copy->Settings = original->Settings;
    copy->Settings3D = original->Settings3D;
    ApplySettings(copy);
    return copy;
}

void WiiSoundManager::ReleaseSource(void *source)
{
    WiiSoundSource *wiiSource = (WiiSoundSource *)source;
    if (!wiiSource)
        return;
    StopVoice(wiiSource);
    free(wiiSource->Buffer);
    delete wiiSource;
}

// The engine passes a SoundMinion instead of a source for fire-and-forget sounds.
WiiSoundSource *WiiSoundManager::ResolveSource(CKWaveSound *ws, void *source)
{
    if (!source)
        return NULL;
    if (ws)
        return (WiiSoundSource *)source;
    SoundMinion *minion = (SoundMinion *)source;
    return (WiiSoundSource *)minion->m_Source;
}

// ---------------------------------------------------------------------------
// Voices

int WiiSoundManager::AllocateVoice(WiiSoundSource *requester)
{
    // Voices whose one-shot sound ended are free again.
    for (int voice = 0; voice < MAX_SND_VOICES; ++voice)
    {
        WiiSoundSource *owner = m_Voices[voice];
        if (owner && !owner->Looping && !owner->Streamed && ASND_StatusVoice(voice) == SND_UNUSED)
        {
            owner->Voice = -1;
            owner->Playing = FALSE;
            owner->StartFrame = 0.0;
            owner->PlayedFrames = 0.0;
            m_Voices[voice] = NULL;
            g_Voices[voice] = NULL;
        }
        if (!m_Voices[voice])
            return voice;
    }

    // All sixteen voices busy: take the quietest effect, never music.
    int victim = -1;
    float quietest = 2.0f;
    for (int voice = 0; voice < MAX_SND_VOICES; ++voice)
    {
        WiiSoundSource *owner = m_Voices[voice];
        if (!owner || owner == requester || (owner->Type & CK_WAVESOUND_BACKGROUND))
            continue;
        const float loudness = owner->LeftGain > owner->RightGain ? owner->LeftGain : owner->RightGain;
        if (loudness < quietest)
        {
            quietest = loudness;
            victim = voice;
        }
    }
    if (victim >= 0)
        StopVoice(m_Voices[victim]);
    return victim;
}

void WiiSoundManager::StopVoice(WiiSoundSource *source)
{
    if (!source || source->Voice < 0)
        return;
    const int voice = source->Voice;
    if (m_Voices[voice] == source)
    {
        g_Voices[voice] = NULL;
        ASND_StopVoice(voice);
        m_Voices[voice] = NULL;
    }
    source->Voice = -1;
}

void WiiSoundManager::UpdatePlayedFrames(WiiSoundSource *source)
{
    if (source->Voice < 0 || source->Paused)
        return;
    const CKDWORD ticks = ASND_GetTickCounterVoice(source->Voice);
    const CKDWORD delta = ticks - source->LastTicks;
    source->LastTicks = ticks;
    source->PlayedFrames += (double)delta * ((double)source->Format.nSamplesPerSec * source->Pitch / kOutputRate);
}

static double CurrentFrame(const WiiSoundSource *source)
{
    const double frames = source->StartFrame + source->PlayedFrames;
    const double totalFrames = (double)source->Size / (double)FrameBytes(source->Format);
    if (totalFrames <= 0.0)
        return 0.0;
    if (source->Looping || source->Streamed)
        return fmod(frames, totalFrames);
    return frames < totalFrames ? frames : totalFrames;
}

void WiiSoundManager::StartVoice(WiiSoundSource *source)
{
    if (!m_Initialized || !source->Buffer)
        return;

    if (source->Voice < 0)
    {
        const int voice = AllocateVoice(source);
        if (voice < 0)
            return;
        source->Voice = voice;
        m_Voices[voice] = source;
        g_Voices[voice] = source;
    }

    const CKDWORD frameBytes = FrameBytes(source->Format);
    CKDWORD start = AlignDown((CKDWORD)(source->StartFrame * frameBytes));
    if (start >= source->Size)
        start = 0;
    source->StartFrame = (double)(start / frameBytes);
    source->PlayedFrames = 0.0;

    FlushRange(source->Buffer, source->Capacity);

    const int format = VoiceFormat(source->Format);
    const s32 pitch = (s32)Clamp((float)source->Format.nSamplesPerSec * source->Pitch, 1.0f, (float)MAX_PITCH);
    const s32 left = (s32)(Clamp(source->LeftGain * m_ListenerSettings.m_GlobalGain, 0.0f, 1.0f) * MAX_VOLUME);
    const s32 right = (s32)(Clamp(source->RightGain * m_ListenerSettings.m_GlobalGain, 0.0f, 1.0f) * MAX_VOLUME);

    if (source->Looping || source->Streamed)
    {
        const CKDWORD loopBytes = LoopBytes(source);
        if (start == 0 || start >= loopBytes)
        {
            source->StartFrame = 0.0;
            ASND_SetInfiniteVoice(source->Voice, format, pitch, 0, source->Buffer, loopBytes, left, right);
        }
        else
        {
            ASND_SetVoice(source->Voice, format, pitch, 0, source->Buffer + start, loopBytes - start, left, right, LoopVoiceCallback);
            ASND_AddVoice(source->Voice, source->Buffer, loopBytes);
        }
    }
    else
    {
        // A whole sound plays to its end, padded with silence to the block size.
        ASND_SetVoice(source->Voice, format, pitch, 0, source->Buffer + start, AlignUp(source->Size - start), left, right, NULL);
    }

    source->LastTicks = 0;
}

void WiiSoundManager::ApplyVoiceParameters(WiiSoundSource *source)
{
    if (source->Voice < 0 || m_Voices[source->Voice] != source)
        return;
    UpdatePlayedFrames(source);
    const float globalGain = m_ListenerSettings.m_GlobalGain;
    ASND_ChangeVolumeVoice(source->Voice,
                           (s32)(Clamp(source->LeftGain * globalGain, 0.0f, 1.0f) * MAX_VOLUME),
                           (s32)(Clamp(source->RightGain * globalGain, 0.0f, 1.0f) * MAX_VOLUME));
    ASND_ChangePitchVoice(source->Voice,
                          (s32)Clamp((float)source->Format.nSamplesPerSec * source->Pitch, 1.0f, (float)MAX_PITCH));
}

// ---------------------------------------------------------------------------
// Playback

void WiiSoundManager::InternalPlay(void *source, CKBOOL loop)
{
    WiiSoundSource *wiiSource = (WiiSoundSource *)source;
    if (!wiiSource || !m_Initialized)
        return;

    ApplySettings(wiiSource);
    Apply3DSettings(wiiSource);

    // Resume a paused voice where it stopped.
    if (wiiSource->Paused && wiiSource->Voice >= 0 && m_Voices[wiiSource->Voice] == wiiSource &&
        ASND_StatusVoice(wiiSource->Voice) != SND_UNUSED && wiiSource->Looping == loop)
    {
        ASND_PauseVoice(wiiSource->Voice, 0);
        wiiSource->LastTicks = ASND_GetTickCounterVoice(wiiSource->Voice);
        wiiSource->Paused = FALSE;
        wiiSource->Playing = TRUE;
        ApplyVoiceParameters(wiiSource);
        return;
    }

    UpdatePlayedFrames(wiiSource);
    wiiSource->StartFrame = CurrentFrame(wiiSource);
    wiiSource->Looping = loop;
    StopVoice(wiiSource);
    wiiSource->Paused = FALSE;
    wiiSource->Playing = TRUE;
    StartVoice(wiiSource);
}

void WiiSoundManager::InternalPause(void *source)
{
    WiiSoundSource *wiiSource = (WiiSoundSource *)source;
    if (!wiiSource || !wiiSource->Playing || wiiSource->Paused)
        return;
    UpdatePlayedFrames(wiiSource);
    if (wiiSource->Voice >= 0)
        ASND_PauseVoice(wiiSource->Voice, 1);
    wiiSource->Paused = TRUE;
}

void WiiSoundManager::Play(CKWaveSound *ws, void *source, CKBOOL loop)
{
    WiiSoundSource *wiiSource = ResolveSource(ws, source);
    if (!wiiSource)
        return;
    if (ws)
        m_SoundsPlaying.AddIfNotHere(ws->GetID());
    InternalPlay(wiiSource, loop);
}

void WiiSoundManager::Pause(CKWaveSound *ws, void *source)
{
    (void)ws;
    InternalPause(source);
}

void WiiSoundManager::SetPlayPosition(void *source, int pos)
{
    WiiSoundSource *wiiSource = (WiiSoundSource *)source;
    if (!wiiSource || pos < 0)
        return;

    const CKBOOL wasRunning = wiiSource->Playing && !wiiSource->Paused && wiiSource->Voice >= 0;
    StopVoice(wiiSource);
    const CKDWORD bytes = (CKDWORD)pos < wiiSource->Size ? (CKDWORD)pos : wiiSource->Size;
    wiiSource->StartFrame = (double)(bytes / FrameBytes(wiiSource->Format));
    wiiSource->PlayedFrames = 0.0;
    if (wasRunning)
        StartVoice(wiiSource);
}

int WiiSoundManager::GetPlayPosition(void *source)
{
    WiiSoundSource *wiiSource = (WiiSoundSource *)source;
    if (!wiiSource)
        return 0;
    UpdatePlayedFrames(wiiSource);
    return (int)(CKDWORD)CurrentFrame(wiiSource) * (int)FrameBytes(wiiSource->Format);
}

CKBOOL WiiSoundManager::IsPlaying(void *source)
{
    WiiSoundSource *wiiSource = (WiiSoundSource *)source;
    if (!wiiSource || !wiiSource->Playing || wiiSource->Paused)
        return FALSE;

    if (wiiSource->Voice < 0 || m_Voices[wiiSource->Voice] != wiiSource)
    {
        // Lost its voice to a louder sound.
        wiiSource->Playing = FALSE;
        return FALSE;
    }

    if (!wiiSource->Looping && !wiiSource->Streamed && ASND_StatusVoice(wiiSource->Voice) == SND_UNUSED)
    {
        // Finished: free the voice and rewind for the next Play.
        StopVoice(wiiSource);
        wiiSource->Playing = FALSE;
        wiiSource->StartFrame = 0.0;
        wiiSource->PlayedFrames = 0.0;
        return FALSE;
    }
    return TRUE;
}

// ---------------------------------------------------------------------------
// Buffers

CKERROR WiiSoundManager::SetWaveFormat(void *source, CKWaveFormat &wf)
{
    WiiSoundSource *wiiSource = (WiiSoundSource *)source;
    if (!wiiSource)
        return CKERR_INVALIDPARAMETER;
    StopVoice(wiiSource);
    wiiSource->Format = wf;
    wiiSource->StartFrame = 0.0;
    wiiSource->PlayedFrames = 0.0;
    ApplySettings(wiiSource);
    Apply3DSettings(wiiSource);
    return CK_OK;
}

CKERROR WiiSoundManager::GetWaveFormat(void *source, CKWaveFormat &wf)
{
    WiiSoundSource *wiiSource = (WiiSoundSource *)source;
    if (!wiiSource)
        return CKERR_INVALIDPARAMETER;
    wf = wiiSource->Format;
    return CK_OK;
}

int WiiSoundManager::GetWaveSize(void *source)
{
    WiiSoundSource *wiiSource = (WiiSoundSource *)source;
    return wiiSource ? (int)wiiSource->Size : 0;
}

CKERROR WiiSoundManager::Lock(void *source, CKDWORD writeCursor, CKDWORD numBytes,
                              void **audioPtr1, CKDWORD *audioBytes1,
                              void **audioPtr2, CKDWORD *audioBytes2,
                              CK_WAVESOUND_LOCKMODE flags)
{
    WiiSoundSource *wiiSource = (WiiSoundSource *)source;
    if (!wiiSource || !audioPtr1 || !audioBytes1 || !wiiSource->Buffer || wiiSource->Size == 0)
        return CKERR_INVALIDPARAMETER;

    const CKDWORD size = wiiSource->Size;
    const CKDWORD requested = (flags & CK_WAVESOUND_LOCKENTIREBUFFER) ? size : numBytes;
    if (requested > size)
        return CKERR_INVALIDSIZE;

    CKDWORD start;
    if (flags & CK_WAVESOUND_LOCKENTIREBUFFER)
        start = 0;
    else if (flags & CK_WAVESOUND_LOCKFROMWRITE)
        start = wiiSource->WritePos;
    else
        start = writeCursor % size;

    const CKDWORD bytes1 = (requested < size - start) ? requested : size - start;
    const CKDWORD bytes2 = requested - bytes1;
    if (bytes2 > 0 && (!audioPtr2 || !audioBytes2))
        return CKERR_INVALIDPARAMETER;

    *audioPtr1 = wiiSource->Buffer + start;
    *audioBytes1 = bytes1;
    if (audioPtr2)
        *audioPtr2 = bytes2 ? wiiSource->Buffer : NULL;
    if (audioBytes2)
        *audioBytes2 = bytes2;
    return CK_OK;
}

CKERROR WiiSoundManager::Unlock(void *source, void *audioPtr1, CKDWORD numBytes1, void *audioPtr2, CKDWORD audioBytes2)
{
    WiiSoundSource *wiiSource = (WiiSoundSource *)source;
    if (!wiiSource)
        return CKERR_INVALIDPARAMETER;

    wiiSource->WritePos = (wiiSource->WritePos + numBytes1 + audioBytes2) % wiiSource->Size;

    // The DSP reads main memory directly; push the new samples out of the CPU cache.
    FlushRange(audioPtr1, numBytes1);
    FlushRange(audioPtr2, audioBytes2);
    return CK_OK;
}

void WiiSoundManager::SetType(void *source, CK_WAVESOUND_TYPE type)
{
    WiiSoundSource *wiiSource = (WiiSoundSource *)source;
    if (!wiiSource)
        return;
    wiiSource->Type = type;
    ApplySettings(wiiSource);
    Apply3DSettings(wiiSource);
}

CK_WAVESOUND_TYPE WiiSoundManager::GetType(void *source)
{
    WiiSoundSource *wiiSource = (WiiSoundSource *)source;
    return wiiSource ? wiiSource->Type : (CK_WAVESOUND_TYPE)0;
}

// ---------------------------------------------------------------------------
// Settings

void WiiSoundManager::UpdateSettings(void *source, CK_SOUNDMANAGER_CAPS options, CKWaveSoundSettings &settings, CKBOOL set)
{
    WiiSoundSource *wiiSource = (WiiSoundSource *)source;
    if (!wiiSource)
        return;

    CKWaveSoundSettings &current = wiiSource->Settings;
    if (set)
    {
        if (options & CK_WAVESOUND_SETTINGS_GAIN) current.m_Gain = settings.m_Gain;
        if (options & CK_WAVESOUND_SETTINGS_PITCH) current.m_Pitch = settings.m_Pitch;
        if (options & CK_WAVESOUND_SETTINGS_PAN) current.m_Pan = settings.m_Pan;
        if (options & CK_WAVESOUND_SETTINGS_PRIORITY) current.m_Priority = settings.m_Priority;
        if (options & CK_WAVESOUND_SETTINGS_EQUALIZATION) current.m_Eq = settings.m_Eq;
        ApplySettings(wiiSource);
        Apply3DSettings(wiiSource);
    }
    else
    {
        if (options & CK_WAVESOUND_SETTINGS_GAIN) settings.m_Gain = current.m_Gain;
        if (options & CK_WAVESOUND_SETTINGS_PITCH) settings.m_Pitch = current.m_Pitch;
        if (options & CK_WAVESOUND_SETTINGS_PAN) settings.m_Pan = current.m_Pan;
        if (options & CK_WAVESOUND_SETTINGS_PRIORITY) settings.m_Priority = current.m_Priority;
        if (options & CK_WAVESOUND_SETTINGS_EQUALIZATION) settings.m_Eq = current.m_Eq;
    }
}

void WiiSoundManager::Update3DSettings(void *source, CK_SOUNDMANAGER_CAPS options, CKWaveSound3DSettings &settings, CKBOOL set)
{
    WiiSoundSource *wiiSource = (WiiSoundSource *)source;
    if (!wiiSource)
        return;

    CKWaveSound3DSettings &current = wiiSource->Settings3D;
    if (set)
    {
        if (options & CK_WAVESOUND_3DSETTINGS_CONE)
        {
            current.m_InAngle = settings.m_InAngle;
            current.m_OutAngle = settings.m_OutAngle;
            current.m_OutsideGain = settings.m_OutsideGain;
        }
        if (options & CK_WAVESOUND_3DSETTINGS_MINMAXDISTANCE)
        {
            current.m_MinDistance = settings.m_MinDistance;
            current.m_MaxDistance = settings.m_MaxDistance;
        }
        if (options & CK_WAVESOUND_3DSETTINGS_POSITION) current.m_Position = settings.m_Position;
        if (options & CK_WAVESOUND_3DSETTINGS_VELOCITY) current.m_Velocity = settings.m_Velocity;
        if (options & CK_WAVESOUND_3DSETTINGS_ORIENTATION)
        {
            current.m_OrientationDir = settings.m_OrientationDir;
            current.m_OrientationUp = settings.m_OrientationUp;
        }
        if (options & CK_WAVESOUND_3DSETTINGS_HEADRELATIVE)
        {
            current.m_HeadRelative = settings.m_HeadRelative;
            current.m_MuteAfterMax = settings.m_MuteAfterMax;
        }
        Apply3DSettings(wiiSource);
    }
    else
    {
        if (options & CK_WAVESOUND_3DSETTINGS_CONE)
        {
            settings.m_InAngle = current.m_InAngle;
            settings.m_OutAngle = current.m_OutAngle;
            settings.m_OutsideGain = current.m_OutsideGain;
        }
        if (options & CK_WAVESOUND_3DSETTINGS_MINMAXDISTANCE)
        {
            settings.m_MinDistance = current.m_MinDistance;
            settings.m_MaxDistance = current.m_MaxDistance;
        }
        if (options & CK_WAVESOUND_3DSETTINGS_POSITION) settings.m_Position = current.m_Position;
        if (options & CK_WAVESOUND_3DSETTINGS_VELOCITY) settings.m_Velocity = current.m_Velocity;
        if (options & CK_WAVESOUND_3DSETTINGS_ORIENTATION)
        {
            settings.m_OrientationDir = current.m_OrientationDir;
            settings.m_OrientationUp = current.m_OrientationUp;
        }
        if (options & CK_WAVESOUND_3DSETTINGS_HEADRELATIVE)
        {
            settings.m_HeadRelative = current.m_HeadRelative;
            settings.m_MuteAfterMax = current.m_MuteAfterMax;
        }
    }
}

void WiiSoundManager::UpdateListenerSettings(CK_SOUNDMANAGER_CAPS options, CKListenerSettings &settings, CKBOOL set)
{
    if (set)
    {
        CKBOOL refresh = FALSE;
        if (options & CK_LISTENERSETTINGS_DISTANCE) { m_ListenerSettings.m_DistanceFactor = settings.m_DistanceFactor; refresh = TRUE; }
        if (options & CK_LISTENERSETTINGS_DOPPLER) { m_ListenerSettings.m_DopplerFactor = settings.m_DopplerFactor; refresh = TRUE; }
        if (options & CK_LISTENERSETTINGS_ROLLOFF) { m_ListenerSettings.m_RollOff = settings.m_RollOff; refresh = TRUE; }
        if (options & CK_LISTENERSETTINGS_GAIN) { m_ListenerSettings.m_GlobalGain = settings.m_GlobalGain; refresh = TRUE; }
        if (options & CK_LISTENERSETTINGS_PRIORITY) m_ListenerSettings.m_PriorityBias = settings.m_PriorityBias;
        if (options & CK_LISTENERSETTINGS_SOFTWARESOURCES) m_ListenerSettings.m_SoftwareSources = settings.m_SoftwareSources;
        if (refresh)
            Refresh3DSources();
    }
    else
    {
        if (options & CK_LISTENERSETTINGS_DISTANCE) settings.m_DistanceFactor = m_ListenerSettings.m_DistanceFactor;
        if (options & CK_LISTENERSETTINGS_DOPPLER) settings.m_DopplerFactor = m_ListenerSettings.m_DopplerFactor;
        if (options & CK_LISTENERSETTINGS_ROLLOFF) settings.m_RollOff = m_ListenerSettings.m_RollOff;
        if (options & CK_LISTENERSETTINGS_GAIN) settings.m_GlobalGain = m_ListenerSettings.m_GlobalGain;
        if (options & CK_LISTENERSETTINGS_PRIORITY) settings.m_PriorityBias = m_ListenerSettings.m_PriorityBias;
        if (options & CK_LISTENERSETTINGS_SOFTWARESOURCES) settings.m_SoftwareSources = m_ListenerSettings.m_SoftwareSources;
    }
}

// Background sounds: gain, pitch and stereo pan.
void WiiSoundManager::ApplySettings(WiiSoundSource *source)
{
    source->Pitch = source->Settings.m_Pitch > 0.0f ? source->Settings.m_Pitch : 1.0f;
    if (source->Type & CK_WAVESOUND_BACKGROUND)
    {
        const float pan = Clamp(source->Settings.m_Pan, -1.0f, 1.0f);
        source->LeftGain = source->Settings.m_Gain * (pan > 0.0f ? 1.0f - pan : 1.0f);
        source->RightGain = source->Settings.m_Gain * (pan < 0.0f ? 1.0f + pan : 1.0f);
    }
    else
    {
        source->LeftGain = source->Settings.m_Gain;
        source->RightGain = source->Settings.m_Gain;
    }
    ApplyVoiceParameters(source);
}

// 3D sounds: distance and cone attenuation, panning and doppler, as the desktop manager does.
void WiiSoundManager::Apply3DSettings(WiiSoundSource *source)
{
    if (source->Type & CK_WAVESOUND_BACKGROUND)
        return;

    const CKWaveSound3DSettings &settings = source->Settings3D;
    const CKListenerSettings &listener = m_ListenerSettings;

    VxVector right = m_ListenerRight, up = m_ListenerUp, forward = m_ListenerForward;
    NormalizeOrDefault(right, 1.0f, 0.0f, 0.0f);
    NormalizeOrDefault(up, 0.0f, 1.0f, 0.0f);
    NormalizeOrDefault(forward, 0.0f, 0.0f, 1.0f);

    VxVector relative, sourceVelocity, sourceDirection, listenerVelocity;
    if (settings.m_HeadRelative)
    {
        relative = settings.m_Position;
        sourceVelocity = settings.m_Velocity;
        sourceDirection = settings.m_OrientationDir;
        listenerVelocity.Set(0.0f, 0.0f, 0.0f);
    }
    else
    {
        const VxVector world = settings.m_Position - m_ListenerPosition;
        relative.Set(DotProduct(world, right), DotProduct(world, up), DotProduct(world, forward));
        sourceVelocity.Set(DotProduct(settings.m_Velocity, right), DotProduct(settings.m_Velocity, up),
                           DotProduct(settings.m_Velocity, forward));
        sourceDirection.Set(DotProduct(settings.m_OrientationDir, right), DotProduct(settings.m_OrientationDir, up),
                            DotProduct(settings.m_OrientationDir, forward));
        listenerVelocity.Set(DotProduct(m_ListenerVelocity, right), DotProduct(m_ListenerVelocity, up),
                             DotProduct(m_ListenerVelocity, forward));
    }

    const VxVector scaled = relative * listener.m_DistanceFactor;
    const float distance = scaled.Magnitude();
    float minDistance = settings.m_MinDistance < 0.000001f ? 0.000001f : settings.m_MinDistance;
    float maxDistance = settings.m_MaxDistance < minDistance ? minDistance : settings.m_MaxDistance;
    const float rolloff = listener.m_RollOff < 0.0f ? 0.0f : listener.m_RollOff;

    float attenuation;
    if (distance < minDistance || rolloff == 0.0f)
        attenuation = 1.0f;
    else if (distance > maxDistance && settings.m_MuteAfterMax)
        attenuation = 0.0f;
    else
    {
        const float clamped = distance > maxDistance ? maxDistance : distance;
        attenuation = Clamp(minDistance / (minDistance + rolloff * (clamped - minDistance)), 0.0f, 1.0f);
    }

    float coneGain = 1.0f;
    const float directionLength = sourceDirection.SquareMagnitude();
    if (directionLength > 0.000001f && distance > 0.000001f && settings.m_OutAngle < 360.0f)
    {
        VxVector coneDirection = sourceDirection * (1.0f / sqrtf(directionLength));
        VxVector toListener(-relative.x, -relative.y, -relative.z);
        toListener.Normalize();
        const float angle = acosf(Clamp(DotProduct(coneDirection, toListener), -1.0f, 1.0f)) * 180.0f / kPi;
        const float inAngle = settings.m_InAngle * 0.5f;
        float outAngle = settings.m_OutAngle * 0.5f;
        if (outAngle < inAngle)
            outAngle = inAngle;
        const float outsideGain = Clamp(settings.m_OutsideGain, 0.0f, 1.0f);
        if (angle >= outAngle)
            coneGain = outsideGain;
        else if (angle > inAngle && outAngle - inAngle > 0.000001f)
            coneGain = 1.0f - ((angle - inAngle) / (outAngle - inAngle)) * (1.0f - outsideGain);
    }

    float pan = 0.0f;
    VxVector toSource(0.0f, 0.0f, 1.0f);
    const float relativeLength = relative.SquareMagnitude();
    if (relativeLength > 0.000001f)
    {
        toSource = relative * (1.0f / sqrtf(relativeLength));
        pan = DirectionToPan(toSource);
    }
    float leftPan, rightPan;
    ConstantPowerPan(pan, leftPan, rightPan);

    float doppler = 1.0f;
    if (listener.m_DopplerFactor != 0.0f && relativeLength > 0.000001f)
    {
        const VxVector toListener(-toSource.x, -toSource.y, -toSource.z);
        const float sourceToward = DotProduct(sourceVelocity, toListener);
        const float listenerToward = settings.m_HeadRelative ? 0.0f : DotProduct(listenerVelocity, toSource);
        float speed = kSpeedOfSound;
        if (listener.m_DistanceFactor > 0.000001f)
            speed /= listener.m_DistanceFactor;
        float numerator = speed + listenerToward * listener.m_DopplerFactor;
        float denominator = speed - sourceToward * listener.m_DopplerFactor;
        if (denominator < speed * 0.1f) denominator = speed * 0.1f;
        if (numerator < speed * 0.1f) numerator = speed * 0.1f;
        doppler = Clamp(numerator / denominator, kMinDopplerRatio, kMaxDopplerRatio);
    }

    const float gain = source->Settings.m_Gain * attenuation * coneGain;
    // ASND pans per channel; the constant-power gains keep the centre at full level.
    source->LeftGain = gain * leftPan * 1.41421356f;
    source->RightGain = gain * rightPan * 1.41421356f;
    source->Pitch = (source->Settings.m_Pitch > 0.0f ? source->Settings.m_Pitch : 1.0f) * doppler;
    ApplyVoiceParameters(source);
}

void WiiSoundManager::Refresh3DSources()
{
    for (CK_ID *it = m_SoundsPlaying.Begin(); it != m_SoundsPlaying.End(); ++it)
    {
        CKWaveSound *ws = (CKWaveSound *)m_Context->GetObject(*it);
        if (ws && ws->m_Source)
        {
            WiiSoundSource *source = (WiiSoundSource *)ws->m_Source;
            ApplySettings(source);
            Apply3DSettings(source);
        }
    }
    for (SoundMinion **it = m_Minions.Begin(); it != m_Minions.End(); ++it)
    {
        if (*it && (*it)->m_Source)
        {
            WiiSoundSource *source = (WiiSoundSource *)(*it)->m_Source;
            ApplySettings(source);
            Apply3DSettings(source);
        }
    }
}

void WiiSoundManager::PositionSource(WiiSoundSource *source, CK3dEntity *ent, const VxVector &position,
                                     const VxVector &direction, VxVector &oldPosition, float deltaTime)
{
    VxVector pos = position;
    VxVector dir = direction;
    VxVector up(0.0f, 1.0f, 0.0f);
    if (ent)
    {
        ent->Transform(&pos, &position);
        ent->TransformVector(&dir, &direction);
        const VxVector baseUp = up;
        ent->TransformVector(&up, &baseUp);
    }

    source->Settings3D.m_Velocity = deltaTime > 0.0f ? (pos - oldPosition) / (deltaTime * 0.001f) : VxVector(0.0f, 0.0f, 0.0f);
    source->Settings3D.m_Position = pos;
    source->Settings3D.m_OrientationDir = dir;
    source->Settings3D.m_OrientationUp = up;
    Apply3DSettings(source);
    oldPosition = pos;
}

// ---------------------------------------------------------------------------
// Manager callbacks

CKERROR WiiSoundManager::OnCKInit()
{
    if (m_Context->GetStartOptions() & CK_CONFIG_DISABLEDSOUND)
        return CK_OK;

    ASND_Init();
    ASND_Pause(0);
    m_Initialized = TRUE;

    RegisterAttribute();

    // Sounds loaded before the manager started need their sources now.
    const int count = m_Context->GetObjectsCountByClassID(CKCID_WAVESOUND);
    CK_ID *ids = m_Context->GetObjectsListByClassID(CKCID_WAVESOUND);
    for (int i = 0; i < count; ++i)
    {
        CKWaveSound *ws = (CKWaveSound *)m_Context->GetObject(ids[i]);
        if (ws)
            ws->Recreate();
    }
    return CK_OK;
}

void WiiSoundManager::StopAllPlayingSounds()
{
    const int count = m_Context->GetObjectsCountByClassID(CKCID_WAVESOUND);
    CK_ID *ids = m_Context->GetObjectsListByClassID(CKCID_WAVESOUND);
    for (int i = 0; i < count; ++i)
    {
        CKWaveSound *ws = (CKWaveSound *)m_Context->GetObject(ids[i]);
        if (ws)
            ws->Release();
    }
}

CKERROR WiiSoundManager::OnCKEnd()
{
    if (!m_Initialized)
        return CK_OK;
    StopAllPlayingSounds();
    ReleaseMinions();
    ASND_Pause(1);
    ASND_End();
    memset(m_Voices, 0, sizeof(m_Voices));
    memset(g_Voices, 0, sizeof(g_Voices));
    m_Initialized = FALSE;
    return CK_OK;
}

CKERROR WiiSoundManager::PostClearAll()
{
    const CKERROR result = CKSoundManager::PostClearAll();
    m_SoundsPlaying.Clear();
    ReleaseMinions();
    RegisterAttribute();
    return result;
}

CKERROR WiiSoundManager::PostProcess()
{
    if (!m_Initialized)
        return CK_OK;

    const float deltaTime = m_Context->GetTimeManager()->GetLastDeltaTime();
    const VxVector oldPosition = m_ListenerPosition;
    const VxVector oldRight = m_ListenerRight;
    const VxVector oldUp = m_ListenerUp;
    const VxVector oldForward = m_ListenerForward;

    CK3dEntity *listener = GetListener();
    if (listener)
    {
        const VxMatrix &mat = listener->GetWorldMatrix();
        const VxVector position(mat[3][0], mat[3][1], mat[3][2]);
        if (deltaTime > 0.0f)
            m_ListenerVelocity = (position - m_ListenerPosition) / (deltaTime * 0.001f);
        else
            m_ListenerVelocity.Set(0.0f, 0.0f, 0.0f);
        m_ListenerRight.Set(mat[0][0], mat[0][1], mat[0][2]);
        m_ListenerUp.Set(mat[1][0], mat[1][1], mat[1][2]);
        m_ListenerForward.Set(mat[2][0], mat[2][1], mat[2][2]);
        m_ListenerPosition = position;
    }
    else
    {
        m_ListenerPosition.Set(0.0f, 0.0f, 0.0f);
        m_ListenerVelocity.Set(0.0f, 0.0f, 0.0f);
        m_ListenerRight.Set(1.0f, 0.0f, 0.0f);
        m_ListenerUp.Set(0.0f, 1.0f, 0.0f);
        m_ListenerForward.Set(0.0f, 0.0f, 1.0f);
    }

    const float threshold = 0.000001f;
    if ((m_ListenerPosition - oldPosition).SquareMagnitude() > threshold ||
        (m_ListenerRight - oldRight).SquareMagnitude() > threshold ||
        (m_ListenerUp - oldUp).SquareMagnitude() > threshold ||
        (m_ListenerForward - oldForward).SquareMagnitude() > threshold)
    {
        Refresh3DSources();
    }

    for (CK_ID *it = m_SoundsPlaying.Begin(); it != m_SoundsPlaying.End();)
    {
        CKWaveSound *ws = (CKWaveSound *)m_Context->GetObject(*it);
        if (ws && ws->IsPlaying())
        {
            if (ws->GetFileStreaming() && !(ws->GetState() & CK_WAVESOUND_STREAMFULLYLOADED))
                ws->WriteDataFromReader();
            ws->UpdateFade();
            if (!(ws->GetType() & CK_WAVESOUND_BACKGROUND))
                ws->UpdatePosition(deltaTime);
            ++it;
        }
        else
        {
            it = m_SoundsPlaying.Remove(it);
        }
    }

    for (SoundMinion **it = m_Minions.Begin(); it != m_Minions.End(); ++it)
    {
        if (!*it || !IsPlaying((*it)->m_Source) || !(*it)->m_Entity)
            continue;
        CK3dEntity *ent = (CK3dEntity *)m_Context->GetObject((*it)->m_Entity);
        if (ent)
            PositionSource((WiiSoundSource *)(*it)->m_Source, ent, (*it)->m_Position, (*it)->m_Direction,
                           (*it)->m_OldPosition, deltaTime);
    }
    ProcessMinions();
    return CK_OK;
}

CKERROR WiiSoundManager::OnCKReset()
{
    if (!m_Initialized)
        return CK_OK;
    for (CK_ID *it = m_SoundsPlaying.Begin(); it != m_SoundsPlaying.End(); ++it)
    {
        CKWaveSound *ws = (CKWaveSound *)m_Context->GetObject(*it);
        if (ws && ws->m_Source)
            ws->InternalStop();
    }
    m_SoundsPlaying.Clear();
    ReleaseMinions();
    return CK_OK;
}

CKERROR WiiSoundManager::OnCKPause()
{
    for (CK_ID *it = m_SoundsPlaying.Begin(); it != m_SoundsPlaying.End(); ++it)
    {
        CKWaveSound *ws = (CKWaveSound *)m_Context->GetObject(*it);
        if (ws)
            ws->Pause();
    }
    PauseMinions();
    return CK_OK;
}

CKERROR WiiSoundManager::OnCKPlay()
{
    for (CK_ID *it = m_SoundsPlaying.Begin(); it != m_SoundsPlaying.End(); ++it)
    {
        CKWaveSound *ws = (CKWaveSound *)m_Context->GetObject(*it);
        if (ws)
            ws->Resume();
    }
    ResumeMinions();
    return CK_OK;
}

CKERROR WiiSoundManager::PreLaunchScene(CKScene *oldScene, CKScene *newScene)
{
    (void)oldScene;
    if (!newScene)
        return CKERR_INVALIDPARAMETER;

    for (CK_ID *it = m_SoundsPlaying.Begin(); it != m_SoundsPlaying.End(); ++it)
    {
        CKWaveSound *ws = (CKWaveSound *)m_Context->GetObject(*it);
        if (ws && !ws->IsInScene(newScene))
            ws->Pause();
    }

    for (SoundMinion **it = m_Minions.Begin(); it != m_Minions.End();)
    {
        if (!*it)
        {
            it = m_Minions.Remove(it);
            continue;
        }
        CKSceneObject *original = (CKSceneObject *)m_Context->GetObject((*it)->m_OriginalSound);
        if (!original || !original->IsInScene(newScene))
        {
            Stop(NULL, (*it)->m_Source);
            ReleaseSource((*it)->m_Source);
            delete *it;
            it = m_Minions.Remove(it);
        }
        else
        {
            ++it;
        }
    }
    return CK_OK;
}

CKERROR WiiSoundManager::SequenceToBeDeleted(CK_ID *objids, int count)
{
    if (!objids || count <= 0)
        return CKERR_INVALIDPARAMETER;

    const CKERROR result = CKSoundManager::SequenceDeleted(objids, count);

    for (CK_ID *it = m_SoundsPlaying.Begin(); it != m_SoundsPlaying.End();)
    {
        CKWaveSound *ws = (CKWaveSound *)m_Context->GetObject(*it);
        if (!ws || ws->IsToBeDeleted())
        {
            if (ws)
                ws->Stop();
            it = m_SoundsPlaying.Remove(it);
        }
        else
        {
            ++it;
        }
    }

    for (SoundMinion **it = m_Minions.Begin(); it != m_Minions.End(); ++it)
    {
        if (!*it)
            continue;
        CKWaveSound *ws = (CKWaveSound *)m_Context->GetObject((*it)->m_OriginalSound);
        if (ws && ws->IsToBeDeleted())
            (*it)->m_OriginalSound = 0;
        CKObject *entity = m_Context->GetObject((*it)->m_Entity);
        if (!entity || entity->IsToBeDeleted())
            (*it)->m_Entity = 0;
    }
    return result;
}

// ---------------------------------------------------------------------------
// Plugin entry points

#ifdef CK_LIB
#define CreateNewManager    CreateNewSoundManager
#define RemoveManager       RemoveSoundManager
#define CKGetPluginInfo     CKGet_SoundManager_PluginInfo
#define g_PluginInfo        g_SoundManager_PluginInfo
#endif

CKPluginInfo g_PluginInfo;

static CKERROR CreateNewManager(CKContext *context)
{
    new WiiSoundManager(context);
    return CK_OK;
}

static CKERROR RemoveManager(CKContext *context)
{
    delete (WiiSoundManager *)context->GetManagerByGuid(SOUND_MANAGER_GUID);
    return CK_OK;
}

PLUGIN_EXPORT CKPluginInfo *CKGetPluginInfo(int index)
{
    (void)index;
    g_PluginInfo.m_Author = (CKSTRING)"Ballanced";
    g_PluginInfo.m_Description = (CKSTRING)"Wii DSP sound manager (ASND)";
    g_PluginInfo.m_Extension = (CKSTRING)"";
    g_PluginInfo.m_Type = CKPLUGIN_MANAGER_DLL;
    g_PluginInfo.m_Version = 0x000001;
    g_PluginInfo.m_InitInstanceFct = CreateNewManager;
    g_PluginInfo.m_ExitInstanceFct = RemoveManager;
    g_PluginInfo.m_GUID = SOUND_MANAGER_GUID;
    g_PluginInfo.m_Summary = (CKSTRING)"Wii Sound Manager";
    return &g_PluginInfo;
}
