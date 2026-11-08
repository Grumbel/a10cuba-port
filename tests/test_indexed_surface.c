#include "indexed_surface.h"
#include <SDL3/SDL.h>
#include <stdio.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "%s:%d: %s: %s\n", __FILE__, __LINE__, #condition, SDL_GetError()); \
        goto cleanup; \
    } \
} while (0)

static bool pixel_is(SDL_Surface *surface, int x, int y, Uint8 r, Uint8 g, Uint8 b)
{
    Uint8 actual_r, actual_g, actual_b, actual_a;
    return SDL_ReadSurfacePixel(surface, x, y, &actual_r, &actual_g, &actual_b, &actual_a) &&
           actual_r == r && actual_g == g && actual_b == b && actual_a == 255;
}

int main(void)
{
    int result = 1;
    IndexedSurface indexed = {0};
    SDL_Window *window = NULL;
    SDL_Renderer *renderer = NULL;
    SDL_Texture *texture = NULL;
    SDL_Texture *wrong_size = NULL;
    SDL_Surface *readback = NULL;
    SDL_Color palette[] = {{0, 0, 0, 255}, {255, 0, 0, 255}, {0, 255, 0, 255}};
    SDL_Color blue = {0, 0, 255, 255};

    CHECK(SDL_Init(SDL_INIT_VIDEO));
    CHECK(indexed_surface_init(&indexed, 3, 2));
    CHECK(indexed.surface->pitch >= 3);
    CHECK(indexed_surface_set_palette(&indexed, palette, 0, 3));
    CHECK(!indexed_surface_set_palette(&indexed, &blue, 256, 1));
    Uint8 *pixels = indexed.surface->pixels;
    pixels[0] = 1;
    pixels[1] = 2;
    pixels[2] = 1;
    pixels[indexed.surface->pitch] = 2;
    pixels[indexed.surface->pitch + 1] = 1;
    pixels[indexed.surface->pitch + 2] = 2;

    window = SDL_CreateWindow("indexed test", 3, 2, SDL_WINDOW_HIDDEN);
    CHECK(window);
    renderer = SDL_CreateRenderer(window, NULL);
    CHECK(renderer);
    texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32,
                                SDL_TEXTUREACCESS_STREAMING, 3, 2);
    CHECK(texture);
    wrong_size = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32,
                                   SDL_TEXTUREACCESS_STREAMING, 4, 2);
    CHECK(wrong_size);
    CHECK(!indexed_surface_upload(&indexed, wrong_size));
    CHECK(SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_NONE));
    CHECK(indexed_surface_upload(&indexed, texture));
    CHECK(SDL_RenderClear(renderer));
    CHECK(SDL_RenderTexture(renderer, texture, NULL, NULL));
    readback = SDL_RenderReadPixels(renderer, NULL);
    CHECK(readback);
    CHECK(pixel_is(readback, 0, 0, 255, 0, 0));
    CHECK(pixel_is(readback, 0, 1, 0, 255, 0));
    SDL_DestroySurface(readback);
    readback = NULL;

    /* Change a palette entry without changing its pixel indices. */
    CHECK(indexed_surface_set_palette(&indexed, &blue, 1, 1));
    CHECK(indexed_surface_upload(&indexed, texture));
    CHECK(SDL_RenderClear(renderer));
    CHECK(SDL_RenderTexture(renderer, texture, NULL, NULL));
    readback = SDL_RenderReadPixels(renderer, NULL);
    CHECK(readback);
    CHECK(pixel_is(readback, 0, 0, 0, 0, 255));
    CHECK(pixel_is(readback, 1, 0, 0, 255, 0));
    CHECK(pixel_is(readback, 1, 1, 0, 0, 255));
    result = 0;
cleanup:
    SDL_DestroySurface(readback);
    SDL_DestroyTexture(wrong_size);
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    indexed_surface_destroy(&indexed);
    SDL_Quit();
    return result;
}
