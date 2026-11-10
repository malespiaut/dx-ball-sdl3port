#ifndef DXBALL_SDL3_DRAW_H
#define DXBALL_SDL3_DRAW_H

#include "platform.h"

/* Software DirectDraw 1 for the Linux build: 8-bit system-memory surfaces,
   a flip chain and palettes, behind the same COM slots the game calls. */
DxBallInt DXBALL_DDCALL dxball_sdl3_direct_draw_create(void *guid, DxBallDDraw **device,
                                                      void *outer);
/* Shows the primary's 640x480 pixels with its palette. */
extern void (*dxball_sdl3_draw_present)(const DxBallByte *pixels, DxBallInt pitch,
                                        const DxBallPaletteEntry palette[256]);
/* Blocks until the next display refresh (Flip and WaitForVerticalBlank). */
extern void (*dxball_sdl3_draw_vblank)(void);
/* Presents the primary if it changed since it was last shown. */
void dxball_sdl3_draw_flush(void);

#endif
