/*
Copyright (C) 2003-2006 Andrey Nazarov

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

drawStatic_t draw;

static void GL_StretchPicVerts(const glVertex2D_t verts[4], GLuint texnum, imageflags_t flags)
{
    if (tess.numverts + 4 > TESS_MAX_VERTICES ||
        tess.numindices + 6 > TESS_MAX_INDICES ||
        (tess.numverts && tess.texnum[TMU_TEXTURE] != texnum))
        GL_Flush2D();

    tess.texnum[TMU_TEXTURE] = texnum;

    for (int i = 0; i < 4; i++)
        tess.vertices2D[tess.numverts + i] = verts[i];

    glIndex_t *dst_indices = tess.indices + tess.numindices;
    dst_indices[0] = tess.numverts + 0;
    dst_indices[1] = tess.numverts + 1;
    dst_indices[2] = tess.numverts + 2;
    dst_indices[3] = tess.numverts + 3;
    dst_indices[4] = tess.numverts + 2;
    dst_indices[5] = tess.numverts + 1;

    if (flags & IF_TRANSPARENT) {
        if ((flags & IF_PALETTED) && draw.scale == 1)
            tess.flags |= GLS_ALPHATEST_ENABLE;
        else
            tess.flags |= GLS_BLEND_BLEND;
    }

    if (verts[0].color.a != 255)
        tess.flags |= GLS_BLEND_BLEND;

    tess.numverts += 4;
    tess.numindices += 6;
}

static inline void GL_MakePicVerts(glVertex2D_t verts[4], box2_t box, box2_t tc, color_t color)
{
    for (int i = 0; i < 4; i++) {
        verts[i].xy = Vec2(box.bounds[i & 1].x, box.bounds[i >> 1].y);
        verts[i].st = Vec2(tc.bounds[i & 1].s, tc.bounds[i >> 1].t);
        verts[i].color = color;
    }
}

static inline void GL_StretchPic(box2_t box, box2_t tc, color_t color, const image_t *image)
{
    glVertex2D_t verts[4];

    if (image->flags & IF_SCRAP)
        tc = image->tc;

    GL_MakePicVerts(verts, box, tc, color);
    GL_StretchPicVerts(verts, image->texnum, image->flags);
}

static void GL_DrawVignette(box2_t box, color_t outer, color_t inner, float frac)
{
    if (tess.numverts + 8 > TESS_MAX_VERTICES ||
        tess.numindices + 24 > TESS_MAX_INDICES ||
        (tess.numverts && tess.texnum[TMU_TEXTURE] != TEXNUM_WHITE))
        GL_Flush2D();

    tess.texnum[TMU_TEXTURE] = TEXNUM_WHITE;

    // outer vertices
    GL_MakePicVerts(tess.vertices2D + tess.numverts, box, box2_origin, outer);

    // inner vertices
    box = Box2_Expand(box, Vec2_Scale(Box2_Size(box), -frac));
    GL_MakePicVerts(tess.vertices2D + tess.numverts + 4, box, box2_origin, inner);

    /*
    0             1
        4     5

        6     7
    2             3
    */

    static const byte indices[24] = {
        0, 5, 4, 0, 1, 5, 1, 7, 5, 1, 3, 7, 7, 3, 2, 7, 2, 6, 0, 6, 2, 0, 4, 6
    };

    glIndex_t *dst_indices = tess.indices + tess.numindices;
    for (int i = 0; i < 24; i++)
        dst_indices[i] = tess.numverts + indices[i];

    tess.flags |= GLS_BLEND_BLEND;

    tess.numverts += 8;
    tess.numindices += 24;
}

void GL_Blend(void)
{
    box2_t box = Box2_At(glr.fd.x, glr.fd.y, glr.fd.width, glr.fd.height);

    if (glr.fd.screen_blend.a) {
        color_t color = Vec4_ToColor(glr.fd.screen_blend);
        GL_StretchPic(box, box2_origin, color, R_WHITEIMAGE);
    }

    if (glr.fd.damage_blend.a) {
        color_t outer, inner;

        outer = inner = Vec4_ToColor(glr.fd.damage_blend);
        inner.a = 0;

        if (gl_damageblend_frac->value > 0)
            GL_DrawVignette(box, outer, inner, Cvar_ClampValue(gl_damageblend_frac, 0, 0.5f));
        else
            GL_StretchPic(box, box2_origin, outer, R_WHITEIMAGE);
    }
}

void R_GetPalette(uint32_t palette[256])
{
    memcpy(palette, d_8to24table, sizeof(d_8to24table));
}

void R_ClearColor(void)
{
    draw.colors[0].u32 = U32_WHITE;
    draw.colors[1].u32 = U32_WHITE;
}

void R_SetAlpha(float alpha)
{
    draw.colors[0].a =
    draw.colors[1].a = alpha * 255;
}

void R_SetColor24(uint32_t color)
{
    draw.colors[0].u32 = color;
    draw.colors[0].a = draw.colors[1].a;
}

void R_SetColor(uint32_t color)
{
    draw.colors[0].u32 = color;
    draw.colors[1].a = draw.colors[0].a;
}

void R_SetClipBox(box2_t box)
{
    int left, right, top, bottom;

    GL_Flush2D();

    if (Box2_IsNull(box)) {
clear:
        if (draw.scissor) {
            qglDisable(GL_SCISSOR_TEST);
            draw.scissor = false;
        }
        return;
    }

    box = Box2_Scale(box, 1.0f / draw.scale);
    box = Box2_Intersection(box, Box2_At(0, 0, r_config.width, r_config.height));
    if (Box2_IsNull(box))
        goto clear;

    left = box.mins.x + 0.5f;
    right = box.maxs.x + 0.5f;
    top = box.mins.y + 0.5f;
    bottom = box.maxs.y + 0.5f;

    qglEnable(GL_SCISSOR_TEST);
    qglScissor(left, r_config.height - bottom, right - left, bottom - top);
    draw.scissor = true;
}

static int get_auto_scale(void)
{
    int scale = 1;

    if (r_config.height < r_config.width) {
        if (r_config.height >= 2160)
            scale = 4;
        else if (r_config.height >= 1080)
            scale = 2;
    } else {
        if (r_config.width >= 3840)
            scale = 4;
        else if (r_config.width >= 1920)
            scale = 2;
    }

    if (vid && vid->get_dpi_scale) {
        int min_scale = vid->get_dpi_scale();
        return max(scale, min_scale);
    }

    return scale;
}

float R_ClampScale(cvar_t *var)
{
    if (var && var->value)
        return 1.0f / Cvar_ClampValue(var, 1.0f, 10.0f);

    return 1.0f / get_auto_scale();
}

void R_SetScale(float scale)
{
    if (draw.scale == scale)
        return;

    GL_Flush2D();

    GL_Ortho(0, r_config.width * scale, r_config.height * scale, 0, -1, 1);

    draw.scale = scale;
}

void R_DrawBoxPic(box2_t box, box2_t tc, qhandle_t pic)
{
    GL_StretchPic(box, tc, draw.colors[0], IMG_ForHandle(pic));
}

void R_DrawStretchPic(int x, int y, int w, int h, qhandle_t pic)
{
    const image_t *image = IMG_ForHandle(pic);
    GL_StretchPic(Box2_At(x, y, w, h), image->tc, draw.colors[0], image);
}

void R_DrawKeepAspectPic(int x, int y, int w, int h, qhandle_t pic)
{
    const image_t *image = IMG_ForHandle(pic);

    float scale_w = w;
    float scale_h = h * image->aspect;
    float scale = max(scale_w, scale_h);

    float s = (1.0f - scale_w / scale) * 0.5f;
    float t = (1.0f - scale_h / scale) * 0.5f;

    GL_StretchPic(Box2_At(x, y, w, h), Box2_Expand(box2_unit, Vec2(-s, -t)), draw.colors[0], image);
}

void R_DrawPic(int x, int y, qhandle_t pic)
{
    const image_t *image = IMG_ForHandle(pic);
    GL_StretchPic(Box2_At(x, y, image->width, image->height), image->tc, draw.colors[0], image);
}

void R_DrawStretchRaw(int x, int y, int w, int h)
{
    glVertex2D_t verts[4];
    GL_MakePicVerts(verts, Box2_At(x, y, w, h), box2_unit, (color_t){ U32_WHITE });
    GL_StretchPicVerts(verts, TEXNUM_RAW, IF_NONE);
}

void R_UpdateRawPic(int pic_w, int pic_h, const uint32_t *pic)
{
    GL_ForceTexture(TMU_TEXTURE, TEXNUM_RAW);
    qglTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, pic_w, pic_h, 0, GL_RGBA, GL_UNSIGNED_BYTE, pic);
}

void R_DrawFill8(int x, int y, int w, int h, int c)
{
    if (!w || !h)
        return;
    GL_StretchPic(Box2_At(x, y, w, h), box2_unit, (color_t){ d_8to24table[c & 0xff] }, R_WHITEIMAGE);
}

void R_DrawFill32(int x, int y, int w, int h, uint32_t color)
{
    if (!w || !h)
        return;
    GL_StretchPic(Box2_At(x, y, w, h), box2_unit, (color_t){ color }, R_WHITEIMAGE);
}

static inline void draw_char(int x, int y, int flags, int c, const image_t *image)
{
    float s, t;

    if ((c & 127) == 32)
        return;

    if ((flags & UI_IGNORECOLOR) == UI_IGNORECOLOR)
        c &= 0x7f;
    else if (flags & UI_ALTCOLOR)
        c |= 0x80;
    else if (flags & UI_XORCOLOR)
        c ^= 0x80;

    s = (c & 15) * 0.0625f;
    t = (c >> 4) * 0.0625f;

    box2_t box = Box2_At(x, y, CONCHAR_WIDTH, CONCHAR_HEIGHT);
    box2_t tc  = Box2_At(s, t, 0.0625f, 0.0625f);

    if (flags & UI_DROPSHADOW && c != 0x83) {
        color_t black = { .a = draw.colors[0].a };

        GL_StretchPic(Box2_Translate(box, Vec2(1, 1)), tc, black, image);

        if (gl_fontshadow->integer > 1)
            GL_StretchPic(Box2_Translate(box, Vec2(2, 2)), tc, black, image);
    }

    GL_StretchPic(box, tc, draw.colors[c >> 7], image);
}

void R_DrawChar(int x, int y, int flags, int c, qhandle_t font)
{
    if (gl_fontshadow->integer > 0)
        flags |= UI_DROPSHADOW;

    draw_char(x, y, flags, c & 255, IMG_ForHandle(font));
}

int R_DrawString(int x, int y, int flags, size_t maxlen, const char *s, qhandle_t font)
{
    const image_t *image = IMG_ForHandle(font);

    if (gl_fontshadow->integer > 0)
        flags |= UI_DROPSHADOW;

    while (maxlen-- && *s) {
        byte c = *s++;
        draw_char(x, y, flags, c, image);
        x += CONCHAR_WIDTH;
    }

    return x;
}

#if USE_DEBUG

qhandle_t r_charset;

static void Draw_Stringf(int x, int y, const char *fmt, ...)
{
    va_list argptr;
    char buffer[MAX_STRING_CHARS];

    va_start(argptr, fmt);
    Q_vsnprintf(buffer, sizeof(buffer), fmt, argptr);
    va_end(argptr);

    R_DrawString(x, y, 0, -1, buffer, r_charset);
}

void Draw_Stats(void)
{
    int x = 10, y = 10;
    float scale = get_auto_scale();
    if (scale >= 2.0f)
        scale *= 0.5f;

    R_SetScale(1.0f / scale);
    R_DrawFill8(8, 8, 25*8, 26*10+2, 4);

    Draw_Stringf(x, y, "Nodes visible  : %i", glr.nodes_visible); y += 10;
    Draw_Stringf(x, y, "Nodes culled   : %i", c.nodesCulled); y += 10;
    Draw_Stringf(x, y, "Nodes drawn    : %i", c.nodesDrawn); y += 10;
    Draw_Stringf(x, y, "Leaves drawn   : %i", c.leavesDrawn); y += 10;
    Draw_Stringf(x, y, "Faces drawn    : %i", c.facesDrawn); y += 10;
    Draw_Stringf(x, y, "Faces culled   : %i", c.facesCulled); y += 10;
    Draw_Stringf(x, y, "Boxes culled   : %i", c.boxesCulled); y += 10;
    Draw_Stringf(x, y, "Spheres culled : %i", c.spheresCulled); y += 10;
    Draw_Stringf(x, y, "RtBoxes culled : %i", c.rotatedBoxesCulled); y += 10;
    Draw_Stringf(x, y, "Shadows culled : %i", c.shadowsCulled); y += 10;
    Draw_Stringf(x, y, "Lights culled  : %i", c.lightsCulled); y += 10;
    Draw_Stringf(x, y, "Tris drawn     : %i", c.trisDrawn); y += 10;
    Draw_Stringf(x, y, "Tex switches   : %i", c.texSwitches); y += 10;
    Draw_Stringf(x, y, "Tex uploads    : %i", c.texUploads); y += 10;
    Draw_Stringf(x, y, "Batches drawn  : %i", c.batchesDrawn); y += 10;
    Draw_Stringf(x, y, "Faces / batch  : %.1f", c.batchesDrawn ? (float)c.facesDrawn / c.batchesDrawn : 0.0f); y += 10;
    Draw_Stringf(x, y, "Tris / batch   : %.1f", c.batchesDrawn ? (float)c.facesTris / c.batchesDrawn : 0.0f); y += 10;
    Draw_Stringf(x, y, "2D batches     : %i", c.batchesDrawn2D); y += 10;
    Draw_Stringf(x, y, "Total entities : %i", r_numentities); y += 10;
    Draw_Stringf(x, y, "Total dlights  : %i", r_numdlights); y += 10;
    Draw_Stringf(x, y, "Total particles: %i", r_numparticles); y += 10;
    Draw_Stringf(x, y, "Uniform uploads: %i", c.uniformUploads); y += 10;
    Draw_Stringf(x, y, "Array binds    : %i", c.vertexArrayBinds); y += 10;
    Draw_Stringf(x, y, "Occl. queries  : %i", c.occlusionQueries); y += 10;
    Draw_Stringf(x, y, "Shadow views   : %i", glr.num_shadow_views); y += 10;
    Draw_Stringf(x, y, "Static shadows : %i/%i/%i", c.staticShadowsCached, c.staticShadowsDrawn, c.staticShadowsOverrun); y += 10;

    R_SetScale(1.0f);
}

void Draw_Lightmaps(void)
{
#if 0
    int block = lm.block_size;
    int rows = 0, cols = 0;

    while (block) {
        rows = max(r_config.height / block, 1);
        cols = max(lm.nummaps / rows, 1);
        if (cols * block <= r_config.width)
            break;
        block >>= 1;
    }

    for (int i = 0; i < cols; i++) {
        for (int j = 0; j < rows; j++) {
            int k = j * cols + i;
            if (k < lm.nummaps)
                GL_StretchPic_(block * i, block * j, block, block,
                               0, 0, 1, 1, U32_WHITE, lm.texnums[k], 0);
        }
    }
#endif
}

void Draw_Scrap(void)
{
    glVertex2D_t verts[4];
    GL_MakePicVerts(verts, Box2_At(0, 0, 512, 512), box2_unit, (color_t){ U32_WHITE });
    GL_StretchPicVerts(verts, TEXNUM_SCRAP, IF_PALETTED | IF_TRANSPARENT);
}

#endif
