#include <SDL3/SDL.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

static int fail(const char *operation)
{
    fprintf(stderr, "%s: %s\n", operation, SDL_GetError());
    return 1;
}

int main(int argc, char **argv)
{
    const char *snapshot = NULL;
    if (argc == 4 && strcmp(argv[2], "--snapshot") == 0) {
        snapshot = argv[3];
    } else if (argc != 2) {
        fprintf(stderr, "Usage: %s IMAGE.bmp [--snapshot OUTPUT.bmp]\n", argv[0]);
        return 2;
    }

    int result = 1;
    SDL_Surface *bitmap = NULL;
    SDL_Surface *capture = NULL;
    SDL_Window *window = NULL;
    SDL_Renderer *renderer = NULL;
    SDL_Texture *texture = NULL;

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        return fail("SDL_Init");
    }
    bitmap = SDL_LoadBMP(argv[1]);
    if (!bitmap) {
        fail("SDL_LoadBMP");
        goto cleanup;
    }
    printf("Loaded %dx%d bitmap (%s, pitch %d)\n", bitmap->w, bitmap->h,
           SDL_GetPixelFormatName(bitmap->format), bitmap->pitch);
    window = SDL_CreateWindow("A-10 Cuba! bitmap probe", bitmap->w, bitmap->h,
                              snapshot ? SDL_WINDOW_HIDDEN : 0);
    if (!window) {
        fail("SDL_CreateWindow");
        goto cleanup;
    }
    renderer = SDL_CreateRenderer(window, NULL);
    if (!renderer) {
        fail("SDL_CreateRenderer");
        goto cleanup;
    }
    texture = SDL_CreateTextureFromSurface(renderer, bitmap);
    if (!texture) {
        fail("SDL_CreateTextureFromSurface");
        goto cleanup;
    }
    if (!SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_NONE) ||
        !SDL_RenderClear(renderer) || !SDL_RenderTexture(renderer, texture, NULL, NULL)) {
        fail("SDL render");
        goto cleanup;
    }
    if (snapshot) {
        capture = SDL_RenderReadPixels(renderer, NULL);
        if (!capture || !SDL_SaveBMP(capture, snapshot)) {
            fail("Save rendered snapshot");
            goto cleanup;
        }
    }
    if (!SDL_RenderPresent(renderer)) {
        fail("SDL_RenderPresent");
        goto cleanup;
    }
    if (!snapshot) {
        bool running = true;
        while (running) {
            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                if (event.type == SDL_EVENT_QUIT ||
                    (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE)) {
                    running = false;
                }
            }
            if (!SDL_RenderClear(renderer) ||
                !SDL_RenderTexture(renderer, texture, NULL, NULL) ||
                !SDL_RenderPresent(renderer)) {
                fail("SDL render");
                goto cleanup;
            }
            SDL_Delay(10);
        }
    }
    result = 0;
cleanup:
    SDL_DestroySurface(capture);
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_DestroySurface(bitmap);
    SDL_Quit();
    return result;
}
