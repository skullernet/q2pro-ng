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
#include <windows.h>

static size_t pagesize;

void Hunk_Init(void)
{
    SYSTEM_INFO si = { 0 };

    GetSystemInfo(&si);
    pagesize = si.dwPageSize;
    Q_assert(pagesize && !(pagesize & (pagesize - 1)));
}

size_t Hunk_PageSize(void)
{
    return pagesize;
}

void Hunk_Begin(memhunk_t *hunk, size_t maxsize)
{
    Q_assert(maxsize <= SIZE_MAX - (pagesize - 1));

    if (hunk->base) {
        Q_assert(hunk->maxsize == Q_ALIGN(maxsize, pagesize));
        Q_assert(hunk->cursize == 0);
        return;
    }

    // reserve a huge chunk of memory, but don't commit any yet
    hunk->cursize = 0;
    hunk->maxsize = Q_ALIGN(maxsize, pagesize);
    hunk->base = VirtualAlloc(NULL, hunk->maxsize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!hunk->base)
        Com_Error(ERR_FATAL,
                  "VirtualAlloc reserve %zu bytes failed with error %lu",
                  hunk->maxsize, GetLastError());
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
        Q_assert(hunk->base);
        VirtualFree((byte *)hunk->base + newsize, hunk->maxsize - newsize, MEM_DECOMMIT);
    }

    hunk->maxsize = newsize;
}

void Hunk_Free(memhunk_t *hunk)
{
    if (hunk->base && !VirtualFree(hunk->base, 0, MEM_RELEASE))
        Com_Error(ERR_FATAL, "VirtualFree failed with error %lu", GetLastError());

    memset(hunk, 0, sizeof(*hunk));
}
