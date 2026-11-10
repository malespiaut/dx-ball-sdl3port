/* Win32 service emulation used by the SDL3 build, exercised without a window. */
#define _DEFAULT_SOURCE /* mkdtemp, setenv */
#include "check.h"
#include "sdl3_adapter.h"
#include "clock_api.h"
#include "file.h"
#include "memory.h"
#include "midi.h"
#include "paddle.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static DxBallUInt seen[16];
static DxBallHandle seen_wparam[16];
static int seen_count;

static DxBallWindowResult DXBALL_DDCALL record(DxBallHandle window, DxBallUInt message,
    DxBallHandle wparam, DxBallWindowResult lparam)
{
    (void)window; (void)lparam;
    if (seen_count < 16) { seen_wparam[seen_count] = wparam; seen[seen_count++] = message; }
    return 0;
}

static void test_virtual_keys(void)
{
    CHECK_EQ(dxball_sdl3_virtual_key(SDLK_A), 'A');
    CHECK_EQ(dxball_sdl3_virtual_key(SDLK_P), 'P');
    CHECK_EQ(dxball_sdl3_virtual_key(SDLK_Z), 'Z');
    CHECK_EQ(dxball_sdl3_virtual_key(SDLK_0), '0');
    CHECK_EQ(dxball_sdl3_virtual_key(SDLK_9), '9');
    CHECK_EQ(dxball_sdl3_virtual_key(SDLK_F1), 0x70);
    CHECK_EQ(dxball_sdl3_virtual_key(SDLK_F12), 0x7b);
    CHECK_EQ(dxball_sdl3_virtual_key(SDLK_ESCAPE), 0x1b);
    CHECK_EQ(dxball_sdl3_virtual_key(SDLK_SPACE), 0x20);
    CHECK_EQ(dxball_sdl3_virtual_key(SDLK_RETURN), 0x0d);
    CHECK_EQ(dxball_sdl3_virtual_key(SDLK_BACKSPACE), 0x08);
    CHECK_EQ(dxball_sdl3_virtual_key(SDLK_LCTRL), 0x11);
    CHECK_EQ(dxball_sdl3_virtual_key(SDLK_RCTRL), 0x11);
    CHECK_EQ(dxball_sdl3_virtual_key(SDLK_RSHIFT), 0x10);
    CHECK_EQ(dxball_sdl3_virtual_key(SDLK_LEFT), 0x25);
    CHECK_EQ(dxball_sdl3_virtual_key(SDLK_DOWN), 0x28);
    CHECK_EQ(dxball_sdl3_virtual_key(SDLK_KP_0), 0x60);
    CHECK_EQ(dxball_sdl3_virtual_key(SDLK_KP_1), 0x61);
    CHECK_EQ(dxball_sdl3_virtual_key(SDLK_KP_9), 0x69);
    CHECK_EQ(dxball_sdl3_virtual_key(SDLK_MENU), 0);
}

static void test_message_queue(void)
{
    DxBallWindowClass window_class;
    DxBallMessage message;
    SDL_Event event;
    DxBallPoint point;
    memset(&window_class, 0, sizeof(window_class));
    window_class.procedure = record;
    dxball_window_register_class(&window_class);

    CHECK_EQ(dxball_window_peek_message(&message, 0, 0, 0, 0), 0);
    dxball_window_post_message(1, 0x10, 0, 0);
    memset(&event, 0, sizeof(event));
    event.type = SDL_EVENT_KEY_DOWN; event.key.key = SDLK_P; event.key.down = true;
    dxball_sdl3_translate_event(&event);
    event.type = SDL_EVENT_KEY_UP; event.key.key = SDLK_LCTRL; event.key.down = false;
    dxball_sdl3_translate_event(&event);

    /* Peek without removal keeps the head; GetMessage then drains in order. */
    CHECK_EQ(dxball_window_peek_message(&message, 0, 0, 0, 0), 1);
    CHECK_EQ(message.message, 0x10);
    CHECK_EQ(dxball_window_get_message(&message, 0, 0, 0), 1);
    CHECK_EQ(message.message, 0x10);
    CHECK_EQ(dxball_window_get_message(&message, 0, 0, 0), 1);
    CHECK_EQ(message.message, 0x100);
    CHECK_EQ(message.wparam, 'P');
    CHECK_EQ(dxball_window_get_message(&message, 0, 0, 0), 1);
    CHECK_EQ(message.message, 0x101);
    CHECK_EQ(message.wparam, 0x11);

    /* Relative motion accumulates and clamps to the 640x480 screen. */
    dxball_set_cursor_position(630, 5);
    memset(&event, 0, sizeof(event));
    event.type = SDL_EVENT_MOUSE_MOTION; event.motion.xrel = 25; event.motion.yrel = -9;
    dxball_sdl3_translate_event(&event);
    dxball_window_get_cursor_pos(&point);
    CHECK_EQ(point.x, 639);
    CHECK_EQ(point.y, 0);
    CHECK_EQ(dxball_window_get_message(&message, 0, 0, 0), 1);
    CHECK_EQ(message.message, 0x200);
    CHECK_EQ(message.lparam, 639);

    memset(&event, 0, sizeof(event));
    event.type = SDL_EVENT_MOUSE_BUTTON_DOWN; event.button.button = SDL_BUTTON_LEFT; event.button.down = true;
    dxball_sdl3_translate_event(&event);
    event.type = SDL_EVENT_MOUSE_BUTTON_UP; event.button.button = SDL_BUTTON_RIGHT; event.button.down = false;
    dxball_sdl3_translate_event(&event);
    event.type = SDL_EVENT_WINDOW_FOCUS_GAINED;
    dxball_sdl3_translate_event(&event);
    event.type = SDL_EVENT_WINDOW_FOCUS_LOST;
    dxball_sdl3_translate_event(&event);
    {
        static const DxBallUInt expected[] = { 0x201, 0x205, 0x1c, 7, 8, 0x1c };
        static const DxBallHandle expected_wparam[] = { 0, 0, 1, 0, 0, 0 };
        size_t i;
        for (i = 0; i < sizeof(expected) / sizeof(expected[0]); ++i) {
            CHECK_EQ(dxball_window_get_message(&message, 0, 0, 0), 1);
            CHECK_EQ(message.message, expected[i]);
            CHECK_EQ(message.wparam, expected_wparam[i]);
        }
    }

    /* WM_CLOSE through DefWindowProc destroys the window (WM_DESTROY), and
       WM_QUIT makes GetMessage return zero with the exit code. */
    message.window = 1; message.message = 0x10; message.wparam = 0; message.lparam = 0;
    seen_count = 0;
    dxball_window_dispatch_message(&message);
    CHECK_EQ(seen_count, 1);
    dxball_window_default_window_proc(1, 0x10, 0, 0);
    CHECK_EQ(seen_count, 2);
    CHECK_EQ(seen[1], 2);
    dxball_window_post_quit_message(7);
    CHECK_EQ(dxball_window_get_message(&message, 0, 0, 0), 0);
    CHECK_EQ(message.wparam, 7);
}

static void test_files(const char *directory)
{
    char path[4096], buffer[16];
    DxBallHandle file, mapping;
    DxBallUInt read_bytes = 99, high = 99;
    FILE *output;
    char *view;
    snprintf(path, sizeof(path), "%s/sample.dat", directory);
    output = fopen(path, "wb");
    fputs("DX-Ball data", output);
    fclose(output);

    file = dxball_file_create(path, 0x80000000UL, 1, NULL, 3, 0x80, 0);
    CHECK(file != (DxBallHandle)-1);
    CHECK_EQ(dxball_file_size(file, &high), 12);
    CHECK_EQ(high, 0);
    memset(buffer, 0, sizeof(buffer));
    CHECK_EQ(dxball_file_read(file, buffer, 16, &read_bytes, NULL), 1);
    CHECK_EQ(read_bytes, 12);
    CHECK(memcmp(buffer, "DX-Ball data", 12) == 0);

    mapping = dxball_midi_create_mapping(file, NULL, 2, 0, 0, NULL);
    CHECK(mapping != 0);
    view = (char *)dxball_midi_map_view(mapping, 4, 0, 0, 0);
    CHECK(view != NULL && memcmp(view, "DX-Ball data", 12) == 0);
    dxball_midi_unmap_view(view);
    CHECK_EQ(dxball_file_close(mapping), 1);
    CHECK_EQ(dxball_file_close(file), 1);

    snprintf(path, sizeof(path), "%s/missing.dat", directory);
    CHECK(dxball_file_create(path, 0x80000000UL, 1, NULL, 3, 0x80, 0) == (DxBallHandle)-1);
}

static void test_memory_and_clock(void)
{
    unsigned char *memory = (unsigned char *)dxball_local_alloc(0x40, 4096);
    DxBallMidiHandle global;
    DxBallCounter frequency, before, after;
    int i, zero = 1;
    for (i = 0; i < 4096; ++i) zero &= memory[i] == 0;
    CHECK(zero);
    CHECK(dxball_local_free(memory) == NULL);

    global = dxball_midi_global_alloc(0x2002, 64);
    CHECK(global != 0);
    CHECK(dxball_midi_global_handle(dxball_midi_global_lock(global)) == global);
    CHECK(dxball_midi_global_free(global) == 0);

    CHECK_EQ(dxball_query_performance_frequency(&frequency), 1);
    CHECK(frequency.low != 0 || frequency.high != 0);
    dxball_query_performance_counter(&before);
    SDL_Delay(2);
    dxball_query_performance_counter(&after);
    CHECK(((unsigned long long)(DxBallUInt)after.high << 32 | after.low) >
          ((unsigned long long)(DxBallUInt)before.high << 32 | before.low));
    CHECK(timeGetTime() - dxball_clock_ops.time_ms() < 2);
}

static void test_instance_lock(const char *directory)
{
    DxBallSecurityAttributes attributes = { 12, NULL, 1 };
    DxBallHandle held;
    setenv("XDG_RUNTIME_DIR", directory, 1);
    CHECK(dxball_window_open_semaphore(2, 0, "dxball-test") == 0);
    held = dxball_window_create_semaphore(&attributes, 0, 1, "dxball-test");
    CHECK(held != 0);
    /* A second claim while the first is held reports a running instance. */
    CHECK(dxball_window_open_semaphore(2, 0, "dxball-test") != 0);
    CHECK_EQ(dxball_file_close(held), 1);
    CHECK(dxball_window_open_semaphore(2, 0, "dxball-test") == 0);
}

int main(void)
{
    char directory[] = "/tmp/dxball-adapter-XXXXXX";
    char path[4096];
    CHECK(mkdtemp(directory) != NULL);
    dxball_bind_sdl3();
    test_virtual_keys();
    test_message_queue();
    test_files(directory);
    test_memory_and_clock();
    test_instance_lock(directory);
    snprintf(path, sizeof(path), "%s/sample.dat", directory); unlink(path);
    snprintf(path, sizeof(path), "%s/dxball-test.lock", directory); unlink(path);
    rmdir(directory);
    return CHECK_DONE();
}
