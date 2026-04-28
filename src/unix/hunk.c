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
#include "system/hunk.h"
#include <sys/mman.h>
#include <errno.h>
#include <unistd.h>

static size_t pagesize;

void Hunk_Init(void)
{
    pagesize = sysconf(_SC_PAGESIZE);
    Q_assert(pagesize && !(pagesize & (pagesize - 1)));
}

size_t Hunk_PageSize(void)
{
    return pagesize;
}

void Hunk_Begin(memhunk_t *hunk, size_t maxsize)
{
    void *buf;

    Q_assert(maxsize <= SIZE_MAX - (pagesize - 1));

    if (hunk->base) {
        Q_assert(hunk->maxsize == Q_ALIGN(maxsize, pagesize));
        Q_assert(hunk->cursize == 0);
        return;
    }

    // reserve a huge chunk of memory, but don't commit any yet
    hunk->cursize = 0;
    hunk->maxsize = Q_ALIGN(maxsize, pagesize);
    buf = mmap(NULL, hunk->maxsize, PROT_READ | PROT_WRITE,
               MAP_PRIVATE | MAP_ANON, -1, 0);
    if (buf == MAP_FAILED)
        Com_Error(ERR_FATAL, "%s: couldn't reserve %zu bytes: %s",
                  __func__, hunk->maxsize, strerror(errno));
    hunk->base = buf;
}

void *Hunk_TryAlloc(memhunk_t *hunk, size_t size, size_t align)
{
    size_t cursize;
    void *buf;

    // round to cacheline
    Q_assert(align <= pagesize);
    cursize = Q_ALIGN(hunk->cursize, align);
    Q_assert(cursize <= hunk->maxsize);

    if (size > hunk->maxsize - cursize)
        return NULL;

    buf = (byte *)hunk->base + cursize;
    hunk->cursize = cursize + size;
    return buf;
}

void *Hunk_Alloc(memhunk_t *hunk, size_t size, size_t align)
{
    void *buf = Hunk_TryAlloc(hunk, size, align);
    if (!buf)
        Com_Error(ERR_FATAL, "%s: couldn't allocate %zu bytes", __func__, size);
    return buf;
}

void Hunk_FreeToWatermark(memhunk_t *hunk, size_t size)
{
    Q_assert(size <= hunk->cursize);
    hunk->cursize = size;
}

void Hunk_End(memhunk_t *hunk)
{
    size_t newsize;

    Q_assert(hunk->cursize <= hunk->maxsize);
    newsize = Q_ALIGN(hunk->cursize, pagesize);

    if (newsize < hunk->maxsize) {
#if defined(__linux__)
        void *buf = mremap(hunk->base, hunk->maxsize, newsize, 0);
#elif defined(__NetBSD__)
        void *buf = mremap(hunk->base, hunk->maxsize, NULL, newsize, 0);
#else
        void *unmap_base = (byte *)hunk->base + newsize;
        size_t unmap_len = hunk->maxsize - newsize;
        void *buf = munmap(unmap_base, unmap_len) + (byte *)hunk->base;
#endif
        if (buf != hunk->base)
            Com_Error(ERR_FATAL, "%s: couldn't remap virtual block: %s",
                      __func__, strerror(errno));
    }

    hunk->maxsize = newsize;
}

void Hunk_Free(memhunk_t *hunk)
{
    if (hunk->base && munmap(hunk->base, hunk->maxsize))
        Com_Error(ERR_FATAL, "%s: munmap failed: %s",
                  __func__, strerror(errno));

    memset(hunk, 0, sizeof(*hunk));
}
