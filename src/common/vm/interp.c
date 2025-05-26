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

// Size of memory load.
// This starts with the first memory load operator at opcode 0x28
static const uint8_t mem_load_size[I64_Store32 - I32_Load + 1] = {
    4, 8, 4, 8, 1, 1, 2, 2, 1, 1, 2, 2, 4, 4, // loads
    4, 8, 4, 8, 1, 2, 1, 2, 4 // stores
};

//
// Stack machine (byte code related functions)
//

void VM_PushBlock(vm_t *m, const vm_block_t *block, int sp)
{
    ASSERT(m->csp < CALLSTACK_SIZE - 1, "Call stack overflow");

    vm_frame_t *frame = &m->callstack[++m->csp];
    frame->block = block;
    frame->sp = sp;
    frame->fp = m->fp;
    frame->ra = m->pc;
}

static const vm_block_t *VM_PopBlock(vm_t *m)
{
    ASSERT(m->csp >= 0, "Call stack underflow");

    const vm_frame_t *frame = &m->callstack[m->csp--];
    const vm_type_t *t = frame->block->type;

    m->fp = frame->fp; // Restore frame pointer

    // Validate the return value
    if (t->num_results == 1)
        ASSERT(m->stack[m->sp].value_type == t->results[0], "Call type mismatch");

    // Restore stack pointer
    if (t->num_results == 1) {
        // Save top value as result
        if (frame->sp < m->sp) {
            m->stack[frame->sp + 1] = m->stack[m->sp];
            m->sp = frame->sp + 1;
        }
    } else {
        if (frame->sp < m->sp) {
            m->sp = frame->sp;
        }
    }

    if (frame->block->block_type == 0x00) {
        // Function, set pc to return address
        m->pc = frame->ra;
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
    VM_PushBlock(m, func, m->sp - type->num_params);

    // Push locals (dropping extras)
    m->fp = m->sp - type->num_params + 1;

    // Validate arguments vs formal params
    for (uint32_t f = 0; f < type->num_params; f++)
        ASSERT(type->params[f] == m->stack[m->fp + f].value_type, "Function call param types differ");

    // Push function locals
    for (uint32_t lidx = 0; lidx < func->num_locals; lidx++) {
        m->sp++;
        m->stack[m->sp].value_type = func->locals[lidx];
        m->stack[m->sp].value.u64 = 0; // Initialize whole union to 0
    }

    // Set program counter to start of function
    m->pc = func->start_addr;
}

void VM_ThunkOut(vm_t *m, uint32_t fidx)
{
    const vm_block_t  *func = &m->funcs[fidx];
    const vm_type_t   *type = func->type;

    // Validate arguments vs formal params
    uint32_t fp = m->sp - type->num_params + 1;
    for (uint32_t f = 0; f < type->num_params; f++)
        ASSERT(type->params[f] == m->stack[fp + f].value_type, "Function call param types differ");

    func->thunk(&m->memory, &m->stack[fp]);
    m->sp += type->num_results - type->num_params;

    // Set return value type
    if (type->num_results == 1)
        m->stack[m->sp].value_type = type->results[0];
}

static uint32_t read_leb(vm_t *m)
{
    uint32_t v = 0;
    int c, bits = 0;

    do {
        c = m->bytes[m->pc++];
        v |= (c & UINT32_C(0x7f)) << bits;
        bits += 7;
    } while (c & 0x80);

    return v;
}

static int32_t read_leb_si(vm_t *m)
{
    uint32_t v = 0;
    int c, bits = 0;

    do {
        c = m->bytes[m->pc++];
        v |= (c & UINT32_C(0x7f)) << bits;
        bits += 7;
    } while (c & 0x80);

    if (bits < 32)
        return SignExtend(v, bits);
    return v;
}

static int64_t read_leb64_si(vm_t *m)
{
    uint64_t v = 0;
    int c, bits = 0;

    do {
        c = m->bytes[m->pc++];
        v |= (c & UINT64_C(0x7f)) << bits;
        bits += 7;
    } while (c & 0x80);

    if (bits < 64)
        return SignExtend64(v, bits);
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

void VM_Interpret(vm_t *m)
{
    vm_value_t          *stack = m->stack;
    const vm_block_t    *block;
    uint32_t     cur_pc;
    uint32_t     arg, val, fidx, tidx, cond, depth, count;
    uint32_t     offset, addr, dst, src, n;
    uint8_t     *maddr;
    uint32_t     opcode;
    uint32_t     a, b, c; // I32 math
    uint64_t     d, e, f; // I64 math
    float        g, h, i; // F32 math
    double       j, k, l; // F64 math

    while (1) {
        ASSERT(m->pc < m->num_bytes, "Program counter out of bounds");

        cur_pc = m->pc;
        opcode = m->bytes[m->pc++];

        switch (opcode) {
        //
        // Control flow operators
        //
        case Unreachable:
            ASSERT(0, "Unreachable instruction");
            continue;

        case Nop:
            continue;

        case Block:
            read_leb(m);  // ignore block type
            VM_PushBlock(m, m->block_lookup[cur_pc], m->sp);
            continue;

        case Loop:
            read_leb(m);  // ignore block type
            VM_PushBlock(m, m->block_lookup[cur_pc], m->sp);
            continue;

        case If:
            read_leb(m);  // ignore block type
            block = m->block_lookup[cur_pc];
            VM_PushBlock(m, block, m->sp);

            cond = stack[m->sp--].value.u32;
            if (cond == 0) { // if false
                // branch to else block or after end of if
                if (block->else_addr == 0) {
                    // no else block, pop if block and skip end
                    m->csp--;
                    m->pc = block->br_addr + 1;
                } else {
                    m->pc = block->else_addr;
                }
            }
            // if true, keep going
            continue;

        case Else:
            block = m->callstack[m->csp].block;
            m->pc = block->br_addr;
            continue;

        case End:
            block = VM_PopBlock(m);
            if (block->block_type == 0x00 && m->csp == -1)
                return; // return to top-level from function
            continue;

        case Br:
            depth = read_leb(m);
            ASSERT(m->csp >= depth, "Call stack underflow");
            m->csp -= depth;
            // set to end for VM_PopBlock
            m->pc = m->callstack[m->csp].block->br_addr;
            continue;

        case BrIf:
            depth = read_leb(m);
            cond = stack[m->sp--].value.u32;
            if (cond) { // if true
                ASSERT(m->csp >= depth, "Call stack underflow");
                m->csp -= depth;
                // set to end for VM_PopBlock
                m->pc = m->callstack[m->csp].block->br_addr;
            }
            continue;

        case BrTable:
            count = read_leb(m);
            ASSERT(count <= BR_TABLE_SIZE, "BrTable size too big");
            for (uint32_t i = 0; i < count; i++)
                m->br_table[i] = read_leb(m);

            depth = read_leb(m);

            int32_t didx = stack[m->sp--].value.i32;
            if (didx >= 0 && didx < count)
                depth = m->br_table[didx];

            ASSERT(m->csp >= depth, "Call stack underflow");
            m->csp -= depth;
            // set to end for VM_PopBlock
            m->pc = m->callstack[m->csp].block->br_addr;
            continue;

        case Return:
            while (m->csp >= 0 && m->callstack[m->csp].block->block_type != 0x00)
                m->csp--;
            // Set the program count to the end of the function
            // The actual VM_PopBlock and return is handled by the end opcode.
            m->pc = m->callstack[0].block->end_addr;
            continue;

        //
        // Call operators
        //
        case Call:
            fidx = read_leb(m);
            if (fidx < m->num_imports) {
                VM_ThunkOut(m, fidx);   // import/thunk call
            } else {
                ASSERT(fidx < m->num_funcs, "Bad function index");
                VM_SetupCall(m, fidx);  // regular function call
            }
            continue;

        case CallIndirect:
            tidx = read_leb(m);
            ASSERT(tidx < m->num_types, "Bad type index");
            read_leb(m); // ignore default table
            val = stack[m->sp--].value.u32;
            ASSERT(val < m->table.maximum, "Undefined element %#x in table", val);
            fidx = m->table.entries[val];
            ASSERT(fidx < m->num_funcs, "Bad function index");

            const vm_block_t *func = &m->funcs[fidx];
            const vm_type_t *type = func->type;

            ASSERT(type == &m->types[tidx], "Indirect call function type differ");

            if (fidx < m->num_imports)
                VM_ThunkOut(m, fidx);   // import/thunk call
            else
                VM_SetupCall(m, fidx);  // regular function call
            continue;

        //
        // Parametric operators
        //
        case Drop:
            m->sp--;
            continue;

        case Select:
            cond = stack[m->sp--].value.u32;
            m->sp--;
            if (!cond)  // use a instead of b
                stack[m->sp] = stack[m->sp + 1];
            continue;

        //
        // Variable access
        //
        case LocalGet:
            arg = read_leb(m);
            stack[++m->sp] = stack[m->fp + arg];
            continue;
        case LocalSet:
            arg = read_leb(m);
            stack[m->fp + arg] = stack[m->sp--];
            continue;
        case LocalTee:
            arg = read_leb(m);
            stack[m->fp + arg] = stack[m->sp];
            continue;
        case GlobalGet:
            arg = read_leb(m);
            stack[++m->sp] = m->globals[arg];
            continue;
        case GlobalSet:
            arg = read_leb(m);
            m->globals[arg] = stack[m->sp--];
            continue;

        //
        // Memory-related operators
        //
        case MemorySize:
            m->pc++; // ignore reserved
            stack[++m->sp].value_type = I32;
            stack[m->sp].value.u32 = m->memory.pages;
            continue;

        case MemoryGrow:
            m->pc++; // ignore reserved
            uint32_t prev_pages = m->memory.pages;
            uint32_t delta = stack[m->sp].value.u32;
            stack[m->sp].value.u32 = prev_pages;
            if (delta == 0)
                continue; // no change
            stack[m->sp].value.u32 = -1;    // resize not supported
            continue;

        case Extended:
            opcode = read_leb(m);
            switch (opcode) {
            case I32_Trunc_sat_f32_s:
                stack[m->sp].value.i32 = stack[m->sp].value.f32;
                stack[m->sp].value_type = I32;
                break;
            case I32_Trunc_sat_f32_u:
                stack[m->sp].value.u32 = stack[m->sp].value.f32;
                stack[m->sp].value_type = I32;
                break;
            case I32_Trunc_sat_f64_s:
                stack[m->sp].value.i32 = stack[m->sp].value.f64;
                stack[m->sp].value_type = I32;
                break;
            case I32_Trunc_sat_f64_u:
                stack[m->sp].value.u32 = stack[m->sp].value.f64;
                stack[m->sp].value_type = I32;
                break;
            case I64_Trunc_sat_f32_s:
                stack[m->sp].value.i64 = stack[m->sp].value.f32;
                stack[m->sp].value_type = I64;
                break;
            case I64_Trunc_sat_f32_u:
                stack[m->sp].value.u64 = stack[m->sp].value.f32;
                stack[m->sp].value_type = I64;
                break;
            case I64_Trunc_sat_f64_s:
                stack[m->sp].value.i64 = stack[m->sp].value.f64;
                stack[m->sp].value_type = I64;
                break;
            case I64_Trunc_sat_f64_u:
                stack[m->sp].value.u64 = stack[m->sp].value.f64;
                stack[m->sp].value_type = I64;
                break;
            case MemoryCopy:
                dst = stack[m->sp - 2].value.u32;
                src = stack[m->sp - 1].value.u32;
                n   = stack[m->sp    ].value.u32;
                ASSERT((uint64_t)dst + n <= m->memory.pages * VM_PAGE_SIZE &&
                       (uint64_t)src + n <= m->memory.pages * VM_PAGE_SIZE, "Memory copy out of bounds");
                memmove(m->memory.bytes + dst, m->memory.bytes + src, n);
                m->pc += 2;
                m->sp -= 3;
                continue;

            case MemoryFill:
                dst = stack[m->sp - 2].value.u32;
                src = stack[m->sp - 1].value.u32;
                n   = stack[m->sp    ].value.u32;
                ASSERT((uint64_t)dst + n <= m->memory.pages * VM_PAGE_SIZE, "Memory fill out of bounds");
                memset(m->memory.bytes + dst, src, n);
                m->pc += 1;
                m->sp -= 3;
                continue;

            default:
                ASSERT(0, "Unrecognized extended opcode %#x", opcode);
            }
            continue;

        // Memory load operators
        case I32_Load ... I64_Load32_u:
            read_leb(m); // skip flags
            offset = read_leb(m);
            addr = stack[m->sp--].value.u32;
            ASSERT((uint64_t)addr + (uint64_t)offset + mem_load_size[opcode - I32_Load]
                   <= m->memory.pages * VM_PAGE_SIZE, "Memory load out of bounds");
            maddr = m->memory.bytes + offset + addr;
            stack[++m->sp].value.u64 = 0; // initialize to 0

            switch (opcode) {
            case I32_Load:
                stack[m->sp].value.u32 = RN32(maddr);
                stack[m->sp].value_type = I32;
                break;
            case I64_Load:
                stack[m->sp].value.u64 = RN64(maddr);
                stack[m->sp].value_type = I64;
                break;
            case F32_Load:
                stack[m->sp].value.u32 = RN32(maddr);
                stack[m->sp].value_type = F32;
                break;
            case F64_Load:
                stack[m->sp].value.u64 = RN64(maddr);
                stack[m->sp].value_type = F64;
                break;
            case I32_Load8_s:
                stack[m->sp].value.i32 = (int8_t)*maddr;
                stack[m->sp].value_type = I32;
                break;
            case I32_Load8_u:
                stack[m->sp].value.u32 = *maddr;
                stack[m->sp].value_type = I32;
                break;
            case I32_Load16_s:
                stack[m->sp].value.i32 = (int16_t)RN16(maddr);
                stack[m->sp].value_type = I32;
                break;
            case I32_Load16_u:
                stack[m->sp].value.u32 = RN16(maddr);
                stack[m->sp].value_type = I32;
                break;
            case I64_Load8_s:
                stack[m->sp].value.i64 = (int8_t)*maddr;
                stack[m->sp].value_type = I64;
                break;
            case I64_Load8_u:
                stack[m->sp].value.u64 = *maddr;
                stack[m->sp].value_type = I64;
                break;
            case I64_Load16_s:
                stack[m->sp].value.i64 = (int16_t)RN16(maddr);
                stack[m->sp].value_type = I64;
                break;
            case I64_Load16_u:
                stack[m->sp].value.u64 = RN16(maddr);
                stack[m->sp].value_type = I64;
                break;
            case I64_Load32_s:
                stack[m->sp].value.i64 = (int32_t)RN32(maddr);
                stack[m->sp].value_type = I64;
                break;
            case I64_Load32_u:
                stack[m->sp].value.u64 = RN32(maddr);
                stack[m->sp].value_type = I64;
                break;
            }
            continue;

        // Memory store operators
        case I32_Store ... I64_Store32:
            read_leb(m); // skip flags
            offset = read_leb(m);
            vm_value_t *sval = &stack[m->sp--];
            addr = stack[m->sp--].value.u32;
            ASSERT((uint64_t)addr + (uint64_t)offset + mem_load_size[opcode - I32_Load]
                   <= m->memory.pages * VM_PAGE_SIZE, "Memory store out of bounds");
            maddr = m->memory.bytes + offset + addr;

            switch (opcode) {
            case I32_Store:
            case F32_Store:
                WN32(maddr, sval->value.u32);
                break;
            case I64_Store:
            case F64_Store:
                WN64(maddr, sval->value.u64);
                break;
            case I32_Store8:
                *maddr = sval->value.u32;
                break;
            case I32_Store16:
                WN16(maddr, sval->value.u32);
                break;
            case I64_Store8:
                *maddr = sval->value.u64;
                break;
            case I64_Store16:
                WN16(maddr, sval->value.u64);
                break;
            case I64_Store32:
                WN32(maddr, sval->value.u64);
                break;
            }
            continue;

        //
        // Constants
        //
        case I32_Const:
            stack[++m->sp].value_type = I32;
            stack[m->sp].value.u32 = read_leb_si(m);
            continue;
        case I64_Const:
            stack[++m->sp].value_type = I64;
            stack[m->sp].value.u64 = read_leb64_si(m);
            continue;
        case F32_Const:
            stack[++m->sp].value_type = F32;
            stack[m->sp].value.u32 = RL32(m->bytes + m->pc);
            m->pc += 4;
            continue;
        case F64_Const:
            stack[++m->sp].value_type = F64;
            stack[m->sp].value.u64 = RL64(m->bytes + m->pc);
            m->pc += 8;
            continue;

        //
        // Comparison operators
        //

        // unary
        case I32_Eqz:
            stack[m->sp].value.u32 = stack[m->sp].value.u32 == 0;
            continue;
        case I64_Eqz:
            stack[m->sp].value_type = I32;
            stack[m->sp].value.u32 = stack[m->sp].value.u64 == 0;
            continue;

        // binary i32
        case I32_Eq ... I32_Ge_u:
            a = stack[m->sp - 1].value.u32;
            b = stack[m->sp].value.u32;
            m->sp--;
            switch (opcode) {
            case I32_Eq:
                c = a == b;
                break;
            case I32_Ne:
                c = a != b;
                break;
            case I32_Lt_s:
                c = (int32_t)a < (int32_t)b;
                break;
            case I32_Lt_u:
                c = a < b;
                break;
            case I32_Gt_s:
                c = (int32_t)a > (int32_t)b;
                break;
            case I32_Gt_u:
                c = a > b;
                break;
            case I32_Le_s:
                c = (int32_t)a <= (int32_t)b;
                break;
            case I32_Le_u:
                c = a <= b;
                break;
            case I32_Ge_s:
                c = (int32_t)a >= (int32_t)b;
                break;
            case I32_Ge_u:
                c = a >= b;
                break;
            }
            stack[m->sp].value_type = I32;
            stack[m->sp].value.u32 = c;
            continue;

        // binary i64
        case I64_Eq ... I64_Ge_u:
            d = stack[m->sp - 1].value.u64;
            e = stack[m->sp].value.u64;
            m->sp--;
            switch (opcode) {
            case I64_Eq:
                c = d == e;
                break;
            case I64_Ne:
                c = d != e;
                break;
            case I64_Lt_s:
                c = (int64_t)d < (int64_t)e;
                break;
            case I64_Lt_u:
                c = d < e;
                break;
            case I64_Gt_s:
                c = (int64_t)d > (int64_t)e;
                break;
            case I64_Gt_u:
                c = d > e;
                break;
            case I64_Le_s:
                c = (int64_t)d <= (int64_t)e;
                break;
            case I64_Le_u:
                c = d <= e;
                break;
            case I64_Ge_s:
                c = (int64_t)d >= (int64_t)e;
                break;
            case I64_Ge_u:
                c = d >= e;
                break;
            }
            stack[m->sp].value_type = I32;
            stack[m->sp].value.u32 = c;
            continue;

        // binary f32
        case F32_Eq ... F32_Ge:
            g = stack[m->sp - 1].value.f32;
            h = stack[m->sp].value.f32;
            m->sp--;
            switch (opcode) {
            case F32_Eq:
                c = g == h;
                break;
            case F32_Ne:
                c = g != h;
                break;
            case F32_Lt:
                c = g < h;
                break;
            case F32_Gt:
                c = g > h;
                break;
            case F32_Le:
                c = g <= h;
                break;
            case F32_Ge:
                c = g >= h;
                break;
            }
            stack[m->sp].value_type = I32;
            stack[m->sp].value.u32 = c;
            continue;

        // binary f64
        case F64_Eq ... F64_Ge:
            j = stack[m->sp - 1].value.f64;
            k = stack[m->sp].value.f64;
            m->sp--;
            switch (opcode) {
            case F64_Eq:
                c = j == k;
                break;
            case F64_Ne:
                c = j != k;
                break;
            case F64_Lt:
                c = j < k;
                break;
            case F64_Gt:
                c = j > k;
                break;
            case F64_Le:
                c = j <= k;
                break;
            case F64_Ge:
                c = j >= k;
                break;
            }
            stack[m->sp].value_type = I32;
            stack[m->sp].value.u32 = c;
            continue;

        //
        // Numeric operators
        //

        // unary i32
        case I32_Clz ... I32_Popcnt:
            a = stack[m->sp].value.u32;
            switch (opcode) {
            case I32_Clz:
                c = a == 0 ? 32 : __builtin_clz(a);
                break;
            case I32_Ctz:
                c = a == 0 ? 32 : __builtin_ctz(a);
                break;
            case I32_Popcnt:
                c = __builtin_popcount(a);
                break;
            }
            stack[m->sp].value.u32 = c;
            continue;

        // unary i64
        case I64_Clz ... I64_Popcnt:
            d = stack[m->sp].value.u64;
            switch (opcode) {
            case I64_Clz:
                f = d == 0 ? 64 : __builtin_clzll(d);
                break;
            case I64_Ctz:
                f = d == 0 ? 64 : __builtin_ctzll(d);
                break;
            case I64_Popcnt:
                f = __builtin_popcountll(d);
                break;
            }
            stack[m->sp].value.u64 = f;
            continue;

        // unary f32
        case F32_Abs:
            stack[m->sp].value.f32 = fabsf(stack[m->sp].value.f32);
            break;
        case F32_Neg:
            stack[m->sp].value.f32 = -stack[m->sp].value.f32;
            break;
        case F32_Ceil:
            stack[m->sp].value.f32 = ceilf(stack[m->sp].value.f32);
            break;
        case F32_Floor:
            stack[m->sp].value.f32 = floorf(stack[m->sp].value.f32);
            break;
        case F32_Trunc:
            stack[m->sp].value.f32 = truncf(stack[m->sp].value.f32);
            break;
        case F32_Nearest:
            stack[m->sp].value.f32 = rintf(stack[m->sp].value.f32);
            break;
        case F32_Sqrt:
            stack[m->sp].value.f32 = sqrtf(stack[m->sp].value.f32);
            break;

        // unary f64
        case F64_Abs:
            stack[m->sp].value.f64 = fabs(stack[m->sp].value.f64);
            break;
        case F64_Neg:
            stack[m->sp].value.f64 = -stack[m->sp].value.f64;
            break;
        case F64_Ceil:
            stack[m->sp].value.f64 = ceil(stack[m->sp].value.f64);
            break;
        case F64_Floor:
            stack[m->sp].value.f64 = floor(stack[m->sp].value.f64);
            break;
        case F64_Trunc:
            stack[m->sp].value.f64 = trunc(stack[m->sp].value.f64);
            break;
        case F64_Nearest:
            stack[m->sp].value.f64 = rint(stack[m->sp].value.f64);
            break;
        case F64_Sqrt:
            stack[m->sp].value.f64 = sqrt(stack[m->sp].value.f64);
            break;

        // binary i32
        case I32_Add ... I32_Rotr:
            a = stack[m->sp - 1].value.u32;
            b = stack[m->sp].value.u32;
            m->sp--;
            if (opcode >= I32_Div_s && opcode <= I32_Rem_u)
                ASSERT(b, "Integer divide by zero");
            switch (opcode) {
            case I32_Add:
                c = a + b;
                break;
            case I32_Sub:
                c = a - b;
                break;
            case I32_Mul:
                c = a * b;
                break;
            case I32_Div_s:
                ASSERT(!(a == INT32_MIN && b == -1), "Integer overflow");
                c = (int32_t)a / (int32_t)b;
                break;
            case I32_Div_u:
                c = a / b;
                break;
            case I32_Rem_s:
                c = (a == INT32_MIN && b == -1) ? 0 : (int32_t)a % (int32_t)b;
                break;
            case I32_Rem_u:
                c = a % b;
                break;
            case I32_And:
                c = a & b;
                break;
            case I32_Or:
                c = a | b;
                break;
            case I32_Xor:
                c = a ^ b;
                break;
            case I32_Shl:
                c = a << b;
                break;
            case I32_Shr_s:
                c = (int32_t)a >> b;
                break;
            case I32_Shr_u:
                c = a >> b;
                break;
            case I32_Rotl:
                c = rotl32(a, b);
                break;
            case I32_Rotr:
                c = rotr32(a, b);
                break;
            }
            stack[m->sp].value.u32 = c;
            continue;

        // binary i64
        case I64_Add ... I64_Rotr:
            d = stack[m->sp - 1].value.u64;
            e = stack[m->sp].value.u64;
            m->sp--;
            if (opcode >= I64_Div_s && opcode <= I64_Rem_u)
                ASSERT(e, "Integer divide by zero");
            switch (opcode) {
            case I64_Add:
                f = d + e;
                break;
            case I64_Sub:
                f = d - e;
                break;
            case I64_Mul:
                f = d * e;
                break;
            case I64_Div_s:
                ASSERT(!(d == INT64_MIN && e == -1), "Integer overflow");
                f = (int64_t)d / (int64_t)e;
                break;
            case I64_Div_u:
                f = d / e;
                break;
            case I64_Rem_s:
                f = (d == INT64_MIN && e == -1) ? 0 : (int64_t)d % (int64_t)e;
                break;
            case I64_Rem_u:
                f = d % e;
                break;
            case I64_And:
                f = d & e;
                break;
            case I64_Or:
                f = d | e;
                break;
            case I64_Xor:
                f = d ^ e;
                break;
            case I64_Shl:
                f = d << e;
                break;
            case I64_Shr_s:
                f = (int64_t)d >> e;
                break;
            case I64_Shr_u:
                f = d >> e;
                break;
            case I64_Rotl:
                f = rotl64(d, e);
                break;
            case I64_Rotr:
                f = rotr64(d, e);
                break;
            }
            stack[m->sp].value.u64 = f;
            continue;

        // binary f32
        case F32_Add ... F32_Copysign:
            g = stack[m->sp - 1].value.f32;
            h = stack[m->sp].value.f32;
            m->sp--;
            switch (opcode) {
            case F32_Add:
                i = g + h;
                break;
            case F32_Sub:
                i = g - h;
                break;
            case F32_Mul:
                i = g * h;
                break;
            case F32_Div:
                i = g / h;
                break;
            case F32_Min:
                i = min(g, h);
                break;
            case F32_Max:
                i = max(g, h);
                break;
            case F32_Copysign:
                i = h < 0 ? -fabsf(g) : fabsf(g);
                break;
            }
            stack[m->sp].value.f32 = i;
            continue;

        // binary f64
        case F64_Add ... F64_Copysign:
            j = stack[m->sp - 1].value.f64;
            k = stack[m->sp].value.f64;
            m->sp--;
            switch (opcode) {
            case F64_Add:
                l = j + k;
                break;
            case F64_Sub:
                l = j - k;
                break;
            case F64_Mul:
                l = j * k;
                break;
            case F64_Div:
                l = j / k;
                break;
            case F64_Min:
                l = min(j, k);
                break;
            case F64_Max:
                l = max(j, k);
                break;
            case F64_Copysign:
                l = k < 0 ? -fabs(j) : fabs(j);
                break;
            }
            stack[m->sp].value.f64 = l;
            continue;

        // conversion operations
        case I32_Wrap_i64:
            stack[m->sp].value.u32 = stack[m->sp].value.u64;
            stack[m->sp].value_type = I32;
            break;
        case I32_Trunc_f32_s:
            stack[m->sp].value.i32 = stack[m->sp].value.f32;
            stack[m->sp].value_type = I32;
            break;
        case I32_Trunc_f32_u:
            stack[m->sp].value.u32 = stack[m->sp].value.f32;
            stack[m->sp].value_type = I32;
            break;
        case I32_Trunc_f64_s:
            stack[m->sp].value.i32 = stack[m->sp].value.f64;
            stack[m->sp].value_type = I32;
            break;
        case I32_Trunc_f64_u:
            stack[m->sp].value.u32 = stack[m->sp].value.f64;
            stack[m->sp].value_type = I32;
            break;
        case I64_Extend_i32_s:
            stack[m->sp].value.u64 = stack[m->sp].value.i32;
            stack[m->sp].value_type = I64;
            break;
        case I64_Extend_i32_u:
            stack[m->sp].value.u64 = stack[m->sp].value.u32;
            stack[m->sp].value_type = I64;
            break;
        case I64_Trunc_f32_s:
            stack[m->sp].value.i64 = stack[m->sp].value.f32;
            stack[m->sp].value_type = I64;
            break;
        case I64_Trunc_f32_u:
            stack[m->sp].value.u64 = stack[m->sp].value.f32;
            stack[m->sp].value_type = I64;
            break;
        case I64_Trunc_f64_s:
            stack[m->sp].value.i64 = stack[m->sp].value.f64;
            stack[m->sp].value_type = I64;
            break;
        case I64_Trunc_f64_u:
            stack[m->sp].value.u64 = stack[m->sp].value.f64;
            stack[m->sp].value_type = I64;
            break;
        case F32_Convert_i32_s:
            stack[m->sp].value.f32 = stack[m->sp].value.i32;
            stack[m->sp].value_type = F32;
            break;
        case F32_Convert_i32_u:
            stack[m->sp].value.f32 = stack[m->sp].value.u32;
            stack[m->sp].value_type = F32;
            break;
        case F32_Convert_i64_s:
            stack[m->sp].value.f32 = stack[m->sp].value.i64;
            stack[m->sp].value_type = F32;
            break;
        case F32_Convert_i64_u:
            stack[m->sp].value.f32 = stack[m->sp].value.u64;
            stack[m->sp].value_type = F32;
            break;
        case F32_Demote_f64:
            stack[m->sp].value.f32 = stack[m->sp].value.f64;
            stack[m->sp].value_type = F32;
            break;
        case F64_Convert_i32_s:
            stack[m->sp].value.f64 = stack[m->sp].value.i32;
            stack[m->sp].value_type = F64;
            break;
        case F64_Convert_i32_u:
            stack[m->sp].value.f64 = stack[m->sp].value.u32;
            stack[m->sp].value_type = F64;
            break;
        case F64_Convert_i64_s:
            stack[m->sp].value.f64 = stack[m->sp].value.i64;
            stack[m->sp].value_type = F64;
            break;
        case F64_Convert_i64_u:
            stack[m->sp].value.f64 = stack[m->sp].value.u64;
            stack[m->sp].value_type = F64;
            break;
        case F64_Promote_f32:
            stack[m->sp].value.f64 = stack[m->sp].value.f32;
            stack[m->sp].value_type = F64;
            break;

        // reinterpretations
        case I32_Reinterpret_f32:
            stack[m->sp].value_type = I32;
            break;
        case I64_Reinterpret_f64:
            stack[m->sp].value_type = I64;
            break;
        case F32_Reinterpret_i32:
            stack[m->sp].value_type = F32;
            break;
        case F64_Reinterpret_i64:
            stack[m->sp].value_type = F64;
            break;

        // sign extensions
        case I32_Extend8_s:
            stack[m->sp].value.i32 = (int8_t)stack[m->sp].value.i32;
            break;
        case I32_Extend16_s:
            stack[m->sp].value.i32 = (int16_t)stack[m->sp].value.i32;
            break;
        case I64_Extend8_s:
            stack[m->sp].value.i64 = (int8_t)stack[m->sp].value.i64;
            break;
        case I64_Extend16_s:
            stack[m->sp].value.i64 = (int16_t)stack[m->sp].value.i64;
            break;
        case I64_Extend32_s:
            stack[m->sp].value.i64 = (int32_t)stack[m->sp].value.i64;
            break;

        default:
            ASSERT(0, "Unrecognized opcode %#x", opcode);
        }
    }
}
