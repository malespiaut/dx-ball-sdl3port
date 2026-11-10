/* DirectSound 1 for the Linux build: static PCM buffers mixed in software
   with DirectSound's volume (hundredths of a decibel), pan and frequency. */
#include "sdl3_sound.h"
#include <SDL3/SDL.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define DS_INVALID_PARAM ((DxBallInt)0x80070057UL)
#define DS_BAD_FORMAT ((DxBallInt)0x88780064UL)
#define DS_OUT_OF_MEMORY ((DxBallInt)0x8007000EUL)
#define DS_NO_DRIVER ((DxBallInt)0x88780078UL)

enum {
    DSBCAPS_PRIMARYBUFFER = 1, DSBPLAY_LOOPING = 1, DSBLOCK_ENTIREBUFFER = 2,
    DSBSTATUS_PLAYING = 1, DSBSTATUS_LOOPING = 4
};

typedef struct Buffer {
    DxBallSoundBuffer com;
    DxBallUInt references;
    DxBallByte *data;
    DxBallUInt bytes, frames, rate, frequency;
    int channels, bits, block, primary, playing, looping;
    DxBallInt volume, pan;
    double position; /* in source frames */
    struct Buffer *next;
} Buffer;

static DxBallSoundDeviceVTable device_methods;
static DxBallSoundBufferVTable buffer_methods;
static DxBallSoundDevice device = { &device_methods };
static Buffer *buffers;
static SDL_Mutex *mixer_lock;
static SDL_AudioStream *output;

static void lock_mixer(void) { SDL_LockMutex(mixer_lock); }
static void unlock_mixer(void) { SDL_UnlockMutex(mixer_lock); }

static unsigned read16(const DxBallByte *p) { return (unsigned)p[0] | (unsigned)p[1] << 8; }
static DxBallUInt read32(const DxBallByte *p)
{ return (DxBallUInt)read16(p) | (DxBallUInt)read16(p + 2) << 16; }

static float sample(const Buffer *buffer, DxBallUInt frame, int channel)
{
    const DxBallByte *at;
    if (frame >= buffer->frames) {
        if (!buffer->looping) return 0;
        frame %= buffer->frames;
    }
    at = buffer->data + (size_t)frame * (size_t)buffer->block +
         (size_t)(channel < buffer->channels ? channel : 0) * (size_t)(buffer->bits / 8);
    return buffer->bits == 8 ? ((float)at[0] - 128.0f) / 128.0f
                             : (float)(short)read16(at) / 32768.0f;
}

static float decibels(DxBallInt hundredths) { return powf(10.0f, (float)hundredths / 2000.0f); }

void dxball_sdl3_sound_mix(float *stereo, int frames)
{
    Buffer *buffer;
    lock_mixer();
    for (buffer = buffers; buffer != NULL; buffer = buffer->next) {
        double step;
        float gain, left, right;
        int i;
        if (!buffer->playing || buffer->primary || buffer->frames == 0) continue;
        step = (double)(buffer->frequency != 0 ? buffer->frequency : buffer->rate) /
               DXBALL_SDL3_AUDIO_RATE;
        gain = decibels(buffer->volume);
        left = gain * (buffer->pan > 0 ? decibels(-buffer->pan) : 1.0f);
        right = gain * (buffer->pan < 0 ? decibels(buffer->pan) : 1.0f);
        for (i = 0; i < frames && buffer->playing; ++i) {
            DxBallUInt frame = (DxBallUInt)buffer->position;
            float t = (float)(buffer->position - frame);
            stereo[2 * i] += left * (sample(buffer, frame, 0) * (1 - t) + sample(buffer, frame + 1, 0) * t);
            stereo[2 * i + 1] += right * (sample(buffer, frame, 1) * (1 - t) + sample(buffer, frame + 1, 1) * t);
            buffer->position += step;
            if (buffer->position >= buffer->frames) {
                if (buffer->looping) {
                    buffer->position = fmod(buffer->position, buffer->frames);
                } else {
                    buffer->playing = 0; /* a finished static buffer rewinds */
                    buffer->position = 0;
                }
            }
        }
    }
    unlock_mixer();
}

/* ---- Buffers ---- */

static DxBallUInt DXBALL_DDCALL buffer_release(DxBallSoundBuffer *com)
{
    Buffer *buffer = (Buffer *)com, **link;
    lock_mixer();
    if (--buffer->references != 0) { unlock_mixer(); return buffer->references; }
    for (link = &buffers; *link != NULL; link = &(*link)->next)
        if (*link == buffer) { *link = buffer->next; break; }
    unlock_mixer();
    free(buffer->data);
    free(buffer);
    return 0;
}

static DxBallInt DXBALL_DDCALL get_volume(DxBallSoundBuffer *com, DxBallInt *volume)
{ *volume = ((Buffer *)com)->volume; return 0; }
static DxBallInt DXBALL_DDCALL get_pan(DxBallSoundBuffer *com, DxBallInt *pan)
{ *pan = ((Buffer *)com)->pan; return 0; }
static DxBallInt DXBALL_DDCALL get_frequency(DxBallSoundBuffer *com, DxBallUInt *frequency)
{
    Buffer *buffer = (Buffer *)com;
    *frequency = buffer->frequency != 0 ? buffer->frequency : buffer->rate;
    return 0;
}
static DxBallInt DXBALL_DDCALL get_status(DxBallSoundBuffer *com, DxBallUInt *status)
{
    Buffer *buffer = (Buffer *)com;
    lock_mixer();
    *status = (buffer->playing ? DSBSTATUS_PLAYING : 0) |
              (buffer->playing && buffer->looping ? DSBSTATUS_LOOPING : 0);
    unlock_mixer();
    return 0;
}

static DxBallInt DXBALL_DDCALL buffer_lock(DxBallSoundBuffer *com, DxBallUInt offset, DxBallUInt bytes,
    void **first, DxBallUInt *first_bytes, void **second, DxBallUInt *second_bytes, DxBallUInt flags)
{
    Buffer *buffer = (Buffer *)com;
    if ((flags & DSBLOCK_ENTIREBUFFER) != 0) { offset = 0; bytes = buffer->bytes; }
    if (buffer->primary || offset >= buffer->bytes || bytes > buffer->bytes) return DS_INVALID_PARAM;
    *first = buffer->data + offset;
    *first_bytes = bytes < buffer->bytes - offset ? bytes : buffer->bytes - offset;
    if (second != NULL) *second = bytes > *first_bytes ? buffer->data : NULL;
    if (second_bytes != NULL) *second_bytes = bytes - *first_bytes;
    return 0;
}

static DxBallInt DXBALL_DDCALL buffer_unlock(DxBallSoundBuffer *com, void *first, DxBallUInt first_bytes,
                                             void *second, DxBallUInt second_bytes)
{ (void)com; (void)first; (void)first_bytes; (void)second; (void)second_bytes; return 0; }

static DxBallInt DXBALL_DDCALL play(DxBallSoundBuffer *com, DxBallUInt reserved, DxBallUInt priority,
                                    DxBallUInt flags)
{
    Buffer *buffer = (Buffer *)com;
    (void)reserved; (void)priority;
    lock_mixer();
    buffer->playing = 1;
    buffer->looping = (flags & DSBPLAY_LOOPING) != 0;
    unlock_mixer();
    return 0;
}

static DxBallInt DXBALL_DDCALL set_position(DxBallSoundBuffer *com, DxBallUInt position)
{
    Buffer *buffer = (Buffer *)com;
    if (!buffer->primary && position >= buffer->bytes) return DS_INVALID_PARAM;
    lock_mixer();
    if (!buffer->primary) buffer->position = position / (DxBallUInt)buffer->block;
    unlock_mixer();
    return 0;
}

static DxBallInt DXBALL_DDCALL set_volume(DxBallSoundBuffer *com, DxBallInt volume)
{
    if (volume > 0 || volume < -10000) return DS_INVALID_PARAM;
    lock_mixer(); ((Buffer *)com)->volume = volume; unlock_mixer();
    return 0;
}
static DxBallInt DXBALL_DDCALL set_pan(DxBallSoundBuffer *com, DxBallInt pan)
{
    if (pan > 10000 || pan < -10000) return DS_INVALID_PARAM;
    lock_mixer(); ((Buffer *)com)->pan = pan; unlock_mixer();
    return 0;
}
static DxBallInt DXBALL_DDCALL set_frequency(DxBallSoundBuffer *com, DxBallUInt frequency)
{
    if (frequency != 0 && (frequency < 100 || frequency > 100000)) return DS_INVALID_PARAM;
    lock_mixer(); ((Buffer *)com)->frequency = frequency; unlock_mixer();
    return 0;
}
static DxBallInt DXBALL_DDCALL stop(DxBallSoundBuffer *com)
{
    lock_mixer(); ((Buffer *)com)->playing = 0; unlock_mixer();
    return 0;
}
static DxBallInt DXBALL_DDCALL restore(DxBallSoundBuffer *com) { (void)com; return 0; }

/* ---- Device ---- */

/* The descriptor's size field holds the i386 value (20); the format is a
   WAVEFORMATEX inside the caller's file and is copied here. */
static DxBallInt DXBALL_DDCALL create_buffer(DxBallSoundDevice *com, const DxBallSoundBufferDesc *desc,
                                             DxBallSoundBuffer **output, void *outer)
{
    Buffer *buffer;
    (void)com; (void)outer;
    *output = NULL;
    buffer = (Buffer *)calloc(1, sizeof(*buffer));
    if (buffer == NULL) return DS_OUT_OF_MEMORY;
    buffer->com.vtable = &buffer_methods;
    buffer->references = 1;
    if ((desc->flags & DSBCAPS_PRIMARYBUFFER) != 0) {
        buffer->primary = 1;
    } else {
        const DxBallByte *format = (const DxBallByte *)desc->format;
        if (format == NULL || read16(format) != 1 || desc->bytes == 0) { free(buffer); return DS_BAD_FORMAT; }
        buffer->channels = (int)read16(format + 2);
        buffer->rate = read32(format + 4);
        buffer->block = (int)read16(format + 12);
        buffer->bits = (int)read16(format + 14);
        if ((buffer->channels != 1 && buffer->channels != 2) || (buffer->bits != 8 && buffer->bits != 16) ||
            buffer->block != buffer->channels * buffer->bits / 8 || buffer->rate == 0) {
            free(buffer);
            return DS_BAD_FORMAT;
        }
        buffer->bytes = desc->bytes;
        buffer->frames = desc->bytes / (DxBallUInt)buffer->block;
        buffer->data = (DxBallByte *)calloc(1, desc->bytes);
        if (buffer->data == NULL) { free(buffer); return DS_OUT_OF_MEMORY; }
    }
    lock_mixer();
    buffer->next = buffers;
    buffers = buffer;
    unlock_mixer();
    *output = &buffer->com;
    return 0;
}

static DxBallInt DXBALL_DDCALL set_cooperative_level(DxBallSoundDevice *com, DxBallHandle window,
                                                     DxBallUInt level)
{ (void)com; (void)window; (void)level; return 0; }

static DxBallUInt DXBALL_DDCALL device_release(DxBallSoundDevice *com)
{
    (void)com;
    if (output != NULL) { SDL_DestroyAudioStream(output); output = NULL; }
    return 0;
}

DxBallSoundDevice *dxball_sdl3_sound_device(void)
{
    if (mixer_lock == NULL) mixer_lock = SDL_CreateMutex();
    device_methods.release = device_release;
    device_methods.create_buffer = create_buffer;
    device_methods.set_cooperative_level = set_cooperative_level;
    buffer_methods.release = buffer_release;
    buffer_methods.get_volume = get_volume;
    buffer_methods.get_pan = get_pan;
    buffer_methods.get_frequency = get_frequency;
    buffer_methods.get_status = get_status;
    buffer_methods.lock = buffer_lock;
    buffer_methods.play = play;
    buffer_methods.set_current_position = set_position;
    buffer_methods.set_volume = set_volume;
    buffer_methods.set_pan = set_pan;
    buffer_methods.set_frequency = set_frequency;
    buffer_methods.stop = stop;
    buffer_methods.unlock = buffer_unlock;
    buffer_methods.restore = restore;
    return &device;
}

static void SDLCALL feed(void *user, SDL_AudioStream *stream, int needed, int total)
{
    float chunk[2 * 512];
    (void)user; (void)total;
    while (needed > 0) {
        int frames = needed / (int)(2 * sizeof(float)), i;
        if (frames <= 0) break;
        if (frames > 512) frames = 512;
        memset(chunk, 0, sizeof(chunk));
        dxball_sdl3_sound_mix(chunk, frames);
        for (i = 0; i < 2 * frames; ++i)
            chunk[i] = chunk[i] > 1.0f ? 1.0f : chunk[i] < -1.0f ? -1.0f : chunk[i];
        SDL_PutAudioStreamData(stream, chunk, frames * (int)(2 * sizeof(float)));
        needed -= frames * (int)(2 * sizeof(float));
    }
}

DxBallInt DXBALL_DDCALL dxball_sdl3_sound_create(void *guid, DxBallSoundDevice **result, void *outer)
{
    SDL_AudioSpec spec;
    (void)guid; (void)outer;
    *result = NULL;
    dxball_sdl3_sound_device(); /* the mixer lock must exist before feed runs */
    spec.format = SDL_AUDIO_F32; spec.channels = 2; spec.freq = DXBALL_SDL3_AUDIO_RATE;
    if (output == NULL) {
        output = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, feed, NULL);
        if (output == NULL) return DS_NO_DRIVER;
        SDL_ResumeAudioStreamDevice(output);
    }
    *result = &device;
    return 0;
}
