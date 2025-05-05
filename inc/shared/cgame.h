
//
// functions provided by the main engine
//
typedef struct {
    uint32_t    apiversion;
    uint32_t    structsize;
} cgame_import_t;

//
// functions exported by the cgame subsystem
//
typedef struct {
    uint32_t    apiversion;
    uint32_t    structsize;

    void (*Init)(void);
    void (*Shutdown)(void);
    void (*Pmove)(pmove_t *pmove);
} cgame_export_t;

typedef cgame_export_t *(*cgame_entry_t)(const cgame_import_t *);
