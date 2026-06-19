#define SDL_MAIN_HANDLED 1

#include <SDL3/SDL.h>
#include <Process.h>
#include <stdint.h>
#include <stdio.h>

#define SDLTEST_WIDTH 720
#define SDLTEST_HEIGHT 520
#define SDLTEST_FRAME_MS 16u

static void draw_grid(SDL_Renderer *renderer)
{
    SDL_SetRenderDrawColor(renderer, 30, 38, 45, 255);
    for (int x = 0; x <= SDLTEST_WIDTH; x += 40) {
        SDL_RenderLine(renderer, (float)x, 0.0f, (float)x, (float)SDLTEST_HEIGHT);
    }
    for (int y = 0; y <= SDLTEST_HEIGHT; y += 40) {
        SDL_RenderLine(renderer, 0.0f, (float)y, (float)SDLTEST_WIDTH, (float)y);
    }
}

static void draw_points(SDL_Renderer *renderer, int frame)
{
    SDL_SetRenderDrawColor(renderer, 242, 214, 107, 255);
    for (int i = 0; i < 96; ++i) {
        int x = (i * 67 + frame * 3) % SDLTEST_WIDTH;
        int y = (i * 41 + frame * 2) % SDLTEST_HEIGHT;
        SDL_RenderPoint(renderer, (float)x, (float)y);
    }
}

static void render_frame(SDL_Renderer *renderer, int frame, float mouse_x, float mouse_y)
{
    SDL_SetRenderDrawColor(renderer, 18, 24, 31, 255);
    SDL_RenderClear(renderer);

    draw_grid(renderer);

    SDL_FRect panel = { 34.0f, 34.0f, 652.0f, 452.0f };
    SDL_SetRenderDrawColor(renderer, 42, 53, 63, 255);
    SDL_RenderFillRect(renderer, &panel);
    SDL_SetRenderDrawColor(renderer, 94, 117, 132, 255);
    SDL_RenderRect(renderer, &panel);

    SDL_FRect red = { 72.0f, 82.0f, 180.0f, 120.0f };
    SDL_SetRenderDrawColor(renderer, 224, 82, 82, 255);
    SDL_RenderFillRect(renderer, &red);

    SDL_FRect cyan = { 292.0f, 104.0f, 160.0f, 92.0f };
    SDL_SetRenderDrawColor(renderer, 71, 173, 183, 255);
    SDL_RenderFillRect(renderer, &cyan);
    SDL_SetRenderDrawColor(renderer, 210, 238, 240, 255);
    SDL_RenderRect(renderer, &cyan);

    SDL_FRect moving = {
        98.0f + (float)((frame * 5) % 380),
        268.0f,
        86.0f,
        64.0f
    };
    SDL_SetRenderDrawColor(renderer, 244, 181, 73, 255);
    SDL_RenderFillRect(renderer, &moving);

    SDL_SetRenderDrawColor(renderer, 238, 238, 220, 255);
    SDL_RenderLine(renderer, 70.0f, 408.0f, 650.0f, 108.0f);
    SDL_RenderLine(renderer, 70.0f, 108.0f, 650.0f, 408.0f);

    SDL_FRect mouse_rect = { mouse_x - 18.0f, mouse_y - 18.0f, 36.0f, 36.0f };
    SDL_SetRenderDrawColor(renderer, 126, 205, 122, 255);
    SDL_RenderRect(renderer, &mouse_rect);

    draw_points(renderer, frame);
    SDL_RenderPresent(renderer);
}

int main(void)
{
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        printf("SDL init failed: %s\n", SDL_GetError());
        return 1;
    }

    SDL_Window *window = SDL_CreateWindow("SDL 2D Test", SDLTEST_WIDTH, SDLTEST_HEIGHT, 0);
    if (!window) {
        printf("SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    SDL_Renderer *renderer = SDL_CreateRenderer(window, NULL);
    if (!renderer) {
        printf("SDL_CreateRenderer failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    int frame = 0;
    float mouse_x = SDLTEST_WIDTH / 2.0f;
    float mouse_y = SDLTEST_HEIGHT / 2.0f;
    bool running = true;

    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
            case SDL_EVENT_QUIT:
                running = false;
                break;
            case SDL_EVENT_KEY_DOWN:
                if (event.key.scancode == SDL_SCANCODE_ESCAPE ||
                    event.key.scancode == SDL_SCANCODE_Q) {
                    running = false;
                }
                break;
            case SDL_EVENT_MOUSE_MOTION:
                mouse_x = event.motion.x;
                mouse_y = event.motion.y;
                break;
            default:
                break;
            }
        }

        render_frame(renderer, frame++, mouse_x, mouse_y);
        SDL_Delay(SDLTEST_FRAME_MS);
    }

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}

void _start(void)
{
    int status = main();
    process_exit((int32_t)status);
    for (;;) {
        process_yield();
    }
}
