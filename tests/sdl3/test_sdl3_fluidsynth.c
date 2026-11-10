/* FluidSynth sink: a note-on produces audio. Skips without a soundfont. */
#include "check.h"
#include "sdl3_midi.h"
#include <math.h>

int main(void)
{
    static float stereo[2 * 4410];
    double energy = 0;
    int i;
    if (!dxball_sdl3_fluidsynth_start()) return 77;
    dxball_sdl3_midi_sink.message(0xc0);       /* program 0 on channel 0 */
    dxball_sdl3_midi_sink.message(0x7f3c90);   /* note 60, velocity 127 */
    dxball_sdl3_midi_sink.render(stereo, 4410);
    for (i = 0; i < 2 * 4410; ++i) energy += fabs(stereo[i]);
    CHECK(energy > 1.0);
    return CHECK_DONE();
}
