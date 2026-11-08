#include "indexed_surface.h"
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
    IndexedSurface indexed = {0};

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
    if (bitmap->format == SDL_PIXELFORMAT_INDEX8) {
        SDL_Palette *palette = SDL_GetSurfacePalette(bitmap);
        if (!palette || !indexed_surface_init(&indexed, bitmap->w, bitmap->h) ||
            !indexed_surface_set_palette(&indexed, palette->colors, 0, palette->ncolors)) {
            fail("Create indexed surface");
            goto cleanup;
        }
        for (int y = 0; y < bitmap->h; ++y) {
            memcpy((Uint8 *)indexed.surface->pixels + y * indexed.surface->pitch,
                   (Uint8 *)bitmap->pixels + y * bitmap->pitch, (size_t)bitmap->w);
        }
        texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32,
                                    SDL_TEXTUREACCESS_STREAMING, bitmap->w, bitmap->h);
        if (!texture || !indexed_surface_upload(&indexed, texture)) {
            fail("Upload indexed surface");
            goto cleanup;
        }
    } else {
        texture = SDL_CreateTextureFromSurface(renderer, bitmap);
    }
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
    indexed_surface_destroy(&indexed);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_DestroySurface(bitmap);
    SDL_Quit();
    return result;
}
