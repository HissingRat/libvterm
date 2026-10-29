#include "utils.h"

#include <limits.h>
#include <stdint.h>

void *vt_memcpy(void *dest, const void *src, size_t count) {
    /* Volatile byte stores prevent loop-to-libc transformations, even when
     * this file is built by a hosted compiler with builtin optimizations. */
    volatile unsigned char *out = dest;
    const unsigned char *in = src;
    for (size_t i = 0; i < count; i++)
        out[i] = in[i];
    return dest;
}

void *vt_memmove(void *dest, const void *src, size_t count) {
    volatile unsigned char *out = dest;
    const unsigned char *in = src;
    if (dest == src || count == 0)
        return dest;

    /* Relational pointer comparisons are undefined for unrelated objects.
     * uintptr_t gives an address ordering on our flat-address targets. */
    if ((uintptr_t)dest < (uintptr_t)src) {
        for (size_t i = 0; i < count; i++)
            out[i] = in[i];
    } else {
        while (count) {
            count--;
            out[count] = in[count];
        }
    }
    return dest;
}

void *vt_memset(void *dest, int value, size_t count) {
    volatile unsigned char *out = dest;
    for (size_t i = 0; i < count; i++)
        out[i] = (unsigned char)value;
    return dest;
}

char *vt_strncpy(char *dest, const char *src, size_t count) {
    volatile char *out = dest;
    size_t i = 0;
    for (; i < count && src[i]; i++)
        out[i] = src[i];
    for (; i < count; i++)
        out[i] = '\0';
    return dest;
}

int vt_strncmp(const char *a, const char *b, size_t count) {
    for (size_t i = 0; i < count; i++) {
        unsigned char left = (unsigned char)a[i];
        unsigned char right = (unsigned char)b[i];
        if (left != right)
            return (int)left - (int)right;
        if (left == 0)
            break;
    }
    return 0;
}

int vt_abs(int value) {
    return value < 0 ? -value : value;
}

typedef struct {
    char *buffer;
    size_t size;
    size_t written;
    size_t length;
    int failed;
} Writer;

static void append(Writer *writer, const char *bytes, size_t count) {
    if (writer->failed)
        return;
    if (count > (size_t)INT_MAX - writer->length) {
        writer->failed = 1;
        return;
    }
    size_t available = writer->size ? writer->size - 1 - writer->written : 0;
    size_t copy = count < available ? count : available;
    if (copy) {
        vt_memcpy(writer->buffer + writer->written, bytes, copy);
        writer->written += copy;
    }
    writer->length += count;
}

static void repeat(Writer *writer, char byte, size_t count) {
    if (writer->failed)
        return;
    if (count > (size_t)INT_MAX - writer->length) {
        writer->failed = 1;
        return;
    }
    size_t available = writer->size ? writer->size - 1 - writer->written : 0;
    size_t copy = count < available ? count : available;
    if (copy) {
        vt_memset(writer->buffer + writer->written, byte, copy);
        writer->written += copy;
    }
    writer->length += count;
}

static int decimal(const char **format, int *result) {
    int value = 0;
    while (**format >= '0' && **format <= '9') {
        int digit = *(*format)++ - '0';
        if (value > (INT_MAX - digit) / 10)
            return 0;
        value = value * 10 + digit;
    }
    *result = value;
    return 1;
}

typedef enum { LEN_NONE, LEN_HH, LEN_H, LEN_L, LEN_LL, LEN_J, LEN_Z, LEN_T } Length;

static intmax_t signed_arg(va_list *args, Length length) {
    switch (length) {
        case LEN_HH: return (signed char)va_arg(*args, int);
        case LEN_H:  return (short)va_arg(*args, int);
        case LEN_L:  return va_arg(*args, long);
        case LEN_LL: return va_arg(*args, long long);
        case LEN_J:  return va_arg(*args, intmax_t);
        case LEN_Z:
            /* Signed counterpart of size_t on ILP32, LP64 and LLP64 ABIs. */
            if (SIZE_MAX == UINT_MAX) return va_arg(*args, int);
            if (SIZE_MAX == ULONG_MAX) return va_arg(*args, long);
            return va_arg(*args, long long);
        case LEN_T: return va_arg(*args, ptrdiff_t);
        default:    return va_arg(*args, int);
    }
}

static uintmax_t unsigned_arg(va_list *args, Length length) {
    switch (length) {
        case LEN_HH: return (unsigned char)va_arg(*args, unsigned int);
        case LEN_H:  return (unsigned short)va_arg(*args, unsigned int);
        case LEN_L:  return va_arg(*args, unsigned long);
        case LEN_LL: return va_arg(*args, unsigned long long);
        case LEN_J:  return va_arg(*args, uintmax_t);
        case LEN_Z:  return va_arg(*args, size_t);
        case LEN_T:
            if (PTRDIFF_MAX == INT_MAX) return va_arg(*args, unsigned int);
            if (PTRDIFF_MAX == LONG_MAX) return va_arg(*args, unsigned long);
            return va_arg(*args, unsigned long long);
        default: return va_arg(*args, unsigned int);
    }
}

static void number(Writer *writer, uintmax_t value, char sign, unsigned int base,
                   int upper, int alternate, int left, int zero, int width, int precision) {
    char reversed[sizeof(uintmax_t) * CHAR_BIT];
    const char *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    size_t count = 0;
    uintmax_t remaining = value;
    if (value || precision != 0) {
        do {
            reversed[count++] = digits[remaining % base];
            remaining /= base;
        } while (remaining);
    }

    size_t min_digits = precision < 0 ? 1 : (size_t)precision;
    if (alternate && base == 8 && (value || count == 0) && min_digits <= count)
        min_digits = count + 1;
    size_t zeros = min_digits > count ? min_digits - count : 0;
    size_t prefix = (sign != 0) + (alternate && base == 16 && value ? 2 : 0);
    size_t total = prefix + zeros + count;
    size_t padding = (size_t)width > total ? (size_t)width - total : 0;
    if (!left && !(zero && precision < 0))
        repeat(writer, ' ', padding);
    if (sign)
        append(writer, &sign, 1);
    if (alternate && base == 16 && value)
        append(writer, upper ? "0X" : "0x", 2);
    if (!left && zero && precision < 0)
        repeat(writer, '0', padding);
    repeat(writer, '0', zeros);
    while (count)
        append(writer, &reversed[--count], 1);
    if (left)
        repeat(writer, ' ', padding);
}

int vt_vsnprintf(char *buffer, size_t size, const char *format, va_list args) {
    Writer writer = { .buffer = buffer, .size = size };
    va_list copy;
    va_copy(copy, args);

    while (*format && !writer.failed) {
        const char *literal = format;
        while (*format && *format != '%')
            format++;
        append(&writer, literal, (size_t)(format - literal));
        if (!*format || writer.failed)
            break;
        format++;
        if (*format == '%') {
            append(&writer, format++, 1);
            continue;
        }

        int left = 0, plus = 0, space = 0, alternate = 0, zero = 0;
        for (;;) {
            if (*format == '-') left = 1;
            else if (*format == '+') plus = 1;
            else if (*format == ' ') space = 1;
            else if (*format == '#') alternate = 1;
            else if (*format == '0') zero = 1;
            else break;
            format++;
        }

        int width = 0, precision = -1;
        if (*format == '*') {
            format++;
            width = va_arg(copy, int);
            if (width == INT_MIN) {
                writer.failed = 1;
                break;
            }
            if (width < 0) {
                left = 1;
                width = -width;
            }
        } else if (!decimal(&format, &width)) {
            writer.failed = 1;
            break;
        }
        if (*format == '.') {
            format++;
            if (*format == '*') {
                format++;
                precision = va_arg(copy, int);
                if (precision < 0)
                    precision = -1;
            } else if (!decimal(&format, &precision)) {
                writer.failed = 1;
                break;
            }
        }

        Length length = LEN_NONE;
        switch (*format) {
            case 'h': length = *++format == 'h' ? (format++, LEN_HH) : LEN_H; break;
            case 'l': length = *++format == 'l' ? (format++, LEN_LL) : LEN_L; break;
            case 'j': length = LEN_J; format++; break;
            case 'z': length = LEN_Z; format++; break;
            case 't': length = LEN_T; format++; break;
        }
        char spec = *format;
        if (spec)
            format++;
        if (spec == 'd' || spec == 'i' || spec == 'u' || spec == 'o' || spec == 'x' || spec == 'X') {
            char sign = 0;
            uintmax_t value;
            if (spec == 'd' || spec == 'i') {
                intmax_t signed_value = signed_arg(&copy, length);
                if (signed_value < 0) {
                    sign = '-';
                    value = (uintmax_t)(-(signed_value + 1)) + 1;
                } else {
                    sign = plus ? '+' : space ? ' ' : 0;
                    value = (uintmax_t)signed_value;
                }
                alternate = 0;
            } else {
                value = unsigned_arg(&copy, length);
            }
            number(&writer, value, sign, spec == 'o' ? 8 : (spec == 'x' || spec == 'X') ? 16 : 10,
                   spec == 'X', alternate, left, zero, width, precision);
        } else if ((spec == 's' || spec == 'c') && length == LEN_NONE) {
            char character;
            const char *text;
            size_t count = 0;
            if (spec == 'c') {
                character = (unsigned char)va_arg(copy, int);
                text = &character;
                count = 1;
            } else {
                text = va_arg(copy, const char *);
                while ((precision < 0 || count < (size_t)precision) && text[count])
                    count++;
            }
            size_t padding = (size_t)width > count ? (size_t)width - count : 0;
            if (!left)
                repeat(&writer, ' ', padding);
            append(&writer, text, count);
            if (left)
                repeat(&writer, ' ', padding);
        } else {
            writer.failed = 1;
        }
    }
    va_end(copy);
    if (size)
        buffer[writer.written] = '\0';
    return writer.failed ? -1 : (int)writer.length;
}

int vt_snprintf(char *buffer, size_t size, const char *format, ...) {
    va_list args;
    va_start(args, format);
    int result = vt_vsnprintf(buffer, size, format, args);
    va_end(args);
    return result;
}
