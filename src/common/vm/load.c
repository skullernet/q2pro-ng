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
    for (uint32_t e = 0; e < m->num_exports; e++) {
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
    { .form = BLOCK, .num_results = 0, },
    { .form = BLOCK, .num_results = 1, .results = { I32 } },
    { .form = BLOCK, .num_results = 1, .results = { I64 } },
    { .form = BLOCK, .num_results = 1, .results = { F32 } },
    { .form = BLOCK, .num_results = 1, .results = { F64 } }
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

    if (type->num_results == 1)
        mask |= 0x80 - type->results[0];

    mask = mask << 4;
    for (uint32_t p = 0; p < type->num_params; p++) {
        mask <<= 4;
        mask |= 0x80 - type->params[p];
    }
    return mask;
}

static char *sz_read_string(sizebuf_t *sz)
{
    uint32_t len = SZ_ReadLeb(sz);
    char *src = SZ_ReadData(sz, len);
    char *dst = VM_Malloc(len + 1);
    memcpy(dst, src, len);
    return dst;
}

static vm_value_t run_init_expr(vm_t *m, uint8_t type, sizebuf_t *sz)
{
    int opcode = SZ_ReadByte(sz);
    vm_value_t ret;
    uint32_t arg;

    switch (opcode) {
    case GlobalGet:
        arg = SZ_ReadLeb(sz);
        ASSERT(arg < m->num_globals, "bad global");
        ret = m->globals[arg];
        break;
    case I32_Const:
        ret.value_type = I32;
        ret.value.u32  = SZ_ReadSignedLeb(sz);
        break;
    case I64_Const:
        ret.value_type = I64;
        ret.value.u64  = SZ_ReadSignedLeb(sz);
        break;
    case F32_Const:
        ret.value_type = F32;
        ret.value.u32  = SZ_ReadLong(sz);
        break;
    case F64_Const:
        ret.value_type = F64;
        ret.value.u64  = SZ_ReadLong64(sz);
        break;
    default:
        ASSERT(0, "init_expr not constant");
    }

    ASSERT(ret.value_type == type, "init_expr type mismatch");
    return ret;
}

static void parse_custom(vm_t *m, sizebuf_t *sb)
{
}

static void parse_types(vm_t *m, sizebuf_t *sz)
{
    m->num_types = SZ_ReadLeb(sz);
    ASSERT(m->num_types <= SZ_Remaining(sz) / 3, "Too many types");
    m->types = VM_Malloc(m->num_types * sizeof(m->types[0]));

    for (uint32_t c = 0; c < m->num_types; c++) {
        vm_type_t *type = &m->types[c];
        type->form = SZ_ReadByte(sz);
        type->num_params = SZ_ReadLeb(sz);
        ASSERT(type->num_params <= SZ_Remaining(sz) / 3, "Too many parameters");
        type->params = VM_Malloc(type->num_params * sizeof(type->params[0]));
        for (uint32_t p = 0; p < type->num_params; p++)
            type->params[p] = SZ_ReadLeb(sz);
        type->num_results = SZ_ReadLeb(sz);
        ASSERT(type->num_results <= MAX_RESULTS, "Too many results");
        for (uint32_t r = 0; r < type->num_results; r++)
            type->results[r] = SZ_ReadLeb(sz);
        type->mask = get_type_mask(type);
    }
}

static void parse_imports(vm_t *m, sizebuf_t *sz)
{
    uint32_t num_imports = SZ_ReadLeb(sz);
    for (uint32_t gidx = 0; gidx < num_imports; gidx++) {
        char *import_module = sz_read_string(sz);
        char *import_field = sz_read_string(sz);
        uint32_t kind = SZ_ReadByte(sz);
        uint32_t type_index = 0, fidx;
        uint8_t content_type = 0, mutability;

        switch (kind) {
        case KIND_FUNCTION:
            type_index = SZ_ReadLeb(sz);
            break;
        case KIND_GLOBAL:
            content_type = SZ_ReadLeb(sz);
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
            fidx = m->num_funcs;
            m->num_imports++;
            m->num_funcs++;
            m->funcs = VM_Realloc(m->funcs, m->num_imports * sizeof(m->funcs[0]));

            vm_block_t *func = &m->funcs[fidx];
            func->import_module = import_module;
            func->import_field = import_field;
            func->type = &m->types[type_index];
            func->func_ptr = val;
            break;

        case KIND_GLOBAL:
            m->num_globals++;
            m->globals = VM_Realloc(m->globals, m->num_globals * sizeof(m->globals[0]));
            vm_value_t *glob = &m->globals[m->num_globals - 1];
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
    uint32_t count = SZ_ReadLeb(sz);
    ASSERT(count <= SZ_Remaining(sz), "too many funcs");
    m->num_funcs += count;
    m->funcs = VM_Realloc(m->funcs, m->num_funcs * sizeof(m->funcs[0]));

    for (uint32_t f = m->num_imports; f < m->num_funcs; f++) {
        uint32_t tidx = SZ_ReadLeb(sz);
        ASSERT(tidx <= m->num_types, "Bad type index");
        m->funcs[f].fidx = f;
        m->funcs[f].type = &m->types[tidx];
    }
}

static void parse_tables(vm_t *m, sizebuf_t *sz)
{
    uint32_t table_count = SZ_ReadLeb(sz);
    ASSERT(table_count == 1, "More than 1 table not supported %d",table_count);

    uint32_t flags = SZ_ReadByte(sz);
    uint32_t tsize = SZ_ReadLeb(sz); // Initial size
    m->table.initial = tsize;
    m->table.size = tsize;
    // Limit maximum to 64K
    if (flags & 0x1) {
        tsize = SZ_ReadLeb(sz); // Max size
        m->table.maximum = min(0x10000, tsize);
    } else {
        m->table.maximum = 0x10000;
    }

    // Allocate the table
    m->table.entries = VM_Malloc(m->table.size * sizeof(m->table.entries[0]));
}

static void parse_memory(vm_t *m, sizebuf_t *sz)
{
    uint32_t memory_count = SZ_ReadLeb(sz);
    ASSERT(memory_count == 1, "More than 1 memory not supported\n");

    uint32_t flags = SZ_ReadByte(sz);
    uint32_t pages = SZ_ReadLeb(sz); // Initial size
    m->memory.initial = pages;
    m->memory.pages = pages;
    // Limit the maximum to 2GB
    if (flags & 0x1) {
        pages = SZ_ReadLeb(sz); // Max size
        m->memory.maximum = min(0x8000, pages);
    } else {
        m->memory.maximum = 0x8000;
    }
    if (flags & 0x8) {
        SZ_ReadLeb(sz); // Page size
    }

    // Allocate memory
    m->memory.bytes = VM_Malloc(m->memory.pages * PAGE_SIZE);
}

static void parse_globals(vm_t *m, sizebuf_t *sz)
{
    uint32_t num_globals = SZ_ReadLeb(sz);
    ASSERT(num_globals <= SZ_Remaining(sz) / 2, "Too many globals");
    m->globals = VM_Malloc(num_globals * sizeof(m->globals[0]));
    m->num_globals = num_globals;

    for (uint32_t g = 0; g < num_globals; g++) {
        // Same allocation Import of global above
        uint32_t type = SZ_ReadLeb(sz);
        // TODO: use mutability
        uint8_t mutability = SZ_ReadByte(sz);
        (void)mutability;

        // Run the init_expr to get global value
        m->globals[g] = run_init_expr(m, type, sz);
    }
}

static void parse_exports(vm_t *m, sizebuf_t *sz)
{
    uint32_t num_exports = SZ_ReadLeb(sz);
    ASSERT(num_exports <= SZ_Remaining(sz) / 3, "Too many exports");
    m->exports = VM_Malloc(num_exports * sizeof(m->exports[0]));
    m->num_exports = num_exports;

    for (uint32_t e = 0; e < num_exports; e++) {
        vm_export_t *export = &m->exports[e];
        char *name = sz_read_string(sz);
        uint32_t kind = SZ_ReadByte(sz);
        uint32_t index = SZ_ReadLeb(sz);
        export->kind = kind;
        export->name = name;

        switch (kind) {
        case KIND_FUNCTION:
            ASSERT(index < m->num_funcs, "Bad function index");
            export->value = &m->funcs[index];
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
            ASSERT(index < m->num_globals, "Bad global index");
            export->value = &m->globals[index];
            break;
        }
    }
}

static void parse_start(vm_t *m, sizebuf_t *sz)
{
    m->start_func = SZ_ReadLeb(sz);
    ASSERT(m->start_func < m->num_funcs, "Bad function index");
}

static void parse_elements(vm_t *m, sizebuf_t *sz)
{
    uint32_t element_count = SZ_ReadLeb(sz);
    for (uint32_t c = 0; c < element_count; c++) {
        uint32_t index = SZ_ReadLeb(sz);
        ASSERT(index == 0, "Only 1 default table in MVP");

        // Run the init_expr to get offset
        vm_value_t init = run_init_expr(m, I32, sz);

        uint32_t offset = init.value.u32;
        uint32_t num_elem = SZ_ReadLeb(sz);
        ASSERT((uint64_t)offset + num_elem <= m->table.size, "table overflow");
        for (uint32_t n = 0; n < num_elem; n++)
            m->table.entries[offset + n] = SZ_ReadLeb(sz);
    }
}

static void parse_data(vm_t *m, sizebuf_t *sz)
{
    uint32_t seg_count = SZ_ReadLeb(sz);
    for (uint32_t s = 0; s < seg_count; s++) {
        uint32_t index = SZ_ReadLeb(sz);
        ASSERT(index == 0, "Only 1 default memory in MVP %d",index);

        // Run the init_expr to get the offset
        vm_value_t init = run_init_expr(m, I32, sz);

        // Copy the data to the memory offset
        uint32_t offset = init.value.u32;
        uint32_t size = SZ_ReadLeb(sz);
        ASSERT((uint64_t)offset + size <= m->memory.pages * PAGE_SIZE, "memory overflow");
        memcpy(m->memory.bytes + offset, SZ_ReadData(sz, size), size);
    }
}

static void parse_code(vm_t *m, sizebuf_t *sz)
{
    uint32_t body_count = SZ_ReadLeb(sz);
    ASSERT(body_count <= m->num_funcs - m->num_imports, "Too many functions");

    for (uint32_t b = 0; b < body_count; b++) {
        vm_block_t *func = &m->funcs[m->num_imports + b];
        uint32_t body_size = SZ_ReadLeb(sz);
        ASSERT(body_size <= SZ_Remaining(sz), "Function out of bounds");
        uint32_t payload_start = sz->readcount;
        uint32_t num_locals = SZ_ReadLeb(sz);
        uint32_t save_pos, tidx, lidx, lecount;

        // Get number of locals for alloc
        save_pos = sz->readcount;
        func->num_locals = 0;
        for (uint32_t l = 0; l < num_locals; l++) {
            lecount = SZ_ReadLeb(sz);
            ASSERT(lecount <= MAX_LOCALS - func->num_locals, "Too many locals");
            func->num_locals += lecount;
            tidx = SZ_ReadLeb(sz);
            (void)tidx;
        }
        func->locals = VM_Malloc(func->num_locals * sizeof(func->locals[0]));

        // Restore position and read the locals
        sz->readcount = save_pos;
        lidx = 0;
        for (uint32_t l = 0; l < num_locals; l++) {
            lecount = SZ_ReadLeb(sz);
            tidx = SZ_ReadLeb(sz);
            for (uint32_t l = 0; l < lecount; l++)
                func->locals[lidx++] = tidx;
        }

        func->start_addr = sz->readcount;
        func->end_addr = payload_start + body_size - 1;
        func->br_addr = func->end_addr;
        ASSERT(sz->data[func->end_addr] == End, "Function block doesn't end with End opcode");
        sz->readcount = func->end_addr + 1;
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
    case Block ... If:
    case Br:
    case BrIf:
    case Call:
    case LocalGet ... GlobalSet:
    case I32_Const:
    case I64_Const:
        SZ_ReadLeb(sz);
        break;
    case CallIndirect:
        SZ_ReadLeb(sz);
        sz->readcount++;
        break;
    case F32_Const:
        sz->readcount += 4;
        break;
    case F64_Const:
        sz->readcount += 8;
        break;
    case I32_Load ... I64_Store32:
        SZ_ReadLeb(sz);
        SZ_ReadLeb(sz);
        break;
    case BrTable:
        count = SZ_ReadLeb(sz); // target count
        for (uint32_t i = 0; i < count; i++)
            SZ_ReadLeb(sz);
        SZ_ReadLeb(sz); // default target
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
            block->type = get_block_type(SZ_ReadLeb(sz));
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
    m->num_bytes = len;
    m->block_lookup = VM_Malloc(m->num_bytes * sizeof(m->block_lookup[0]));
    m->start_func = -1;

    // Read the sections
    vm_section_t sections[NUM_SECTIONS] = { 0 };
    while (sz.readcount < sz.cursize) {
        uint32_t id = SZ_ReadByte(&sz);
        uint32_t len = SZ_ReadLeb(&sz);
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

    for (uint32_t f = m->num_imports; f < m->num_funcs; f++)
        find_blocks(m, &m->funcs[f], &sz);

    if (m->start_func != -1) {
        uint32_t fidx = m->start_func;
        if (fidx < m->num_imports) {
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

    for (i = 0; i < m->num_types; i++)
        Z_Free(m->types[i].params);

    for (i = 0; i < m->num_funcs; i++)
        Z_Free(m->funcs[i].locals);

    for (i = 0; i < m->num_exports; i++)
        Z_Free(m->exports[i].name);

    for (i = 0; i < m->num_bytes; i++)
        Z_Free(m->block_lookup[i]);

    Z_Free(m->types);
    Z_Free(m->funcs);
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
    ASSERT(!(e < 0 || e >= m->num_exports), "bad export");

    const vm_export_t *export = &m->exports[e];
    ASSERT(export->kind == KIND_FUNCTION, "not a function");

    const vm_block_t *func = export->value;
    const vm_type_t *type = func->type;
    va_list ap;

    va_start(ap, e);
    for (int i = 0; i < type->num_params; i++) {
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
