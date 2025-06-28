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

#include "vm.h"
#include "common/intreadwrite.h"

//
// Stack machine (byte code related functions)
//

// Used for control blocks.
static void VM_PushBlock(vm_t *m, const vm_block_t *block, intptr_t sp, intptr_t *csp)
{
    VM_ASSERT(*csp < CALLSTACK_SIZE - 1, "Call stack overflow");

    vm_frame_t *frame = &m->callstack[++(*csp)];
    frame->block = block;
    frame->sp = sp;
}

// Used for both function and control blocks.
static const vm_block_t *VM_PopBlock(vm_t *m, vm_pc_t *pc, intptr_t *sp, intptr_t *csp)
{
    VM_ASSERT(*csp >= 0, "Call stack underflow");

    const vm_frame_t *frame = &m->callstack[(*csp)--];
    const vm_type_t  *type = frame->block->type;

    // Restore stack pointer
    if (type->num_results == 1) {
        // Save top value as result
        if (frame->sp < *sp) {
            m->stack[frame->sp + 1] = m->stack[*sp];
            *sp = frame->sp + 1;
        }
    } else {
        if (frame->sp < *sp) {
            *sp = frame->sp;
        }
    }

    if (frame->block->opcode == 0x00) {
        // Function, restore frame pointer and set pc to return address
        m->fp = frame->fp;
        *pc = frame->ra;
    }

    return frame->block;
}

// Setup a function
// Push params and locals on the stack and save a call frame on the call stack
// Sets new pc value for the start of the function
void VM_SetupCall(vm_t *m, uint32_t fidx)
{
    const vm_block_t  *func = &m->funcs[fidx];
    const vm_type_t   *type = func->type;

    // Push current frame on the call stack
    VM_ASSERT(m->csp < CALLSTACK_SIZE - 1, "Call stack overflow");

    vm_frame_t *frame = &m->callstack[++m->csp];
    frame->block = func;
    frame->sp = m->sp - type->num_params;
    frame->fp = m->fp;
    frame->ra = m->pc;

    // Push params (dropping extras)
    m->fp = m->sp - type->num_params + 1;
    VM_ASSERT(m->fp >= 0, "Stack underflow");

    // Push function locals
    VM_ASSERT(m->sp < STACK_SIZE - (int)func->num_locals, "Stack overflow");
    memset(m->stack + m->sp + 1, 0, sizeof(m->stack[0]) * func->num_locals);
    m->sp += func->num_locals;

    // Set program counter to start of function
    m->pc = m->code + func->start_addr;
}

// Call imported function.
// Pops params and pushes return value.
static void VM_ThunkOut(vm_t *m, uint32_t fidx)
{
    const vm_block_t  *func = &m->funcs[fidx];
    const vm_type_t   *type = func->type;

    int fp = m->sp - type->num_params + 1;
    VM_ASSERT(fp >= 0, "Stack underflow");

    // Pop params
    m->sp -= type->num_params;

    func->thunk(&m->memory, &m->stack[fp]);

    // Push return value
    m->sp += type->num_results;
}

static uint16_t get_u16(vm_pc_t *pc)
{
    uint16_t v = RN16(*pc);
    *pc += 2;
    return v;
}

static uint32_t get_u32(vm_pc_t *pc)
{
    uint32_t v = RN32(*pc);
    *pc += 4;
    return v;
}

static uint64_t get_u64(vm_pc_t *pc)
{
    uint64_t v = RN64(*pc);
    *pc += 8;
    return v;
}

static uint32_t rotl32(uint32_t n, int c)
{
    c &= 31;
    return (n << c) | (n >> (32 - c));
}

static uint32_t rotr32(uint32_t n, int c)
{
    c &= 31;
    return (n >> c) | (n << (32 - c));
}

static uint64_t rotl64(uint64_t n, int c)
{
    c &= 63;
    return (n << c) | (n >> (64 - c));
}

static uint64_t rotr64(uint64_t n, int c)
{
    c &= 63;
    return (n >> c) | (n << (64 - c));
}

#define clz32(x) ((x) ? __builtin_clz(x) : 32)
#define ctz32(x) ((x) ? __builtin_ctz(x) : 32)

#define clz64(x) ((x) ? __builtin_clzll(x) : 64)
#define ctz64(x) ((x) ? __builtin_ctzll(x) : 64)

#define have(n) \
    VM_ASSERT(cur_sp >= n - 1, "Stack underflow")

#define need(n) \
    VM_ASSERT(cur_sp < STACK_SIZE - n, "Stack overflow")

#define save_reg \
    m->pc = cur_pc; \
    m->sp = cur_sp; \
    m->csp = cur_csp

#define load_reg \
    cur_pc = m->pc; \
    cur_sp = m->sp; \
    cur_csp = m->csp

#define fetch_op \
    opcode = *cur_pc++

#define dispatch_op \
    goto *dispatch_table[opcode]

#define dispatch \
    fetch_op; \
    dispatch_op

#define LOAD_OP(op, ty, size, read) \
    do_##op: \
    offset = get_u32(&cur_pc); \
    fetch_op; \
    have(1); \
    addr = stack[cur_sp].u32; \
    VM_ASSERT((uint64_t)addr + (uint64_t)offset + size <= msize, "Memory load out of bounds"); \
    maddr = m->memory.bytes + offset + addr; \
    stack[cur_sp].ty = read(maddr); \
    dispatch_op;

#define STOR_OP(op, ty, size, writ) \
    do_##op: \
    offset = get_u32(&cur_pc); \
    fetch_op; \
    have(2); \
    sval = stack[cur_sp--]; \
    addr = stack[cur_sp--].u32; \
    VM_ASSERT((uint64_t)addr + (uint64_t)offset + size <= msize, "Memory store out of bounds"); \
    maddr = m->memory.bytes + offset + addr; \
    writ(maddr, sval.ty); \
    dispatch_op;

#define UN_OP(op, ty, func) \
    do_##op: \
    fetch_op; \
    have(1); \
    stack[cur_sp].ty = func(stack[cur_sp].ty); \
    dispatch_op;

#define BIN_OP(ty1, ty2, op, stmt) \
    do_##op: { \
    fetch_op; \
    have(2); \
    __typeof__(stack[0].ty1) a = stack[cur_sp - 1].ty1; \
    __typeof__(stack[0].ty1) b = stack[cur_sp].ty1; \
    stack[--cur_sp].ty2 = ({ stmt; }); \
    dispatch_op; }

#define CMP_U32(op, stmt) BIN_OP(u32, u32, op, stmt)
#define CMP_I32(op, stmt) BIN_OP(i32, u32, op, stmt)
#define CMP_U64(op, stmt) BIN_OP(u64, u32, op, stmt)
#define CMP_I64(op, stmt) BIN_OP(i64, u32, op, stmt)
#define CMP_F32(op, stmt) BIN_OP(f32, u32, op, stmt)
#define CMP_F64(op, stmt) BIN_OP(f64, u32, op, stmt)

#define BOP_U32(op, stmt) BIN_OP(u32, u32, op, stmt)
#define BOP_I32(op, stmt) BIN_OP(i32, i32, op, stmt)
#define BOP_U64(op, stmt) BIN_OP(u64, u64, op, stmt)
#define BOP_I64(op, stmt) BIN_OP(i64, i64, op, stmt)
#define BOP_F32(op, stmt) BIN_OP(f32, f32, op, stmt)
#define BOP_F64(op, stmt) BIN_OP(f64, f64, op, stmt)

#define CNV_OP(op, ty1, ty2) \
    do_##op: \
    fetch_op; \
    have(1); \
    stack[cur_sp].ty1 = stack[cur_sp].ty2; \
    dispatch_op;

#define SEX_OP(op, ty, b) \
    do_##op: \
    fetch_op; \
    have(1); \
    stack[cur_sp].ty = (int##b##_t)stack[cur_sp].ty; \
    dispatch_op;

void VM_Interpret(vm_t *m)
{
    vm_value_t *const   stack = m->stack;
    const uint64_t      msize = m->memory.pages * VM_PAGE_SIZE;
    const intptr_t      enter_csp = m->csp;
    const vm_block_t   *block;
    uint32_t     arg, val, fidx, tidx, cond, depth, count, index;
    uint32_t     offset, addr, dst, src, n;
    uint8_t     *maddr;
    vm_value_t   sval;
    uint32_t     opcode;
    vm_pc_t      cur_pc = m->pc;
    intptr_t     cur_sp = m->sp;
    intptr_t     cur_csp = m->csp;

    VM_ASSERT(enter_csp >= 0, "Call stack underflow");

#include "dispatch.h"

    dispatch;

    //
    // Control flow operators
    //
    do_Unreachable:
        VM_ASSERT(0, "Unreachable instruction");

    do_Block:
        index = get_u16(&cur_pc);
        VM_PushBlock(m, &m->blocks[index], cur_sp, &cur_csp);
        dispatch;

    do_If:
        index = get_u16(&cur_pc);
        block = &m->blocks[index];
        VM_PushBlock(m, block, cur_sp, &cur_csp);

        have(1);
        cond = stack[cur_sp--].u32;
        if (cond == 0) { // if false
            // branch to else block or after end of if
            if (block->start_addr == 0) {
                // no else block, pop if block and skip end
                cur_csp--;
                cur_pc = m->code + block->end_addr + 1;
            } else {
                cur_pc = m->code + block->start_addr;
            }
        }
        // if true, keep going
        dispatch;

    do_Else:
        cur_pc = m->code + m->callstack[cur_csp].block->end_addr;
        dispatch;

    do_End:
        block = VM_PopBlock(m, &cur_pc, &cur_sp, &cur_csp);
        if (cur_csp < enter_csp) {
            VM_ASSERT(block->opcode == 0x00, "Not a function");
            save_reg;
            return; // return to top-level from function
        }
        dispatch;

    do_Br:
        depth = get_u16(&cur_pc);
        VM_ASSERT(cur_csp >= depth, "Call stack underflow");
        cur_csp -= depth;
        // set to end for VM_PopBlock
        cur_pc = m->code + m->callstack[cur_csp].block->end_addr;
        dispatch;

    do_BrIf:
        depth = get_u16(&cur_pc);
        have(1);
        cond = stack[cur_sp--].u32;
        if (cond) { // if true
            VM_ASSERT(cur_csp >= depth, "Call stack underflow");
            cur_csp -= depth;
            // set to end for VM_PopBlock
            cur_pc = m->code + m->callstack[cur_csp].block->end_addr;
        }
        dispatch;

    do_BrTable:
        cur_pc += (uintptr_t)cur_pc & 1;
        count = get_u16(&cur_pc);
        const uint16_t *br_table = (const uint16_t *)cur_pc;
        cur_pc += count * 2;

        depth = get_u16(&cur_pc);

        have(1);
        index = stack[cur_sp--].u32;
        if (index < count)
            depth = br_table[index];

        VM_ASSERT(cur_csp >= depth, "Call stack underflow");
        cur_csp -= depth;
        // set to end for VM_PopBlock
        cur_pc = m->code + m->callstack[cur_csp].block->end_addr;
        dispatch;

    do_Return:
        while (cur_csp >= 0 && m->callstack[cur_csp].block->opcode != 0x00)
            cur_csp--;
        // Set the program count to the end of the function
        // The actual VM_PopBlock and return is handled by the end opcode.
        VM_ASSERT(cur_csp >= 0, "Call stack underflow");
        cur_pc = m->code + m->callstack[cur_csp].block->end_addr;
        dispatch;

    //
    // Call operators
    //
    do_Call:
        fidx = get_u16(&cur_pc);
        save_reg;
        if (fidx < m->num_imports) {
            VM_ThunkOut(m, fidx);   // import/thunk call
        } else {
            VM_ASSERT(fidx < m->num_funcs, "Bad function index");
            VM_SetupCall(m, fidx);  // regular function call
        }
        load_reg;
        dispatch;

    do_CallIndirect:
        tidx = get_u16(&cur_pc);
        VM_ASSERT(tidx < m->num_types, "Bad type index");
        have(1);
        val = stack[cur_sp--].u32;
        VM_ASSERT(val < m->table.maximum, "Undefined element in table");
        fidx = m->table.entries[val];
        VM_ASSERT(fidx < m->num_funcs, "Bad function index");
        VM_ASSERT(m->funcs[fidx].type == &m->types[tidx], "Indirect call function type differ");

        save_reg;
        if (fidx < m->num_imports)
            VM_ThunkOut(m, fidx);   // import/thunk call
        else
            VM_SetupCall(m, fidx);  // regular function call
        load_reg;
        dispatch;

    //
    // Parametric operators
    //
    do_Drop:
        fetch_op;
        have(1);
        cur_sp--;
        dispatch_op;

    do_Select:
        fetch_op;
        have(3);
        cond = stack[cur_sp--].u32;
        cur_sp--;
        if (!cond)  // use a instead of b
            stack[cur_sp] = stack[cur_sp + 1];
        dispatch_op;

    //
    // Variable access
    //
    do_LocalGet:
        arg = get_u16(&cur_pc);
        fetch_op;
        need(1);
        stack[++cur_sp] = stack[m->fp + arg];
        dispatch_op;

    do_LocalSet:
        arg = get_u16(&cur_pc);
        fetch_op;
        have(1);
        stack[m->fp + arg] = stack[cur_sp--];
        dispatch_op;

    do_LocalTee:
        arg = get_u16(&cur_pc);
        fetch_op;
        have(1);
        stack[m->fp + arg] = stack[cur_sp];
        dispatch_op;

    do_GlobalGet:
        arg = get_u16(&cur_pc);
        fetch_op;
        need(1);
        stack[++cur_sp] = m->globals[arg];
        dispatch_op;

    do_GlobalSet:
        arg = get_u16(&cur_pc);
        fetch_op;
        have(1);
        m->globals[arg] = stack[cur_sp--];
        dispatch_op;

    //
    // Memory-related operators
    //
    do_MemorySize:
        fetch_op;
        need(1);
        stack[++cur_sp].u32 = m->memory.pages;
        dispatch_op;

    do_MemoryGrow:
        fetch_op;
        have(1);
        uint32_t prev_pages = m->memory.pages;
        uint32_t delta = stack[cur_sp].u32;
        stack[cur_sp].u32 = prev_pages;
        if (delta)
            stack[cur_sp].u32 = -1; // resize not supported
        dispatch_op;

    do_ExtMemoryCopy:
        fetch_op;
        have(3);
        dst = stack[cur_sp - 2].u32;
        src = stack[cur_sp - 1].u32;
        n   = stack[cur_sp    ].u32;
        VM_ASSERT((uint64_t)dst + n <= msize &&
                  (uint64_t)src + n <= msize, "Memory copy out of bounds");
        memmove(m->memory.bytes + dst, m->memory.bytes + src, n);
        cur_sp -= 3;
        dispatch_op;

    do_ExtMemoryFill:
        fetch_op;
        have(3);
        dst = stack[cur_sp - 2].u32;
        src = stack[cur_sp - 1].u32;
        n   = stack[cur_sp    ].u32;
        VM_ASSERT((uint64_t)dst + n <= msize, "Memory fill out of bounds");
        memset(m->memory.bytes + dst, src, n);
        cur_sp -= 3;
        dispatch_op;

    //
    // Memory load operators
    //
    LOAD_OP(I32_Load,     u32, 4, RN32)
    LOAD_OP(I64_Load,     u64, 8, RN64)
    LOAD_OP(I32_Load8_s,  i32, 1, RN8S)
    LOAD_OP(I32_Load8_u,  u32, 1, RN8)
    LOAD_OP(I32_Load16_s, i32, 2, RN16S)
    LOAD_OP(I32_Load16_u, u32, 2, RN16)
    LOAD_OP(I64_Load8_s,  i64, 1, RN8S)
    LOAD_OP(I64_Load8_u,  u64, 1, RN8)
    LOAD_OP(I64_Load16_s, i64, 2, RN16S)
    LOAD_OP(I64_Load16_u, u64, 2, RN16)
    LOAD_OP(I64_Load32_s, i64, 4, RN32S)
    LOAD_OP(I64_Load32_u, u64, 4, RN32)

    //
    // Memory store operators
    //
    STOR_OP(I32_Store,   u32, 4, WN32)
    STOR_OP(I64_Store,   u64, 8, WN64)
    STOR_OP(I32_Store8,  u32, 1, WN8)
    STOR_OP(I32_Store16, u32, 2, WN16)
    STOR_OP(I64_Store8,  u64, 1, WN8)
    STOR_OP(I64_Store16, u64, 2, WN16)
    STOR_OP(I64_Store32, u64, 4, WN32)

    //
    // Constants
    //
    do_I32_Const:
        need(1);
        stack[++cur_sp].u32 = get_u32(&cur_pc);
        dispatch;

    do_I64_Const:
        need(1);
        stack[++cur_sp].u64 = get_u64(&cur_pc);
        dispatch;

    //
    // Comparison operators
    //

    // unary
    do_I32_Eqz:
        fetch_op;
        have(1);
        stack[cur_sp].u32 = stack[cur_sp].u32 == 0;
        dispatch_op;

    do_I64_Eqz:
        fetch_op;
        have(1);
        stack[cur_sp].u32 = stack[cur_sp].u64 == 0;
        dispatch_op;

    // binary i32
    CMP_U32(I32_Eq,   a == b)
    CMP_U32(I32_Ne,   a != b)
    CMP_I32(I32_Lt_s, a <  b)
    CMP_U32(I32_Lt_u, a <  b)
    CMP_I32(I32_Gt_s, a >  b)
    CMP_U32(I32_Gt_u, a >  b)
    CMP_I32(I32_Le_s, a <= b)
    CMP_U32(I32_Le_u, a <= b)
    CMP_I32(I32_Ge_s, a >= b)
    CMP_U32(I32_Ge_u, a >= b)

    // binary i64
    CMP_U64(I64_Eq,   a == b)
    CMP_U64(I64_Ne,   a != b)
    CMP_I64(I64_Lt_s, a <  b)
    CMP_U64(I64_Lt_u, a <  b)
    CMP_I64(I64_Gt_s, a >  b)
    CMP_U64(I64_Gt_u, a >  b)
    CMP_I64(I64_Le_s, a <= b)
    CMP_U64(I64_Le_u, a <= b)
    CMP_I64(I64_Ge_s, a >= b)
    CMP_U64(I64_Ge_u, a >= b)

    // binary f32
    CMP_F32(F32_Eq, a == b)
    CMP_F32(F32_Ne, a != b)
    CMP_F32(F32_Lt, a <  b)
    CMP_F32(F32_Gt, a >  b)
    CMP_F32(F32_Le, a <= b)
    CMP_F32(F32_Ge, a >= b)

    // binary f64
    CMP_F64(F64_Eq, a == b)
    CMP_F64(F64_Ne, a != b)
    CMP_F64(F64_Lt, a <  b)
    CMP_F64(F64_Gt, a >  b)
    CMP_F64(F64_Le, a <= b)
    CMP_F64(F64_Ge, a >= b)

    //
    // Numeric operators
    //

    // unary i32
    UN_OP(I32_Clz,    u32, clz32)
    UN_OP(I32_Ctz,    u32, ctz32)
    UN_OP(I32_Popcnt, u32, __builtin_popcount)

    // unary i64
    UN_OP(I64_Clz,    u64, clz64)
    UN_OP(I64_Ctz,    u64, ctz64)
    UN_OP(I64_Popcnt, u64, __builtin_popcountll)

    // unary f32
    UN_OP(F32_Abs,     f32, fabsf)
    UN_OP(F32_Neg,     f32, -)
    UN_OP(F32_Ceil,    f32, ceilf)
    UN_OP(F32_Floor,   f32, floorf)
    UN_OP(F32_Trunc,   f32, truncf)
    UN_OP(F32_Nearest, f32, rintf)
    UN_OP(F32_Sqrt,    f32, sqrtf)

    // unary f64
    UN_OP(F64_Abs,     f64, fabs)
    UN_OP(F64_Neg,     f64, -)
    UN_OP(F64_Ceil,    f64, ceil)
    UN_OP(F64_Floor,   f64, floor)
    UN_OP(F64_Trunc,   f64, trunc)
    UN_OP(F64_Nearest, f64, rint)
    UN_OP(F64_Sqrt,    f64, sqrt)

    // binary i32
    BOP_U32(I32_Add, a + b)
    BOP_U32(I32_Sub, a - b)
    BOP_U32(I32_Mul, a * b)
    BOP_I32(I32_Div_s,
        VM_ASSERT(b, "Integer divide by zero");
        VM_ASSERT(!(a == INT32_MIN && b == -1), "Integer overflow");
        a / b
    )
    BOP_U32(I32_Div_u,
        VM_ASSERT(b, "Integer divide by zero");
        a / b
    )
    BOP_I32(I32_Rem_s,
        VM_ASSERT(b, "Integer divide by zero");
        !(a == INT32_MIN && b == -1) ? a % b : 0
    )
    BOP_U32(I32_Rem_u,
        VM_ASSERT(b, "Integer divide by zero");
        a % b
    )
    BOP_U32(I32_And,   a & b)
    BOP_U32(I32_Or,    a | b)
    BOP_U32(I32_Xor,   a ^ b)
    BOP_U32(I32_Shl,   a << (b & 31))
    BOP_I32(I32_Shr_s, a >> (b & 31))
    BOP_U32(I32_Shr_u, a >> (b & 31))
    BOP_U32(I32_Rotl,  rotl32(a, b))
    BOP_U32(I32_Rotr,  rotr32(a, b))

    // binary i64
    BOP_U64(I64_Add, a + b)
    BOP_U64(I64_Sub, a - b)
    BOP_U64(I64_Mul, a * b)
    BOP_I64(I64_Div_s,
        VM_ASSERT(b, "Integer divide by zero");
        VM_ASSERT(!(a == INT64_MIN && b == -1), "Integer overflow");
        a / b
    )
    BOP_U64(I64_Div_u,
        VM_ASSERT(b, "Integer divide by zero");
        a / b
    )
    BOP_I64(I64_Rem_s,
        VM_ASSERT(b, "Integer divide by zero");
        !(a == INT64_MIN && b == -1) ? a % b : 0
    )
    BOP_U64(I64_Rem_u,
        VM_ASSERT(b, "Integer divide by zero");
        a % b
    )
    BOP_U64(I64_And,   a & b)
    BOP_U64(I64_Or,    a | b)
    BOP_U64(I64_Xor,   a ^ b)
    BOP_U64(I64_Shl,   a << (b & 63))
    BOP_I64(I64_Shr_s, a >> (b & 63))
    BOP_U64(I64_Shr_u, a >> (b & 63))
    BOP_U64(I64_Rotl,  rotl64(a, b))
    BOP_U64(I64_Rotr,  rotr64(a, b))

    // binary f32
    BOP_F32(F32_Add, a + b)
    BOP_F32(F32_Sub, a - b)
    BOP_F32(F32_Mul, a * b)
    BOP_F32(F32_Div, a / b)
    BOP_F32(F32_Min, fminf(a, b))
    BOP_F32(F32_Max, fmaxf(a, b))
    BOP_F32(F32_Copysign, copysignf(a, b))

    // binary f64
    BOP_F64(F64_Add, a + b)
    BOP_F64(F64_Sub, a - b)
    BOP_F64(F64_Mul, a * b)
    BOP_F64(F64_Div, a / b)
    BOP_F64(F64_Min, fmin(a, b))
    BOP_F64(F64_Max, fmax(a, b))
    BOP_F64(F64_Copysign, copysign(a, b))

    // conversion operations
    CNV_OP(I32_Wrap_i64,      u32, u64)
    CNV_OP(I32_Trunc_f32_s,   i32, f32)
    CNV_OP(I32_Trunc_f32_u,   u32, f32)
    CNV_OP(I32_Trunc_f64_s,   i32, f64)
    CNV_OP(I32_Trunc_f64_u,   u32, f64)
    CNV_OP(I64_Extend_i32_s,  i64, i32)
    CNV_OP(I64_Extend_i32_u,  u64, u32)
    CNV_OP(I64_Trunc_f32_s,   i64, f32)
    CNV_OP(I64_Trunc_f32_u,   u64, f32)
    CNV_OP(I64_Trunc_f64_s,   i64, f64)
    CNV_OP(I64_Trunc_f64_u,   u64, f64)
    CNV_OP(F32_Convert_i32_s, f32, i32)
    CNV_OP(F32_Convert_i32_u, f32, u32)
    CNV_OP(F32_Convert_i64_s, f32, i64)
    CNV_OP(F32_Convert_i64_u, f32, u64)
    CNV_OP(F32_Demote_f64,    f32, f64)
    CNV_OP(F64_Convert_i32_s, f64, i32)
    CNV_OP(F64_Convert_i32_u, f64, u32)
    CNV_OP(F64_Convert_i64_s, f64, i64)
    CNV_OP(F64_Convert_i64_u, f64, u64)
    CNV_OP(F64_Promote_f32,   f64, f32)

    // sign extensions
    SEX_OP(I32_Extend8_s,  i32,  8)
    SEX_OP(I32_Extend16_s, i32, 16)
    SEX_OP(I64_Extend8_s,  i64,  8)
    SEX_OP(I64_Extend16_s, i64, 16)
    SEX_OP(I64_Extend32_s, i64, 32)
}

static uint32_t remap_extended_opcode(uint32_t opcode)
{
    switch (opcode) {
        // we don't support saturating conversions
        case I32_Trunc_sat_f32_s: return I32_Trunc_f32_s;
        case I32_Trunc_sat_f32_u: return I32_Trunc_f32_u;
        case I32_Trunc_sat_f64_s: return I32_Trunc_f64_s;
        case I32_Trunc_sat_f64_u: return I32_Trunc_f64_u;
        case I64_Trunc_sat_f32_s: return I64_Trunc_f32_s;
        case I64_Trunc_sat_f32_u: return I64_Trunc_f32_u;
        case I64_Trunc_sat_f64_s: return I64_Trunc_f64_s;
        case I64_Trunc_sat_f64_u: return I64_Trunc_f64_u;

        // remap to unused opcodes
        case MemoryCopy: return ExtMemoryCopy;
        case MemoryFill: return ExtMemoryFill;
    }
    return 0;
}

// merge some opcodes to avoid code duplication
static uint32_t simplify_opcode(uint32_t opcode)
{
    switch (opcode) {
        case F32_Load: return I32_Load;
        case F64_Load: return I64_Load;
        case F32_Store: return I32_Store;
        case F64_Store: return I64_Store;
        case F32_Const: return I32_Const;
        case F64_Const: return I64_Const;
        case Loop: return Block;
    }
    return opcode;
}

static void put_u8(sizebuf_t *out, uint8_t v)
{
    WN8(SZ_GetSpace(out, 1), v);
}

static void put_u16(sizebuf_t *out, uint16_t v)
{
    WN16(SZ_GetSpace(out, 2), v);
}

static void put_u32(sizebuf_t *out, uint32_t v)
{
    WN32(SZ_GetSpace(out, 4), v);
}

static void put_u64(sizebuf_t *out, uint64_t v)
{
    WN64(SZ_GetSpace(out, 8), v);
}

static bool VM_PrepareFunction(vm_t *m, vm_block_t *func, sizebuf_t *in, sizebuf_t *out)
{
    vm_block_t  *block;
    uint16_t     blockstack[BLOCKSTACK_SIZE];
    int          top = -1;
    uint32_t     opcode = Unreachable;
    uint32_t     count, index;

    in->readcount = func->start_addr;
    func->start_addr = out->cursize;
    while (in->readcount <= func->end_addr) {
        uint32_t pos = in->readcount;

        opcode = SZ_ReadByte(in);
        switch (opcode) {
        case Extended:
            index = SZ_ReadLeb(in);
            opcode = remap_extended_opcode(index);
            ASSERT(opcode, "Unrecognized extended opcode %#x", index);
            break;
        case Nop:
        case I32_Reinterpret_f32 ... F64_Reinterpret_i64:
            continue;
        case ExtMemoryCopy:
        case ExtMemoryFill:
            goto badcode;
        }

        put_u8(out, simplify_opcode(opcode));

        switch (opcode) {
        case Block:
        case Loop:
        case If:
            ASSERT(top < BLOCKSTACK_SIZE - 1, "Blockstack overflow");
            ASSERT(m->num_blocks < MAX_BLOCKS, "Too many blocks");
            if (!(m->num_blocks & 1023))
                m->blocks = VM_Realloc(m->blocks, (m->num_blocks + 1024) * sizeof(m->blocks[0]));
            index = m->num_blocks++;
            block = &m->blocks[index];
            block->opcode = opcode;
            block->type = VM_GetBlockType(SZ_ReadLeb(in));
            if (!block->type)
                return false;
            blockstack[++top] = index;
            put_u16(out, index);
            if (opcode == Loop)
                block->end_addr = out->cursize;    // loop: label after start
            break;

        case Else:
            ASSERT(top >= 0, "Blockstack underflow");
            block = &m->blocks[blockstack[top]];
            ASSERT(block->opcode == If, "Else not matched with if");
            block->start_addr = out->cursize;
            break;

        case End:
            if (pos == func->end_addr)
                break;
            ASSERT(top >= 0, "Blockstack underflow");
            block = &m->blocks[blockstack[top--]];
            if (block->opcode != Loop)
                block->end_addr = out->cursize - 1; // block, if: label at end
            break;

        case Br:
        case BrIf:
            ASSERT(top >= 0, "Blockstack underflow");
            index = SZ_ReadLeb(in);
            ASSERT(index <= top, "Bad label");
            put_u16(out, index);
            break;

        case BrTable:
            ASSERT(top >= 0, "Blockstack underflow");
            count = SZ_ReadLeb(in); // target count
            ASSERT(count < BR_TABLE_SIZE, "BrTable size too big");
            out->cursize += out->cursize & 1;
            put_u16(out, count);
            for (uint32_t i = 0; i < count; i++) {
                index = SZ_ReadLeb(in);
                ASSERT(index <= top, "Bad label");
                put_u16(out, index);
            }
            index = SZ_ReadLeb(in); // default target
            ASSERT(index <= top, "Bad label");
            put_u16(out, index);
            break;

        case LocalGet:
        case LocalSet:
        case LocalTee:
            index = SZ_ReadLeb(in);
            ASSERT(index < func->type->num_params + func->num_locals, "Bad local index");
            put_u16(out, index);
            break;

        case GlobalGet:
        case GlobalSet:
            index = SZ_ReadLeb(in);
            ASSERT(index < m->num_globals, "Bad global index");
            put_u16(out, index);
            break;

        case MemorySize:
        case MemoryGrow:
            in->readcount++;
            break;

        case I32_Load ... I64_Store32:
            SZ_ReadLeb(in);
            put_u32(out, SZ_ReadLeb(in));
            break;

        case I32_Const:
            put_u32(out, SZ_ReadSignedLeb(in, 32));
            break;

        case I64_Const:
            put_u64(out, SZ_ReadSignedLeb(in, 64));
            break;

        case Call:
            index = SZ_ReadLeb(in);
            ASSERT(index < m->num_funcs, "Bad function index");
            put_u16(out, index);
            break;

        case CallIndirect:
            index = SZ_ReadLeb(in);
            ASSERT(index < m->num_types, "Bad type index");
            put_u16(out, index);
            index = SZ_ReadLeb(in);
            ASSERT(index == 0, "Only 1 default table supported");
            break;

        case F32_Const:
            put_u32(out, SZ_ReadLong(in));
            break;

        case F64_Const:
            put_u64(out, SZ_ReadLong64(in));
            break;

        case Unreachable:
        case Return:
        case Drop:
        case Select:
        case I32_Eqz ... I64_Extend32_s:
            break;

        case ExtMemoryCopy:
            in->readcount += 2;
            break;
        case ExtMemoryFill:
            in->readcount += 1;
            break;

        default:
        badcode:
            ASSERT(0, "Unrecognized opcode %#x", opcode);
        }
    }

    func->end_addr = out->cursize - 1;

    ASSERT(!out->overflowed, "Output buffer overflowed");
    ASSERT(top == -1, "Function ended in middle of block");
    ASSERT(opcode == End, "Function block doesn't end with End opcode");

    return true;
}

bool VM_PrepareInterpreter(vm_t *m, sizebuf_t *in)
{
    sizebuf_t out;

    m->code = VM_Malloc(in->cursize * 2);
    SZ_InitWrite(&out, m->code, in->cursize * 2);

    for (uint32_t f = m->num_imports; f < m->num_funcs; f++)
        if (!VM_PrepareFunction(m, &m->funcs[f], in, &out))
            return false;

    m->code = VM_Realloc(m->code, Q_ALIGN(out.cursize, 64) + 64);
    m->num_bytes = out.cursize;

    return true;
}
