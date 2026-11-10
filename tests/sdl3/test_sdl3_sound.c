/* Software DirectSound mixer used by the Linux build. */
#include "check.h"
#include "sdl3_sound.h"
#include <SDL3/SDL.h>
#include <math.h>
#include <string.h>

#define NEAR(actual, expected) CHECK(fabsf((float)(actual) - (float)(expected)) < 1e-3f)

static DxBallByte format_8bit_mono[16] = { 1, 0, 1, 0, 0x44, 0xac, 0, 0, 0x44, 0xac, 0, 0, 1, 0, 8, 0 };
static DxBallByte format_16bit_stereo[16] = { 1, 0, 2, 0, 0x44, 0xac, 0, 0, 0x10, 0xb1, 2, 0, 4, 0, 16, 0 };

static DxBallSoundBuffer *make(DxBallSoundDevice *device, const void *format,
                               const void *pcm, DxBallUInt bytes)
{
    DxBallSoundBufferDesc desc;
    DxBallSoundBuffer *buffer = NULL;
    void *first, *second;
    DxBallUInt first_bytes, second_bytes;
    memset(&desc, 0, sizeof(desc));
    desc.size = 20; desc.flags = 0xe2; desc.bytes = bytes; desc.format = format;
    CHECK_EQ(device->vtable->create_buffer(device, &desc, &buffer, NULL), 0);
    CHECK_EQ(buffer->vtable->lock(buffer, 0, bytes, &first, &first_bytes, &second, &second_bytes, 0), 0);
    CHECK_EQ(first_bytes, bytes);
    CHECK_EQ(second_bytes, 0);
    memcpy(first, pcm, bytes);
    buffer->vtable->unlock(buffer, first, first_bytes, second, second_bytes);
    return buffer;
}

static DxBallUInt status_of(DxBallSoundBuffer *buffer)
{
    DxBallUInt status = 99;
    CHECK_EQ(buffer->vtable->get_status(buffer, &status), 0);
    return status;
}

int main(void)
{
    DxBallSoundDevice *device = dxball_sdl3_sound_device();
    DxBallSoundBufferDesc desc;
    DxBallSoundBuffer *primary = NULL, *mono, *stereo;
    static const DxBallByte mono_pcm[4] = { 192, 64, 128, 255 };
    static const short stereo_pcm[4] = { 16384, -16384, 8192, 0 };
    float out[16];
    DxBallUInt frequency;
    DxBallInt value;

    /* Primary buffer: accepted, plays, holds no data. */
    memset(&desc, 0, sizeof(desc));
    desc.size = 20; desc.flags = 1;
    CHECK_EQ(device->vtable->create_buffer(device, &desc, &primary, NULL), 0);
    CHECK_EQ(primary->vtable->play(primary, 0, 0, 1), 0);
    desc.flags = 0xe2; desc.bytes = 4; desc.format = NULL;
    CHECK(device->vtable->create_buffer(device, &desc, &mono, NULL) != 0);

    mono = make(device, format_8bit_mono, mono_pcm, sizeof(mono_pcm));
    CHECK_EQ(mono->vtable->get_frequency(mono, &frequency), 0);
    CHECK_EQ(frequency, 44100);
    CHECK_EQ(mono->vtable->get_volume(mono, &value), 0);
    CHECK_EQ(value, 0);
    CHECK_EQ(status_of(mono), 0);

    /* Not playing: silence. */
    memset(out, 0, sizeof(out));
    dxball_sdl3_sound_mix(out, 4);
    NEAR(out[0], 0);

    /* 8-bit unsigned mono at the output rate, centre pan, full volume. */
    CHECK_EQ(mono->vtable->play(mono, 0, 0, 0), 0);
    CHECK_EQ(status_of(mono), 1);
    memset(out, 0, sizeof(out));
    dxball_sdl3_sound_mix(out, 6);
    NEAR(out[0], 0.5f); NEAR(out[1], 0.5f);
    NEAR(out[2], -0.5f); NEAR(out[3], -0.5f);
    NEAR(out[4], 0); NEAR(out[6], 127.0f / 128.0f);
    NEAR(out[8], 0); NEAR(out[10], 0);
    /* A finished static buffer stops and rewinds. */
    CHECK_EQ(status_of(mono), 0);
    CHECK_EQ(mono->vtable->play(mono, 0, 0, 0), 0);
    memset(out, 0, sizeof(out));
    dxball_sdl3_sound_mix(out, 1);
    NEAR(out[0], 0.5f);
    mono->vtable->stop(mono);
    CHECK_EQ(mono->vtable->set_current_position(mono, 0), 0);

    /* Volume -600 (6 dB down) and full right pan silence the left channel. */
    CHECK_EQ(mono->vtable->set_volume(mono, -600), 0);
    CHECK_EQ(mono->vtable->set_pan(mono, 10000), 0);
    CHECK(mono->vtable->set_pan(mono, 10001) != 0);
    CHECK_EQ(mono->vtable->play(mono, 0, 0, 0), 0);
    memset(out, 0, sizeof(out));
    dxball_sdl3_sound_mix(out, 1);
    NEAR(out[0], 0.5f * powf(10.0f, -5.3f));
    NEAR(out[1], 0.5f * powf(10.0f, -0.3f));
    mono->vtable->stop(mono);
    mono->vtable->set_current_position(mono, 0);
    mono->vtable->set_volume(mono, 0);
    mono->vtable->set_pan(mono, 0);

    /* Double frequency skips every other frame; looping wraps around. */
    CHECK_EQ(mono->vtable->set_frequency(mono, 88200), 0);
    CHECK_EQ(mono->vtable->play(mono, 0, 0, 1), 0);
    CHECK_EQ(status_of(mono), 5);
    memset(out, 0, sizeof(out));
    dxball_sdl3_sound_mix(out, 3);
    NEAR(out[0], 0.5f); NEAR(out[2], 0); NEAR(out[4], 0.5f);
    mono->vtable->stop(mono);
    CHECK_EQ(status_of(mono), 0);

    /* 16-bit signed stereo keeps channels apart. */
    stereo = make(device, format_16bit_stereo, stereo_pcm, sizeof(stereo_pcm));
    stereo->vtable->play(stereo, 0, 0, 0);
    memset(out, 0, sizeof(out));
    dxball_sdl3_sound_mix(out, 2);
    NEAR(out[0], 0.5f); NEAR(out[1], -0.5f);
    NEAR(out[2], 0.25f); NEAR(out[3], 0);

    CHECK_EQ(stereo->vtable->release(stereo), 0);
    CHECK_EQ(mono->vtable->release(mono), 0);
    CHECK_EQ(primary->vtable->release(primary), 0);
    memset(out, 0, sizeof(out));
    dxball_sdl3_sound_mix(out, 2);
    NEAR(out[0], 0);

    /* The factory opens a real output stream (dummy driver under CTest). */
    if (SDL_Init(SDL_INIT_AUDIO)) {
        DxBallSoundDevice *opened = NULL;
        CHECK_EQ(dxball_sdl3_sound_create(NULL, &opened, NULL), 0);
        CHECK(opened == device);
        CHECK_EQ(opened->vtable->release(opened), 0);
        SDL_Quit();
    }
    return CHECK_DONE();
}
