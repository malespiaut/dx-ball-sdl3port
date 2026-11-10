/* Win32 services for the SDL3 build (Linux, macOS, Windows): window, message
   queue, input, files, local/global memory, file mapping, clock and the
   instance lock. Mirrors windows_adapter.c; recovered game code calls the
   same cells. */
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE /* open flags, flock under strict C11 */
#endif
#include "sdl3_adapter.h"
#include "allocator.h"
#include "clock_api.h"
#include "file.h"
#include "memory.h"
#include "midi.h"
#include "paddle.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#ifdef _WIN32
#include <io.h>
#include <sys/locking.h>
#define try_lock(file) _locking(file, _LK_NBLCK, 1)
#define NULL_DEVICE "NUL"
#else
#include <sys/file.h>
#define try_lock(file) flock(file, LOCK_EX | LOCK_NB)
#define NULL_DEVICE "/dev/null"
#endif
#ifndef O_BINARY
#define O_BINARY 0 /* POSIX opens are always binary */
#endif
#ifndef O_CLOEXEC
#define O_CLOEXEC 0 /* Windows descriptors are not inherited by default */
#endif

enum {
    WM_DESTROY = 2, WM_SETFOCUS = 7, WM_KILLFOCUS = 8, WM_CLOSE = 0x10, WM_QUIT = 0x12,
    WM_ACTIVATEAPP = 0x1c, WM_KEYDOWN = 0x100, WM_KEYUP = 0x101, WM_MOUSEMOVE = 0x200,
    WM_LBUTTONDOWN = 0x201, WM_LBUTTONUP = 0x202, WM_RBUTTONDOWN = 0x204, WM_RBUTTONUP = 0x205,
    SCREEN_WIDTH = 640, SCREEN_HEIGHT = 480, QUEUE_SIZE = 256, MAIN_WINDOW = 1
};
#define INVALID_HANDLE ((DxBallHandle)-1)

static DxBallMessage queue[QUEUE_SIZE];
static unsigned queue_head, queue_count;
static DxBallWindowProc window_procedure;
static SDL_Window *window;
static SDL_Renderer *renderer;
static SDL_Texture *texture;
static float cursor_x = SCREEN_WIDTH / 2, cursor_y = SCREEN_HEIGHT / 2;
static int instance_lock = -1;
static DxBallByte last_frame[SCREEN_WIDTH * SCREEN_HEIGHT];
static SDL_Color last_colors[256];
void (*dxball_sdl3_idle)(void);

static DxBallInt post(DxBallHandle target, DxBallUInt message, DxBallHandle wparam,
                      DxBallWindowResult lparam)
{
    DxBallMessage *slot;
    if (queue_count == QUEUE_SIZE) return 0;
    slot = &queue[(queue_head + queue_count++) % QUEUE_SIZE];
    slot->window = target; slot->message = message;
    slot->wparam = wparam; slot->lparam = lparam;
    slot->time = (DxBallUInt)SDL_GetTicks();
    slot->point.x = (DxBallInt)cursor_x; slot->point.y = (DxBallInt)cursor_y;
    return 1;
}

DxBallUInt dxball_sdl3_virtual_key(SDL_Keycode key)
{
    if (key >= SDLK_A && key <= SDLK_Z) return 'A' + (key - SDLK_A);
    if (key >= SDLK_0 && key <= SDLK_9) return '0' + (key - SDLK_0);
    if (key >= SDLK_F1 && key <= SDLK_F12) return 0x70 + (key - SDLK_F1);
    if (key >= SDLK_KP_1 && key <= SDLK_KP_9) return 0x61 + (key - SDLK_KP_1);
    switch (key) {
    case SDLK_BACKSPACE: return 0x08;
    case SDLK_TAB: return 0x09;
    case SDLK_RETURN: case SDLK_KP_ENTER: return 0x0d;
    case SDLK_LSHIFT: case SDLK_RSHIFT: return 0x10;
    case SDLK_LCTRL: case SDLK_RCTRL: return 0x11;
    case SDLK_LALT: case SDLK_RALT: return 0x12;
    case SDLK_PAUSE: return 0x13;
    case SDLK_CAPSLOCK: return 0x14;
    case SDLK_ESCAPE: return 0x1b;
    case SDLK_SPACE: return 0x20;
    case SDLK_PAGEUP: return 0x21;
    case SDLK_PAGEDOWN: return 0x22;
    case SDLK_END: return 0x23;
    case SDLK_HOME: return 0x24;
    case SDLK_LEFT: return 0x25;
    case SDLK_UP: return 0x26;
    case SDLK_RIGHT: return 0x27;
    case SDLK_DOWN: return 0x28;
    case SDLK_INSERT: return 0x2d;
    case SDLK_DELETE: return 0x2e;
    case SDLK_KP_0: return 0x60;
    case SDLK_KP_MULTIPLY: return 0x6a;
    case SDLK_KP_PLUS: return 0x6b;
    case SDLK_KP_MINUS: return 0x6d;
    case SDLK_KP_PERIOD: return 0x6e;
    case SDLK_KP_DIVIDE: return 0x6f;
    case SDLK_SEMICOLON: return 0xba;
    case SDLK_EQUALS: return 0xbb;
    case SDLK_COMMA: return 0xbc;
    case SDLK_MINUS: return 0xbd;
    case SDLK_PERIOD: return 0xbe;
    case SDLK_SLASH: return 0xbf;
    case SDLK_GRAVE: return 0xc0;
    case SDLK_LEFTBRACKET: return 0xdb;
    case SDLK_BACKSLASH: return 0xdc;
    case SDLK_RIGHTBRACKET: return 0xdd;
    case SDLK_APOSTROPHE: return 0xde;
    default: return 0;
    }
}

static float clamp(float value, float limit)
{
    return value < 0 ? 0 : value > limit ? limit : value;
}

void dxball_sdl3_translate_event(SDL_Event *event)
{
    DxBallUInt key;
    switch (event->type) {
    case SDL_EVENT_QUIT:
    case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
        post(MAIN_WINDOW, WM_CLOSE, 0, 0);
        break;
    case SDL_EVENT_KEY_DOWN:
        if (event->key.key == SDLK_RETURN && (event->key.mod & SDL_KMOD_ALT) != 0) {
            if (window != NULL)
                SDL_SetWindowFullscreen(window,
                    (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN) == 0);
            break;
        }
        /* fall through */
    case SDL_EVENT_KEY_UP:
        key = dxball_sdl3_virtual_key(event->key.key);
        if (key != 0)
            post(MAIN_WINDOW, event->type == SDL_EVENT_KEY_DOWN ? WM_KEYDOWN : WM_KEYUP, key, 0);
        break;
    case SDL_EVENT_MOUSE_MOTION:
        if (renderer != NULL) SDL_ConvertEventToRenderCoordinates(renderer, event);
        if (window == NULL || SDL_GetWindowRelativeMouseMode(window)) {
            cursor_x += event->motion.xrel; cursor_y += event->motion.yrel;
        } else {
            cursor_x = event->motion.x; cursor_y = event->motion.y;
        }
        cursor_x = clamp(cursor_x, SCREEN_WIDTH - 1);
        cursor_y = clamp(cursor_y, SCREEN_HEIGHT - 1);
        post(MAIN_WINDOW, WM_MOUSEMOVE, 0,
             ((DxBallWindowResult)(DxBallInt)cursor_y << 16) | (DxBallInt)cursor_x);
        break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
        if (event->button.button == SDL_BUTTON_LEFT)
            post(MAIN_WINDOW, event->button.down ? WM_LBUTTONDOWN : WM_LBUTTONUP, 0, 0);
        else if (event->button.button == SDL_BUTTON_RIGHT)
            post(MAIN_WINDOW, event->button.down ? WM_RBUTTONDOWN : WM_RBUTTONUP, 0, 0);
        break;
    case SDL_EVENT_WINDOW_FOCUS_GAINED:
        if (window != NULL) SDL_SetWindowRelativeMouseMode(window, true);
        post(MAIN_WINDOW, WM_ACTIVATEAPP, 1, 0);
        post(MAIN_WINDOW, WM_SETFOCUS, 0, 0);
        break;
    case SDL_EVENT_WINDOW_FOCUS_LOST:
        if (window != NULL) SDL_SetWindowRelativeMouseMode(window, false);
        post(MAIN_WINDOW, WM_KILLFOCUS, 0, 0);
        post(MAIN_WINDOW, WM_ACTIVATEAPP, 0, 0);
        break;
    default:
        break;
    }
}

static void pump(void)
{
    SDL_Event event;
    while (SDL_PollEvent(&event)) dxball_sdl3_translate_event(&event);
}

static void pop(DxBallMessage *message, int remove)
{
    *message = queue[queue_head];
    if (remove) { queue_head = (queue_head + 1) % QUEUE_SIZE; --queue_count; }
}

static DxBallInt DXBALL_DDCALL peek_message(DxBallMessage *message, DxBallHandle target,
    DxBallUInt first, DxBallUInt last, DxBallUInt remove)
{
    static int frame_ran; /* the last empty peek let WinMain run a frame */
    (void)target; (void)first; (void)last;
    pump();
    /* Pace once per frame, never per message: mouse motion arriving during
       every idle wait would otherwise keep the loop from running frames. */
    if (queue_count == 0 && frame_ran && dxball_sdl3_idle != NULL) {
        frame_ran = 0;
        dxball_sdl3_idle();
        pump();
    }
    if (queue_count == 0) { frame_ran = 1; return 0; }
    pop(message, (remove & 1) != 0);
    return 1;
}

static DxBallInt DXBALL_DDCALL wait_message(void)
{
    SDL_Event event;
    if (queue_count == 0 && SDL_WaitEvent(&event)) dxball_sdl3_translate_event(&event);
    return 1;
}

static DxBallInt DXBALL_DDCALL get_message(DxBallMessage *message, DxBallHandle target,
                                           DxBallUInt first, DxBallUInt last)
{
    SDL_Event event;
    (void)target; (void)first; (void)last;
    pump();
    while (queue_count == 0) {
        if (!SDL_WaitEvent(&event)) return -1;
        dxball_sdl3_translate_event(&event);
    }
    pop(message, 1);
    return message->message != WM_QUIT;
}

static DxBallInt DXBALL_DDCALL translate_message(const DxBallMessage *message)
{ (void)message; return 0; }

static DxBallWindowResult DXBALL_DDCALL dispatch_message(const DxBallMessage *message)
{
    if (window_procedure == NULL || message->window == 0) return 0;
    return window_procedure(message->window, message->message, message->wparam, message->lparam);
}

static DxBallInt DXBALL_DDCALL destroy_window(DxBallHandle target)
{
    if (window_procedure != NULL) window_procedure(target, WM_DESTROY, 0, 0);
    if (window != NULL) SDL_HideWindow(window);
    return 1;
}

static DxBallWindowResult DXBALL_DDCALL default_proc(DxBallHandle target, DxBallUInt message,
    DxBallHandle wparam, DxBallWindowResult lparam)
{
    (void)wparam; (void)lparam;
    if (message == WM_CLOSE) destroy_window(target);
    return 0;
}

static DxBallInt DXBALL_DDCALL post_message(DxBallHandle target, DxBallUInt message,
    DxBallHandle wparam, DxBallWindowResult lparam)
{ return post(target, message, wparam, lparam); }
static void DXBALL_DDCALL post_quit(DxBallInt code) { post(0, WM_QUIT, (DxBallHandle)code, 0); }
static DxBallHandle DXBALL_DDCALL set_cursor(DxBallHandle cursor) { (void)cursor; return 0; }
static DxBallHandle DXBALL_DDCALL set_capture(DxBallHandle target) { (void)target; return 0; }
static DxBallInt DXBALL_DDCALL release_capture(void) { return 1; }
static DxBallHandle DXBALL_DDCALL load_resource(DxBallHandle instance, DxBallHandle name)
{ (void)instance; (void)name; return 1; }
static DxBallHandle DXBALL_DDCALL stock_object(DxBallInt object) { (void)object; return 1; }
static DxBallUInt DXBALL_DDCALL register_class(const DxBallWindowClass *input)
{
    window_procedure = input->procedure;
    return 1;
}

static DxBallHandle DXBALL_DDCALL create_window(DxBallUInt extended, const char *class_name,
    const char *title, DxBallUInt style, DxBallInt x, DxBallInt y, DxBallInt width,
    DxBallInt height, DxBallHandle parent, DxBallHandle menu, DxBallHandle instance, void *parameter)
{
    SDL_Rect usable;
    int scale = 1;
    (void)extended; (void)class_name; (void)style; (void)x; (void)y;
    (void)parent; (void)menu; (void)instance; (void)parameter;
    if (SDL_GetDisplayUsableBounds(SDL_GetPrimaryDisplay(), &usable) &&
        usable.w >= width * 2 && usable.h >= height * 2)
        scale = 2;
    window = SDL_CreateWindow(title, width * scale, height * scale, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN);
    if (window == NULL) return 0;
    renderer = SDL_CreateRenderer(window, NULL);
    if (renderer != NULL) {
        SDL_SetRenderLogicalPresentation(renderer, SCREEN_WIDTH, SCREEN_HEIGHT,
                                         SDL_LOGICAL_PRESENTATION_LETTERBOX);
        texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_XRGB8888,
                                    SDL_TEXTUREACCESS_STREAMING, SCREEN_WIDTH, SCREEN_HEIGHT);
    }
    if (texture == NULL) {
        fprintf(stderr, "dxball: %s\n", SDL_GetError());
        SDL_DestroyWindow(window); window = NULL;
        return 0;
    }
    SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST);
    return MAIN_WINDOW;
}

static DxBallInt DXBALL_DDCALL show_window(DxBallHandle target, DxBallInt show)
{
    (void)target;
    if (window == NULL) return 0;
    return show == 0 ? SDL_HideWindow(window) : SDL_ShowWindow(window);
}
static DxBallInt DXBALL_DDCALL update_window(DxBallHandle target) { (void)target; return 1; }
static DxBallHandle DXBALL_DDCALL set_focus(DxBallHandle target)
{
    if (window != NULL) SDL_RaiseWindow(window);
    return target;
}

void dxball_sdl3_present(const DxBallByte *pixels, DxBallInt pitch,
                         const DxBallPaletteEntry palette[256])
{
    Uint32 colors[256];
    void *output;
    int output_pitch, x, y;
    for (x = 0; x < 256; ++x) {
        colors[x] = (Uint32)palette[x].red << 16 | (Uint32)palette[x].green << 8 | palette[x].blue;
        last_colors[x].r = palette[x].red; last_colors[x].g = palette[x].green;
        last_colors[x].b = palette[x].blue; last_colors[x].a = 255;
    }
    for (y = 0; y < SCREEN_HEIGHT; ++y)
        memcpy(last_frame + y * SCREEN_WIDTH, pixels + (ptrdiff_t)y * pitch, SCREEN_WIDTH);
    if (texture == NULL || !SDL_LockTexture(texture, NULL, &output, &output_pitch)) return;
    for (y = 0; y < SCREEN_HEIGHT; ++y) {
        const DxBallByte *source = last_frame + y * SCREEN_WIDTH;
        Uint32 *row = (Uint32 *)((Uint8 *)output + (ptrdiff_t)y * output_pitch);
        for (x = 0; x < SCREEN_WIDTH; ++x) row[x] = colors[source[x]];
    }
    SDL_UnlockTexture(texture);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    SDL_RenderTexture(renderer, texture, NULL, NULL);
    SDL_RenderPresent(renderer);
}

int dxball_sdl3_save_screenshot(const char *path)
{
    SDL_Surface *image = SDL_CreateSurfaceFrom(SCREEN_WIDTH, SCREEN_HEIGHT, SDL_PIXELFORMAT_INDEX8,
                                               last_frame, SCREEN_WIDTH);
    SDL_Palette *palette = image != NULL ? SDL_CreateSurfacePalette(image) : NULL;
    int saved = palette != NULL && SDL_SetPaletteColors(palette, last_colors, 0, 256) &&
                SDL_SaveBMP(image, path);
    SDL_DestroySurface(image);
    return saved;
}

static DxBallInt DXBALL_DDCALL message_box(DxBallHandle target, const char *text,
                                           const char *title, DxBallUInt flags)
{
    static const SDL_MessageBoxButtonData ok[] = {
        { SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT | SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 1, "OK" } };
    static const SDL_MessageBoxButtonData abort_retry_ignore[] = {
        { 0, 3, "Abort" }, { SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 4, "Retry" },
        { SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 5, "Ignore" } };
    static const SDL_MessageBoxButtonData yes_no[] = {
        { SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 6, "Yes" },
        { SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 7, "No" } };
    SDL_MessageBoxData box;
    int button = 1;
    (void)target;
    memset(&box, 0, sizeof(box));
    box.flags = SDL_MESSAGEBOX_INFORMATION;
    box.window = window;
    box.title = title; box.message = text;
    /* Without a display the answer defaults to "continue without sound". */
    switch (flags & 0xf) {
    case 2: box.buttons = abort_retry_ignore; box.numbuttons = 3; button = 5; break;
    case 4: box.buttons = yes_no; box.numbuttons = 2; button = 6; break;
    default: box.buttons = ok; box.numbuttons = 1; break;
    }
    fprintf(stderr, "%s: %s\n", title, text);
    SDL_ShowMessageBox(&box, &button);
    return button;
}

static DxBallInt DXBALL_DDCALL cursor_position(DxBallPoint *point)
{
    point->x = (DxBallInt)cursor_x; point->y = (DxBallInt)cursor_y;
    return 1;
}
static DxBallInt DXBALL_DDCALL set_cursor_position(DxBallInt x, DxBallInt y)
{
    cursor_x = (float)x; cursor_y = (float)y;
    return 1;
}
static DxBallInt DXBALL_DDCALL version_info(DxBallVersionInfo *version)
{
    version->major = 4; version->minor = 0; version->build = 0;
    version->platform = 2; /* VER_PLATFORM_WIN32_NT: use the performance counter */
    version->service_pack[0] = 0;
    return 1;
}

/* Kernel32 instance semaphore: an advisory lock held for the process lifetime. */
static DxBallHandle DXBALL_DDCALL open_semaphore(DxBallUInt access, DxBallInt inherit, const char *name)
{
    static const char *const variables[] = { "XDG_RUNTIME_DIR", "TMPDIR", "TEMP" };
    const char *directory = NULL;
    char path[4096];
    int file;
    size_t i;
    (void)access; (void)inherit;
    for (i = 0; directory == NULL && i < sizeof(variables) / sizeof(variables[0]); ++i)
        directory = getenv(variables[i]);
    snprintf(path, sizeof(path), "%s/%s.lock", directory != NULL ? directory : "/tmp", name);
    file = open(path, O_RDWR | O_CREAT | O_BINARY | O_CLOEXEC, 0600);
    if (file >= 0 && try_lock(file) != 0) {
        close(file);
        return 1; /* another instance holds the lock; the game never closes this */
    }
    instance_lock = file >= 0 ? file : open(NULL_DEVICE, O_RDONLY | O_CLOEXEC);
    return 0;
}
static DxBallHandle DXBALL_DDCALL create_semaphore(const DxBallSecurityAttributes *attributes,
    DxBallInt initial, DxBallInt maximum, const char *name)
{
    (void)attributes; (void)initial; (void)maximum; (void)name;
    return instance_lock >= 0 ? (DxBallHandle)instance_lock : 0;
}

static DxBallHandle DXBALL_DDCALL create_file(const char *name, DxBallUInt access,
    DxBallUInt share, void *security, DxBallUInt creation, DxBallUInt flags, DxBallHandle template_file)
{
    static const int dispositions[6] = { 0, O_CREAT | O_EXCL, O_CREAT | O_TRUNC, 0, O_CREAT, O_TRUNC };
    int mode, file;
    (void)share; (void)security; (void)flags; (void)template_file;
    if (creation < 1 || creation > 5) return INVALID_HANDLE;
    mode = (access & 0xc0000000UL) == 0xc0000000UL ? O_RDWR :
           (access & 0x40000000UL) != 0 ? O_WRONLY : O_RDONLY;
    file = open(name, mode | dispositions[creation] | O_BINARY | O_CLOEXEC, 0644);
    return file < 0 ? INVALID_HANDLE : (DxBallHandle)file;
}
static DxBallUInt DXBALL_DDCALL file_size(DxBallHandle file, DxBallUInt *high)
{
    struct stat status;
    if (fstat((int)file, &status) != 0) return 0xffffffffUL;
    if (high != NULL) *high = (DxBallUInt)((unsigned long long)status.st_size >> 32);
    return (DxBallUInt)status.st_size;
}
static DxBallInt DXBALL_DDCALL read_file(DxBallHandle file, void *destination,
    DxBallUInt bytes, DxBallUInt *read_bytes, void *overlapped)
{
    DxBallUInt total = 0;
    (void)overlapped;
    while (total < bytes) {
        ssize_t part = read((int)file, (char *)destination + total, bytes - total);
        if (part < 0 && errno == EINTR) continue;
        if (part < 0) { *read_bytes = total; return 0; }
        if (part == 0) break; /* ReadFile succeeds short at end of file */
        total += (DxBallUInt)part;
    }
    *read_bytes = total;
    return 1;
}
static DxBallInt DXBALL_DDCALL close_handle(DxBallHandle handle)
{ return close((int)handle) == 0; }

static void *DXBALL_DDCALL local_alloc(DxBallUInt flags, size_t bytes)
{ return (flags & 0x40) != 0 ? calloc(1, bytes) : malloc(bytes); }
static void *DXBALL_DDCALL local_free(void *memory) { free(memory); return NULL; }

/* A mapping handle is a duplicate descriptor; a view is a private copy.
   The duplicate shares the file offset, so a view seeks before it reads. */
static DxBallMidiHandle DXBALL_DDCALL create_mapping(DxBallMidiHandle file, void *security,
    DxBallUInt protect, DxBallUInt high, DxBallUInt low, const char *name)
{
    int mapping;
    (void)security; (void)protect; (void)high; (void)low; (void)name;
    mapping = dup((int)file);
    return mapping < 0 ? 0 : (DxBallMidiHandle)mapping;
}
static void *DXBALL_DDCALL map_view(DxBallMidiHandle mapping, DxBallUInt access,
    DxBallUInt high, DxBallUInt low, size_t bytes)
{
    struct stat status;
    char *view;
    size_t total = 0;
    (void)access;
    if (high != 0 || fstat((int)mapping, &status) != 0 || (size_t)status.st_size < low) return NULL;
    if (bytes == 0) bytes = (size_t)status.st_size - low;
    if (lseek((int)mapping, (off_t)low, SEEK_SET) < 0) return NULL;
    view = (char *)malloc(bytes != 0 ? bytes : 1);
    while (view != NULL && total < bytes) {
        ssize_t part = read((int)mapping, view + total, bytes - total);
        if (part < 0 && errno == EINTR) continue;
        if (part <= 0) { free(view); return NULL; }
        total += (size_t)part;
    }
    return view;
}
static DxBallInt DXBALL_DDCALL unmap_view(const void *memory) { free((void *)memory); return 1; }
static DxBallMidiHandle DXBALL_DDCALL global_alloc(DxBallUInt flags, size_t bytes)
{ (void)flags; return (DxBallMidiHandle)calloc(1, bytes); }
static void *DXBALL_DDCALL global_lock(DxBallMidiHandle memory) { return (void *)memory; }
static DxBallMidiHandle DXBALL_DDCALL global_handle(const void *memory)
{ return (DxBallMidiHandle)memory; }
static DxBallInt DXBALL_DDCALL global_unlock(DxBallMidiHandle memory) { (void)memory; return 0; }
static DxBallMidiHandle DXBALL_DDCALL global_free(DxBallMidiHandle memory)
{ free((void *)memory); return 0; }

#ifdef _WIN32
/* Recovered code also calls the real WinMM timeGetTime; share its epoch. */
static DxBallUInt DXBALL_DDCALL time_ms(void) { return (DxBallUInt)timeGetTime(); }
#else
static DxBallUInt DXBALL_DDCALL time_ms(void) { return (DxBallUInt)SDL_GetTicks(); }
#endif
static void split_counter(DxBallCounter *counter, Uint64 value)
{
    counter->low = (DxBallUInt)value;
    counter->high = (DxBallInt)(value >> 32);
}
static DxBallInt DXBALL_DDCALL clock_frequency(DxBallCounter *counter)
{ split_counter(counter, SDL_GetPerformanceFrequency()); return 1; }
static DxBallInt DXBALL_DDCALL clock_counter(DxBallCounter *counter)
{ split_counter(counter, SDL_GetPerformanceCounter()); return 1; }

void dxball_bind_sdl3(void)
{
    dxball_window_load_icon = load_resource; dxball_window_load_cursor = load_resource;
    dxball_window_stock_object = stock_object; dxball_window_register_class = register_class;
    dxball_window_create_window_ex = create_window; dxball_window_show_window = show_window;
    dxball_window_update_window = update_window; dxball_window_set_focus = set_focus;
    dxball_window_destroy_window = destroy_window; dxball_window_message_box = message_box;
    dxball_window_get_cursor_pos = cursor_position;
    dxball_window_peek_message = peek_message; dxball_window_wait_message = wait_message;
    dxball_window_get_message = get_message; dxball_window_translate_message = translate_message;
    dxball_window_dispatch_message = dispatch_message; dxball_window_default_window_proc = default_proc;
    dxball_window_post_message = post_message; dxball_window_post_quit_message = post_quit;
    dxball_window_set_cursor = set_cursor; dxball_window_set_capture = set_capture;
    dxball_window_release_capture = release_capture;
    dxball_window_open_semaphore = open_semaphore;
    dxball_window_create_semaphore = create_semaphore;
    dxball_window_get_version_ex = version_info;
    dxball_clock_ops.time_ms = time_ms; dxball_clock_ops.frequency = clock_frequency;
    dxball_clock_ops.counter = clock_counter; dxball_set_cursor_position = set_cursor_position;
    dxball_file_create = create_file; dxball_file_size = file_size;
    dxball_file_read = read_file; dxball_file_close = close_handle;
    dxball_local_alloc = local_alloc; dxball_local_free = local_free;
    dxball_midi_create_mapping = create_mapping; dxball_midi_map_view = map_view;
    dxball_midi_unmap_view = unmap_view;
    dxball_midi_global_alloc = global_alloc; dxball_midi_global_lock = global_lock;
    dxball_midi_global_handle = global_handle; dxball_midi_global_unlock = global_unlock;
    dxball_midi_global_free = global_free;
}
