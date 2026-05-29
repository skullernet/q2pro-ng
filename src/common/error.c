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

#include "shared/shared.h"
#include "common/error.h"
#include <errno.h>

static const char *const error_table[] = {
    [-Q_ERR_FAILURE] = "Unspecified error",
    [-Q_ERR_OUT_OF_MEMORY] = "Out of memory",
    [-Q_ERR_OUT_OF_RANGE] = "Out of range",
    [-Q_ERR_OUT_OF_SLOTS] = "Out of slots",
    [-Q_ERR_INVALID_HANDLE] = "Invalid handle",
    [-Q_ERR_INVALID_ARGS] = "Invalid argument",
    [-Q_ERR_INVALID_OPERATION] = "Invalid operation",
    [-Q_ERR_INVALID_DATA] = "Invalid data found",
    [-Q_ERR_INVALID_PATH] = "Invalid quake path",
    [-Q_ERR_PATH_TOO_LONG] = "File path too long",
    [-Q_ERR_TOO_MANY_LINKS] = "Too many links",
    [-Q_ERR_ACCESS_DENIED] = "Access denied",
    [-Q_ERR_DOES_NOT_EXIST] = "No such file or directory",
    [-Q_ERR_ALREADY_EXISTS] = "File exists",
    [-Q_ERR_TOO_MANY_OPEN_FILES] = "Too many open files",
    [-Q_ERR_UNEXPECTED_EOF] = "Unexpected end of file",
    [-Q_ERR_FILE_TOO_SMALL] = "File too small",
    [-Q_ERR_FILE_TOO_BIG] = "File too big",
    [-Q_ERR_FILE_NOT_REGULAR] = "Not a regular file",
    [-Q_ERR_NOT_DIRECTORY] = "Not a directory",
    [-Q_ERR_IS_DIRECTORY] = "Is a directory",
    [-Q_ERR_DIRECTORY_NOT_EMPTY] = "Directory not empty",
    [-Q_ERR_NO_SPACE_LEFT] = "No space left on device",
    [-Q_ERR_READ_ONLY_FS] = "Read-only file system",
    [-Q_ERR_DEADLOCK] = "Resource deadlock would occur",
    [-Q_ERR_TIMEOUT] = "Operation timed out",
    [-Q_ERR_BUSY] = "Device or resource busy",
    [-Q_ERR_IO] = "Input/output error",
    [-Q_ERR_BAD_SEEK] = "Illegal seek",
    [-Q_ERR_BAD_PIPE] = "Broken pipe",
    [-Q_ERR_BUFFER_TOO_SMALL] = "Buffer too small",
    [-Q_ERR_EXTERNAL] = "External library error",
    [-Q_ERR_NOT_IMPLEMENTED] = "Function not implemented",
    [-Q_ERR_NOT_COHERENT] = "Coherency check failed",
    [-Q_ERR_BAD_COMPRESSION] = "Bad compression method",
    [-Q_ERR_CHECKSUM_MISMATCH] = "Checksum mismatch",
};

qerror_t Q_Errno(void)
{
    switch (errno) {
    case ENOMEM:
        return Q_ERR_OUT_OF_MEMORY;
    case EOVERFLOW:
    case ERANGE:
    case EDOM:
        return Q_ERR_OUT_OF_RANGE;
    case EBADF:
        return Q_ERR_INVALID_HANDLE;
    case EINVAL:
        return Q_ERR_INVALID_ARGS;
    case ENAMETOOLONG:
        return Q_ERR_PATH_TOO_LONG;
    case EMLINK:
    case ELOOP:
        return Q_ERR_TOO_MANY_LINKS;
    case ENOENT:
        return Q_ERR_DOES_NOT_EXIST;
    case EEXIST:
        return Q_ERR_ALREADY_EXISTS;
    case ENFILE:
    case EMFILE:
        return Q_ERR_TOO_MANY_OPEN_FILES;
    case EFBIG:
        return Q_ERR_FILE_TOO_BIG;
    case EPERM:
    case EACCES:
        return Q_ERR_ACCESS_DENIED;
    case ENOTDIR:
        return Q_ERR_NOT_DIRECTORY;
    case EISDIR:
        return Q_ERR_IS_DIRECTORY;
    case ENOTEMPTY:
        return Q_ERR_DIRECTORY_NOT_EMPTY;
    case ENOSPC:
        return Q_ERR_NO_SPACE_LEFT;
    case EROFS:
        return Q_ERR_READ_ONLY_FS;
    case EDEADLK:
        return Q_ERR_DEADLOCK;
    case ETIMEDOUT:
        return Q_ERR_TIMEOUT;
    case EBUSY:
        return Q_ERR_BUSY;
    case EIO:
        return Q_ERR_IO;
    case ESPIPE:
        return Q_ERR_BAD_SEEK;
    case EPIPE:
        return Q_ERR_BAD_PIPE;
    case ENOSYS:
        return Q_ERR_NOT_IMPLEMENTED;
    default:
        return Q_ERR_FAILURE;
    }
}

const char *Q_ErrorString(qerror_t error)
{
    if (error >= 0)
        return "Success";

    error = -error;
    if (error >= q_countof(error_table) || !error_table[error])
        error = -Q_ERR_FAILURE;

    return error_table[error];
}

size_t Q_ErrorStringBuffer(qerror_t error, char *buffer, size_t size)
{
    return Q_strlcpy(buffer, Q_ErrorString(error), size);
}
