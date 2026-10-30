#include "vterm_internal.h"

void vt_error(VTerm *vt, VTermStatus status) {
    if (vt->error == VTERM_OK)
        vt->error = status;
}

VTermStatus vt_begin(VTerm *vt) {
    if (!vt)
        return VTERM_ERR_INVALID_ARGUMENT;
    if (vt->operation_depth++ == 0)
        vt->error = VTERM_OK;
    return VTERM_OK;
}

VTermStatus vt_end(VTerm *vt) {
    if (!vt->operation_depth)
        vt_fatal(vt, "Unbalanced operation boundary");
    vt->operation_depth--;
    return vt->error;
}

int vt_callback(VTerm *vt, VTermCallbackResult result) {
    vt_error(vt, result.status);
    /* A rejected callback must not trigger another implementation of the
     * same operation. It is distinct from an ordinary unhandled result. */
    return result.status != VTERM_OK || result.handled;
}

VTermCallbackResult vt_result(VTerm *vt, int handled) {
    return (VTermCallbackResult){ .status = vt->error, .handled = !!handled };
}

VTermStatus vt_dimensions(int rows, int cols) {
    if (rows <= 0 || cols <= 0)
        return VTERM_ERR_INVALID_ARGUMENT;
    /* Existing cell addressing uses int; also bound byte-sized allocations. */
    if (cols > INT_MAX - 7 || rows > INT_MAX / cols ||
        (size_t)rows > SIZE_MAX / (size_t)cols / sizeof(VTermScreenCell))
        return VTERM_ERR_OVERFLOW;
    return VTERM_OK;
}

void *vt_alloc_array(VTerm *vt, size_t count, size_t size) {
    if (size && count > SIZE_MAX / size) {
        vt_error(vt, VTERM_ERR_OVERFLOW);
        return NULL;
    }
    return vterm_allocator_malloc(vt, count * size);
}

static size_t diagnostic(char *buffer, size_t size, const char *format, va_list args) {
    int length = vt_vsnprintf(buffer, size, format, args);
    if (length < 0) {
        const char message[] = "libvterm diagnostic formatting failed";
        size_t n = sizeof(message) - 1;
        if (n >= size) n = size - 1;
        vt_memcpy(buffer, message, n);
        return n;
    }
    return (size_t)length < size ? (size_t)length : size - 1;
}

void vt_log(VTerm *vt, VTermLogLevel level, const char *format, ...) {
    if (!vt || !vt->log)
        return;
    char message[256];
    va_list args;
    va_start(args, format);
    size_t len = diagnostic(message, sizeof(message), format, args);
    va_end(args);
    vt->log(vt->userdata, level, message, len);
}

void vt_fatal(VTerm *vt, const char *format, ...) {
    char message[256];
    va_list args;
    va_start(args, format);
    size_t len = diagnostic(message, sizeof(message), format, args);
    va_end(args);
    vt->fatal(vt->userdata, message, len);
    /* Returning from fatal is a host contract violation, never permission
     * to resume with corrupt state. No libc abort/exit fallback. */
    __builtin_trap();
}
