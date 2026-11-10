/* C-layout COM objects whose methods record the slot that was reached. */
#include "vtable_probe.h"

int probe_last_slot = -1;

static DxBallUInt DXBALL_DDCALL s_release(DxBallDDSurface *s) { (void)s; probe_last_slot = 2; return 0; }
static DxBallInt DXBALL_DDCALL s_blt(DxBallDDSurface *s, DxBallRect *d, DxBallDDSurface *src,
    DxBallRect *r, DxBallUInt f, void *fx)
{ (void)s; (void)d; (void)src; (void)r; (void)f; (void)fx; probe_last_slot = 5; return 0; }
static DxBallInt DXBALL_DDCALL s_blt_fast(DxBallDDSurface *s, DxBallUInt x, DxBallUInt y,
    DxBallDDSurface *src, DxBallRect *r, DxBallUInt f)
{ (void)s; (void)x; (void)y; (void)src; (void)r; (void)f; probe_last_slot = 7; return 0; }
static DxBallInt DXBALL_DDCALL s_flip(DxBallDDSurface *s, DxBallDDSurface *t, DxBallUInt f)
{ (void)s; (void)t; (void)f; probe_last_slot = 11; return 0; }
static DxBallInt DXBALL_DDCALL s_attached(DxBallDDSurface *s, const DxBallUInt *c, DxBallDDSurface **o)
{ (void)s; (void)c; (void)o; probe_last_slot = 12; return 0; }
static DxBallInt DXBALL_DDCALL s_blt_status(DxBallDDSurface *s, DxBallUInt f)
{ (void)s; (void)f; probe_last_slot = 13; return 0; }
static DxBallInt DXBALL_DDCALL s_get_desc(DxBallDDSurface *s, DxBallSurfaceDesc *d)
{ (void)s; (void)d; probe_last_slot = 22; return 0; }
static DxBallInt DXBALL_DDCALL s_lock(DxBallDDSurface *s, DxBallRect *r, DxBallSurfaceDesc *d,
    DxBallUInt f, void *e)
{ (void)s; (void)r; (void)d; (void)f; (void)e; probe_last_slot = 25; return 0; }
static DxBallInt DXBALL_DDCALL s_restore(DxBallDDSurface *s) { (void)s; probe_last_slot = 27; return 0; }
static DxBallInt DXBALL_DDCALL s_set_key(DxBallDDSurface *s, DxBallUInt f, DxBallColorKey *k)
{ (void)s; (void)f; (void)k; probe_last_slot = 29; return 0; }
static DxBallInt DXBALL_DDCALL s_set_palette(DxBallDDSurface *s, DxBallDDPalette *p)
{ (void)s; (void)p; probe_last_slot = 31; return 0; }
static DxBallInt DXBALL_DDCALL s_unlock(DxBallDDSurface *s, void *p)
{ (void)s; (void)p; probe_last_slot = 32; return 0; }
static DxBallInt DXBALL_DDCALL p_set_entries(DxBallDDPalette *p, DxBallUInt f, DxBallUInt s,
    DxBallUInt c, DxBallPaletteEntry *e)
{ (void)p; (void)f; (void)s; (void)c; (void)e; probe_last_slot = 6; return 0; }

static DxBallDDSurfaceVTable surface_methods;
static DxBallDDPaletteVTable palette_methods;
static DxBallDDSurface surface = { &surface_methods };
static DxBallDDPalette palette = { &palette_methods };

void *probe_surface(void)
{
    surface_methods.release = s_release;
    surface_methods.blt = s_blt;
    surface_methods.blt_fast = s_blt_fast;
    surface_methods.flip = s_flip;
    surface_methods.slot_12 = (DxBallUnknownDDMethod)s_attached;
    surface_methods.get_blt_status = s_blt_status;
    surface_methods.get_desc = s_get_desc;
    surface_methods.lock = s_lock;
    surface_methods.restore = s_restore;
    surface_methods.set_color_key = s_set_key;
    surface_methods.set_palette = s_set_palette;
    surface_methods.unlock = s_unlock;
    return &surface;
}

void *probe_palette(void)
{
    palette_methods.set_entries = p_set_entries;
    return &palette;
}
