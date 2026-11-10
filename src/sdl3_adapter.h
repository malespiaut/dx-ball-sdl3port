#ifndef DXBALL_SDL3_ADAPTER_H
#define DXBALL_SDL3_ADAPTER_H

#include <SDL3/SDL.h>
#include "platform.h"

/* Linux/SDL3 bindings for the Win32 import boundary that windows_adapter.c
   binds on Windows. Game code is unchanged; only import cells are assigned. */
void dxball_bind_sdl3(void);

/* Layout-aware SDL keycode to Windows virtual-key code; 0 when unmapped. */
DxBallUInt dxball_sdl3_virtual_key(SDL_Keycode key);
/* Queue the window messages one SDL event produces. */
void dxball_sdl3_translate_event(SDL_Event *event);
/* Show one 640x480 eight-bit frame through the window's renderer. */
void dxball_sdl3_present(const DxBallByte *pixels, DxBallInt pitch,
                         const DxBallPaletteEntry palette[256]);
/* Saves the last presented frame as an 8-bit BMP. */
int dxball_sdl3_save_screenshot(const char *path);
/* Called whenever the message loop looks for input (display flush). */
extern void (*dxball_sdl3_idle)(void);

#endif
