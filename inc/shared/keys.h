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

#pragma once

//
// these are the key numbers that should be passed to Key_Event
//
typedef enum {
    K_BACKSPACE    = 8,
    K_TAB          = 9,
    K_ENTER        = 13,
    K_PAUSE        = 19,
    K_ESCAPE       = 27,
    K_SPACE        = 32,
    K_DEL          = 127,

// normal keys should be passed as lowercased ascii
    K_ASCIIFIRST   = 32,
    K_ASCIILAST    = 127,

    K_UPARROW,
    K_DOWNARROW,
    K_LEFTARROW,
    K_RIGHTARROW,

    K_LALT,
    K_RALT,
    K_LCTRL,
    K_RCTRL,
    K_LSHIFT,
    K_RSHIFT,
    K_F1,
    K_F2,
    K_F3,
    K_F4,
    K_F5,
    K_F6,
    K_F7,
    K_F8,
    K_F9,
    K_F10,
    K_F11,
    K_F12,

    K_INS,
    K_PGDN,
    K_PGUP,
    K_HOME,
    K_END,

    K_102ND,

    K_NUMLOCK,
    K_CAPSLOCK,
    K_SCROLLOCK,
    K_LWINKEY,
    K_RWINKEY,
    K_MENU,
    K_PRINTSCREEN,

    K_KP_HOME,
    K_KP_UPARROW,
    K_KP_PGUP,
    K_KP_LEFTARROW,
    K_KP_5,
    K_KP_RIGHTARROW,
    K_KP_END,
    K_KP_DOWNARROW,
    K_KP_PGDN,
    K_KP_ENTER,
    K_KP_INS,
    K_KP_DEL,
    K_KP_SLASH,
    K_KP_MINUS,
    K_KP_PLUS,
    K_KP_MULTIPLY,

// mouse buttons generate virtual keys
    K_MOUSE1 = 200,
    K_MOUSE2,
    K_MOUSE3,
    K_MOUSE4,
    K_MOUSE5,
    K_MOUSE6,
    K_MOUSE7,
    K_MOUSE8,

// mouse wheel generates virtual keys
    K_MWHEELDOWN = 210,
    K_MWHEELUP,
    K_MWHEELRIGHT,
    K_MWHEELLEFT,

// modifiers don't generate events, but can be tested with Key_IsDown
    K_ALT = 256,
    K_CTRL,
    K_SHIFT,
} keyevent_t;

typedef enum {
    KEY_NONE    = 0,
    KEY_CONSOLE = BIT(0),
    KEY_MESSAGE = BIT(1),
    KEY_MENU    = BIT(2),
    KEY_GAME    = BIT(3),
} keydest_t;
