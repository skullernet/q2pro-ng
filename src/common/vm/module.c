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
*/

#include "shared/shared.h"
#include "common/common.h"
#include "common/cvar.h"
#include "common/files.h"
#include "common/list.h"
#include "common/vm.h"
#include "system/system.h"
#include <errno.h>

#define MIN_VM_CVARS    64
#define MAX_VM_CVARS    1024

static LIST_DECL(vm_modules);

typedef const void *(*dll_entry_t)(const void *);

static void *try_load_lib(const char *libdir, const char *gamedir, const char *name)
{
    char path[MAX_OSPATH];
    void *handle;

    if (Q_concat(path, sizeof(path), libdir,
                 PATH_SEP_STRING, gamedir, PATH_SEP_STRING,
                 name, CPUSTRING LIBSUFFIX) >= sizeof(path)) {
        Com_EPrintf("Native library path length exceeded\n");
        return NULL;
    }

    if (os_access(path, X_OK)) {
        Com_DPrintf("Can't access %s: %s\n", path, strerror(errno));
        return NULL;
    }

    handle = Sys_LoadLibrary(path);
    if (handle) {
        Com_Printf("Loaded %s library from %s\n", name, path);
        return handle;
    }

    Com_EPrintf("Failed to load %s library: %s\n", name, Com_GetLastError());
    return NULL;
}

const void *VM_LoadModule(vm_module_t *mod, const vm_interface_t *iface)
{
    Q_assert(!mod->vm);
    Q_assert(!mod->lib);

    if (!com_native_modules->integer) {
        char buffer[MAX_QPATH];
        Q_concat(buffer, sizeof(buffer), "vm/", iface->name, ".qvm");

        mod->vm = VM_Load(buffer, iface->vm_imports, iface->vm_exports);
        if (mod->vm) {
            List_Append(&vm_modules, &mod->entry);
            return iface->dll_exports;
        }

        Com_Error(ERR_DROP, "Couldn't load %s: %s", buffer, Com_GetLastError());
    }

    void *handle = NULL;

    // try game first
    if (fs_game->string[0]) {
        if (sys_homedir->string[0])
            handle = try_load_lib(sys_homedir->string, fs_game->string, iface->name);
        if (!handle)
            handle = try_load_lib(sys_libdir->string, fs_game->string, iface->name);
    }

    // then try baseq2
    if (!handle) {
        if (sys_homedir->string[0])
            handle = try_load_lib(sys_homedir->string, BASEGAME, iface->name);
        if (!handle)
            handle = try_load_lib(sys_libdir->string, BASEGAME, iface->name);
    }

    if (!handle)
        Com_Error(ERR_DROP, "Failed to load %s library", iface->name);

    dll_entry_t entry = Sys_GetProcAddress(handle, iface->dll_entry_name);
    if (!entry) {
        Sys_FreeLibrary(handle);
        Com_Error(ERR_DROP, "%s entry point not found", iface->name);
    }

    const void *exports = entry(iface->dll_imports);
    if (!exports) {
        Sys_FreeLibrary(handle);
        Com_Error(ERR_DROP, "%s returned NULL exports", iface->name);
    }

    uint32_t version = *(const uint32_t *)exports;
    if (version != iface->api_version) {
        Sys_FreeLibrary(handle);
        Com_Error(ERR_DROP, "%s is version %d, expected %d", iface->name, version, iface->api_version);
    }

    List_Append(&vm_modules, &mod->entry);
    mod->lib = handle;
    return exports;
}

static void VM_UpdateCvar(vm_cvar_t *var, const cvar_t *cv)
{
    var->integer = cv->integer;
    var->value = cv->value;
    var->modified = true;
    Q_strlcpy(var->string, cv->string, sizeof(var->string));
}

bool VM_RegisterCvar(vm_module_t *mod, vm_cvar_t *vmc, const char *name, const char *value, unsigned flags)
{
    cvar_t *var = Cvar_Get(name, value, flags);
    if (!var)
        return false;
    if (!vmc)
        return true;

    for (int i = 0; i < mod->num_cvars; i++)
        if (mod->cvars[i].vmc == vmc)
            return true;

    if (mod->num_cvars >= MAX_VM_CVARS) {
        Com_WPrintf("Too many VM cvars\n");
        return false;
    }

    if (!(mod->num_cvars & (MIN_VM_CVARS - 1)))
        mod->cvars = VM_Realloc(mod->cvars, (mod->num_cvars + MIN_VM_CVARS) * sizeof(mod->cvars[0]));

    vm_cvar_glue_t *glue = &mod->cvars[mod->num_cvars++];
    glue->vmc = vmc;
    glue->var = var;

    VM_UpdateCvar(vmc, var);
    return true;
}

void VM_CvarChanged(const cvar_t *var)
{
    vm_module_t *mod;

    LIST_FOR_EACH(mod, &vm_modules, entry)
        for (int i = 0; i < mod->num_cvars; i++)
            if (mod->cvars[i].var == var)
                VM_UpdateCvar(mod->cvars[i].vmc, var);
}

int64_t VM_OpenFile(vm_module_t *mod, const char *path, qhandle_t *f, unsigned mode)
{
    qhandle_t h;
    int64_t ret;
    int i;

    // NULL handle tests for file existence
    if (!f) {
        if ((mode & FS_MODE_MASK) != FS_MODE_READ)
            return Q_ERR(EINVAL);
        ret = FS_OpenFile(path, &h, mode | FS_FLAG_LOADFILE);
        if (h)
            FS_CloseFile(h);
        return ret;
    }

    *f = 0;

    if (mode & FS_FLAG_LOADFILE)
        return Q_ERR(EPERM);

    if ((mode & FS_MODE_MASK) != FS_MODE_READ) {
        char normalized[MAX_OSPATH];
        if (FS_NormalizePathBuffer(normalized, path, sizeof(normalized)) >= sizeof(normalized))
            return Q_ERR(ENAMETOOLONG);
        if (!strchr(normalized, '/'))
            return Q_ERR(EPERM);
    }

    for (i = 0; i < MAX_VM_HANDLES; i++)
        if (!mod->handles[i])
            break;
    if (i == MAX_VM_HANDLES)
        return Q_ERR(EMFILE);

    ret = FS_OpenFile(path, &h, mode);
    if (h) {
        *f = i + 1;
        mod->handles[i] = h;
    }

    return ret;
}

void VM_FreeModule(vm_module_t *mod)
{
    VM_Free(mod->vm);
    Sys_FreeLibrary(mod->lib);
    Z_Free(mod->cvars);

    for (int i = 0; i < MAX_VM_HANDLES; i++)
        if (mod->handles[i])
            FS_CloseFile(mod->handles[i]);

    if (mod->entry.next)
        List_Remove(&mod->entry);
    memset(mod, 0, sizeof(*mod));
}
