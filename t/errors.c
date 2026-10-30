#include "../src/vterm_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/resource.h>

static unsigned long checks;
#define CHECK(expr) do { checks++; if (!(expr)) { \
    fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expr); exit(1); \
} } while (0)

typedef struct {
    void *blocks[128];
    size_t attempts, fail_at, live;
    int callbacks, logs, fd, fatal_returns;
    VTermStatus sink_status;
    char output[128], text[128];
    size_t output_len, text_len, text_limit;
    VTermStatus text_status;
} Host;

static void *allocate(size_t size, void *user) {
    Host *host = user;
    if (++host->attempts == host->fail_at) return NULL;
    for (size_t i = 0; i < 128; i++) {
        if (host->blocks[i]) continue;
        void *ptr = malloc(size);
        CHECK(ptr != NULL);
        memset(ptr, 0xa5, size); /* The library, not the allocator, must zero. */
        host->blocks[i] = ptr;
        host->live++;
        return ptr;
    }
    CHECK(0);
    return NULL;
}
static void release(void *ptr, void *user) {
    Host *host = user;
    CHECK(ptr != NULL); /* The library must filter NULL before invoking us. */
    for (size_t i = 0; i < 128; i++) {
        if (host->blocks[i] != ptr) continue;
        host->blocks[i] = NULL;
        host->live--;
        free(ptr);
        return;
    }
    CHECK(0);
}
static void fatal(void *user, const char *message, size_t len) {
    Host *host = user;
    if (host->fd >= 0) {
        if (!len || len > 255 || !message) _Exit(72);
        if (write(host->fd, "F", 1) != 1) _Exit(72);
        if (host->fatal_returns) return;
        _Exit(73);
    }
    fprintf(stderr, "Unexpected fatal: %.*s\n", (int)len, message);
    abort();
}
static void log_message(void *user, VTermLogLevel level, const char *message, size_t len) {
    Host *host = user;
    (void)level;
    CHECK(message != NULL && len <= 255);
    host->logs++;
}
static const VTermAllocatorFunctions allocator = {allocate, release};
static struct VTermBuilder builder(Host *host) {
    return (struct VTermBuilder){.ver = VTERM_API_VERSION, .rows = 4, .cols = 8,
        .allocator = &allocator, .allocdata = host, .fatal = fatal,
        .log = log_message, .userdata = host};
}
static VTerm *create(Host *host) {
    struct VTermBuilder config = builder(host);
    VTerm *vt = NULL;
    CHECK(vterm_build(&config, &vt) == VTERM_OK);
    CHECK(vt && !vterm_get_utf8(vt));
    return vt;
}
static void destroy(VTerm *vt, Host *host) {
    vterm_free(vt);
    CHECK(host->live == 0);
}
static VTermScreen *screen(VTerm *vt) {
    VTermScreen *result = NULL;
    CHECK(vterm_obtain_screen(vt, &result) == VTERM_OK && result);
    CHECK(vterm_screen_reset(result, 1) == VTERM_OK);
    return result;
}
static void input(VTerm *vt, const char *text, size_t length) {
    size_t consumed = SIZE_MAX;
    CHECK(vterm_input_write(vt, text, length, &consumed) == VTERM_OK);
    CHECK(consumed == length);
}
static VTermCallbackResult notification(VTermRect rect, void *user) {
    Host *host = user;
    (void)rect;
    host->callbacks++;
    return (VTermCallbackResult){host->sink_status, true};
}
static void test_configuration(void) {
    Host host = {.fd = -1};
    struct VTermBuilder config = builder(&host);
    VTerm *vt = (VTerm *)(uintptr_t)1;
    CHECK(vterm_build(NULL, &vt) == VTERM_ERR_INVALID_ARGUMENT && !vt);
    CHECK(vterm_build(&config, NULL) == VTERM_ERR_INVALID_ARGUMENT);
    config.allocator = NULL;
    CHECK(vterm_build(&config, &vt) == VTERM_ERR_INVALID_ARGUMENT && !vt);
    config = builder(&host); config.fatal = NULL;
    CHECK(vterm_build(&config, &vt) == VTERM_ERR_INVALID_ARGUMENT && !vt);
    VTermAllocatorFunctions incomplete = allocator; incomplete.free = NULL;
    config = builder(&host); config.allocator = &incomplete;
    CHECK(vterm_build(&config, &vt) == VTERM_ERR_INVALID_ARGUMENT && !vt);
    config = builder(&host); config.ver++;
    CHECK(vterm_build(&config, &vt) == VTERM_ERR_VERSION_MISMATCH && !vt);
    config = builder(&host); config.rows = 0;
    CHECK(vterm_build(&config, &vt) == VTERM_ERR_INVALID_ARGUMENT && !vt);
    config = builder(&host); config.cols = INT_MAX;
    CHECK(vterm_build(&config, &vt) == VTERM_ERR_OVERFLOW && !vt);
    config = builder(&host); config.rows = INT_MAX;
    CHECK(vterm_build(&config, &vt) == VTERM_ERR_OVERFLOW && !vt);
    CHECK(host.attempts == 0);
    CHECK(vterm_check_version(VTERM_VERSION_MAJOR + 1, 0) == VTERM_ERR_VERSION_MISMATCH);
    CHECK(VTERM_CHECK_VERSION == VTERM_OK);
}
static void test_construction_failures(void) {
    for (size_t n = 1; n <= 3; n++) {
        Host host = {.fd = -1, .fail_at = n};
        struct VTermBuilder config = builder(&host);
        VTerm *vt = (VTerm *)(uintptr_t)1;
        CHECK(vterm_build(&config, &vt) == VTERM_ERR_NO_MEMORY && !vt);
        CHECK(host.live == 0);
    }
    for (int target = 0; target < 3; target++) {
        size_t count = target == 0 ? 5 : target == 1 ? 8 : 3;
        for (size_t n = 1; n <= count; n++) {
            Host host = {.fd = -1};
            VTerm *vt = create(&host);
            VTermState *state = NULL;
            if (target == 2) CHECK(vterm_obtain_state(vt, &state) == VTERM_OK);
            size_t base = host.live;
            const VTermParserCallbacks original = {0};
            if (!state) vterm_parser_set_callbacks(vt, &original, &host);
            host.fail_at = host.attempts + n;
            if (target == 0) {
                CHECK(vterm_obtain_state(vt, &state) == VTERM_ERR_NO_MEMORY && !state);
            } else {
                VTermScreen *result = NULL;
                CHECK(vterm_obtain_screen(vt, &result) == VTERM_ERR_NO_MEMORY && !result);
            }
            CHECK(host.live == base);
            if (target != 2) CHECK(vterm_parser_get_cbdata(vt) == &host);
            host.fail_at = 0;
            CHECK(screen(vt) != NULL);
            destroy(vt, &host);
        }
    }
}
static void test_resize_failures(void) {
    for (int use_screen = 0; use_screen < 3; use_screen++) {
        size_t count = use_screen == 0 ? 3 : use_screen == 1 ? 5 : 6;
        for (size_t n = 1; n <= count; n++) {
            Host host = {.fd = -1};
            VTerm *vt = create(&host);
            VTermScreen *scr = NULL;
            VTermState *state = NULL;
            if (use_screen) {
                scr = screen(vt);
                if (use_screen == 2) CHECK(vterm_screen_enable_altscreen(scr, 1) == VTERM_OK);
            } else {
                CHECK(vterm_obtain_state(vt, &state) == VTERM_OK);
                CHECK(vterm_state_reset(state, 1) == VTERM_OK);
            }
            input(vt, "A", 1);
            VTermScreenCell before = {0}, after = {0};
            if (scr) CHECK(vterm_screen_get_cell(scr, (VTermPos){0, 0}, &before));
            const VTermScreenCallbacks callbacks = {.damage = notification};
            if (scr) vterm_screen_set_callbacks(scr, &callbacks, &host);
            size_t base = host.live;
            host.fail_at = host.attempts + n;
            CHECK(vterm_set_size(vt, 6, 12) == VTERM_ERR_NO_MEMORY);
            int rows, cols; vterm_get_size(vt, &rows, &cols);
            CHECK(rows == 4 && cols == 8 && host.live == base && !host.callbacks);
            if (scr) {
                CHECK(vterm_screen_get_cell(scr, (VTermPos){0, 0}, &after));
                CHECK(memcmp(&before, &after, sizeof(before)) == 0);
            }
            host.fail_at = 0;
            CHECK(vterm_set_size(vt, 6, 12) == VTERM_OK);
            vterm_get_size(vt, &rows, &cols);
            CHECK(rows == 6 && cols == 12);
            destroy(vt, &host);
        }
    }
}
static void test_combine_and_alt_failure(void) {
    Host host = {.fd = -1};
    VTerm *vt = create(&host);
    VTermScreen *scr = screen(vt);
    size_t base = host.live;
    host.fail_at = host.attempts + 1;
    CHECK(vterm_screen_enable_altscreen(scr, 1) == VTERM_ERR_NO_MEMORY);
    CHECK(host.live == base);
    host.fail_at = 0;
    CHECK(vterm_screen_enable_altscreen(scr, 1) == VTERM_OK);
    vterm_set_utf8(vt, 1);
    input(vt, "a", 1);
    char accents[40];
    for (size_t i = 0; i < sizeof(accents); i += 2) {
        accents[i] = (char)0xcc; accents[i + 1] = (char)0x81;
    }
    /* Preserve an already accepted UTF-8 lead byte across an OOM retry. */
    input(vt, accents, 1);
    base = host.live;
    host.fail_at = host.attempts + 1;
    size_t consumed;
    CHECK(vterm_input_write(vt, accents + 1, sizeof(accents) - 1, &consumed) == VTERM_ERR_NO_MEMORY);
    CHECK(consumed == 0 && host.live == base);
    host.fail_at = 0;
    input(vt, accents + 1, sizeof(accents) - 1);
    input(vt, "B", 1);
    VTermScreenCell cell = {0};
    CHECK(vterm_screen_get_cell(scr, (VTermPos){0, 1}, &cell) && cell.chars[0] == 'B');
    destroy(vt, &host);
}
static void test_utf8_chunk_boundaries(void) {
    const char text[] = "A\xc2\xa3" "B\xe4\xb8\xad" "C\xf0\x9f\x99\x82" "De\xcc\x81" "Z";
    const size_t length = sizeof(text) - 1;
    Host host = {.fd = -1};
    VTerm *vt = create(&host);
    vterm_set_utf8(vt, 1);
    VTermScreen *scr = screen(vt);
    CHECK(vterm_set_size(vt, 4, 40) == VTERM_OK);
    CHECK(vterm_screen_reset(scr, 1) == VTERM_OK);
    input(vt, text, length);
    VTermScreenCell expected[40] = {0};
    for (int col = 0; col < 40; col++)
        CHECK(vterm_screen_get_cell(scr, (VTermPos){0, col}, &expected[col]));
    VTermPos expected_cursor;
    vterm_state_get_cursorpos(vt->state, &expected_cursor);
    /* Every three-part split, including empty parts and splits inside UTF-8.
     * Both default UTF-8 GL and explicitly designated ASCII GL must work. */
    for (int ascii_gl = 0; ascii_gl < 2; ascii_gl++) {
        for (size_t first = 0; first <= length; first++) {
            for (size_t second = first; second <= length; second++) {
                CHECK(vterm_screen_reset(scr, 1) == VTERM_OK);
                if (ascii_gl) input(vt, "\x1b(B", 3);
                input(vt, text, first);
                input(vt, text + first, second - first);
                input(vt, text + second, length - second);
                for (int col = 0; col < 40; col++) {
                    VTermScreenCell cell = {0};
                    CHECK(vterm_screen_get_cell(scr, (VTermPos){0, col}, &cell));
                    CHECK(cell.width == expected[col].width);
                    CHECK(memcmp(cell.chars, expected[col].chars, sizeof(cell.chars)) == 0);
                }
                VTermPos cursor;
                vterm_state_get_cursorpos(vt->state, &cursor);
                CHECK(cursor.row == expected_cursor.row && cursor.col == expected_cursor.col);
            }
        }
    }
    for (int hard = 0; hard < 2; hard++) {
        CHECK(vterm_screen_reset(scr, 1) == VTERM_OK);
        input(vt, "\xe4", 1);
        CHECK(vterm_screen_reset(scr, hard) == VTERM_OK);
        input(vt, "\xb8\xad", 2);
        for (int col = 0; col < 2; col++) {
            VTermScreenCell cell = {0};
            CHECK(vterm_screen_get_cell(scr, (VTermPos){0, col}, &cell));
            CHECK(cell.chars[0] == 0xfffd);
        }
    }
    destroy(vt, &host);
}

static VTermStatus text_callback(const char *bytes, size_t len, size_t *consumed, void *user) {
    Host *host = user;
    *consumed = host->text_limit && len > host->text_limit ? host->text_limit : len;
    CHECK(host->text_len + *consumed <= sizeof(host->text));
    memcpy(host->text + host->text_len, bytes, *consumed);
    host->text_len += *consumed;
    return host->text_status;
}
static VTermCallbackResult control_callback(unsigned char byte, void *user) {
    Host *host = user; (void)byte;
    host->callbacks++;
    return (VTermCallbackResult){host->sink_status, false};
}
static VTermStatus sink(const char *bytes, size_t len, void *user) {
    Host *host = user;
    host->callbacks++;
    if (host->sink_status != VTERM_OK) return host->sink_status;
    CHECK(host->output_len + len <= sizeof(host->output));
    memcpy(host->output + host->output_len, bytes, len);
    host->output_len += len;
    return VTERM_OK;
}
static void test_stream_errors(void) {
    Host host = {.fd = -1, .text_limit = 2, .text_status = VTERM_ERR_NO_MEMORY};
    VTerm *vt = create(&host);
    const VTermParserCallbacks callbacks = {.text = text_callback, .control = control_callback};
    vterm_parser_set_callbacks(vt, &callbacks, &host);
    size_t consumed;
    CHECK(vterm_input_write(vt, "abcd", 4, &consumed) == VTERM_ERR_NO_MEMORY && consumed == 2);
    host.text_status = VTERM_OK;
    input(vt, "cd", 2);
    CHECK(host.text_len == 4 && memcmp(host.text, "abcd", 4) == 0);
    host.sink_status = VTERM_ERR_HOST;
    CHECK(vterm_input_write(vt, "\aX", 2, &consumed) == VTERM_ERR_HOST && consumed == 1);
    CHECK(host.callbacks == 1 && host.text_len == 4);
    host.sink_status = VTERM_OK;
    input(vt, "\a", 1); /* Unhandled is not an error. */
    input(vt, "X", 1);
    CHECK(host.text_len == 5 && host.text[4] == 'X');
    destroy(vt, &host);

    memset(&host, 0, sizeof(host)); host.fd = -1;
    vt = create(&host);
    VTermScreen *scr = screen(vt);
    vterm_output_set_callback(vt, sink, &host);
    host.sink_status = VTERM_ERR_HOST;
    CHECK(vterm_input_write(vt, "\x1b[6nZ", 5, &consumed) == VTERM_ERR_HOST && consumed == 4);
    CHECK(vterm_output_get_buffer_current(vt) == 6 && host.output_len == 0);
    host.sink_status = VTERM_OK;
    input(vt, "Z", 1); /* Flushes the retained reply before new input. */
    CHECK(host.output_len == 6 && memcmp(host.output, "\x1b[1;1R", 6) == 0);
    VTermScreenCell cell = {0};
    CHECK(vterm_screen_get_cell(scr, (VTermPos){0, 0}, &cell) && cell.chars[0] == 'Z');
    CHECK(vterm_output_flush(vt) == VTERM_OK && host.output_len == 6);
    vterm_output_set_callback(vt, NULL, NULL);
    CHECK(vterm_input_write(vt, "\x1b[6n\x1b[6n", 8, &consumed) == VTERM_ERR_NO_SPACE && consumed == 4);
    char reply[32];
    CHECK(vterm_output_read(vt, reply, sizeof(reply)) == 6);
    input(vt, "\x1b[6n", 4);
    CHECK(vterm_output_read(vt, reply, sizeof(reply)) == 6);
    const VTermScreenCallbacks display = {.damage = notification};
    vterm_screen_set_callbacks(scr, &display, &host);
    host.sink_status = VTERM_ERR_HOST;
    CHECK(vterm_input_write(vt, "Q", 1, &consumed) == VTERM_ERR_HOST && consumed == 1);
    CHECK(vterm_screen_get_cell(scr, (VTermPos){0, 1}, &cell) && cell.chars[0] == 'Q');
    int notifications = host.callbacks;
    CHECK(vterm_screen_flush_damage(scr) == VTERM_ERR_HOST);
    CHECK(host.callbacks > notifications);
    host.sink_status = VTERM_OK;
    CHECK(vterm_screen_flush_damage(scr) == VTERM_OK);
    notifications = host.callbacks;
    CHECK(vterm_screen_flush_damage(scr) == VTERM_OK && host.callbacks == notifications);
    destroy(vt, &host);
}
static void test_parser_limits(void) {
    Host host = {.fd = -1};
    VTerm *vt = create(&host);
    VTermScreen *scr = screen(vt);
    const char sequence[] = "\x1b[;;;;;;;;;;;;;;;;;mZ";
    size_t consumed;
    CHECK(vterm_input_write(vt, sequence, sizeof(sequence) - 1, &consumed) == VTERM_ERR_OVERFLOW);
    CHECK(consumed > 0 && consumed < sizeof(sequence) - 1);
    input(vt, sequence + consumed, sizeof(sequence) - 1 - consumed);
    VTermScreenCell cell = {0};
    CHECK(vterm_screen_get_cell(scr, (VTermPos){0, 0}, &cell) && cell.chars[0] == 'Z');
    CHECK(vterm_set_size(vt, INT_MAX, INT_MAX) == VTERM_ERR_OVERFLOW);
    CHECK(vterm_keyboard_unichar(vt, 0x110000, VTERM_MOD_NONE) == VTERM_ERR_INVALID_ARGUMENT);
    CHECK(vterm_mouse_move(vt, -1, 0, VTERM_MOD_NONE) == VTERM_ERR_INVALID_ARGUMENT);
    destroy(vt, &host);
}
static VTermCallbackResult property_callback(VTermProp prop, VTermValue *value, void *user) {
    (void)prop; (void)value;
    Host *host = user;
    return (VTermCallbackResult){host->sink_status, host->sink_status == VTERM_OK};
}
static void test_property_errors(void) {
    Host host = {.fd = -1};
    VTerm *vt = create(&host);
    VTermScreen *scr = screen(vt);
    CHECK(vterm_screen_enable_altscreen(scr, 1) == VTERM_OK);
    const VTermScreenCallbacks properties = {.settermprop = property_callback};
    vterm_screen_set_callbacks(scr, &properties, &host);
    VTermValue value = {.boolean = 1};
    bool handled;
    host.sink_status = VTERM_ERR_HOST;
    CHECK(vterm_state_set_termprop(vt->state, VTERM_PROP_ALTSCREEN, &value, &handled) == VTERM_ERR_HOST);
    CHECK(!handled && !vt->state->mode.alt_screen);
    input(vt, "P", 1);
    host.sink_status = VTERM_OK;
    CHECK(vterm_state_set_termprop(vt->state, VTERM_PROP_ALTSCREEN, &value, &handled) == VTERM_OK && handled);
    value.boolean = 0;
    const VTermScreenCallbacks display = {.damage = notification};
    vterm_screen_set_callbacks(scr, &display, &host);
    host.sink_status = VTERM_ERR_HOST;
    CHECK(vterm_state_set_termprop(vt->state, VTERM_PROP_ALTSCREEN, &value, &handled) == VTERM_ERR_HOST);
    CHECK(handled && !vt->state->mode.alt_screen);
    VTermScreenCell cell = {0};
    CHECK(vterm_screen_get_cell(scr, (VTermPos){0, 0}, &cell) && cell.chars[0] == 'P');
    host.sink_status = VTERM_OK;
    CHECK(vterm_screen_flush_damage(scr) == VTERM_OK);
    destroy(vt, &host);
}
static VTermStatus overconsume(const char *bytes, size_t len, size_t *consumed, void *user) {
    (void)bytes; (void)user;
    *consumed = len + 1;
    return VTERM_OK;
}
static void test_fatal_and_logging(void) {
    Host host = {.fd = -1};
    VTerm *vt = create(&host);
    char message[400]; memset(message, 'L', sizeof(message)); message[399] = 0;
    memset(vt->tmpbuffer, 0x5a, vt->tmpbuffer_len);
    size_t attempts = host.attempts;
    vt_log(vt, VTERM_LOG_DEBUG, "%s", message);
    CHECK(host.logs == 1 && host.attempts == attempts);
    for (size_t i = 0; i < vt->tmpbuffer_len; i++) CHECK(vt->tmpbuffer[i] == 0x5a);
    const VTermParserCallbacks callbacks = {.text = overconsume};
    vterm_parser_set_callbacks(vt, &callbacks, NULL);
    for (int returns = 0; returns <= 1; returns++) {
        int pipefd[2]; CHECK(pipe(pipefd) == 0);
        host.fd = pipefd[1]; host.fatal_returns = returns;
        pid_t pid = fork(); CHECK(pid >= 0);
        if (pid == 0) {
            struct rlimit limit = {0, 0}; setrlimit(RLIMIT_CORE, &limit);
            close(pipefd[0]); alarm(5);
            size_t consumed;
            vterm_input_write(vt, "x", 1, &consumed);
            if (write(pipefd[1], "R", 1) != 1) _Exit(98);
            _Exit(99);
        }
        close(pipefd[1]);
        char observed[2]; ssize_t length = read(pipefd[0], observed, sizeof(observed));
        close(pipefd[0]);
        int status; CHECK(waitpid(pid, &status, 0) == pid);
        CHECK(length == 1 && observed[0] == 'F');
        if (!returns) CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 73);
        else CHECK(WIFSIGNALED(status) && (WTERMSIG(status) == SIGILL || WTERMSIG(status) == SIGTRAP));
    }
    host.fd = -1;
    destroy(vt, &host);
}
int main(void) {
    test_configuration();
    test_construction_failures();
    test_resize_failures();
    test_combine_and_alt_failure();
    test_utf8_chunk_boundaries();
    test_stream_errors();
    test_parser_limits();
    test_property_errors();
    test_fatal_and_logging();
    printf("errors: %lu checks passed (OOM, propagation, rollback, logs and fatal)\n", checks);
    return 0;
}
