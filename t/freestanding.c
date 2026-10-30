/* No libc/startup objects. Linux CI executes this fixture using the syscall
 * below; other freestanding targets can compile/link it without executing it.
 * The memory symbols are compiler ABI glue supplied by the embedding host. */
#include "vterm.h"
#include "../src/lib/utils.h"

void *memcpy(void *d, const void *s, size_t n) { return vt_memcpy(d, s, n); }
void *memmove(void *d, const void *s, size_t n) { return vt_memmove(d, s, n); }
void *memset(void *d, int c, size_t n) { return vt_memset(d, c, n); }
int memcmp(const void *a, const void *b, size_t n) {
    const unsigned char *x = a, *y = b;
    for (size_t i = 0; i < n; i++) if (x[i] != y[i]) return (int)x[i] - y[i];
    return 0;
}

static unsigned char arena[65536] __attribute__((aligned(16)));
static size_t used;
static void *allocate(size_t size, void *user) {
    (void)user;
    size_t offset = (used + 15) & ~(size_t)15;
    if (size > sizeof(arena) - offset) return NULL;
    used = offset + size;
    return arena + offset;
}
static void release(void *ptr, void *user) { (void)ptr; (void)user; }
__attribute__((noreturn)) static void finish(int code) {
#if defined(__linux__) && defined(__x86_64__)
    __asm__ volatile("syscall" : : "a"(60L), "D"((long)code) : "rcx", "r11", "memory");
#else
    (void)code;
#endif
    __builtin_trap();
}
static void fatal(void *user, const char *message, size_t len) {
    (void)user; (void)message; (void)len;
    finish(90);
}
__attribute__((noreturn)) void _start(void) {
    const VTermAllocatorFunctions allocator = {allocate, release};
    const struct VTermBuilder builder = {.ver = VTERM_API_VERSION, .rows = 4, .cols = 8,
        .allocator = &allocator, .fatal = fatal};
    VTerm *vt = NULL;
    if (vterm_build(&builder, &vt) != VTERM_OK) finish(1);
    VTermScreen *screen = NULL;
    if (vterm_obtain_screen(vt, &screen) != VTERM_OK) finish(2);
    if (vterm_screen_reset(screen, 1) != VTERM_OK) finish(3);
    size_t consumed;
    const char text[] = "\x1b[31mOK";
    if (vterm_input_write(vt, text, sizeof(text) - 1, &consumed) != VTERM_OK || consumed != sizeof(text) - 1) finish(4);
    VTermScreenCell cell;
    if (!vterm_screen_get_cell(screen, (VTermPos){0, 1}, &cell) || cell.chars[0] != 'K') finish(5);
    if (vterm_input_write(vt, "\x1b[6n", 4, &consumed) != VTERM_OK) finish(6);
    char response[16];
    if (vterm_output_read(vt, response, sizeof(response)) != 6 || vt_strncmp(response, "\x1b[1;3R", 6)) finish(7);
    if (vterm_set_size(vt, 6, 12) != VTERM_OK) finish(8);
    vterm_free(vt);
    finish(0);
}
