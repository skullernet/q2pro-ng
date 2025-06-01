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
#include "common/common.h"

#define ASSERT(cond, ...) \
    do { if (!(cond)) { Com_SetLastError(va(__VA_ARGS__)); return false; } } while (0)

static bool vm_string_eq(const vm_string_t *s, const char *str)
{
    return s->len == strlen(str) && !memcmp(s->data, str, s->len);
}

// Static definition of block_types
static const vm_type_t block_types[5] = {
    { .form = BLOCK, .num_results = 0, },
    { .form = BLOCK, .num_results = 1, .results = { I32 } },
    { .form = BLOCK, .num_results = 1, .results = { I64 } },
    { .form = BLOCK, .num_results = 1, .results = { F32 } },
    { .form = BLOCK, .num_results = 1, .results = { F64 } }
};

static const vm_type_t *get_block_type(uint32_t value_type)
{
    switch (value_type) {
    case BLOCK:
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
        ASSERT(0, "Invalid block value_type: %#x", value_type);
        return NULL;
    }
}

static uint64_t get_type_mask(const vm_type_t *type)
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

static int get_value_type(int c)
{
    switch (c) {
    case 'i':
        return I32;
    case 'I':
        return I64;
    case 'f':
        return F32;
    case 'F':
        return F64;
    }
    return 0;
}

static uint64_t calc_type_mask(const char *s)
{
    uint64_t mask = 0x80;

    if (*s && s[1] == ' ') {
        mask |= 0x80 - get_value_type(*s);
        s += 2;
    }
    mask <<= 4;

    while (*s) {
        mask <<= 4;
        mask |= 0x80 - get_value_type(*s);
        s++;
    }

    return mask;
}

static bool import_function(vm_t *m, const vm_string_t *module, const vm_string_t *name, const vm_type_t *type)
{
    const vm_import_t *import;

    for (import = m->imports; import->name; import++)
        if (vm_string_eq(name, import->name))
            break;

    ASSERT(import->name, "Import %.*s not found", name->len, name->data);

    ASSERT(calc_type_mask(import->mask) == type->mask, "Import %.*s type mismatch", name->len, name->data);

    m->num_imports++;
    m->num_funcs++;
    m->funcs = VM_Realloc(m->funcs, m->num_imports * sizeof(m->funcs[0]));

    vm_block_t *func = &m->funcs[m->num_imports - 1];
    func->type = type;
    func->thunk = import->thunk;
    return true;
}

#if 0
static bool import_global(vm_t *m, const char *module, const char *name, uint32_t type)
{
    m->num_globals++;
    m->globals = VM_Realloc(m->globals, m->num_globals * sizeof(m->globals[0]));
    vm_value_t *glob = &m->globals[m->num_globals - 1];

    glob->value_type = type;
    switch (type) {
    case I32:
    case F32:
        glob->value.u32 = RN32(val);
        break;
    case I64:
    case F64:
        glob->value.u64 = RN64(val);
        break;
    default:
        ASSERT(0, "Import of type %d not supported", type);
    }

    return true;
}
#endif

static void vm_read_string(sizebuf_t *sz, vm_string_t *s)
{
    s->len = SZ_ReadLeb(sz);
    s->data = SZ_ReadData(sz, s->len);
}

static bool run_init_expr(vm_t *m, vm_value_t *val, uint32_t type, sizebuf_t *sz)
{
    int opcode = SZ_ReadByte(sz);
    uint32_t arg;

    switch (opcode) {
    case GlobalGet:
        arg = SZ_ReadLeb(sz);
        ASSERT(arg < m->num_globals, "Bad global index");
        *val = m->globals[arg];
        break;
    case I32_Const:
        val->value_type = I32;
        val->value.u32  = SZ_ReadSignedLeb(sz, 32);
        break;
    case I64_Const:
        val->value_type = I64;
        val->value.u64  = SZ_ReadSignedLeb(sz, 64);
        break;
    case F32_Const:
        val->value_type = F32;
        val->value.u32  = SZ_ReadLong(sz);
        break;
    case F64_Const:
        val->value_type = F64;
        val->value.u64  = SZ_ReadLong64(sz);
        break;
    default:
        ASSERT(0, "Init expression not constant (opcode = %#x)", opcode);
    }

    opcode = SZ_ReadByte(sz);
    ASSERT(opcode == End, "End opcode expected after init expression");

    ASSERT(val->value_type == type, "Init expression type mismatch");
    return true;
}

static bool parse_types(vm_t *m, sizebuf_t *sz)
{
    m->num_types = SZ_ReadLeb(sz);
    ASSERT(m->num_types <= SZ_Remaining(sz) / 3, "Too many types");
    m->types = VM_Malloc(m->num_types * sizeof(m->types[0]));

    for (uint32_t c = 0; c < m->num_types; c++) {
        vm_type_t *type = &m->types[c];
        type->form = SZ_ReadLeb(sz);
        ASSERT(type->form == FUNC, "Must be function type");
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

    return true;
}

static bool parse_imports(vm_t *m, sizebuf_t *sz)
{
    uint32_t num_imports = SZ_ReadLeb(sz);
    for (uint32_t gidx = 0; gidx < num_imports; gidx++) {
        vm_string_t module, name;
        vm_read_string(sz, &module);
        vm_read_string(sz, &name);
        uint32_t kind = SZ_ReadByte(sz);
        uint32_t tidx;

        switch (kind) {
        case KIND_FUNCTION:
            tidx = SZ_ReadLeb(sz);
            ASSERT(tidx < m->num_types, "Bad type index");
            if (!import_function(m, &module, &name, &m->types[tidx]))
                return false;
            break;
#if 0
        case KIND_GLOBAL:
            tidx = SZ_ReadLeb(sz);
            SZ_ReadByte(sz); // mutability
            if (!import_global(m, &modue, &name, tidx))
                return false;
            break;
#endif
        default:
            ASSERT(0, "Import of kind %d not supported", kind);
        }
    }

    return true;
}

static bool parse_functions(vm_t *m, sizebuf_t *sz)
{
    uint32_t count = SZ_ReadLeb(sz);
    ASSERT(count <= SZ_Remaining(sz), "Too many functions");
    m->num_funcs += count;
    m->funcs = VM_Realloc(m->funcs, m->num_funcs * sizeof(m->funcs[0]));

    for (uint32_t f = m->num_imports; f < m->num_funcs; f++) {
        uint32_t tidx = SZ_ReadLeb(sz);
        ASSERT(tidx < m->num_types, "Bad type index");
        m->funcs[f].type = &m->types[tidx];
    }

    return true;
}

static bool parse_tables(vm_t *m, sizebuf_t *sz)
{
    uint32_t table_count = SZ_ReadLeb(sz);
    ASSERT(table_count == 1, "Only 1 default table supported");

    uint32_t type = SZ_ReadLeb(sz);
    ASSERT(type == FUNCREF, "Must be funcref");

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
    ASSERT(m->table.size <= m->table.maximum, "Bad table size");

    // Allocate the table
    m->table.entries = VM_Malloc(m->table.size * sizeof(m->table.entries[0]));
    return true;
}

static bool parse_memory(vm_t *m, sizebuf_t *sz)
{
    uint32_t memory_count = SZ_ReadLeb(sz);
    ASSERT(memory_count == 1, "Only 1 default memory supported");

    uint32_t flags = SZ_ReadByte(sz);
    uint32_t pages = SZ_ReadLeb(sz); // Initial size
    m->memory.initial = pages;
    m->memory.pages = pages;
    // Limit the maximum to 100MB
    if (flags & 0x1) {
        pages = SZ_ReadLeb(sz); // Max size
        m->memory.maximum = min(0x600, pages);
    } else {
        m->memory.maximum = 0x600;
    }
    if (flags & 0x8) {
        SZ_ReadLeb(sz); // Page size
    }
    ASSERT(m->memory.pages <= m->memory.maximum, "Bad memory size");

    // Allocate memory
    m->memory.bytes = VM_Malloc(m->memory.pages * VM_PAGE_SIZE + 1024);
    return true;
}

static bool parse_globals(vm_t *m, sizebuf_t *sz)
{
    uint32_t num_globals = SZ_ReadLeb(sz);
    ASSERT(num_globals <= SZ_Remaining(sz) / 2, "Too many globals");
    m->globals = VM_Malloc(num_globals * sizeof(m->globals[0]));
    m->num_globals = num_globals;

    for (uint32_t g = 0; g < num_globals; g++) {
        uint32_t type = SZ_ReadLeb(sz);
        SZ_ReadByte(sz); // mutability

        // Run the init_expr to get global value
        if (!run_init_expr(m, &m->globals[g], type, sz))
            return false;
    }
    return true;
}

static bool parse_exports(vm_t *m, sizebuf_t *sz)
{
    uint32_t num_exports = SZ_ReadLeb(sz);
    ASSERT(num_exports <= SZ_Remaining(sz) / 3, "Too many exports");
    m->exports = VM_Malloc(num_exports * sizeof(m->exports[0]));
    m->num_exports = num_exports;

    for (uint32_t e = 0; e < num_exports; e++) {
        wa_export_t *export = &m->exports[e];
        vm_read_string(sz, &export->name);
        uint32_t kind = SZ_ReadByte(sz);
        uint32_t index = SZ_ReadLeb(sz);
        export->kind = kind;

        switch (kind) {
        case KIND_FUNCTION:
            ASSERT(index < m->num_funcs, "Bad function index");
            export->value = &m->funcs[index];
            break;
        case KIND_TABLE:
            ASSERT(index == 0, "Only 1 default table supported");
            export->value = &m->table;
            break;
        case KIND_MEMORY:
            ASSERT(index == 0, "Only 1 default memory supported");
            export->value = &m->memory;
            break;
        case KIND_GLOBAL:
            ASSERT(index < m->num_globals, "Bad global index");
            export->value = &m->globals[index];
            break;
        default:
            ASSERT(0, "Export of kind %d not supported", kind);
        }
    }

    return true;
}

static bool parse_start(vm_t *m, sizebuf_t *sz)
{
    m->start_func = SZ_ReadLeb(sz);
    ASSERT(m->start_func < m->num_funcs - m->num_imports, "Bad start function index");
    m->start_func += m->num_imports;
    const vm_type_t *type = m->funcs[m->start_func].type;
    ASSERT(type->num_params == 0 && type->num_results == 0, "Bad start function type");
    return true;
}

static bool parse_elements(vm_t *m, sizebuf_t *sz)
{
    uint32_t element_count = SZ_ReadLeb(sz);
    for (uint32_t c = 0; c < element_count; c++) {
        uint32_t flags = SZ_ReadLeb(sz);
        ASSERT(flags == 0, "Flags must be 0");

        // Run the init_expr to get offset
        vm_value_t init;
        if (!run_init_expr(m, &init, I32, sz))
            return false;

        uint32_t offset = init.value.u32;
        uint32_t num_elem = SZ_ReadLeb(sz);
        ASSERT((uint64_t)offset + num_elem <= m->table.size, "Table init out of bounds");
        for (uint32_t n = 0; n < num_elem; n++)
            m->table.entries[offset + n] = SZ_ReadLeb(sz);
    }

    return true;
}

static bool parse_data(vm_t *m, sizebuf_t *sz)
{
    uint32_t seg_count = SZ_ReadLeb(sz);
    for (uint32_t s = 0; s < seg_count; s++) {
        uint32_t flags = SZ_ReadLeb(sz);
        ASSERT(flags == 0, "Flags must be 0");

        // Run the init_expr to get the offset
        vm_value_t init;
        if (!run_init_expr(m, &init, I32, sz))
            return false;

        // Copy the data to the memory offset
        uint32_t offset = init.value.u32;
        uint32_t size = SZ_ReadLeb(sz);
        ASSERT((uint64_t)offset + size <= m->memory.pages * VM_PAGE_SIZE, "Memory init out of bounds");
        memcpy(m->memory.bytes + offset, SZ_ReadData(sz, size), size);
    }

    return true;
}

static bool parse_code(vm_t *m, sizebuf_t *sz)
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
        ASSERT(sz->data[func->end_addr] == End, "Function block doesn't end with End opcode");
        sz->readcount = func->end_addr + 1;
    }

    return true;
}

static bool find_blocks(vm_t *m, const vm_block_t *func, sizebuf_t *sz)
{
    vm_block_t  *block;
    uint16_t     blockstack[BLOCKSTACK_SIZE];
    int          top = -1;
    uint32_t     opcode = Unreachable;
    uint32_t     count, index;

    sz->readcount = func->start_addr;
    while (sz->readcount <= func->end_addr) {
        uint32_t pos = sz->readcount;
        opcode = SZ_ReadByte(sz);
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
            block->block_type = opcode;
            block->type = get_block_type(SZ_ReadLeb(sz));
            if (!block->type)
                return false;
            if (opcode == Loop)
                block->end_addr = sz->readcount;    // loop: label after start
            blockstack[++top] = index;
            m->block_lookup[pos] = index;
            break;

        case Else:
            ASSERT(top >= 0, "Blockstack underflow");
            block = &m->blocks[blockstack[top]];
            ASSERT(block->block_type == If, "Else not matched with if");
            block->start_addr = pos + 1;
            break;

        case End:
            if (pos == func->end_addr)
                break;
            ASSERT(top >= 0, "Blockstack underflow");
            block = &m->blocks[blockstack[top--]];
            if (block->block_type != Loop)
                block->end_addr = pos; // block, if: label at end
            break;

        case Br:
        case BrIf:
            index = SZ_ReadLeb(sz);
            ASSERT(index < BLOCKSTACK_SIZE, "Bad label");
            break;

        case BrTable:
            count = SZ_ReadLeb(sz); // target count
            ASSERT(count <= BR_TABLE_SIZE, "BrTable size too big");
            for (uint32_t i = 0; i < count; i++) {
                index = SZ_ReadLeb(sz);
                ASSERT(index < BLOCKSTACK_SIZE, "Bad label");
            }
            index = SZ_ReadLeb(sz); // default target
            ASSERT(index < BLOCKSTACK_SIZE, "Bad label");
            break;

        case LocalGet:
        case LocalSet:
        case LocalTee:
            index = SZ_ReadLeb(sz);
            ASSERT(index < func->type->num_params + func->num_locals, "Bad local index");
            break;

        case GlobalGet:
        case GlobalSet:
            index = SZ_ReadLeb(sz);
            ASSERT(index < m->num_globals, "Bad global index");
            break;

        case MemorySize:
        case MemoryGrow:
            sz->readcount++;
            break;

        case I32_Load ... I64_Store32:
            SZ_ReadLeb(sz);
            SZ_ReadLeb(sz);
            break;

        case I32_Const:
            SZ_ReadSignedLeb(sz, 32);
            break;

        case I64_Const:
            SZ_ReadSignedLeb(sz, 64);
            break;

        case Call:
            index = SZ_ReadLeb(sz);
            ASSERT(index < m->num_funcs, "Bad function index");
            break;

        case CallIndirect:
            index = SZ_ReadLeb(sz);
            ASSERT(index < m->num_types, "Bad type index");
            index = SZ_ReadLeb(sz);
            ASSERT(index == 0, "Only 1 default table supported");
            break;

        case F32_Const:
            sz->readcount += 4;
            break;

        case F64_Const:
            sz->readcount += 8;
            break;

        case Unreachable:
        case Nop:
        case Return:
        case Drop:
        case Select:
        case I32_Eqz ... I64_Extend32_s:
            break;

        case Extended:
            opcode = SZ_ReadLeb(sz);
            switch (opcode) {
            case I32_Trunc_sat_f32_s ... I64_Trunc_sat_f64_u:
                break;
            case MemoryCopy:
                sz->readcount += 2;
                break;
            case MemoryFill:
                sz->readcount += 1;
                break;
            default:
               ASSERT(0, "Unrecognized extended opcode %#x", opcode);
            }
            break;

        default:
            ASSERT(0, "Unrecognized opcode %#x", opcode);
        }
    }

    ASSERT(top == -1, "Function ended in middle of block");
    ASSERT(opcode == End, "Function block doesn't end with End opcode");

    return true;
}

#define NUM_SECTIONS    13

typedef struct {
    uint32_t pos, len;
} vm_section_t;

typedef bool (*parsefunc_t)(vm_t *m, sizebuf_t *sz);

static const parsefunc_t parsefuncs[NUM_SECTIONS] = {
    NULL,
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
    NULL
};

static int vm_load_file(const char *name, byte **data)
{
    qhandle_t f;
    int64_t len;
    int ret;

    *data = NULL;

    len = FS_OpenFile(name, &f, FS_MODE_READ | FS_FLAG_LOADFILE);
    if (len < 0)
        return len;

    if (len < 8) {
        FS_CloseFile(f);
        return Q_ERR_FILE_TOO_SMALL;
    }

    if (len > MAX_LOADFILE) {
        FS_CloseFile(f);
        return Q_ERR(EFBIG);
    }

    *data = VM_Malloc(Q_ALIGN(len, 1024) + 1024);
    ret = FS_Read(*data, len, f);
    FS_CloseFile(f);

    if (ret < 0)
        return ret;
    if (ret != len)
        return Q_ERR_UNEXPECTED_EOF;

    return ret;
}

static bool parse_sections(vm_t *m, sizebuf_t *sz)
{
    // Read the sections
    vm_section_t sections[NUM_SECTIONS] = { 0 };
    while (sz->readcount < sz->cursize) {
        uint32_t id = SZ_ReadByte(sz);
        uint32_t len = SZ_ReadLeb(sz);
        ASSERT(id < NUM_SECTIONS, "Unknown section %u", id);
        ASSERT(len <= SZ_Remaining(sz), "Section %u out of bounds", id);
        sections[id].pos = sz->readcount;
        sections[id].len = len;
        sz->readcount += len;
    }

    for (uint32_t id = 0; id < NUM_SECTIONS; id++) {
        if (!sections[id].len)
            continue;
        if (!parsefuncs[id])
            continue;
        sz->readcount = sections[id].pos;
        if (!parsefuncs[id](m, sz))
            return false;
    }

    return true;
}

static bool fill_exports(vm_t *m, const vm_export_t *exports)
{
    const vm_export_t *exp;
    int i;

    for (i = 0, exp = exports; exp->name; i++, exp++)
        ;
    m->num_func_exports = i;
    m->func_exports = VM_Malloc(m->num_func_exports * sizeof(m->func_exports[0]));

    for (i = 0, exp = exports; i < m->num_func_exports; i++, exp++) {
        uint32_t e;

        for (e = 0; e < m->num_exports; e++) {
            const wa_export_t *export = &m->exports[e];
            if (export->kind != KIND_FUNCTION)
                continue;
            if (!vm_string_eq(&export->name, exp->name))
                continue;

            const vm_block_t *func = export->value;
            ASSERT(func->type->mask == calc_type_mask(exp->mask), "Export %s type mismatch", exp->name);
            m->func_exports[i] = func - m->funcs;
            break;
        }

        ASSERT(e < m->num_exports, "Export %s not found", exp->name);
    }

    return true;
}

vm_t *VM_Load(const char *name, const vm_import_t *imports, const vm_export_t *exports)
{
    vm_t        *m;
    sizebuf_t   sz;
    byte        *data;
    int         len;

    len = vm_load_file(name, &data);
    if (len < 0) {
        Com_SetLastError(Q_ErrorString(len));
        goto fail1;
    }

    SZ_InitRead(&sz, data, len);

    if (SZ_ReadLong(&sz) != VM_MAGIC) {
        Com_SetLastError("Bad magic");
        goto fail1;
    }

    if (SZ_ReadLong(&sz) != VM_VERSION) {
        Com_SetLastError("Bad version");
        goto fail1;
    }

    // Allocate the module
    m = VM_Malloc(sizeof(*m));

    // Empty stacks
    m->sp  = -1;
    m->fp  = -1;
    m->csp = -1;

    m->bytes = data;
    m->num_bytes = len;
    m->start_func = -1;
    m->imports = imports;

    if (!parse_sections(m, &sz))
        goto fail2;

    if (!fill_exports(m, exports))
        goto fail2;

    m->block_lookup = VM_Malloc(m->num_bytes * sizeof(m->block_lookup[0]));

    for (uint32_t f = m->num_imports; f < m->num_funcs; f++)
        if (!find_blocks(m, &m->funcs[f], &sz))
            goto fail2;

    // assume first global is LLVM stack pointer
    if (m->num_globals)
        m->llvm_stack_start = m->globals[0];

    Com_Printf("Loaded %s: %d bytes of code, %d MB of memory\n", name,
               m->num_bytes, m->memory.pages * VM_PAGE_SIZE / 1000000);

    return m;

fail1:
    Z_Free(data);
    return NULL;

fail2:
    VM_Free(m);
    return NULL;
}

void VM_Free(vm_t *m)
{
    uint32_t i;

    if (!m)
        return;

    for (i = 0; i < m->num_types; i++)
        Z_Free(m->types[i].params);

    for (i = m->num_imports; i < m->num_funcs; i++)
        Z_Free(m->funcs[i].locals);

    Z_Free(m->types);
    Z_Free(m->funcs);
    Z_Free(m->globals);
    Z_Free(m->exports);
    Z_Free(m->func_exports);
    Z_Free(m->table.entries);
    Z_Free(m->memory.bytes);
    Z_Free(m->block_lookup);
    Z_Free(m->blocks);
    Z_Free(m->bytes);
    Z_Free(m);
}

void VM_Call(vm_t *m, uint32_t e)
{
    VM_ASSERT(e < m->num_func_exports, "Bad function index");
    uint32_t fidx = m->func_exports[e];

    const vm_block_t *func = &m->funcs[fidx];
    const vm_type_t *type = func->type;

    int fp = m->sp - type->num_params + 1;
    VM_ASSERT(fp >= 0, "Stack underflow");

    // Set pushed params type
    for (uint32_t f = 0; f < type->num_params; f++)
        m->stack[fp + f].value_type = type->params[f];

    VM_SetupCall(m, fidx);
    VM_Interpret(m);

    // Validate the return value
    if (type->num_results == 1) {
        VM_ASSERT(m->sp >= 0, "Stack underflow");
        VM_ASSERT(m->stack[m->sp].value_type == type->results[0], "Call type mismatch");
    }
}

vm_value_t *VM_Push(vm_t *m, int n)
{
    VM_ASSERT(m->sp < STACK_SIZE - n, "Stack overflow");
    m->sp += n;
    return &m->stack[m->sp - n + 1];
}

vm_value_t *VM_Pop(vm_t *m)
{
    VM_ASSERT(m->sp >= 0, "Stack underflow");
    return &m->stack[m->sp--];
}

const vm_memory_t *VM_Memory(const vm_t *m)
{
    return &m->memory;
}

void VM_Reset(vm_t *m)
{
    if (!m)
        return;

    // empty stacks
    m->sp  = -1;
    m->fp  = -1;
    m->csp = -1;

    // reset LLVM stack pointer
    if (m->num_globals)
        m->globals[0] = m->llvm_stack_start;
}
