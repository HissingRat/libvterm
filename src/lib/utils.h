#ifndef LIBVTERM_NVIM_UTILS_H
#define LIBVTERM_NVIM_UTILS_H

#include <stdarg.h>
#include <stddef.h>

#if defined(__GNUC__)
#define VT_UTILS_INTERNAL __attribute__((visibility("hidden")))
#define VT_PRINTF_FORMAT(a, b) __attribute__((format(printf, a, b)))
#else
#define VT_UTILS_INTERNAL
#define VT_PRINTF_FORMAT(a, b)
#endif

VT_UTILS_INTERNAL void *vt_memcpy(void *dest, const void *src, size_t count);
VT_UTILS_INTERNAL void *vt_memmove(void *dest, const void *src, size_t count);
VT_UTILS_INTERNAL void *vt_memset(void *dest, int value, size_t count);
VT_UTILS_INTERNAL char *vt_strncpy(char *dest, const char *src, size_t count);
VT_UTILS_INTERNAL int vt_strncmp(const char *a, const char *b, size_t count);

/* Like abs(), INT_MIN is outside the defined input domain. */
VT_UTILS_INTERNAL int vt_abs(int value);

/* C-locale, byte-oriented printf subset: d i u o x X c s and %.
 * Integer lengths: hh h l ll j z t. Flags, width, precision and * follow
 * snprintf semantics, including the would-have-written return length.
 * size == 0 permits buffer == NULL. A nonempty buffer is NUL-terminated.
 * Unsupported/invalid formats or a result exceeding INT_MAX return -1;
 * errno is not used. Floating point, wide characters, %p and %n are excluded.
 */
VT_UTILS_INTERNAL int vt_snprintf(char *buffer, size_t size,
                                const char *format, ...) VT_PRINTF_FORMAT(3, 4);
VT_UTILS_INTERNAL int vt_vsnprintf(char *buffer, size_t size,
                                 const char *format, va_list args) VT_PRINTF_FORMAT(3, 0);

#undef VT_UTILS_INTERNAL
#undef VT_PRINTF_FORMAT

#endif
