#ifndef OPENCMR2_VECTOR_SPRITE_H
#define OPENCMR2_VECTOR_SPRITE_H
#include "port/gfx.h"

#ifdef __cplusplus
extern "C" {
#endif

// Traces only the original co-driver and menu icon textures. Coordinates and
// native pixels stay unchanged. [video] vector_icons = 0 keeps the bitmaps.
void VectorSprite_Replace(const char *name, GfxTexture *texture);

#ifdef __cplusplus
}
#endif
#endif
