/* FluidSynth sink for the SDL3 MIDI stream emulation. FluidSynth is loaded
   when the game starts, so a system without it still runs the game, with
   silent music. Without FluidSynth headers at build time music is silent. */
#include "sdl3_midi.h"
#include "sdl3_sound.h"
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef DXBALL_HAVE_FLUIDSYNTH
#include <fluidsynth.h>

#if defined(_WIN32)
#define FLUIDSYNTH_LIBRARY "libfluidsynth-3.dll"
#elif defined(__APPLE__)
#define FLUIDSYNTH_LIBRARY "libfluidsynth.3.dylib"
#else
#define FLUIDSYNTH_LIBRARY "libfluidsynth.so.3"
#endif

/* Every FluidSynth function the sink calls, resolved from the library. */
#define FLUIDSYNTH_FUNCTIONS(X) X(fluid_synth_noteoff) X(fluid_synth_noteon) \
    X(fluid_synth_key_pressure) X(fluid_synth_cc) X(fluid_synth_program_change) \
    X(fluid_synth_channel_pressure) X(fluid_synth_pitch_bend) X(fluid_synth_write_float) \
    X(fluid_synth_all_sounds_off) X(fluid_audio_driver_register) X(fluid_set_log_function) \
    X(new_fluid_settings) X(fluid_settings_setnum) X(new_fluid_synth) \
    X(fluid_synth_sfload) X(delete_fluid_synth)
#define DECLARE(name) static __typeof__(&name) p_##name;
FLUIDSYNTH_FUNCTIONS(DECLARE)
#undef DECLARE

static fluid_synth_t *synth;

/* The game's own folder first (Windows, and macOS libs/), then the system. */
static int load_library(void)
{
    static const char *const folders[] = { "", "libs/" };
    const char *override = getenv("DXBALL_FLUIDSYNTH");
    const char *base = SDL_GetBasePath();
    SDL_SharedObject *library = NULL;
    char path[4096];
    size_t i;
    if (override != NULL) library = SDL_LoadObject(override);
    for (i = 0; override == NULL && library == NULL && base != NULL && i < 2; ++i) {
        SDL_snprintf(path, sizeof(path), "%s%s%s", base, folders[i], FLUIDSYNTH_LIBRARY);
        library = SDL_LoadObject(path);
    }
    if (override == NULL && library == NULL) library = SDL_LoadObject(FLUIDSYNTH_LIBRARY);
    if (library == NULL) return 0;
#define RESOLVE(name) p_##name = (__typeof__(&name))SDL_LoadFunction(library, #name); \
    if (p_##name == NULL) { SDL_UnloadObject(library); return 0; }
    FLUIDSYNTH_FUNCTIONS(RESOLVE)
#undef RESOLVE
    return 1;
}

static void message(DxBallUInt value)
{
    int status = (int)(value & 0xf0), channel = (int)(value & 0x0f);
    int first = (int)(value >> 8 & 0x7f), second = (int)(value >> 16 & 0x7f);
    switch (status) {
    case 0x80: p_fluid_synth_noteoff(synth, channel, first); break;
    case 0x90: p_fluid_synth_noteon(synth, channel, first, second); break;
    case 0xa0: p_fluid_synth_key_pressure(synth, channel, first, second); break;
    case 0xb0: p_fluid_synth_cc(synth, channel, first, second); break;
    case 0xc0: p_fluid_synth_program_change(synth, channel, first); break;
    case 0xd0: p_fluid_synth_channel_pressure(synth, channel, first); break;
    case 0xe0: p_fluid_synth_pitch_bend(synth, channel, second << 7 | first); break;
    default: break;
    }
}

static void render(float *stereo, int frames)
{
    p_fluid_synth_write_float(synth, frames, stereo, 0, 2, stereo, 1, 2);
}

static void silence(void) { p_fluid_synth_all_sounds_off(synth, -1); }

int dxball_sdl3_fluidsynth_start(void)
{
    /* soundfont.sf2 in the game folder first: Windows and macOS have no
       system General MIDI soundfont. */
    static const char *const defaults[] = { "soundfont.sf2",
        "/usr/share/soundfonts/FluidR3_GM.sf2", "/usr/share/soundfonts/default.sf2",
        "/usr/share/sounds/sf2/FluidR3_GM.sf2", "/usr/share/sounds/sf2/default-GM.sf2", NULL };
    const char *chosen = getenv("DXBALL_SOUNDFONT");
    fluid_settings_t *settings;
    int i, loaded = FLUID_FAILED;
    static const char *no_drivers[] = { NULL };
    if (!load_library()) {
        fprintf(stderr, "dxball: FluidSynth (%s) not found; music is silent.\n", FLUIDSYNTH_LIBRARY);
        return 0;
    }
    /* SDL3 plays the samples; skip FluidSynth's own audio driver probing. */
    p_fluid_audio_driver_register(no_drivers);
    p_fluid_set_log_function(FLUID_WARN, NULL, NULL);
    p_fluid_set_log_function(FLUID_INFO, NULL, NULL);
    settings = p_new_fluid_settings();
    if (settings == NULL) return 0;
    p_fluid_settings_setnum(settings, "synth.sample-rate", DXBALL_SDL3_AUDIO_RATE);
    synth = p_new_fluid_synth(settings);
    if (synth == NULL) return 0;
    if (chosen != NULL) loaded = p_fluid_synth_sfload(synth, chosen, 1);
    /* Only existing candidates: FluidSynth logs an error for each missing one. */
    for (i = 0; loaded == FLUID_FAILED && chosen == NULL && defaults[i] != NULL; ++i) {
        FILE *probe = fopen(defaults[i], "rb");
        if (probe == NULL) continue;
        fclose(probe);
        loaded = p_fluid_synth_sfload(synth, defaults[i], 1);
    }
    if (loaded == FLUID_FAILED) {
        fprintf(stderr, "dxball: no General MIDI soundfont found; put one in the game folder as "
                        "soundfont.sf2 or set DXBALL_SOUNDFONT. Music is silent.\n");
        p_delete_fluid_synth(synth);
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
