#include "client.h"

cgame_export_t *cge;

void CL_InitCGame(void)
{
    cgame_entry_t entry;

    if (cls.game_library)
        return;

    cls.game_library = Sys_LoadGameLibrary();
    if (!cls.game_library)
        Com_Error(ERR_DROP, "Couldn't load game library");

    entry = Sys_GetProcAddress(cls.game_library, "GetCGameAPI");
    if (!entry)
        Com_Error(ERR_DROP, "Couldn't get cgame entry point");

    static const cgame_import_t import = {
        0
    };

    cge = entry(&import);
}

void CL_FreeCGame(void)
{
    if (cge) {
        cge->Shutdown();
        cge = NULL;
    }
    if (cls.game_library) {
        Sys_FreeLibrary(cls.game_library);
        cls.game_library = NULL;
    }
}
