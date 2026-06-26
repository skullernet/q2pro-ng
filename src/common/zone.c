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

#include "shared/shared.h"
#include "common/common.h"
#include "common/zone.h"

#define Z_MAGIC     0x1d0d3e5e
#define Z_POISON    0xdeadf00d

typedef struct zhead_s {
    size_t           magic;
    size_t           size;
    struct zhead_s  *prev;
    struct zhead_s  *next;
    struct zhead_s  *child;     // pointer to first child
    struct zhead_s  *parent;    // only first child has this set
} zhead_t;

typedef struct {
    size_t      count;
    size_t      bytes;
} zstats_t;

// tags are still supported, but primarily used for statistics now.
static zhead_t  z_tags[TAG_MAX];

static const char *const z_tagnames[TAG_MAX] = {
    [TAG_GENERAL]    = "generic",
    [TAG_CMD]        = "cmd",
    [TAG_CVAR]       = "cvar",
    [TAG_FILESYSTEM] = "fs",
    [TAG_RENDERER]   = "refresh",
    [TAG_UI]         = "ui",
    [TAG_SERVER]     = "server",
    [TAG_SOUND]      = "sound",
    [TAG_CMODEL]     = "cmodel",
    [TAG_VM]         = "vm",
};

/*
========================
Z_Init
========================
*/
void Z_Init(void)
{
    for (int i = 0; i < TAG_MAX; i++)
        z_tags[i].magic = Z_MAGIC;
}

/*
========================
Z_TreeStats
========================
*/
static void Z_TreeStats(zstats_t *s, const zhead_t *z)
{
    Q_assert(z->magic == Z_MAGIC);

    // don't count tag head
    if (z->size) {
        s->count++;
        s->bytes += z->size;
    }

    // recursively count children
    for (const zhead_t *c = z->child; c; c = c->next)
        Z_TreeStats(s, c);
}

/*
========================
Z_LeakTest

Verifies if all memory allocated under tag has been freed.
========================
*/
void Z_LeakTest(memtag_t tag)
{
    Q_assert(tag < TAG_MAX);

    zstats_t s = { 0 };
    Z_TreeStats(&s, &z_tags[tag]);

    if (s.count) {
        Com_WPrintf("************* Z_LeakTest *************\n"
                    "%s leaked %zu bytes of memory (%zu object%s)\n"
                    "**************************************\n",
                    z_tagnames[tag],
                    s.bytes, s.count, s.count == 1 ? "" : "s");
    }
}

/*
========================
Z_SetParent

Moves allocation to new parent (can be NULL).
Be careful, circular dependencies are not allowed.
========================
*/
void *Z_SetParent(void *ptr, void *parent)
{
    if (!ptr)
        return NULL;

    zhead_t *z = (zhead_t *)ptr - 1;
    Q_assert(z->magic == Z_MAGIC);

    // unlink from siblings
    if (z->prev)
        z->prev->next = z->next;
    if (z->next)
        z->next->prev = z->prev;

    // if first child, unlink from old parent
    if (z->parent) {
        Q_assert(!z->prev);
        Q_assert(z->parent->magic == Z_MAGIC);
        Q_assert(z->parent->child == z);
        z->parent->child = z->next;
        if (z->next) {
            Q_assert(!z->next->parent);
            z->next->parent = z->parent;
        }
    }

    z->prev = z->next = z->parent = NULL;

    // insert at start of list as first child
    if (parent) {
        zhead_t *p = (zhead_t *)parent - 1;
        Q_assert(p->magic == Z_MAGIC);
        z->next = p->child;
        if (z->next) {
            z->next->prev = z;
            z->next->parent = NULL;
        }
        p->child = z;
        z->parent = p;
    }

    return ptr;
}

/*
========================
Z_Free

Frees memory block and its children recursively.
========================
*/
void Z_Free(void *ptr)
{
    if (!ptr)
        return;

    zhead_t *z = (zhead_t *)ptr - 1;
    Q_assert(z->magic == Z_MAGIC);
    Q_assert(z->size);
    z->size = 0;

    // recursively free children
    while (z->child)
        Z_Free(z->child + 1);

    Z_SetParent(ptr, NULL);
    z->magic = Z_POISON;
    free(z);
}

/*
========================
Z_Stats_f
========================
*/
void Z_Stats_f(void)
{
    size_t bytes = 0, count = 0;

    Com_Printf("    bytes blocks name\n"
               "--------- ------ -------\n");

    for (int i = 0; i < TAG_MAX; i++) {
        zstats_t s = { 0 };
        Z_TreeStats(&s, &z_tags[i]);
        if (!s.count)
            continue;
        Com_Printf("%9zu %6zu %s\n", s.bytes, s.count, z_tagnames[i]);
        bytes += s.bytes;
        count += s.count;
    }

    Com_Printf("--------- ------ -------\n"
               "%9zu %6zu total\n", bytes, count);
}

/*
========================
Z_FreeTags

Frees memory allocated under tag.
========================
*/
void Z_FreeTags(memtag_t tag)
{
    Q_assert(tag < TAG_MAX);
    zhead_t *z = &z_tags[tag];

    // recursively free children
    while (z->child)
        Z_Free(z->child + 1);
}

/*
========================
Z_BlockSize

Returns usable size of memory block.
========================
*/
size_t Z_BlockSize(const void *ptr)
{
    if (!ptr)
        return 0;
    const zhead_t *z = (const zhead_t *)ptr - 1;
    Q_assert(z->magic == Z_MAGIC);
    Q_assert(z->size >= sizeof(*z));
    return z->size - sizeof(*z);
}

/*
========================
Z_TreeMalloc

Allocates zero-initialized memory block under parent.
========================
*/
void *Z_TreeMalloc(void *parent, size_t size)
{
    zhead_t *z;

    if (!size)
        return NULL;

    Q_assert_add(&size, size, sizeof(*z));
    z = calloc(1, size);
    if (!z)
        Com_Error(ERR_FATAL, "%s: couldn't allocate %zu bytes", __func__, size);
    z->magic = Z_MAGIC;
    z->size = size;
    Z_SetParent(z + 1, parent);

    return z + 1;
}

/*
========================
Z_NewContext

Allocates empty memory block under tag that
can be used as parent for other allocations.
========================
*/
void *Z_NewContext(memtag_t tag)
{
    Q_assert(tag < TAG_MAX);
    zhead_t *z = calloc(1, sizeof(*z));
    Q_assert(z);
    z->magic = Z_MAGIC;
    z->size = sizeof(*z);
    Z_SetParent(z + 1, &z_tags[tag] + 1);
    return z + 1;
}

void *Z_TagMalloc(size_t size, memtag_t tag)
{
    Q_assert(tag < TAG_MAX);
    return Z_TreeMalloc(&z_tags[tag] + 1, size);
}

void *Z_Malloc(size_t size)
{
    return Z_TagMalloc(size, TAG_GENERAL);
}

void *Z_MallocArray(void *parent, size_t nmemb, size_t size)
{
    Q_assert_mul(&size, nmemb, size);
    return Z_TreeMalloc(parent, size);
}

void *Z_TagMallocArray(size_t nmemb, size_t size, memtag_t tag)
{
    Q_assert(tag < TAG_MAX);
    return Z_MallocArray(&z_tags[tag] + 1, nmemb, size);
}

/*
========================
Z_TreeRealloc

Reallocates memory block, or allocates new one if ptr is NULL.
0 size frees the block. parent is only used when ptr is NULL.
If block size is grown, new memory is zero-initialized.
========================
*/
void *Z_TreeRealloc(void *parent, void *ptr, size_t size)
{
    zhead_t *z, *old_z;

    if (!ptr)
        return Z_TreeMalloc(parent, size);

    if (!size) {
        Z_Free(ptr);
        return NULL;
    }

    z = (zhead_t *)ptr - 1;
    Q_assert(z->magic == Z_MAGIC);

    Q_assert_add(&size, size, sizeof(*z));
    if (z->size == size)
        return z + 1;

    old_z = z;
    z = realloc(z, size);
    if (!z)
        Com_Error(ERR_FATAL, "%s: couldn't realloc %zu bytes", __func__, size);

    if (size > z->size)
        memset((byte *)z + z->size, 0, size - z->size);

    z->size = size;
    if (z != old_z) {
        if (z->prev)
            z->prev->next = z;
        if (z->next)
            z->next->prev = z;
        if (z->parent)
            z->parent->child = z;
        if (z->child)
            z->child->parent = z;
    }

    return z + 1;
}

void *Z_Realloc(void *ptr, size_t size)
{
    return Z_TreeRealloc(&z_tags[TAG_GENERAL] + 1, ptr, size);
}

void *Z_ReallocArray(void *parent, void *ptr, size_t nmemb, size_t size)
{
    Q_assert_mul(&size, nmemb, size);
    return Z_TreeRealloc(parent, ptr, size);
}

void *Z_TagReallocArray(void *ptr, size_t nmemb, size_t size, memtag_t tag)
{
    Q_assert(tag < TAG_MAX);
    return Z_ReallocArray(&z_tags[tag] + 1, ptr, nmemb, size);
}

/*
================
Z_TreeCopyString
================
*/
char *Z_TreeCopyString(void *parent, const char *in)
{
    if (in) {
        size_t len = strlen(in) + 1;
        return memcpy(Z_TreeMalloc(parent, len), in, len);
    }
    return NULL;
}

/*
================
Z_TagCopyString
================
*/
char *Z_TagCopyString(const char *in, memtag_t tag)
{
    Q_assert(tag < TAG_MAX);
    return Z_TreeCopyString(&z_tags[tag] + 1, in);
}
