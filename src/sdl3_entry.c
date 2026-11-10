/* Linux entry: bind SDL3/POSIX services into the recovered import cells,
   then run the original WinMain loop, as windows_entry.c does on Windows. */
#include "sdl3_adapter.h"
#include "sdl3_draw.h"
#include "sdl3_midi.h"
#include "sdl3_sound.h"
#include "allocator.h"
#include "core.h"
#include "gameplay.h"
#include "round.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* DXBALL_TEST_SCRIPT drives automated runs through the normal SDL event path:
   "FRAME:ACTION;..." with ACTION key=NAME, down=NAME, up=NAME, click, rclick,
   move=DX[,DY], shot=FILE.bmp or quit. FRAME counts display refreshes.
   Each shot also prints the game state for the test to check. */
typedef struct Step { long frame; char action[96]; } Step;
static Step steps[128];
static int step_count, step_next;
static long refreshes, release_at;
static Uint8 held_button;
static Uint64 next_refresh;
static int refreshed; /* a refresh wait happened since the last idle check */

static void load_script(const char *script)
{
    while (script != NULL && *script != '\0' && step_count < 128) {
        const char *end = strchr(script, ';');
        size_t length = end != NULL ? (size_t)(end - script) : strlen(script);
        char *colon;
        Step *step = &steps[step_count];
        if (length < sizeof(step->action)) {
            memcpy(step->action, script, length);
            step->action[length] = '\0';
            colon = strchr(step->action, ':');
            if (colon != NULL) {
                step->frame = strtol(step->action, NULL, 10);
                memmove(step->action, colon + 1, strlen(colon + 1) + 1);
                ++step_count;
            }
        }
        script = end != NULL ? end + 1 : NULL;
    }
}

static void push_key(const char *name, int down)
{
    SDL_Event event;
    SDL_zero(event);
    event.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
    event.key.key = SDL_GetKeyFromName(name);
    event.key.down = down != 0;
    if (event.key.key == SDLK_UNKNOWN) fprintf(stderr, "dxball: unknown key %s\n", name);
    else SDL_PushEvent(&event);
}

/* A click holds the button for a few refreshes, as a person does; the game
   samples the button state once per frame. */
static void push_button(Uint8 button, int down)
{
    SDL_Event event;
    SDL_zero(event);
    event.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
    event.button.button = button; event.button.down = down != 0;
    SDL_PushEvent(&event);
    held_button = down ? button : 0;
    release_at = refreshes + 6;
}

static void run_step(const char *action)
{
    SDL_Event event;
    if (strncmp(action, "key=", 4) == 0) { push_key(action + 4, 1); push_key(action + 4, 0); }
    else if (strncmp(action, "down=", 5) == 0) push_key(action + 5, 1);
    else if (strncmp(action, "up=", 3) == 0) push_key(action + 3, 0);
    else if (strcmp(action, "click") == 0) push_button(SDL_BUTTON_LEFT, 1);
    else if (strcmp(action, "rclick") == 0) push_button(SDL_BUTTON_RIGHT, 1);
    else if (strncmp(action, "move=", 5) == 0) {
        const char *comma = strchr(action, ',');
        SDL_zero(event);
        event.type = SDL_EVENT_MOUSE_MOTION;
        event.motion.xrel = strtof(action + 5, NULL);
        event.motion.yrel = comma != NULL ? strtof(comma + 1, NULL) : 0;
        SDL_PushEvent(&event);
    } else if (strncmp(action, "shot=", 5) == 0) {
        if (!dxball_sdl3_save_screenshot(action + 5))
            fprintf(stderr, "dxball: screenshot %s failed: %s\n", action + 5, SDL_GetError());
        printf("dxball-state shot=%s refresh=%ld mode=%d paused=%d score=%d lives=%d board=%d balls=%d\n",
               action + 5, refreshes, dxball_display_mode, dxball_paused, dxball_score,
               dxball_lives, dxball_board_index, dxball_ball_count);
        fflush(stdout);
    } else if (strcmp(action, "quit") == 0) {
        SDL_zero(event);
        event.type = SDL_EVENT_QUIT;
        SDL_PushEvent(&event);
    } else {
        fprintf(stderr, "dxball: unknown script action %s\n", action);
    }
}

/* The original targets a 60 Hz display (its readme calls it "the 60 FPS
   standard"); Flip and WaitForVerticalBlank block on this fixed clock. */
static void wait_refresh(void)
{
    const Uint64 period = SDL_NS_PER_SECOND / 60;
    Uint64 now = SDL_GetTicksNS();
    if (next_refresh == 0 || now > next_refresh + 4 * period) next_refresh = now;
    else if (now < next_refresh) SDL_DelayPrecise(next_refresh - now);
    next_refresh += period;
    refreshed = 1;
    ++refreshes;
    if (held_button != 0 && refreshes >= release_at) push_button(held_button, 0);
    while (step_next < step_count && steps[step_next].frame <= refreshes)
        run_step(steps[step_next++].action);
}

/* Message-loop idle: show pending primary changes. Loops that never wait for
   a refresh (pause spins on dispatch_frame) are paced to the display clock
   instead of consuming a whole CPU; game timing is clock-based. */
static void idle(void)
{
    dxball_sdl3_draw_flush();
    if (!refreshed) wait_refresh();
    refreshed = 0;
}

int main(int argc, char **argv)
{
    DxBallHandle status;
    FILE *probe = fopen("intro.pcx", "rb");
    (void)argc;
    /* Recovered loaders read files without checking that they opened. */
    if (probe == NULL) {
        fprintf(stderr, "dxball: game data (intro.pcx, ...) not found in the current directory.\n"
                        "Run scripts/run-linux.py, or start dxball from its prepared folder.\n");
        return 1;
    }
    fclose(probe);
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        fprintf(stderr, "dxball: %s\n", SDL_GetError());
        return 1;
    }
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO))
        fprintf(stderr, "dxball: no audio: %s\n", SDL_GetError());
    load_script(getenv("DXBALL_TEST_SCRIPT"));

    dxball_bind_sdl3();
    dxball_bind_sdl3_midi();
    dxball_draw_factory_backend = dxball_sdl3_direct_draw_create;
    dxball_sound_api.create_device = dxball_sdl3_sound_create;
    dxball_sdl3_draw_present = dxball_sdl3_present;
    dxball_sdl3_draw_vblank = wait_refresh;
    dxball_sdl3_idle = idle;
    dxball_sdl3_fluidsynth_start();
    dxball_sdl3_midi_open_output();
    if (dxball_runtime_heap == NULL) dxball_initialize_runtime_heap();

    status = dxball_win_main(1, 0, (DxBallHandle)argv, 1);
    SDL_Quit();
    return (int)status;
}
