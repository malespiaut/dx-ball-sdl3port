/* FluidSynth sink for the Linux MIDI stream emulation. Without FluidSynth
   at build time, music is silent and the game otherwise runs unchanged. */
#include "sdl3_midi.h"
#include "sdl3_sound.h"
#include <stdio.h>
#include <stdlib.h>

#ifdef DXBALL_HAVE_FLUIDSYNTH
#include <fluidsynth.h>

static fluid_synth_t *synth;

static void message(DxBallUInt value)
{
    int status = (int)(value & 0xf0), channel = (int)(value & 0x0f);
    int first = (int)(value >> 8 & 0x7f), second = (int)(value >> 16 & 0x7f);
    switch (status) {
    case 0x80: fluid_synth_noteoff(synth, channel, first); break;
    case 0x90: fluid_synth_noteon(synth, channel, first, second); break;
    case 0xa0: fluid_synth_key_pressure(synth, channel, first, second); break;
    case 0xb0: fluid_synth_cc(synth, channel, first, second); break;
    case 0xc0: fluid_synth_program_change(synth, channel, first); break;
    case 0xd0: fluid_synth_channel_pressure(synth, channel, first); break;
    case 0xe0: fluid_synth_pitch_bend(synth, channel, second << 7 | first); break;
    default: break;
    }
}

static void render(float *stereo, int frames)
{
    fluid_synth_write_float(synth, frames, stereo, 0, 2, stereo, 1, 2);
}

static void silence(void) { fluid_synth_all_sounds_off(synth, -1); }

int dxball_sdl3_fluidsynth_start(void)
{
    static const char *const defaults[] = {
        "/usr/share/soundfonts/FluidR3_GM.sf2", "/usr/share/soundfonts/default.sf2",
        "/usr/share/sounds/sf2/FluidR3_GM.sf2", "/usr/share/sounds/sf2/default-GM.sf2", NULL };
    const char *chosen = getenv("DXBALL_SOUNDFONT");
    fluid_settings_t *settings;
    int i, loaded = FLUID_FAILED;
    static const char *no_drivers[] = { NULL };
    /* SDL3 plays the samples; skip FluidSynth's own audio driver probing. */
    fluid_audio_driver_register(no_drivers);
    fluid_set_log_function(FLUID_WARN, NULL, NULL);
    fluid_set_log_function(FLUID_INFO, NULL, NULL);
    settings = new_fluid_settings();
    if (settings == NULL) return 0;
    fluid_settings_setnum(settings, "synth.sample-rate", DXBALL_SDL3_AUDIO_RATE);
    synth = new_fluid_synth(settings);
    if (synth == NULL) return 0;
    if (chosen != NULL) loaded = fluid_synth_sfload(synth, chosen, 1);
    for (i = 0; loaded == FLUID_FAILED && chosen == NULL && defaults[i] != NULL; ++i)
        loaded = fluid_synth_sfload(synth, defaults[i], 1);
    if (loaded == FLUID_FAILED) {
        fprintf(stderr, "dxball: no General MIDI soundfont found; set DXBALL_SOUNDFONT. Music is silent.\n");
        delete_fluid_synth(synth);
        synth = NULL;
        return 0;
    }
    dxball_sdl3_midi_sink.message = message;
    dxball_sdl3_midi_sink.render = render;
    dxball_sdl3_midi_sink.silence = silence;
    return 1;
}
#else
int dxball_sdl3_fluidsynth_start(void)
{
    fprintf(stderr, "dxball: built without FluidSynth; music is silent.\n");
    return 0;
}
#endif
