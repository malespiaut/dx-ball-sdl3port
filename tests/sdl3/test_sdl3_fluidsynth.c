#define _DEFAULT_SOURCE /* setenv, unsetenv */
/* FluidSynth sink: a missing library leaves music silent without failing;
   a note-on produces audio. Skips without FluidSynth or a soundfont. */
#include "check.h"
#include "sdl3_midi.h"
#include <math.h>
#include <stdlib.h>
#ifdef _WIN32
#define setenv(name, value, overwrite) _putenv_s(name, value)
#define unsetenv(name) _putenv_s(name, "")
#endif

int main(void)
{
    static float stereo[2 * 4410];
    double energy = 0;
    int i;
    setenv("DXBALL_FLUIDSYNTH", "dxball-no-such-fluidsynth-library", 1);
    CHECK_EQ(dxball_sdl3_fluidsynth_start(), 0);
    CHECK(dxball_sdl3_midi_sink.message == NULL);
    unsetenv("DXBALL_FLUIDSYNTH");
    if (check_failures != 0) return CHECK_DONE();
    if (!dxball_sdl3_fluidsynth_start()) return 77;
    dxball_sdl3_midi_sink.message(0xc0);       /* program 0 on channel 0 */
    dxball_sdl3_midi_sink.message(0x7f3c90);   /* note 60, velocity 127 */
    dxball_sdl3_midi_sink.render(stereo, 4410);
    for (i = 0; i < 2 * 4410; ++i) energy += fabs(stereo[i]);
    CHECK(energy > 1.0);
    return CHECK_DONE();
}
