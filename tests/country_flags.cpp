// Exercise the real sprite queues and flag mesh without game assets or a GPU.
// Pixel output is checked separately with both OpenGL and SDL_GPU.
#include "port/types.h"
#include "port/gfx.h"
#include "Frontend.h"
#include "Graphics.h"
#include "Game.h"
#include "Sprite.h"

#include <SDL3/SDL.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

extern void Sprite_DrawLayer(int);
extern void Scene_InitFixedMathTables();
extern void FrontendDraw_AnimatedMatrixBackground(short, short, char *);
extern char g_matrixMaps[11][216];
extern GfxTLVertex g_spriteVerts1[1024 * 4];

static void Check(bool ok, const char *message)
{
    if (!ok) {
        fprintf(stderr, "country flags: %s\n", message);
        exit(1);
    }
}

static bool Same(const GfxTLVertex &a, const GfxTLVertex &b)
{
    return memcmp(&a, &b, sizeof(a)) == 0;
}

static void CheckSpriteInterleaving()
{
    Texture texture{};
    texture.width = texture.height = 32;
    texture.pSurface = Gfx_CreateTexture(32, 32, GFX_FORMAT_BGRA8, 1, 0);
    SpriteRect src{ 0, 0, 28, 18 }, first{ 10, 20, 56, 36 }, last{ 300, 200, 56, 36 };
    BYTE white[] = { 255, 255, 255, 255 };
    Sprite_Queue(&src, &first, &texture, 1, 0, nullptr, nullptr, white, 8);
    Sprite_Queue(&src, &last, &texture, 1, 0, nullptr, nullptr, white, 8);
    Sprite_DrawLayer(1);
    GfxTLVertex baseline[8];
    memcpy(baseline, g_spriteVerts1, sizeof(baseline));
    GfxTLVertex quad[4] = {};
    for (int i = 0; i < 4; i++) {
        quad[i].sx = 100.25f + (i % 2) * 80;
        quad[i].sy = 110.75f + (i / 2) * 40 + (i % 2) * 3;
        quad[i].rhw = 1;
        quad[i].color = GFX_RGBA(20 + i * 50, 30, 180, 128);
        quad[i].tu = (i % 2) * 0.875f;
        quad[i].tv = (i / 2) * 0.5625f;
    }
    Sprite_Queue(&src, &first, &texture, 1, 0, nullptr, nullptr, white, 8);
    Sprite_QueueQuad(&texture, quad, 1);
    Sprite_Queue(&src, &last, &texture, 1, 0, nullptr, nullptr, white, 8);
    Check(g_spriteCount1 == 3, "ordinary and deformed sprites share the same queue");
    Sprite_DrawLayer(1);
    for (int i = 0; i < 4; i++)
        Check(Same(g_spriteVerts1[4 + i], quad[i]), "drawing preserves deformed corners, tint and fractional UVs");
    Check(memcmp(baseline, g_spriteVerts1, 4 * sizeof(GfxTLVertex)) == 0 &&
          memcmp(baseline + 4, g_spriteVerts1 + 8, 4 * sizeof(GfxTLVertex)) == 0,
          "ordinary bitmap sprites surrounding a deformed quad retain their exact geometry");
    Gfx_Present(FALSE);
    Gfx_DestroyTexture(texture.pSurface);
}

static void CheckBackgrounds(Texture *matrix)
{
    for (int flag = 0; flag < 11; flag++) {
        FrontendDraw_AnimatedMatrixBackground(160, 100, g_matrixMaps[flag]);
        Check(g_spriteCount1 == 216, "flag retains the original background's sprite budget");
        Check(g_spriteLayer1[0].pTexture != matrix, "each rally and language uses a vector flag");
        for (int y = 0; y < 12; y++) {
            for (int x = 0; x < 18; x++) {
                const GfxTLVertex *quad = g_spriteVerts1 + (y * 18 + x) * 4;
                if (x < 17) {
                    const GfxTLVertex *next = quad + 4;
                    Check(Same(quad[1], next[0]) && Same(quad[3], next[2]), "horizontal mesh edges join exactly");
                }
                if (y < 11) {
                    const GfxTLVertex *next = quad + 18 * 4;
                    Check(Same(quad[2], next[0]) && Same(quad[3], next[1]), "vertical mesh edges join exactly");
                }
            }
        }
        std::vector<GfxTLVertex> queued(g_spriteVerts1, g_spriteVerts1 + 216 * 4);
        Sprite_DrawLayer(1);
        Check(memcmp(queued.data(), g_spriteVerts1, queued.size() * sizeof(GfxTLVertex)) == 0,
              "sprite drawing preserves the continuous flag mesh");
        Gfx_Present(FALSE);
    }
    char otherMap[216] = {};
    FrontendDraw_AnimatedMatrixBackground(160, 100, otherMap);
    Check(g_spriteCount1 == 216 && g_spriteLayer1[0].pTexture == matrix,
          "other matrix backgrounds keep the original drawing path");
    Sprite_DrawLayer(1);
    Gfx_Present(FALSE);
}

int main()
{
    SDL_SetEnvironmentVariable(SDL_GetEnvironment(), "OPENCMR2_HEADLESS", "1", true);
    Scene_InitFixedMathTables();
    Graphics graphics{};
    graphics.resX = 640;
    graphics.resY = 480;
    g_pGraphics = &graphics;
    Check(Gfx_SetVideoMode(640, 480, FALSE), "initialise headless renderer");
    CheckSpriteInterleaving();
    for (int mode = 0; mode < 2; mode++) {
        Texture matrix{};
        matrix.width = matrix.height = 16;
        matrix.pSurface = Gfx_CreateTexture(16, 16, GFX_FORMAT_BGRA8, 1, 0);
        CFrontend::m_pLgMatrixTexture = &matrix;
        CheckBackgrounds(&matrix);
        Gfx_Shutdown();
        if (mode == 0)
            Check(Gfx_SetVideoMode(1920, 1080, FALSE), "recreate renderer after resolution change");
    }
    CFrontend::m_pLgMatrixTexture = nullptr;
    CGame::UnwindCallbacks(0);
    g_pGraphics = nullptr;
    puts("country flags: original bitmap sprites, mixed quads, continuous meshes and device recreation passed");
}
