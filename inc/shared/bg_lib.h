#pragma once

#define EINVAL 1
#define ERANGE 2
#define EOVERFLOW 3

extern int errno;

long strtol(const char *nptr, char **endptr, int base);
long long strtoll(const char *nptr, char **endptr, int base);
unsigned long strtoul(const char *nptr, char **endptr, int base);
unsigned long long strtoull(const char *nptr, char **endptr, int base);
float strtof(const char *nptr, char **endptr);
double strtod(const char *nptr, char **endptr);

int strcmp(const char *s1, const char *s2);
int strncmp(const char *s1, const char *s2, size_t n);
char *strcpy(char *dst, const char *src);
char *strcat(char *dst, const char *src);
size_t strlen(const char *s);
char *strstr(const char *haystack, const char *needle);
char *strchr(const char *s, int c);
char *strrchr(const char *s, int c);
void *memchr(const void *s, int c, size_t n);

#define PRId64  "lld"
#define PRIx64  "llx"

int sprintf(char *dst, const char *fmt, ...);
int vsnprintf(char *str, size_t size, const char *fmt, va_list ap);

void qsort(void *base, size_t nmemb, size_t size, int (*compar)(const void *, const void *));

void *memcpy(void *dst, const void *src, size_t n);
void *memset(void *dst, int src, size_t n);
int memcmp(const void *s1, const void *s2, size_t n);

#define isnan(f)    __builtin_isnan(f)
#define isfinite(f) __builtin_isfinite(f)
#define signbit(f)  __builtin_signbit(f)

int abs(int i);
double fabs(double f);
double copysign(double x, double y);
float fabsf(float f);
float sinf(float f);
float cosf(float f);
float tanf(float f);
float atan2f(float y, float x);
float acosf(float f);
float sqrtf(float f);
float floorf(float f);
float ceilf(float f);
float truncf(float f);
