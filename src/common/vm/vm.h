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

#include "shared/shared.h"
#include "common/vm.h"
#include "common/zone.h"
#include "opcodes.h"

#define VM_Malloc(size)         Z_TagMallocz(size, TAG_VM)
#define VM_Realloc(ptr, size)   Z_Realloc(ptr, size)

#define ASSERT(cond, ...) \
    do { if (!(cond)) Com_Error(ERR_DROP, __VA_ARGS__); } while (0)

#define WA_MAGIC   0x6d736100
#define WA_VERSION 0x01

#define PAGE_SIZE       0x10000  // 65536
#define STACK_SIZE      0x10000  // 65536
#define BLOCKSTACK_SIZE 0x1000   // 4096
#define CALLSTACK_SIZE  0x1000   // 4096
#define BR_TABLE_SIZE   0x10000  // 65536
#define MAX_LOCALS      0x1000
#define MAX_RESULTS     1

#define I32       0x7f  // -0x01
#define I64       0x7e  // -0x02
#define F32       0x7d  // -0x03
#define F64       0x7c  // -0x04
#define ANYFUNC   0x70  // -0x10
#define FUNC      0x60  // -0x20
#define BLOCK     0x40  // -0x40

#define KIND_FUNCTION 0
#define KIND_TABLE    1
#define KIND_MEMORY   2
#define KIND_GLOBAL   3

typedef struct {
    uint32_t  form;
    uint32_t  param_count;
    uint32_t *params;
    uint32_t  result_count;
    uint32_t  results[MAX_RESULTS];
    uint64_t  mask; // unique mask value for each type
} vm_type_t;

// A block or function
typedef struct {
    uint8_t    block_type;    // 0x00: function, 0x01: init_exp 0x02: block, 0x03: loop, 0x04: if
    uint32_t   fidx;          // function only (index)
    const vm_type_t *type;          // params/results type
    uint32_t   local_count;   // function only
    uint32_t  *locals;        // function only
    uint32_t   start_addr;
    uint32_t   end_addr;
    uint32_t   else_addr;     // if block only
    uint32_t   br_addr;       // blocks only
    char      *export_name;   // function only (exported)
    char      *import_module; // function only (imported)
    char      *import_field;  // function only (imported)
    void      *(*func_ptr)(void); // function only (imported)
} vm_block_t;

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
    const vm_block_t  *block;
    // Saved state
    int         sp;
    int         fp;
    uint32_t    ra;
} vm_frame_t;

typedef struct {
    uint32_t    initial;     // initial table size
    uint32_t    maximum;     // maximum table size
    uint32_t    size;        // current table size
    uint32_t   *entries;
} vm_table_t;

typedef struct {
    uint32_t    initial;     // initial size (64K pages)
    uint32_t    maximum;     // maximum size (64K pages)
    uint32_t    pages;       // current size (64K pages)
    uint8_t    *bytes;       // memory area
    char       *export_name; // when exported
} vm_memory_t;

typedef struct {
    uint32_t    kind;
    char       *name;
    void       *value;
} vm_export_t;

typedef struct vm_s {
    char       *path;           // file path of the wasm module

    uint32_t    byte_count;     // number of bytes in the module
    uint8_t    *bytes;          // module content/bytes

    uint32_t    type_count;     // number of function types
    vm_type_t  *types;          // function types

    uint32_t    import_count;   // number of leading imports in functions
    uint32_t    function_count; // number of function (including imports)
    vm_block_t  *functions;     // imported and locally defined functions
    vm_block_t  **block_lookup; // map of module byte position to Blocks
                                // same length as byte_count
    uint32_t    start_function; // function to run on module load

    vm_table_t  table;
    vm_memory_t memory;

    uint32_t    global_count;   // number of globals
    vm_value_t  *globals;       // globals

    uint32_t    export_count;   // number of exports
    vm_export_t *exports;

    // Runtime state
    uint32_t    pc;                // program counter
    int         sp;                // operand stack pointer
    int         fp;                // current frame pointer into stack
    vm_value_t  stack[STACK_SIZE]; // main operand stack
    int         csp;               // callstack pointer
    vm_frame_t  callstack[CALLSTACK_SIZE]; // callstack
    uint32_t    br_table[BR_TABLE_SIZE]; // br_table branch indexes
} vm_t;

void VM_PushBlock(vm_t *m, const vm_block_t *block, int sp);
void VM_SetupCall(vm_t *m, uint32_t fidx);
void VM_ThunkOut(vm_t *m, uint32_t fidx);
void VM_Interpret(vm_t *m);
