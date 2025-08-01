/*
Copyright (C) 2012 Andrey Nazarov

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

//
// platform.h -- platform-specific definitions
//

#pragma once

#ifndef Q2_VM

#include <sys/types.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <io.h>
#include <direct.h>
#else
#include <unistd.h>
#endif

#ifdef _WIN32
#define LIBSUFFIX   ".dll"
#elif (defined __APPLE__)
#define LIBSUFFIX   ".dylib"
#else
#define LIBSUFFIX   ".so"
#endif

#ifdef _WIN32
#define PATH_SEP_CHAR       '\\'
#define PATH_SEP_STRING     "\\"
#else
#define PATH_SEP_CHAR       '/'
#define PATH_SEP_STRING     "/"
#endif

#ifdef _WIN32
#define os_mkdir(p)         _mkdir(p)
#define os_unlink(p)        _unlink(p)
#define os_stat(p, s)       _stat64(p, s)
#define os_fstat(f, s)      _fstat64(f, s)
#define os_fseek(f, o, w)   _fseeki64(f, o, w)
#define os_ftell(f)         _ftelli64(f)
#define os_fileno(f)        _fileno(f)
#define os_access(p, m)     _access(p, (m) & ~X_OK)
#define Q_ISREG(m)          (((m) & _S_IFMT) == _S_IFREG)
#define Q_ISDIR(m)          (((m) & _S_IFMT) == _S_IFDIR)
#define Q_STATBUF           struct _stat64
#else
#define os_mkdir(p)         mkdir(p, 0775)
#define os_unlink(p)        unlink(p)
#define os_stat(p, s)       stat(p, s)
#define os_fstat(f, s)      fstat(f, s)
#define os_fseek(f, o, w)   fseeko(f, o, w)
#define os_ftell(f)         ftello(f)
#define os_fileno(f)        fileno(f)
#define os_access(p, m)     access(p, m)
#define Q_ISREG(m)          S_ISREG(m)
#define Q_ISDIR(m)          S_ISDIR(m)
#define Q_STATBUF           struct stat
#endif

#ifndef F_OK
#define F_OK    0
#define X_OK    1
#define W_OK    2
#define R_OK    4
#endif

#endif /* !Q2_VM */

#ifdef __has_builtin
#define q_has_builtin(x)    __has_builtin(x)
#else
#define q_has_builtin(x)    0
#endif

#ifdef __GNUC__

#if (defined __MINGW32__) && !(defined __clang__)
#define q_printf(f, a)      __attribute__((format(gnu_printf, f, a)))
#else
#define q_printf(f, a)      __attribute__((format(printf, f, a)))
#endif
#define q_noreturn          __attribute__((noreturn))
#define q_noreturn_ptr      q_noreturn
#define q_noinline          __attribute__((noinline))
#define q_malloc            __attribute__((malloc))
#define q_sentinel          __attribute__((sentinel))
#define q_cold              __attribute__((cold))

#define q_likely(x)         __builtin_expect(!!(x), 1)
#define q_unlikely(x)       __builtin_expect(!!(x), 0)
#define q_alignof(t)        __alignof__(t)

#ifdef _WIN32
#define q_exported          __attribute__((dllexport))
#else
#define q_exported          __attribute__((visibility("default")))
#endif

#ifdef Q2_VM
#define qvm_exported        __attribute__((visibility("default")))
#else
#define qvm_exported
#endif

#define q_unused            __attribute__((unused))

#if q_has_builtin(__builtin_unreachable)
#define q_unreachable()     __builtin_unreachable()
#else
#define q_unreachable()     abort()
#endif

#define q_forceinline       inline __attribute__((always_inline))

#else /* __GNUC__ */

#ifdef _MSC_VER
#define q_noreturn          __declspec(noreturn)
#define q_noinline          __declspec(noinline)
#define q_malloc            __declspec(restrict)
#define q_alignof(t)        __alignof(t)
#define q_unreachable()     __assume(0)
#define q_forceinline       __forceinline
#else
#define q_noreturn
#define q_noinline
#define q_malloc
#define q_alignof(t)        _Alignof(t)
#define q_unreachable()     abort()
#define q_forceinline       inline
#endif

#define q_printf(f, a)
#define q_noreturn_ptr
#define q_sentinel
#define q_cold

#define q_likely(x)         (x)
#define q_unlikely(x)       (x)

#define q_gameabi

#ifdef _WIN32
#define q_exported          __declspec(dllexport)
#else
#define q_exported
#endif

#define q_unused

#endif /* !__GNUC__ */
