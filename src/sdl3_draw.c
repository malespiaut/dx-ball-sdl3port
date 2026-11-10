/* Software DirectDraw 1 for the Linux build. Only the methods and flags the
   recovered game uses are implemented; other slots stay NULL. Surfaces are
   never lost, so Restore/IsLost always succeed. */
#include "sdl3_draw.h"
#include "device.h"
#include <stdlib.h>
#include <string.h>

#define DD_INVALID_RECT ((DxBallInt)0x88760096UL)
#define DD_INVALID_PARAMS ((DxBallInt)0x80070057UL)
#define DD_OUT_OF_MEMORY ((DxBallInt)0x8007000EUL)
#define DD_NOT_FOUND ((DxBallInt)0x887600FFUL)
#define DD_UNSUPPORTED_MODE ((DxBallInt)0x8876024EUL)

enum {
    DDSD_CAPS = 1, DDSD_HEIGHT = 2, DDSD_WIDTH = 4, DDSD_PITCH = 8, DDSD_BACKBUFFERCOUNT = 0x20,
    DDSD_PIXELFORMAT = 0x1000, DDSCAPS_BACKBUFFER = 4, DDSCAPS_FLIP = 0x10,
    DDSCAPS_FRONTBUFFER = 0x20, DDSCAPS_PRIMARY = 0x200, DDSCAPS_SYSTEMMEMORY = 0x800,
    DDBLT_COLORFILL = 0x400, DDBLT_KEYSRC = 0x8000, DDBLTFAST_SRCCOLORKEY = 1,
    DDCKEY_SRCBLT = 8, SCREEN_WIDTH = 640, SCREEN_HEIGHT = 480, MAX_SIDE = 4096
};

typedef struct Palette {
    DxBallDDPalette com;
    DxBallUInt references;
    DxBallPaletteEntry entries[256];
} Palette;

typedef struct Surface {
    DxBallDDSurface com;
    DxBallUInt references, caps;
    DxBallInt width, height, pitch;
    DxBallByte *pixels;
    DxBallInt keyed;
    DxBallByte key;
    Palette *palette;
    struct Surface *next; /* flip chain: primary -> back buffers */
} Surface;

typedef struct Clipper {
    DxBallDDClipper com;
    DxBallUInt references;
} Clipper;

void (*dxball_sdl3_draw_present)(const DxBallByte *, DxBallInt, const DxBallPaletteEntry[256]);
void (*dxball_sdl3_draw_vblank)(void);
static Surface *primary;
static DxBallInt primary_changed;
static const DxBallPaletteEntry black[256];

static void touch(const Surface *surface)
{
    if (surface == primary) primary_changed = 1;
}

void dxball_sdl3_draw_flush(void)
{
    if (primary == NULL || !primary_changed || dxball_sdl3_draw_present == NULL) return;
    primary_changed = 0;
    dxball_sdl3_draw_present(primary->pixels, primary->pitch,
                             primary->palette != NULL ? primary->palette->entries : black);
}

static void wait_refresh(void)
{
    dxball_sdl3_draw_flush();
    if (dxball_sdl3_draw_vblank != NULL) dxball_sdl3_draw_vblank();
}

/* ---- Surfaces ---- */

static DxBallDDSurfaceVTable surface_methods;

static Surface *new_surface(DxBallInt width, DxBallInt height, DxBallUInt caps)
{
    Surface *surface = (Surface *)calloc(1, sizeof(*surface));
    if (surface == NULL) return NULL;
    surface->com.vtable = &surface_methods;
    surface->references = 1;
    surface->caps = caps;
    surface->width = width;
    surface->height = height;
    surface->pitch = (width + 7) & ~7;
    surface->pixels = (DxBallByte *)calloc((size_t)surface->pitch, (size_t)height);
    if (surface->pixels == NULL) { free(surface); return NULL; }
    return surface;
}

static void free_surface(Surface *surface)
{
    free(surface->pixels);
    free(surface);
}

static DxBallUInt DXBALL_DDCALL surface_release(DxBallDDSurface *com)
{
    Surface *surface = (Surface *)com, *back;
    if (--surface->references != 0) return surface->references;
    if (surface == primary) {
        /* Attached back buffers are released with the primary. */
        for (back = surface->next; back != NULL; ) {
            Surface *following = back->next;
            free_surface(back);
            back = following;
        }
        primary = NULL;
        primary_changed = 0;
    }
    if ((surface->caps & DDSCAPS_BACKBUFFER) == 0) free_surface(surface);
    return 0;
}

static DxBallRect whole(const Surface *surface)
{
    DxBallRect rect;
    rect.left = 0; rect.top = 0; rect.right = surface->width; rect.bottom = surface->height;
    return rect;
}

static int inside(const DxBallRect *rect, const Surface *surface)
{
    return rect->left >= 0 && rect->top >= 0 && rect->left < rect->right &&
           rect->top < rect->bottom && rect->right <= surface->width &&
           rect->bottom <= surface->height;
}

/* Copies source rect to destination rect, scaling by nearest sample. When the
   two surfaces are the same, rows are staged so overlap reads old pixels. */
static DxBallInt copy_rect(Surface *destination, const DxBallRect *to, Surface *source,
                           const DxBallRect *from, int keyed)
{
    DxBallInt width = to->right - to->left, height = to->bottom - to->top;
    DxBallInt source_width = from->right - from->left, source_height = from->bottom - from->top;
    DxBallInt x, y, pitch = source->pitch;
    const DxBallByte *pixels = source->pixels;
    DxBallByte *staged = NULL;
    if (!inside(to, destination) || !inside(from, source)) return DD_INVALID_RECT;
    if (source == destination) {
        staged = (DxBallByte *)malloc((size_t)source->pitch * (size_t)source->height);
        if (staged == NULL) return DD_OUT_OF_MEMORY;
        memcpy(staged, source->pixels, (size_t)source->pitch * (size_t)source->height);
        pixels = staged;
    }
    for (y = 0; y < height; ++y) {
        const DxBallByte *row = pixels +
            (ptrdiff_t)(from->top + (DxBallInt)((long long)y * source_height / height)) * pitch;
        DxBallByte *out = destination->pixels + (ptrdiff_t)(to->top + y) * destination->pitch + to->left;
        for (x = 0; x < width; ++x) {
            DxBallByte value = row[from->left + (DxBallInt)((long long)x * source_width / width)];
            if (!keyed || value != source->key) out[x] = value;
        }
    }
    free(staged);
    touch(destination);
    return 0;
}

static DxBallInt DXBALL_DDCALL surface_blt(DxBallDDSurface *com, DxBallRect *to,
    DxBallDDSurface *source_com, DxBallRect *from, DxBallUInt flags, void *effects)
{
    Surface *surface = (Surface *)com, *source = (Surface *)source_com;
    DxBallRect target = to != NULL ? *to : whole(surface), origin;
    if ((flags & DDBLT_COLORFILL) != 0) {
        DxBallByte color = (DxBallByte)((const DxBallColorFillFx *)effects)->fill_color;
        DxBallInt y;
        if (!inside(&target, surface)) return DD_INVALID_RECT;
        for (y = target.top; y < target.bottom; ++y)
            memset(surface->pixels + (ptrdiff_t)y * surface->pitch + target.left, color,
                   (size_t)(target.right - target.left));
        touch(surface);
        return 0;
    }
    if (source == NULL) return DD_INVALID_PARAMS;
    origin = from != NULL ? *from : whole(source);
    return copy_rect(surface, &target, source, &origin,
                     (flags & DDBLT_KEYSRC) != 0 && source->keyed);
}

static DxBallInt DXBALL_DDCALL surface_blt_fast(DxBallDDSurface *com, DxBallUInt x, DxBallUInt y,
    DxBallDDSurface *source_com, DxBallRect *from, DxBallUInt flags)
{
    Surface *source = (Surface *)source_com;
    DxBallRect origin, target;
    if (source == NULL) return DD_INVALID_PARAMS;
    origin = from != NULL ? *from : whole(source);
    target.left = (DxBallInt)x; target.top = (DxBallInt)y;
    target.right = target.left + (origin.right - origin.left);
    target.bottom = target.top + (origin.bottom - origin.top);
    return copy_rect((Surface *)com, &target, source, &origin,
                     (flags & DDBLTFAST_SRCCOLORKEY) != 0 && source->keyed);
}

/* Rotate pixel buffers along the chain: the first back buffer becomes visible. */
static DxBallInt DXBALL_DDCALL surface_flip(DxBallDDSurface *com, DxBallDDSurface *target,
                                            DxBallUInt flags)
{
    Surface *surface = (Surface *)com, *link;
    DxBallByte *front;
    (void)target; (void)flags;
    if (surface != primary || surface->next == NULL) return DD_NOT_FOUND;
    front = surface->pixels;
    for (link = surface; link->next != NULL; link = link->next) link->pixels = link->next->pixels;
    link->pixels = front;
    primary_changed = 1;
    wait_refresh();
    return 0;
}

static DxBallInt DXBALL_DDCALL surface_attached(DxBallDDSurface *com, const DxBallUInt *caps,
                                                DxBallDDSurface **output)
{
    Surface *surface = (Surface *)com;
    *output = NULL;
    if (surface->next == NULL || (*caps & DDSCAPS_BACKBUFFER) == 0) return DD_NOT_FOUND;
    ++surface->next->references;
    *output = &surface->next->com;
    return 0;
}

/* Callers fill size with the i386 value (108); the record itself is complete. */
static void describe(const Surface *surface, DxBallSurfaceDesc *desc, const DxBallRect *rect)
{
    DxBallUInt size = desc->size;
    memset(desc, 0, sizeof(*desc));
    desc->size = size;
    desc->flags = DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH | DDSD_PITCH | DDSD_PIXELFORMAT;
    desc->height = (DxBallUInt)surface->height;
    desc->width = (DxBallUInt)surface->width;
    desc->pitch = surface->pitch;
    desc->pixels = surface->pixels;
    if (rect != NULL) desc->pixels += (ptrdiff_t)rect->top * surface->pitch + rect->left;
    desc->pixel_format[0] = 32;   /* DDPIXELFORMAT size */
    desc->pixel_format[1] = 0x60; /* DDPF_PALETTEINDEXED8 | DDPF_RGB */
    desc->pixel_format[3] = 8;    /* bits per pixel */
    desc->caps = surface->caps;
}

static DxBallInt DXBALL_DDCALL surface_get_desc(DxBallDDSurface *com, DxBallSurfaceDesc *desc)
{
    describe((Surface *)com, desc, NULL);
    return 0;
}

static DxBallInt DXBALL_DDCALL surface_lock(DxBallDDSurface *com, DxBallRect *rect,
    DxBallSurfaceDesc *desc, DxBallUInt flags, void *event)
{
    Surface *surface = (Surface *)com;
    (void)flags; (void)event;
    if (rect != NULL && !inside(rect, surface)) return DD_INVALID_RECT;
    describe(surface, desc, rect);
    return 0;
}

static DxBallInt DXBALL_DDCALL surface_unlock(DxBallDDSurface *com, void *pixels)
{
    (void)pixels;
    touch((Surface *)com);
    return 0;
}

static DxBallInt DXBALL_DDCALL surface_succeed(DxBallDDSurface *com) { (void)com; return 0; }

/* Software blits complete synchronously. */
static DxBallInt DXBALL_DDCALL surface_blt_status(DxBallDDSurface *com, DxBallUInt flags)
{ (void)com; (void)flags; return 0; }

static DxBallInt DXBALL_DDCALL surface_set_clipper(DxBallDDSurface *com, DxBallDDClipper *clipper)
{ (void)com; (void)clipper; return 0; }

static DxBallInt DXBALL_DDCALL surface_set_key(DxBallDDSurface *com, DxBallUInt flags,
                                               DxBallColorKey *key)
{
    Surface *surface = (Surface *)com;
    if ((flags & DDCKEY_SRCBLT) == 0) return 0;
    surface->keyed = key != NULL;
    surface->key = key != NULL ? (DxBallByte)key->low : 0;
    return 0;
}

static DxBallInt DXBALL_DDCALL surface_set_palette(DxBallDDSurface *com, DxBallDDPalette *palette)
{
    Surface *surface = (Surface *)com;
    surface->palette = (Palette *)palette;
    touch(surface);
    return 0;
}

/* ---- Palettes and clippers ---- */

static DxBallDDPaletteVTable palette_methods;

static DxBallUInt DXBALL_DDCALL palette_release(DxBallDDPalette *com)
{
    Palette *palette = (Palette *)com;
    if (--palette->references != 0) return palette->references;
    if (primary != NULL && primary->palette == palette) primary->palette = NULL;
    free(palette);
    return 0;
}

static DxBallInt DXBALL_DDCALL palette_set_entries(DxBallDDPalette *com, DxBallUInt flags,
    DxBallUInt start, DxBallUInt count, DxBallPaletteEntry *entries)
{
    Palette *palette = (Palette *)com;
    (void)flags;
    if (start > 256 || count > 256 - start) return DD_INVALID_PARAMS;
    memcpy(palette->entries + start, entries, count * sizeof(*entries));
    if (primary != NULL && primary->palette == palette) primary_changed = 1;
    return 0;
}

static DxBallDDClipperVTable clipper_methods;

static DxBallUInt DXBALL_DDCALL clipper_release(DxBallDDClipper *com)
{
    Clipper *clipper = (Clipper *)com;
    if (--clipper->references != 0) return clipper->references;
    free(clipper);
    return 0;
}

static DxBallInt DXBALL_DDCALL clipper_set_window(DxBallDDClipper *com, DxBallUInt flags, size_t window)
{ (void)com; (void)flags; (void)window; return 0; }

/* ---- DirectDraw object ---- */

static DxBallDDrawVTable draw_methods;
static DxBallDDraw draw = { &draw_methods };

static DxBallInt DXBALL_DDCALL draw_create_palette(DxBallDDraw *device, DxBallUInt flags,
    DxBallPaletteEntry *entries, DxBallDDPalette **output, void *outer)
{
    Palette *palette;
    (void)device; (void)flags; (void)outer;
    *output = NULL;
    palette = (Palette *)calloc(1, sizeof(*palette));
    if (palette == NULL) return DD_OUT_OF_MEMORY;
    palette->com.vtable = &palette_methods;
    palette->references = 1;
    if (entries != NULL) memcpy(palette->entries, entries, sizeof(palette->entries));
    *output = &palette->com;
    return 0;
}

static DxBallInt DXBALL_DDCALL draw_create_surface(DxBallDDraw *device, DxBallSurfaceDesc *desc,
                                                   DxBallDDSurface **output, void *outer)
{
    DxBallUInt caps = (desc->flags & DDSD_CAPS) != 0 ? desc->caps : 0, backs, index;
    Surface *surface, *link;
    (void)device; (void)outer;
    *output = NULL;
    if ((caps & DDSCAPS_PRIMARY) != 0) {
        if (primary != NULL) return DD_INVALID_PARAMS;
        backs = (desc->flags & DDSD_BACKBUFFERCOUNT) != 0 ? desc->ancillary[0] : 0;
        if (backs > 3) return DD_INVALID_PARAMS;
        surface = new_surface(SCREEN_WIDTH, SCREEN_HEIGHT, caps | DDSCAPS_FRONTBUFFER);
        if (surface == NULL) return DD_OUT_OF_MEMORY;
        for (link = surface, index = 0; index < backs; ++index, link = link->next) {
            link->next = new_surface(SCREEN_WIDTH, SCREEN_HEIGHT,
                                     DDSCAPS_BACKBUFFER | DDSCAPS_FLIP | DDSCAPS_SYSTEMMEMORY);
            if (link->next == NULL) { primary = surface; surface_release(&surface->com); return DD_OUT_OF_MEMORY; }
        }
        primary = surface;
        primary_changed = 1;
    } else {
        if ((desc->flags & (DDSD_WIDTH | DDSD_HEIGHT)) != (DDSD_WIDTH | DDSD_HEIGHT) ||
            desc->width == 0 || desc->height == 0 || desc->width > MAX_SIDE || desc->height > MAX_SIDE)
            return DD_INVALID_PARAMS;
        surface = new_surface((DxBallInt)desc->width, (DxBallInt)desc->height,
                              caps | DDSCAPS_SYSTEMMEMORY);
        if (surface == NULL) return DD_OUT_OF_MEMORY;
    }
    *output = &surface->com;
    return 0;
}

static DxBallInt DXBALL_DDCALL draw_wait_vertical_blank(DxBallDDraw *device, DxBallUInt flags, void *event)
{
    (void)device; (void)flags; (void)event;
    wait_refresh();
    return 0;
}

static DxBallInt DXBALL_DDCALL draw_create_clipper(DxBallDDraw *device, DxBallUInt flags,
                                                   DxBallDDClipper **output, void *outer)
{
    Clipper *clipper;
    (void)device; (void)flags; (void)outer;
    clipper = (Clipper *)calloc(1, sizeof(*clipper));
    *output = clipper != NULL ? &clipper->com : NULL;
    if (clipper == NULL) return DD_OUT_OF_MEMORY;
    clipper->com.vtable = &clipper_methods;
    clipper->references = 1;
    return 0;
}

/* Reports hardware blitting with ample video memory: full flip mode. */
static DxBallInt DXBALL_DDCALL draw_get_caps(DxBallDDraw *device, void *driver, void *emulation)
{
    DxBallDrawCaps *caps = (DxBallDrawCaps *)driver;
    (void)device; (void)emulation;
    if (caps != NULL) {
        DxBallUInt size = caps->size;
        memset(caps, 0, sizeof(*caps));
        caps->size = size;
        caps->free_video_memory = 16u << 20;
    }
    return 0;
}

static DxBallInt DXBALL_DDCALL draw_set_cooperative_level(DxBallDDraw *device, size_t window,
                                                          DxBallUInt flags)
{ (void)device; (void)window; (void)flags; return 0; }

static DxBallInt DXBALL_DDCALL draw_set_display_mode(DxBallDDraw *device, DxBallUInt width,
                                                     DxBallUInt height, DxBallUInt bits)
{
    (void)device;
    return width == SCREEN_WIDTH && height == SCREEN_HEIGHT && bits == 8 ? 0 : DD_UNSUPPORTED_MODE;
}

static DxBallUInt DXBALL_DDCALL draw_release(DxBallDDraw *device) { (void)device; return 0; }

DxBallInt DXBALL_DDCALL dxball_sdl3_direct_draw_create(void *guid, DxBallDDraw **device, void *outer)
{
    (void)guid; (void)outer;
    surface_methods.release = surface_release;
    surface_methods.blt = surface_blt;
    surface_methods.blt_fast = surface_blt_fast;
    surface_methods.flip = surface_flip;
    surface_methods.slot_12 = (DxBallUnknownDDMethod)surface_attached;
    surface_methods.get_blt_status = surface_blt_status;
    surface_methods.get_desc = surface_get_desc;
    surface_methods.is_lost = (DxBallUnknownDDMethod)surface_succeed;
    surface_methods.lock = surface_lock;
    surface_methods.restore = surface_succeed;
    surface_methods.set_clipper = (DxBallUnknownDDMethod)surface_set_clipper;
    surface_methods.set_color_key = surface_set_key;
    surface_methods.set_palette = surface_set_palette;
    surface_methods.unlock = surface_unlock;
    palette_methods.slots_0_to_5[2] = (DxBallUnknownDDMethod)palette_release;
    palette_methods.set_entries = palette_set_entries;
    clipper_methods.slots_0_to_7[2] = (DxBallUnknownDDMethod)clipper_release;
    clipper_methods.set_window = clipper_set_window;
    draw_methods.slots_0_to_4[2] = (DxBallUnknownDDMethod)draw_release;
    draw_methods.slots_0_to_4[4] = (DxBallUnknownDDMethod)draw_create_clipper;
    draw_methods.create_palette = draw_create_palette;
    draw_methods.create_surface = draw_create_surface;
    draw_methods.slots_7_to_21[4] = (DxBallUnknownDDMethod)draw_get_caps;
    draw_methods.slots_7_to_21[13] = (DxBallUnknownDDMethod)draw_set_cooperative_level;
    draw_methods.slots_7_to_21[14] = (DxBallUnknownDDMethod)draw_set_display_mode;
    draw_methods.wait_vertical_blank = draw_wait_vertical_blank;
    *device = &draw;
    return 0;
}
