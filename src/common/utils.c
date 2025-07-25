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

#include "shared/shared.h"
#include "common/common.h"
#include "common/utils.h"
#include "common/zone.h"

/*
==============================================================================

                        WILDCARD COMPARE

==============================================================================
*/

static bool match_raw(int c1, int c2, bool ignorecase)
{
    if (c1 != c2) {
        if (!ignorecase) {
            return false;
        }
#ifdef _WIN32
        // ugly hack for file listing
        c1 = c1 == '\\' ? '/' : Q_tolower(c1);
        c2 = c2 == '\\' ? '/' : Q_tolower(c2);
#else
        c1 = Q_tolower(c1);
        c2 = Q_tolower(c2);
#endif
        if (c1 != c2) {
            return false;
        }
    }

    return true;
}

static bool match_char(int c1, int c2, bool ignorecase)
{
    if (c1 == '?') {
        return c2; // match any char except NUL
    }

    return match_raw(c1, c2, ignorecase);
}

static bool match_part(const char *filter, const char *string,
                       size_t len, bool ignorecase)
{
    bool match;

    do {
        // skip over escape character
        if (*filter == '\\') {
            filter++;
            match = match_raw(*filter, *string, ignorecase);
        } else {
            match = match_char(*filter, *string, ignorecase);
        }

        if (!match) {
            return false;
        }

        filter++;
        string++;
    } while (--len);

    return true;
}

// match the longest possible part
static const char *match_filter(const char *filter, const char *string,
                                size_t len, bool ignorecase)
{
    const char *ret = NULL;
    size_t remaining = strlen(string);

    while (remaining >= len) {
        if (match_part(filter, string, len, ignorecase)) {
            string += len;
            remaining -= len;
            ret = string;
            continue;
        }
        string++;
        remaining--;
    }

    return ret;
}

/*
=================
Com_WildCmpEx

Wildcard compare. Returns true if string matches the pattern, false otherwise.

- 'term' is handled as an additional filter terminator (besides NUL).
- '*' matches any substring, including the empty string, but prefers longest
possible substrings.
- '?' matches any single character except NUL.
- '\\' can be used to escape any character, including itself. any special
characters lose their meaning in this case.

=================
*/
bool Com_WildCmpEx(const char *filter, const char *string,
                   int term, bool ignorecase)
{
    const char *sub;
    size_t len;
    bool match;

    while (*filter && *filter != term) {
        if (*filter == '*') {
            // skip consecutive wildcards
            do {
                filter++;
            } while (*filter == '*');

            // scan out filter part to match
            for (sub = filter, len = 0; *filter && *filter != term && *filter != '*'; filter++, len++) {
                // skip over escape character
                if (*filter == '\\') {
                    filter++;
                    if (!*filter) {
                        break;
                    }
                }
            }

            // wildcard at the end matches everything
            if (!len) {
                return true;
            }

            string = match_filter(sub, string, len, ignorecase);
            if (!string) {
                return false;
            }
        } else {
            // skip over escape character
            if (*filter == '\\') {
                filter++;
                if (!*filter) {
                    break;
                }
                match = match_raw(*filter, *string, ignorecase);
            } else {
                match = match_char(*filter, *string, ignorecase);
            }

            // match single character
            if (!match) {
                return false;
            }

            filter++;
            string++;
        }
    }

    // match NUL at the end
    return !*string;
}

/*
==============================================================================

                        MISC

==============================================================================
*/

#if USE_REF
/*
================
Com_ParseExtensionString

Helper function to parse an OpenGL-style extension string.
================
*/
unsigned Com_ParseExtensionString(const char *s, const char *const extnames[])
{
    unsigned mask;
    const char *p;
    size_t l1, l2;
    int i;

    if (!s) {
        return 0;
    }

    mask = 0;
    while (*s) {
        p = Q_strchrnul(s, ' ');
        l1 = p - s;
        for (i = 0; i < 32 && extnames[i]; i++) {
            l2 = strlen(extnames[i]);
            if (l1 == l2 && !memcmp(s, extnames[i], l1)) {
                Com_DPrintf("Found %s\n", extnames[i]);
                mask |= BIT(i);
                break;
            }
        }
        if (!*p) {
            break;
        }
        s = p + 1;
    }

    return mask;
}
#endif

/*
================
Com_ParseMapName
================
*/
bool Com_ParseMapName(char *out, const char *in, size_t size)
{
    if (Q_stricmpn(in, "maps/", 5))
        return false;
    in += 5;

    char *ext = COM_FileExtension(in);
    if (ext == in || Q_stricmp(ext, ".bsp"))
        return false;

    return COM_StripExtension(out, in, size) < size;
}

#if USE_CLIENT
/*
================
Com_ParseTimespec

Parses time/frame specification for seeking in demos.
Does not check for integer overflow...
================
*/
bool Com_ParseTimespec(const char *s, int64_t *msec)
{
    unsigned long long c1, c2, c3;
    char *p;

    c1 = strtoull(s, &p, 10);
    if (!*p) {
        *msec = c1 * 1000; // sec
        return true;
    }

    if (*p == '.') {
        c2 = strtoull(p + 1, &p, 10);
        if (*p)
            return false;
        *msec = c1 * 1000 + c2 * 100; // sec.frac
        return true;
    }

    if (*p == ':') {
        c2 = strtoull(p + 1, &p, 10);
        if (!*p) {
            *msec = c1 * 60000 + c2 * 1000; // min:sec
            return true;
        }

        if (*p == '.') {
            c3 = strtoull(p + 1, &p, 10);
            if (*p)
                return false;
            *msec = c1 * 60000 + c2 * 1000 + c3 * 100; // min:sec.frac
            return true;
        }

        return false;
    }

    return false;
}
#endif

/*
================
Com_HashString
================
*/
unsigned Com_HashString(const char *s, unsigned size)
{
    unsigned hash, c;

    hash = 0;
    while (*s) {
        c = *s++;
        hash = 127 * hash + c;
    }

    hash = (hash >> 20) ^ (hash >> 10) ^ hash;
    return hash & (size - 1);
}

/*
================
Com_HashStringLen

A case-insensitive version of Com_HashString that hashes up to 'len'
characters.
================
*/
unsigned Com_HashStringLen(const char *s, size_t len, unsigned size)
{
    unsigned hash, c;

    hash = 0;
    while (*s && len--) {
        c = Q_tolower(*s++);
        hash = 127 * hash + c;
    }

    hash = (hash >> 20) ^ (hash >> 10) ^ hash;
    return hash & (size - 1);
}

/*
===============
Com_PageInMemory

===============
*/
int    paged_total;

void Com_PageInMemory(void *buffer, size_t size)
{
    int        i;

    for (i = size - 1; i > 0; i -= 4096)
        paged_total += ((byte *)buffer)[i];
}

size_t Com_FormatLocalTime(char *buffer, size_t size, const char *fmt)
{
    static struct tm cached_tm;
    static time_t cached_time;
    time_t now;
    struct tm *tm;
    size_t ret;

    if (!size)
        return 0;

    now = time(NULL);
    if (now == cached_time) {
        // avoid calling localtime() too often since it is not that cheap
        tm = &cached_tm;
    } else {
        tm = localtime(&now);
        if (!tm)
            goto fail;
        cached_time = now;
        cached_tm = *tm;
    }

    ret = strftime(buffer, size, fmt, tm);
    Q_assert(ret < size);
    if (ret)
        return ret;
fail:
    buffer[0] = 0;
    return 0;
}

size_t Com_FormatTime(char *buffer, size_t size, time_t t)
{
    int     sec, min, hour, day;

    min = t / 60; sec = t % 60;
    hour = min / 60; min %= 60;
    day = hour / 24; hour %= 24;

    if (day) {
        return Q_scnprintf(buffer, size, "%d+%d:%02d.%02d", day, hour, min, sec);
    }
    if (hour) {
        return Q_scnprintf(buffer, size, "%d:%02d.%02d", hour, min, sec);
    }
    return Q_scnprintf(buffer, size, "%02d.%02d", min, sec);
}

size_t Com_FormatTimeLong(char *buffer, size_t size, time_t t)
{
    int     sec, min, hour, day;
    size_t  len;

    if (!t) {
        return Q_scnprintf(buffer, size, "0 secs");
    }

    min = t / 60; sec = t % 60;
    hour = min / 60; min %= 60;
    day = hour / 24; hour %= 24;

    len = 0;

    if (day) {
        len += Q_scnprintf(buffer + len, size - len,
                           "%d day%s%s", day, day == 1 ? "" : "s", (hour || min || sec) ? ", " : "");
    }
    if (hour) {
        len += Q_scnprintf(buffer + len, size - len,
                           "%d hour%s%s", hour, hour == 1 ? "" : "s", (min || sec) ? ", " : "");
    }
    if (min) {
        len += Q_scnprintf(buffer + len, size - len,
                           "%d min%s%s", min, min == 1 ? "" : "s", sec ? ", " : "");
    }
    if (sec) {
        len += Q_scnprintf(buffer + len, size - len,
                           "%d sec%s", sec, sec == 1 ? "" : "s");
    }

    return len;
}

size_t Com_TimeDiff(char *buffer, size_t size, time_t *p, time_t now)
{
    time_t diff;

    if (*p > now) {
        *p = now;
    }
    diff = now - *p;
    return Com_FormatTime(buffer, size, diff);
}

size_t Com_TimeDiffLong(char *buffer, size_t size, time_t *p, time_t now)
{
    time_t diff;

    if (*p > now) {
        *p = now;
    }
    diff = now - *p;
    return Com_FormatTimeLong(buffer, size, diff);
}

size_t Com_FormatSize(char *dest, size_t destsize, int64_t bytes)
{
    if (bytes >= 1000000000) {
        return Q_scnprintf(dest, destsize, "%.1fG", bytes * 1e-9);
    }
    if (bytes >= 10000000) {
        return Q_scnprintf(dest, destsize, "%"PRId64"M", bytes / 1000000);
    }
    if (bytes >= 1000000) {
        return Q_scnprintf(dest, destsize, "%.1fM", bytes * 1e-6);
    }
    if (bytes >= 1000) {
        return Q_scnprintf(dest, destsize, "%"PRId64"K", bytes / 1000);
    }
    if (bytes >= 0) {
        return Q_scnprintf(dest, destsize, "%"PRId64, bytes);
    }
    return Q_scnprintf(dest, destsize, "???");
}

size_t Com_FormatSizeLong(char *dest, size_t destsize, int64_t bytes)
{
    if (bytes >= 1000000000) {
        return Q_scnprintf(dest, destsize, "%.1f GB", bytes * 1e-9);
    }
    if (bytes >= 10000000) {
        return Q_scnprintf(dest, destsize, "%"PRId64" MB", bytes / 1000000);
    }
    if (bytes >= 1000000) {
        return Q_scnprintf(dest, destsize, "%.1f MB", bytes * 1e-6);
    }
    if (bytes >= 1000) {
        return Q_scnprintf(dest, destsize, "%"PRId64" kB", bytes / 1000);
    }
    if (bytes >= 0) {
        return Q_scnprintf(dest, destsize, "%"PRId64" byte%s",
                           bytes, bytes == 1 ? "" : "s");
    }
    return Q_scnprintf(dest, destsize, "unknown size");
}

#if USE_CLIENT

/*
===============
Com_SlowRand

`Slow' PRNG that begins consecutive frames with the same seed. Reseeded each 16
ms. Used for random effects that shouldn't cause too much tearing without vsync.
===============
*/
uint32_t Com_SlowRand(void)
{
    static uint32_t com_rand_ts;
    static uint32_t com_rand_base;
    static uint32_t com_rand_seed;
    static uint32_t com_rand_frame;

    // see if it's time to reseed
    if (com_rand_ts != com_localTime2 / 16) {
        com_rand_ts = com_localTime2 / 16;
        com_rand_base = Q_rand();
    }

    // reset if started new frame
    if (com_rand_frame != com_framenum) {
        com_rand_frame = com_framenum;
        com_rand_seed = com_rand_base;
    }

    // xorshift RNG
    uint32_t x = com_rand_seed;

    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;

    return com_rand_seed = x;
}

#endif

int64_t Com_RealTime(void)
{
    return time(NULL);
}

bool Com_LocalTime(int64_t in, vm_time_t *out)
{
    time_t t = in;
    if (t != in)
        return false;

    struct tm *tm = localtime(&t);
    if (!tm)
        return false;

    out->tm_sec = tm->tm_sec;
    out->tm_min = tm->tm_min;
    out->tm_hour = tm->tm_hour;
    out->tm_mday = tm->tm_mday;
    out->tm_mon = tm->tm_mon;
    out->tm_year = tm->tm_year;
    out->tm_wday = tm->tm_wday;
    out->tm_yday = tm->tm_yday;
    out->tm_isdst = tm->tm_isdst;
    return true;
}

/*
==============================================================================

                        UNICODE

==============================================================================
*/

#if USE_CLIENT

#define QCHAR_BOX   11

#include "unicode_translit.h"

/*
==================
UTF8_ReadCodePoint

Reads at most 4 bytes from *src and advances the pointer.
Returns 32-bit codepoint, or UNICODE_UNKNOWN on error.
==================
*/
uint32_t UTF8_ReadCodePoint(const char **src)
{
    static const uint32_t mincode[3] = { 0x80, 0x800, 0x10000 };
    const char  *text = *src;
    uint32_t    code;
    uint8_t     first, cont;
    int         bytes, i;

    first = text[0];
    if (!first)
        return 0;

    if (first < 128) {
        *src = text + 1;
        return first;
    }

    bytes = 7 - Q_log2(first ^ 255);
    if (bytes < 2 || bytes > 4) {
        *src = text + 1;
        return UNICODE_UNKNOWN;
    }

    code = first & (127 >> bytes);
    for (i = 1; i < bytes; i++) {
        cont = text[i];
        if ((cont & 0xC0) != 0x80) {
            *src = text + i;
            return UNICODE_UNKNOWN;
        }
        code = (code << 6) | (cont & 63);
    }

    *src = text + i;

    if (code > UNICODE_MAX)
        return UNICODE_UNKNOWN; // out of range

    if (code >= 0xD800 && code <= 0xDFFF)
        return UNICODE_UNKNOWN; // surrogate

    if (code < mincode[bytes - 2])
        return UNICODE_UNKNOWN; // overlong

    return code;
}

static const char *UTF8_TranslitCode(uint32_t code)
{
    int left = 0;
    int right = q_countof(unicode_translit) - 1;

    if (code > unicode_translit[right].code)
        return NULL;

    while (left <= right) {
        int i = (left + right) / 2;
        if (unicode_translit[i].code < code)
            left = i + 1;
        else if (unicode_translit[i].code > code)
            right = i - 1;
        else
            return unicode_translit[i].remap;
    }

    return NULL;
}

/*
==================
UTF8_TranslitBuffer

Transliterates a string from UTF-8 to Quake encoding.

Returns the number of characters (not including the NUL terminator) that would
be written into output buffer. Return value >= size signifies overflow.
==================
*/
size_t UTF8_TranslitBuffer(char *dst, const char *src, size_t size)
{
    size_t len = 0;

    while (*src) {
        // ASCII fast path
        uint8_t c = *src;
        if (q_likely(c < 128)) {
            if (++len < size)
                *dst++ = c;
            src++;
            continue;
        }

        // a codepoint produces from 1 to 4 Quake characters
        const char *res = UTF8_TranslitCode(UTF8_ReadCodePoint(&src));
        if (res) {
            for (int i = 0; i < 4 && res[i]; i++)
                if (++len < size)
                    *dst++ = res[i];
        } else {
            if (++len < size)
                *dst++ = QCHAR_BOX;
        }
    }

    if (size)
        *dst = 0;

    return len;
}

/*
==================
UTF8_TranslitString

Transliterates a string from UTF-8 to Quake encoding.
Allocates copy of the string. Returned data must be Z_Free'd.
==================
*/
char *UTF8_TranslitString(const char *src)
{
    size_t len = UTF8_TranslitBuffer(NULL, src, 0) + 1;
    char *copy = Z_Malloc(len);
    UTF8_TranslitBuffer(copy, src, len);
    return copy;
}

#endif // USE_CLINET
