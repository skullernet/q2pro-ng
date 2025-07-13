/*
Copyright (C) 1997-2001 Id Software, Inc.
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

#pragma once

//
// images.h -- common image manager
//

#include "common/files.h"
#include "common/list.h"
#include "common/zone.h"
#include "common/error.h"
#include "refresh/refresh.h"

#define IMG_AllocPixels(x)  FS_AllocTempMem(x)
#define IMG_FreePixels(x)   FS_FreeTempMem(x)

#define LUMINANCE(r, g, b) ((r) * 0.2126f + (g) * 0.7152f + (b) * 0.0722f)

#define U32_ALPHA   MakeColor(  0,   0,   0, 255)
#define U32_RGB     MakeColor(255, 255, 255,   0)

// absolute limit for OpenGL renderer
#define MAX_TEXTURE_SIZE    8192

typedef enum {
    IF_NONE             = 0,
    IF_PERMANENT        = BIT(0),   // not freed by R_EndRegistration()
    IF_TRANSPARENT      = BIT(1),   // known to be transparent
    IF_PALETTED         = BIT(2),   // loaded from 8-bit paletted format
    IF_UPSCALED         = BIT(3),   // upscaled
    IF_SCRAP            = BIT(4),   // put in scrap texture
    IF_TURBULENT        = BIT(5),   // turbulent surface (don't desaturate, etc)
    IF_REPEAT           = BIT(6),   // tiling image
    IF_NEAREST          = BIT(7),   // don't bilerp
    IF_OPAQUE           = BIT(8),   // known to be opaque
    IF_DEFAULT_FLARE    = BIT(9),   // default flare hack
    IF_CUBEMAP          = BIT(10),  // cubemap (or part of it)
    IF_CLASSIC_SKY      = BIT(11),  // split in two halves

    // these flags only affect R_RegisterImage() behavior,
    // and are not stored in image
    IF_OPTIONAL         = BIT(16),  // don't warn if not found
    IF_KEEP_EXTENSION   = BIT(17),  // don't override extension
} imageflags_t;

typedef enum {
    IT_PIC,
    IT_FONT,
    IT_SKIN,
    IT_SPRITE,
    IT_WALL,
    IT_SKY,

    IT_MAX
} imagetype_t;

typedef enum {
    IM_PCX,
    IM_WAL,
#if USE_TGA
    IM_TGA,
#endif
#if USE_JPG
    IM_JPG,
#endif
#if USE_PNG
    IM_PNG,
#endif
    IM_MAX
} imageformat_t;

typedef struct image_s {
    list_t          entry;
    char            name[MAX_QPATH]; // game path
    uint8_t         baselen; // without extension
    uint8_t         type;
    uint16_t        flags;
    uint16_t        width, height; // source image
    uint16_t        upload_width, upload_height; // after power of two and picmip
    unsigned        registration_sequence;
    unsigned        texnum, texnum2; // gl texture binding
    float           sl, sh, tl, th;
    float           aspect;
} image_t;

#define MAX_RIMAGES     8192

extern image_t  r_images[MAX_RIMAGES];
extern int      r_numImages;

extern unsigned r_registration_sequence;

#define R_NUM_AUTO_IMG  3
#define R_NOTEXTURE     (&r_images[0])
#define R_SHELLTEXTURE  (&r_images[1])
#define R_SKYTEXTURE    (&r_images[2])

extern uint32_t d_8to24table[256];

image_t *IMG_Find(const char *name, imagetype_t type, imageflags_t flags);
void IMG_FreeUnused(void);
void IMG_FreeAll(void);
void IMG_Init(void);
void IMG_Shutdown(void);
void IMG_GetPalette(void);

image_t *IMG_ForHandle(qhandle_t h);

void IMG_Unload(image_t *image);
void IMG_Load(image_t *image, byte *pic);

typedef struct screenshot_s screenshot_t;

typedef int (*save_cb_t)(const screenshot_t *);

struct screenshot_s {
    save_cb_t save_cb;
    byte *pixels;
    FILE *fp;
    char *filename;
    int width, height, rowbytes, bpp, status, param;
    bool async;
};

int IMG_ReadPixels(screenshot_t *s);
