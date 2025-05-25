/*
 * Copyright (c) 1990 The Regents of the University of California.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <float.h>
#include <limits.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#include "shared/bg_lib.h"

int errno;

char *
strcat(char *s, const char *append)
{
    char *save = s;

    for (; *s; ++s);
    while ((*s++ = *append++) != '\0');
    return(save);
}

char *
strcpy(char *to, const char *from)
{
    char *save = to;

    for (; (*to = *from) != '\0'; ++from, ++to);
    return(save);
}

size_t
strlen(const char *str)
{
    const char *s;

    for (s = str; *s; ++s)
        ;
    return (s - str);
}

char *strstr(const char *string, const char *substring)
{
    const char *a;
    const char *b;

    /* First scan quickly through the two strings looking for a
     * single-character match.  When it's found, then compare the
     * rest of the substring.
     */

    b = substring;

    if (*b == 0)
        return (char *)string;

    for (; *string != 0; string += 1) {
        if (*string != *b)
            continue;

        a = string;

        while (1) {
            if (*b == 0)
                return (char *)string;

            if (*a++ != *b++)
                break;
        }

        b = substring;
    }

    return NULL;
}

char *
strchr(const char *p, int ch)
{
    for (;; ++p) {
        if (*p == (char) ch)
            return((char *)p);
        if (!*p)
            return((char *)NULL);
    }
    /* NOTREACHED */
}

char *
strrchr(const char *p, int ch)
{
    char *save;

    for (save = NULL;; ++p) {
        if (*p == (char) ch)
            save = (char *)p;
        if (!*p)
            return(save);
    }
    /* NOTREACHED */
}

void *
memchr(const void *s, int c, size_t n)
{
    if (n != 0) {
        const unsigned char *p = s;

        do {
            if (*p++ == (unsigned char)c)
                return ((void *)(p - 1));
        } while (--n != 0);
    }
    return (NULL);
}

/*
 * Compare strings.
 */
int
strcmp(const char *s1, const char *s2)
{
    while (*s1 == *s2++)
        if (*s1++ == 0)
            return (0);
    return (*(unsigned char *)s1 - *(unsigned char *)--s2);
}

int
strncmp(const char *s1, const char *s2, size_t n)
{
    if (n == 0)
        return (0);
    do {
        if (*s1 != *s2++)
            return (*(unsigned char *)s1 - *(unsigned char *)--s2);
        if (*s1++ == 0)
            break;
    } while (--n != 0);
    return (0);
}

/*
 * Compare memory regions.
 */
int
memcmp(const void *s1, const void *s2, size_t n)
{
    if (n != 0) {
        const unsigned char *p1 = s1, *p2 = s2;

        do {
            if (*p1++ != *p2++)
                return (*--p1 - *--p2);
        } while (--n != 0);
    }
    return (0);
}

// ============================================================================

/* Copyright (C) 2011 by Lynn Ochs
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

/* Minor changes by Rich Felker for integration in musl, 2011-04-27. */

/* Smoothsort, an adaptive variant of Heapsort.  Memory usage: O(1).
   Run time: Worst case O(n log n), close to O(n) in the mostly-sorted case. */

#define ntz(x) __builtin_ctz((x))

typedef int (*cmpfun)(const void *, const void *);

static inline int pntz(size_t p[2]) {
    int r = ntz(p[0] - 1);
    if(r != 0 || (r = 8*sizeof(size_t) + ntz(p[1])) != 8*sizeof(size_t)) {
        return r;
    }
    return 0;
}

static void cycle(size_t width, unsigned char* ar[], int n)
{
    unsigned char tmp[256];
    size_t l;
    int i;

    if(n < 2) {
        return;
    }

    ar[n] = tmp;
    while(width) {
        l = sizeof(tmp) < width ? sizeof(tmp) : width;
        memcpy(ar[n], ar[0], l);
        for(i = 0; i < n; i++) {
            memcpy(ar[i], ar[i + 1], l);
            ar[i] += l;
        }
        width -= l;
    }
}

/* shl() and shr() need n > 0 */
static inline void shl(size_t p[2], int n)
{
    if(n >= 8 * sizeof(size_t)) {
        n -= 8 * sizeof(size_t);
        p[1] = p[0];
        p[0] = 0;
    }
    p[1] <<= n;
    p[1] |= p[0] >> (sizeof(size_t) * 8 - n);
    p[0] <<= n;
}

static inline void shr(size_t p[2], int n)
{
    if(n >= 8 * sizeof(size_t)) {
        n -= 8 * sizeof(size_t);
        p[0] = p[1];
        p[1] = 0;
    }
    p[0] >>= n;
    p[0] |= p[1] << (sizeof(size_t) * 8 - n);
    p[1] >>= n;
}

static void sift(unsigned char *head, size_t width, cmpfun cmp, int pshift, size_t lp[])
{
    unsigned char *rt, *lf;
    unsigned char *ar[14 * sizeof(size_t) + 1];
    int i = 1;

    ar[0] = head;
    while(pshift > 1) {
        rt = head - width;
        lf = head - width - lp[pshift - 2];

        if(cmp(ar[0], lf) >= 0 && cmp(ar[0], rt) >= 0) {
            break;
        }
        if(cmp(lf, rt) >= 0) {
            ar[i++] = lf;
            head = lf;
            pshift -= 1;
        } else {
            ar[i++] = rt;
            head = rt;
            pshift -= 2;
        }
    }
    cycle(width, ar, i);
}

static void trinkle(unsigned char *head, size_t width, cmpfun cmp, size_t pp[2], int pshift, int trusty, size_t lp[])
{
    unsigned char *stepson,
                  *rt, *lf;
    size_t p[2];
    unsigned char *ar[14 * sizeof(size_t) + 1];
    int i = 1;
    int trail;

    p[0] = pp[0];
    p[1] = pp[1];

    ar[0] = head;
    while(p[0] != 1 || p[1] != 0) {
        stepson = head - lp[pshift];
        if(cmp(stepson, ar[0]) <= 0) {
            break;
        }
        if(!trusty && pshift > 1) {
            rt = head - width;
            lf = head - width - lp[pshift - 2];
            if(cmp(rt, stepson) >= 0 || cmp(lf, stepson) >= 0) {
                break;
            }
        }

        ar[i++] = stepson;
        head = stepson;
        trail = pntz(p);
        shr(p, trail);
        pshift += trail;
        trusty = 0;
    }
    if(!trusty) {
        cycle(width, ar, i);
        sift(head, width, cmp, pshift, lp);
    }
}

void qsort(void *base, size_t nel, size_t width, cmpfun cmp)
{
    size_t lp[12*sizeof(size_t)];
    size_t i, size = width * nel;
    unsigned char *head, *high;
    size_t p[2] = {1, 0};
    int pshift = 1;
    int trail;

    if (!size) return;

    head = base;
    high = head + size - width;

    /* Precompute Leonardo numbers, scaled by element width */
    for(lp[0]=lp[1]=width, i=2; (lp[i]=lp[i-2]+lp[i-1]+width) < size; i++);

    while(head < high) {
        if((p[0] & 3) == 3) {
            sift(head, width, cmp, pshift, lp);
            shr(p, 2);
            pshift += 2;
        } else {
            if(lp[pshift - 1] >= high - head) {
                trinkle(head, width, cmp, p, pshift, 0, lp);
            } else {
                sift(head, width, cmp, pshift, lp);
            }

            if(pshift == 1) {
                shl(p, 1);
                pshift = 0;
            } else {
                shl(p, pshift - 1);
                pshift = 1;
            }
        }

        p[0] |= 1;
        head += width;
    }

    trinkle(head, width, cmp, p, pshift, 0, lp);

    while(pshift != 1 || p[0] != 1 || p[1] != 0) {
        if(pshift <= 1) {
            trail = pntz(p);
            shr(p, trail);
            pshift += trail;
        } else {
            shl(p, 2);
            pshift -= 2;
            p[0] ^= 7;
            shr(p, 1);
            trinkle(head - lp[pshift] - width, width, cmp, p, pshift + 1, 1, lp);
            shl(p, 1);
            p[0] |= 1;
            trinkle(head - width, width, cmp, p, pshift, 1, lp);
        }
        head -= width;
    }
}

// ============================================================================

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

/* Some useful macros */

#define MAX(a,b) ((a)>(b) ? (a) : (b))
#define MIN(a,b) ((a)<(b) ? (a) : (b))

#define NL_ARGMAX 9

#define isdigit(a) (((unsigned)(a)-'0') < 10)

static inline int isspace(int c)
{
    return c == ' ' || (unsigned)c-'\t' < 5;
}

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
    NOARG,
    MAXSTATE
};

#define S(x) [(x)-'A']

static const unsigned char states[]['z'-'A'+1] = {
    { /* 0: bare types */
        S('d') = INT, S('i') = INT,
        S('o') = UINT, S('u') = UINT, S('x') = UINT, S('X') = UINT,
        S('e') = DBL, S('f') = DBL, S('g') = DBL, S('a') = DBL,
        S('E') = DBL, S('F') = DBL, S('G') = DBL, S('A') = DBL,
        S('c') = INT, S('C') = UINT,
        S('s') = PTR, S('S') = PTR, S('p') = UIPTR, S('n') = PTR,
        S('m') = NOARG,
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

union arg
{
    uintmax_t i;
    double f;
    void *p;
};

static void pop_arg(union arg *arg, int type, va_list *ap)
{
    switch (type) {
           case PTR:    arg->p = va_arg(*ap, void *);
    break; case INT:    arg->i = va_arg(*ap, int);
    break; case UINT:   arg->i = va_arg(*ap, unsigned int);
    break; case LONG:   arg->i = va_arg(*ap, long);
    break; case ULONG:  arg->i = va_arg(*ap, unsigned long);
    break; case ULLONG: arg->i = va_arg(*ap, unsigned long long);
    break; case SHORT:  arg->i = (short)va_arg(*ap, int);
    break; case USHORT: arg->i = (unsigned short)va_arg(*ap, int);
    break; case CHAR:   arg->i = (signed char)va_arg(*ap, int);
    break; case UCHAR:  arg->i = (unsigned char)va_arg(*ap, int);
    break; case LLONG:  arg->i = va_arg(*ap, long long);
    break; case SIZET:  arg->i = va_arg(*ap, size_t);
    break; case IMAX:   arg->i = va_arg(*ap, intmax_t);
    break; case UMAX:   arg->i = va_arg(*ap, uintmax_t);
    break; case PDIFF:  arg->i = va_arg(*ap, ptrdiff_t);
    break; case UIPTR:  arg->i = (uintptr_t)va_arg(*ap, void *);
    break; case DBL:    arg->f = va_arg(*ap, double);
    break; case LDBL:   arg->f = va_arg(*ap, double);   /* FIXME */
    }
}

static int wctomb(char *s, wchar_t wc)
{
    return -1; /* no wide characters support */
}

static size_t strnlen(const char *s, size_t n)
{
    const char *p = memchr(s, 0, n);
    return p ? p-s : n;
}

typedef struct {
    char *buf;
    size_t pos;
} FILE;

static void out(FILE *f, const char *s, size_t l)
{
    size_t n = MIN(f->pos, l);
    if (n) {
        memcpy(f->buf, s, n);
        f->buf += n;
        f->pos -= n;
    }
}

static void pad(FILE *f, char c, int w, int l, int fl)
{
    char pad[256];
    if (fl & (LEFT_ADJ | ZERO_PAD) || l >= w) return;
    l = w - l;
    memset(pad, c, l>sizeof pad ? sizeof pad : l);
    for (; l >= sizeof pad; l -= sizeof pad)
        out(f, pad, sizeof pad);
    out(f, pad, l);
}

static const char xdigits[16] = {
    "0123456789ABCDEF"
};

static char *fmt_x(uintmax_t x, char *s, int lower)
{
    for (; x; x>>=4) *--s = xdigits[(x&15)]|lower;
    return s;
}

static char *fmt_o(uintmax_t x, char *s)
{
    for (; x; x>>=3) *--s = '0' + (x&7);
    return s;
}

static char *fmt_u(uintmax_t x, char *s)
{
    unsigned long y;
    for (   ; x>ULONG_MAX; x/=10) *--s = '0' + x%10;
    for (y=x;       y>=10; y/=10) *--s = '0' + y%10;
    if (y) *--s = '0' + y;
    return s;
}

static double scalbn(double x, int n)
{
    union {double f; uint64_t i;} u;
    double y = x;

    if (n > 1023) {
        y *= 0x1p1023;
        n -= 1023;
        if (n > 1023) {
            y *= 0x1p1023;
            n -= 1023;
            if (n > 1023)
                n = 1023;
        }
    } else if (n < -1022) {
        /* make sure final n < -53 to avoid double
           rounding in the subnormal range */
        y *= 0x1p-1022 * 0x1p53;
        n += 1022 - 53;
        if (n < -1022) {
            y *= 0x1p-1022 * 0x1p53;
            n += 1022 - 53;
            if (n < -1022)
                n = -1022;
        }
    }
    u.i = (uint64_t)(0x3ff+n)<<52;
    x = y * u.f;
    return x;
}

static double frexp(double x, int *e)
{
    union { double d; uint64_t i; } y = { x };
    int ee = y.i>>52 & 0x7ff;

    if (!ee) {
        if (x) {
            x = frexp(x*0x1p64, e);
            *e -= 64;
        } else *e = 0;
        return x;
    } else if (ee == 0x7ff) {
        return x;
    }

    *e = ee - 0x3fe;
    y.i &= 0x800fffffffffffffull;
    y.i |= 0x3fe0000000000000ull;
    return y.d;
}

static int fmt_fp(FILE *f, double y, int w, int p, int fl, int t)
{
    uint32_t big[128];
    uint32_t *a, *d, *r, *z;
    int e2=0, e, i, j, l;
    char buf[9+DBL_MANT_DIG/4], *s;
    const char *prefix="-0X+0X 0X-0x+0x 0x";
    int pl;
    char ebuf0[3*sizeof(int)], *ebuf=&ebuf0[3*sizeof(int)], *estr;

    pl=1;
    if (signbit(y)) {
        y=-y;
    } else if (fl & MARK_POS) {
        prefix+=3;
    } else if (fl & PAD_POS) {
        prefix+=6;
    } else prefix++, pl=0;

    if (!isfinite(y)) {
        char *s = (t&32)?"inf":"INF";
        if (y!=y) s=(t&32)?"nan":"NAN";
        pad(f, ' ', w, 3+pl, fl&~ZERO_PAD);
        out(f, prefix, pl);
        out(f, s, 3);
        pad(f, ' ', w, 3+pl, fl^LEFT_ADJ);
        return MAX(w, 3+pl);
    }

    y = frexp(y, &e2) * 2;
    if (y) e2--;

    if ((t|32)=='a') {
        if (t&32) prefix += 9;
        pl += 2;

        if (p>=0 && p<(DBL_MANT_DIG-1+3)/4) {
            double round = scalbn(1, DBL_MANT_DIG-1-(p*4));
            if (*prefix=='-') {
                y=-y;
                y-=round;
                y+=round;
                y=-y;
            } else {
                y+=round;
                y-=round;
            }
        }

        estr=fmt_u(e2<0 ? -e2 : e2, ebuf);
        if (estr==ebuf) *--estr='0';
        *--estr = (e2<0 ? '-' : '+');
        *--estr = t+('p'-'a');

        s=buf;
        do {
            int x=y;
            *s++=xdigits[x]|(t&32);
            y=16*(y-x);
            if (s-buf==1 && (y||p>0||(fl&ALT_FORM))) *s++='.';
        } while (y);

        if (p > INT_MAX-2-(ebuf-estr)-pl)
            return -1;
        if (p && s-buf-2 < p)
            l = (p+2) + (ebuf-estr);
        else
            l = (s-buf) + (ebuf-estr);

        pad(f, ' ', w, pl+l, fl);
        out(f, prefix, pl);
        pad(f, '0', w, pl+l, fl^ZERO_PAD);
        out(f, buf, s-buf);
        pad(f, '0', l-(ebuf-estr)-(s-buf), 0, 0);
        out(f, estr, ebuf-estr);
        pad(f, ' ', w, pl+l, fl^LEFT_ADJ);
        return MAX(w, pl+l);
    }
    if (p<0) p=6;

    if (y) y *= 0x1p28, e2-=28;

    if (e2<0) a=r=z=big;
    else a=r=z=big+sizeof(big)/sizeof(*big) - DBL_MANT_DIG - 1;

    do {
        *z = y;
        y = 1000000000*(y-*z++);
    } while (y);

    while (e2>0) {
        uint32_t carry=0;
        int sh=MIN(29,e2);
        for (d=z-1; d>=a; d--) {
            uint64_t x = ((uint64_t)*d<<sh)+carry;
            *d = x % 1000000000;
            carry = x / 1000000000;
        }
        if (carry) *--a = carry;
        while (z>a && !z[-1]) z--;
        e2-=sh;
    }
    while (e2<0) {
        uint32_t carry=0, *b;
        int sh=MIN(9,-e2), need=1+(p+DBL_MANT_DIG/3U+8)/9;
        for (d=a; d<z; d++) {
            uint32_t rm = *d & (1<<sh)-1;
            *d = (*d>>sh) + carry;
            carry = (1000000000>>sh) * rm;
        }
        if (!*a) a++;
        if (carry) *z++ = carry;
        /* Avoid (slow!) computation past requested precision */
        b = (t|32)=='f' ? r : a;
        if (z-b > need) z = b+need;
        e2+=sh;
    }

    if (a<z) for (i=10, e=9*(r-a); *a>=i; i*=10, e++);
    else e=0;

    /* Perform rounding: j is precision after the radix (possibly neg) */
    j = p - ((t|32)!='f')*e - ((t|32)=='g' && p);
    if (j < 9*(z-r-1)) {
        uint32_t x;
        /* We avoid C's broken division of negative numbers */
        d = r + 1 + ((j+9*DBL_MAX_EXP)/9 - DBL_MAX_EXP);
        j += 9*DBL_MAX_EXP;
        j %= 9;
        for (i=10, j++; j<9; i*=10, j++);
        x = *d % i;
        /* Are there any significant digits past j? */
        if (x || d+1!=z) {
            double round = 2/DBL_EPSILON;
            double small;
            if ((*d/i & 1) || (i==1000000000 && d>a && (d[-1]&1)))
                round += 2;
            if (x<i/2) small=0x0.8p0;
            else if (x==i/2 && d+1==z) small=0x1.0p0;
            else small=0x1.8p0;
            if (pl && *prefix=='-') round*=-1, small*=-1;
            *d -= x;
            /* Decide whether to round by probing round+small */
            if (round+small != round) {
                *d = *d + i;
                while (*d > 999999999) {
                    *d--=0;
                    if (d<a) *--a=0;
                    (*d)++;
                }
                for (i=10, e=9*(r-a); *a>=i; i*=10, e++);
            }
        }
        if (z>d+1) z=d+1;
    }
    for (; z>a && !z[-1]; z--);

    if ((t|32)=='g') {
        if (!p) p++;
        if (p>e && e>=-4) {
            t--;
            p-=e+1;
        } else {
            t-=2;
            p--;
        }
        if (!(fl&ALT_FORM)) {
            /* Count trailing zeros in last place */
            if (z>a && z[-1]) for (i=10, j=0; z[-1]%i==0; i*=10, j++);
            else j=9;
            if ((t|32)=='f')
                p = MIN(p,MAX(0,9*(z-r-1)-j));
            else
                p = MIN(p,MAX(0,9*(z-r-1)+e-j));
        }
    }
    if (p > INT_MAX-1-(p || (fl&ALT_FORM)))
        return -1;
    l = 1 + p + (p || (fl&ALT_FORM));
    if ((t|32)=='f') {
        if (e > INT_MAX-l) return -1;
        if (e>0) l+=e;
    } else {
        estr=fmt_u(e<0 ? -e : e, ebuf);
        while(ebuf-estr<2) *--estr='0';
        *--estr = (e<0 ? '-' : '+');
        *--estr = t;
        if (ebuf-estr > INT_MAX-l) return -1;
        l += ebuf-estr;
    }

    if (l > INT_MAX-pl) return -1;
    pad(f, ' ', w, pl+l, fl);
    out(f, prefix, pl);
    pad(f, '0', w, pl+l, fl^ZERO_PAD);

    if ((t|32)=='f') {
        if (a>r) a=r;
        for (d=a; d<=r; d++) {
            char *s = fmt_u(*d, buf+9);
            if (d!=a) while (s>buf) *--s='0';
            else if (s==buf+9) *--s='0';
            out(f, s, buf+9-s);
        }
        if (p || (fl&ALT_FORM)) out(f, ".", 1);
        for (; d<z && p>0; d++, p-=9) {
            char *s = fmt_u(*d, buf+9);
            while (s>buf) *--s='0';
            out(f, s, MIN(9,p));
        }
        pad(f, '0', p+9, 9, 0);
    } else {
        if (z<=a) z=a+1;
        for (d=a; d<z && p>=0; d++) {
            char *s = fmt_u(*d, buf+9);
            if (s==buf+9) *--s='0';
            if (d!=a) while (s>buf) *--s='0';
            else {
                out(f, s++, 1);
                if (p>0||(fl&ALT_FORM)) out(f, ".", 1);
            }
            out(f, s, MIN(buf+9-s, p));
            p -= buf+9-s;
        }
        pad(f, '0', p+18, 18, 0);
        out(f, estr, ebuf-estr);
    }

    pad(f, ' ', w, pl+l, fl^LEFT_ADJ);

    return MAX(w, pl+l);
}

static int getint(char **s) {
    int i;
    for (i=0; isdigit(**s); (*s)++) {
        if (i > INT_MAX/10U || **s-'0' > INT_MAX-10*i) i = -1;
        else i = 10*i + (**s-'0');
    }
    return i;
}

static int printf_core(FILE *f, const char *fmt, va_list *ap, union arg *nl_arg, int *nl_type)
{
    char *a, *z, *s=(char *)fmt;
    unsigned l10n=0, fl;
    int w, p, xp;
    union arg arg;
    int argpos;
    unsigned st, ps;
    int cnt=0, l=0;
    size_t i;
    char buf[sizeof(uintmax_t)*3];
    const char *prefix;
    int t, pl;
    wchar_t wc[2], *ws;
    char mb[4];

    for (;;) {
        /* This error is only specified for snprintf, but since it's
         * unspecified for other forms, do the same. Stop immediately
         * on overflow; otherwise %n could produce wrong results. */
        if (l > INT_MAX - cnt) goto overflow;

        /* Update output count, end loop when fmt is exhausted */
        cnt += l;
        if (!*s) break;

        /* Handle literal text and %% format specifiers */
        for (a=s; *s && *s!='%'; s++);
        for (z=s; s[0]=='%' && s[1]=='%'; z++, s+=2);
        if (z-a > INT_MAX-cnt) goto overflow;
        l = z-a;
        if (f) out(f, a, l);
        if (l) continue;

        if (isdigit(s[1]) && s[2]=='$') {
            l10n=1;
            argpos = s[1]-'0';
            s+=3;
        } else {
            argpos = -1;
            s++;
        }

        /* Read modifier flags */
        for (fl=0; (unsigned)*s-' '<32 && (FLAGMASK&(1U<<(*s-' '))); s++)
            fl |= 1U<<(*s-' ');

        /* Read field width */
        if (*s=='*') {
            if (isdigit(s[1]) && s[2]=='$') {
                l10n=1;
                if (!f) nl_type[s[1]-'0'] = INT, w = 0;
                else w = nl_arg[s[1]-'0'].i;
                s+=3;
            } else if (!l10n) {
                w = f ? va_arg(*ap, int) : 0;
                s++;
            } else goto inval;
            if (w<0) fl|=LEFT_ADJ, w=-w;
        } else if ((w=getint(&s))<0) goto overflow;

        /* Read precision */
        if (*s=='.' && s[1]=='*') {
            if (isdigit(s[2]) && s[3]=='$') {
                if (!f) nl_type[s[2]-'0'] = INT, p = 0;
                else p = nl_arg[s[2]-'0'].i;
                s+=4;
            } else if (!l10n) {
                p = f ? va_arg(*ap, int) : 0;
                s+=2;
            } else goto inval;
            xp = (p>=0);
        } else if (*s=='.') {
            s++;
            p = getint(&s);
            xp = 1;
        } else {
            p = -1;
            xp = 0;
        }

        /* Format specifier state machine */
        st=0;
        do {
            if (OOB(*s)) goto inval;
            ps=st;
            st=states[st]S(*s++);
        } while (st-1<STOP);
        if (!st) goto inval;

        /* Check validity of argument type (nl/normal) */
        if (st==NOARG) {
            if (argpos>=0) goto inval;
        } else {
            if (argpos>=0) {
                if (!f) nl_type[argpos]=st;
                else arg=nl_arg[argpos];
            } else if (f) pop_arg(&arg, st, ap);
            else return 0;
        }

        if (!f) continue;

        /* Do not process any new directives once in error state. */
        // if (ferror(f)) return -1;

        z = buf + sizeof(buf);
        prefix = "-+   0X0x";
        pl = 0;
        t = s[-1];

        /* Transform ls,lc -> S,C */
        if (ps && (t&15)==3) t&=~32;

        /* - and 0 flags are mutually exclusive */
        if (fl & LEFT_ADJ) fl &= ~ZERO_PAD;

        switch(t) {
        case 'n':
            switch(ps) {
            case BARE: *(int *)arg.p = cnt; break;
            case LPRE: *(long *)arg.p = cnt; break;
            case LLPRE: *(long long *)arg.p = cnt; break;
            case HPRE: *(unsigned short *)arg.p = cnt; break;
            case HHPRE: *(unsigned char *)arg.p = cnt; break;
            case ZTPRE: *(size_t *)arg.p = cnt; break;
            case JPRE: *(uintmax_t *)arg.p = cnt; break;
            }
            continue;
        case 'p':
            p = MAX(p, 2*sizeof(void*));
            t = 'x';
            fl |= ALT_FORM;
        case 'x': case 'X':
            a = fmt_x(arg.i, z, t&32);
            if (arg.i && (fl & ALT_FORM)) prefix+=(t>>4), pl=2;
            goto ifmt_tail;
        case 'o':
            a = fmt_o(arg.i, z);
            if ((fl&ALT_FORM) && p<z-a+1) p=z-a+1;
            goto ifmt_tail;
        case 'd': case 'i':
            pl=1;
            if (arg.i>INTMAX_MAX) {
                arg.i=-arg.i;
            } else if (fl & MARK_POS) {
                prefix++;
            } else if (fl & PAD_POS) {
                prefix+=2;
            } else pl=0;
        case 'u':
            a = fmt_u(arg.i, z);
        ifmt_tail:
            if (xp && p<0) goto overflow;
            if (xp) fl &= ~ZERO_PAD;
            if (!arg.i && !p) {
                a=z;
                break;
            }
            p = MAX(p, z-a + !arg.i);
            break;
        narrow_c:
        case 'c':
            *(a=z-(p=1))=arg.i;
            fl &= ~ZERO_PAD;
            break;
        case 'm':
            if (1) a = "unknown"; else
        case 's':
            a = arg.p ? arg.p : "(null)";
            z = a + strnlen(a, p<0 ? INT_MAX : p);
            if (p<0 && *z) goto overflow;
            p = z-a;
            fl &= ~ZERO_PAD;
            break;
        case 'C':
            if (!arg.i) goto narrow_c;
            wc[0] = arg.i;
            wc[1] = 0;
            arg.p = wc;
            p = -1;
        case 'S':
            ws = arg.p;
            for (i=l=0; i<p && *ws && (l=wctomb(mb, *ws++))>=0 && l<=p-i; i+=l);
            if (l<0) return -1;
            if (i > INT_MAX) goto overflow;
            p = i;
            pad(f, ' ', w, p, fl);
            ws = arg.p;
            for (i=0; i<0U+p && *ws && i+(l=wctomb(mb, *ws++))<=p; i+=l)
                out(f, mb, l);
            pad(f, ' ', w, p, fl^LEFT_ADJ);
            l = w>p ? w : p;
            continue;
        case 'e': case 'f': case 'g': case 'a':
        case 'E': case 'F': case 'G': case 'A':
            if (xp && p<0) goto overflow;
            l = fmt_fp(f, arg.f, w, p, fl, t);
            if (l<0) goto overflow;
            continue;
        }

        if (p < z-a) p = z-a;
        if (p > INT_MAX-pl) goto overflow;
        if (w < pl+p) w = pl+p;
        if (w > INT_MAX-cnt) goto overflow;

        pad(f, ' ', w, pl+p, fl);
        out(f, prefix, pl);
        pad(f, '0', w, pl+p, fl^ZERO_PAD);
        pad(f, '0', p, z-a, 0);
        out(f, a, z-a);
        pad(f, ' ', w, pl+p, fl^LEFT_ADJ);

        l = w;
    }

    if (f) return cnt;
    if (!l10n) return 0;

    for (i=1; i<=NL_ARGMAX && nl_type[i]; i++)
        pop_arg(nl_arg+i, nl_type[i], ap);
    for (; i<=NL_ARGMAX && !nl_type[i]; i++);
    if (i<=NL_ARGMAX) goto inval;
    return 1;

inval:
    errno = EINVAL;
    return -1;
overflow:
    errno = EOVERFLOW;
    return -1;
}

int vsnprintf(char *restrict str, size_t size, const char *restrict fmt, va_list ap)
{
    va_list ap2;
    int nl_type[NL_ARGMAX+1] = {0};
    union arg nl_arg[NL_ARGMAX+1];
    int ret;

    /* the copy allows passing va_list* even if va_list is an array */
    va_copy(ap2, ap);
    if (printf_core(0, fmt, &ap2, nl_arg, nl_type) < 0) {
        va_end(ap2);
        return -1;
    }

    FILE f = { str, size ? size - 1 : 0 };
    ret = printf_core(&f, fmt, &ap2, nl_arg, nl_type);
    if (size)
        *f.buf = 0;

    va_end(ap2);
    return ret;
}

int sprintf(char *restrict s, const char *restrict fmt, ...)
{
    int ret;
    va_list ap;
    va_start(ap, fmt);
    ret = vsnprintf(s, INT_MAX, fmt, ap);
    va_end(ap);
    return ret;
}

// ============================================================================

#define LD_B1B_DIG 2
#define LD_B1B_MAX 9007199, 254740991
#define KMAX 128

#define MASK (KMAX-1)

static void sh_fromstring(FILE *f, const char *s)
{
    f->buf = (char *)s;
    f->pos = 0;
}

static int shgetc(FILE *f)
{
    return (unsigned char)f->buf[f->pos++];
}

static void shunget(FILE *f) { f->pos--; }
static size_t shcnt(FILE *f) { return f->pos; }
static void shlim(FILE *f, int lim) { /* nop */ }

static double fmod(double x, double y)
{
    union {double f; uint64_t i;} ux = {x}, uy = {y};
    int ex = ux.i>>52 & 0x7ff;
    int ey = uy.i>>52 & 0x7ff;
    int sx = ux.i>>63;
    uint64_t i;

    /* in the followings uxi should be ux.i, but then gcc wrongly adds */
    /* float load/store to inner loops ruining performance and code size */
    uint64_t uxi = ux.i;

    if (uy.i<<1 == 0 || isnan(y) || ex == 0x7ff)
        return (x*y)/(x*y);
    if (uxi<<1 <= uy.i<<1) {
        if (uxi<<1 == uy.i<<1)
            return 0*x;
        return x;
    }

    /* normalize x and y */
    if (!ex) {
        for (i = uxi<<12; i>>63 == 0; ex--, i <<= 1);
        uxi <<= -ex + 1;
    } else {
        uxi &= -1ULL >> 12;
        uxi |= 1ULL << 52;
    }
    if (!ey) {
        for (i = uy.i<<12; i>>63 == 0; ey--, i <<= 1);
        uy.i <<= -ey + 1;
    } else {
        uy.i &= -1ULL >> 12;
        uy.i |= 1ULL << 52;
    }

    /* x mod y */
    for (; ex > ey; ex--) {
        i = uxi - uy.i;
        if (i >> 63 == 0) {
            if (i == 0)
                return 0*x;
            uxi = i;
        }
        uxi <<= 1;
    }
    i = uxi - uy.i;
    if (i >> 63 == 0) {
        if (i == 0)
            return 0*x;
        uxi = i;
    }
    for (; uxi>>52 == 0; uxi <<= 1, ex--);

    /* scale result */
    if (ex > 0) {
        uxi -= 1ULL << 52;
        uxi |= (uint64_t)ex << 52;
    } else {
        uxi >>= -ex + 1;
    }
    uxi |= (uint64_t)sx << 63;
    ux.i = uxi;
    return ux.f;
}

static long long scanexp(FILE *f, int pok)
{
    int c;
    int x;
    long long y;
    int neg = 0;

    c = shgetc(f);
    if (c=='+' || c=='-') {
        neg = (c=='-');
        c = shgetc(f);
        if (c-'0'>=10U && pok) shunget(f);
    }
    if (c-'0'>=10U) {
        shunget(f);
        return LLONG_MIN;
    }
    for (x=0; c-'0'<10U && x<INT_MAX/10; c = shgetc(f))
        x = 10*x + c-'0';
    for (y=x; c-'0'<10U && y<LLONG_MAX/100; c = shgetc(f))
        y = 10*y + c-'0';
    for (; c-'0'<10U; c = shgetc(f));
    shunget(f);
    return neg ? -y : y;
}

static double decfloat(FILE *f, int c, int bits, int emin, int sign, int pok)
{
    uint32_t x[KMAX];
    static const uint32_t th[] = { LD_B1B_MAX };
    int i, j, k, a, z;
    long long lrp=0, dc=0;
    long long e10=0;
    int lnz = 0;
    int gotdig = 0, gotrad = 0;
    int rp;
    int e2;
    int emax = -emin-bits+3;
    int denormal = 0;
    double y;
    double frac=0;
    double bias=0;
    static const int p10s[] = { 10, 100, 1000, 10000,
        100000, 1000000, 10000000, 100000000 };

    j=0;
    k=0;

    /* Don't let leading zeros consume buffer space */
    for (; c=='0'; c = shgetc(f)) gotdig=1;
    if (c=='.') {
        gotrad = 1;
        for (c = shgetc(f); c=='0'; c = shgetc(f)) gotdig=1, lrp--;
    }

    x[0] = 0;
    for (; c-'0'<10U || c=='.'; c = shgetc(f)) {
        if (c == '.') {
            if (gotrad) break;
            gotrad = 1;
            lrp = dc;
        } else if (k < KMAX-3) {
            dc++;
            if (c!='0') lnz = dc;
            if (j) x[k] = x[k]*10 + c-'0';
            else x[k] = c-'0';
            if (++j==9) {
                k++;
                j=0;
            }
            gotdig=1;
        } else {
            dc++;
            if (c!='0') {
                lnz = (KMAX-4)*9;
                x[KMAX-4] |= 1;
            }
        }
    }
    if (!gotrad) lrp=dc;

    if (gotdig && (c|32)=='e') {
        e10 = scanexp(f, pok);
        if (e10 == LLONG_MIN) {
            if (pok) {
                shunget(f);
            } else {
                shlim(f, 0);
                return 0;
            }
            e10 = 0;
        }
        lrp += e10;
    } else if (c>=0) {
        shunget(f);
    }
    if (!gotdig) {
        errno = EINVAL;
        shlim(f, 0);
        return 0;
    }

    /* Handle zero specially to avoid nasty special cases later */
    if (!x[0]) return sign * 0.0;

    /* Optimize small integers (w/no exponent) and over/under-flow */
    if (lrp==dc && dc<10 && (bits>30 || x[0]>>bits==0))
        return sign * (double)x[0];
    if (lrp > -emin/2) {
        errno = ERANGE;
        return sign * DBL_MAX * DBL_MAX;
    }
    if (lrp < emin-2*DBL_MANT_DIG) {
        errno = ERANGE;
        return sign * DBL_MIN * DBL_MIN;
    }

    /* Align incomplete final B1B digit */
    if (j) {
        for (; j<9; j++) x[k]*=10;
        k++;
        j=0;
    }

    a = 0;
    z = k;
    e2 = 0;
    rp = lrp;

    /* Optimize small to mid-size integers (even in exp. notation) */
    if (lnz<9 && lnz<=rp && rp < 18) {
        if (rp == 9) return sign * (double)x[0];
        if (rp < 9) return sign * (double)x[0] / p10s[8-rp];
        int bitlim = bits-3*(int)(rp-9);
        if (bitlim>30 || x[0]>>bitlim==0)
            return sign * (double)x[0] * p10s[rp-10];
    }

    /* Drop trailing zeros */
    for (; !x[z-1]; z--);

    /* Align radix point to B1B digit boundary */
    if (rp % 9) {
        int rpm9 = rp>=0 ? rp%9 : rp%9+9;
        int p10 = p10s[8-rpm9];
        uint32_t carry = 0;
        for (k=a; k!=z; k++) {
            uint32_t tmp = x[k] % p10;
            x[k] = x[k]/p10 + carry;
            carry = 1000000000/p10 * tmp;
            if (k==a && !x[k]) {
                a = (a+1 & MASK);
                rp -= 9;
            }
        }
        if (carry) x[z++] = carry;
        rp += 9-rpm9;
    }

    /* Upscale until desired number of bits are left of radix point */
    while (rp < 9*LD_B1B_DIG || (rp == 9*LD_B1B_DIG && x[a]<th[0])) {
        uint32_t carry = 0;
        e2 -= 29;
        for (k=(z-1 & MASK); ; k=(k-1 & MASK)) {
            uint64_t tmp = ((uint64_t)x[k] << 29) + carry;
            if (tmp > 1000000000) {
                carry = tmp / 1000000000;
                x[k] = tmp % 1000000000;
            } else {
                carry = 0;
                x[k] = tmp;
            }
            if (k==(z-1 & MASK) && k!=a && !x[k]) z = k;
            if (k==a) break;
        }
        if (carry) {
            rp += 9;
            a = (a-1 & MASK);
            if (a == z) {
                z = (z-1 & MASK);
                x[z-1 & MASK] |= x[z];
            }
            x[a] = carry;
        }
    }

    /* Downscale until exactly number of bits are left of radix point */
    for (;;) {
        uint32_t carry = 0;
        int sh = 1;
        for (i=0; i<LD_B1B_DIG; i++) {
            k = (a+i & MASK);
            if (k == z || x[k] < th[i]) {
                i=LD_B1B_DIG;
                break;
            }
            if (x[a+i & MASK] > th[i]) break;
        }
        if (i==LD_B1B_DIG && rp==9*LD_B1B_DIG) break;
        /* FIXME: find a way to compute optimal sh */
        if (rp > 9+9*LD_B1B_DIG) sh = 9;
        e2 += sh;
        for (k=a; k!=z; k=(k+1 & MASK)) {
            uint32_t tmp = x[k] & (1<<sh)-1;
            x[k] = (x[k]>>sh) + carry;
            carry = (1000000000>>sh) * tmp;
            if (k==a && !x[k]) {
                a = (a+1 & MASK);
                i--;
                rp -= 9;
            }
        }
        if (carry) {
            if ((z+1 & MASK) != a) {
                x[z] = carry;
                z = (z+1 & MASK);
            } else x[z-1 & MASK] |= 1;
        }
    }

    /* Assemble desired bits into floating point variable */
    for (y=i=0; i<LD_B1B_DIG; i++) {
        if ((a+i & MASK)==z) x[(z=(z+1 & MASK))-1] = 0;
        y = 1000000000.0 * y + x[a+i & MASK];
    }

    y *= sign;

    /* Limit precision for denormal results */
    if (bits > DBL_MANT_DIG+e2-emin) {
        bits = DBL_MANT_DIG+e2-emin;
        if (bits<0) bits=0;
        denormal = 1;
    }

    /* Calculate bias term to force rounding, move out lower bits */
    if (bits < DBL_MANT_DIG) {
        bias = copysign(scalbn(1, 2*DBL_MANT_DIG-bits-1), y);
        frac = fmod(y, scalbn(1, DBL_MANT_DIG-bits));
        y -= frac;
        y += bias;
    }

    /* Process tail of decimal input so it can affect rounding */
    if ((a+i & MASK) != z) {
        uint32_t t = x[a+i & MASK];
        if (t < 500000000 && (t || (a+i+1 & MASK) != z))
            frac += 0.25*sign;
        else if (t > 500000000)
            frac += 0.75*sign;
        else if (t == 500000000) {
            if ((a+i+1 & MASK) == z)
                frac += 0.5*sign;
            else
                frac += 0.75*sign;
        }
        if (DBL_MANT_DIG-bits >= 2 && !fmod(frac, 1))
            frac++;
    }

    y += frac;
    y -= bias;

    if ((e2+DBL_MANT_DIG & INT_MAX) > emax-5) {
        if (fabs(y) >= 2/DBL_EPSILON) {
            if (denormal && bits==DBL_MANT_DIG+e2-emin)
                denormal = 0;
            y *= 0.5;
            e2++;
        }
        if (e2+DBL_MANT_DIG>emax || (denormal && frac))
            errno = ERANGE;
    }

    return scalbn(y, e2);
}

static double hexfloat(FILE *f, int bits, int emin, int sign, int pok)
{
    uint32_t x = 0;
    double y = 0;
    double scale = 1;
    double bias = 0;
    int gottail = 0, gotrad = 0, gotdig = 0;
    long long rp = 0;
    long long dc = 0;
    long long e2 = 0;
    int d;
    int c;

    c = shgetc(f);

    /* Skip leading zeros */
    for (; c=='0'; c = shgetc(f)) gotdig = 1;

    if (c=='.') {
        gotrad = 1;
        c = shgetc(f);
        /* Count zeros after the radix point before significand */
        for (rp=0; c=='0'; c = shgetc(f), rp--) gotdig = 1;
    }

    for (; c-'0'<10U || (c|32)-'a'<6U || c=='.'; c = shgetc(f)) {
        if (c=='.') {
            if (gotrad) break;
            rp = dc;
            gotrad = 1;
        } else {
            gotdig = 1;
            if (c > '9') d = (c|32)+10-'a';
            else d = c-'0';
            if (dc<8) {
                x = x*16 + d;
            } else if (dc < DBL_MANT_DIG/4+1) {
                y += d*(scale/=16);
            } else if (d && !gottail) {
                y += 0.5*scale;
                gottail = 1;
            }
            dc++;
        }
    }
    if (!gotdig) {
        shunget(f);
        if (pok) {
            shunget(f);
            if (gotrad) shunget(f);
        } else {
            shlim(f, 0);
        }
        return sign * 0.0;
    }
    if (!gotrad) rp = dc;
    while (dc<8) x *= 16, dc++;
    if ((c|32)=='p') {
        e2 = scanexp(f, pok);
        if (e2 == LLONG_MIN) {
            if (pok) {
                shunget(f);
            } else {
                shlim(f, 0);
                return 0;
            }
            e2 = 0;
        }
    } else {
        shunget(f);
    }
    e2 += 4*rp - 32;

    if (!x) return sign * 0.0;
    if (e2 > -emin) {
        errno = ERANGE;
        return sign * DBL_MAX * DBL_MAX;
    }
    if (e2 < emin-2*DBL_MANT_DIG) {
        errno = ERANGE;
        return sign * DBL_MIN * DBL_MIN;
    }

    while (x < 0x80000000) {
        if (y>=0.5) {
            x += x + 1;
            y += y - 1;
        } else {
            x += x;
            y += y;
        }
        e2--;
    }

    if (bits > 32+e2-emin) {
        bits = 32+e2-emin;
        if (bits<0) bits=0;
    }

    if (bits < DBL_MANT_DIG)
        bias = copysign(scalbn(1, 32+DBL_MANT_DIG-bits-1), sign);

    if (bits<32 && y && !(x&1)) x++, y=0;

    y = bias + sign*(double)x + sign*y;
    y -= bias;

    if (!y) errno = ERANGE;

    return scalbn(y, e2);
}

static double floatscan(FILE *f, int prec, int pok)
{
    int sign = 1;
    size_t i;
    int bits;
    int emin;
    int c;

    switch (prec) {
    case 0:
        bits = FLT_MANT_DIG;
        emin = FLT_MIN_EXP-bits;
        break;
    case 1:
        bits = DBL_MANT_DIG;
        emin = DBL_MIN_EXP-bits;
        break;
    default:
        return 0;
    }

    while (isspace((c=shgetc(f))));

    if (c=='+' || c=='-') {
        sign -= 2*(c=='-');
        c = shgetc(f);
    }

    for (i=0; i<8 && (c|32)=="infinity"[i]; i++)
        if (i<7) c = shgetc(f);
    if (i==3 || i==8 || (i>3 && pok)) {
        if (i!=8) {
            shunget(f);
            if (pok) for (; i>3; i--) shunget(f);
        }
        return sign * INFINITY;
    }
    if (!i) for (i=0; i<3 && (c|32)=="nan"[i]; i++)
        if (i<2) c = shgetc(f);
    if (i==3) {
        if (shgetc(f) != '(') {
            shunget(f);
            return NAN;
        }
        for (i=1; ; i++) {
            c = shgetc(f);
            if (c-'0'<10U || c-'A'<26U || c-'a'<26U || c=='_')
                continue;
            if (c==')') return NAN;
            shunget(f);
            if (!pok) {
                errno = EINVAL;
                shlim(f, 0);
                return 0;
            }
            while (i--) shunget(f);
            return NAN;
        }
        return NAN;
    }

    if (i) {
        shunget(f);
        errno = EINVAL;
        shlim(f, 0);
        return 0;
    }

    if (c=='0') {
        c = shgetc(f);
        if ((c|32) == 'x')
            return hexfloat(f, bits, emin, sign, pok);
        shunget(f);
        c = '0';
    }

    return decfloat(f, c, bits, emin, sign, pok);
}

static double strtox(const char *s, char **p, int prec)
{
    FILE f;
    sh_fromstring(&f, s);
    shlim(&f, 0);
    double y = floatscan(&f, prec, 1);
    if (p) {
        size_t cnt = shcnt(&f);
        *p = (char *)s + cnt;
    }
    return y;
}

float strtof(const char *restrict s, char **restrict p)
{
    return strtox(s, p, 0);
}

double strtod(const char *restrict s, char **restrict p)
{
    return strtox(s, p, 1);
}

// ============================================================================

/* Lookup table for digit values. -1==255>=36 -> invalid */
static const unsigned char table[] = { -1,
-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,-1,-1,-1,-1,-1,-1,
-1,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,
25,26,27,28,29,30,31,32,33,34,35,-1,-1,-1,-1,-1,
-1,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,
25,26,27,28,29,30,31,32,33,34,35,-1,-1,-1,-1,-1,
-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
};

static unsigned long long intscan(FILE *f, unsigned base, int pok, unsigned long long lim)
{
    const unsigned char *val = table+1;
    int c, neg=0;
    unsigned x;
    unsigned long long y;
    if (base > 36 || base == 1) {
        errno = EINVAL;
        return 0;
    }
    while (isspace((c=shgetc(f))));
    if (c=='+' || c=='-') {
        neg = -(c=='-');
        c = shgetc(f);
    }
    if ((base == 0 || base == 16) && c=='0') {
        c = shgetc(f);
        if ((c|32)=='x') {
            c = shgetc(f);
            if (val[c]>=16) {
                shunget(f);
                if (pok) shunget(f);
                else shlim(f, 0);
                return 0;
            }
            base = 16;
        } else if (base == 0) {
            base = 8;
        }
    } else {
        if (base == 0) base = 10;
        if (val[c] >= base) {
            shunget(f);
            shlim(f, 0);
            errno = EINVAL;
            return 0;
        }
    }
    if (base == 10) {
        for (x=0; c-'0'<10U && x<=UINT_MAX/10-1; c=shgetc(f))
            x = x*10 + (c-'0');
        for (y=x; c-'0'<10U && y<=ULLONG_MAX/10 && 10*y<=ULLONG_MAX-(c-'0'); c=shgetc(f))
            y = y*10 + (c-'0');
        if (c-'0'>=10U) goto done;
    } else if (!(base & base-1)) {
        int bs = "\0\1\2\4\7\3\6\5"[(0x17*base)>>5&7];
        for (x=0; val[c]<base && x<=UINT_MAX/32; c=shgetc(f))
            x = x<<bs | val[c];
        for (y=x; val[c]<base && y<=ULLONG_MAX>>bs; c=shgetc(f))
            y = y<<bs | val[c];
    } else {
        for (x=0; val[c]<base && x<=UINT_MAX/36-1; c=shgetc(f))
            x = x*base + val[c];
        for (y=x; val[c]<base && y<=lim/base && base*y<=lim-val[c]; c=shgetc(f))
            y = y*base + val[c];
    }
    if (val[c]<base) {
        for (; val[c]<base; c=shgetc(f));
        errno = ERANGE;
        y = lim;
        if (lim&1) neg = 0;
    }
done:
    shunget(f);
    if (y>=lim) {
        if (!(lim&1) && !neg) {
            errno = ERANGE;
            return lim-1;
        } else if (y>lim) {
            errno = ERANGE;
            return lim;
        }
    }
    return (y^neg)-neg;
}

static unsigned long long strtoxl(const char *s, char **p, int base, unsigned long long lim)
{
    FILE f;
    sh_fromstring(&f, s);
    shlim(&f, 0);
    unsigned long long y = intscan(&f, base, 1, lim);
    if (p) {
        size_t cnt = shcnt(&f);
        *p = (char *)s + cnt;
    }
    return y;
}

unsigned long long strtoull(const char *restrict s, char **restrict p, int base)
{
    return strtoxl(s, p, base, ULLONG_MAX);
}

long long strtoll(const char *restrict s, char **restrict p, int base)
{
    return strtoxl(s, p, base, LLONG_MIN);
}

unsigned long strtoul(const char *restrict s, char **restrict p, int base)
{
    return strtoxl(s, p, base, ULONG_MAX);
}

long strtol(const char *restrict s, char **restrict p, int base)
{
    return strtoxl(s, p, base, 0UL+LONG_MIN);
}
