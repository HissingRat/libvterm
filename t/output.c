#include "../support/host.h"
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

static VTermStatus output(const char *bytes, size_t len, void *user) {
    (void)user;
    CHECK(len <= sizeof(received) - received_len);
    memcpy(received + received_len, bytes, len);
    received_len += len;
    return VTERM_OK;
}

static void expect(VTerm *vt, int callback, size_t capacity, const char *bytes, size_t len) {
    if (!callback)
        received_len = vterm_output_read(vt, received, sizeof(received));
    size_t expected = capacity > len ? len : 0;
    CHECK(received_len == expected);
    CHECK(memcmp(received, bytes, expected) == 0);
    received_len = 0;
}

typedef struct {
    void *blocks[32];
    void *last;
    size_t live;
    int fail;
} Allocations;

static void *allocate(size_t size, void *user) {
    Allocations *allocations = user;
    if (allocations->fail)
        return NULL;
    for (size_t i = 0; i < sizeof(allocations->blocks) / sizeof(allocations->blocks[0]); i++) {
        if (!allocations->blocks[i]) {
            void *block = calloc(1, size);
            CHECK(block != NULL);
            allocations->blocks[i] = block;
            allocations->last = block;
            allocations->live++;
            return block;
        }
    }
    CHECK(0);
    return NULL;
}

static void release(void *block, void *user) {
    Allocations *allocations = user;
    if (!block)
        return;
    for (size_t i = 0; i < sizeof(allocations->blocks) / sizeof(allocations->blocks[0]); i++) {
        if (allocations->blocks[i] == block) {
            allocations->blocks[i] = NULL;
            allocations->live--;
            free(block);
            return;
        }
    }
    CHECK(0); /* A borrowed buffer must never be freed by libvterm. */
}

static void test_selection_ownership(void) {
    const VTermAllocatorFunctions allocator = { .malloc = allocate, .free = release };
    for (int borrowed_at_exit = 0; borrowed_at_exit <= 1; borrowed_at_exit++) {
        Allocations allocations = {0};
        VTerm *vt = host_build((struct VTermBuilder){
            .rows = 2, .cols = 8, .allocator = &allocator, .allocdata = &allocations,
        });
        CHECK(vt != NULL);
        VTermState *state = host_state(vt);
        CHECK(state != NULL);
        size_t base = allocations.live;
        vterm_state_set_selection_callbacks(state, NULL, NULL, NULL, 16);
        CHECK(allocations.live == base + 1);
        vterm_state_set_selection_callbacks(state, NULL, NULL, NULL, 32);
        CHECK(allocations.live == base + 1);
        /* Re-registering the same pointer preserves library ownership. */
        vterm_state_set_selection_callbacks(state, NULL, NULL, allocations.last, 32);
        CHECK(allocations.live == base + 1);
        allocations.fail = 1;
        CHECK(vterm_state_set_selection_callbacks(state, NULL, NULL, NULL, 64) == VTERM_ERR_NO_MEMORY);
        CHECK(allocations.live == base + 1);
        allocations.fail = 0;
        char borrowed[16];
        vterm_state_set_selection_callbacks(state, NULL, NULL, borrowed, sizeof(borrowed));
        CHECK(allocations.live == base);
        vterm_state_set_selection_callbacks(state, NULL, NULL, NULL, 16);
        CHECK(allocations.live == base + 1);
        vterm_state_set_selection_callbacks(state, NULL, NULL, NULL, 0);
        CHECK(allocations.live == base);
        vterm_state_set_selection_callbacks(state, NULL, NULL,
                                            borrowed_at_exit ? borrowed : NULL, 16);
        vterm_free(vt);
        CHECK(allocations.live == 0);
    }
}

int main(void) {
    for (int callback = 0; callback <= 1; callback++) {
        for (size_t capacity = 1; capacity <= 32; capacity++) {
            VTerm *vt = host_build((struct VTermBuilder){
                .rows = 2, .cols = 8, .tmpbuffer_len = capacity,
            });
            CHECK(vt != NULL);
            VTermState *state = host_state(vt);
            CHECK(state != NULL);
            CHECK(vterm_state_reset(state, 1) == VTERM_OK);
            if (callback)
                vterm_output_set_callback(vt, output, NULL);

            CHECK(vterm_keyboard_unichar(vt, 'a', VTERM_MOD_ALT) == (capacity > 2 ? VTERM_OK : VTERM_ERR_NO_SPACE));
            expect(vt, callback, capacity, "\x1b" "a", 2);
            CHECK(vterm_keyboard_unichar(vt, ' ', VTERM_MOD_CTRL) == (capacity > 1 ? VTERM_OK : VTERM_ERR_NO_SPACE));
            expect(vt, callback, capacity, "\0", 1);
            CHECK(vterm_keyboard_unichar(vt, ' ', VTERM_MOD_CTRL | VTERM_MOD_ALT) == (capacity > 2 ? VTERM_OK : VTERM_ERR_NO_SPACE));
            expect(vt, callback, capacity, "\x1b\0", 2);
            CHECK(vterm_keyboard_key(vt, VTERM_KEY_UP, VTERM_MOD_ALT) == (capacity > 6 ? VTERM_OK : VTERM_ERR_NO_SPACE));
            expect(vt, callback, capacity, "\x1b[1;3A", 6);
            size_t consumed;
            CHECK(vterm_input_write(vt, "\x1b[6n", 4, &consumed) ==
                  (capacity > 6 ? VTERM_OK : VTERM_ERR_NO_SPACE));
            CHECK(consumed == 4);
            expect(vt, callback, capacity, "\x1b[1;1R", 6);
            vterm_free(vt);
        }
    }
    test_selection_ownership();
    printf("integration: %u checks passed (output buffers and allocation ownership)\n", checks);
    return 0;
}
