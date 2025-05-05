#include "shared/shared.h"
#include "system/system.h"
#include "common/common.h"
#include "common/files.h"
#include <errno.h>

static void *load_game_lib(const char *path)
{
    void *handle;

    handle = Sys_LoadLibrary(path);
    if (!handle)
        Com_EPrintf("Failed to load game library: %s\n", Com_GetLastError());
    else
        Com_Printf("Loaded game library from %s\n", path);

    return handle;
}

static void *try_load_game(const char *libdir, const char *gamedir)
{
    char path[MAX_OSPATH];

    if (Q_concat(path, sizeof(path), libdir,
                 PATH_SEP_STRING, gamedir, PATH_SEP_STRING,
                 "game" CPUSTRING LIBSUFFIX) >= sizeof(path)) {
        Com_EPrintf("Game library path length exceeded\n");
        return NULL;
    }

    if (os_access(path, X_OK)) {
        Com_Printf("Can't access %s: %s\n", path, strerror(errno));
        return NULL;
    }

    return load_game_lib(path);
}

void *Sys_LoadGameLibrary(void)
{
    void *handle = NULL;

    // for debugging or `proxy' mods
    if (sys_forcegamelib->string[0])
        handle = load_game_lib(sys_forcegamelib->string);

    // try game first
    if (!handle && fs_game->string[0]) {
        if (sys_homedir->string[0])
            handle = try_load_game(sys_homedir->string, fs_game->string);
        if (!handle)
            handle = try_load_game(sys_libdir->string, fs_game->string);
    }

    // then try baseq2
    if (!handle) {
        if (sys_homedir->string[0])
            handle = try_load_game(sys_homedir->string, BASEGAME);
        if (!handle)
            handle = try_load_game(sys_libdir->string, BASEGAME);
    }

    return handle;
}
