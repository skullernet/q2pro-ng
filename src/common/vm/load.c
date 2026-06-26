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
        VM_ENSURE(0, "Invalid block value_type: %#x", value_type);
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

static bool import_function(vm_t *m, bstr_t module, bstr_t name, const vm_type_t *type)
{
    const vm_import_t *import;

    VM_ENSURE(Bstr_IsEqualStr(module, "env"), "Unknown import module %.*s", (int)module.len, module.str);

    for (import = m->imports; import->name; import++)
        if (Bstr_IsEqualStr(name, import->name))
            break;

    if (!import->name)
        for (import = vm_stdlib; import->name; import++)
            if (Bstr_IsEqualStr(name, import->name))
                break;

    VM_ENSURE(import->name, "Import %.*s not found", (int)name.len, name.str);

    VM_ENSURE(vm_type_eq(type, import->mask), "Import %.*s type mismatch", (int)name.len, name.str);

    vm_block_t *func = &m->funcs[m->num_imports++];
    func->type = type;
    func->thunk = import->thunk;
    return true;
}

static bstr_t vm_read_string(sizebuf_t *sz)
{
    bstr_t s;
    s.len = SZ_ReadLeb(sz);
    s.str = SZ_ReadData(sz, s.len);
    return s;
}

static bool run_init_expr(vm_t *m, vm_value_t *val, sizebuf_t *sz)
{
    int opcode = SZ_ReadByte(sz);
    uint32_t arg;

    switch (opcode) {
    case GlobalGet:
        arg = SZ_ReadLeb(sz);
        VM_ENSURE(arg < m->num_globals, "Bad global index");
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
        VM_ENSURE(0, "Init expression not constant (opcode = %#x)", opcode);
    }

    opcode = SZ_ReadByte(sz);
    VM_ENSURE(opcode == End, "End opcode expected after init expression");
    return true;
}

static bool parse_types(vm_t *m, sizebuf_t *sz)
{
    m->num_types = SZ_ReadLeb(sz);
    VM_ENSURE(m->num_types <= MAX_TYPES, "Too many types");
    m->types = Z_MallocArray(m, m->num_types, sizeof(m->types[0]));

    for (uint32_t c = 0; c < m->num_types; c++) {
        vm_type_t *type = &m->types[c];
        type->form = SZ_ReadLeb(sz);
        VM_ENSURE(type->form == FUNC, "Must be function type");

        type->num_params = SZ_ReadLeb(sz);
        VM_ENSURE(type->num_params <= MAX_LOCALS, "Too many parameters");
        type->params = Z_MallocArray(m, type->num_params, sizeof(type->params[0]));
        for (uint32_t p = 0; p < type->num_params; p++)
            type->params[p] = SZ_ReadLeb(sz);

        type->num_results = SZ_ReadLeb(sz);
        VM_ENSURE(type->num_results <= MAX_RESULTS, "Too many results");
        for (uint32_t r = 0; r < type->num_results; r++)
            type->results[r] = SZ_ReadLeb(sz);
    }

    return true;
}

static bool parse_imports(vm_t *m, sizebuf_t *sz)
{
    uint32_t num_imports = SZ_ReadLeb(sz);
    VM_ENSURE(num_imports <= MAX_FUNCS, "Too many imports");
    m->funcs = Z_MallocArray(m, num_imports, sizeof(m->funcs[0]));

    for (uint32_t gidx = 0; gidx < num_imports; gidx++) {
        bstr_t module = vm_read_string(sz);
        bstr_t name = vm_read_string(sz);
        VM_ENSURE(module.str && name.str, "Read past end of section");

        uint32_t kind = SZ_ReadByte(sz);
        VM_ENSURE(kind == KIND_FUNCTION, "Import of kind %d not supported", kind);

        uint32_t tidx = SZ_ReadLeb(sz);
        VM_ENSURE(tidx < m->num_types, "Bad type index");
        if (!import_function(m, module, name, &m->types[tidx]))
            return false;
    }

    m->num_funcs = m->num_imports;
    return true;
}

static bool parse_functions(vm_t *m, sizebuf_t *sz)
{
    uint32_t count = SZ_ReadLeb(sz);
    VM_ENSURE(count <= MAX_FUNCS - m->num_funcs, "Too many functions");
    m->num_funcs += count;
    m->funcs = Z_ReallocArray(m, m->funcs, m->num_funcs, sizeof(m->funcs[0]));

    for (uint32_t f = m->num_imports; f < m->num_funcs; f++) {
        uint32_t tidx = SZ_ReadLeb(sz);
        VM_ENSURE(tidx < m->num_types, "Bad type index");
        m->funcs[f].type = &m->types[tidx];
    }

    return true;
}

static bool parse_tables(vm_t *m, sizebuf_t *sz)
{
    uint32_t table_count = SZ_ReadLeb(sz);
    VM_ENSURE(table_count == 1, "Only 1 default table supported");

    uint32_t type = SZ_ReadLeb(sz);
    VM_ENSURE(type == FUNCREF, "Must be funcref");

    uint32_t flags = SZ_ReadByte(sz);
    uint32_t tsize = SZ_ReadLeb(sz); // Initial size
    m->table.initial = tsize;
    m->table.size = tsize;
    // Limit the maximum to 64K elements
    if (flags & 0x1) {
        tsize = SZ_ReadLeb(sz); // Max size
        m->table.maximum = min(MAX_ELEMS, tsize);
    } else {
        m->table.maximum = MAX_ELEMS;
    }
    VM_ENSURE(m->table.size <= m->table.maximum, "Bad table size");

    // Allocate the table
    m->table.entries = Z_MallocArray(m, m->table.size, sizeof(m->table.entries[0]));
    return true;
}

static bool parse_memory(vm_t *m, sizebuf_t *sz)
{
    uint32_t memory_count = SZ_ReadLeb(sz);
    VM_ENSURE(memory_count == 1, "Only 1 default memory supported");

    uint32_t flags = SZ_ReadByte(sz);
    uint32_t pages = SZ_ReadLeb(sz); // Initial size
    m->memory.initial = pages;
    m->memory.num_pages = pages;
    // Limit the maximum to 4096 pages (256 MiB)
    if (flags & 0x1) {
        pages = SZ_ReadLeb(sz); // Max size
        m->memory.maximum = min(MAX_PAGES, pages);
    } else {
        m->memory.maximum = MAX_PAGES;
    }
    if (flags & 0x8) {
        uint32_t page_size = SZ_ReadLeb(sz); // Page size
        VM_ENSURE(page_size == VM_PAGE_SIZE, "Page size %u not supported", page_size);
    }
    VM_ENSURE(m->memory.num_pages <= m->memory.maximum, "Bad memory size");

    // Allocate memory
    uintptr_t ptr = (uintptr_t)Z_MallocArray(m, m->memory.num_pages + 1, VM_PAGE_SIZE);
    m->memory.bytes = (uint8_t *)Q_ALIGN(ptr, 4096);
    m->memory.num_bytes = m->memory.num_pages * VM_PAGE_SIZE;
    return true;
}

static bool parse_globals(vm_t *m, sizebuf_t *sz)
{
    uint32_t num_globals = SZ_ReadLeb(sz);
    VM_ENSURE(num_globals <= MAX_GLOBALS, "Too many globals");
    m->globals = Z_MallocArray(m, num_globals, sizeof(m->globals[0]));
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
    VM_ENSURE(num_exports <= SZ_Remaining(sz) / 3, "Too many exports");
    m->exports = Z_MallocArray(m, num_exports, sizeof(m->exports[0]));
    m->num_exports = num_exports;

    for (uint32_t e = 0; e < num_exports; e++) {
        wa_export_t *export = &m->exports[e];
        export->name = vm_read_string(sz);
        VM_ENSURE(export->name.str, "Read past end of section");
        uint32_t kind = SZ_ReadByte(sz);
        uint32_t index = SZ_ReadLeb(sz);
        export->kind = kind;

        switch (kind) {
        case KIND_FUNCTION:
            VM_ENSURE(index < m->num_funcs, "Bad function index");
            export->value = &m->funcs[index];
            break;
        case KIND_TABLE:
            VM_ENSURE(index == 0, "Only 1 default table supported");
            export->value = &m->table;
            break;
        case KIND_MEMORY:
            VM_ENSURE(index == 0, "Only 1 default memory supported");
            export->value = &m->memory;
            break;
        case KIND_GLOBAL:
            VM_ENSURE(index < m->num_globals, "Bad global index");
            export->value = &m->globals[index];
            break;
        default:
            VM_ENSURE(0, "Export of kind %d not supported", kind);
        }
    }

    return true;
}

static bool parse_elements(vm_t *m, sizebuf_t *sz)
{
    uint32_t element_count = SZ_ReadLeb(sz);
    for (uint32_t c = 0; c < element_count; c++) {
        uint32_t flags = SZ_ReadLeb(sz);
        VM_ENSURE(flags == 0, "Element flags %#x not supported", flags);

        // Run the init_expr to get offset
        vm_value_t init = { 0 };
        if (!run_init_expr(m, &init, sz))
            return false;

        uint32_t offset = init.u32;
        uint32_t num_elem = SZ_ReadLeb(sz);
        VM_ENSURE((uint64_t)offset + num_elem <= m->table.size, "Table init out of bounds");
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
        VM_ENSURE(flags == 0, "Segment flags %#x not supported", flags);

        // Run the init_expr to get the offset
        vm_value_t init = { 0 };
        if (!run_init_expr(m, &init, sz))
            return false;

        // Copy the data to the memory offset
        uint32_t offset = init.u32;
        uint32_t size = SZ_ReadLeb(sz);
        VM_ENSURE((uint64_t)offset + size <= m->memory.num_bytes, "Memory init out of bounds");
        void *data = SZ_ReadData(sz, size);
        VM_ENSURE(data, "Read past end of section");
        memcpy(m->memory.bytes + offset, data, size);
    }

    return true;
}

static bool parse_code(vm_t *m, sizebuf_t *sz)
{
    uint32_t body_count = SZ_ReadLeb(sz);
    VM_ENSURE(body_count <= m->num_funcs - m->num_imports, "Too many functions");

    for (uint32_t b = 0; b < body_count; b++) {
        vm_block_t *func = &m->funcs[m->num_imports + b];
        uint32_t body_size = SZ_ReadLeb(sz);
        VM_ENSURE(body_size > 0, "Empty function");
        VM_ENSURE(body_size <= SZ_Remaining(sz), "Function out of bounds");
        uint32_t payload_start = sz->readcount;
        uint32_t num_locals = SZ_ReadLeb(sz);
        uint32_t save_pos, tidx, lidx, lecount;

        // Get number of locals for alloc
        save_pos = sz->readcount;
        func->num_locals = 0;
        for (uint32_t l = 0; l < num_locals; l++) {
            lecount = SZ_ReadLeb(sz);
            VM_ENSURE(lecount <= MAX_LOCALS - func->num_locals, "Too many locals");
            func->num_locals += lecount;
            tidx = SZ_ReadLeb(sz);
            (void)tidx;
        }
        func->locals = Z_MallocArray(m, func->num_locals, sizeof(func->locals[0]));

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
        VM_ENSURE(sz->data[func->end_addr] == End, "Function block doesn't end with End opcode");
        sz->readcount = func->end_addr + 1;
        m->num_code_bytes += func->end_addr - func->start_addr + 1;
        VM_ENSURE(m->num_code_bytes <= INT32_MAX / 2, "Too many bytes of code");
    }

    return true;
}

typedef struct {
    uint32_t pos, len;
} vm_section_t;

typedef bool (*vm_parsefunc_t)(vm_t *m, sizebuf_t *sz);

static const vm_parsefunc_t parsefuncs[NumSections] = {
    [SectTypes]     = parse_types,
    [SectImports]   = parse_imports,
    [SectFunctions] = parse_functions,
    [SectTables]    = parse_tables,
    [SectMemory]    = parse_memory,
    [SectGlobals]   = parse_globals,
    [SectExports]   = parse_exports,
    [SectElements]  = parse_elements,
    [SectCode]      = parse_code,
    [SectData]      = parse_data,
};

static bool parse_sections(vm_t *m, sizebuf_t *sz)
{
    // Read the sections
    vm_section_t sections[NumSections] = { 0 };
    while (sz->readcount < sz->cursize) {
        uint32_t id = SZ_ReadByte(sz);
        uint32_t len = SZ_ReadLeb(sz);
        VM_ENSURE(id < NumSections, "Unknown section %u", id);
        VM_ENSURE(len <= SZ_Remaining(sz), "Section %u out of bounds", id);
        sections[id].pos = sz->readcount;
        sections[id].len = len;
        sz->readcount += len;
    }

    uint32_t cursize = sz->cursize;
    for (uint32_t id = 0; id < NumSections; id++) {
        if (!sections[id].len)
            continue;
        if (!parsefuncs[id])
            continue;
        sz->readcount = sections[id].pos;
        sz->cursize = sections[id].pos + sections[id].len;
        if (!parsefuncs[id](m, sz))
            return false;
        VM_ENSURE(sz->readcount <= sz->cursize, "Read past end of section");
    }

    sz->readcount = 0;
    sz->cursize = cursize;
    return true;
}

static const wa_export_t *find_export(vm_t *m, uint32_t kind, const char *name)
{
    for (uint32_t e = 0; e < m->num_exports; e++) {
        const wa_export_t *export = &m->exports[e];
        if (export->kind == kind && Bstr_IsEqualStr(export->name, name))
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
    m->func_exports = Z_MallocArray(m, m->num_func_exports, sizeof(m->func_exports[0]));

    // Find function exports
    for (e = 0, exp = exports; e < m->num_func_exports; e++, exp++) {
        export = find_export(m, KIND_FUNCTION, exp->name);
        VM_ENSURE(export, "Export %s not found", exp->name);
        const vm_block_t *func = export->value;
        VM_ENSURE(vm_type_eq(func->type, exp->mask), "Export %s type mismatch", exp->name);
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
        m->exports[e].name = bstr_null;

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
    m = Z_TagMalloc(sizeof(*m), TAG_VM);

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
                m->num_code_bytes / 1000, m->memory.num_bytes / 1000000);

    return m;

fail2:
    VM_Free(m);
fail1:
    FS_FreeFile(data);
    return NULL;
}

void VM_Free(vm_t *m)
{
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
