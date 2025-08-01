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
#include <hidusage.h>

win_state_t     win;

static cvar_t   *vid_flip_on_switch;
static cvar_t   *vid_hwgamma;
static cvar_t   *win_noalttab;
static cvar_t   *win_disablewinkey;
static cvar_t   *win_noresize;
static cvar_t   *win_notitle;
static cvar_t   *win_alwaysontop;
static cvar_t   *win_noborder;

static void     Win_ClipCursor(void);

/*
===============================================================================

COMMON WIN32 VIDEO RELATED ROUTINES

===============================================================================
*/

static void Win_SetPosition(void)
{
    RECT            r;
    LONG            style;
    int             x, y, w, h;
    HWND            after;

    // get previous window style
    style = GetWindowLong(win.wnd, GWL_STYLE);
    style &= ~(WS_OVERLAPPEDWINDOW | WS_POPUP | WS_DLGFRAME);

    // set new style bits
    if (win.flags & QVF_FULLSCREEN) {
        after = HWND_TOPMOST;
        style |= WS_POPUP;
    } else {
        if (win_alwaysontop->integer) {
            after = HWND_TOPMOST;
        } else {
            after = HWND_NOTOPMOST;
        }
        style |= WS_OVERLAPPED;
        if (win_noborder->integer) {
            style |= WS_POPUP | WS_MINIMIZEBOX | WS_MAXIMIZEBOX; // allow minimize and maximize hotkeys.
        } else if (win_notitle->integer) {
            if (win_noresize->integer) {
                style |= WS_DLGFRAME;
            } else {
                style |= WS_THICKFRAME;
            }
        } else {
            style |= WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX;
            if (!win_noresize->integer) {
                style |= WS_THICKFRAME;
            }
        }
    }

    // adjust for non-client area
    r.left = 0;
    r.top = 0;
    r.right = win.rc.width;
    r.bottom = win.rc.height;

    AdjustWindowRect(&r, style, FALSE);

    // figure out position
    x = win.rc.x;
    y = win.rc.y;
    w = r.right - r.left;
    h = r.bottom - r.top;

    // clip to monitor work area
    if (!(win.flags & QVF_FULLSCREEN)) {
        OffsetRect(&r, x, y);
        MONITORINFO mi = { .cbSize = sizeof(mi) };
        if (GetMonitorInfoA(MonitorFromRect(&r, MONITOR_DEFAULTTONEAREST), &mi)) {
            x = max(mi.rcWork.left, min(mi.rcWork.right  - w, x));
            y = max(mi.rcWork.top,  min(mi.rcWork.bottom - h, y));
        }
    }

    // set new window style and position
    SetWindowLong(win.wnd, GWL_STYLE, style);
    SetWindowPos(win.wnd, after, x, y, w, h, SWP_FRAMECHANGED | SWP_SHOWWINDOW);
    UpdateWindow(win.wnd);
    SetForegroundWindow(win.wnd);
    SetFocus(win.wnd);

    if (win.mouse.grabbed) {
        Win_ClipCursor();
    }
}

/*
============
Win_ModeChanged
============
*/
static void Win_ModeChanged(void)
{
    R_ModeChanged(win.rc.width, win.rc.height, win.flags);
    SCR_ModeChanged();
}

static int modecmp(const void *p1, const void *p2)
{
    const DEVMODE *dm1 = (const DEVMODE *)p1;
    const DEVMODE *dm2 = (const DEVMODE *)p2;
    DWORD size1 = dm1->dmPelsWidth * dm1->dmPelsHeight;
    DWORD size2 = dm2->dmPelsWidth * dm2->dmPelsHeight;

    // sort from highest resolution to lowest
    if (size1 < size2)
        return 1;
    if (size1 > size2)
        return -1;

    // sort from highest frequency to lowest
    if (dm1->dmDisplayFrequency < dm2->dmDisplayFrequency)
        return 1;
    if (dm1->dmDisplayFrequency > dm2->dmDisplayFrequency)
        return -1;

    return 0;
}

static bool mode_is_sane(const DEVMODE *dm)
{
    // should have all these flags set
    if (~dm->dmFields & (DM_BITSPERPEL | DM_PELSWIDTH | DM_PELSHEIGHT | DM_DISPLAYFLAGS | DM_DISPLAYFREQUENCY))
        return false;

    // grayscale and interlaced modes are not supported
    if (dm->dmDisplayFlags & (DM_GRAYSCALE | DM_INTERLACED))
        return false;

    // according to MSDN, frequency can be 0 or 1 for some weird hardware
    if (dm->dmDisplayFrequency == 0 || dm->dmDisplayFrequency == 1)
        return false;

    return true;
}

static bool modes_are_equal(const DEVMODE *base, const DEVMODE *compare)
{
    if (!mode_is_sane(base))
        return false;

    if ((compare->dmFields & DM_PELSWIDTH) && base->dmPelsWidth != compare->dmPelsWidth)
        return false;

    if ((compare->dmFields & DM_PELSHEIGHT) && base->dmPelsHeight != compare->dmPelsHeight)
        return false;

    if ((compare->dmFields & DM_BITSPERPEL) && base->dmBitsPerPel != compare->dmBitsPerPel)
        return false;

    if ((compare->dmFields & DM_DISPLAYFREQUENCY) && base->dmDisplayFrequency != compare->dmDisplayFrequency)
        return false;

    return true;
}

/*
============
Win_GetModeList
============
*/
char *Win_GetModeList(void)
{
    DEVMODE desktop, dm, *modes;
    int i, j, num_modes, max_modes;
    size_t size, len;
    char *buf;

    memset(&desktop, 0, sizeof(desktop));
    desktop.dmSize = sizeof(desktop);

    if (!EnumDisplaySettings(NULL, ENUM_REGISTRY_SETTINGS, &desktop))
        return Z_CopyString(VID_MODELIST);

    modes = NULL;
    num_modes = 0;
    max_modes = 0;
    for (i = 0; i < 4096; i++) {
        memset(&dm, 0, sizeof(dm));
        dm.dmSize = sizeof(dm);
        if (!EnumDisplaySettings(NULL, i, &dm))
            break;

        // sanity check
        if (!mode_is_sane(&dm))
            continue;

        // completely ignore non-desktop bit depths for now
        if (dm.dmBitsPerPel != desktop.dmBitsPerPel)
            continue;

        // skip duplicate modes
        for (j = 0; j < num_modes; j++)
            if (modes_are_equal(&modes[j], &dm))
                break;
        if (j != num_modes)
            continue;

        if (num_modes == max_modes) {
            max_modes += 32;
            modes = Z_Realloc(modes, sizeof(modes[0]) * max_modes);
        }

        modes[num_modes++] = dm;
    }

    if (!num_modes)
        return Z_CopyString(VID_MODELIST);

    qsort(modes, num_modes, sizeof(modes[0]), modecmp);

    size = 8 + num_modes * 32 + 1;
    buf = Z_Malloc(size);

    len = Q_strlcpy(buf, "desktop ", size);
    for (i = 0; i < num_modes; i++) {
        len += Q_scnprintf(buf + len, size - len, "%lux%lu@%lu ",
                           modes[i].dmPelsWidth,
                           modes[i].dmPelsHeight,
                           modes[i].dmDisplayFrequency);
    }
    buf[len - 1] = 0;

    Z_Free(modes);

    return buf;
}

// avoid doing CDS to the same fullscreen mode to reduce flickering
static bool mode_is_current(const DEVMODE *dm)
{
    DEVMODE current;

    memset(&current, 0, sizeof(current));
    current.dmSize = sizeof(current);

    if (!EnumDisplaySettings(NULL, ENUM_CURRENT_SETTINGS, &current))
        return false;

    return modes_are_equal(&current, dm);
}

static LONG set_fullscreen_mode(void)
{
    DEVMODE desktop, dm;
    LONG ret;
    int freq, depth;

    memset(&desktop, 0, sizeof(desktop));
    desktop.dmSize = sizeof(desktop);

    EnumDisplaySettings(NULL, ENUM_REGISTRY_SETTINGS, &desktop);

    // parse vid_modelist specification
    if (VID_GetFullscreen(&win.rc, &freq, &depth)) {
        Com_DPrintf("...setting fullscreen mode: %dx%d\n",
                    win.rc.width, win.rc.height);
    } else if (mode_is_sane(&desktop)) {
        win.rc.width = desktop.dmPelsWidth;
        win.rc.height = desktop.dmPelsHeight;
        Com_DPrintf("...falling back to desktop mode: %dx%d\n",
                    win.rc.width, win.rc.height);
    } else {
        Com_DPrintf("...falling back to default mode: %dx%d\n",
                    win.rc.width, win.rc.height);
    }

    memset(&dm, 0, sizeof(dm));
    dm.dmSize       = sizeof(dm);
    dm.dmPelsWidth  = win.rc.width;
    dm.dmPelsHeight = win.rc.height;
    dm.dmFields     = DM_PELSWIDTH | DM_PELSHEIGHT;

    if (freq) {
        dm.dmDisplayFrequency = freq;
        dm.dmFields |= DM_DISPLAYFREQUENCY;
        Com_DPrintf("...using display frequency of %d\n", freq);
    } else if (modes_are_equal(&desktop, &dm)) {
        dm.dmDisplayFrequency = desktop.dmDisplayFrequency;
        dm.dmFields |= DM_DISPLAYFREQUENCY;
        Com_DPrintf("...using desktop display frequency of %lu\n", desktop.dmDisplayFrequency);
    }

    if (depth) {
        dm.dmBitsPerPel = depth;
        dm.dmFields |= DM_BITSPERPEL;
        Com_DPrintf("...using bitdepth of %d\n", depth);
    } else if (mode_is_sane(&desktop)) {
        dm.dmBitsPerPel = desktop.dmBitsPerPel;
        dm.dmFields |= DM_BITSPERPEL;
        Com_DPrintf("...using desktop bitdepth of %lu\n", desktop.dmBitsPerPel);
    }

    if (mode_is_current(&dm)) {
        Com_DPrintf("...skipping CDS\n");
        ret = DISP_CHANGE_SUCCESSFUL;
    } else {
        Com_DPrintf("...calling CDS: ");
        ret = ChangeDisplaySettings(&dm, CDS_FULLSCREEN);
        if (ret != DISP_CHANGE_SUCCESSFUL) {
            Com_DPrintf("failed with error %ld\n", ret);
            return ret;
        }
        Com_DPrintf("ok\n");
    }

    win.dm = dm;
    win.flags |= QVF_FULLSCREEN;
    Win_SetPosition();
    Win_ModeChanged();
    win.mode_changed = 0;

    return ret;
}

int Win_GetDpiScale(void)
{
    if (win.GetDpiForWindow) {
        int dpi = win.GetDpiForWindow(win.wnd);
        if (dpi) {
            int scale = (dpi + USER_DEFAULT_SCREEN_DPI / 2) / USER_DEFAULT_SCREEN_DPI;
            return Q_clip(scale, 1, 10);
        }
    }
    return 1;
}

/*
============
Win_SetMode
============
*/
void Win_SetMode(void)
{
    // set full screen mode if requested
    if (vid_fullscreen->integer > 0) {
        LONG ret;

        ret = set_fullscreen_mode();
        switch (ret) {
        case DISP_CHANGE_SUCCESSFUL:
            return;
        case DISP_CHANGE_FAILED:
            Com_EPrintf("Display driver failed the %dx%d video mode.\n", win.rc.width, win.rc.height);
            break;
        case DISP_CHANGE_BADMODE:
            Com_EPrintf("Video mode %dx%d is not supported.\n", win.rc.width, win.rc.height);
            break;
        default:
            Com_EPrintf("Video mode %dx%d failed with error %ld.\n",  win.rc.width, win.rc.height, ret);
            break;
        }

        // fall back to windowed mode
        Cvar_Reset(vid_fullscreen);
    }

    ChangeDisplaySettings(NULL, 0);

    // parse vid_geometry specification
    VID_GetGeometry(&win.rc);

    Com_DPrintf("...setting windowed mode: %dx%d%+d%+d\n",
                win.rc.width, win.rc.height, win.rc.x, win.rc.y);

    memset(&win.dm, 0, sizeof(win.dm));
    win.flags &= ~QVF_FULLSCREEN;
    Win_SetPosition();
    Win_ModeChanged();
    win.mode_changed = 0;
}

/*
============
Win_UpdateGamma
============
*/
void Win_UpdateGamma(const byte *table)
{
    WORD v;
    int i;

    if (win.flags & QVF_GAMMARAMP) {
        for (i = 0; i < 256; i++) {
            v = table[i] << 8;
            win.gamma_cust[0][i] = v;
            win.gamma_cust[1][i] = v;
            win.gamma_cust[2][i] = v;
        }

        SetDeviceGammaRamp(win.dc, win.gamma_cust);
    }
}

static void Win_DisableAltTab(void)
{
    if (!win.alttab_disabled) {
        RegisterHotKey(0, 0, MOD_ALT, VK_TAB);
        RegisterHotKey(0, 1, MOD_ALT, VK_RETURN);
        win.alttab_disabled = true;
    }
}

static void Win_EnableAltTab(void)
{
    if (win.alttab_disabled) {
        UnregisterHotKey(0, 0);
        UnregisterHotKey(0, 1);
        win.alttab_disabled = false;
    }
}

static void win_noalttab_changed(cvar_t *self)
{
    if (self->integer) {
        Win_DisableAltTab();
    } else {
        Win_EnableAltTab();
    }
}

static void Win_Activate(WPARAM wParam)
{
    active_t active;

    if (HIWORD(wParam)) {
        // we don't want to act like we're active if we're minimized
        active = ACT_MINIMIZED;
    } else if (LOWORD(wParam)) {
        active = ACT_ACTIVATED;
    } else {
        active = ACT_RESTORED;
    }

    CL_Activate(active);

    if (win_noalttab->integer) {
        if (active == ACT_ACTIVATED) {
            Win_EnableAltTab();
        } else {
            Win_DisableAltTab();
        }
    }

    if (win.flags & QVF_GAMMARAMP) {
        if (active == ACT_ACTIVATED) {
            SetDeviceGammaRamp(win.dc, win.gamma_cust);
        } else {
            SetDeviceGammaRamp(win.dc, win.gamma_orig);
        }
    }

    if (win.flags & QVF_FULLSCREEN) {
        if (active == ACT_ACTIVATED) {
            ShowWindow(win.wnd, SW_RESTORE);
        } else {
            ShowWindow(win.wnd, SW_MINIMIZE);
        }

        if (vid_flip_on_switch->integer) {
            if (active == ACT_ACTIVATED) {
                if (!mode_is_current(&win.dm)) {
                    ChangeDisplaySettings(&win.dm, CDS_FULLSCREEN);
                }
            } else {
                ChangeDisplaySettings(NULL, 0);
            }
        }
    }

    if (active == ACT_ACTIVATED) {
        SetForegroundWindow(win.wnd);
    }
}

static LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    PKBDLLHOOKSTRUCT kb = (PKBDLLHOOKSTRUCT)lParam;
    unsigned key;

    if (nCode != HC_ACTION) {
        goto ignore;
    }

    switch (kb->vkCode) {
    case VK_LWIN:
        key = K_LWINKEY;
        break;
    case VK_RWIN:
        key = K_RWINKEY;
        break;
    default:
        goto ignore;
    }

    switch (wParam) {
    case WM_KEYDOWN:
        Key_Event(key, true, kb->time);
        return TRUE;
    case WM_KEYUP:
        Key_Event(key, false, kb->time);
        return TRUE;
    default:
        break;
    }

ignore:
    return CallNextHookEx(NULL, nCode, wParam, lParam);
}

static void win_disablewinkey_changed(cvar_t *self)
{
    if (self->integer) {
        win.kbdHook = SetWindowsHookEx(WH_KEYBOARD_LL, LowLevelKeyboardProc, hGlobalInstance, 0);
        if (!win.kbdHook) {
            Com_EPrintf("Couldn't set low-level keyboard hook, error %#lX\n", GetLastError());
            Cvar_Set("win_disablewinkey", "0");
        }
    } else {
        if (win.kbdHook) {
            UnhookWindowsHookEx(win.kbdHook);
            win.kbdHook = NULL;
        }
    }
}

static const byte scantokey[2][96] = {
    {
//      0               1           2           3               4           5               6           7
//      8               9           A           B               C           D               E           F
        0,              K_ESCAPE,   '1',        '2',            '3',        '4',            '5',        '6',
        '7',            '8',        '9',        '0',            '-',        '=',            K_BACKSPACE,K_TAB,      // 0
        'q',            'w',        'e',        'r',            't',        'y',            'u',        'i',
        'o',            'p',        '[',        ']',            K_ENTER,    K_LCTRL,        'a',        's',        // 1
        'd',            'f',        'g',        'h',            'j',        'k',            'l',        ';',
        '\'',           '`',        K_LSHIFT,   '\\',           'z',        'x',            'c',        'v',        // 2
        'b',            'n',        'm',        ',',            '.',        '/',            K_RSHIFT,   K_KP_MULTIPLY,
        K_LALT,         K_SPACE,    K_CAPSLOCK, K_F1,           K_F2,       K_F3,           K_F4,       K_F5,       // 3
        K_F6,           K_F7,       K_F8,       K_F9,           K_F10,      K_PAUSE,        K_SCROLLOCK,K_KP_HOME,
        K_KP_UPARROW,   K_KP_PGUP,  K_KP_MINUS, K_KP_LEFTARROW, K_KP_5,     K_KP_RIGHTARROW,K_KP_PLUS,  K_KP_END,   // 4
        K_KP_DOWNARROW, K_KP_PGDN,  K_KP_INS,   K_KP_DEL,       0,          0,              K_102ND,    K_F11,
        K_F12,          0,          0,          0,              0,          0,              0,          0,          // 5
    },
    {
        0,              0,          0,          0,              0,          0,              0,          0,
        0,              0,          0,          0,              0,          0,              0,          0,          // 0
        0,              0,          0,          0,              0,          0,              0,          0,
        0,              0,          0,          0,              K_KP_ENTER, K_RCTRL,        0,          0,          // 1
        0,              0,          0,          0,              0,          0,              0,          0,
        0,              0,          0,          0,              0,          0,              0,          0,          // 2
        0,              0,          0,          0,              0,          K_KP_SLASH,     0,          K_PRINTSCREEN,
        K_RALT,         0,          0,          0,              0,          0,              0,          0,          // 3
        0,              0,          0,          0,              0,          K_NUMLOCK,      0,          K_HOME,
        K_UPARROW,      K_PGUP,     0,          K_LEFTARROW,    0,          K_RIGHTARROW,   0,          K_END,      // 4
        K_DOWNARROW,    K_PGDN,     K_INS,      K_DEL,          0,          0,              0,          0,
        0,              0,          0,          K_LWINKEY,      K_RWINKEY,  K_MENU,         0,          0,          // 5
    }
};

// Map from windows to quake keynums
static void legacy_key_event(WPARAM wParam, LPARAM lParam, bool down)
{
    int scancode = (lParam >> 16) & 255;
    int extended = (lParam >> 24) & 1;
    int result = 0;

    if (scancode < 96)
        result = scantokey[extended][scancode];

    if (!result) {
        Com_DPrintf("%s: unknown %sscancode %d\n",
                    __func__, extended ? "extended " : "", scancode);
        return;
    }

    Key_Event(result, down, win.lastMsgTime);
}

static void mouse_wheel_event(int delta)
{
    UINT lines, key;

    // FIXME: handle WHEEL_DELTA and partial scrolls...
    if (delta > 0) {
        key = K_MWHEELUP;
    } else if (delta < 0) {
        key = K_MWHEELDOWN;
    } else {
        return;
    }

    if (Key_GetDest() & KEY_CONSOLE) {
        SystemParametersInfo(SPI_GETWHEELSCROLLLINES, 0, &lines, 0);
        lines = Q_clip(lines, 1, 9);
    } else {
        lines = 1;
    }

    do {
        Key_Event(key, true, win.lastMsgTime);
        Key_Event(key, false, win.lastMsgTime);
    } while (--lines);
}

static void mouse_hwheel_event(int delta)
{
    UINT key;

    // FIXME: handle WHEEL_DELTA and partial scrolls...
    if (delta > 0) {
        key = K_MWHEELRIGHT;
    } else if (delta < 0) {
        key = K_MWHEELLEFT;
    } else {
        return;
    }

    Key_Event(key, true, win.lastMsgTime);
    Key_Event(key, false, win.lastMsgTime);
}

// returns TRUE if mouse cursor inside client area
static BOOL check_cursor_pos(void)
{
    POINT pt;

    if (win.mouse.grabbed)
        return TRUE;

    if (!GetCursorPos(&pt))
        return FALSE;

    return PtInRect(&win.screen_rc, pt);
}

#define BTN_DN(i)   BIT((i) * 2 + 0)
#define BTN_UP(i)   BIT((i) * 2 + 1)

static void raw_mouse_event(const RAWMOUSE *rm)
{
    int i;

    if (!check_cursor_pos()) {
        // cursor is over non-client area
        // perform just button up actions
        for (i = 0; i < MOUSE_BUTTONS; i++) {
            if (rm->usButtonFlags & BTN_UP(i)) {
                Key_Event(K_MOUSE1 + i, false, win.lastMsgTime);
            }
        }
        return;
    }

    if (rm->usButtonFlags) {
        // perform button actions
        for (i = 0; i < MOUSE_BUTTONS; i++) {
            if (rm->usButtonFlags & BTN_DN(i)) {
                Key_Event(K_MOUSE1 + i, true, win.lastMsgTime);
            }
            if (rm->usButtonFlags & BTN_UP(i)) {
                Key_Event(K_MOUSE1 + i, false, win.lastMsgTime);
            }
        }

        if (rm->usButtonFlags & RI_MOUSE_WHEEL) {
            mouse_wheel_event((short)rm->usButtonData);
        }

        if (rm->usButtonFlags & RI_MOUSE_HWHEEL) {
            mouse_hwheel_event((short)rm->usButtonData);
        }
    }

    if ((rm->usFlags & (MOUSE_MOVE_RELATIVE | MOUSE_MOVE_ABSOLUTE)) == MOUSE_MOVE_RELATIVE) {
        win.mouse.mx += rm->lLastX;
        win.mouse.my += rm->lLastY;
    }
}

static void raw_input_event(HRAWINPUT handle)
{
    BYTE buffer[64];
    UINT len, ret;
    PRAWINPUT ri;

    len = sizeof(buffer);
    ret = GetRawInputData(handle, RID_INPUT, buffer, &len, sizeof(RAWINPUTHEADER));
    if (ret == (UINT)-1) {
        Com_EPrintf("GetRawInputData failed with error %#lx\n", GetLastError());
        return;
    }

    ri = (PRAWINPUT)buffer;
    if (ri->header.dwType == RIM_TYPEMOUSE) {
        raw_mouse_event(&ri->data.mouse);
    }
}

static int get_window_dpi(void)
{
    if (win.GetDpiForWindow) {
        int dpi = win.GetDpiForWindow(win.wnd);
        if (dpi)
            return dpi;
    }
    return USER_DEFAULT_SCREEN_DPI;
}

static void pos_changing_event(HWND wnd, WINDOWPOS *pos)
{
    LONG style;
    RECT rc;
    int dpi;

    if (win.flags & QVF_FULLSCREEN)
        return;

    if (pos->flags & SWP_NOSIZE)
        return;

    style = GetWindowLong(wnd, GWL_STYLE);
    dpi = get_window_dpi();

    // calculate size of non-client area
    rc.left = 0;
    rc.top = 0;
    rc.right = MulDiv(320, dpi, USER_DEFAULT_SCREEN_DPI);
    rc.bottom = MulDiv(240, dpi, USER_DEFAULT_SCREEN_DPI);

    AdjustWindowRect(&rc, style, FALSE);

    // don't allow too small size
    pos->cx = max(pos->cx, rc.right - rc.left);
    pos->cy = max(pos->cy, rc.bottom - rc.top);
}

static void pos_changed_event(HWND wnd, const WINDOWPOS *pos)
{
    RECT rc;

    // get window position
    GetWindowRect(wnd, &rc);
    win.rc.x = rc.left;
    win.rc.y = rc.top;

    // get size of client area
    GetClientRect(wnd, &rc);
    win.rc.width = rc.right - rc.left;
    win.rc.height = rc.bottom - rc.top;

    // get rectangle of client area in screen coordinates
    MapWindowPoints(wnd, NULL, (POINT *)&rc, 2);
    win.screen_rc = rc;
    win.center_x = (rc.right + rc.left) / 2;
    win.center_y = (rc.top + rc.bottom) / 2;

    // set mode_changed flags unless in full screen
    if (win.flags & QVF_FULLSCREEN)
        return;

    if (!pos) {
        win.mode_changed |= MODE_STYLE;
        return;
    }

    if (!(pos->flags & SWP_NOSIZE))
        win.mode_changed |= MODE_SIZE;

    if (!(pos->flags & SWP_NOMOVE))
        win.mode_changed |= MODE_POS;
}

// main window procedure
static LRESULT WINAPI Win_MainWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg) {
    case WM_MOUSEMOVE:
        if (win.mouse.initialized)
            Key_MouseEvent((short)LOWORD(lParam), (short)HIWORD(lParam));
        break;

    case WM_HOTKEY:
        return FALSE;

    case WM_INPUT:
        if (wParam == RIM_INPUT && win.mouse.initialized)
            raw_input_event((HRAWINPUT)lParam);
        break;

    case WM_CLOSE:
        PostQuitMessage(0);
        return FALSE;

    case WM_ACTIVATE:
        Win_Activate(wParam);
        break;

    case WM_WINDOWPOSCHANGING:
        pos_changing_event(hWnd, (WINDOWPOS *)lParam);
        break;

    case WM_WINDOWPOSCHANGED:
        pos_changed_event(hWnd, (WINDOWPOS *)lParam);
        return FALSE;

    case WM_STYLECHANGED:
    case WM_THEMECHANGED:
        pos_changed_event(hWnd, NULL);
        break;

    case WM_SYSCOMMAND:
        switch (wParam & 0xFFF0) {
        case SC_SCREENSAVE:
            return FALSE;
        case SC_MAXIMIZE:
            if (win_noborder->integer)
                break; // default maximize
            if (!vid_fullscreen->integer)
                VID_ToggleFullscreen();
            return FALSE;
        }
        break;

    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
        legacy_key_event(wParam, lParam, true);
        return FALSE;

    case WM_KEYUP:
    case WM_SYSKEYUP:
        legacy_key_event(wParam, lParam, false);
        return FALSE;

    case WM_SYSCHAR:
    case WM_CHAR:
        return FALSE;

    case WM_ERASEBKGND:
        if (win.flags & QVF_FULLSCREEN)
            return FALSE;
        break;

    case WM_DPICHANGED:
        win.mode_changed |= MODE_SIZE;
        break;

    default:
        break;
    }

    // pass all unhandled messages to DefWindowProc
    return DefWindowProc(hWnd, uMsg, wParam, lParam);
}

/*
============
Win_PumpEvents
============
*/
void Win_PumpEvents(void)
{
    MSG        msg;

    win.lastMsgTime = Sys_Milliseconds();
    while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) {
            Com_Quit(NULL, ERR_DISCONNECT);
            break;
        }
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    if (win.mode_changed) {
        if (win.mode_changed & MODE_REPOSITION) {
            Win_SetPosition();
        }
        if (win.mode_changed & (MODE_SIZE | MODE_POS | MODE_STYLE)) {
            VID_SetGeometry(&win.rc);
            if (win.mouse.grabbed) {
                Win_ClipCursor();
            }
        }
        if (win.mode_changed & MODE_SIZE) {
            Win_ModeChanged();
        }
        win.mode_changed = 0;
    }
}

static void win_style_changed(cvar_t *self)
{
    if (win.wnd && !(win.flags & QVF_FULLSCREEN)) {
        win.mode_changed |= MODE_REPOSITION;
    }
}

/*
============
Win_Init
============
*/
void Win_Init(void)
{
    WNDCLASSEXA wc;

    // register variables
    vid_flip_on_switch = Cvar_Get("vid_flip_on_switch", "0", 0);
    vid_hwgamma = Cvar_Get("vid_hwgamma", "0", CVAR_REFRESH);
    win_noalttab = Cvar_Get("win_noalttab", "0", CVAR_ARCHIVE);
    win_noalttab->changed = win_noalttab_changed;
    win_disablewinkey = Cvar_Get("win_disablewinkey", "0", 0);
    win_disablewinkey->changed = win_disablewinkey_changed;
    win_noresize = Cvar_Get("win_noresize", "0", 0);
    win_noresize->changed = win_style_changed;
    win_notitle = Cvar_Get("win_notitle", "0", 0);
    win_notitle->changed = win_style_changed;
    win_alwaysontop = Cvar_Get("win_alwaysontop", "0", 0);
    win_alwaysontop->changed = win_style_changed;
    win_noborder = Cvar_Get("win_noborder", "0", 0);
    win_noborder->changed = win_style_changed;

    win_disablewinkey_changed(win_disablewinkey);

    win.GetDpiForWindow = (PVOID)GetProcAddress(GetModuleHandle("user32"), "GetDpiForWindow");

    // register the frame class
    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = Win_MainWndProc;
    wc.hInstance = hGlobalInstance;
    wc.hIcon = LoadImage(hGlobalInstance, MAKEINTRESOURCE(IDI_APP),
                         IMAGE_ICON, 32, 32, LR_CREATEDIBSECTION);
    wc.hIconSm = LoadImage(hGlobalInstance, MAKEINTRESOURCE(IDI_APP),
                           IMAGE_ICON, 16, 16, LR_CREATEDIBSECTION);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = WINDOW_CLASS_NAME;

    if (!RegisterClassExA(&wc)) {
        Com_Error(ERR_FATAL, "Couldn't register main window class");
    }

    // create the window
    win.wnd = CreateWindowA(WINDOW_CLASS_NAME, PRODUCT, 0, 0, 0, 0, 0, NULL,
                            NULL, hGlobalInstance, NULL);
    if (!win.wnd) {
        Com_Error(ERR_FATAL, "Couldn't create main window");
    }

    win.dc = GetDC(win.wnd);
    if (!win.dc) {
        Com_Error(ERR_FATAL, "Couldn't get DC of the main window");
    }

    // init gamma ramp
    if (vid_hwgamma->integer) {
        if (GetDeviceGammaRamp(win.dc, win.gamma_orig)) {
            Com_DPrintf("...enabling hardware gamma\n");
            win.flags |= QVF_GAMMARAMP;
            memcpy(win.gamma_cust, win.gamma_orig, sizeof(win.gamma_cust));
        } else {
            Com_DPrintf("...hardware gamma not supported\n");
            Cvar_Set("vid_hwgamma", "0");
        }
    }
}

/*
============
Win_Shutdown
============
*/
void Win_Shutdown(void)
{
    if (win.flags & QVF_GAMMARAMP) {
        SetDeviceGammaRamp(win.dc, win.gamma_orig);
    }

    if (win.dc) {
        ReleaseDC(win.wnd, win.dc);
    }

    if (win.wnd) {
        DestroyWindow(win.wnd);
    }

    UnregisterClassA(WINDOW_CLASS_NAME, hGlobalInstance);

    if (win.kbdHook) {
        UnhookWindowsHookEx(win.kbdHook);
    }

    if (win.flags & QVF_FULLSCREEN) {
        ChangeDisplaySettings(NULL, 0);
    }

    memset(&win, 0, sizeof(win));
}

/*
===============================================================================

MOUSE

===============================================================================
*/

// Called when the window gains focus or changes in some way
static void Win_ClipCursor(void)
{
    SetCursorPos(win.center_x, win.center_y);
    ClipCursor(&win.screen_rc);
}

// Called when the window gains focus
static void Win_AcquireMouse(void)
{
    Win_ClipCursor();
    SetCapture(win.wnd);

    while (ShowCursor(FALSE) >= 0)
        ;
}

// Called when the window loses focus
static void Win_DeAcquireMouse(void)
{
    SetCursorPos(win.center_x, win.center_y);

    ClipCursor(NULL);
    ReleaseCapture();

    while (ShowCursor(TRUE) < 0)
        ;
}

bool Win_GetMouseMotion(int *dx, int *dy)
{
    if (!win.mouse.grabbed) {
        return false;
    }

    *dx = win.mouse.mx;
    *dy = win.mouse.my;
    win.mouse.mx = 0;
    win.mouse.my = 0;
    return true;
}

static BOOL register_raw_mouse(bool enable)
{
    RAWINPUTDEVICE rid = {
        .usUsagePage = HID_USAGE_PAGE_GENERIC,
        .usUsage = HID_USAGE_GENERIC_MOUSE,
    };

    if (enable)
        rid.hwndTarget = win.wnd;
    else
        rid.dwFlags = RIDEV_REMOVE;

    return RegisterRawInputDevices(&rid, 1, sizeof(rid));
}

void Win_ShutdownMouse(void)
{
    if (!win.mouse.initialized) {
        return;
    }

    if (win.mouse.grabbed) {
        Win_DeAcquireMouse();
    }

    register_raw_mouse(false);

    memset(&win.mouse, 0, sizeof(win.mouse));
}

bool Win_InitMouse(void)
{
    if (!win.wnd) {
        return false;
    }

    if (!register_raw_mouse(true)) {
        Com_EPrintf("RegisterRawInputDevices failed with error %#lx\n", GetLastError());
        return false;
    }

    Com_Printf("Raw mouse initialized.\n");
    win.mouse.initialized = true;
    return true;
}

// Called when the main window gains or loses focus.
void Win_GrabMouse(bool grab)
{
    if (!win.mouse.initialized) {
        return;
    }

    if (win.mouse.grabbed == grab) {
        win.mouse.mx = 0;
        win.mouse.my = 0;
        return;
    }

    if (grab) {
        Win_AcquireMouse();
    } else {
        Win_DeAcquireMouse();
    }

    win.mouse.grabbed = grab;
    win.mouse.mx = 0;
    win.mouse.my = 0;
}

void Win_WarpMouse(int x, int y)
{
    SetCursorPos(win.screen_rc.left + x, win.screen_rc.top + y);
}

/*
================
Win_GetClipboardData
================
*/
char *Win_GetClipboardData(void)
{
    HANDLE clipdata;
    WCHAR *cliptext;
    char *data;

    if (!OpenClipboard(NULL)) {
        Com_DPrintf("Couldn't open clipboard.\n");
        return NULL;
    }

    data = NULL;
    if (!(clipdata = GetClipboardData(CF_UNICODETEXT)))
        goto fail;
    if (!(cliptext = GlobalLock(clipdata)))
        goto fail;

    int len = WideCharToMultiByte(CP_UTF8, 0, cliptext, -1, NULL, 0, NULL, NULL);
    if (len > 0) {
        char *text = Z_Malloc(len);
        if (WideCharToMultiByte(CP_UTF8, 0, cliptext, -1, text, len, NULL, NULL) == len)
            data = UTF8_TranslitString(text);
        Z_Free(text);
    }
    GlobalUnlock(clipdata);

fail:
    CloseClipboard();
    return data;
}

/*
================
Win_SetClipboardData
================
*/
void Win_SetClipboardData(const char *data)
{
    HANDLE clipdata;
    char *cliptext;
    size_t length;

    if (!data || !*data) {
        return;
    }

    if (!OpenClipboard(NULL)) {
        Com_DPrintf("Couldn't open clipboard.\n");
        return;
    }

    EmptyClipboard();

    length = strlen(data) + 1;
    if ((clipdata = GlobalAlloc(GMEM_MOVEABLE | GMEM_DDESHARE, length)) != NULL) {
        if ((cliptext = GlobalLock(clipdata)) != NULL) {
            memcpy(cliptext, data, length);
            GlobalUnlock(clipdata);
            SetClipboardData(CF_TEXT, clipdata);
        }
    }

    CloseClipboard();
}
