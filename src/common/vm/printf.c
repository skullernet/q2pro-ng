/* Copyright © 2005-2020 Rich Felker, et al.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include "shared/shared.h"
#include "common/intreadwrite.h"
#include "common/vm.h"
#include <float.h>

/* Some useful macros */

#define MAX(a,b) ((a)>(b) ? (a) : (b))
#define MIN(a,b) ((a)<(b) ? (a) : (b))

#define ISDIGIT(a) (((unsigned)(a)-'0') < 10)

/* Convenient bit representation for modifier flags, which all fall
 * within 31 codepoints of the space character. */

#define ALT_FORM   (1U<<('#'-' '))
#define ZERO_PAD   (1U<<('0'-' '))
#define LEFT_ADJ   (1U<<('-'-' '))
#define PAD_POS    (1U<<(' '-' '))
#define MARK_POS   (1U<<('+'-' '))
#define GROUPED    (1U<<('\''-' '))

#define FLAGMASK (ALT_FORM|ZERO_PAD|LEFT_ADJ|PAD_POS|MARK_POS|GROUPED)

/* State machine to accept length modifiers + conversion specifiers.
 * Result is 0 on failure, or an argument type to pop on success. */

enum {
    BARE, LPRE, LLPRE, HPRE, HHPRE, BIGLPRE,
    ZTPRE, JPRE,
    STOP,
    PTR, INT, UINT, ULLONG,
    LONG, ULONG,
    SHORT, USHORT, CHAR, UCHAR,
    LLONG, SIZET, IMAX, UMAX, PDIFF, UIPTR,
    DBL, LDBL,
    MAXSTATE
};

#define S(x) [(x)-'A']

static const unsigned char states[]['z' - 'A' + 1] = {
    { /* 0: bare types */
        S('d') = INT, S('i') = INT,
        S('o') = UINT, S('u') = UINT, S('x') = UINT, S('X') = UINT,
        S('e') = DBL, S('f') = DBL, S('g') = DBL, S('a') = DBL,
        S('E') = DBL, S('F') = DBL, S('G') = DBL, S('A') = DBL,
        S('c') = INT, S('C') = UINT,
        S('s') = PTR, S('S') = PTR, S('p') = UIPTR, S('n') = PTR,
        S('l') = LPRE, S('h') = HPRE, S('L') = BIGLPRE,
        S('z') = ZTPRE, S('j') = JPRE, S('t') = ZTPRE,
    }, { /* 1: l-prefixed */
        S('d') = LONG, S('i') = LONG,
        S('o') = ULONG, S('u') = ULONG, S('x') = ULONG, S('X') = ULONG,
        S('e') = DBL, S('f') = DBL, S('g') = DBL, S('a') = DBL,
        S('E') = DBL, S('F') = DBL, S('G') = DBL, S('A') = DBL,
        S('c') = UINT, S('s') = PTR, S('n') = PTR,
        S('l') = LLPRE,
    }, { /* 2: ll-prefixed */
        S('d') = LLONG, S('i') = LLONG,
        S('o') = ULLONG, S('u') = ULLONG,
        S('x') = ULLONG, S('X') = ULLONG,
        S('n') = PTR,
    }, { /* 3: h-prefixed */
        S('d') = SHORT, S('i') = SHORT,
        S('o') = USHORT, S('u') = USHORT,
        S('x') = USHORT, S('X') = USHORT,
        S('n') = PTR,
        S('h') = HHPRE,
    }, { /* 4: hh-prefixed */
        S('d') = CHAR, S('i') = CHAR,
        S('o') = UCHAR, S('u') = UCHAR,
        S('x') = UCHAR, S('X') = UCHAR,
        S('n') = PTR,
    }, { /* 5: L-prefixed */
        S('e') = LDBL, S('f') = LDBL, S('g') = LDBL, S('a') = LDBL,
        S('E') = LDBL, S('F') = LDBL, S('G') = LDBL, S('A') = LDBL,
        S('n') = PTR,
    }, { /* 6: z- or t-prefixed (assumed to be same size) */
        S('d') = PDIFF, S('i') = PDIFF,
        S('o') = SIZET, S('u') = SIZET,
        S('x') = SIZET, S('X') = SIZET,
        S('n') = PTR,
    }, { /* 7: j-prefixed */
        S('d') = IMAX, S('i') = IMAX,
        S('o') = UMAX, S('u') = UMAX,
        S('x') = UMAX, S('X') = UMAX,
        S('n') = PTR,
    }
};

#define OOB(x) ((unsigned)(x)-'A' > 'z'-'A')

union arg {
    uint64_t i;
    double f;
    void *p;
};

static bool pop_arg(const vm_memory_t *m, union arg *arg, int type, uint32_t *ap)
{
    uint64_t val;

    switch (type) {
    case LLONG:
    case ULLONG:
    case IMAX:
    case UMAX:
    case DBL:
    case LDBL:
        if (*ap > m->bytesize - 8)
            return false;
        *ap = Q_ALIGN(*ap, 8);
        val = RN64(m->bytes + *ap);
        *ap += 8;
        break;
    default:
        if (*ap > m->bytesize - 4)
            return false;
        *ap = Q_ALIGN(*ap, 4);
        val = RN32(m->bytes + *ap);
        *ap += 4;
        break;
    }

    switch (type) {
    case PTR:
        if (!val) {
            arg->p = NULL;
            break;
        }
        if (val >= m->bytesize)
            return false;
        arg->p = m->bytes + val;
        break;
    case INT:
    case LONG:
    case PDIFF:
        arg->i = (int32_t)val;
        break;
    case UINT:
    case ULONG:
    case UIPTR:
    case SIZET:
        arg->i = (uint32_t)val;
        break;
    case LLONG:
    case IMAX:
        arg->i = (int64_t)val;
        break;
    case ULLONG:
    case UMAX:
    case DBL:
    case LDBL:
        arg->i = (uint64_t)val;
        break;
    case SHORT:
        arg->i = (int16_t)val;
        break;
    case USHORT:
        arg->i = (uint16_t)val;
        break;
    case CHAR:
        arg->i = (int8_t)val;
        break;
    case UCHAR:
        arg->i = (uint8_t)val;
        break;
    default:
        return false;
    }

    return true;
}

static bool put_cnt(const vm_memory_t *m, byte *p, int type, int cnt)
{
    const byte *end = m->bytes + m->bytesize;

    switch (type) {
    case BARE:
    case LPRE:
    case ZTPRE:
        if (p > end - 4)
            return false;
        WN32(p, cnt);
        break;
    case LLPRE:
    case JPRE:
        if (p > end - 8)
            return false;
        WN64(p, cnt);
        break;
    case HPRE:
        if (p > end - 2)
            return false;
        WN16(p, cnt);
        break;
    case HHPRE:
        if (p > end - 1)
            return false;
        WN8(p, cnt);
        break;
    default:
        return false;
    }

    return true;
}

struct buffer {
    char *buf;
    size_t pos;
};

static void out(struct buffer *f, const char *s, size_t l)
{
    size_t n = MIN(f->pos, l);
    if (n) {
        memcpy(f->buf, s, n);
        f->buf += n;
        f->pos -= n;
    }
}

static void pad(struct buffer *f, char c, int w, int l, int fl)
{
    char pad[256];
    if (fl & (LEFT_ADJ | ZERO_PAD) || l >= w) return;
    l = w - l;
    memset(pad, c, l > sizeof pad ? sizeof pad : l);
    for (; l >= sizeof pad; l -= sizeof pad)
        out(f, pad, sizeof pad);
    out(f, pad, l);
}

static char *fmt_x(uint64_t x, char *s, int lower)
{
    for (; x; x >>= 4) *--s = com_hexchars[(x & 15)] | lower;
    return s;
}

static char *fmt_o(uint64_t x, char *s)
{
    for (; x; x >>= 3) *--s = '0' + (x & 7);
    return s;
}

static char *fmt_u(uint64_t x, char *s)
{
    unsigned long y;
    for (; x > ULONG_MAX; x /= 10) *--s = '0' + x % 10;
    for (y = x;  y >= 10; y /= 10) *--s = '0' + y % 10;
    if (y) *--s = '0' + y;
    return s;
}

static int fmt_fp(struct buffer *f, double y, int w, int p, int fl, int t)
{
    uint32_t big[128];
    uint32_t *a, *d, *r, *z;
    int e2 = 0, e, i, j, l;
    char buf[9 + DBL_MANT_DIG / 4], *s;
    const char *prefix = "-0X+0X 0X-0x+0x 0x";
    int pl;
    char ebuf0[3 * sizeof(int)], *ebuf = &ebuf0[3 * sizeof(int)], *estr = NULL;

    pl = 1;
    if (signbit(y)) {
        y = -y;
    } else if (fl & MARK_POS) {
        prefix += 3;
    } else if (fl & PAD_POS) {
        prefix += 6;
    } else prefix++, pl = 0;

    if (!isfinite(y)) {
        char *s = (t & 32) ? "inf" : "INF";
        if (y != y) s = (t & 32) ? "nan" : "NAN";
        pad(f, ' ', w, 3 + pl, fl & ~ZERO_PAD);
        out(f, prefix, pl);
        out(f, s, 3);
        pad(f, ' ', w, 3 + pl, fl ^ LEFT_ADJ);
        return MAX(w, 3 + pl);
    }

    y = frexp(y, &e2) * 2;
    if (y) e2--;

    if ((t | 32) == 'a') {
        if (t & 32) prefix += 9;
        pl += 2;

        if (p >= 0 && p < (DBL_MANT_DIG - 1 + 3) / 4) {
            double round = scalbn(1, DBL_MANT_DIG - 1 - (p * 4));
            if (*prefix == '-') {
                y = -y;
                y -= round;
                y += round;
                y = -y;
            } else {
                y += round;
                y -= round;
            }
        }

        estr = fmt_u(e2 < 0 ? -e2 : e2, ebuf);
        if (estr == ebuf) *--estr = '0';
        *--estr = (e2 < 0 ? '-' : '+');
        *--estr = t + ('p' - 'a');

        s = buf;
        do {
            int x = y;
            *s++ = com_hexchars[x] | (t & 32);
            y = 16 * (y - x);
            if (s - buf == 1 && (y || p > 0 || (fl & ALT_FORM))) *s++ = '.';
        } while (y);

        if (p > INT_MAX - 2 - (ebuf - estr) - pl)
            return -1;
        if (p && s - buf - 2 < p)
            l = (p + 2) + (ebuf - estr);
        else
            l = (s - buf) + (ebuf - estr);

        pad(f, ' ', w, pl + l, fl);
        out(f, prefix, pl);
        pad(f, '0', w, pl + l, fl ^ ZERO_PAD);
        out(f, buf, s - buf);
        pad(f, '0', l - (ebuf - estr) - (s - buf), 0, 0);
        out(f, estr, ebuf - estr);
        pad(f, ' ', w, pl + l, fl ^ LEFT_ADJ);
        return MAX(w, pl + l);
    }
    if (p < 0) p = 6;

    if (y) y *= 0x1p28, e2 -= 28;

    if (e2 < 0) a = r = z = big;
    else a = r = z = big + sizeof(big) / sizeof(*big) - DBL_MANT_DIG - 1;

    do {
        *z = y;
        y = 1000000000 * (y - *z++);
    } while (y);

    while (e2 > 0) {
        uint32_t carry = 0;
        int sh = MIN(29, e2);
        for (d = z - 1; d >= a; d--) {
            uint64_t x = ((uint64_t) * d << sh) + carry;
            *d = x % 1000000000;
            carry = x / 1000000000;
        }
        if (carry) *--a = carry;
        while (z > a && !z[-1]) z--;
        e2 -= sh;
    }
    while (e2 < 0) {
        uint32_t carry = 0, *b;
        int sh = MIN(9, -e2), need = 1 + (p + DBL_MANT_DIG / 3U + 8) / 9;
        for (d = a; d < z; d++) {
            uint32_t rm = *d & ((1 << sh) - 1);
            *d = (*d >> sh) + carry;
            carry = (1000000000 >> sh) * rm;
        }
        if (!*a) a++;
        if (carry) *z++ = carry;
        /* Avoid (slow!) computation past requested precision */
        b = (t | 32) == 'f' ? r : a;
        if (z - b > need) z = b + need;
        e2 += sh;
    }

    if (a < z) for (i = 10, e = 9 * (r - a); *a >= i; i *= 10, e++);
    else e = 0;

    /* Perform rounding: j is precision after the radix (possibly neg) */
    j = p - ((t | 32) != 'f') * e - ((t | 32) == 'g' && p);
    if (j < 9 * (z - r - 1)) {
        uint32_t x;
        /* We avoid C's broken division of negative numbers */
        d = r + 1 + ((j + 9 * DBL_MAX_EXP) / 9 - DBL_MAX_EXP);
        j += 9 * DBL_MAX_EXP;
        j %= 9;
        for (i = 10, j++; j < 9; i *= 10, j++);
        x = *d % i;
        /* Are there any significant digits past j? */
        if (x || d + 1 != z) {
            double round = 2 / DBL_EPSILON;
            double small;
            if ((*d / i & 1) || (i == 1000000000 && d > a && (d[-1] & 1)))
                round += 2;
            if (x < i / 2) small = 0x0.8p0;
            else if (x == i / 2 && d + 1 == z) small = 0x1.0p0;
            else small = 0x1.8p0;
            if (pl && *prefix == '-') round *= -1, small *= -1;
            *d -= x;
            /* Decide whether to round by probing round+small */
            if (round + small != round) {
                *d = *d + i;
                while (*d > 999999999) {
                    *d-- = 0;
                    if (d < a) *--a = 0;
                    (*d)++;
                }
                for (i = 10, e = 9 * (r - a); *a >= i; i *= 10, e++);
            }
        }
        if (z > d + 1) z = d + 1;
    }
    for (; z > a && !z[-1]; z--);

    if ((t | 32) == 'g') {
        if (!p) p++;
        if (p > e && e >= -4) {
            t--;
            p -= e + 1;
        } else {
            t -= 2;
            p--;
        }
        if (!(fl & ALT_FORM)) {
            /* Count trailing zeros in last place */
            if (z > a && z[-1]) for (i = 10, j = 0; z[-1] % i == 0; i *= 10, j++);
            else j = 9;
            if ((t | 32) == 'f')
                p = MIN(p, MAX(0, 9 * (z - r - 1) - j));
            else
                p = MIN(p, MAX(0, 9 * (z - r - 1) + e - j));
        }
    }
    if (p > INT_MAX - 1 - (p || (fl & ALT_FORM)))
        return -1;
    l = 1 + p + (p || (fl & ALT_FORM));
    if ((t | 32) == 'f') {
        if (e > INT_MAX - l) return -1;
        if (e > 0) l += e;
    } else {
        estr = fmt_u(e < 0 ? -e : e, ebuf);
        while (ebuf - estr < 2) *--estr = '0';
        *--estr = (e < 0 ? '-' : '+');
        *--estr = t;
        if (ebuf - estr > INT_MAX - l) return -1;
        l += ebuf - estr;
    }

    if (l > INT_MAX - pl) return -1;
    pad(f, ' ', w, pl + l, fl);
    out(f, prefix, pl);
    pad(f, '0', w, pl + l, fl ^ ZERO_PAD);

    if ((t | 32) == 'f') {
        if (a > r) a = r;
        for (d = a; d <= r; d++) {
            char *s = fmt_u(*d, buf + 9);
            if (d != a) while (s > buf) *--s = '0';
            else if (s == buf + 9) *--s = '0';
            out(f, s, buf + 9 - s);
        }
        if (p || (fl & ALT_FORM)) out(f, ".", 1);
        for (; d < z && p > 0; d++, p -= 9) {
            char *s = fmt_u(*d, buf + 9);
            while (s > buf) *--s = '0';
            out(f, s, MIN(9, p));
        }
        pad(f, '0', p + 9, 9, 0);
    } else {
        if (z <= a) z = a + 1;
        for (d = a; d < z && p >= 0; d++) {
            char *s = fmt_u(*d, buf + 9);
            if (s == buf + 9) *--s = '0';
            if (d != a) while (s > buf) *--s = '0';
            else {
                out(f, s++, 1);
                if (p > 0 || (fl & ALT_FORM)) out(f, ".", 1);
            }
            out(f, s, MIN(buf + 9 - s, p));
            p -= buf + 9 - s;
        }
        pad(f, '0', p + 18, 18, 0);
        out(f, estr, ebuf - estr);
    }

    pad(f, ' ', w, pl + l, fl ^ LEFT_ADJ);

    return MAX(w, pl + l);
}

static int getint(char **s)
{
    int i;
    for (i = 0; ISDIGIT(**s); (*s)++) {
        if (i > INT_MAX / 10U || **s - '0' > INT_MAX - 10 * i) i = -1;
        else i = 10 * i + (**s - '0');
    }
    return i;
}

int VM_vsnprintf(const vm_memory_t *m, char *str, size_t size, const char *fmt, uint32_t ap)
{
    char *a, *z, *s = (char *)fmt;
    unsigned fl;
    int w, p, xp;
    union arg arg;
    unsigned st, ps;
    int cnt = 0, l = 0;
    char buf[sizeof(uint64_t) * 3];
    const char *prefix;
    int t, pl;
    struct buffer f = { str, size ? size - 1 : 0 };

    for (;;) {
        /* This error is only specified for snprintf, but since it's
         * unspecified for other forms, do the same. Stop immediately
         * on overflow; otherwise %n could produce wrong results. */
        if (l > INT_MAX - cnt) return -1;

        /* Update output count, end loop when fmt is exhausted */
        cnt += l;
        if (!*s) break;

        /* Handle literal text and %% format specifiers */
        for (a = s; *s && *s != '%'; s++);
        for (z = s; s[0] == '%' && s[1] == '%'; z++, s += 2);
        if (z - a > INT_MAX - cnt) return -1;
        l = z - a;
        out(&f, a, l);
        if (l) continue;
        s++;

        /* Read modifier flags */
        for (fl = 0; (unsigned)*s - ' ' < 32 && (FLAGMASK & (1U << (*s - ' '))); s++)
            fl |= 1U << (*s - ' ');

        /* Read field width */
        if (*s == '*') {
            if (!pop_arg(m, &arg, INT, &ap)) return -1;
            w = arg.i;
            s++;
            if (w < 0) fl |= LEFT_ADJ, w = -w;
        } else if ((w = getint(&s)) < 0) return -1;

        /* Read precision */
        if (*s == '.' && s[1] == '*') {
            if (!pop_arg(m, &arg, INT, &ap)) return -1;
            p = arg.i;
            s += 2;
            xp = (p >= 0);
        } else if (*s == '.') {
            s++;
            p = getint(&s);
            xp = 1;
        } else {
            p = -1;
            xp = 0;
        }

        /* Format specifier state machine */
        st = 0;
        do {
            if (OOB(*s)) return -1;
            ps = st;
            st = states[st]S(*s++);
        } while (st - 1 < STOP);

        if (!st) return -1;
        if (!pop_arg(m, &arg, st, &ap)) return -1;

        z = buf + sizeof(buf);
        prefix = "-+   0X0x";
        pl = 0;
        t = s[-1];

        /* Transform ls,lc -> S,C */
        if (ps && (t & 15) == 3) t &= ~32;

        /* - and 0 flags are mutually exclusive */
        if (fl & LEFT_ADJ) fl &= ~ZERO_PAD;

        switch (t) {
        case 'n':
            if (!arg.p) return -1;
            if (!put_cnt(m, arg.p, ps, cnt)) return -1;
            continue;
        case 'p':
            p = MAX(p, 8);
            t = 'x';
            fl |= ALT_FORM;
        case 'x':
        case 'X':
            a = fmt_x(arg.i, z, t & 32);
            if (arg.i && (fl & ALT_FORM)) prefix += (t >> 4), pl = 2;
            goto ifmt_tail;
        case 'o':
            a = fmt_o(arg.i, z);
            if ((fl & ALT_FORM) && p < z - a + 1) p = z - a + 1;
            goto ifmt_tail;
        case 'd':
        case 'i':
            pl = 1;
            if (arg.i > INT64_MAX) {
                arg.i = -arg.i;
            } else if (fl & MARK_POS) {
                prefix++;
            } else if (fl & PAD_POS) {
                prefix += 2;
            } else pl = 0;
        case 'u':
            a = fmt_u(arg.i, z);
        ifmt_tail:
            if (xp && p < 0) return -1;
            if (xp) fl &= ~ZERO_PAD;
            if (!arg.i && !p) {
                a = z;
                break;
            }
            p = MAX(p, z - a + !arg.i);
            break;
        case 'c':
            *(a = z - (p = 1)) = arg.i;
            fl &= ~ZERO_PAD;
            break;
        case 's':
            a = arg.p ? arg.p : "(null)";
            z = a + Q_strnlen(a, p < 0 ? INT_MAX : p);
            if (p < 0 && *z) return -1;
            p = z - a;
            fl &= ~ZERO_PAD;
            break;
        case 'C':
        case 'S':
            return -1;
        case 'e':
        case 'f':
        case 'g':
        case 'a':
        case 'E':
        case 'F':
        case 'G':
        case 'A':
            if (xp && p < 0) return -1;
            l = fmt_fp(&f, arg.f, w, p, fl, t);
            if (l < 0) return -1;
            continue;
        }

        if (p < z - a) p = z - a;
        if (p > INT_MAX - pl) return -1;
        if (w < pl + p) w = pl + p;
        if (w > INT_MAX - cnt) return -1;

        pad(&f, ' ', w, pl + p, fl);
        out(&f, prefix, pl);
        pad(&f, '0', w, pl + p, fl ^ ZERO_PAD);
        pad(&f, '0', p, z - a, 0);
        out(&f, a, z - a);
        pad(&f, ' ', w, pl + p, fl ^ LEFT_ADJ);

        l = w;
    }

    if (size)
        *f.buf = 0;

    return cnt;
}
