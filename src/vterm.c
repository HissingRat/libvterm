#include "vterm_internal.h"

#include <stdarg.h>

/*****************
 * API functions *
 *****************/

VTermStatus vterm_build(const struct VTermBuilder *builder, VTerm **out_vt) {
    if (!out_vt) return VTERM_ERR_INVALID_ARGUMENT;
    *out_vt = NULL;
    if (!builder || !builder->allocator || !builder->allocator->malloc ||
        !builder->allocator->free || !builder->fatal)
        return VTERM_ERR_INVALID_ARGUMENT;
    if (builder->ver != VTERM_API_VERSION)
        return VTERM_ERR_VERSION_MISMATCH;
    VTermStatus status = vt_dimensions(builder->rows, builder->cols);
    if (status != VTERM_OK) return status;
    size_t outlen = builder->outbuffer_len ? builder->outbuffer_len : 4096;
    size_t tmplen = builder->tmpbuffer_len ? builder->tmpbuffer_len : 4096;
    if (outlen < tmplen) return VTERM_ERR_INVALID_ARGUMENT;
    if (outlen > INT_MAX || tmplen > INT_MAX)
        return VTERM_ERR_OVERFLOW;
    VTerm *vt = builder->allocator->malloc(sizeof(*vt), builder->allocdata);
    if (!vt) return VTERM_ERR_NO_MEMORY;
    vt_memset(vt, 0, sizeof(*vt));
    vt->allocator = *builder->allocator;
    vt->allocdata = builder->allocdata;
    vt->fatal = builder->fatal;
    vt->log = builder->log;
    vt->userdata = builder->userdata;
    vt->rows = builder->rows;
    vt->cols = builder->cols;
    vt->parser.state = NORMAL;
    vt->outbuffer_len = outlen;
    vt->tmpbuffer_len = tmplen;
    vt->outbuffer = vterm_allocator_malloc(vt, outlen);
    vt->tmpbuffer = vterm_allocator_malloc(vt, tmplen);
    if (!vt->outbuffer || !vt->tmpbuffer) {
        status = vt->error;
        vterm_free(vt);
        return status;
    }
    *out_vt = vt;
    return VTERM_OK;
}

void vterm_free(VTerm *vt) {
    if (!vt) return;
    if (vt->screen)
        vterm_screen_free(vt->screen);

    if (vt->state)
        vterm_state_free(vt->state);

    vterm_allocator_free(vt, vt->outbuffer);
    vterm_allocator_free(vt, vt->tmpbuffer);

    vterm_allocator_free(vt, vt);
}

INTERNAL void *vterm_allocator_malloc(VTerm *vt, size_t size) {
    void *ptr = vt->allocator.malloc(size, vt->allocdata);
    if (!ptr) {
        vt_error(vt, VTERM_ERR_NO_MEMORY);
        return NULL;
    }
    vt_memset(ptr, 0, size);
    return ptr;
}

INTERNAL void vterm_allocator_free(VTerm *vt, void *ptr) {
    if (ptr) vt->allocator.free(ptr, vt->allocdata);
}

void vterm_get_size(const VTerm *vt, int *rowsp, int *colsp) {
    if (rowsp)
        *rowsp = vt->rows;
    if (colsp)
        *colsp = vt->cols;
}

VTermStatus vterm_set_size(VTerm *vt, int rows, int cols) {
    if (!vt) return VTERM_ERR_INVALID_ARGUMENT;
    VTermStatus status = vt_dimensions(rows, cols);
    if (status != VTERM_OK) return status;
    vt_begin(vt);
    vt->resize_committed = false;
    if (rows != vt->rows || cols != vt->cols) {
        if (vt->parser.callbacks && vt->parser.callbacks->resize)
            vt_callback(vt, vt->parser.callbacks->resize(rows, cols, vt->parser.cbdata));
        if (vt->error == VTERM_OK || vt->resize_committed) {
            vt->rows = rows;
            vt->cols = cols;
        }
    }
    return vt_end(vt);
}

int vterm_get_utf8(const VTerm *vt) {
    return vt->mode.utf8;
}

void vterm_set_utf8(VTerm *vt, int is_utf8) {
    vt->mode.utf8 = is_utf8;
}

void vterm_output_set_callback(VTerm *vt, VTermOutputCallback *func, void *user) {
    vt->outfunc = func;
    vt->outdata = user;
}

VTermStatus vterm_output_flush(VTerm *vt) {
    if (vt_begin(vt) != VTERM_OK) return VTERM_ERR_INVALID_ARGUMENT;
    if (vt->error == VTERM_OK && vt->outfunc && vt->outbuffer_cur) {
        VTermStatus status = vt->outfunc(vt->outbuffer, vt->outbuffer_cur, vt->outdata);
        if (status == VTERM_OK)
            vt->outbuffer_cur = 0;
        else
            vt_error(vt, status);
    }
    return vt_end(vt);
}

INTERNAL void vterm_push_output_bytes(VTerm *vt, const char *bytes, size_t len) {
    if (len > vt->outbuffer_len - vt->outbuffer_cur) {
        vt_error(vt, VTERM_ERR_NO_SPACE);
        return;
    }
    vt_memcpy(vt->outbuffer + vt->outbuffer_cur, bytes, len);
    vt->outbuffer_cur += len;
    vterm_output_flush(vt);
}

INTERNAL void vterm_push_output_vsprintf(VTerm *vt, const char *format, va_list args) {
    int len = vt_vsnprintf(vt->tmpbuffer, vt->tmpbuffer_len, format, args);
    if (len < 0 || (size_t)len >= vt->tmpbuffer_len) {
        vt_error(vt, len < 0 ? VTERM_ERR_INVALID_ARGUMENT : VTERM_ERR_NO_SPACE);
        return;
    }

    vterm_push_output_bytes(vt, vt->tmpbuffer, (size_t)len);
}

INTERNAL void vterm_push_output_sprintf(VTerm *vt, const char *format, ...) {
    va_list args;
    va_start(args, format);
    vterm_push_output_vsprintf(vt, format, args);
    va_end(args);
}

INTERNAL void vterm_push_output_sprintf_ctrl(VTerm *vt, unsigned char ctrl, const char *fmt, ...) {
    size_t cur;

    if (ctrl >= 0x80 && !vt->mode.ctrl8bit)
        cur = vt_snprintf(vt->tmpbuffer, vt->tmpbuffer_len,
                       ESC_S "%c", ctrl - 0x40);
    else
        cur = vt_snprintf(vt->tmpbuffer, vt->tmpbuffer_len,
                       "%c", ctrl);

    if (cur >= vt->tmpbuffer_len) {
        vt_error(vt, VTERM_ERR_NO_SPACE);
        return;
    }

    va_list args;
    va_start(args, fmt);
    int len = vt_vsnprintf(vt->tmpbuffer + cur, vt->tmpbuffer_len - cur, fmt, args);
    va_end(args);

    if (len < 0 || (size_t)len >= vt->tmpbuffer_len - cur) {
        vt_error(vt, len < 0 ? VTERM_ERR_INVALID_ARGUMENT : VTERM_ERR_NO_SPACE);
        return;
    }
    cur += (size_t)len;

    vterm_push_output_bytes(vt, vt->tmpbuffer, cur);
}

INTERNAL void vterm_push_output_sprintf_str(VTerm *vt, unsigned char ctrl, bool term, const char *fmt, ...) {
    size_t cur = 0;

    if (ctrl) {
        if (ctrl >= 0x80 && !vt->mode.ctrl8bit)
            cur = vt_snprintf(vt->tmpbuffer, vt->tmpbuffer_len,
                           ESC_S "%c", ctrl - 0x40);
        else
            cur = vt_snprintf(vt->tmpbuffer, vt->tmpbuffer_len,
                           "%c", ctrl);

        if (cur >= vt->tmpbuffer_len) {
        vt_error(vt, VTERM_ERR_NO_SPACE);
        return;
    }
    }

    va_list args;
    va_start(args, fmt);
    int len = vt_vsnprintf(vt->tmpbuffer + cur, vt->tmpbuffer_len - cur, fmt, args);
    va_end(args);

    if (len < 0 || (size_t)len >= vt->tmpbuffer_len - cur) {
        vt_error(vt, len < 0 ? VTERM_ERR_INVALID_ARGUMENT : VTERM_ERR_NO_SPACE);
        return;
    }
    cur += (size_t)len;

    if (term) {
        cur += vt_snprintf(vt->tmpbuffer + cur, vt->tmpbuffer_len - cur,
                        vt->mode.ctrl8bit ? "\x9C" : ESC_S "\\"); // ST

        if (cur >= vt->tmpbuffer_len) {
        vt_error(vt, VTERM_ERR_NO_SPACE);
        return;
    }
    }

    vterm_push_output_bytes(vt, vt->tmpbuffer, cur);
}

size_t vterm_output_get_buffer_size(const VTerm *vt) {
    return vt->outbuffer_len;
}

size_t vterm_output_get_buffer_current(const VTerm *vt) {
    return vt->outbuffer_cur;
}

size_t vterm_output_get_buffer_remaining(const VTerm *vt) {
    return vt->outbuffer_len - vt->outbuffer_cur;
}

size_t vterm_output_read(VTerm *vt, char *buffer, size_t len) {
    if (len > vt->outbuffer_cur)
        len = vt->outbuffer_cur;

    vt_memcpy(buffer, vt->outbuffer, len);

    if (len < vt->outbuffer_cur)
        vt_memmove(vt->outbuffer, vt->outbuffer + len, vt->outbuffer_cur - len);

    vt->outbuffer_cur -= len;

    return len;
}

VTermValueType vterm_get_attr_type(VTermAttr attr) {
    switch (attr) {
        case VTERM_ATTR_BOLD: return VTERM_VALUETYPE_BOOL;
        case VTERM_ATTR_UNDERLINE: return VTERM_VALUETYPE_INT;
        case VTERM_ATTR_ITALIC: return VTERM_VALUETYPE_BOOL;
        case VTERM_ATTR_BLINK: return VTERM_VALUETYPE_BOOL;
        case VTERM_ATTR_REVERSE: return VTERM_VALUETYPE_BOOL;
        case VTERM_ATTR_CONCEAL: return VTERM_VALUETYPE_BOOL;
        case VTERM_ATTR_STRIKE: return VTERM_VALUETYPE_BOOL;
        case VTERM_ATTR_FONT: return VTERM_VALUETYPE_INT;
        case VTERM_ATTR_FOREGROUND: return VTERM_VALUETYPE_COLOR;
        case VTERM_ATTR_BACKGROUND: return VTERM_VALUETYPE_COLOR;
        case VTERM_ATTR_SMALL: return VTERM_VALUETYPE_BOOL;
        case VTERM_ATTR_BASELINE: return VTERM_VALUETYPE_INT;

        case VTERM_N_ATTRS: return 0;
    }
    return 0; /* UNREACHABLE */
}

VTermValueType vterm_get_prop_type(VTermProp prop) {
    switch (prop) {
        case VTERM_PROP_CURSORVISIBLE: return VTERM_VALUETYPE_BOOL;
        case VTERM_PROP_CURSORBLINK: return VTERM_VALUETYPE_BOOL;
        case VTERM_PROP_ALTSCREEN: return VTERM_VALUETYPE_BOOL;
        case VTERM_PROP_TITLE: return VTERM_VALUETYPE_STRING;
        case VTERM_PROP_ICONNAME: return VTERM_VALUETYPE_STRING;
        case VTERM_PROP_REVERSE: return VTERM_VALUETYPE_BOOL;
        case VTERM_PROP_CURSORSHAPE: return VTERM_VALUETYPE_INT;
        case VTERM_PROP_MOUSE: return VTERM_VALUETYPE_INT;
        case VTERM_PROP_FOCUSREPORT: return VTERM_VALUETYPE_BOOL;

        case VTERM_N_PROPS: return 0;
    }
    return 0; /* UNREACHABLE */
}

VTermStatus vterm_scroll_rect(VTermRect rect,
                       int downward,
                       int rightward,
                       VTermCallbackResult (*moverect)(VTermRect src, VTermRect dest, void *user),
                       VTermCallbackResult (*eraserect)(VTermRect rect, int selective, void *user),
                       void *user) {
    if (!eraserect) return VTERM_ERR_INVALID_ARGUMENT;
    int64_t height = (int64_t)rect.end_row - rect.start_row;
    int64_t width = (int64_t)rect.end_col - rect.start_col;
    if (height < 0 || width < 0) return VTERM_ERR_INVALID_ARGUMENT;
    if (height > INT_MAX || width > INT_MAX) return VTERM_ERR_OVERFLOW;
    VTermStatus status = VTERM_OK;
    VTermRect src;
    VTermRect dest;

    if (downward >= rect.end_row - rect.start_row || downward <= -(rect.end_row - rect.start_row) ||
        rightward >= rect.end_col - rect.start_col || rightward <= -(rect.end_col - rect.start_col)) {
        /* Scroll more than area; just erase the lot */
        return (*eraserect)(rect, 0, user).status;
    }

    if (rightward >= 0) {
        /* rect: [XXX................]
         * src:     [----------------]
         * dest: [----------------]
         */
        dest.start_col = rect.start_col;
        dest.end_col = rect.end_col - rightward;
        src.start_col = rect.start_col + rightward;
        src.end_col = rect.end_col;
    } else {
        /* rect: [................XXX]
         * src:  [----------------]
         * dest:    [----------------]
         */
        int leftward = -rightward;
        dest.start_col = rect.start_col + leftward;
        dest.end_col = rect.end_col;
        src.start_col = rect.start_col;
        src.end_col = rect.end_col - leftward;
    }

    if (downward >= 0) {
        dest.start_row = rect.start_row;
        dest.end_row = rect.end_row - downward;
        src.start_row = rect.start_row + downward;
        src.end_row = rect.end_row;
    } else {
        int upward = -downward;
        dest.start_row = rect.start_row + upward;
        dest.end_row = rect.end_row;
        src.start_row = rect.start_row;
        src.end_row = rect.end_row - upward;
    }

    if (moverect)
        status = (*moverect)(dest, src, user).status;

    if (downward > 0)
        rect.start_row = rect.end_row - downward;
    else if (downward < 0)
        rect.end_row = rect.start_row - downward;

    if (rightward > 0)
        rect.start_col = rect.end_col - rightward;
    else if (rightward < 0)
        rect.end_col = rect.start_col - rightward;

    VTermStatus erased = (*eraserect)(rect, 0, user).status;
    return status != VTERM_OK ? status : erased;
}

void vterm_copy_cells(VTermRect dest,
                      VTermRect src,
                      void (*copycell)(VTermPos dest, VTermPos src, void *user),
                      void *user) {
    int downward = src.start_row - dest.start_row;
    int rightward = src.start_col - dest.start_col;

    int init_row, test_row, init_col, test_col;
    int inc_row, inc_col;

    if (downward < 0) {
        init_row = dest.end_row - 1;
        test_row = dest.start_row - 1;
        inc_row = -1;
    } else /* downward >= 0 */ {
        init_row = dest.start_row;
        test_row = dest.end_row;
        inc_row = +1;
    }

    if (rightward < 0) {
        init_col = dest.end_col - 1;
        test_col = dest.start_col - 1;
        inc_col = -1;
    } else /* rightward >= 0 */ {
        init_col = dest.start_col;
        test_col = dest.end_col;
        inc_col = +1;
    }

    VTermPos pos;
    for (pos.row = init_row; pos.row != test_row; pos.row += inc_row)
        for (pos.col = init_col; pos.col != test_col; pos.col += inc_col) {
            VTermPos srcpos = {pos.row + downward, pos.col + rightward};
            (*copycell)(pos, srcpos, user);
        }
}

VTermStatus vterm_check_version(int major, int minor) {
    return major == VTERM_VERSION_MAJOR && minor == VTERM_VERSION_MINOR
        ? VTERM_OK : VTERM_ERR_VERSION_MISMATCH;
}
