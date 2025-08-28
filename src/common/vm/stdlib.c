/*
Copyright (C) 2025 Andrey Nazarov

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
#include "common/vm.h"

VM_THUNK(sinf) {
    VM_F32(0) = sinf(VM_F32(0));
}

VM_THUNK(cosf) {
    VM_F32(0) = cosf(VM_F32(0));
}

VM_THUNK(tanf) {
    VM_F32(0) = tanf(VM_F32(0));
}

VM_THUNK(asinf) {
    VM_F32(0) = asinf(VM_F32(0));
}

VM_THUNK(acosf) {
    VM_F32(0) = acosf(VM_F32(0));
}

VM_THUNK(atanf) {
    VM_F32(0) = atanf(VM_F32(0));
}

VM_THUNK(atan2f) {
    VM_F32(0) = atan2f(VM_F32(0), VM_F32(1));
}

VM_THUNK(powf) {
    VM_F32(0) = powf(VM_F32(0), VM_F32(1));
}

VM_THUNK(logf) {
    VM_F32(0) = logf(VM_F32(0));
}

VM_THUNK(log2f) {
    VM_F32(0) = log2f(VM_F32(0));
}

VM_THUNK(log10f) {
    VM_F32(0) = log10f(VM_F32(0));
}

VM_THUNK(memcmp) {
    uint32_t p1   = VM_U32(0);
    uint32_t p2   = VM_U32(1);
    uint32_t size = VM_U32(2);

    VM_ASSERT((uint64_t)p1 + size <= m->bytesize &&
              (uint64_t)p2 + size <= m->bytesize, "Memory compare out of bounds");

    VM_I32(0) = memcmp(m->bytes + p1, m->bytes + p2, size);
}

// Varargs WASM functions pass implicit va_list as the last argument, which is
// a pointer to array of I32/I64 params on shadow stack. snprintf and vsnprintf
// signatures are thus equivalent.
VM_THUNK(snprintf) {
    VM_I32(0) = VM_vsnprintf(m, VM_STR_BUF(0, 1), VM_U32(1), VM_STR(2), VM_U32(3));
}

VM_THUNK(sprintf) {
    VM_I32(0) = VM_vsnprintf(m, VM_STR(0), m->bytesize - VM_U32(0), VM_STR(1), VM_U32(2));
}

VM_THUNK(strtof) {
    const char *s = VM_STR(0);
    char *p;
    float res = strtof(s, &p);
    if (VM_U32(1))
        *VM_PTR(1, uint32_t) = VM_U32(0) + (p - s);
    VM_F32(0) = res;
}

VM_THUNK(strtod) {
    const char *s = VM_STR(0);
    char *p;
    double res = strtod(s, &p);
    if (VM_U32(1))
        *VM_PTR(1, uint32_t) = VM_U32(0) + (p - s);
    VM_F64(0) = res;
}

VM_THUNK(strtoul) {
    const char *s = VM_STR(0);
    char *p;
    unsigned long res = strtoul(s, &p, VM_I32(2));
    if (VM_U32(1))
        *VM_PTR(1, uint32_t) = VM_U32(0) + (p - s);
    VM_U32(0) = min(res, UINT32_MAX);
}

VM_THUNK(strtol) {
    const char *s = VM_STR(0);
    char *p;
    long res = strtol(s, &p, VM_I32(2));
    if (VM_U32(1))
        *VM_PTR(1, uint32_t) = VM_U32(0) + (p - s);
    VM_I32(0) = Q_clipl_int32(res);
}

VM_THUNK(strtoull) {
    const char *s = VM_STR(0);
    char *p;
    unsigned long long res = strtoull(s, &p, VM_I32(2));
    if (VM_U32(1))
        *VM_PTR(1, uint32_t) = VM_U32(0) + (p - s);
    VM_U64(0) = res;
}

VM_THUNK(strtoll) {
    const char *s = VM_STR(0);
    char *p;
    long long res = strtoll(s, &p, VM_I32(2));
    if (VM_U32(1))
        *VM_PTR(1, uint32_t) = VM_U32(0) + (p - s);
    VM_I64(0) = res;
}

const vm_import_t vm_stdlib[] = {
    VM_IMPORT_RAW(sinf, "f f"),
    VM_IMPORT_RAW(cosf, "f f"),
    VM_IMPORT_RAW(tanf, "f f"),
    VM_IMPORT_RAW(asinf, "f f"),
    VM_IMPORT_RAW(acosf, "f f"),
    VM_IMPORT_RAW(atanf, "f f"),
    VM_IMPORT_RAW(atan2f, "f ff"),
    VM_IMPORT_RAW(powf, "f ff"),
    VM_IMPORT_RAW(logf, "f f"),
    VM_IMPORT_RAW(log2f, "f f"),
    VM_IMPORT_RAW(log10f, "f f"),
    VM_IMPORT_RAW(memcmp, "i iii"),
    VM_IMPORT_RAW(snprintf, "i iiii"),
    VM_IMPORT_RAW(sprintf, "i iii"),
    VM_IMPORT_RAW(strtof, "f ii"),
    VM_IMPORT_RAW(strtod, "F ii"),
    VM_IMPORT_RAW(strtoul, "i iii"),
    VM_IMPORT_RAW(strtol, "i iii"),
    VM_IMPORT_RAW(strtoull, "I iii"),
    VM_IMPORT_RAW(strtoll, "I iii"),
    { "vsnprintf", "i iiii", thunk_snprintf },  // redirect vsnprintf to snprintf

    { 0 }
};
