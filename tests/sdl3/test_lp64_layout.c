/* Layouts the Linux adapter reads or writes on behalf of recovered code.
   Fixed-width records must not grow on LP64; pointer-bearing records may,
   and their callers' 32-bit size literals must then never be trusted. */
#include "check.h"
#include "platform.h"
#include "sound.h"
#include "midi.h"
#include "startup.h"
#include <stddef.h>

#define SLOT(type, field) ((long long)(offsetof(type, field) / sizeof(void (*)(void))))

int main(void)
{
    CHECK_EQ(sizeof(DxBallInt), 4);
    CHECK_EQ(sizeof(DxBallUInt), 4);
    CHECK_EQ(sizeof(DxBallHandle), sizeof(void *));
    CHECK_EQ(sizeof(DxBallSurface), sizeof(void *));
    CHECK_EQ(sizeof(DxBallMidiHandle), sizeof(void *));

    /* Fixed-width records: identical on i386 and LP64. */
    CHECK_EQ(sizeof(DxBallColorFillFx), 100);
    CHECK_EQ(offsetof(DxBallColorFillFx, fill_color), 80);
    CHECK_EQ(sizeof(DxBallDrawCaps), 316);
    CHECK_EQ(offsetof(DxBallDrawCaps, caps), 4);
    CHECK_EQ(offsetof(DxBallDrawCaps, free_video_memory), 64);
    CHECK_EQ(sizeof(DxBallScoreRecord), 44);
    CHECK_EQ(sizeof(DxBallMidiProperty), 8);
    CHECK_EQ(sizeof(DxBallPaletteEntry), 4);
    CHECK_EQ(sizeof(DxBallColorKey), 8);

    /* Pointer-bearing records: fields before the pointer keep i386 offsets. */
    CHECK_EQ(offsetof(DxBallSurfaceDesc, pitch), 16);
    CHECK_EQ(offsetof(DxBallSurfaceDesc, ancillary), 20);
    CHECK_EQ(offsetof(DxBallSurfaceDesc, pixels) % sizeof(void *), 0);
    CHECK(sizeof(DxBallSurfaceDesc) >= 108);
    CHECK(sizeof(DxBallSoundBufferDesc) >= 20);
    CHECK(sizeof(DxBallMidiHeader) >= 64);
    CHECK_EQ(offsetof(DxBallMidiHeader, data), 0);

    /* COM slot indices of the methods the game calls. */
    CHECK_EQ(SLOT(DxBallDDSurfaceVTable, release), 2);
    CHECK_EQ(SLOT(DxBallDDSurfaceVTable, blt), 5);
    CHECK_EQ(SLOT(DxBallDDSurfaceVTable, blt_fast), 7);
    CHECK_EQ(SLOT(DxBallDDSurfaceVTable, flip), 11);
    CHECK_EQ(SLOT(DxBallDDSurfaceVTable, slot_12), 12);
    CHECK_EQ(SLOT(DxBallDDSurfaceVTable, get_blt_status), 13);
    CHECK_EQ(SLOT(DxBallDDSurfaceVTable, get_desc), 22);
    CHECK_EQ(SLOT(DxBallDDSurfaceVTable, lock), 25);
    CHECK_EQ(SLOT(DxBallDDSurfaceVTable, restore), 27);
    CHECK_EQ(SLOT(DxBallDDSurfaceVTable, set_clipper), 28);
    CHECK_EQ(SLOT(DxBallDDSurfaceVTable, set_color_key), 29);
    CHECK_EQ(SLOT(DxBallDDSurfaceVTable, set_palette), 31);
    CHECK_EQ(SLOT(DxBallDDSurfaceVTable, unlock), 32);
    CHECK_EQ(sizeof(DxBallDDSurfaceVTable) / sizeof(void (*)(void)), 33);
    CHECK_EQ(SLOT(DxBallDDrawVTable, create_palette), 5);
    CHECK_EQ(SLOT(DxBallDDrawVTable, create_surface), 6);
    CHECK_EQ(SLOT(DxBallDDrawVTable, slots_7_to_21) + 4, 11);   /* GetCaps */
    CHECK_EQ(SLOT(DxBallDDrawVTable, slots_7_to_21) + 13, 20);  /* SetCooperativeLevel */
    CHECK_EQ(SLOT(DxBallDDrawVTable, slots_7_to_21) + 14, 21);  /* SetDisplayMode */
    CHECK_EQ(SLOT(DxBallDDrawVTable, wait_vertical_blank), 22);
    CHECK_EQ(SLOT(DxBallDDPaletteVTable, set_entries), 6);
    CHECK_EQ(SLOT(DxBallDDClipperVTable, set_window), 8);
    CHECK_EQ(SLOT(DxBallSoundDeviceVTable, create_buffer), 3);
    CHECK_EQ(SLOT(DxBallSoundDeviceVTable, set_cooperative_level), 6);
    CHECK_EQ(SLOT(DxBallSoundBufferVTable, get_volume), 6);
    CHECK_EQ(SLOT(DxBallSoundBufferVTable, get_status), 9);
    CHECK_EQ(SLOT(DxBallSoundBufferVTable, lock), 11);
    CHECK_EQ(SLOT(DxBallSoundBufferVTable, play), 12);
    CHECK_EQ(SLOT(DxBallSoundBufferVTable, set_volume), 15);
    CHECK_EQ(SLOT(DxBallSoundBufferVTable, stop), 18);
    CHECK_EQ(SLOT(DxBallSoundBufferVTable, restore), 20);
    return CHECK_DONE();
}
