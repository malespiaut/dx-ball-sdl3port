#ifndef DXBALL_SDL3_MIDI_H
#define DXBALL_SDL3_MIDI_H

#include "midi.h"

/* WinMM MIDI stream emulation for the Linux build. Stream buffers hold
   MIDIEVENT records (delta ticks, stream id, event); the sequencer runs at
   the audio sample clock and sends channel messages to the synth sink. */
typedef struct DxBallMidiSink {
    void (*message)(DxBallUInt short_message);
    void (*render)(float *stereo, int frames); /* writes; NULL leaves silence */
    void (*silence)(void);                     /* all notes and sounds off */
} DxBallMidiSink;

extern DxBallMidiSink dxball_sdl3_midi_sink;
/* Bind the dxball_midi_stream_* import cells. */
void dxball_bind_sdl3_midi(void);
/* Advance the sequencer by frames at DXBALL_SDL3_AUDIO_RATE and render. */
void dxball_sdl3_midi_advance(float *stereo, int frames);
/* Open an SDL3 audio stream that calls dxball_sdl3_midi_advance. */
int dxball_sdl3_midi_open_output(void);
/* Loads FluidSynth ($DXBALL_FLUIDSYNTH, else the game folder, else the
   system) and installs it as the sink; returns 0 when it is unavailable. */
int dxball_sdl3_fluidsynth_start(void);

#endif
