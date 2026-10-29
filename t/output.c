#include "vterm.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char received[64];
static size_t received_len;
static unsigned int checks;

#define CHECK(expr) do { \
    checks++; \
    if (!(expr)) { \
        fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expr); \
        exit(1); \
    } \
} while (0)

static void output(const char *bytes, size_t len, void *user) {
    (void)user;
    CHECK(len <= sizeof(received) - received_len);
    memcpy(received + received_len, bytes, len);
    received_len += len;
}

static void expect(VTerm *vt, int callback, size_t capacity, const char *bytes, size_t len) {
    if (!callback)
        received_len = vterm_output_read(vt, received, sizeof(received));
    size_t expected = capacity > len ? len : 0;
    CHECK(received_len == expected);
    CHECK(memcmp(received, bytes, expected) == 0);
    received_len = 0;
}

int main(void) {
    for (int callback = 0; callback <= 1; callback++) {
        for (size_t capacity = 1; capacity <= 32; capacity++) {
            VTerm *vt = vterm_build(&(struct VTermBuilder){
                .rows = 2, .cols = 8, .tmpbuffer_len = capacity,
            });
            CHECK(vt != NULL);
            VTermState *state = vterm_obtain_state(vt);
            CHECK(state != NULL);
            vterm_state_reset(state, 1);
            if (callback)
                vterm_output_set_callback(vt, output, NULL);

            vterm_keyboard_unichar(vt, 'a', VTERM_MOD_ALT);
            expect(vt, callback, capacity, "\x1b" "a", 2);
            vterm_keyboard_unichar(vt, ' ', VTERM_MOD_CTRL);
            expect(vt, callback, capacity, "\0", 1);
            vterm_keyboard_unichar(vt, ' ', VTERM_MOD_CTRL | VTERM_MOD_ALT);
            expect(vt, callback, capacity, "\x1b\0", 2);
            vterm_keyboard_key(vt, VTERM_KEY_UP, VTERM_MOD_ALT);
            expect(vt, callback, capacity, "\x1b[1;3A", 6);
            CHECK(vterm_input_write(vt, "\x1b[6n", 4) == 4);
            expect(vt, callback, capacity, "\x1b[1;1R", 6);
            vterm_free(vt);
        }
    }
    printf("output: %u checks passed (small buffers and embedded NUL)\n", checks);
    return 0;
}
