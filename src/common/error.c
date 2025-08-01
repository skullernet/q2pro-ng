/*
Copyright (C) 2010 Andrey Nazarov

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
#include "common/error.h"

static const char *const error_table[] = {
    "Unspecified error",
    "Unknown file format",
    "Invalid file format",
    "Invalid quake path",
    "Unexpected end of file",
    "File too small",
    "Not a regular file",
    "Decompression overrun",
    "String truncation avoided",
    "Infinite loop avoided",
    "Library error",
    "Out of slots",
    "Inflate failed",
    "Deflate failed",
    "Coherency check failed",
    "Bad compression method",
};

const char *Q_ErrorString(int error)
{
    int e;

    if (error >= 0) {
        return "Success";
    }

    if (error > -ERRNO_MAX) {
#if EINVAL > 0
        e = -error;
#else
        e = error;
#endif
        return strerror(e);
    }

    e = Q_ERR_(error);
    if (e >= q_countof(error_table)) {
        e = 0;
    }

    return error_table[e];
}

size_t Q_ErrorStringBuffer(int error, char *buffer, size_t size)
{
    return Q_strlcpy(buffer, Q_ErrorString(error), size);
}
