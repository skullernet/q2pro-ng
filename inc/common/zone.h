/*
Copyright (C) 1997-2001 Id Software, Inc.

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

#define Z_CopyString(string)    Z_TagCopyString(string, TAG_GENERAL)
#define Z_CopyStruct(ptr)       memcpy(Z_Malloc(sizeof(*(ptr))), (ptr), sizeof(*(ptr)))

#define Z_Freep(ptr) do { \
    if (*(ptr)) { Z_Free(*(ptr)); *(ptr) = NULL; } } while (0)

#define Z_ARRAY_GROW(ptr, index, tag) do { \
    if (Z_BlockSize(ptr) / sizeof(*(ptr)) <= (index)) \
        ptr = Z_TagReallocArray((ptr), Z_CalcPreallocElems(index), sizeof(*(ptr)), tag); \
    } while (0)

#define Z_ARRAY_APPEND(ptr, index, val, tag) do { \
    Z_ARRAY_GROW(ptr, index, tag); (ptr)[(index)++] = (val); } while (0)

// memory tags to allow dynamic memory to be cleaned up
typedef enum {
    TAG_GENERAL,
    TAG_CMD,
    TAG_CVAR,
    TAG_FILESYSTEM,
    TAG_RENDERER,
    TAG_UI,
    TAG_SERVER,
    TAG_SOUND,
    TAG_CMODEL,
    TAG_VM,

    TAG_MAX
} memtag_t;

static inline size_t Z_CalcPreallocElems(size_t index)
{
    if (index == 0)
        return 1;
    if (index > SIZE_MAX / 2)
        return SIZE_MAX;
    return index * 2;
}

void    Z_Init(void);
void    Z_Free(void *ptr);
void   *Z_Realloc(void *ptr, size_t size);
void   *Z_TreeRealloc(void *parent, void *ptr, size_t size);
void   *Z_TreeReallocArray(void *parent, void *ptr, size_t nmemb, size_t size);
void   *Z_TagReallocArray(void *ptr, size_t nmemb, size_t size, memtag_t tag);
void   *Z_SetParent(void *ptr, void *parent);
size_t  Z_BlockSize(const void *ptr);

q_malloc void *Z_NewContext(memtag_t tag);
q_malloc void *Z_Malloc(size_t size);
q_malloc void *Z_TagMalloc(size_t size, memtag_t tag);
q_malloc void *Z_TreeMalloc(void *parent, size_t size);
q_malloc void *Z_TreeMallocArray(void *parent, size_t nmemb, size_t size);
q_malloc void *Z_TagMallocArray(size_t nmemb, size_t size, memtag_t tag);
q_malloc char *Z_TreeCopyString(void *parent, const char *in);
q_malloc char *Z_TagCopyString(const char *in, memtag_t tag);

void    Z_FreeTags(memtag_t tag);
void    Z_LeakTest(memtag_t tag);
void    Z_Stats_f(void);
