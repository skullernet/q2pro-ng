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

This file incorporates work covered by the following copyright and
permission notice:

Copyright (C) Joel Martin <github@martintribe.org>

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

#pragma once

#define VM_ASSERT_FUNC(cond, msg, func) \
    do { if (!(cond)) Com_Error(ERR_DROP, "%s: %s", func, msg); } while (0)

#define VM_ASSERT(cond, msg) VM_ASSERT_FUNC(cond, msg, __func__)
#define VM_ASSERT2(cond, msg) VM_ASSERT_FUNC(cond, msg, func)

#define VM_PAGE_SIZE    0x10000

typedef struct {
    uint8_t    value_type;
    union {
        uint32_t   u32;
        int32_t    i32;
        uint64_t   u64;
        int64_t    i64;
        float      f32;
        double     f64;
    } value;
} vm_value_t;

typedef struct {
    uint32_t    initial;     // initial size (64K pages)
    uint32_t    maximum;     // maximum size (64K pages)
    uint32_t    pages;       // current size (64K pages)
    uint8_t    *bytes;       // memory area
} vm_memory_t;

typedef void (*vm_thunk_t)(const vm_memory_t *, vm_value_t *);

typedef struct {
    vm_thunk_t thunk;
    const char *name;
    const char *mask;
} vm_import_t;

static inline void *VM_GetPointer(const vm_memory_t *m, uint32_t ptr, uint32_t size,
                                  uint32_t nmemb, uint32_t align, const char *func)
{
    VM_ASSERT2(ptr, "Null VM pointer");
    VM_ASSERT2(!(ptr & (align - 1)), "Misaligned VM pointer");
    VM_ASSERT2((uint64_t)ptr + (uint64_t)size * nmemb <= m->pages * VM_PAGE_SIZE, "Out of bounds VM pointer");
    return m->bytes + ptr;
}

#define VM_PTR_CNT(arg, type, cnt) \
    ((type *)VM_GetPointer(m, VM_U32(arg), sizeof(type), cnt, q_alignof(type), __func__))

#define VM_PTR_NULL_CNT(arg, type, cnt) \
    (VM_U32(arg) ? VM_PTR_CNT(arg, type, cnt) : NULL)

#define VM_PTR_NULL(arg, type) VM_PTR_NULL_CNT(arg, type, 1)

#define VM_VEC3(arg) VM_PTR_CNT(arg, vec_t, 3)
#define VM_VEC3_NULL(arg) VM_PTR_NULL_CNT(arg, vec_t, 3)

#define VM_PTR(arg, type) VM_PTR_CNT(arg, type, 1)

#define VM_STR_BUF(arg, siz) VM_PTR_CNT(arg, char, VM_U32(siz))
#define VM_STR_NULL(arg) VM_PTR_NULL(arg, char)
#define VM_STR(arg) VM_PTR(arg, char)

#define VM_U32(arg) stack[arg].value.u32
#define VM_I32(arg) stack[arg].value.i32
#define VM_U64(arg) stack[arg].value.u64
#define VM_I64(arg) stack[arg].value.i64
#define VM_F32(arg) stack[arg].value.f32
#define VM_F64(arg) stack[arg].value.f64

#define VM_THUNK(x) \
    static void thunk_##x(const vm_memory_t *m, vm_value_t *stack)

#define VM_IMPORT_RAW(thunk, name, mask) \
    { thunk, name, mask }
#define VM_IMPORT(name, mask) \
    { thunk_##name, "trap_"#name, mask }

typedef struct vm_s vm_t;

int VM_GetExport(vm_t *m, const char *name);
vm_t *VM_Load(const char *name, const vm_import_t *imports);
void VM_Free(vm_t *m);
void VM_Call(vm_t *m, uint32_t e);
vm_value_t *VM_StackPush(vm_t *m, int n);
vm_value_t *VM_StackPop(vm_t *m);
const vm_memory_t *VM_Memory(vm_t *m);
void VM_Reset(vm_t *m);
