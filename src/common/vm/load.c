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
#include "common/sizebuf.h"
#include "common/files.h"

int VM_GetExport(vm_t *m, const char *name, uint32_t kind)
{
    for (uint32_t e = 0; e < m->export_count; e++) {
        const vm_export_t *export = &m->exports[e];
        if (export->kind != kind)
            continue;
        if (!strcmp(name, export->name))
            return e;
    }
    return -1;
}

// Static definition of block_types
static const vm_type_t block_types[5] = {
    { .form = BLOCK, .result_count = 0, },
    { .form = BLOCK, .result_count = 1, .results = { I32 } },
    { .form = BLOCK, .result_count = 1, .results = { I64 } },
    { .form = BLOCK, .result_count = 1, .results = { F32 } },
    { .form = BLOCK, .result_count = 1, .results = { F64 } }
};

static const vm_type_t *get_block_type(uint8_t value_type)
{
    switch (value_type) {
    case 0x40:
        return &block_types[0];
    case I32:
        return &block_types[1];
    case I64:
        return &block_types[2];
    case F32:
        return &block_types[3];
    case F64:
        return &block_types[4];
    default:
        ASSERT(0, "invalid block_type value_type: %d", value_type);
        return NULL;
    }
}

// TODO: calculate this while parsing types
static uint64_t get_type_mask(vm_type_t *type)
{
    uint64_t mask = 0x80;

    if (type->result_count == 1)
        mask |= 0x80 - type->results[0];

    mask = mask << 4;
    for (uint32_t p = 0; p < type->param_count; p++) {
        mask <<= 4;
        mask |= 0x80 - type->params[p];
    }
    return mask;
}

static uint32_t sz_read_leb(sizebuf_t *sz)
{
    uint32_t v = 0;
    int c, bits = 0;

    do {
        ASSERT(bits < 32, "LEB encoding too long");
        c = SZ_ReadByte(sz);
        v |= (c & UINT32_C(0x7f)) << bits;
        bits += 7;
    } while (c & 0x80);

    return v;
}

static uint64_t sz_read_leb64(sizebuf_t *sz)
{
    uint64_t v = 0;
    int c, bits = 0;

    do {
        ASSERT(bits < 64, "LEB encoding too long");
        c = SZ_ReadByte(sz);
        v |= (c & UINT64_C(0x7f)) << bits;
        bits += 7;
    } while (c & 0x80);

    return v;
}

static char *sz_read_string(sizebuf_t *sz)
{
    uint32_t len = sz_read_leb(sz);
    char *src = SZ_ReadData(sz, len);
    char *dst = VM_Malloc(len + 1);
    memcpy(dst, src, len);
    return dst;
}

static void run_init_expr(vm_t *m, uint8_t type, sizebuf_t *sz)
{
    // Run the init_expr
    vm_block_t block = {
        .block_type = 0x01,
        .type       = get_block_type(type),
        .start_addr = sz->readcount
    };

    // WARNING: running code here to get initial value!
    m->pc = sz->readcount;
    VM_PushBlock(m, &block, m->sp);
    VM_Interpret(m);
    sz->readcount = m->pc;

    ASSERT(m->stack[m->sp].value_type == type,
           "init_expr type mismatch 0x%x != 0x%x",
           m->stack[m->sp].value_type, type);
}

static void parse_custom(vm_t *m, sizebuf_t *sb)
{
}

static void parse_types(vm_t *m, sizebuf_t *sz)
{
    m->type_count = sz_read_leb(sz);
    ASSERT(m->type_count <= SZ_Remaining(sz) / 3, "Too many types");
    m->types = VM_Malloc(m->type_count * sizeof(m->types[0]));

    for (uint32_t c = 0; c < m->type_count; c++) {
        vm_type_t *type = &m->types[c];
        type->form = SZ_ReadByte(sz);
        type->param_count = sz_read_leb(sz);
        ASSERT(type->param_count <= SZ_Remaining(sz) / 3, "Too many parameters");
        type->params = VM_Malloc(type->param_count * sizeof(type->params[0]));
        for (uint32_t p = 0; p < type->param_count; p++)
            type->params[p] = sz_read_leb(sz);
        type->result_count = sz_read_leb(sz);
        ASSERT(type->result_count <= MAX_RESULTS, "Too many results");
        for (uint32_t r = 0; r < type->result_count; r++)
            type->results[r] = sz_read_leb(sz);
        type->mask = get_type_mask(type);
    }
}

static void parse_imports(vm_t *m, sizebuf_t *sz)
{
    uint32_t import_count = sz_read_leb(sz);
    for (uint32_t gidx = 0; gidx < import_count; gidx++) {
        char *import_module = sz_read_string(sz);
        char *import_field = sz_read_string(sz);
        uint32_t kind = SZ_ReadByte(sz);
        uint32_t type_index = 0, fidx;
        uint8_t content_type = 0, mutability;

        switch (kind) {
        case KIND_FUNCTION:
            type_index = sz_read_leb(sz);
            break;
        case KIND_GLOBAL:
            content_type = sz_read_leb(sz);
            // TODO: use mutability
            mutability = SZ_ReadByte(sz);
            (void)mutability;
            break;
        default:
            ASSERT(0, "Import of kind %d not supported", kind);
        }

        void *val = NULL;   // TODO

        // Store in the right place
        switch (kind) {
        case KIND_FUNCTION:
            fidx = m->function_count;
            m->import_count++;
            m->function_count++;
            m->functions = VM_Realloc(m->functions, m->import_count * sizeof(m->functions[0]));

            vm_block_t *func = &m->functions[fidx];
            func->import_module = import_module;
            func->import_field = import_field;
            func->type = &m->types[type_index];
            func->func_ptr = val;
            break;

        case KIND_GLOBAL:
            m->global_count++;
            m->globals = VM_Realloc(m->globals, m->global_count * sizeof(m->globals[0]));
            vm_value_t *glob = &m->globals[m->global_count - 1];
            glob->value_type = content_type;

            switch (content_type) {
            case I32:
                memcpy(&glob->value.u32, val, 4);
                break;
            case I64:
                memcpy(&glob->value.u64, val, 8);
                break;
            case F32:
                memcpy(&glob->value.f32, val, 4);
                break;
            case F64:
                memcpy(&glob->value.f64, val, 8);
                break;
            }
            break;
        }
    }
}

static void parse_functions(vm_t *m, sizebuf_t *sz)
{
    uint32_t count = sz_read_leb(sz);
    ASSERT(count <= SZ_Remaining(sz), "too many functions");
    m->function_count += count;
    m->functions = VM_Realloc(m->functions, m->function_count * sizeof(m->functions[0]));

    for (uint32_t f = m->import_count; f < m->function_count; f++) {
        uint32_t tidx = sz_read_leb(sz);
        ASSERT(tidx <= m->type_count, "Bad type index");
        m->functions[f].fidx = f;
        m->functions[f].type = &m->types[tidx];
    }
}

static void parse_tables(vm_t *m, sizebuf_t *sz)
{
    uint32_t table_count = sz_read_leb(sz);
    ASSERT(table_count == 1, "More than 1 table not supported %d",table_count);

    uint32_t flags = SZ_ReadByte(sz);
    uint32_t tsize = sz_read_leb(sz); // Initial size
    m->table.initial = tsize;
    m->table.size = tsize;
    // Limit maximum to 64K
    if (flags & 0x1) {
        tsize = sz_read_leb(sz); // Max size
        m->table.maximum = min(0x10000, tsize);
    } else {
        m->table.maximum = 0x10000;
    }

    // Allocate the table
    m->table.entries = VM_Malloc(m->table.size * sizeof(m->table.entries[0]));
}

static void parse_memory(vm_t *m, sizebuf_t *sz)
{
    uint32_t memory_count = sz_read_leb(sz);
    ASSERT(memory_count == 1, "More than 1 memory not supported\n");

    uint32_t flags = SZ_ReadByte(sz);
    uint32_t pages = sz_read_leb(sz); // Initial size
    m->memory.initial = pages;
    m->memory.pages = pages;
    // Limit the maximum to 2GB
    if (flags & 0x1) {
        pages = sz_read_leb(sz); // Max size
        m->memory.maximum = min(0x8000, pages);
    } else {
        m->memory.maximum = 0x8000;
    }
    if (flags & 0x8) {
        sz_read_leb(sz); // Page size
    }

    // Allocate memory
    m->memory.bytes = VM_Malloc(m->memory.pages * PAGE_SIZE);
}

static void parse_globals(vm_t *m, sizebuf_t *sz)
{
    uint32_t global_count = sz_read_leb(sz);
    ASSERT(global_count <= SZ_Remaining(sz) / 2, "Too many globals");
    m->globals = VM_Malloc(global_count * sizeof(m->globals[0]));
    m->global_count = global_count;

    for (uint32_t g = 0; g < global_count; g++) {
        // Same allocation Import of global above
        uint32_t type = sz_read_leb(sz);
        // TODO: use mutability
        uint8_t mutability = SZ_ReadByte(sz);
        (void)mutability;
        m->globals[g].value_type = type;

        // Run the init_expr to get global value
        run_init_expr(m, type, sz);

        m->globals[g] = m->stack[m->sp--];
    }
}

static void parse_exports(vm_t *m, sizebuf_t *sz)
{
    uint32_t export_count = sz_read_leb(sz);
    ASSERT(export_count <= SZ_Remaining(sz) / 3, "Too many exports");
    m->exports = VM_Malloc(export_count * sizeof(m->exports[0]));
    m->export_count = export_count;

    for (uint32_t e = 0; e < export_count; e++) {
        vm_export_t *export = &m->exports[e];
        char *name = sz_read_string(sz);
        uint32_t kind = SZ_ReadByte(sz);
        uint32_t index = sz_read_leb(sz);
        export->kind = kind;
        export->name = name;

        switch (kind) {
        case KIND_FUNCTION:
            ASSERT(index < m->function_count, "Bad function index");
            export->value = &m->functions[index];
            break;
        case KIND_TABLE:
            ASSERT(index == 0, "Only 1 table in MVP");
            export->value = &m->table;
            break;
        case KIND_MEMORY:
            ASSERT(index == 0, "Only 1 memory in MVP");
            export->value = &m->memory;
            break;
        case KIND_GLOBAL:
            ASSERT(index < m->global_count, "Bad global index");
            export->value = &m->globals[index];
            break;
        }
    }
}

static void parse_start(vm_t *m, sizebuf_t *sz)
{
    m->start_function = sz_read_leb(sz);
    ASSERT(m->start_function < m->function_count, "Bad function index");
}

static void parse_elements(vm_t *m, sizebuf_t *sz)
{
    uint32_t element_count = sz_read_leb(sz);
    for (uint32_t c = 0; c < element_count; c++) {
        uint32_t index = sz_read_leb(sz);
        ASSERT(index == 0, "Only 1 default table in MVP");

        // Run the init_expr to get offset
        run_init_expr(m, I32, sz);

        uint32_t offset = m->stack[m->sp--].value.u32;
        uint32_t num_elem = sz_read_leb(sz);
        ASSERT((uint64_t)offset + num_elem <= m->table.size, "table overflow");
        for (uint32_t n = 0; n < num_elem; n++)
            m->table.entries[offset + n] = sz_read_leb(sz);
    }
}

static void parse_data(vm_t *m, sizebuf_t *sz)
{
    uint32_t seg_count = sz_read_leb(sz);
    for (uint32_t s = 0; s < seg_count; s++) {
        uint32_t index = sz_read_leb(sz);
        ASSERT(index == 0, "Only 1 default memory in MVP %d",index);

        // Run the init_expr to get the offset
        run_init_expr(m, I32, sz);

        // Copy the data to the memory offset
        uint32_t offset = m->stack[m->sp--].value.u32;
        uint32_t size = sz_read_leb(sz);
        ASSERT((uint64_t)offset + size <= m->memory.pages * PAGE_SIZE, "memory overflow");
        memcpy(m->memory.bytes + offset, SZ_ReadData(sz, size), size);
    }
}

static void parse_code(vm_t *m, sizebuf_t *sz)
{
    uint32_t body_count = sz_read_leb(sz);
    ASSERT(body_count <= m->function_count - m->import_count, "Too many functions");

    for (uint32_t b = 0; b < body_count; b++) {
        vm_block_t *function = &m->functions[m->import_count + b];
        uint32_t body_size = sz_read_leb(sz);
        ASSERT(body_size <= SZ_Remaining(sz), "function out of bounds");
        uint32_t payload_start = sz->readcount;
        uint32_t local_count = sz_read_leb(sz);
        uint32_t save_pos, tidx, lidx, lecount;

        // Get number of locals for alloc
        save_pos = sz->readcount;
        function->local_count = 0;
        for (uint32_t l = 0; l < local_count; l++) {
            lecount = sz_read_leb(sz);
            ASSERT(lecount <= MAX_LOCALS - function->local_count, "Too many locals");
            function->local_count += lecount;
            tidx = sz_read_leb(sz);
            (void)tidx;
        }
        function->locals = VM_Malloc(function->local_count * sizeof(function->locals[0]));

        // Restore position and read the locals
        sz->readcount = save_pos;
        lidx = 0;
        for (uint32_t l = 0; l < local_count; l++) {
            lecount = sz_read_leb(sz);
            tidx = sz_read_leb(sz);
            for (uint32_t l = 0; l < lecount; l++)
                function->locals[lidx++] = tidx;
        }

        function->start_addr = sz->readcount;
        function->end_addr = payload_start + body_size - 1;
        function->br_addr = function->end_addr;
        ASSERT(sz->data[function->end_addr] == End, "Function block doesn't end with End opcode");
        sz->readcount = function->end_addr + 1;
    }
}

static void skip_immediates(sizebuf_t *sz, int opcode)
{
    uint32_t count;

    switch (opcode) {
    case MemorySize:
    case MemoryGrow:
        sz->readcount++;
        break;
    case Br:
    case BrIf:
    case Call:
    case LocalGet ... GlobalSet:
    case I32_Const:
        sz_read_leb(sz);
        break;
    case CallIndirect:
        sz_read_leb(sz);
        sz->readcount++;
        break;
    case I64_Const:
        sz_read_leb64(sz);
        break;
    case F32_Const:
        sz->readcount += 4;
        break;
    case F64_Const:
        sz->readcount += 8;
        break;
    case Block ... If:
        sz_read_leb(sz);
        break;
    case I32_Load ... I64_Store32:
        sz_read_leb(sz);
        sz_read_leb(sz);
        break;
    case BrTable:
        count = sz_read_leb(sz); // target count
        for (uint32_t i = 0; i < count; i++)
            sz_read_leb(sz);
        sz_read_leb(sz); // default target
        break;
    default:    // no immediates
        break;
    }
}

static void find_blocks(vm_t *m, vm_block_t *function, sizebuf_t *sz)
{
    vm_block_t  *block;
    vm_block_t  *blockstack[BLOCKSTACK_SIZE];
    int          top = -1;
    int          opcode = Unreachable;

    sz->readcount = function->start_addr;
    while (sz->readcount <= function->end_addr) {
        uint32_t pos = sz->readcount;
        opcode = SZ_ReadByte(sz);
        switch (opcode) {
        case Block:
        case Loop:
        case If:
            ASSERT(top < BLOCKSTACK_SIZE - 1, "blockstack overflow");
            block = VM_Malloc(sizeof(*block));
            block->block_type = opcode;
            block->type = get_block_type(sz_read_leb(sz));
            block->start_addr = pos;
            blockstack[++top] = block;
            m->block_lookup[pos] = block;
            break;

        case Else:
            ASSERT(blockstack[top]->block_type == If, "else not matched with if");
            blockstack[top]->else_addr = pos + 1;
            break;

        case End:
            if (pos == function->end_addr)
                break;
            ASSERT(top >= 0, "blockstack underflow");
            block = blockstack[top--];
            block->end_addr = pos;
            if (block->block_type == Loop) {
                // loop: label after start
                block->br_addr = block->start_addr + 2;
            } else {
                // block, if: label at end
                block->br_addr = pos;
            }
            break;
        default:
            skip_immediates(sz, opcode);
            break;
        }
    }

    ASSERT(top == -1, "Function ended in middle of block");
    ASSERT(opcode == End, "Function block doesn't end with End opcode");
}

#define NUM_SECTIONS    12

typedef struct {
    uint32_t pos, len;
} vm_section_t;

typedef void (*parsefunc_t)(vm_t *m, sizebuf_t *sz);

static const parsefunc_t parsefuncs[NUM_SECTIONS] = {
    parse_custom,
    parse_types,
    parse_imports,
    parse_functions,
    parse_tables,
    parse_memory,
    parse_globals,
    parse_exports,
    parse_start,
    parse_elements,
    parse_code,
    parse_data,
};

vm_t *VM_Load(const char *name)
{
    uint32_t    word;
    vm_t        *m;
    sizebuf_t   sz;
    byte        *data;
    int         len;

    len = FS_LoadFile(name, (void **)&data);
    if (!data)
        return NULL;

    SZ_InitRead(&sz, data, len);
    sz.allowunderflow = false;

    // Check the module
    word = SZ_ReadLong(&sz);
    if (word != WA_MAGIC)
        return NULL;

    word = SZ_ReadLong(&sz);
    if (word != WA_VERSION)
        return NULL;

    // Allocate the module
    m = VM_Malloc(sizeof(*m));

    // Empty stacks
    m->sp  = -1;
    m->fp  = -1;
    m->csp = -1;

    m->bytes = data;
    m->byte_count = len;
    m->block_lookup = VM_Malloc(m->byte_count * sizeof(m->block_lookup[0]));
    m->start_function = -1;

    // Read the sections
    vm_section_t sections[NUM_SECTIONS] = { 0 };
    while (sz.readcount < sz.cursize) {
        uint32_t id = SZ_ReadByte(&sz);
        uint32_t len = sz_read_leb(&sz);
        ASSERT(id < NUM_SECTIONS, "unknown section");
        ASSERT(len <= SZ_Remaining(&sz), "section out of bounds");
        sections[id].pos = sz.readcount;
        sections[id].len = len;
        sz.readcount += len;
    }

    for (uint32_t id = 0; id < NUM_SECTIONS; id++) {
        if (!sections[id].len)
            continue;
        sz.readcount = sections[id].pos;
        parsefuncs[id](m, &sz);
    }

    for (uint32_t f = m->import_count; f < m->function_count; f++)
        find_blocks(m, &m->functions[f], &sz);

    if (m->start_function != -1) {
        uint32_t fidx = m->start_function;
        if (fidx < m->import_count) {
            VM_ThunkOut(m, fidx);     // import/thunk call
        } else {
            VM_SetupCall(m, fidx);   // regular function call
            VM_Interpret(m);
        }
    }

    return m;
}

void VM_Free(vm_t *m)
{
    uint32_t i;

    if (!m)
        return;

    for (i = 0; i < m->type_count; i++)
        Z_Free(m->types[i].params);

    for (i = 0; i < m->function_count; i++)
        Z_Free(m->functions[i].locals);

    for (i = 0; i < m->export_count; i++)
        Z_Free(m->exports[i].name);

    for (i = 0; i < m->byte_count; i++)
        Z_Free(m->block_lookup[i]);

    Z_Free(m->types);
    Z_Free(m->functions);
    Z_Free(m->globals);
    Z_Free(m->exports);
    Z_Free(m->table.entries);
    Z_Free(m->memory.bytes);
    Z_Free(m->block_lookup);
    Z_Free(m->bytes);
    Z_Free(m);
}

uint64_t VM_Call(vm_t *m, uint32_t e, ...)
{
    ASSERT(!(e < 0 || e >= m->export_count), "bad export");

    const vm_export_t *export = &m->exports[e];
    ASSERT(export->kind == KIND_FUNCTION, "not a function");

    const vm_block_t *func = export->value;
    const vm_type_t *type = func->type;
    va_list ap;

    va_start(ap, e);
    for (int i = 0; i < type->param_count; i++) {
        vm_value_t *value = &m->stack[++m->sp];
        value->value_type = type->params[i];
        switch (type->params[i]) {
        case I32:
            value->value.u32 = va_arg(ap, uint32_t);
            break;
        case I64:
            value->value.u64 = va_arg(ap, uint64_t);
            break;
        case F32:
            value->value.f32 = va_arg(ap, double);
            break;
        case F64:
            value->value.f64 = va_arg(ap, double);
            break;
        default:
            ASSERT(0, "bad param");
        }
    }
    va_end(ap);

    VM_SetupCall(m, func->fidx);
    VM_Interpret(m);
    return m->stack[m->sp--].value.u64;
}
