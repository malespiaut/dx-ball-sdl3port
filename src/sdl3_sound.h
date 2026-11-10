#ifndef DXBALL_SDL3_SOUND_H
#define DXBALL_SDL3_SOUND_H

#include "sound.h"

enum { DXBALL_SDL3_AUDIO_RATE = 44100 };

/* DirectSoundCreate for the Linux build: the shared mixer device, played
   through its own SDL3 audio stream. */
DxBallInt DXBALL_DDCALL dxball_sdl3_sound_create(void *guid, DxBallSoundDevice **device,
                                                void *outer);
/* The same device without audio output, for tests. */
DxBallSoundDevice *dxball_sdl3_sound_device(void);
/* Adds the playing buffers to interleaved stereo at DXBALL_SDL3_AUDIO_RATE. */
void dxball_sdl3_sound_mix(float *stereo, int frames);

#endif
