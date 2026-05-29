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

#pragma once

typedef enum {
    Q_ERR_SUCCESS               = 0,
    Q_ERR_FAILURE               = -1,
    Q_ERR_OUT_OF_MEMORY         = -2,
    Q_ERR_OUT_OF_RANGE          = -3,
    Q_ERR_OUT_OF_SLOTS          = -4,
    Q_ERR_INVALID_HANDLE        = -5,
    Q_ERR_INVALID_ARGS          = -6,
    Q_ERR_INVALID_OPERATION     = -7,
    Q_ERR_INVALID_DATA          = -8,
    Q_ERR_INVALID_PATH          = -9,
    Q_ERR_PATH_TOO_LONG         = -10,
    Q_ERR_TOO_MANY_LINKS        = -11,
    Q_ERR_ACCESS_DENIED         = -12,
    Q_ERR_DOES_NOT_EXIST        = -13,
    Q_ERR_ALREADY_EXISTS        = -14,
    Q_ERR_TOO_MANY_OPEN_FILES   = -15,
    Q_ERR_UNEXPECTED_EOF        = -16,
    Q_ERR_FILE_TOO_SMALL        = -17,
    Q_ERR_FILE_TOO_BIG          = -18,
    Q_ERR_FILE_NOT_REGULAR      = -19,
    Q_ERR_NOT_DIRECTORY         = -20,
    Q_ERR_IS_DIRECTORY          = -21,
    Q_ERR_DIRECTORY_NOT_EMPTY   = -22,
    Q_ERR_NO_SPACE_LEFT         = -23,
    Q_ERR_READ_ONLY_FS          = -24,
    Q_ERR_DEADLOCK              = -25,
    Q_ERR_TIMEOUT               = -26,
    Q_ERR_BUSY                  = -27,
    Q_ERR_IO                    = -28,
    Q_ERR_BAD_SEEK              = -29,
    Q_ERR_BAD_PIPE              = -30,
    Q_ERR_BUFFER_TOO_SMALL      = -31,
    Q_ERR_EXTERNAL              = -32,
    Q_ERR_NOT_IMPLEMENTED       = -33,
    Q_ERR_NOT_COHERENT          = -34,
    Q_ERR_BAD_COMPRESSION       = -35,
    Q_ERR_CHECKSUM_MISMATCH     = -36,
} qerror_t;
