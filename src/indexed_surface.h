#ifndef A10_INDEXED_SURFACE_H
#define A10_INDEXED_SURFACE_H

#include <SDL3/SDL.h>

/* Zero-initialize before init; destroy before reinitializing. Use surface->pitch for pixel rows. */
typedef struct {
    SDL_Surface *surface;
} IndexedSurface;

bool indexed_surface_init(IndexedSurface *indexed, int width, int height);
void indexed_surface_destroy(IndexedSurface *indexed);
bool indexed_surface_set_palette(IndexedSurface *indexed, const SDL_Color *colors,
                                 int first, int count);
/* Upload to a matching-size RGBA32 texture; repeat after pixel or palette changes. */
bool indexed_surface_upload(const IndexedSurface *indexed, SDL_Texture *texture);

#endif
