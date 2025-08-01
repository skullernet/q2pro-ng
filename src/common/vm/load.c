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
#include "common/files.h"

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

const vm_type_t *VM_GetBlockType(uint32_t value_type)
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

static bool vm_type_eq(const vm_type_t *type, const char *s)
{
    int t;

    if (type->num_results == 1) {
        if (!(t = get_value_type(*s)))
            return false;
        if (s[1] != ' ' || type->results[0] != t)
            return false;
        s += 2;
    }

    for (uint32_t p = 0; p < type->num_params; p++, s++) {
        if (!(t = get_value_type(*s)))
            return false;
        if (type->params[p] != t)
            return false;
    }

    return *s == 0;
}

static bool import_function(vm_t *m, const vm_string_t *module, const vm_string_t *name, const vm_type_t *type)
{
    const vm_import_t *import;

    ASSERT(vm_string_eq(module, "env"), "Unknown import module %.*s", module->len, module->data);

    for (import = m->imports; import->name; import++)
        if (vm_string_eq(name, import->name))
            break;

    ASSERT(import->name, "Import %.*s not found", name->len, name->data);

    ASSERT(vm_type_eq(type, import->mask), "Import %.*s type mismatch", name->len, name->data);

    ASSERT(m->num_funcs < MAX_FUNCS, "Too many functions");
    m->num_imports++;
    m->num_funcs++;
    m->funcs = VM_Realloc(m->funcs, m->num_imports * sizeof(m->funcs[0]));

    vm_block_t *func = &m->funcs[m->num_imports - 1];
    func->type = type;
    func->thunk = import->thunk;
    return true;
}

static bool vm_read_string(sizebuf_t *sz, vm_string_t *s)
{
    s->len = SZ_ReadLeb(sz);
    s->data = SZ_ReadData(sz, s->len);
    ASSERT(s->data, "Read past end of section");
    return true;
}

static bool run_init_expr(vm_t *m, vm_value_t *val, sizebuf_t *sz)
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
        val->u32 = SZ_ReadSignedLeb(sz, 32);
        break;
    case I64_Const:
        val->u64 = SZ_ReadSignedLeb(sz, 64);
        break;
    case F32_Const:
        val->u32 = SZ_ReadLong(sz);
        break;
    case F64_Const:
        val->u64 = SZ_ReadLong64(sz);
        break;
    default:
        ASSERT(0, "Init expression not constant (opcode = %#x)", opcode);
    }

    opcode = SZ_ReadByte(sz);
    ASSERT(opcode == End, "End opcode expected after init expression");
    return true;
}

static bool parse_types(vm_t *m, sizebuf_t *sz)
{
    m->num_types = SZ_ReadLeb(sz);
    ASSERT(m->num_types <= MAX_TYPES, "Too many types");
    m->types = VM_Malloc(m->num_types * sizeof(m->types[0]));

    for (uint32_t c = 0; c < m->num_types; c++) {
        vm_type_t *type = &m->types[c];
        type->form = SZ_ReadLeb(sz);
        ASSERT(type->form == FUNC, "Must be function type");

        type->num_params = SZ_ReadLeb(sz);
        ASSERT(type->num_params <= MAX_LOCALS, "Too many parameters");
        type->params = VM_Malloc(type->num_params * sizeof(type->params[0]));
        for (uint32_t p = 0; p < type->num_params; p++)
            type->params[p] = SZ_ReadLeb(sz);

        type->num_results = SZ_ReadLeb(sz);
        ASSERT(type->num_results <= MAX_RESULTS, "Too many results");
        for (uint32_t r = 0; r < type->num_results; r++)
            type->results[r] = SZ_ReadLeb(sz);
    }

    return true;
}

static bool parse_imports(vm_t *m, sizebuf_t *sz)
{
    uint32_t num_imports = SZ_ReadLeb(sz);
    for (uint32_t gidx = 0; gidx < num_imports; gidx++) {
        vm_string_t module, name;
        if (!vm_read_string(sz, &module))
            return false;
        if (!vm_read_string(sz, &name))
            return false;

        uint32_t kind = SZ_ReadByte(sz);
        ASSERT(kind == KIND_FUNCTION, "Import of kind %d not supported", kind);

        uint32_t tidx = SZ_ReadLeb(sz);
        ASSERT(tidx < m->num_types, "Bad type index");
        if (!import_function(m, &module, &name, &m->types[tidx]))
            return false;
    }

    return true;
}

static bool parse_functions(vm_t *m, sizebuf_t *sz)
{
    uint32_t count = SZ_ReadLeb(sz);
    ASSERT(count <= MAX_FUNCS - m->num_funcs, "Too many functions");
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
    // Limit the maximum to 64K elements
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
    // Limit the maximum to 1.5K pages (100MB)
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
    ASSERT(num_globals <= MAX_GLOBALS, "Too many globals");
    m->globals = VM_Malloc(num_globals * sizeof(m->globals[0]));
    m->num_globals = num_globals;

    for (uint32_t g = 0; g < num_globals; g++) {
        uint32_t type = SZ_ReadLeb(sz);
        SZ_ReadByte(sz); // mutability
        (void)type;

        // Run the init_expr to get global value
        if (!run_init_expr(m, &m->globals[g], sz))
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
        if (!vm_read_string(sz, &export->name))
            return false;
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

static bool parse_elements(vm_t *m, sizebuf_t *sz)
{
    uint32_t element_count = SZ_ReadLeb(sz);
    for (uint32_t c = 0; c < element_count; c++) {
        uint32_t flags = SZ_ReadLeb(sz);
        ASSERT(flags == 0, "Element flags %#x not supported", flags);

        // Run the init_expr to get offset
        vm_value_t init = { 0 };
        if (!run_init_expr(m, &init, sz))
            return false;

        uint32_t offset = init.u32;
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
        ASSERT(flags == 0, "Segment flags %#x not supported", flags);

        // Run the init_expr to get the offset
        vm_value_t init = { 0 };
        if (!run_init_expr(m, &init, sz))
            return false;

        // Copy the data to the memory offset
        uint32_t offset = init.u32;
        uint32_t size = SZ_ReadLeb(sz);
        ASSERT((uint64_t)offset + size <= m->memory.pages * VM_PAGE_SIZE, "Memory init out of bounds");
        void *data = SZ_ReadData(sz, size);
        ASSERT(data, "Read past end of section");
        memcpy(m->memory.bytes + offset, data, size);
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
        ASSERT(body_size > 0, "Empty function");
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

#define NUM_SECTIONS    13

typedef struct {
    uint32_t pos, len;
} vm_section_t;

typedef bool (*parsefunc_t)(vm_t *m, sizebuf_t *sz);

static const parsefunc_t parsefuncs[NUM_SECTIONS] = {
    NULL,   // custom
    parse_types,
    parse_imports,
    parse_functions,
    parse_tables,
    parse_memory,
    parse_globals,
    parse_exports,
    NULL,   // start
    parse_elements,
    parse_code,
    parse_data,
    NULL,   // data count
};

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

    uint32_t cursize = sz->cursize;
    for (uint32_t id = 0; id < NUM_SECTIONS; id++) {
        if (!sections[id].len)
            continue;
        if (!parsefuncs[id])
            continue;
        sz->readcount = sections[id].pos;
        sz->cursize = sections[id].pos + sections[id].len;
        if (!parsefuncs[id](m, sz))
            return false;
        ASSERT(sz->readcount <= sz->cursize, "Read past end of section");
    }

    sz->readcount = 0;
    sz->cursize = cursize;
    return true;
}

static const wa_export_t *find_export(vm_t *m, uint32_t kind, const char *name)
{
    for (uint32_t e = 0; e < m->num_exports; e++) {
        const wa_export_t *export = &m->exports[e];
        if (export->kind == kind && vm_string_eq(&export->name, name))
            return export;
    }
    return NULL;
}

static bool fill_exports(vm_t *m, const vm_export_t *exports)
{
    const vm_export_t *exp;
    const wa_export_t *export;
    uint32_t e;

    for (e = 0, exp = exports; exp->name; e++, exp++)
        ;
    m->num_func_exports = e;
    m->func_exports = VM_Malloc(m->num_func_exports * sizeof(m->func_exports[0]));

    // Find function exports
    for (e = 0, exp = exports; e < m->num_func_exports; e++, exp++) {
        export = find_export(m, KIND_FUNCTION, exp->name);
        ASSERT(export, "Export %s not found", exp->name);
        const vm_block_t *func = export->value;
        ASSERT(vm_type_eq(func->type, exp->mask), "Export %s type mismatch", exp->name);
        m->func_exports[e] = func - m->funcs;
    }

    // Find LLVM stack pointer
    export = find_export(m, KIND_GLOBAL, "__stack_pointer");
    if (export)
        m->llvm_stack_pointer = export->value;
    else
        Com_WPrintf("Export __stack_pointer not found\n");

    // Prevent dangling pointers after file is freed
    for (e = 0; e < m->num_exports; e++)
        m->exports[e].name = (vm_string_t){ 0 };

    return true;
}

vm_t *VM_Load(const char *name, const vm_import_t *imports, const vm_export_t *exports)
{
    vm_t        *m;
    sizebuf_t   sz;
    byte        *data;
    int         len;

    len = FS_LoadFile(name, (void **)&data);
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

    m->imports = imports;

    if (!parse_sections(m, &sz))
        goto fail2;

    if (!fill_exports(m, exports))
        goto fail2;

    if (!VM_PrepareInterpreter(m, &sz))
        goto fail2;

    FS_FreeFile(data);

    // Save LLVM stack start
    if (m->llvm_stack_pointer)
        m->llvm_stack_start = *m->llvm_stack_pointer;

    Com_DPrintf("Loaded %s: %d KB of code, %d MB of memory\n", name,
                m->num_bytes / 1000, m->memory.pages * VM_PAGE_SIZE / 1000000);

    return m;

fail2:
    VM_Free(m);
fail1:
    FS_FreeFile(data);
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
    Z_Free(m->code);
    Z_Free(m);
}

// Call exported function by vm_export_t index.
// Caller pushes params and pops return value.
void VM_Call(vm_t *m, uint32_t e)
{
    VM_ASSERT(e < m->num_func_exports, "Bad function index");
    VM_SetupCall(m, m->func_exports[e]);
    VM_Interpret(m);
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

    // Empty stacks
    m->sp  = -1;
    m->fp  = -1;
    m->csp = -1;

    // Reset LLVM stack pointer
    if (m->llvm_stack_pointer)
        *m->llvm_stack_pointer = m->llvm_stack_start;
}
