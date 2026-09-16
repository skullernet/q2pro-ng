/*
Copyright (C) 2026 Andrey Nazarov

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program; if not, write to the Free Software Foundation, Inc.,
51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#include "gl.h"
#include "common/blake2b.h"

#define MAX_FONTS   128

static font_t   r_fonts[MAX_FONTS];
static int      r_numfonts;

#if USE_FREETYPE

#include <ft2build.h>
#include FT_FREETYPE_H

#define FONT_PAGE_SIZE  1024

static FT_Library   ft_lib;
static byte         ft_bitmap[FONT_PAGE_SIZE * FONT_PAGE_SIZE * 4];
static uint16_t     ft_inuse[FONT_PAGE_SIZE];

#define Font_AllocBlock(w, h, x, y) \
    GL_AllocBlock(FONT_PAGE_SIZE, FONT_PAGE_SIZE, ft_inuse, w, h, x, y)

static void Font_InitBlock(void)
{
    memset(ft_bitmap, 0, sizeof(ft_bitmap));
    memset(ft_inuse, 0, sizeof(ft_inuse));
}

static void Font_UploadBlock(font_t *font)
{
    if (font->numpages >= MAX_FONT_PAGES)
        return;

    if (!ft_inuse[0])
        return;

    image_t *image = IMG_Alloc();
    if (!image) {
        Com_WPrintf("Out of image slots\n");
        font->pages[font->numpages++] = R_NOTEXTURE;
        return;
    }

    Q_snprintf(image->name, sizeof(image->name), "%s_%d_%d",
               font->name, font->size, font->numpages);
    image->width = image->upload_width =
    image->height = image->upload_height = FONT_PAGE_SIZE;
    image->registration_sequence = r_registration_sequence;
    image->type = IT_FONT;
    image->flags = IF_TRANSPARENT;
    if (font->permanent)
        image->flags |= IF_PERMANENT;
    List_Init(&image->entry);
    IMG_Load(image, ft_bitmap);

    font->pages[font->numpages++] = image;
}

static void Font_RenderGlyph(font_t *font, FT_Face face, uint32_t code)
{
    FT_GlyphSlot glyph;
    FT_Error error;
    FT_UInt index;
    int x, y, w, h, pitch;

    if (font->numpages >= MAX_FONT_PAGES)
        return;     // can't have any more

    index = FT_Get_Char_Index(face, code);
    if (!index && code != UNICODE_UNKNOWN)
        return;     // don't render missing glyphs

    error = FT_Load_Glyph(face, index, FT_LOAD_RENDER);
    if (error) {
        Com_WPrintf("FT_Load_Glyph(%u) failed with error %d\n", index, error);
        return;
    }

    glyph = face->glyph;
    w = glyph->bitmap.width;
    h = glyph->bitmap.rows;
    Q_assert(w >= 0 && h >= 0);
    x = y = 0;

    if (w && h && !Font_AllocBlock(w + 1, h + 1, &x, &y)) {
        Font_UploadBlock(font);
        if (font->numpages >= MAX_FONT_PAGES) {
            Com_WPrintf("Too many font pages\n");
            return;
        }
        Font_InitBlock();
        if (!Font_AllocBlock(w + 1, h + 1, &x, &y)) {
            Com_WPrintf("Font_AllocBlock(%d, %d) failed\n", w, h);
            return;
        }
    }

    pitch = glyph->bitmap.pitch;
    for (int i = 0; i < h; i++) {
        const byte *src = &glyph->bitmap.buffer[i * pitch];
        byte *dst = &ft_bitmap[((y + i) * FONT_PAGE_SIZE + x) << 2];
        for (int j = 0; j < w; j++, dst += 4)
            Vec4_Set(dst, 255, 255, 255, *src++);
    }

    glyph_t gl = {
        .w = w, .h = h,
        .left = glyph->bitmap_left,
        .top = glyph->bitmap_top,
        .adv = glyph->advance.x >> 6,
        .tc = Box2_Scale(Box2_At(x, y, w, h), 1.0f / FONT_PAGE_SIZE),
        .page = font->numpages
    };

    HashMap_Insert(font->map, &code, &gl);

    // FIXME: use face metrics?
    if (index) {
        font->height = max(font->height, gl.h);
        font->ascent = max(font->ascent, gl.top);
    }
}

static bool Font_RegisterFreetype(font_t *font)
{
    FT_Face face;
    FT_Error error;
    void *data;
    int len;

    if (!ft_lib)
        return false;

    len = FS_LoadFile(va("fonts/%s", font->name), (void **)&data);
    if (!data)
        return false;

    error = FT_New_Memory_Face(ft_lib, data, len, 0, &face);
    if (error) {
        Com_WPrintf("FT_New_Memory_Face failed with error %d\n", error);
        FS_FreeFile(data);
        return false;
    }

    font->scale = draw.scale;

    error = FT_Set_Pixel_Sizes(face, 0, font->size);
    if (error) {
        Com_WPrintf("FT_Set_Pixel_Sizes failed with error %d\n", error);
        FS_FreeFile(data);
        return false;
    }

    font->map = HashMap_TagCreate(uint32_t, glyph_t, HashInt32, NULL, TAG_RENDERER);

    Font_InitBlock();

    // Latin + Extended A/B
    for (int i = 0; i <= 0x024F; i++)
        Font_RenderGlyph(font, face, i);

    // Cyrillic
    for (int i = 0x0400; i <= 0x04FF; i++)
        Font_RenderGlyph(font, face, i);

    // General Punctuation
    for (int i = 0x2000; i <= 0x206F; i++)
        Font_RenderGlyph(font, face, i);

    // Replacement Character
    Font_RenderGlyph(font, face, UNICODE_UNKNOWN);

    Font_UploadBlock(font);

    FT_Done_Face(face);
    FS_FreeFile(data);

    Com_DDPrintf("%s: %s, %d size, %d height, %d ascent, %d glyphs, %d pages\n", __func__,
                 font->name, font->size, font->height, font->ascent, HashMap_Size(font->map), font->numpages);

    return true;
}

#else
#define Font_RegisterFreetype(font) false
#endif

static float Font_BitmapScale(const font_t *font)
{
    // only do integer scaling for bitmap fonts
    return max(font->size / font->height, 1) * draw.scale;
}

static bool Font_RegisterKFont(font_t *font)
{
    char *data;
    const char *s, *tok;
    float sx, sy;
    image_t *image = NULL;
    int len;
    bool shit = false;

    len = FS_LoadFile(va("fonts/%s", font->name), (void **)&data);
    if (!data)
        return false;

    if (len == 5202) {
        byte out[4]; blake2b(out, sizeof(out), data, len); shit = Q_RL32(out) == 0x2d67809e;
    }

    s = data;
    while (1) {
        tok = COM_Parse(&s);
        if (!s)
            break;

        if (!strcmp(tok, "texture")) {
            tok = COM_Parse(&s);

            if (image) {
                Com_WPrintf("Duplicate 'texture' in %s\n", font->name);
                continue;
            }

            imageflags_t flags = IF_NONE;
            if (font->permanent)
                flags |= IF_PERMANENT;

            qhandle_t pic = IMG_Register(va("/%s", tok), IT_FONT, flags);
            if (!pic)
                return false;

            image = IMG_ForHandle(pic);
            continue;
        }

        if (!strcmp(tok, "unicode"))
            continue;

        if (!strcmp(tok, "mapchar")) {
            tok = COM_Parse(&s);
            if (strcmp(tok, "{")) {
                Com_WPrintf("Expected '{', got '%s' in %s\n", tok, font->name);
                break;
            }

            if (!image) {
                Com_WPrintf("'mapchar' before 'texture' in %s\n", font->name);
                return false;
            }

            if (!font->map)
                font->map = HashMap_TagCreate(uint32_t, glyph_t, HashInt32, NULL, TAG_RENDERER);

            sx = 1.0f / image->width;
            sy = 1.0f / image->height;

            while (1) {
                tok = COM_Parse(&s);
                if (!s) {
                    Com_WPrintf("Unexpected end of file in %s\n", font->name);
                    break;
                }
                if (!strcmp(tok, "}"))
                    break;

                uint32_t code = Q_atoi(tok);
                int x = Q_atoi(COM_Parse(&s));
                int y = Q_atoi(COM_Parse(&s));
                int w = Q_atoi(COM_Parse(&s));
                int h = Q_atoi(COM_Parse(&s));
                COM_SkipToken(&s);

                if (shit) {
                    y += 3;
                    h -= 6;
                }

                glyph_t gl = {
                    .w = w, .h = h, .adv = w,
                    .tc = Box2_At(x * sx, y * sy, w * sx, h * sy)
                };

                HashMap_Insert(font->map, &code, &gl);
                font->height = max(font->height, h);
            }
            continue;
        }

        Com_WPrintf("Unknown token '%s' in %s\n", tok, font->name);
    }

    FS_FreeFile(data);

    if (!font->map)
        return false;

    font->pages[0] = image;
    font->numpages = 1;
    font->scale = Font_BitmapScale(font);
    return true;
}

static bool Font_RegisterLegacy(font_t *font)
{
    qhandle_t pic = IMG_Register(font->name, IT_FONT, font->permanent ? IF_PERMANENT : IF_NONE);
    if (!pic)
        return false;

    font->pages[0] = IMG_ForHandle(pic);
    font->numpages = 1;
    font->height = CONCHAR_HEIGHT;
    font->scale = Font_BitmapScale(font);
    return true;
}

void R_FreeFonts(void)
{
    font_t *font;
    int i;

    for (i = 1, font = r_fonts + i; i < r_numfonts; i++, font++) {
        if (!font->name[0])
            continue;        // free font_t slot
        if (font->registration_sequence == r_registration_sequence)
            continue;        // used this sequence
        if (font->permanent)
            continue;        // permanent
        // free it
        HashMap_Destroy(font->map);
        memset(font, 0, sizeof(*font));
    }
}

void R_ShutdownFonts(void)
{
    font_t *font;
    int i;

#if USE_FREETYPE
    if (ft_lib) {
        FT_Done_FreeType(ft_lib);
        ft_lib = NULL;
    }
#endif

    for (i = 1, font = r_fonts + i; i < r_numfonts; i++, font++) {
        if (!font->name[0])
            continue;
        HashMap_Destroy(font->map);
    }

    memset(r_fonts, 0, sizeof(r_fonts));
    r_numfonts = 0;
}

void R_InitFonts(void)
{
#if USE_FREETYPE
    FT_Init_FreeType(&ft_lib);
#endif
    r_fonts[0].pages[0] = R_NOTEXTURE;
    r_fonts[0].height = CONCHAR_HEIGHT;
    r_fonts[0].scale = 1.0f;
    r_numfonts = 1;
}

const font_t *R_FontForHandle(qhandle_t hfont)
{
    Q_assert_soft(hfont < r_numfonts);
    const font_t *font = &r_fonts[hfont];
    if (font->name[0])
        return font;
    return r_fonts;
}

static font_t *Font_Alloc(void)
{
    font_t *font;
    int i;

    for (i = 1, font = r_fonts + i; i < r_numfonts; i++, font++)
        if (!font->name[0])
            return font;

    if (i == r_numfonts) {
        if (r_numfonts == MAX_FONTS) {
            Com_WPrintf("Too many fonts\n");
            return NULL;
        }
        r_numfonts++;
    }

    return font;
}

static qhandle_t Font_Reference(font_t *font)
{
    font->registration_sequence = r_registration_sequence;
    for (int j = 0; j < font->numpages; j++)
        font->pages[j]->registration_sequence = r_registration_sequence;
    return font - r_fonts;
}

static qhandle_t Font_Rescale(const font_t *base)
{
    font_t *font = Font_Alloc();
    if (!font)
        return 0;

    *font = *base;
    font->cached_scale = draw.scale;

    if (font->scalable)
        font->scale = draw.scale;
    else
        font->scale = Font_BitmapScale(font);

    if (base->map) {
        uint32_t map_size = HashMap_Size(base->map);

        font->map = HashMap_TagCreate(uint32_t, glyph_t, HashInt32, NULL, TAG_RENDERER);
        HashMap_Reserve(font->map, map_size);

        for (uint32_t i = 0; i < map_size; i++)
            HashMap_Insert(font->map, HashMap_GetKey(uint32_t, base->map, i), HashMap_GetValue(glyph_t, base->map, i));
    }

    return Font_Reference(font);
}

static font_t *Font_Lookup(const char *name, int size)
{
    font_t *font, *base = NULL;
    int i;

    for (i = 1, font = r_fonts + i; i < r_numfonts; i++, font++) {
        if (!font->name[0])
            continue;
        if (Q_stricmp(font->name, name))
            continue;
        if (font->size != size)
            continue;
        if (font->cached_scale == draw.scale)
            return font;    // found exact match
        base = font;
    }

    return base;
}

static qhandle_t Font_Register(const char *name, int size, bool permanent)
{
    font_t *font;
    const char *ext;
    bool ok;

    if (!*name)
        return 0;

    if (size <= 0)
        size = CONCHAR_HEIGHT;
    else if (size > 256)
        size = 256;

    size = size / draw.scale + 0.5f;

    // see if pixel size is already loaded
    font = Font_Lookup(name, size);
    if (font) {
        // check for exact match first
        if (font->cached_scale == draw.scale)
            return Font_Reference(font);

        // duplicate handle with different scale
        return Font_Rescale(font);
    }

    font = Font_Alloc();
    if (!font)
        return 0;

    Q_strlcpy(font->name, name, sizeof(font->name));
    font->permanent = permanent;
    font->registration_sequence = r_registration_sequence;
    font->size = size;
    font->cached_scale = draw.scale;

    ext = COM_FileExtension(name);

    if (!Q_stricmp(ext, ".kfont"))
        ok = Font_RegisterKFont(font);
    else if (!Q_stricmp(ext, ".ttf") || !Q_stricmp(ext, ".otf"))
        ok = Font_RegisterFreetype(font);
    else
        ok = Font_RegisterLegacy(font);

    if (!ok) {
        memset(font, 0, sizeof(*font));
        return 0;
    }

    return font - r_fonts;
}

qhandle_t R_RegisterFont(const char *name, int size)
{
    return Font_Register(name, size, true);
}

qhandle_t R_RegisterTempFont(const char *name, int size)
{
    return Font_Register(name, size, false);
}
