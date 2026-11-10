/* WinMM MIDI stream emulation for the Linux build: one stream, queued
   headers played in order, MOM_DONE callbacks when a header completes. */
#include "sdl3_midi.h"
#include "sdl3_sound.h"
#include <SDL3/SDL.h>
#include <math.h>
#include <string.h>

enum {
    MMSYSERR_ALLOCATED = 4, MMSYSERR_INVALHANDLE = 5, MMSYSERR_NOMEM = 7,
    MMSYSERR_INVALPARAM = 11, MIDIERR_UNPREPARED = 64, MIDIERR_STILLPLAYING = 65,
    MHDR_DONE = 1, MHDR_PREPARED = 2, MHDR_INQUEUE = 4, MOM_DONE = 0x3c9,
    MEVT_SHORTMSG = 0, MEVT_TEMPO = 1, QUEUE_SIZE = 64, STREAM = 1
};
#define MIDIPROP_SET 0x80000000UL
#define MIDIPROP_GET 0x40000000UL
#define MIDIPROP_TIMEDIV 1UL
#define MIDIPROP_TEMPO 2UL
#define MEVT_F_LONG 0x80000000UL

DxBallMidiSink dxball_sdl3_midi_sink;

static SDL_Mutex *state_lock;    /* sequencer state */
static SDL_Mutex *callback_lock; /* serializes callbacks with reset/close */
static SDL_AudioStream *output;
static struct {
    int open, paused, have_event;
    DxBallMidiCallback callback;
    size_t instance;
    DxBallUInt time_division, tempo, offset, event;
    double wait; /* samples until the loaded event */
    DxBallMidiHeader *queue[QUEUE_SIZE];
    unsigned head, count;
} stream;

static void locks(void)
{
    if (state_lock == NULL) state_lock = SDL_CreateMutex();
    if (callback_lock == NULL) callback_lock = SDL_CreateMutex();
}

static DxBallUInt read32(const DxBallByte *p)
{
    return (DxBallUInt)p[0] | (DxBallUInt)p[1] << 8 | (DxBallUInt)p[2] << 16 | (DxBallUInt)p[3] << 24;
}

static double samples_per_tick(void)
{
    DxBallUInt division = stream.time_division != 0 ? stream.time_division : 96;
    return (double)DXBALL_SDL3_AUDIO_RATE * stream.tempo / 1e6 / division;
}

static void silence(void)
{
    if (dxball_sdl3_midi_sink.silence != NULL) dxball_sdl3_midi_sink.silence();
}

/* Loads the next event header from the queue. Returns 0 when nothing is queued. */
static int load_event(void)
{
    const DxBallMidiHeader *header;
    if (stream.count == 0) return 0;
    header = stream.queue[stream.head];
    if (stream.offset + 12 > header->bytes_recorded) return 0; /* caller completes it */
    stream.wait += read32(header->data + stream.offset) * samples_per_tick();
    stream.event = read32(header->data + stream.offset + 8);
    stream.have_event = 1;
    return 1;
}

static DxBallMidiHeader *finish_header_if_done(void)
{
    DxBallMidiHeader *header;
    if (stream.count == 0 || stream.have_event) return NULL;
    header = stream.queue[stream.head];
    if (stream.offset + 12 <= header->bytes_recorded) return NULL;
    stream.head = (stream.head + 1) % QUEUE_SIZE;
    --stream.count;
    stream.offset = 0;
    header->flags = (header->flags & ~(DxBallUInt)MHDR_INQUEUE) | MHDR_DONE;
    return header;
}

static void execute_event(void)
{
    DxBallUInt event = stream.event, type = (event >> 24) & 0x3f;
    stream.offset += 12;
    if ((event & MEVT_F_LONG) != 0) stream.offset += ((event & 0xffffffUL) + 3) & ~3UL;
    else if (type == MEVT_SHORTMSG && dxball_sdl3_midi_sink.message != NULL)
        dxball_sdl3_midi_sink.message(event & 0xffffffUL);
    else if (type == MEVT_TEMPO && (event & 0xffffffUL) != 0)
        stream.tempo = event & 0xffffffUL;
    stream.have_event = 0;
}

/* Runs due events; returns a completed header whose callback is owed. */
static DxBallMidiHeader *run_due_events(void)
{
    DxBallMidiHeader *done;
    while (stream.open && !stream.paused) {
        if ((done = finish_header_if_done()) != NULL) return done;
        if (!stream.have_event && !load_event()) break;
        if (stream.wait > 0) break;
        execute_event();
    }
    return NULL;
}

static void notify(DxBallMidiHeader *header)
{
    DxBallMidiCallback callback = stream.callback;
    size_t instance = stream.instance;
    if (callback != NULL) callback(STREAM, MOM_DONE, instance, header, 0);
}

void dxball_sdl3_midi_advance(float *stereo, int frames)
{
    int stalled = 0; /* completions without time passing (empty headers) */
    locks();
    while (frames > 0) {
        DxBallMidiHeader *done;
        int count = frames;
        SDL_LockMutex(state_lock);
        done = stalled > QUEUE_SIZE ? NULL : run_due_events();
        if (done == NULL) {
            if (stream.open && !stream.paused && stream.have_event) {
                double due = ceil(stream.wait);
                if (due < count) count = due < 1 ? 1 : (int)due;
                stream.wait -= count;
            }
        }
        SDL_UnlockMutex(state_lock);
        if (done != NULL) {
            /* The game re-submits from its callback, which takes state_lock. */
            SDL_LockMutex(callback_lock);
            notify(done);
            SDL_UnlockMutex(callback_lock);
            ++stalled;
            continue;
        }
        stalled = 0;
        if (dxball_sdl3_midi_sink.render != NULL) dxball_sdl3_midi_sink.render(stereo, count);
        stereo += 2 * count;
        frames -= count;
    }
}

static DxBallUInt DXBALL_DDCALL stream_open(DxBallMidiHandle *handle, DxBallUInt *device,
    DxBallUInt count, DxBallMidiCallback callback, size_t instance, DxBallUInt flags)
{
    (void)device; (void)count; (void)flags;
    locks();
    SDL_LockMutex(state_lock);
    if (stream.open) { SDL_UnlockMutex(state_lock); return MMSYSERR_ALLOCATED; }
    memset(&stream, 0, sizeof(stream));
    stream.open = 1;
    stream.paused = 1; /* a new stream waits for midiStreamRestart */
    stream.callback = callback;
    stream.instance = instance;
    stream.tempo = 500000;
    stream.time_division = 96;
    SDL_UnlockMutex(state_lock);
    *handle = STREAM;
    return 0;
}

static DxBallUInt DXBALL_DDCALL stream_property(DxBallMidiHandle handle,
    DxBallMidiProperty *property, DxBallUInt flags)
{
    DxBallUInt *field;
    if (handle != STREAM || !stream.open) return MMSYSERR_INVALHANDLE;
    if ((flags & MIDIPROP_TIMEDIV) != 0) field = &stream.time_division;
    else if ((flags & MIDIPROP_TEMPO) != 0) field = &stream.tempo;
    else return MMSYSERR_INVALPARAM;
    SDL_LockMutex(state_lock);
    if ((flags & MIDIPROP_SET) != 0 && property->value != 0) *field = property->value;
    else if ((flags & MIDIPROP_GET) != 0) property->value = *field;
    SDL_UnlockMutex(state_lock);
    return 0;
}

/* Header sizes arrive as the i386 value (64); the record itself is complete. */
static DxBallUInt DXBALL_DDCALL prepare_header(DxBallMidiHandle handle, DxBallMidiHeader *header,
                                               DxBallUInt size)
{
    (void)handle; (void)size;
    header->flags |= MHDR_PREPARED;
    return 0;
}

static DxBallUInt DXBALL_DDCALL unprepare_header(DxBallMidiHandle handle, DxBallMidiHeader *header,
                                                 DxBallUInt size)
{
    (void)handle; (void)size;
    if ((header->flags & MHDR_INQUEUE) != 0) return MIDIERR_STILLPLAYING;
    header->flags &= ~(DxBallUInt)MHDR_PREPARED;
    return 0;
}

static DxBallUInt DXBALL_DDCALL stream_out(DxBallMidiHandle handle, DxBallMidiHeader *header,
                                           DxBallUInt size)
{
    (void)size;
    if (handle != STREAM || !stream.open) return MMSYSERR_INVALHANDLE;
    if ((header->flags & MHDR_PREPARED) == 0) return MIDIERR_UNPREPARED;
    SDL_LockMutex(state_lock);
    if (stream.count == QUEUE_SIZE) { SDL_UnlockMutex(state_lock); return MMSYSERR_NOMEM; }
    header->flags = (header->flags & ~(DxBallUInt)MHDR_DONE) | MHDR_INQUEUE;
    stream.queue[(stream.head + stream.count++) % QUEUE_SIZE] = header;
    SDL_UnlockMutex(state_lock);
    return 0;
}

static DxBallUInt set_paused(DxBallMidiHandle handle, int paused)
{
    if (handle != STREAM || !stream.open) return MMSYSERR_INVALHANDLE;
    SDL_LockMutex(state_lock);
    stream.paused = paused;
    if (paused) silence();
    SDL_UnlockMutex(state_lock);
    return 0;
}
static DxBallUInt DXBALL_DDCALL stream_restart(DxBallMidiHandle handle) { return set_paused(handle, 0); }
static DxBallUInt DXBALL_DDCALL stream_pause(DxBallMidiHandle handle) { return set_paused(handle, 1); }

/* midiOutReset: stop, silence and return every queued header as done. */
static DxBallUInt DXBALL_DDCALL out_reset(DxBallMidiHandle handle)
{
    DxBallMidiHeader *returned[QUEUE_SIZE];
    unsigned count, i;
    if (handle != STREAM || !stream.open) return MMSYSERR_INVALHANDLE;
    SDL_LockMutex(callback_lock);
    SDL_LockMutex(state_lock);
    count = stream.count;
    for (i = 0; i < count; ++i) {
        returned[i] = stream.queue[(stream.head + i) % QUEUE_SIZE];
        returned[i]->flags = (returned[i]->flags & ~(DxBallUInt)MHDR_INQUEUE) | MHDR_DONE;
    }
    stream.head = stream.count = 0;
    stream.offset = 0; stream.have_event = 0; stream.wait = 0;
    silence();
    SDL_UnlockMutex(state_lock);
    for (i = 0; i < count; ++i) notify(returned[i]);
    SDL_UnlockMutex(callback_lock);
    return 0;
}

static DxBallUInt DXBALL_DDCALL stream_close(DxBallMidiHandle handle)
{
    if (handle != STREAM || !stream.open) return MMSYSERR_INVALHANDLE;
    SDL_LockMutex(callback_lock);
    SDL_LockMutex(state_lock);
    if (stream.count != 0) {
        SDL_UnlockMutex(state_lock);
        SDL_UnlockMutex(callback_lock);
        return MIDIERR_STILLPLAYING;
    }
    stream.open = 0;
    silence();
    SDL_UnlockMutex(state_lock);
    SDL_UnlockMutex(callback_lock);
    return 0;
}

static void SDLCALL feed(void *user, SDL_AudioStream *audio, int needed, int total)
{
    float chunk[2 * 512];
    (void)user; (void)total;
    while (needed >= (int)(2 * sizeof(float))) {
        int frames = needed / (int)(2 * sizeof(float)), i;
        if (frames > 512) frames = 512;
        memset(chunk, 0, sizeof(chunk));
        dxball_sdl3_midi_advance(chunk, frames);
        for (i = 0; i < 2 * frames; ++i)
            chunk[i] = chunk[i] > 1.0f ? 1.0f : chunk[i] < -1.0f ? -1.0f : chunk[i];
        SDL_PutAudioStreamData(audio, chunk, frames * (int)(2 * sizeof(float)));
        needed -= frames * (int)(2 * sizeof(float));
    }
}

int dxball_sdl3_midi_open_output(void)
{
    SDL_AudioSpec spec;
    locks();
    if (output != NULL) return 1;
    spec.format = SDL_AUDIO_F32; spec.channels = 2; spec.freq = DXBALL_SDL3_AUDIO_RATE;
    output = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, feed, NULL);
    if (output == NULL) return 0;
    SDL_ResumeAudioStreamDevice(output);
    return 1;
}

void dxball_bind_sdl3_midi(void)
{
    locks();
    dxball_midi_stream_open = stream_open;
    dxball_midi_stream_property = stream_property;
    dxball_midi_prepare_header = prepare_header;
    dxball_midi_stream_out = stream_out;
    dxball_midi_stream_restart = stream_restart;
    dxball_midi_stream_pause = stream_pause;
    dxball_midi_out_reset = out_reset;
    dxball_midi_unprepare_header = unprepare_header;
    dxball_midi_stream_close = stream_close;
}
