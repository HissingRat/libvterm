#ifndef VTERM_EXAMPLE_HOST_H
#define VTERM_EXAMPLE_HOST_H

/* Explicit adapters for the hosted examples/tests. Never built into libvterm. */
#include "vterm.h"
#include <stdio.h>
#include <stdlib.h>

static inline void *host_malloc(size_t size, void *user) {
    (void)user;
    return malloc(size);
}
static inline void host_free(void *pointer, void *user) {
    (void)user;
    free(pointer);
}
static inline void host_fatal(void *user, const char *message, size_t len) {
    (void)user;
    fwrite(message, 1, len, stderr);
    fputc('\n', stderr);
    abort();
}
static inline void host_check(VTermStatus status) {
    if (status != VTERM_OK) {
        fprintf(stderr, "libvterm status: %d\n", status);
        exit(1);
    }
}
static inline VTermCallbackResult host_result(int handled) {
    return (VTermCallbackResult){VTERM_OK, !!handled};
}
static inline VTermStatus host_discard_output(const char *bytes, size_t len, void *user) {
    (void)bytes; (void)len; (void)user;
    return VTERM_OK;
}
static inline VTerm *host_build(struct VTermBuilder builder) {
    static const VTermAllocatorFunctions allocator = {host_malloc, host_free};
    builder.ver = VTERM_API_VERSION;
    if (!builder.allocator) builder.allocator = &allocator;
    builder.fatal = host_fatal;
    VTerm *vt = NULL;
    host_check(vterm_build(&builder, &vt));
    return vt;
}
static inline VTerm *host_new(int rows, int cols) {
    return host_build((struct VTermBuilder){.rows = rows, .cols = cols});
}
static inline VTermState *host_state(VTerm *vt) {
    VTermState *state = NULL;
    host_check(vterm_obtain_state(vt, &state));
    return state;
}
static inline VTermScreen *host_screen(VTerm *vt) {
    VTermScreen *screen = NULL;
    host_check(vterm_obtain_screen(vt, &screen));
    return screen;
}
static inline size_t host_input(VTerm *vt, const char *bytes, size_t len) {
    size_t consumed = 0;
    host_check(vterm_input_write(vt, bytes, len, &consumed));
    return consumed;
}
#endif
