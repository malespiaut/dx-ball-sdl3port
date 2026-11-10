#ifndef DXBALL_VTABLE_PROBE_H
#define DXBALL_VTABLE_PROBE_H
#include "resources.h"

#ifdef __cplusplus
extern "C" {
#endif
extern int probe_last_slot;
void *probe_surface(void);
void *probe_palette(void);
#ifdef __cplusplus
}
#endif
#endif
