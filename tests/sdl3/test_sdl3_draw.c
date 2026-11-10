/* Software DirectDraw used by the Linux build, checked pixel by pixel. */
#include "check.h"
#include "sdl3_draw.h"
#include "device.h"
#include <string.h>

#define INVALID_RECT ((DxBallInt)0x88760096UL)

static int presents, vblanks;
static const DxBallByte *presented;
static DxBallPaletteEntry presented_color;

static void present(const DxBallByte *pixels, DxBallInt pitch, const DxBallPaletteEntry palette[256])
{
    (void)pitch;
    ++presents;
    presented = pixels;
    presented_color = palette[pixels[0]];
}
static void vblank(void) { ++vblanks; }

static DxBallDDSurface *offscreen(DxBallDDraw *draw, DxBallUInt width, DxBallUInt height)
{
    DxBallSurfaceDesc desc;
    DxBallDDSurface *surface = NULL;
    memset(&desc, 0, sizeof(desc));
    desc.size = 108; desc.flags = 7; desc.caps = 0x840; desc.width = width; desc.height = height;
    CHECK_EQ(draw->vtable->create_surface(draw, &desc, &surface, NULL), 0);
    return surface;
}

static DxBallByte *pixels_of(DxBallDDSurface *surface, DxBallInt *pitch)
{
    DxBallSurfaceDesc desc;
    desc.size = 108;
    CHECK_EQ(surface->vtable->get_desc(surface, &desc), 0); /* no Unlock: keeps primary clean */
    *pitch = desc.pitch;
    return desc.pixels;
}

static void fill(DxBallDDSurface *surface, DxBallInt left, DxBallInt top, DxBallInt right,
                 DxBallInt bottom, DxBallUInt color)
{
    DxBallRect rect;
    DxBallColorFillFx fx;
    rect.left = left; rect.top = top; rect.right = right; rect.bottom = bottom;
    memset(&fx, 0, sizeof(fx));
    fx.size = 100; fx.fill_color = color;
    CHECK_EQ(surface->vtable->blt(surface, &rect, NULL, NULL, 0x400, &fx), 0);
}

static void test_device(DxBallDDraw *draw)
{
    DxBallDrawCaps caps;
    memset(&caps, 0xff, sizeof(caps));
    caps.size = 316;
    CHECK_EQ(((DxBallSetDisplayMode)draw->vtable->slots_7_to_21[14])(draw, 640, 480, 8), 0);
    CHECK(((DxBallSetDisplayMode)draw->vtable->slots_7_to_21[14])(draw, 800, 600, 8) != 0);
    CHECK_EQ(((DxBallSetCooperativeLevel)draw->vtable->slots_7_to_21[13])(draw, 1, 0x11), 0);
    CHECK_EQ(((DxBallGetCaps)draw->vtable->slots_7_to_21[4])(draw, &caps, NULL), 0);
    CHECK_EQ(caps.size, 316);
    CHECK_EQ(caps.caps & 0x02000000UL, 0);       /* hardware present: full flip path */
    CHECK(caps.free_video_memory >= 310000UL);   /* no low-memory fallback */
}

static void test_descriptions(DxBallDDraw *draw)
{
    DxBallDDSurface *surface = offscreen(draw, 13, 5);
    DxBallSurfaceDesc desc;
    DxBallRect rect = { 2, 3, 4, 5 };
    DxBallByte *base;
    memset(&desc, 0xee, sizeof(desc));
    desc.size = 108;
    CHECK_EQ(surface->vtable->get_desc(surface, &desc), 0);
    CHECK_EQ(desc.size, 108);
    CHECK_EQ(desc.width, 13);
    CHECK_EQ(desc.height, 5);
    CHECK(desc.pitch >= 13);
    CHECK_EQ(desc.pixel_format[3], 8);
    base = desc.pixels;
    CHECK(base != NULL && base[0] == 0);
    CHECK_EQ(surface->vtable->lock(surface, &rect, &desc, 1, NULL), 0);
    CHECK(desc.pixels == base + 3 * desc.pitch + 2);
    rect.right = 14;
    CHECK_EQ(surface->vtable->lock(surface, &rect, &desc, 1, NULL), INVALID_RECT);
    CHECK_EQ(surface->vtable->get_blt_status(surface, 1), 0);
    CHECK_EQ(surface->vtable->release(surface), 0);
}

static void test_blits(DxBallDDraw *draw)
{
    DxBallDDSurface *target = offscreen(draw, 8, 8), *sprite = offscreen(draw, 2, 2);
    DxBallColorKey key = { 0, 0 };
    DxBallRect from = { 0, 0, 2, 2 }, to = { 4, 4, 8, 8 }, row = { 0, 0, 7, 1 };
    DxBallInt pitch, sprite_pitch, x, y, good;
    DxBallByte *out = pixels_of(target, &pitch), *in = pixels_of(sprite, &sprite_pitch);

    /* Colour fill touches the rect only. */
    fill(target, 1, 1, 3, 2, 0x1234);
    CHECK_EQ(out[pitch + 1], 0x34);
    CHECK_EQ(out[pitch + 2], 0x34);
    CHECK_EQ(out[pitch + 3], 0);
    CHECK_EQ(out[0], 0);

    /* Sprite: 5 . / . 7 with key 0 */
    in[0] = 5; in[1] = 0; in[sprite_pitch] = 0; in[sprite_pitch + 1] = 7;
    CHECK_EQ(sprite->vtable->set_color_key(sprite, 8, &key), 0);
    fill(target, 0, 0, 8, 8, 9);

    /* Keyed BltFast keeps destination where the source is transparent. */
    CHECK_EQ(target->vtable->blt_fast(target, 0, 0, sprite, &from, 0x11), 0);
    CHECK_EQ(out[0], 5); CHECK_EQ(out[1], 9); CHECK_EQ(out[pitch], 9); CHECK_EQ(out[pitch + 1], 7);
    /* Unkeyed BltFast copies zeros too. */
    CHECK_EQ(target->vtable->blt_fast(target, 2, 0, sprite, &from, 0x10), 0);
    CHECK_EQ(out[3], 0);
    /* BltFast does not clip: out of bounds fails and draws nothing. */
    out[7 * pitch + 7] = 9;
    CHECK_EQ(target->vtable->blt_fast(target, 7, 7, sprite, &from, 0x10), INVALID_RECT);
    CHECK_EQ(out[7 * pitch + 7], 9);

    /* Blt stretches 2x2 to 4x4 by nearest sample, honouring the key. */
    CHECK_EQ(target->vtable->blt(target, &to, sprite, &from, 0x1008000, NULL), 0);
    good = 1;
    for (y = 0; y < 4; ++y)
        for (x = 0; x < 4; ++x) {
            DxBallByte expected = y < 2 ? (x < 2 ? 5 : 9) : (x < 2 ? 9 : 7);
            good &= out[(4 + y) * pitch + 4 + x] == expected;
        }
    CHECK(good);

    /* Overlapping copy within one surface reads the original pixels. */
    for (x = 0; x < 8; ++x) out[x] = (DxBallByte)(x + 1);
    CHECK_EQ(target->vtable->blt_fast(target, 1, 0, target, &row, 0x10), 0);
    good = out[0] == 1;
    for (x = 1; x < 8; ++x) good &= out[x] == x;
    CHECK(good);

    target->vtable->release(target);
    sprite->vtable->release(sprite);
}

static void test_flip_and_present(DxBallDDraw *draw)
{
    DxBallSurfaceDesc desc;
    DxBallDDSurface *front = NULL, *back = NULL;
    DxBallDDPalette *palette = NULL;
    DxBallPaletteEntry entries[256];
    DxBallUInt caps = 4;
    DxBallInt pitch;
    DxBallByte *front_pixels, *back_pixels;
    dxball_sdl3_draw_present = present;
    dxball_sdl3_draw_vblank = vblank;

    memset(&desc, 0, sizeof(desc));
    desc.size = 108; desc.flags = 0x21; desc.caps = 0x218; desc.ancillary[0] = 1;
    CHECK_EQ(draw->vtable->create_surface(draw, &desc, &front, NULL), 0);
    CHECK_EQ(((DxBallGetAttachedSurface)front->vtable->slot_12)(front, &caps, &back), 0);
    CHECK(back != NULL);
    memset(entries, 0, sizeof(entries));
    entries[1].red = 10; entries[2].green = 20;
    CHECK_EQ(draw->vtable->create_palette(draw, 4, entries, &palette, NULL), 0);
    CHECK_EQ(front->vtable->set_palette(front, palette), 0);

    fill(front, 0, 0, 640, 480, 2);
    fill(back, 0, 0, 640, 480, 1);
    presents = vblanks = 0;
    CHECK_EQ(front->vtable->flip(front, NULL, 0), 0);
    CHECK_EQ(presents, 1);
    CHECK_EQ(vblanks, 1);
    front_pixels = pixels_of(front, &pitch);
    back_pixels = pixels_of(back, &pitch);
    CHECK_EQ(front_pixels[0], 1);   /* former back buffer is visible */
    CHECK_EQ(back_pixels[0], 2);    /* former front buffer is drawable */
    CHECK(presented == front_pixels);
    CHECK_EQ(presented_color.red, 10);

    /* Palette changes and direct primary writes are shown at the next flush. */
    presents = 0;
    dxball_sdl3_draw_flush();
    CHECK_EQ(presents, 0);
    entries[1].red = 99;
    CHECK_EQ(palette->vtable->set_entries(palette, 0, 1, 1, entries + 1), 0);
    CHECK_EQ(draw->vtable->wait_vertical_blank(draw, 1, NULL), 0);
    CHECK_EQ(presents, 1);
    CHECK_EQ(presented_color.red, 99);
    fill(back, 0, 0, 1, 1, 3);
    dxball_sdl3_draw_flush();
    CHECK_EQ(presents, 1);          /* back buffer writes are not visible */
    fill(front, 0, 0, 1, 1, 2);
    dxball_sdl3_draw_flush();
    CHECK_EQ(presents, 2);
    CHECK_EQ(presented_color.green, 20);

    CHECK_EQ(((DxBallReleasePalette)palette->vtable->slots_0_to_5[2])(palette), 0);
    back->vtable->release(back);
    CHECK_EQ(front->vtable->release(front), 0);
    /* A primary can be created again after release (device reset). */
    CHECK_EQ(draw->vtable->create_surface(draw, &desc, &front, NULL), 0);
    front->vtable->release(front);
}

int main(void)
{
    DxBallDDraw *draw = NULL;
    CHECK_EQ(dxball_sdl3_direct_draw_create(NULL, &draw, NULL), 0);
    CHECK(draw != NULL);
    test_device(draw);
    test_descriptions(draw);
    test_blits(draw);
    test_flip_and_present(draw);
    return CHECK_DONE();
}
