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
// cl_scrn.c -- master for refresh, status bar, console, chat, notify, etc

#include "cg_local.h"

#define STAT_PICS       11
#define STAT_MINUS      (STAT_PICS - 1)  // num frame for '-' stats digit

static struct {
    refcfg_t    config;

    qhandle_t   crosshair_pic;
    int         crosshair_width, crosshair_height;
    color_t     crosshair_color;

    qhandle_t   hit_marker_pic;
    int         hit_marker_width, hit_marker_height;

    qhandle_t   pause_pic;

    qhandle_t   loading_pic;
    bool        draw_loading;

    qhandle_t   sb_pics[2][STAT_PICS];
    qhandle_t   inven_pic;
    qhandle_t   field_pic;

    qhandle_t   backtile_pic;

    qhandle_t   net_pic;
    qhandle_t   font_pic;

    int         hud_width, hud_height;
    float       hud_scale;
} scr;

static vm_cvar_t scr_viewsize;
static vm_cvar_t scr_centertime;
static vm_cvar_t scr_printspeed;
static vm_cvar_t scr_showpause;

static vm_cvar_t scr_draw2d;
static vm_cvar_t scr_lag_x;
static vm_cvar_t scr_lag_y;
static vm_cvar_t scr_lag_draw;
static vm_cvar_t scr_lag_min;
static vm_cvar_t scr_lag_max;
static vm_cvar_t scr_alpha;

static vm_cvar_t scr_demobar;
static vm_cvar_t scr_font;
static vm_cvar_t scr_scale;

static vm_cvar_t scr_crosshair;

static vm_cvar_t scr_chathud;
static vm_cvar_t scr_chathud_lines;
static vm_cvar_t scr_chathud_time;
static vm_cvar_t scr_chathud_x;
static vm_cvar_t scr_chathud_y;

static vm_cvar_t ch_health;
static vm_cvar_t ch_red;
static vm_cvar_t ch_green;
static vm_cvar_t ch_blue;
static vm_cvar_t ch_alpha;

static vm_cvar_t ch_scale;
static vm_cvar_t ch_x;
static vm_cvar_t ch_y;

static vm_cvar_t scr_hit_marker_time;

vrect_t     scr_vrect;      // position of render window on screen

/*
===============================================================================

UTILS

===============================================================================
*/

#define SCR_DrawString(x, y, flags, string) \
    SCR_DrawStringEx(x, y, flags, MAX_STRING_CHARS, string, scr.font_pic)

/*
==============
SCR_DrawStringEx
==============
*/
int SCR_DrawStringEx(int x, int y, int flags, size_t maxlen,
                     const char *s, qhandle_t font)
{
    size_t len = strlen(s);

    if (len > maxlen) {
        len = maxlen;
    }

    if ((flags & UI_CENTER) == UI_CENTER) {
        x -= len * CONCHAR_WIDTH / 2;
    } else if (flags & UI_RIGHT) {
        x -= len * CONCHAR_WIDTH;
    }

    return trap_R_DrawString(x, y, flags, maxlen, s, font);
}

/*
==============
SCR_DrawStringMulti
==============
*/
void SCR_DrawStringMulti(int x, int y, int flags, size_t maxlen,
                         const char *s, qhandle_t font)
{
    char    *p;
    size_t  len;
    int     last_x = x;
    int     last_y = y;

    while (*s && maxlen) {
        p = strchr(s, '\n');
        if (!p) {
            last_x = SCR_DrawStringEx(x, y, flags, maxlen, s, font);
            last_y = y;
            break;
        }

        len = min(p - s, maxlen);
        last_x = SCR_DrawStringEx(x, y, flags, len, s, font);
        last_y = y;
        maxlen -= len;

        y += CONCHAR_HEIGHT;
        s = p + 1;
    }

    if (flags & UI_DRAWCURSOR && cgs.realtime & BIT(8))
        trap_R_DrawChar(last_x, last_y, flags, 11, font);
}


/*
=================
SCR_FadeAlpha
=================
*/
float SCR_FadeAlpha(unsigned startTime, unsigned visTime, unsigned fadeTime)
{
    float alpha;
    unsigned timeLeft, delta = cgs.realtime - startTime;

    if (delta >= visTime) {
        return 0;
    }

    if (fadeTime > visTime) {
        fadeTime = visTime;
    }

    alpha = 1;
    timeLeft = visTime - delta;
    if (timeLeft < fadeTime) {
        alpha = (float)timeLeft / fadeTime;
    }

    return alpha;
}

/*
===============================================================================

DEMO BAR

===============================================================================
*/

static void SCR_DrawDemo(void)
{
    cg_demo_info_t info;
    char buffer[16];
    int x, w, h;
    size_t len;

    if (!scr_demobar.integer)
        return;
    if (!cgs.demoplayback)
        return;
    if (!trap_GetDemoInfo(&info))
        return;

    w = Q_rint(scr.hud_width * info.progress);
    h = Q_rint(CONCHAR_HEIGHT / scr.hud_scale);

    scr.hud_height -= h;

    trap_R_DrawFill8(0, scr.hud_height, w, h, 4);
    trap_R_DrawFill8(w, scr.hud_height, scr.hud_width - w, h, 0);

    trap_R_SetScale(scr.hud_scale);

    w = Q_rint(scr.hud_width * scr.hud_scale);
    h = Q_rint(scr.hud_height * scr.hud_scale);

    len = Q_scnprintf(buffer, sizeof(buffer), "%.f%%", info.progress * 100);
    x = (w - len * CONCHAR_WIDTH) / 2;
    trap_R_DrawString(x, h, 0, MAX_STRING_CHARS, buffer, scr.font_pic);

    if (scr_demobar.integer > 1) {
        int sec = info.framenum / BASE_FRAMERATE;
        int sub = info.framenum % BASE_FRAMERATE;
        int min = sec / 60; sec %= 60;

        Q_snprintf(buffer, sizeof(buffer), "%d:%02d.%d", min, sec, sub);
        trap_R_DrawString(0, h, 0, MAX_STRING_CHARS, buffer, scr.font_pic);
    }

    if (sv_paused.integer && cl_paused.integer && scr_showpause.integer == 2)
        SCR_DrawString(w, h, UI_RIGHT, "[PAUSED]");

    trap_R_SetScale(1.0f);
}

/*
===============================================================================

CENTER PRINTING

===============================================================================
*/

#define MAX_CENTERPRINTS    4

typedef struct {
    char        string[MAX_STRING_CHARS - 8];
    uint32_t    start;
    uint16_t    lines;
    uint16_t    typewrite;  // msec to typewrite (0 if instant)
} centerprint_t;

static centerprint_t    scr_centerprints[MAX_CENTERPRINTS];
static unsigned         scr_centerhead, scr_centertail;

void SCR_ClearCenterPrints(void)
{
    memset(scr_centerprints, 0, sizeof(scr_centerprints));
    scr_centerhead = scr_centertail = 0;
}

/*
==============
SCR_CenterPrint

Called for important messages that should stay in the center of the screen
for a few moments
==============
*/
void SCR_CenterPrint(const char *str, bool typewrite)
{
    centerprint_t *cp;
    const char *s;

    // refresh duplicate message
    cp = &scr_centerprints[(scr_centerhead - 1) & (MAX_CENTERPRINTS - 1)];
    if (!strcmp(cp->string, str)) {
        if (cp->start)
            cp->start = cgs.realtime;
        if (scr_centertail == scr_centerhead)
            scr_centertail--;
        return;
    }

    cp = &scr_centerprints[scr_centerhead & (MAX_CENTERPRINTS - 1)];
    Q_strlcpy(cp->string, str, sizeof(cp->string));

    // count the number of lines for centering
    cp->lines = 1;
    s = cp->string;
    while (*s) {
        if (*s == '\n')
            cp->lines++;
        s++;
    }

    cp->start = 0;  // not yet displayed
    cp->typewrite = 0;

    // for typewritten strings set minimum display time,
    // but no longer than 30 sec
    if (typewrite && scr_printspeed.value > 0) {
        size_t nb_chars = strlen(cp->string) - cp->lines + 2;
        cp->typewrite = min(nb_chars * 1000 / scr_printspeed.value + 300, 30000);
    }

    // echo it to the console
    Com_LPrintf(PRINT_ALL | PRINT_SKIPNOTIFY, "%s\n", cp->string);

    scr_centerhead++;
    if (scr_centerhead - scr_centertail > MAX_CENTERPRINTS)
        scr_centertail++;
}

static void SCR_DrawCenterString(void)
{
    centerprint_t *cp;
    int y, flags;
    float alpha;
    size_t maxlen;

    if (scr_centertime.modified) {
        if (scr_centertime.value > 0)
            scr_centertime.integer = 1000 * Q_clipf(scr_centertime.value, 1.0f, 30.0f);
        else
            scr_centertime.integer = 0;
        scr_centertime.modified = false;
    }

    if (!scr_centertime.integer) {
        scr_centertail = scr_centerhead;
        return;
    }

    while (1) {
        if (scr_centertail == scr_centerhead)
            return;
        cp = &scr_centerprints[scr_centertail & (MAX_CENTERPRINTS - 1)];
        if (!cp->start)
            cp->start = cgs.realtime;
        alpha = SCR_FadeAlpha(cp->start, scr_centertime.integer + cp->typewrite, 300);
        if (alpha > 0)
            break;
        scr_centertail++;
    }

    trap_R_SetAlpha(alpha * scr_alpha.value);

    y = scr.hud_height / 4 - cp->lines * CONCHAR_HEIGHT / 2;
    flags = UI_CENTER;

    if (cp->typewrite) {
        maxlen = scr_printspeed.value * 0.001f * (cgs.realtime - cp->start);
        flags |= UI_DROPSHADOW | UI_DRAWCURSOR;
    } else {
        maxlen = MAX_STRING_CHARS;
    }

    SCR_DrawStringMulti(scr.hud_width / 2, y, flags,
                        maxlen, cp->string, scr.font_pic);

    trap_R_SetAlpha(scr_alpha.value);
}

/*
===============================================================================

LAGOMETER

===============================================================================
*/

#define LAG_WIDTH   48
#define LAG_HEIGHT  48

#define LAG_WARN_BIT    BIT(30)
#define LAG_CRIT_BIT    BIT(31)

#define LAG_BASE    0xD5
#define LAG_WARN    0xDC
#define LAG_CRIT    0xF2

static struct {
    unsigned samples[LAG_WIDTH];
    unsigned head;
} lag;

void SCR_LagClear(void)
{
    lag.head = 0;
}

void SCR_LagSample(const cg_server_frame_t *frame)
{
#if 0
    int i = cgs.netchan.incoming_acknowledged & CMD_MASK;
    client_history_t *h = &cg.history[i];
    unsigned ping;

    h->rcvd = cgs.realtime;
    if (!h->cmdNumber || h->rcvd < h->sent) {
        return;
    }

    ping = h->rcvd - h->sent;
    for (i = 0; i < cgs.netchan.dropped; i++) {
        lag.samples[lag.head % LAG_WIDTH] = ping | LAG_CRIT_BIT;
        lag.head++;
    }

    if (cg.frameflags & FF_SUPPRESSED) {
        ping |= LAG_WARN_BIT;
    }
    lag.samples[lag.head % LAG_WIDTH] = ping;
    lag.head++;
#endif
}

static void SCR_LagDraw(int x, int y)
{
    int i, j, v, c, v_min, v_max, v_range;

    v_min = Q_clip(scr_lag_min.integer, 0, LAG_HEIGHT * 10);
    v_max = Q_clip(scr_lag_max.integer, 0, LAG_HEIGHT * 10);

    v_range = v_max - v_min;
    if (v_range < 1)
        return;

    for (i = 0; i < LAG_WIDTH; i++) {
        j = lag.head - i - 1;
        if (j < 0) {
            break;
        }

        v = lag.samples[j % LAG_WIDTH];

        if (v & LAG_CRIT_BIT) {
            c = LAG_CRIT;
        } else if (v & LAG_WARN_BIT) {
            c = LAG_WARN;
        } else {
            c = LAG_BASE;
        }

        v &= ~(LAG_WARN_BIT | LAG_CRIT_BIT);
        v = Q_clip((v - v_min) * LAG_HEIGHT / v_range, 0, LAG_HEIGHT);

        trap_R_DrawFill8(x + LAG_WIDTH - i - 1, y + LAG_HEIGHT - v, 1, v, c);
    }
}

static void SCR_DrawNet(void)
{
    int x = scr_lag_x.integer;
    int y = scr_lag_y.integer;

    if (x < 0) {
        x += scr.hud_width - LAG_WIDTH + 1;
    }
    if (y < 0) {
        y += scr.hud_height - LAG_HEIGHT + 1;
    }

    // draw ping graph
    if (scr_lag_draw.integer) {
        if (scr_lag_draw.integer > 1) {
            trap_R_DrawFill8(x, y, LAG_WIDTH, LAG_HEIGHT, 4);
        }
        SCR_LagDraw(x, y);
    }

    // draw phone jack
    unsigned ack, cur;
    trap_GetUsercmdNumber(&ack, &cur);
    if (cur - ack > CMD_BACKUP) {
        if ((cgs.realtime >> 8) & 3) {
            trap_R_DrawStretchPic(x, y, LAG_WIDTH, LAG_HEIGHT, scr.net_pic);
        }
    }
}

/*
===============================================================================

CHAT HUD

===============================================================================
*/

#define MAX_CHAT_TEXT       150
#define MAX_CHAT_LINES      32
#define CHAT_LINE_MASK      (MAX_CHAT_LINES - 1)

typedef struct {
    char        text[MAX_CHAT_TEXT];
    unsigned    time;
} chatline_t;

static chatline_t   scr_chatlines[MAX_CHAT_LINES];
static unsigned     scr_chathead;

void SCR_ClearChatHUD_f(void)
{
    memset(scr_chatlines, 0, sizeof(scr_chatlines));
    scr_chathead = 0;
}

void SCR_AddToChatHUD(const char *text)
{
    chatline_t *line;
    char *p;

    line = &scr_chatlines[scr_chathead++ & CHAT_LINE_MASK];
    Q_strlcpy(line->text, text, sizeof(line->text));
    line->time = cgs.realtime;

    p = strrchr(line->text, '\n');
    if (p)
        *p = 0;
}

static void SCR_DrawChatHUD(void)
{
    int x, y, i, lines, flags, step;
    float alpha;
    chatline_t *line;

    if (scr_chathud.integer == 0)
        return;

    x = scr_chathud_x.integer;
    y = scr_chathud_y.integer;

    if (scr_chathud.integer == 2)
        flags = UI_ALTCOLOR;
    else
        flags = 0;

    if (x < 0) {
        x += scr.hud_width + 1;
        flags |= UI_RIGHT;
    } else {
        flags |= UI_LEFT;
    }

    if (y < 0) {
        y += scr.hud_height - CONCHAR_HEIGHT + 1;
        step = -CONCHAR_HEIGHT;
    } else {
        step = CONCHAR_HEIGHT;
    }

    lines = scr_chathud_lines.integer;
    if (lines > scr_chathead)
        lines = scr_chathead;

    for (i = 0; i < lines; i++) {
        line = &scr_chatlines[(scr_chathead - i - 1) & CHAT_LINE_MASK];

        if (scr_chathud_time.integer) {
            alpha = SCR_FadeAlpha(line->time, scr_chathud_time.integer, 1000);
            if (!alpha)
                break;

            trap_R_SetAlpha(alpha * scr_alpha.value);
            SCR_DrawString(x, y, flags, line->text);
            trap_R_SetAlpha(scr_alpha.value);
        } else {
            SCR_DrawString(x, y, flags, line->text);
        }

        y += step;
    }
}

//============================================================================

static void ch_scale_changed(void)
{
    int w, h;
    float scale;

    scale = Q_clipf(ch_scale.value, 0.1f, 9.0f);

    // prescale
    trap_R_GetPicSize(&w, &h, scr.crosshair_pic);
    scr.crosshair_width = Q_rint(w * scale);
    scr.crosshair_height = Q_rint(h * scale);

    trap_R_GetPicSize(&w, &h, scr.hit_marker_pic);
    scr.hit_marker_width = Q_rint(w * scale);
    scr.hit_marker_height = Q_rint(h * scale);
}

static void ch_color_changed(void)
{
    if (ch_health.integer) {
        SCR_SetCrosshairColor();
    } else {
        scr.crosshair_color.u8[0] = Q_clip_uint8(ch_red.value * 255);
        scr.crosshair_color.u8[1] = Q_clip_uint8(ch_green.value * 255);
        scr.crosshair_color.u8[2] = Q_clip_uint8(ch_blue.value * 255);
    }
    scr.crosshair_color.u8[3] = Q_clip_uint8(ch_alpha.value * 255);
}

static void scr_crosshair_changed(void)
{
    if (scr_crosshair.integer > 0) {
        scr.crosshair_pic = trap_R_RegisterPic(va("ch%i", scr_crosshair.integer));
        ch_scale_changed();
    } else {
        scr.crosshair_pic = 0;
    }
}

void SCR_SetCrosshairColor(void)
{
    int health;

    if (!ch_health.integer) {
        return;
    }

    health = cg.frame->ps.stats[STAT_HEALTH];
    if (health <= 0) {
        VectorSet(scr.crosshair_color.u8, 0, 0, 0);
        return;
    }

    // red
    scr.crosshair_color.u8[0] = 255;

    // green
    if (health >= 66) {
        scr.crosshair_color.u8[1] = 255;
    } else if (health < 33) {
        scr.crosshair_color.u8[1] = 0;
    } else {
        scr.crosshair_color.u8[1] = (255 * (health - 33)) / 33;
    }

    // blue
    if (health >= 99) {
        scr.crosshair_color.u8[2] = 255;
    } else if (health < 66) {
        scr.crosshair_color.u8[2] = 0;
    } else {
        scr.crosshair_color.u8[2] = (255 * (health - 66)) / 33;
    }
}

static void scr_font_changed(void)
{
    scr.font_pic = trap_R_RegisterFont(scr_font.string);
    if (!scr.font_pic)
        scr.font_pic = trap_R_RegisterFont("conchars");
}

static void scr_scale_changed(void)
{
    if (scr_scale.value >= 1.0f)
        scr.hud_scale = 1.0f / min(scr_scale.value, 10.0f);
    else
        scr.hud_scale = trap_R_GetAutoScale();
}

static void SCR_UpdateCvars(void)
{
    if (scr_scale.modified) {
        scr_scale_changed();
        scr_scale.modified = false;
    }

    if (scr_font.modified) {
        scr_font_changed();
        scr_font.modified = false;
    }

    if (ch_scale.modified) {
        ch_scale_changed();
        ch_scale.modified = false;
    }

    if (ch_red.modified || ch_green.modified || ch_blue.modified || ch_alpha.modified) {
        ch_color_changed();
        ch_red.modified = ch_green.modified = ch_blue.modified = ch_alpha.modified = false;
    }

    if (scr_crosshair.modified) {
        scr_crosshair_changed();
        scr_crosshair.modified = false;
    }
}

/*
==================
SCR_RegisterMedia
==================
*/
void SCR_RegisterMedia(void)
{
    int     i;

    for (i = 0; i < STAT_MINUS; i++)
        scr.sb_pics[0][i] = trap_R_RegisterPic(va("num_%d", i));
    scr.sb_pics[0][i] = trap_R_RegisterPic("num_minus");

    for (i = 0; i < STAT_MINUS; i++)
        scr.sb_pics[1][i] = trap_R_RegisterPic(va("anum_%d", i));
    scr.sb_pics[1][i] = trap_R_RegisterPic("anum_minus");

    scr.inven_pic = trap_R_RegisterPic("inventory");
    scr.field_pic = trap_R_RegisterPic("field_3");
    scr.backtile_pic = trap_R_RegisterPic("*backtile");
    scr.pause_pic = trap_R_RegisterPic("pause");
    scr.loading_pic = trap_R_RegisterPic("loading");
    scr.net_pic = trap_R_RegisterPic("net");
    scr.hit_marker_pic = trap_R_RegisterPic("marker");
}

static const vm_cvar_reg_t scr_cvars[] = {
    { &scr_viewsize, "viewsize", "100", CVAR_ARCHIVE },
    { &scr_showpause, "scr_showpause", "1", 0 },
    { &scr_centertime, "scr_centertime", "2.5", 0 },
    { &scr_printspeed, "scr_printspeed", "16", 0 },
    { &scr_demobar, "scr_demobar", "1", 0 },
    { &scr_font, "scr_font", "conchars", 0 },
    { &scr_scale, "scr_scale", "0", 0 },
    { &scr_crosshair, "crosshair", "0", CVAR_ARCHIVE },

    { &scr_chathud, "scr_chathud", "0", 0 },
    { &scr_chathud_lines, "scr_chathud_lines", "4", 0 },
    { &scr_chathud_time, "scr_chathud_time", "0", 0 },
    { &scr_chathud_x, "scr_chathud_x", "8", 0 },
    { &scr_chathud_y, "scr_chathud_y", "-64", 0 },

    { &ch_health, "ch_health", "0", 0 },
    { &ch_red, "ch_red", "1", 0 },
    { &ch_green, "ch_green", "1", 0 },
    { &ch_blue, "ch_blue", "1", 0 },
    { &ch_alpha, "ch_alpha", "1", 0 },

    { &ch_scale, "ch_scale", "1", 0 },
    { &ch_x, "ch_x", "0", 0 },
    { &ch_y, "ch_y", "0", 0 },

    { &scr_draw2d, "scr_draw2d", "2", 0 },
    { &scr_lag_x, "scr_lag_x", "-1", 0 },
    { &scr_lag_y, "scr_lag_y", "-1", 0 },
    { &scr_lag_draw, "scr_lag_draw", "0", 0 },
    { &scr_lag_min, "scr_lag_min", "0", 0 },
    { &scr_lag_max, "scr_lag_max", "200", 0 },
    { &scr_alpha, "scr_alpha", "1", 0 },

    { &scr_hit_marker_time, "scr_hit_marker_time", "500", 0 },
};

/*
==================
SCR_Init
==================
*/
void SCR_Init(void)
{
    for (int i = 0; i < q_countof(scr_cvars); i++) {
        const vm_cvar_reg_t *reg = &scr_cvars[i];
        trap_Cvar_Register(reg->var, reg->name, reg->default_string, reg->flags);
    }

    SCR_UpdateCvars();

    SCR_RegisterMedia();

    CG_ModeChanged();
}

//=============================================================================

// Sets scr_vrect, the coordinates of the rendered window
static void SCR_CalcVrect(void)
{
    int     size;

    // bound viewsize
    size = Q_clip(scr_viewsize.integer, 40, 100);

    scr_vrect.width = scr.hud_width * size / 100;
    scr_vrect.height = scr.hud_height * size / 100;

    scr_vrect.x = (scr.hud_width - scr_vrect.width) / 2;
    scr_vrect.y = (scr.hud_height - scr_vrect.height) / 2;
}

// Clear any parts of the tiled background that were drawn on last frame
static void SCR_TileClear(void)
{
    int top, bottom, left, right;

    if (scr_viewsize.integer == 100)
        return;     // full screen rendering

    top = scr_vrect.y;
    bottom = top + scr_vrect.height;
    left = scr_vrect.x;
    right = left + scr_vrect.width;

    // clear above view screen
    trap_R_TileClear(0, 0, scr.hud_width, top, scr.backtile_pic);

    // clear below view screen
    trap_R_TileClear(0, bottom, scr.hud_width,
                     scr.hud_height - bottom, scr.backtile_pic);

    // clear left of view screen
    trap_R_TileClear(0, top, left, scr_vrect.height, scr.backtile_pic);

    // clear right of view screen
    trap_R_TileClear(right, top, scr.hud_width - right,
                     scr_vrect.height, scr.backtile_pic);
}

/*
===============================================================================

STAT PROGRAMS

===============================================================================
*/

#define ICON_WIDTH  24
#define ICON_HEIGHT 24
#define DIGIT_WIDTH 16
#define ICON_SPACE  8

#define HUD_DrawString(x, y, string) \
    trap_R_DrawString(x, y, 0, MAX_STRING_CHARS, string, scr.font_pic)

#define HUD_DrawAltString(x, y, string) \
    trap_R_DrawString(x, y, UI_XORCOLOR, MAX_STRING_CHARS, string, scr.font_pic)

#define HUD_DrawCenterString(x, y, string) \
    SCR_DrawStringMulti(x, y, UI_CENTER, MAX_STRING_CHARS, string, scr.font_pic)

#define HUD_DrawAltCenterString(x, y, string) \
    SCR_DrawStringMulti(x, y, UI_CENTER | UI_XORCOLOR, MAX_STRING_CHARS, string, scr.font_pic)

#define HUD_DrawRightString(x, y, string) \
    SCR_DrawStringEx(x, y, UI_RIGHT, MAX_STRING_CHARS, string, scr.font_pic)

#define HUD_DrawAltRightString(x, y, string) \
    SCR_DrawStringEx(x, y, UI_RIGHT | UI_XORCOLOR, MAX_STRING_CHARS, string, scr.font_pic)

static void HUD_DrawNumber(int x, int y, int color, int width, int value)
{
    char    num[16], *ptr;
    int     l;
    int     frame;

    if (width < 1)
        return;

    // draw number string
    if (width > 5)
        width = 5;

    color &= 1;

    l = Q_scnprintf(num, sizeof(num), "%i", value);
    if (l > width)
        l = width;
    x += 2 + DIGIT_WIDTH * (width - l);

    ptr = num;
    while (*ptr && l) {
        if (*ptr == '-')
            frame = STAT_MINUS;
        else
            frame = *ptr - '0';

        trap_R_DrawPic(x, y, scr.sb_pics[color][frame]);
        x += DIGIT_WIDTH;
        ptr++;
        l--;
    }
}

#define DISPLAY_ITEMS   17

static void SCR_DrawInventory(void)
{
    int     i;
    int     num, selected_num, item;
    int     index[MAX_ITEMS];
    char    string[MAX_STRING_CHARS];
    char    name[MAX_QPATH];
    char    bind[MAX_QPATH];
    int     x, y;
    int     selected;
    int     top;

    if (!(cg.frame->ps.stats[STAT_LAYOUTS] & LAYOUTS_INVENTORY))
        return;

    selected = cg.frame->ps.stats[STAT_SELECTED_ITEM];

    num = 0;
    selected_num = 0;
    for (i = 0; i < MAX_ITEMS; i++) {
        if (i == selected) {
            selected_num = num;
        }
        if (cg.inventory[i]) {
            index[num++] = i;
        }
    }

    // determine scroll point
    top = selected_num - DISPLAY_ITEMS / 2;
    if (top > num - DISPLAY_ITEMS) {
        top = num - DISPLAY_ITEMS;
    }
    if (top < 0) {
        top = 0;
    }

    x = (scr.hud_width - 256) / 2;
    y = (scr.hud_height - 240) / 2;

    trap_R_DrawPic(x, y + 8, scr.inven_pic);
    y += 24;
    x += 24;

    HUD_DrawString(x, y, "hotkey ### item");
    y += CONCHAR_HEIGHT;

    HUD_DrawString(x, y, "------ --- ----");
    y += CONCHAR_HEIGHT;

    for (i = top; i < num && i < top + DISPLAY_ITEMS; i++) {
        item = index[i];
        // search for a binding
        trap_GetConfigstring(CS_ITEMS + item, name, sizeof(name));

        Q_concat(string, sizeof(string), "use ", name);
        trap_Key_GetBinding(string, bind, sizeof(bind));

        Q_snprintf(string, sizeof(string), "%6s %3i %s",
                   bind, cg.inventory[item], name);

        if (item != selected) {
            HUD_DrawAltString(x, y, string);
        } else {    // draw a blinky cursor by the selected item
            HUD_DrawString(x, y, string);
            if ((cgs.realtime >> 8) & 1) {
                trap_R_DrawChar(x - CONCHAR_WIDTH, y, 0, 15, scr.font_pic);
            }
        }

        y += CONCHAR_HEIGHT;
    }
}

static void SCR_SkipToEndif(const char **s)
{
    int i, skip = 1;
    char *token;

    while (*s) {
        token = COM_Parse(s);
        if (!strcmp(token, "xl") || !strcmp(token, "xr") || !strcmp(token, "xv") ||
            !strcmp(token, "yt") || !strcmp(token, "yb") || !strcmp(token, "yv") ||
            !strcmp(token, "pic") || !strcmp(token, "picn") || !strcmp(token, "color") ||
            strstr(token, "string")) {
            COM_SkipToken(s);
            continue;
        }

        if (!strcmp(token, "client")) {
            for (i = 0; i < 6; i++)
                COM_SkipToken(s);
            continue;
        }

        if (!strcmp(token, "ctf")) {
            for (i = 0; i < 5; i++)
                COM_SkipToken(s);
            continue;
        }

        if (!strcmp(token, "num") || !strcmp(token, "health_bars")) {
            COM_SkipToken(s);
            COM_SkipToken(s);
            continue;
        }

        if (!strcmp(token, "hnum")) continue;
        if (!strcmp(token, "anum")) continue;
        if (!strcmp(token, "rnum")) continue;

        if (!strcmp(token, "if")) {
            COM_SkipToken(s);
            skip++;
            continue;
        }

        if (!strcmp(token, "endif")) {
            if (--skip > 0)
                continue;
            return;
        }
    }
}

static void SCR_DrawHealthBar(int x, int y, int value)
{
    if (!value)
        return;

    int bar_width = scr.hud_width / 3;
    float percent = (value - 1) / 254.0f;
    int w = bar_width * percent + 0.5f;
    int h = CONCHAR_HEIGHT / 2;

    x -= bar_width / 2;
    trap_R_DrawFill8(x, y, w, h, 240);
    trap_R_DrawFill8(x + w, y, bar_width - w, h, 4);
}

static void SCR_ExecuteLayoutString(const char *s)
{
    char    buffer[MAX_QPATH];
    int     x, y;
    int     value;
    char    *token;
    int     width;
    int     index;
    clientinfo_t    *ci;

    if (!s[0])
        return;

    x = 0;
    y = 0;

    while (s) {
        token = COM_Parse(&s);
        if (token[2] == 0) {
            if (token[0] == 'x') {
                if (token[1] == 'l') {
                    token = COM_Parse(&s);
                    x = Q_atoi(token);
                    continue;
                }

                if (token[1] == 'r') {
                    token = COM_Parse(&s);
                    x = scr.hud_width + Q_atoi(token);
                    continue;
                }

                if (token[1] == 'v') {
                    token = COM_Parse(&s);
                    x = scr.hud_width / 2 - 160 + Q_atoi(token);
                    continue;
                }
            }

            if (token[0] == 'y') {
                if (token[1] == 't') {
                    token = COM_Parse(&s);
                    y = Q_atoi(token);
                    continue;
                }

                if (token[1] == 'b') {
                    token = COM_Parse(&s);
                    y = scr.hud_height + Q_atoi(token);
                    continue;
                }

                if (token[1] == 'v') {
                    token = COM_Parse(&s);
                    y = scr.hud_height / 2 - 120 + Q_atoi(token);
                    continue;
                }
            }
        }

        if (!strcmp(token, "pic")) {
            // draw a pic from a stat number
            token = COM_Parse(&s);
            value = Q_atoi(token);
            if (value < 0 || value >= MAX_STATS) {
                Com_Error(ERR_DROP, "%s: invalid stat index", __func__);
            }
            value = cg.frame->ps.stats[value];
            if (value < 0 || value >= MAX_IMAGES) {
                Com_Error(ERR_DROP, "%s: invalid pic index", __func__);
            }
            if (value) {
                trap_R_DrawPic(x, y, cg.image_precache[value]);
            }
            continue;
        }

        if (!strcmp(token, "client")) {
            // draw a deathmatch client block
            int     score, ping, time;

            token = COM_Parse(&s);
            x = scr.hud_width / 2 - 160 + Q_atoi(token);
            token = COM_Parse(&s);
            y = scr.hud_height / 2 - 120 + Q_atoi(token);

            token = COM_Parse(&s);
            value = Q_atoi(token);
            if (value < 0 || value >= MAX_CLIENTS) {
                Com_Error(ERR_DROP, "%s: invalid client index", __func__);
            }
            ci = &cg.clientinfo[value];

            token = COM_Parse(&s);
            score = Q_atoi(token);

            token = COM_Parse(&s);
            ping = Q_atoi(token);

            token = COM_Parse(&s);
            time = Q_atoi(token);

            HUD_DrawAltString(x + 32, y, ci->name);
            HUD_DrawString(x + 32, y + CONCHAR_HEIGHT, "Score: ");
            Q_snprintf(buffer, sizeof(buffer), "%i", score);
            HUD_DrawAltString(x + 32 + 7 * CONCHAR_WIDTH, y + CONCHAR_HEIGHT, buffer);
            Q_snprintf(buffer, sizeof(buffer), "Ping:  %i", ping);
            HUD_DrawString(x + 32, y + 2 * CONCHAR_HEIGHT, buffer);
            Q_snprintf(buffer, sizeof(buffer), "Time:  %i", time);
            HUD_DrawString(x + 32, y + 3 * CONCHAR_HEIGHT, buffer);

            if (!ci->icon) {
                ci = &cg.baseclientinfo;
            }
            trap_R_DrawPic(x, y, ci->icon);
            continue;
        }

        if (!strcmp(token, "ctf")) {
            // draw a ctf client block
            int     score, ping;

            token = COM_Parse(&s);
            x = scr.hud_width / 2 - 160 + Q_atoi(token);
            token = COM_Parse(&s);
            y = scr.hud_height / 2 - 120 + Q_atoi(token);

            token = COM_Parse(&s);
            value = Q_atoi(token);
            if (value < 0 || value >= MAX_CLIENTS) {
                Com_Error(ERR_DROP, "%s: invalid client index", __func__);
            }
            ci = &cg.clientinfo[value];

            token = COM_Parse(&s);
            score = Q_atoi(token);

            token = COM_Parse(&s);
            ping = Q_atoi(token);
            if (ping > 999)
                ping = 999;

            Q_snprintf(buffer, sizeof(buffer), "%3d %3d %-12.12s",
                       score, ping, ci->name);
            if (value == cg.frame->ps.clientnum) {
                HUD_DrawAltString(x, y, buffer);
            } else {
                HUD_DrawString(x, y, buffer);
            }
            continue;
        }

        if (!strcmp(token, "picn")) {
            // draw a pic from a name
            token = COM_Parse(&s);
            trap_R_DrawPic(x, y, trap_R_RegisterPic(token));
            continue;
        }

        if (!strcmp(token, "num")) {
            // draw a number
            token = COM_Parse(&s);
            width = Q_atoi(token);
            token = COM_Parse(&s);
            value = Q_atoi(token);
            if (value < 0 || value >= MAX_STATS) {
                Com_Error(ERR_DROP, "%s: invalid stat index", __func__);
            }
            value = cg.frame->ps.stats[value];
            HUD_DrawNumber(x, y, 0, width, value);
            continue;
        }

        if (!strcmp(token, "hnum")) {
            // health number
            int     color;

            width = 3;
            value = cg.frame->ps.stats[STAT_HEALTH];
            if (value > 25)
                color = 0;  // green
            else if (value > 0)
                color = (cg.frame->number >> 2) & 1;     // flash
            else
                color = 1;

            if (cg.frame->ps.stats[STAT_FLASHES] & 1)
                trap_R_DrawPic(x, y, scr.field_pic);

            HUD_DrawNumber(x, y, color, width, value);
            continue;
        }

        if (!strcmp(token, "anum")) {
            // ammo number
            int     color;

            width = 3;
            value = cg.frame->ps.stats[STAT_AMMO];
            if (value > 5)
                color = 0;  // green
            else if (value >= 0)
                color = (cg.frame->number >> 2) & 1;     // flash
            else
                continue;   // negative number = don't show

            if (cg.frame->ps.stats[STAT_FLASHES] & 4)
                trap_R_DrawPic(x, y, scr.field_pic);

            HUD_DrawNumber(x, y, color, width, value);
            continue;
        }

        if (!strcmp(token, "rnum")) {
            // armor number
            int     color;

            width = 3;
            value = cg.frame->ps.stats[STAT_ARMOR];
            if (value < 1)
                continue;

            color = 0;  // green

            if (cg.frame->ps.stats[STAT_FLASHES] & 2)
                trap_R_DrawPic(x, y, scr.field_pic);

            HUD_DrawNumber(x, y, color, width, value);
            continue;
        }

        if (!strncmp(token, "stat_", 5)) {
            char *cmd = token + 5;
            token = COM_Parse(&s);
            index = Q_atoi(token);
            if (index < 0 || index >= MAX_STATS) {
                Com_Error(ERR_DROP, "%s: invalid stat index", __func__);
            }
            index = cg.frame->ps.stats[index];
            if (index < 0 || index >= MAX_CONFIGSTRINGS) {
                Com_Error(ERR_DROP, "%s: invalid string index", __func__);
            }
            trap_GetConfigstring(index, buffer, sizeof(buffer));
            if (!strcmp(cmd, "string"))
                HUD_DrawString(x, y, buffer);
            else if (!strcmp(cmd, "string2"))
                HUD_DrawAltString(x, y, buffer);
            else if (!strcmp(cmd, "cstring"))
                HUD_DrawCenterString(x + 320 / 2, y, buffer);
            else if (!strcmp(cmd, "cstring2"))
                HUD_DrawAltCenterString(x + 320 / 2, y, buffer);
            else if (!strcmp(cmd, "rstring"))
                HUD_DrawRightString(x, y, buffer);
            else if (!strcmp(cmd, "rstring2"))
                HUD_DrawAltRightString(x, y, buffer);
            continue;
        }

        if (!strcmp(token, "cstring")) {
            token = COM_Parse(&s);
            HUD_DrawCenterString(x + 320 / 2, y, token);
            continue;
        }

        if (!strcmp(token, "cstring2")) {
            token = COM_Parse(&s);
            HUD_DrawAltCenterString(x + 320 / 2, y, token);
            continue;
        }

        if (!strcmp(token, "string")) {
            token = COM_Parse(&s);
            HUD_DrawString(x, y, token);
            continue;
        }

        if (!strcmp(token, "string2")) {
            token = COM_Parse(&s);
            HUD_DrawAltString(x, y, token);
            continue;
        }

        if (!strcmp(token, "rstring")) {
            token = COM_Parse(&s);
            HUD_DrawRightString(x, y, token);
            continue;
        }

        if (!strcmp(token, "rstring2")) {
            token = COM_Parse(&s);
            HUD_DrawAltRightString(x, y, token);
            continue;
        }

        if (!strcmp(token, "if")) {
            token = COM_Parse(&s);
            value = Q_atoi(token);
            if (value < 0 || value >= MAX_STATS) {
                Com_Error(ERR_DROP, "%s: invalid stat index", __func__);
            }
            value = cg.frame->ps.stats[value];
            if (!value) {   // skip to endif
                SCR_SkipToEndif(&s);
            }
            continue;
        }

        // Q2PRO extension
        if (!strcmp(token, "color")) {
            color_t     color;

            token = COM_Parse(&s);
            if (COM_ParseColor(token, &color)) {
                color.u8[3] *= scr_alpha.value;
                trap_R_SetColor(color.u32);
            }
            continue;
        }

        if (!strcmp(token, "health_bars")) {
            token = COM_Parse(&s);
            value = Q_atoi(token);
            if (value < 0 || value >= MAX_STATS) {
                Com_Error(ERR_DROP, "%s: invalid stat index", __func__);
            }
            value = cg.frame->ps.stats[value];

            token = COM_Parse(&s);
            index = Q_atoi(token);
            if (index < 0 || index >= MAX_CONFIGSTRINGS) {
                Com_Error(ERR_DROP, "%s: invalid string index", __func__);
            }

            trap_GetConfigstring(index, buffer, sizeof(buffer));
            HUD_DrawCenterString(x + 320 / 2, y, buffer);
            SCR_DrawHealthBar(x + 320 / 2, y + CONCHAR_HEIGHT + 4, value & 0xff);
            SCR_DrawHealthBar(x + 320 / 2, y + CONCHAR_HEIGHT + 12, (value >> 8) & 0xff);
            continue;
        }
    }

    trap_R_ClearColor();
    trap_R_SetAlpha(scr_alpha.value);
}

//=============================================================================

static void SCR_DrawPause(void)
{
    int x, y, w, h;

    if (!sv_paused.integer)
        return;
    if (!cl_paused.integer)
        return;
    if (scr_showpause.integer != 1)
        return;

    trap_R_GetPicSize(&w, &h, scr.pause_pic);
    x = (scr.hud_width - w) / 2;
    y = (scr.hud_height - h) / 2;

    trap_R_DrawPic(x, y, scr.pause_pic);
}

static void SCR_DrawLoading(void)
{
    int x, y, w, h;

    if (!scr.draw_loading)
        return;

    scr.draw_loading = false;

    trap_R_SetScale(scr.hud_scale);

    trap_R_GetPicSize(&w, &h, scr.loading_pic);
    x = (scr.config.width * scr.hud_scale - w) / 2;
    y = (scr.config.height * scr.hud_scale - h) / 2;

    trap_R_DrawPic(x, y, scr.loading_pic);

    trap_R_SetScale(1.0f);
}

static void SCR_DrawHitMarker(void)
{
    if (!cg.hit_marker_count)
        return;
    if (!scr.hit_marker_pic || scr_hit_marker_time.integer <= 0 ||
        cgs.realtime - cg.hit_marker_time > scr_hit_marker_time.integer) {
        cg.hit_marker_count = 0;
        return;
    }

    float frac = (float)(cgs.realtime - cg.hit_marker_time) / scr_hit_marker_time.integer;
    float alpha = 1.0f - (frac * frac);

    int x = (scr.hud_width - scr.hit_marker_width) / 2;
    int y = (scr.hud_height - scr.hit_marker_height) / 2;

    trap_R_SetColor(MakeColor(255, 0, 0, alpha * 255));

    trap_R_DrawStretchPic(x + ch_x.integer,
                          y + ch_y.integer,
                          scr.hit_marker_width,
                          scr.hit_marker_height,
                          scr.hit_marker_pic);
}

static void SCR_DrawCrosshair(void)
{
    int x, y;

    if (!scr_crosshair.integer)
        return;
    if (cg.frame->ps.stats[STAT_LAYOUTS] & (LAYOUTS_HIDE_HUD | LAYOUTS_HIDE_CROSSHAIR))
        return;

    x = (scr.hud_width - scr.crosshair_width) / 2;
    y = (scr.hud_height - scr.crosshair_height) / 2;

    trap_R_SetColor(scr.crosshair_color.u32);

    trap_R_DrawStretchPic(x + ch_x.integer,
                          y + ch_y.integer,
                          scr.crosshair_width,
                          scr.crosshair_height,
                          scr.crosshair_pic);

    SCR_DrawHitMarker();
}

// The status bar is a small layout program that is based on the stats array
static void SCR_DrawStats(void)
{
    if (scr_draw2d.integer <= 1)
        return;
    if (cg.frame->ps.stats[STAT_LAYOUTS] & LAYOUTS_HIDE_HUD)
        return;

    SCR_ExecuteLayoutString(cg.statusbar);
}

static void SCR_DrawLayout(void)
{
    if (scr_draw2d.integer == 3 && !trap_Key_IsDown(K_F1))
        return;     // turn off for GTV

    if (cgs.demoplayback && trap_Key_IsDown(K_F1))
        goto draw;

    if (!(cg.frame->ps.stats[STAT_LAYOUTS] & LAYOUTS_LAYOUT))
        return;

draw:
    SCR_ExecuteLayoutString(cg.layout);
}

static void SCR_Draw2D(void)
{
    if (scr_draw2d.integer <= 0)
        return;     // turn off for screenshots

    if (trap_Key_GetDest() & KEY_MENU)
        return;

    trap_R_SetScale(scr.hud_scale);

    scr.hud_height = Q_rint(scr.hud_height * scr.hud_scale);
    scr.hud_width = Q_rint(scr.hud_width * scr.hud_scale);

    // crosshair has its own color and alpha
    SCR_DrawCrosshair();

    // the rest of 2D elements share common alpha
    trap_R_ClearColor();
    trap_R_SetAlpha(scr_alpha.value);

    SCR_DrawStats();

    SCR_DrawLayout();

    SCR_DrawInventory();

    SCR_DrawCenterString();

    SCR_DrawNet();

    SCR_DrawChatHUD();

    SCR_DrawPause();

    trap_R_ClearColor();

    trap_R_SetScale(1.0f);
}

qvm_exported void CG_DrawActiveFrame(void)
{
    trap_R_ClearScene();

    trap_S_ClearLoopingSounds();

    // start with full screen HUD
    scr.hud_height = scr.config.height;
    scr.hud_width = scr.config.width;

    SCR_UpdateCvars();

    SCR_DrawDemo();

    SCR_CalcVrect();

    // clear any dirty part of the background
    SCR_TileClear();

    // draw 3D game view
    V_RenderView();

    // draw all 2D elements
    SCR_Draw2D();
}

qvm_exported void CG_ModeChanged(void)
{
    trap_R_GetConfig(&scr.config);
}
