#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <time.h>
#include <signal.h>
#include <SDL.h>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif
#include "pac.h"

#define CONTROLLER_DEADZONE 8000

static bool should_quit = false;
static bool has_focus = true;
static bool is_paused = false;
static int speed = 1;

static SDL_Renderer* renderer = NULL;
static SDL_Texture* texture = NULL;
static SDL_GameController* controller = NULL;
static SDL_AudioDeviceID audio_device;

static pac* p;
static SDL_Event e;
static uint32_t current_time = 0;
static uint32_t last_time = 0;
static uint32_t dt = 0;

static void update_screen(pac* const p) {
    int pitch = 0;
    void* pixels = NULL;
    if (SDL_LockTexture(texture, NULL, &pixels, &pitch) != 0) {
        SDL_Log("Unable to lock texture: %s", SDL_GetError());
    }
    else {
        memcpy(pixels, p->screen_buffer, pitch * SCREEN_HEIGHT);
    }
    SDL_UnlockTexture(texture);
}

static void push_sample(pac* const p, int16_t sample) {
    SDL_QueueAudio(audio_device, &sample, sizeof(int16_t) * 1);
}

static void send_quit_event() {
    should_quit = true;
}

static void screenshot(pac* const p) {
    // generate filename
    time_t t = time(NULL);
    struct tm tm = *localtime(&t);

    char *filename = (char*) calloc(50, sizeof(char));

    sprintf(filename,"%d-%d-%d %d.%d.%d - %s.bmp",
        tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
        tm.tm_hour, tm.tm_min, tm.tm_sec,
        "pac");

    // if file already exists, we don't want to erase it
    FILE *f = fopen(filename, "r");
    if (f) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "Cannot save screenshot: file %s already exists", filename);
        return;
    }
    fclose(f);

    // render screen buffer to BMP file
    const uint32_t pitch = sizeof(uint8_t) * 3 * SCREEN_WIDTH;
    const uint8_t depth = 32;
    SDL_Surface* s = SDL_CreateRGBSurfaceWithFormatFrom(
        p->screen_buffer, SCREEN_WIDTH, SCREEN_HEIGHT,
        depth, pitch, SDL_PIXELFORMAT_RGB24);
    SDL_SaveBMP(s, filename);
    SDL_FreeSurface(s);

    SDL_Log("Saved screenshot: %s", filename);
    free(filename);
}

static void mainloop(void) {
    current_time = SDL_GetTicks();
    dt = current_time - last_time;

    while (SDL_PollEvent(&e) != 0) {
        if (e.type == SDL_QUIT) {
            should_quit = true;
        }
        else if (e.type == SDL_WINDOWEVENT) {
            if (e.window.event == SDL_WINDOWEVENT_FOCUS_GAINED) {
                has_focus = true;
            }
            else if (e.window.event == SDL_WINDOWEVENT_FOCUS_LOST) {
                has_focus = false;
            }
        }
        else if (e.type == SDL_KEYDOWN) {
            switch (e.key.keysym.sym) {
                case SDLK_RETURN: case SDLK_1: p->p1_start = 1; break; // start (1p)
                case SDLK_2: p->p2_start = 1; break; // start (2p)
                case SDLK_UP: p->p1_up = 1; break; // up
                case SDLK_DOWN: p->p1_down = 1; break; // down
                case SDLK_LEFT: p->p1_left = 1; break; // left
                case SDLK_RIGHT: p->p1_right = 1; break; // right
                case SDLK_c: p->coin_s1 = 1; break; // coin
                case SDLK_v: p->coin_s2 = 1; break; // coin (slot 2)
                case SDLK_t: p->board_test = 1; break; // board test

                case SDLK_m: p->mute_audio = !p->mute_audio; break;
                case SDLK_p: is_paused = !is_paused; break;
                case SDLK_s: screenshot(p); break;
                case SDLK_i: pac_cheat_invincibility(p); break;
                case SDLK_TAB: speed = 5; break;
            }
        }
        else if (e.type == SDL_KEYUP) {
            switch (e.key.keysym.sym) {
                case SDLK_RETURN: case SDLK_1: p->p1_start = 0; break; // start (1p)
                case SDLK_2: p->p2_start = 0; break; // start (2p)
                case SDLK_UP: p->p1_up = 0; break; // up
                case SDLK_DOWN: p->p1_down = 0; break; // down
                case SDLK_LEFT: p->p1_left = 0; break; // left
                case SDLK_RIGHT: p->p1_right = 0; break; // right
                case SDLK_c: p->coin_s1 = 0; break; // coin
                case SDLK_v: p->coin_s2 = 0; break; // coin (slot 2)
                case SDLK_t: p->board_test = 0; break; // board test
                case SDLK_TAB:
                    speed = 1;
                    // clear the queued audio to avoid audio delays
                    SDL_ClearQueuedAudio(audio_device);
                break;
            }
        }
        else if (e.type == SDL_CONTROLLERBUTTONDOWN) {
            switch (e.cbutton.button) {
            case SDL_CONTROLLER_BUTTON_A: p->coin_s1 = 1; break;

            case SDL_CONTROLLER_BUTTON_DPAD_UP: p->p1_up = 1; break;
            case SDL_CONTROLLER_BUTTON_DPAD_DOWN: p->p1_down = 1; break;
            case SDL_CONTROLLER_BUTTON_DPAD_LEFT: p->p1_left = 1; break;
            case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: p->p1_right = 1; break;

            case SDL_CONTROLLER_BUTTON_START: p->p1_start = 1; break;
            case SDL_CONTROLLER_BUTTON_BACK: p->p2_start = 1; break;
            }
        }
        else if (e.type == SDL_CONTROLLERBUTTONUP) {
            switch (e.cbutton.button) {
            case SDL_CONTROLLER_BUTTON_A: p->coin_s1 = 0; break;

            case SDL_CONTROLLER_BUTTON_DPAD_UP: p->p1_up = 0; break;
            case SDL_CONTROLLER_BUTTON_DPAD_DOWN: p->p1_down = 0; break;
            case SDL_CONTROLLER_BUTTON_DPAD_LEFT: p->p1_left = 0; break;
            case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: p->p1_right = 0; break;

            case SDL_CONTROLLER_BUTTON_START: p->p1_start = 0; break;
            case SDL_CONTROLLER_BUTTON_BACK: p->p2_start = 0; break;
            }
        }
        else if (e.type == SDL_CONTROLLERAXISMOTION) {
            switch (e.caxis.axis) {
            case SDL_CONTROLLER_AXIS_LEFTX:
                if (e.caxis.value < -CONTROLLER_DEADZONE) {
                    p->p1_left = 1;
                }
                else if (e.caxis.value > CONTROLLER_DEADZONE) {
                    p->p1_right = 1;
                }
                else {
                    p->p1_left = 0;
                    p->p1_right = 0;
                }
                break;
            case SDL_CONTROLLER_AXIS_LEFTY:
                if (e.caxis.value < -CONTROLLER_DEADZONE) {
                    p->p1_up = 1;
                }
                else if (e.caxis.value > CONTROLLER_DEADZONE) {
                    p->p1_down = 1;
                }
                else {
                    p->p1_up = 0;
                    p->p1_down = 0;
                }
                break;
            }
        }
        else if (e.type == SDL_CONTROLLERDEVICEADDED) {
            const int controller_id = e.cdevice.which;
            if (controller == NULL && SDL_IsGameController(controller_id)) {
                controller = SDL_GameControllerOpen(controller_id);
            }
        }
        else if (e.type == SDL_CONTROLLERDEVICEREMOVED) {
            if (controller != NULL) {
                SDL_GameControllerClose(controller);
                controller = NULL;
            }
        }
    }

    if (!is_paused && has_focus) {
        pac_update(p, dt * speed);
    }

    SDL_RenderClear(renderer);
    SDL_RenderCopy(renderer, texture, NULL, NULL);
    SDL_RenderPresent(renderer);

    last_time = current_time;
}

int main(int argc, char** argv) {
    signal(SIGINT, send_quit_event);

    // SDL init
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) != 0) {
        SDL_Log("Unable to initialise SDL: %s", SDL_GetError());
        return 1;
    }

    SDL_SetHint(SDL_HINT_BMP_SAVE_LEGACY_FORMAT, "1");

    // create SDL window
    SDL_Window* window = SDL_CreateWindow("pac",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        SCREEN_WIDTH * 2,
        SCREEN_HEIGHT * 2,
        SDL_WINDOW_RESIZABLE);

    if (window == NULL) {
        SDL_Log("Unable to create window: %s", SDL_GetError());
        return 1;
    }

    SDL_SetWindowMinimumSize(window, SCREEN_WIDTH, SCREEN_HEIGHT);

    // create renderer
    renderer = SDL_CreateRenderer(
        window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (renderer == NULL) {
        SDL_Log("Unable to create renderer: %s", SDL_GetError());
        return 1;
    }

    SDL_RenderSetLogicalSize(renderer, SCREEN_WIDTH, SCREEN_HEIGHT);

    // print info on renderer:
    SDL_RendererInfo renderer_info;
    SDL_GetRendererInfo(renderer, &renderer_info);
    SDL_Log("Using renderer %s", renderer_info.name);

    texture = SDL_CreateTexture(
        renderer,
        SDL_PIXELFORMAT_RGB24,
        SDL_TEXTUREACCESS_STREAMING,
        SCREEN_WIDTH,
        SCREEN_HEIGHT);
    if (texture == NULL) {
        SDL_Log("Unable to create texture: %s", SDL_GetError());
        return 1;
    }

    // audio init
    SDL_AudioSpec audio_spec;
    SDL_zero(audio_spec);
    audio_spec.freq = 44100;
    audio_spec.format = AUDIO_S16SYS;
    audio_spec.channels = 1;
    audio_spec.samples = 1024;
    audio_spec.callback = NULL;

    audio_device = SDL_OpenAudioDevice(
        NULL, 0, &audio_spec, NULL, 0);

    if (audio_device == 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "failed to open audio: %s", SDL_GetError());
    }
    else {
        const char* driver_name = SDL_GetCurrentAudioDriver();
        SDL_Log("audio device has been opened (%s)", driver_name);
    }

    SDL_PauseAudioDevice(audio_device, 0); // start playing

    // controller init: opening the first available controller
    controller = NULL;
    for (int i = 0; i < SDL_NumJoysticks(); i++) {
        if (SDL_IsGameController(i)) {
            controller = SDL_GameControllerOpen(i);
            if (controller) {
                SDL_Log("game controller detected: %s",
                    SDL_GameControllerNameForIndex(i));
                break;
            }
            else {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                    "could not open game controller: %s", SDL_GetError());
            }
        }
    }

    // pac init
    char* base_path = SDL_GetBasePath();
    char* rom_dir = argc > 1 ? argv[1] : base_path;

    p = calloc(1, sizeof(pac));
    if (pac_init(p, rom_dir) != 0) {
        return 1;
    }
    p->sample_rate = audio_spec.freq;
    p->push_sample = push_sample;
    p->update_screen = update_screen;
    update_screen(p);

    SDL_free(base_path);

    // main loop
    current_time = SDL_GetTicks();
    last_time = SDL_GetTicks();
    #ifdef __EMSCRIPTEN__
    emscripten_set_main_loop(mainloop, 0, 1);
    #else
    while (!should_quit) {
        mainloop();
    }
    #endif

    pac_quit(p);
    free(p);

    if (controller != NULL) {
        SDL_GameControllerClose(controller);
    }

    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_CloseAudioDevice(audio_device);
    SDL_Quit();

    return 0;
}
