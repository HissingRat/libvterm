#include "lib/utils.h"
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

static unsigned long checks;

#define CHECK(expr) do { \
    checks++; \
    if (!(expr)) { \
        fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expr); \
        exit(1); \
    } \
} while (0)

static int sign(int value) {
    return (value > 0) - (value < 0);
}

static void test_memory(void) {
    unsigned char source[96], actual[96], expected[96];
    for (size_t i = 0; i < sizeof(source); i++)
        source[i] = (unsigned char)(i * 71 + 129);

    for (size_t offset = 0; offset < 16; offset++) {
        for (size_t count = 0; count <= sizeof(source) - offset; count++) {
            memset(actual, 0xa5, sizeof(actual));
            memcpy(expected, actual, sizeof(actual));
            CHECK(vt_memcpy(actual + offset, source, count) == actual + offset);
            memcpy(expected + offset, source, count);
            CHECK(memcmp(actual, expected, sizeof(actual)) == 0);

            const int values[] = {0, 1, 127, 128, 255, 256, -1, INT_MIN, INT_MAX};
            for (size_t v = 0; v < sizeof(values) / sizeof(values[0]); v++) {
                memcpy(actual, source, sizeof(actual));
                memcpy(expected, source, sizeof(expected));
                CHECK(vt_memset(actual + offset, values[v], count) == actual + offset);
                memset(expected + offset, values[v], count);
                CHECK(memcmp(actual, expected, sizeof(actual)) == 0);
            }
        }
    }

    for (size_t from = 0; from < 32; from++) {
        for (size_t to = 0; to < 32; to++) {
            size_t maximum = sizeof(actual) - (from > to ? from : to);
            for (size_t count = 0; count <= maximum; count++) {
                memcpy(actual, source, sizeof(actual));
                memcpy(expected, source, sizeof(expected));
                CHECK(vt_memmove(actual + to, actual + from, count) == actual + to);
                memmove(expected + to, expected + from, count);
                CHECK(memcmp(actual, expected, sizeof(actual)) == 0);
            }
        }
    }
    CHECK(vt_memmove(actual, source, sizeof(actual)) == actual);
    CHECK(memcmp(actual, source, sizeof(actual)) == 0);
}

static void test_strings(void) {
    const char *strings[] = {"", "a", "abc", "abcdef", "abc\0ignored", "\x80\xff", "\x7f\xff"};
    char actual[32], expected[32];
    for (size_t s = 0; s < sizeof(strings) / sizeof(strings[0]); s++) {
        for (size_t count = 0; count <= sizeof(actual); count++) {
            memset(actual, 0xa5, sizeof(actual));
            memset(expected, 0xa5, sizeof(expected));
            CHECK(vt_strncpy(actual, strings[s], count) == actual);
            strncpy(expected, strings[s], count);
            CHECK(memcmp(actual, expected, sizeof(actual)) == 0);
            for (size_t t = 0; t < sizeof(strings) / sizeof(strings[0]); t++)
                CHECK(sign(vt_strncmp(strings[s], strings[t], count)) ==
                      sign(strncmp(strings[s], strings[t], count)));
        }
    }
    /* Exact-sized non-NUL sources: neither function may read past count. */
    char exact[] = {'x', 'y', 'z'};
    CHECK(vt_strncpy(actual, exact, sizeof(exact)) == actual);
    CHECK(memcmp(actual, exact, sizeof(exact)) == 0);
    CHECK(vt_strncmp(exact, "xyzmore", sizeof(exact)) == 0);
    const int numbers[] = {INT_MIN + 1, -32768, -1, 0, 1, 32767, INT_MAX};
    for (size_t i = 0; i < sizeof(numbers) / sizeof(numbers[0]); i++)
        CHECK(vt_abs(numbers[i]) == abs(numbers[i]));
}

static void compare_format(const char *format, ...) {
    const size_t sizes[] = {0, 1, 2, 3, 4, 8, 16, 31, 64, 256};
    char actual[256], expected[256];
    va_list args;
    va_start(args, format);
    for (size_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
        va_list libc_args, utils_args;
        va_copy(libc_args, args);
        va_copy(utils_args, args);
        memset(actual, 0xa5, sizeof(actual));
        memset(expected, 0xa5, sizeof(expected));
        int want = vsnprintf(expected, sizes[i], format, libc_args);
        int got = vt_vsnprintf(actual, sizes[i], format, utils_args);
        va_end(libc_args);
        va_end(utils_args);
        if (want != got || memcmp(actual, expected, sizeof(actual))) {
            fprintf(stderr, "format=%s size=%zu expected length=%d actual length=%d\n",
                    format, sizes[i], want, got);
            for (size_t byte = 0; byte < sizes[i]; byte++)
                fprintf(stderr, " byte %zu: libc=%02x utils=%02x\n", byte,
                        (unsigned char)expected[byte], (unsigned char)actual[byte]);
            exit(1);
        }
        checks += 2;
    }
    va_list libc_args;
    va_copy(libc_args, args);
    int want = vsnprintf(NULL, 0, format, libc_args);
    va_end(libc_args);
    CHECK(vt_vsnprintf(NULL, 0, format, args) == want);
    /* vt_vsnprintf must not consume the caller's va_list. */
    CHECK(vt_vsnprintf(NULL, 0, format, args) == want);
    va_end(args);
}

static void test_format(void) {
    const int signed_values[] = {INT_MIN, -1234567, -1, 0, 1, 9, 10, 255, INT_MAX};
    const unsigned int unsigned_values[] = {0, 1, 7, 8, 15, 16, 255, 65535, UINT_MAX};
    const char *widths[] = {"", "1", "2", "8", "24"};
    const char *precisions[] = {"", ".", ".0", ".1", ".8", ".24"};
    for (unsigned int flags = 0; flags < 16; flags++) {
        char flag_text[5];
        size_t f = 0;
        if (flags & 1) flag_text[f++] = '-';
        if (flags & 2) flag_text[f++] = '+';
        if (flags & 4) flag_text[f++] = ' ';
        if (flags & 8) flag_text[f++] = '0';
        flag_text[f] = 0;
        for (size_t w = 0; w < sizeof(widths) / sizeof(widths[0]); w++) {
            for (size_t p = 0; p < sizeof(precisions) / sizeof(precisions[0]); p++) {
                char format[48];
                for (size_t v = 0; v < sizeof(signed_values) / sizeof(signed_values[0]); v++) {
                    snprintf(format, sizeof(format), "pre:%%%s%s%sd:end", flag_text, widths[w], precisions[p]);
                    compare_format(format, signed_values[v]);
                    snprintf(format, sizeof(format), "pre:%%%s%s%si:end", flag_text, widths[w], precisions[p]);
                    compare_format(format, signed_values[v]);
                }
            }
        }
    }
    for (unsigned int flags = 0; flags < 8; flags++) {
        for (const char *spec = "uoxX"; *spec; spec++) {
            if (*spec == 'u' && (flags & 2)) continue;
            char flag_text[4];
            size_t f = 0;
            if (flags & 1) flag_text[f++] = '-';
            if (flags & 2) flag_text[f++] = '#';
            if (flags & 4) flag_text[f++] = '0';
            flag_text[f] = 0;
            for (size_t w = 0; w < sizeof(widths) / sizeof(widths[0]); w++) {
                for (size_t p = 0; p < sizeof(precisions) / sizeof(precisions[0]); p++) {
                    char format[48];
                    snprintf(format, sizeof(format), "pre:%%%s%s%s%c:end", flag_text, widths[w], precisions[p], *spec);
                    for (size_t v = 0; v < sizeof(unsigned_values) / sizeof(unsigned_values[0]); v++)
                        compare_format(format, unsigned_values[v]);
                }
            }
        }
    }

    compare_format("%hhd %hhi %hhu %hho %hhx %hhX", SCHAR_MIN, SCHAR_MAX, UCHAR_MAX, UCHAR_MAX, UCHAR_MAX, UCHAR_MAX);
    compare_format("%hd %hi %hu %ho %hx %hX", SHRT_MIN, SHRT_MAX, USHRT_MAX, USHRT_MAX, USHRT_MAX, USHRT_MAX);
    compare_format("%ld %li %lu %lo %lx %lX", LONG_MIN, LONG_MAX, ULONG_MAX, ULONG_MAX, ULONG_MAX, ULONG_MAX);
    compare_format("%lld %lli %llu %llo %llx %llX", LLONG_MIN, LLONG_MAX, ULLONG_MAX, ULLONG_MAX, ULLONG_MAX, ULLONG_MAX);
    compare_format("%jd %ji %ju %jo %jx %jX", INTMAX_MIN, INTMAX_MAX, UINTMAX_MAX, UINTMAX_MAX, UINTMAX_MAX, UINTMAX_MAX);
    compare_format("%zd %zi %zu %zo %zx %zX", (ssize_t)-123, (ssize_t)123, SIZE_MAX, SIZE_MAX, SIZE_MAX, SIZE_MAX);
    compare_format("%td %ti %tu %to %tx %tX", PTRDIFF_MIN, PTRDIFF_MAX, (uintptr_t)123, (uintptr_t)123, (uintptr_t)123, (uintptr_t)123);
    compare_format("%hhd %hhu %hd %hu", 255, 256u, 65535, 65536u);
    compare_format("literal %% percent\n");
    compare_format("");

    const int dynamic[] = {-24, -1, 0, 1, 8, 24};
    for (size_t w = 0; w < sizeof(dynamic) / sizeof(dynamic[0]); w++) {
        for (size_t p = 0; p < sizeof(dynamic) / sizeof(dynamic[0]); p++) {
            compare_format("%+0*.*d/%#*.*x", dynamic[w], dynamic[p], INT_MIN, dynamic[w], dynamic[p], UINT_MAX);
            compare_format("%*.*s", dynamic[w], dynamic[p], "abcdefghijklmnop");
            compare_format("%-*.*s", dynamic[w], dynamic[p], "");
        }
        compare_format("x%*cy", dynamic[w], 0);
        compare_format("x%-*cy", dynamic[w], 255);
    }
    char exact[] = {'a', 'b', 'c'};
    compare_format("%.3s", exact);
    compare_format("%.*s", 0, exact);

    /* Direct variadic entry point, embedded NUL, truncation and size zero. */
    char actual[32], expected[32];
    for (size_t size = 0; size <= sizeof(actual); size++) {
        memset(actual, 0xa5, sizeof(actual));
        memset(expected, 0xa5, sizeof(expected));
        CHECK(vt_snprintf(actual, size, "%s%c%+08d/%#x", "a", 0, -42, 255u) ==
              snprintf(expected, size, "%s%c%+08d/%#x", "a", 0, -42, 255u));
        CHECK(memcmp(actual, expected, sizeof(actual)) == 0);
    }
    CHECK(vt_snprintf(NULL, 0, "%s:%d", "abc", 42) == 6);
    /* C99: any negative dynamic precision is omitted. Darwin's libc has an
     * INT_MIN precision bug, so verify this boundary against the specified
     * result instead of copying that implementation's integer overflow. */
    CHECK(vt_snprintf(actual, sizeof(actual), "%.*d", INT_MIN, 42) == 2);
    CHECK(strcmp(actual, "42") == 0);
    CHECK(vt_snprintf(actual, sizeof(actual), "%.*s", INT_MIN, "abc") == 3);
    CHECK(strcmp(actual, "abc") == 0);
    /* Very large padding must count in constant time with a short buffer. */
    CHECK(vt_snprintf(actual, sizeof(actual), "%*s", INT_MAX, "") == INT_MAX);
    CHECK(actual[sizeof(actual) - 1] == '\0');
    CHECK(vt_snprintf(actual, sizeof(actual), "%.*d", INT_MAX, 0) == INT_MAX);
    CHECK(vt_snprintf(actual, sizeof(actual), "%*s!", INT_MAX, "") == -1);
    CHECK(vt_snprintf(actual, sizeof(actual), "%*s", INT_MIN, "") == -1);
    CHECK(vt_snprintf(actual, sizeof(actual), "%+.*d", INT_MAX, 0) == -1);

    /* Invalid/unsupported forms have a documented deterministic error. */
    const char *invalid[] = {"%", "%q", "%f", "%ls", "%lc", "%n", "%p", "%2147483648d", "%.2147483648s", "%2$d"};
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
        actual[0] = 'x';
        CHECK(vt_snprintf(actual, sizeof(actual), invalid[i], 0) == -1);
        CHECK(actual[0] == '\0');
    }
}

int main(void) {
    test_memory();
    test_strings();
    test_format();
    printf("utils: %lu checks passed (compared with host libc)\n", checks);
    return 0;
}
