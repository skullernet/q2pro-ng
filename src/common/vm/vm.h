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
#include "common/common.h"
#include "common/sizebuf.h"
#include "common/vm.h"
#include "opcodes.h"

#define ASSERT(cond, ...) \
    do { if (!(cond)) { Com_SetLastError(va(__VA_ARGS__)); return false; } } while (0)

#define VM_MAGIC    MakeLittleLong(0, 'a', 's', 'm')
#define VM_VERSION  0x01

#define STACK_SIZE      0x10000     // 65536
#define BLOCKSTACK_SIZE 0x1000      // 4096
#define CALLSTACK_SIZE  0x1000      // 4096
#define BR_TABLE_SIZE   0x10000     // 65536
#define MAX_BLOCKS      0x10000
#define MAX_FUNCS       0x10000
#define MAX_LOCALS      0x1000
#define MAX_GLOBALS     0x10000
#define MAX_TYPES       0x10000
#define MAX_RESULTS     1

#define I32         0x7f    // -0x01
#define I64         0x7e    // -0x02
#define F32         0x7d    // -0x03
#define F64         0x7c    // -0x04
#define FUNCREF     0x70    // -0x10
#define FUNC        0x60    // -0x20
#define BLOCK       0x40    // -0x40

#define KIND_FUNCTION   0
#define KIND_TABLE      1
#define KIND_MEMORY     2
#define KIND_GLOBAL     3

typedef struct {
    uint32_t  form;
    uint32_t  num_params;
    uint32_t *params;
    uint32_t  num_results;
    uint32_t  results[MAX_RESULTS];
} vm_type_t;

// A block or function
typedef struct {
    uint32_t   opcode;          // 0x00: function, 0x02: block, 0x03: loop, 0x04: if
    uint32_t   num_locals;      // function only
    union {
        uint32_t  *locals;      // function only
        vm_thunk_t thunk;       // function only (imported)
    };
    uint32_t   start_addr;      // else branch addr for if block
    uint32_t   end_addr;        // branch addr
    const vm_type_t *type;      // params/results type
} vm_block_t;

typedef const uint8_t *vm_pc_t;

typedef struct {
    const vm_block_t  *block;
    // Saved state
    int sp, fp;
    vm_pc_t ra;
} vm_frame_t;

typedef struct {
    uint32_t    initial;     // initial table size
    uint32_t    maximum;     // maximum table size
    uint32_t    size;        // current table size
    uint32_t   *entries;
} vm_table_t;

typedef struct {
    char       *data;
    uint32_t    len;
} vm_string_t;

// Internal WASM export
typedef struct {
    uint32_t    kind;
    vm_string_t name;
    void       *value;
} wa_export_t;

typedef struct vm_s {
    const vm_import_t  *imports;

    uint32_t    num_bytes;      // number of bytes in the module
    uint8_t    *code;           // module content/bytes

    uint32_t    num_types;      // number of function types
    vm_type_t  *types;          // function types

    uint32_t    num_imports;    // number of leading imports in functions
    uint32_t    num_funcs;      // number of function (including imports)
    vm_block_t  *funcs;         // imported and locally defined functions

    vm_table_t  table;
    vm_memory_t memory;

    vm_value_t *llvm_stack_pointer;
    vm_value_t  llvm_stack_start;

    uint32_t    num_globals;    // number of globals
    vm_value_t  *globals;       // globals

    uint32_t    num_exports;    // number of exports
    wa_export_t *exports;

    uint32_t    num_func_exports;
    uint32_t   *func_exports;

    // Runtime state
    vm_pc_t     pc;                // program counter
    int         sp;                // operand stack pointer
    int         fp;                // current frame pointer into stack
    vm_value_t  stack[STACK_SIZE]; // main operand stack
    int         csp;               // callstack pointer
    vm_frame_t  callstack[CALLSTACK_SIZE]; // callstack
} vm_t;

void VM_SetupCall(vm_t *m, uint32_t fidx);
void VM_Interpret(vm_t *m);
const vm_type_t *VM_GetBlockType(uint32_t value_type);
bool VM_PrepareInterpreter(vm_t *m, sizebuf_t *sz);
