/*
Copyright (C) 1997-2001 Id Software, Inc.

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

#include "client.h"

static keywaitcb_t  key_wait_cb;
static void         *key_wait_arg;

static char     *keybindings[256];

// bitmap of keys not passed to interpreter while in console
static byte     consolekeys[256 / 8];

// key to map to if shift held down in console
static byte     keyshift[256];

// key down status: if > 1, it is auto-repeating
static byte     keydown[256];

// number of keys down for BUTTON_ANY
static int      anykeydown;

// bitmap for generating button up commands
static byte     buttondown[256 / 8];

static bool     key_overstrike;

typedef struct {
    const char  *name;
    int         keynum;
} keyname_t;

#define K(x) { #x, K_##x }

static const keyname_t keynames[] = {
    K(BACKSPACE),
    K(TAB),
    K(ENTER),
    K(PAUSE),
    K(ESCAPE),
    K(SPACE),

    K(UPARROW),
    K(DOWNARROW),
    K(LEFTARROW),
    K(RIGHTARROW),

    K(LALT),
    K(RALT),
    K(LCTRL),
    K(RCTRL),
    K(LSHIFT),
    K(RSHIFT),

    K(F1),
    K(F2),
    K(F3),
    K(F4),
    K(F5),
    K(F6),
    K(F7),
    K(F8),
    K(F9),
    K(F10),
    K(F11),
    K(F12),

    K(INS),
    K(DEL),
    K(PGDN),
    K(PGUP),
    K(HOME),
    K(END),

    K(102ND),

    K(NUMLOCK),
    K(CAPSLOCK),
    K(SCROLLOCK),
    K(LWINKEY),
    K(RWINKEY),
    K(MENU),
    K(PRINTSCREEN),

    K(KP_HOME),
    K(KP_UPARROW),
    K(KP_PGUP),
    K(KP_LEFTARROW),
    K(KP_5),
    K(KP_RIGHTARROW),
    K(KP_END),
    K(KP_DOWNARROW),
    K(KP_PGDN),
    K(KP_ENTER),
    K(KP_INS),
    K(KP_DEL),
    K(KP_SLASH),
    K(KP_MINUS),
    K(KP_PLUS),
    K(KP_MULTIPLY),

    K(MOUSE1),
    K(MOUSE2),
    K(MOUSE3),
    K(MOUSE4),
    K(MOUSE5),
    K(MOUSE6),
    K(MOUSE7),
    K(MOUSE8),

    K(MWHEELUP),
    K(MWHEELDOWN),
    K(MWHEELRIGHT),
    K(MWHEELLEFT),

    {"SEMICOLON", ';'}, // because a raw semicolon separates commands

    {NULL, 0}
};

#undef K

//============================================================================

/*
===================
Key_GetOverstrikeMode
===================
*/
bool Key_GetOverstrikeMode(void)
{
    return key_overstrike;
}

/*
===================
Key_SetOverstrikeMode
===================
*/
void Key_SetOverstrikeMode(bool overstrike)
{
    key_overstrike = overstrike;
}

/*
===================
Key_GetDest
===================
*/
keydest_t Key_GetDest(void)
{
    return cls.key_dest;
}

/*
===================
Key_SetDest
===================
*/
void Key_SetDest(keydest_t dest)
{
    keydest_t diff;

// if not connected, console or menu should be up
    if (cls.state < ca_active && !(dest & (KEY_MENU | KEY_CONSOLE))) {
        dest |= KEY_CONSOLE;
    }

    diff = cls.key_dest ^ dest;
    cls.key_dest = dest;

// activate or deactivate mouse
    if (diff & (KEY_CONSOLE | KEY_MENU | KEY_GAME)) {
        IN_Activate();
        CL_CheckForPause();
    }

    if (dest == KEY_NONE) {
        anykeydown = 0;
    }
}

/*
===================
Key_IsDown

Returns key down status: if > 1, it is auto-repeating
===================
*/
int Key_IsDown(int key)
{
    switch (key) {
    case K_ALT:
        return keydown[K_LALT] || keydown[K_RALT];
    case K_CTRL:
        return keydown[K_LCTRL] || keydown[K_RCTRL];
    case K_SHIFT:
        return keydown[K_LSHIFT] || keydown[K_RSHIFT];
    }

    if (key < 0 || key > 255) {
        return 0;
    }

    return keydown[key];
}

/*
===================
Key_AnyKeyDown

Returns total number of keys down.
===================
*/
int Key_AnyKeyDown(void)
{
    return anykeydown;
}

/*
===================
Key_StringToKeynum

Returns a key number to be used to index keybindings[] by looking at
the given string.  Single ascii characters return themselves, while
the K_* names are matched up.
===================
*/
int Key_StringToKeynum(const char *str)
{
    const keyname_t *kn;

    if (!str || !str[0])
        return -1;
    if (!str[1])
        return Q_tolower(str[0]);

    for (kn = keynames; kn->name; kn++) {
        if (!Q_stricmp(str, kn->name))
            return kn->keynum;
    }
    return -1;
}

/*
===================
Key_KeynumToString

Returns a string (either a single ascii char, or a K_* name) for the
given keynum.
FIXME: handle quote special (general escape sequence?)
===================
*/
const char *Key_KeynumToString(int keynum)
{
    const keyname_t *kn;
    static char tinystr[2];

    if (keynum == -1)
        return "<KEY NOT FOUND>";

    if (keynum > 32 && keynum < 127 && keynum != ';' && keynum != '"') {
        // printable ascii
        tinystr[0] = keynum;
        tinystr[1] = 0;
        return tinystr;
    }

    for (kn = keynames; kn->name; kn++)
        if (keynum == kn->keynum)
            return kn->name;

    return "<UNKNOWN KEYNUM>";
}

/*
===================
Key_SetBinding

Returns the name of the first key found.
===================
*/
const char *Key_GetBinding(const char *binding)
{
    int key;

    for (key = 0; key < 256; key++) {
        if (keybindings[key]) {
            if (!Q_stricmp(keybindings[key], binding)) {
                return Key_KeynumToString(key);
            }
        }
    }

    return "";
}

/*
===================
Key_EnumBindings
===================
*/
int Key_EnumBindings(int key, const char *binding)
{
    if (key < 0) {
        key = 0;
    }
    for (; key < 256; key++) {
        if (keybindings[key]) {
            if (!Q_stricmp(keybindings[key], binding)) {
                return key;
            }
        }
    }

    return -1;
}

/*
===================
Key_SetBinding
===================
*/
void Key_SetBinding(int keynum, const char *binding)
{
    if (keynum < 0 || keynum > 255)
        return;

// free old binding
    Z_Free(keybindings[keynum]);

// allocate memory for new binding
    keybindings[keynum] = Z_CopyString(binding);
}

static void Key_Name_g(void)
{
    const keyname_t *k;

    Prompt_SetOptions(CMPL_CASELESS);
    for (k = keynames; k->name; k++)
        Prompt_AddMatch(k->name);
}

static void Key_Bound_g(void)
{
    int i;

    Prompt_SetOptions(CMPL_CASELESS);
    for (i = 0; i < 256; i++)
        if (keybindings[i])
            Prompt_AddMatch(Key_KeynumToString(i));
}

static void Key_Bind_c(int firstarg, int argnum)
{
    if (argnum == 1) {
        Key_Name_g();
    } else {
        Com_Generic_c(firstarg + 2, argnum - 2);
    }
}

static void Key_Unbind_c(int firstarg, int argnum)
{
    if (argnum == 1) {
        Key_Bound_g();
    }
}

/*
===================
Key_Unbind_f
===================
*/
static void Key_Unbind_f(void)
{
    int     b;

    if (Cmd_Argc() != 2) {
        Com_Printf("unbind <key> : remove commands from a key\n");
        return;
    }

    b = Key_StringToKeynum(Cmd_Argv(1));
    if (b == -1) {
        Com_Printf("\"%s\" isn't a valid key\n", Cmd_Argv(1));
        return;
    }

    Key_SetBinding(b, NULL);
}

/*
===================
Key_Unbindall_f
===================
*/
static void Key_Unbindall_f(void)
{
    int     i;

    for (i = 0; i < 256; i++)
        if (keybindings[i])
            Key_SetBinding(i, NULL);
}


/*
===================
Key_Bind_f
===================
*/
static void Key_Bind_f(void)
{
    int c, b;

    c = Cmd_Argc();

    if (c < 2) {
        Com_Printf("bind <key> [command] : attach a command to a key\n");
        return;
    }
    b = Key_StringToKeynum(Cmd_Argv(1));
    if (b == -1) {
        Com_Printf("\"%s\" isn't a valid key\n", Cmd_Argv(1));
        return;
    }

    if (c == 2) {
        if (keybindings[b])
            Com_Printf("\"%s\" = \"%s\"\n", Cmd_Argv(1), keybindings[b]);
        else
            Com_Printf("\"%s\" is not bound\n", Cmd_Argv(1));
        return;
    }

// copy the rest of the command line
    Key_SetBinding(b, Cmd_ArgsFrom(2));
}

/*
============
Key_WriteBindings

Writes lines containing "bind key value"
============
*/
void Key_WriteBindings(qhandle_t f)
{
    int     i;

    for (i = 0; i < 256; i++) {
        if (keybindings[i] && keybindings[i][0]) {
            FS_FPrintf(f, "bind %s \"%s\"\n", Key_KeynumToString(i),
                       keybindings[i]);
        }
    }
}


/*
============
Key_Bindlist_f

============
*/
static void Key_Bindlist_f(void)
{
    int     i;

    for (i = 0; i < 256; i++) {
        if (keybindings[i] && keybindings[i][0]) {
            Com_Printf("%s \"%s\"\n", Key_KeynumToString(i),
                       keybindings[i]);
        }
    }
}

static const cmdreg_t c_keys[] = {
    { "bind", Key_Bind_f, Key_Bind_c },
    { "unbind", Key_Unbind_f, Key_Unbind_c },
    { "unbindall", Key_Unbindall_f },
    { "bindlist", Key_Bindlist_f },

    { NULL }
};

/*
===================
Key_Init
===================
*/
void Key_Init(void)
{
    int     i;

//
// init ascii characters in console mode
//
    for (i = K_ASCIIFIRST; i <= K_ASCIILAST; i++)
        Q_SetBit(consolekeys, i);

#define K(x) \
    Q_SetBit(consolekeys, K_##x)

    K(BACKSPACE);
    K(TAB);
    K(ENTER);

    K(UPARROW);
    K(DOWNARROW);
    K(LEFTARROW);
    K(RIGHTARROW);

    K(LALT);
    K(RALT);
    K(LCTRL);
    K(RCTRL);
    K(LSHIFT);
    K(RSHIFT);

    K(INS);
    K(DEL);
    K(PGDN);
    K(PGUP);
    K(HOME);
    K(END);

    K(KP_HOME);
    K(KP_UPARROW);
    K(KP_PGUP);
    K(KP_LEFTARROW);
    K(KP_5);
    K(KP_RIGHTARROW);
    K(KP_END);
    K(KP_DOWNARROW);
    K(KP_PGDN);
    K(KP_ENTER);
    K(KP_INS);
    K(KP_DEL);
    K(KP_SLASH);
    K(KP_MINUS);
    K(KP_PLUS);
    K(KP_MULTIPLY);

    K(MOUSE3);

    K(MWHEELUP);
    K(MWHEELDOWN);

#undef K

//
// init ascii keyshift characters
//
    for (i = 0; i < 256; i++)
        keyshift[i] = i;
    for (i = 'a'; i <= 'z'; i++)
        keyshift[i] = i - 'a' + 'A';

    keyshift['1'] = '!';
    keyshift['2'] = '@';
    keyshift['3'] = '#';
    keyshift['4'] = '$';
    keyshift['5'] = '%';
    keyshift['6'] = '^';
    keyshift['7'] = '&';
    keyshift['8'] = '*';
    keyshift['9'] = '(';
    keyshift['0'] = ')';
    keyshift['-'] = '_';
    keyshift['='] = '+';
    keyshift[','] = '<';
    keyshift['.'] = '>';
    keyshift['/'] = '?';
    keyshift[';'] = ':';
    keyshift['\''] = '"';
    keyshift['['] = '{';
    keyshift[']'] = '}';
    keyshift['`'] = '~';
    keyshift['\\'] = '|';

//
// register our functions
//
    Cmd_Register(c_keys);
}

/*
===================
Key_Event

Called by the system between frames for both key up and key down events
Should NOT be called during an interrupt!
===================
*/
void Key_Event(unsigned key, bool down, unsigned time)
{
    char    *kb;
    char    cmd[MAX_STRING_CHARS];

    Q_assert(key < 256);

    Com_DDDPrintf("%u: %c%s\n", time,
                  down ? '+' : '-', Key_KeynumToString(key));

    // hack for menu key binding
    if (key_wait_cb && down && !key_wait_cb(key_wait_arg, key)) {
        return;
    }

    // update key down and auto-repeat status
    if (down) {
        if (keydown[key] < 255)
            keydown[key]++;
    } else {
        keydown[key] = 0;
    }

    // console key is hardcoded, so the user can never unbind it
    if (!Key_IsDown(K_SHIFT) && (key == '`' || key == '~')) {
        if (keydown[key] == 1) {
            Con_ToggleConsole_f();
        }
        return;
    }

    // Alt+Enter is hardcoded for all systems
    if (Key_IsDown(K_ALT) && key == K_ENTER) {
        if (keydown[key] == 1) {
            VID_ToggleFullscreen();
        }
        return;
    }

    // menu key is hardcoded, so the user can never unbind it
    if (key == K_ESCAPE) {
        if (!down) {
            return;
        }

        // ignore autorepeats
        if (keydown[key] > 1) {
            return;
        }

        if (cls.key_dest & KEY_CONSOLE) {
            if (cls.state < ca_active && !(cls.key_dest & KEY_MENU)) {
                UI_OpenMenu(UIMENU_MAIN);
            } else {
                Con_Close(true);
            }
        } else if (cls.key_dest & KEY_MENU) {
            UI_KeyEvent(key, down);
        } else if (cls.key_dest & KEY_MESSAGE) {
            Key_Message(key);
        } else if (cls.state >= ca_active) {
            if (!(cge && cge->KeyEvent(key, down)))
                UI_OpenMenu(UIMENU_GAME);
        } else {
            UI_OpenMenu(UIMENU_MAIN);
        }
        return;
    }

    // track if any key is down for BUTTON_ANY
    if (down) {
        if (keydown[key] == 1)
            anykeydown++;
    } else {
        anykeydown--;
        if (anykeydown < 0)
            anykeydown = 0;
    }

    // hack for demo freelook in windowed mode
    if (cls.key_dest == KEY_NONE && cls.demo.playback && (key == K_LSHIFT || key == K_RSHIFT) && keydown[key] <= 1) {
        IN_Activate();
    }

//
// if not a consolekey, send to the interpreter no matter what mode is
//
    if ((cls.key_dest == KEY_NONE) ||
        ((cls.key_dest & KEY_CONSOLE) && !Q_IsBitSet(consolekeys, key)) ||
        ((cls.key_dest & KEY_MENU) && (key >= K_F1 && key <= K_F12)) ||
        (!down && Q_IsBitSet(buttondown, key))) {
//
// Key up events only generate commands if the game key binding is a button
// command (leading + sign). These will occur even in console mode, to keep the
// character from continuing an action started before a console switch. Button
// commands include the kenum as a parameter, so multiple downs can be matched
// with ups.
//
        if (!down) {
            kb = keybindings[key];
            if (kb && kb[0] == '+') {
                Q_snprintf(cmd, sizeof(cmd), "-%s %i %i\n",
                           kb + 1, key, time);
                Cbuf_AddText(&cmd_buffer, cmd);
            }
            Q_ClearBit(buttondown, key);
            return;
        }

        // ignore autorepeats
        if (keydown[key] > 1) {
            return;
        }

        // generate button up command when released
        Q_SetBit(buttondown, key);

        kb = keybindings[key];
        if (kb) {
            if (kb[0] == '+') {
                // button commands add keynum and time as a param
                Q_snprintf(cmd, sizeof(cmd), "%s %i %i\n", kb, key, time);
                Cbuf_AddText(&cmd_buffer, cmd);

                // skip the rest of the cinematic
                if (cls.state == ca_cinematic) {
                    SCR_FinishCinematic();
                }
            } else {
                Cbuf_AddText(&cmd_buffer, kb);
                Cbuf_AddText(&cmd_buffer, "\n");
            }
        }
        return;
    }

    if (!down) {
        if (cls.key_dest & KEY_MENU)
            UI_KeyEvent(key, down);
        if (cls.key_dest & KEY_GAME)
            cge->KeyEvent(key, down);
        return;     // other subsystems only care about key down events
    }

    if (cls.key_dest & KEY_CONSOLE) {
        Key_Console(key);
    } else if (cls.key_dest & KEY_MENU) {
        UI_KeyEvent(key, down);
    } else if (cls.key_dest & KEY_MESSAGE) {
        Key_Message(key);
    } else if (cls.key_dest & KEY_GAME) {
        cge->KeyEvent(key, down);
    }

    if (Key_IsDown(K_CTRL) || Key_IsDown(K_ALT)) {
        return;
    }

    switch (key) {
    case K_KP_SLASH:
        key = '/';
        break;
    case K_KP_MULTIPLY:
        key = '*';
        break;
    case K_KP_MINUS:
        key = '-';
        break;
    case K_KP_PLUS:
        key = '+';
        break;
    case K_KP_HOME:
        key = '7';
        break;
    case K_KP_UPARROW:
        key = '8';
        break;
    case K_KP_PGUP:
        key = '9';
        break;
    case K_KP_LEFTARROW:
        key = '4';
        break;
    case K_KP_5:
        key = '5';
        break;
    case K_KP_RIGHTARROW:
        key = '6';
        break;
    case K_KP_END:
        key = '1';
        break;
    case K_KP_DOWNARROW:
        key = '2';
        break;
    case K_KP_PGDN:
        key = '3';
        break;
    case K_KP_INS:
        key = '0';
        break;
    case K_KP_DEL:
        key = '.';
        break;
    }

    // if key is printable, generate char events
    if (key < 32 || key >= 127) {
        return;
    }

    if (Key_IsDown(K_SHIFT)) {
        key = keyshift[key];
    }

    if (cls.key_dest & KEY_CONSOLE) {
        Char_Console(key);
    } else if (cls.key_dest & KEY_MENU) {
        UI_CharEvent(key);
    } else if (cls.key_dest & KEY_MESSAGE) {
        Char_Message(key);
    } else if (cls.key_dest & KEY_GAME) {
        cge->CharEvent(key);
    }
}

/*
===================
Key_MouseEvent

Routes absolute mouse movement events to appropriate place.
===================
*/
void Key_MouseEvent(int x, int y)
{
    if (cls.key_dest & KEY_MENU)
        UI_MouseEvent(x, y);
    if (cls.key_dest & KEY_GAME)
        cge->MouseEvent(x, y);
}

/*
===================
Key_ClearStates
===================
*/
void Key_ClearStates(void)
{
    int     i;

    // hack for menu key binding
    if (key_wait_cb) {
        key_wait_cb(key_wait_arg, K_ESCAPE);
        key_wait_cb = NULL;
    }

    for (i = 0; i < 256; i++) {
        if (keydown[i])
            Key_Event(i, false, com_eventTime);
    }

    memset(buttondown, 0, sizeof(buttondown));
    anykeydown = 0;
}

/*
===================
Key_WaitKey
===================
*/
void Key_WaitKey(keywaitcb_t wait, void *arg)
{
    key_wait_cb = wait;
    key_wait_arg = arg;
}
