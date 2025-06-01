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
#include "common/vm.h"
#include "system/system.h"
#include <errno.h>

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
        Com_Printf("Can't access %s: %s\n", path, strerror(errno));
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

    char buffer[MAX_QPATH];
    Q_concat(buffer, sizeof(buffer), "vm/", iface->name, ".qvm");

    if (!Cvar_VariableInteger("vm_native") && FS_FileExists(buffer)) {
        mod->vm = VM_Load(buffer, iface->vm_imports, iface->vm_exports);
        if (mod->vm)
            return iface->dll_exports;
        Com_WPrintf("Couldn't load %s: %s\n", buffer, Com_GetLastError());
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

    mod->lib = handle;
    return exports;
}

void VM_FreeModule(vm_module_t *mod)
{
    VM_Free(mod->vm);
    Sys_FreeLibrary(mod->lib);
    memset(mod, 0, sizeof(*mod));
}
