/* Recovered C++ owners call DirectDraw through virtual projections; the Linux
   adapter supplies C vtables. Each call must land in the same COM slot. */
#include "check.h"
#include "vtable_probe.h"
#include <stddef.h>

#define EXPECT_SLOT(call, slot) do { probe_last_slot = -1; call; CHECK_EQ(probe_last_slot, slot); } while (0)

int main()
{
    DxBallDDSurface *s = static_cast<DxBallDDSurface *>(probe_surface());
    DxBallDDPalette *p = static_cast<DxBallDDPalette *>(probe_palette());
    DxBallRect rect = {0, 0, 1, 1};
    DxBallSurfaceDesc desc;
    DxBallColorKey key = {0, 0};
    DxBallDDSurface *out = NULL;
    EXPECT_SLOT(s->release(), 2);
    EXPECT_SLOT(s->blt(&rect, s, &rect, 0, NULL), 5);
    EXPECT_SLOT(s->blt_fast(0, 0, s, &rect, 0), 7);
    EXPECT_SLOT(s->flip(NULL, 0), 11);
    EXPECT_SLOT(s->get_attached_surface(NULL, &out), 12);
    EXPECT_SLOT(s->get_blt_status(1), 13);
    EXPECT_SLOT(s->get_desc(&desc), 22);
    EXPECT_SLOT(s->lock(NULL, &desc, 0, NULL), 25);
    EXPECT_SLOT(s->restore(), 27);
    EXPECT_SLOT(s->set_color_key(8, &key), 29);
    EXPECT_SLOT(s->set_palette(p), 31);
    EXPECT_SLOT(s->unlock(NULL), 32);
    EXPECT_SLOT(p->set_entries(0, 0, 0, NULL), 6);
    return CHECK_DONE();
}
