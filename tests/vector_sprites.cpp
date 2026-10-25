// Trace synthetic versions of the actual UI layouts, without game assets.
#include "port/vector_sprite.h"
#include "render/gfx_internal.h"
#include <SDL3/SDL.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

static void Check(bool ok, const char *message) {
    if (!ok) {
        fprintf(stderr, "vector sprites: %s\n", message);
        exit(1);
    }
}
static GfxTexture *Texture(int w, int h) {
    return Gfx_CreateTexture(w, h, GFX_FORMAT_BGRA8, 1, 0);
}
static void Pixel(GfxTexture *t, int x, int y, uint32_t colour) {
    memcpy(t->levels[0][0].data() + ((size_t)y * t->width + x) * 4, &colour, 4);
}
static const uint8_t *Field(GfxTexture *t, float x, float y, int band = 0) {
    GfxTexture *drawn = Gfx_DrawnTexture(t);
    int scale = Gfx_GetDistanceFieldScale(t);
    int px = int(x * scale), py = int(y * scale) + band * drawn->height / 3;
    return drawn->levels[0][0].data() + ((size_t)py * drawn->width + px) * 4;
}
static void CheckReplacement(GfxTexture *texture, const std::vector<uint8_t> &original) {
    GfxTexture *drawn = Gfx_DrawnTexture(texture);
    Check(drawn != texture && (drawn->flags & GFX_TEXTURE_VECTOR_ICON), "supported icon uses traced fields");
    Check(texture->levels[0][0] == original, "native source pixels remain unchanged");
    int scale = Gfx_GetDistanceFieldScale(texture);
    Check(scale >= 2 && drawn->width == texture->width * scale &&
              drawn->height == texture->height * scale * 3,
          "source coordinates cover the same cells, with two separate palette bands");
    Check(Gfx_GetSpriteTexelInset(texture) == .5f / scale, "scaled sprites use the field's own texel inset");
}
static void Monochrome() {
    auto *box = Texture(16, 16);
    for (int y = 2; y < 13; y++)
        for (int x = 2; x < 13; x++)
            if (x < 4 || x >= 11 || y < 4 || y >= 11)
                Pixel(box, x, y, 0xffffffff);
    auto original = box->levels[0][0];
    VectorSprite_Replace(".\\SetupRep\\Textures\\Symbols\\640\\BOX.DDS", box);
    CheckReplacement(box, original);
    Check(Field(box, 2.5f, 6.5f)[0] > 128, "box stroke stays inside the silhouette");
    Check(Field(box, 7.5f, 7.5f)[0] < 128, "box hole remains transparent");
    auto *drawn = Gfx_DrawnTexture(box);
    VectorSprite_Replace("box.tga", box);
    Check(Gfx_DrawnTexture(box) == drawn, "repeated registration reuses the existing replacement");
    auto *again = Texture(16, 16);
    again->levels[0][0] = original;
    VectorSprite_Replace("box.dds", again);
    Check(Gfx_DrawnTexture(again)->levels[0][0] == drawn->levels[0][0],
          "reload rebuilds exactly the same cached fields");
    Gfx_DestroyTexture(box);
    Gfx_DestroyTexture(again);
    for (const char *name :
         {"ar_640a.tga", "ar_640d.dds", "arrow.tga", "tick.tga", "cross.tga", "circle.tga"}) {
        auto *icon = Texture(16, 16);
        for (int y = 3; y < 10; y++)
            for (int x = 3; x < 10; x++)
                Pixel(icon, x, y, 0xffffffff);
        auto before = icon->levels[0][0];
        VectorSprite_Replace(name, icon);
        CheckReplacement(icon, before);
        Gfx_DestroyTexture(icon);
    }
}
static void RaceMenu() {
    auto *icon = Texture(16, 16);
    constexpr uint32_t blue = 0xff464fb8, white = 0xffffffff;
    for (int y = 1; y < 15; y++)
        for (int x = 1; x < 15; x++) {
            bool arrow = y >= 4 && y <= 11 && x == 11 - abs(y - 7);
            Pixel(icon, x, y, arrow ? white : blue);
        }
    // An antialiased pixel lies on the blue/white segment, not the blue/black one.
    Pixel(icon, 7, 4, 0xffa2a7dc);
    auto original = icon->levels[0][0];
    VectorSprite_Replace(".\\game\\menus\\Ar_640A.tga", icon);
    CheckReplacement(icon, original);
    const auto *base = Field(icon, 3.5f, 7.5f, 1);
    const auto *detail = Field(icon, 3.5f, 7.5f, 2);
    Check(base[0] == 184 && base[1] == 79 && base[2] == 70, "race menu keeps its original blue");
    Check(detail[0] == 255 && detail[1] == 255 && detail[2] == 255,
          "race menu keeps white arrow detail rather than assuming black");
    Check(Field(icon, 3.5f, 7.5f)[1] > 128, "blue background remains on the base side");
    Check(Field(icon, 11.5f, 7.5f)[1] < 128, "white selection arrow remains visible inside the square");
    Check(Field(icon, 11.5f, 7.5f)[2] == 255, "race menu uses both colour layers");
    Gfx_DestroyTexture(icon);
}
static void Arrows(int w, int h) {
    struct Cell {
        int x, y, w, h;
    };
    std::vector<Cell> cells;
    if (w == 256)
        for (int i = 0; i < 8; i++)
            cells.push_back({(i % 4) * 64, (i / 4) * 64, 64, 64});
    else
        cells = {{1, 0, 102, 102},   {103, 0, 102, 102},   {205, 0, 102, 102},   {307, 0, 102, 102},
                 {1, 102, 102, 102}, {103, 102, 102, 102}, {205, 102, 102, 102}, {307, 109, 107, 92}};
    const uint32_t colours[] = {0xffff0028, 0xffff1701, 0xffff6e08, 0xffffbd0d,
                                0xff8bba05, 0xffa2b4c8, 0xffd5c2be, 0xffffffff};
    auto *atlas = Texture(w, h);
    for (int i = 0; i < 8; i++) {
        auto c = cells[i];
        for (int y = 2; y < c.h - 2; y++)
            for (int x = 2; x < c.w - 2; x++) {
                bool black = x < 5 || y < 5 || x >= c.w - 5 || y >= c.h - 5 ||
                             (x >= c.w / 2 - 2 && x <= c.w / 2 + 2 && y > c.h / 3);
                Pixel(atlas, c.x + x, c.y + y, black ? 0xff000000 : colours[i]);
            }
    }
    auto original = atlas->levels[0][0];
    VectorSprite_Replace(".\\NEWIMAGE\\OSD\\CODRIV\\ARROWS4.TGA", atlas);
    CheckReplacement(atlas, original);
    for (int i = 0; i < 8; i++) {
        auto c = cells[i];
        float x = c.x + c.w * .25f, y = c.y + c.h * .25f;
        const auto *palette = Field(atlas, x, y, true);
        Check(palette[0] == (colours[i] & 255) && palette[1] == ((colours[i] >> 8) & 255) &&
                  palette[2] == ((colours[i] >> 16) & 255),
              "every cell retains its own exact original fill colour");
        Check(Field(atlas, x, y)[0] > 128 && Field(atlas, x, y)[1] > 128,
              "coloured fill has its own silhouette");
        Check(Field(atlas, c.x + c.w / 2 + .5f, c.y + c.h * .75f)[1] < 128,
              "black arrow detail remains separate from the fill");
    }
    Gfx_DestroyTexture(atlas);
}
int main() {
    SDL_SetEnvironmentVariable(SDL_GetEnvironment(), "OPENCMR2_HEADLESS", "1", true);
    Check(Gfx_SetVideoMode(640, 480, FALSE), "initialise headless renderer");
    Monochrome();
    RaceMenu();
    Arrows(256, 128);
    Arrows(512, 256);
    auto *other = Texture(16, 16);
    VectorSprite_Replace("car.tga", other);
    Check(Gfx_DrawnTexture(other) == other, "unrelated textures remain unchanged");
    Gfx_DestroyTexture(other);
    auto *wrong = Texture(64, 64);
    VectorSprite_Replace("arrows4.tga", wrong);
    Check(Gfx_DrawnTexture(wrong) == wrong, "unknown atlas layouts remain unchanged");
    Gfx_DestroyTexture(wrong);
    Gfx_Shutdown();
    Check(Gfx_SetVideoMode(1920, 1080, FALSE), "recreate renderer");
    Monochrome();
    Gfx_Shutdown();
    puts("vector sprites: native pixels, both original atlases, colour layers, holes and reloads passed");
}
