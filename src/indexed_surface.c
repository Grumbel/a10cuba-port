#include "indexed_surface.h"

bool indexed_surface_init(IndexedSurface *indexed, int width, int height)
{
    if (!indexed || width <= 0 || height <= 0) {
        return SDL_SetError("Invalid indexed surface dimensions");
    }
    indexed->surface = SDL_CreateSurface(width, height, SDL_PIXELFORMAT_INDEX8);
    if (!indexed->surface) {
        return false;
    }
    if (!SDL_CreateSurfacePalette(indexed->surface)) {
        SDL_DestroySurface(indexed->surface);
        indexed->surface = NULL;
        return false;
    }
    return true;
}

void indexed_surface_destroy(IndexedSurface *indexed)
{
    if (indexed) {
        SDL_DestroySurface(indexed->surface);
        indexed->surface = NULL;
    }
}

bool indexed_surface_set_palette(IndexedSurface *indexed, const SDL_Color *colors,
                                 int first, int count)
{
    if (!indexed || !indexed->surface || !colors || first < 0 || count < 0 ||
        first > 256 || count > 256 - first) {
        return SDL_SetError("Invalid indexed palette range");
    }
    return SDL_SetPaletteColors(SDL_GetSurfacePalette(indexed->surface), colors, first, count);
}

bool indexed_surface_upload(const IndexedSurface *indexed, SDL_Texture *texture)
{
    if (!indexed || !indexed->surface || !texture) {
        return SDL_SetError("Invalid indexed surface or texture");
    }
    float width, height;
    SDL_PropertiesID properties = SDL_GetTextureProperties(texture);
    if (!properties || !SDL_GetTextureSize(texture, &width, &height)) {
        return false;
    }
    if (width != indexed->surface->w || height != indexed->surface->h ||
        SDL_GetNumberProperty(properties, SDL_PROP_TEXTURE_FORMAT_NUMBER, 0) != SDL_PIXELFORMAT_RGBA32) {
        return SDL_SetError("Texture must match the indexed surface dimensions and RGBA32 format");
    }
    SDL_Surface *rgba = SDL_ConvertSurface(indexed->surface, SDL_PIXELFORMAT_RGBA32);
    if (!rgba) {
        return false;
    }
    bool result = SDL_UpdateTexture(texture, NULL, rgba->pixels, rgba->pitch);
    SDL_DestroySurface(rgba);
    return result;
}
