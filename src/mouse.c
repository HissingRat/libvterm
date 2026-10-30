#include "vterm_internal.h"

#include "utf8.h"

static void output_mouse(VTermState *state, int code, int pressed, int modifiers, int col, int row) {
    modifiers <<= 2;

    switch (state->mouse_protocol) {
        case MOUSE_X10:
            if (col + 0x21 > 0xff)
                col = 0xff - 0x21;
            if (row + 0x21 > 0xff)
                row = 0xff - 0x21;

            if (!pressed)
                code = 3;

            vterm_push_output_sprintf_ctrl(state->vt, C1_CSI, "M%c%c%c",
                                           (code | modifiers) + 0x20, col + 0x21, row + 0x21);
            break;

        case MOUSE_UTF8: {
            char utf8[18];
            size_t len = 0;

            if (!pressed)
                code = 3;

            len += fill_utf8((code | modifiers) + 0x20, utf8 + len);
            len += fill_utf8(col + 0x21, utf8 + len);
            len += fill_utf8(row + 0x21, utf8 + len);
            utf8[len] = 0;

            vterm_push_output_sprintf_ctrl(state->vt, C1_CSI, "M%s", utf8);
        }
        break;

        case MOUSE_SGR:
            vterm_push_output_sprintf_ctrl(state->vt, C1_CSI, "<%d;%d;%d%c",
                                           code | modifiers, col + 1, row + 1, pressed ? 'M' : 'm');
            break;

        case MOUSE_RXVT:
            if (!pressed)
                code = 3;

            vterm_push_output_sprintf_ctrl(state->vt, C1_CSI, "%d;%d;%dM",
                                           code | modifiers, col + 1, row + 1);
            break;
    }
}

static void vterm_mouse_move_impl(VTerm *vt, int row, int col, VTermModifier mod) {
    VTermState *state = vt->state;

    if (col == state->mouse_col && row == state->mouse_row)
        return;

    state->mouse_col = col;
    state->mouse_row = row;

    if ((state->mouse_flags & MOUSE_WANT_DRAG && state->mouse_buttons) ||
        (state->mouse_flags & MOUSE_WANT_MOVE)) {
        int button = state->mouse_buttons & 0x01
                         ? 1
                         : state->mouse_buttons & 0x02
                               ? 2
                               : state->mouse_buttons & 0x04
                                     ? 3
                                     : 4;
        output_mouse(state, button - 1 + 0x20, 1, mod, col, row);
    }
}

VTermStatus vterm_mouse_move(VTerm *vt, int row, int col, VTermModifier mod) {
    if (row < 0 || col < 0 || row > INT_MAX - 0x21 || col > INT_MAX - 0x21 ||
        (mod & ~VTERM_ALL_MODS_MASK)) return VTERM_ERR_INVALID_ARGUMENT;
    if (!vt || !vt->state) return VTERM_ERR_INVALID_ARGUMENT;
    vt_begin(vt);
    if (vterm_output_flush(vt) != VTERM_OK) return vt_end(vt);
    vterm_mouse_move_impl(vt, row, col, mod);
    return vt_end(vt);
}

static void vterm_mouse_button_impl(VTerm *vt, int button, bool pressed, VTermModifier mod) {
    VTermState *state = vt->state;

    int old_buttons = state->mouse_buttons;

    if (button > 0 && button <= 3) {
        if (pressed)
            state->mouse_buttons |= (1 << (button - 1));
        else
            state->mouse_buttons &= ~(1 << (button - 1));
    }

    /* Most of the time we don't get button releases from 4/5 */
    if (state->mouse_buttons == old_buttons && button < 4)
        return;

    if (!state->mouse_flags)
        return;

    if (button < 4) {
        output_mouse(state, button - 1, pressed, mod, state->mouse_col, state->mouse_row);
    } else if (button < 8) {
        output_mouse(state, button - 4 + 0x40, pressed, mod, state->mouse_col, state->mouse_row);
    }
}

VTermStatus vterm_mouse_button(VTerm *vt, int button, bool pressed, VTermModifier mod) {
    if (button < 1 || button > 7 || (mod & ~VTERM_ALL_MODS_MASK)) return VTERM_ERR_INVALID_ARGUMENT;
    if (!vt || !vt->state) return VTERM_ERR_INVALID_ARGUMENT;
    vt_begin(vt);
    if (vterm_output_flush(vt) != VTERM_OK) return vt_end(vt);
    vterm_mouse_button_impl(vt, button, pressed, mod);
    return vt_end(vt);
}
