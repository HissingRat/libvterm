#ifndef __VTERM_H__
#define __VTERM_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include "vterm_keycodes.h"

#define VTERM_VERSION_MAJOR 0
#define VTERM_VERSION_MINOR 4
#define VTERM_VERSION_PATCH 0

#define VTERM_CHECK_VERSION \
        vterm_check_version(VTERM_VERSION_MAJOR, VTERM_VERSION_MINOR)

/* Any cell can contain at most one basic printing character and 5 combining
 * characters. This number could be changed but will be ABI-incompatible if
 * you do */
#define VTERM_MAX_CHARS_PER_CELL 6

typedef struct VTerm VTerm;
typedef struct VTermState VTermState;
typedef struct VTermScreen VTermScreen;

#define VTERM_API_VERSION 1

typedef enum {
  VTERM_OK = 0,
  VTERM_ERR_INVALID_ARGUMENT,
  VTERM_ERR_NO_MEMORY,
  VTERM_ERR_OVERFLOW,
  VTERM_ERR_NO_SPACE,
  VTERM_ERR_HOST,
  VTERM_ERR_VERSION_MISMATCH
} VTermStatus;

typedef struct {
  VTermStatus status;
  bool handled; /* Also means "row available" for sb_popline; ignored on error. */
} VTermCallbackResult;

typedef enum { VTERM_LOG_DEBUG, VTERM_LOG_ERROR } VTermLogLevel;
typedef void VTermLogCallback(void *user, VTermLogLevel level, const char *message, size_t len);
/* Must not return. The library traps if this contract is violated. */
typedef void VTermFatalCallback(void *user, const char *message, size_t len);


typedef struct {
  int row;
  int col;
} VTermPos;

/* some small utility functions; we can just keep these static here */

/* order points by on-screen flow order */
static inline int vterm_pos_cmp(VTermPos a, VTermPos b)
{
  return (a.row == b.row) ? a.col - b.col : a.row - b.row;
}

typedef struct {
  int start_row;
  int end_row;
  int start_col;
  int end_col;
} VTermRect;

/* true if the rect contains the point */
static inline int vterm_rect_contains(VTermRect r, VTermPos p)
{
  return p.row >= r.start_row && p.row < r.end_row &&
         p.col >= r.start_col && p.col < r.end_col;
}

/* move a rect */
static inline void vterm_rect_move(VTermRect *rect, int row_delta, int col_delta)
{
  rect->start_row += row_delta; rect->end_row += row_delta;
  rect->start_col += col_delta; rect->end_col += col_delta;
}

/**
 * Bit-field describing the content of the tagged union `VTermColor`.
 */
typedef enum {
  /**
   * If the lower bit of `type` is not set, the colour is 24-bit RGB.
   */
  VTERM_COLOR_RGB = 0x00,

  /**
   * The colour is an index into a palette of 256 colours.
   */
  VTERM_COLOR_INDEXED = 0x01,

  /**
   * Mask that can be used to extract the RGB/Indexed bit.
   */
  VTERM_COLOR_TYPE_MASK = 0x01,

  /**
   * If set, indicates that this colour should be the default foreground
   * color, i.e. there was no SGR request for another colour. When
   * rendering this colour it is possible to ignore "idx" and just use a
   * colour that is not in the palette.
   */
  VTERM_COLOR_DEFAULT_FG = 0x02,

  /**
   * If set, indicates that this colour should be the default background
   * color, i.e. there was no SGR request for another colour. A common
   * option when rendering this colour is to not render a background at
   * all, for example by rendering the window transparently at this spot.
   */
  VTERM_COLOR_DEFAULT_BG = 0x04,

  /**
   * Mask that can be used to extract the default foreground/background bit.
   */
  VTERM_COLOR_DEFAULT_MASK = 0x06
} VTermColorType;

/**
 * Returns true if the VTERM_COLOR_RGB `type` flag is set, indicating that the
 * given VTermColor instance is an indexed colour.
 */
#define VTERM_COLOR_IS_INDEXED(col) \
  (((col)->type & VTERM_COLOR_TYPE_MASK) == VTERM_COLOR_INDEXED)

/**
 * Returns true if the VTERM_COLOR_INDEXED `type` flag is set, indicating that
 * the given VTermColor instance is an rgb colour.
 */
#define VTERM_COLOR_IS_RGB(col) \
  (((col)->type & VTERM_COLOR_TYPE_MASK) == VTERM_COLOR_RGB)

/**
 * Returns true if the VTERM_COLOR_DEFAULT_FG `type` flag is set, indicating
 * that the given VTermColor instance corresponds to the default foreground
 * color.
 */
#define VTERM_COLOR_IS_DEFAULT_FG(col) \
  (!!((col)->type & VTERM_COLOR_DEFAULT_FG))

/**
 * Returns true if the VTERM_COLOR_DEFAULT_BG `type` flag is set, indicating
 * that the given VTermColor instance corresponds to the default background
 * color.
 */
#define VTERM_COLOR_IS_DEFAULT_BG(col) \
  (!!((col)->type & VTERM_COLOR_DEFAULT_BG))

/**
 * Tagged union storing either an RGB color or an index into a colour palette.
 * In order to convert indexed colours to RGB, you may use the
 * vterm_state_convert_color_to_rgb() or vterm_screen_convert_color_to_rgb()
 * functions which lookup the RGB colour from the palette maintained by a
 * VTermState or VTermScreen instance.
 */
typedef union {
  /**
   * Tag indicating which union member is actually valid. This variable
   * coincides with the `type` member of the `rgb` and the `indexed` struct
   * in memory. Please use the `VTERM_COLOR_IS_*` test macros to check whether
   * a particular type flag is set.
   */
  uint8_t type;

  /**
   * Valid if `VTERM_COLOR_IS_RGB(type)` is true. Holds the RGB colour values.
   */
  struct {
    /**
     * Same as the top-level `type` member stored in VTermColor.
     */
    uint8_t type;

    /**
     * The actual 8-bit red, green, blue colour values.
     */
    uint8_t red, green, blue;
  } rgb;

  /**
   * If `VTERM_COLOR_IS_INDEXED(type)` is true, this member holds the index into
   * the colour palette.
   */
  struct {
    /**
     * Same as the top-level `type` member stored in VTermColor.
     */
    uint8_t type;

    /**
     * Index into the colour map.
     */
    uint8_t idx;
  } indexed;
} VTermColor;

/**
 * Constructs a new VTermColor instance representing the given RGB values.
 */
static inline void vterm_color_rgb(VTermColor *col, uint8_t red, uint8_t green,
                                   uint8_t blue)
{
  col->type = VTERM_COLOR_RGB;
  col->rgb.red   = red;
  col->rgb.green = green;
  col->rgb.blue  = blue;
}

/**
 * Construct a new VTermColor instance representing an indexed color with the
 * given index.
 */
static inline void vterm_color_indexed(VTermColor *col, uint8_t idx)
{
  col->type = VTERM_COLOR_INDEXED;
  col->indexed.idx = idx;
}

/**
 * Compares two colours. Returns true if the colors are equal, false otherwise.
 */
int vterm_color_is_equal(const VTermColor *a, const VTermColor *b);

typedef enum {
  /* VTERM_VALUETYPE_NONE = 0 */
  VTERM_VALUETYPE_BOOL = 1,
  VTERM_VALUETYPE_INT,
  VTERM_VALUETYPE_STRING,
  VTERM_VALUETYPE_COLOR,

  VTERM_N_VALUETYPES
} VTermValueType;

typedef struct {
  const char *str;
  size_t      len : 30;
  bool        initial : 1;
  bool        final : 1;
} VTermStringFragment;

typedef union {
  int boolean;
  int number;
  VTermStringFragment string;
  VTermColor color;
} VTermValue;

typedef enum {
  /* VTERM_ATTR_NONE = 0 */
  VTERM_ATTR_BOLD = 1,   // bool:   1, 22
  VTERM_ATTR_UNDERLINE,  // number: 4, 21, 24
  VTERM_ATTR_ITALIC,     // bool:   3, 23
  VTERM_ATTR_BLINK,      // bool:   5, 25
  VTERM_ATTR_REVERSE,    // bool:   7, 27
  VTERM_ATTR_CONCEAL,    // bool:   8, 28
  VTERM_ATTR_STRIKE,     // bool:   9, 29
  VTERM_ATTR_FONT,       // number: 10-19
  VTERM_ATTR_FOREGROUND, // color:  30-39 90-97
  VTERM_ATTR_BACKGROUND, // color:  40-49 100-107
  VTERM_ATTR_SMALL,      // bool:   73, 74, 75
  VTERM_ATTR_BASELINE,   // number: 73, 74, 75

  VTERM_N_ATTRS
} VTermAttr;

typedef enum {
  /* VTERM_PROP_NONE = 0 */
  VTERM_PROP_CURSORVISIBLE = 1, // bool
  VTERM_PROP_CURSORBLINK,       // bool
  VTERM_PROP_ALTSCREEN,         // bool
  VTERM_PROP_TITLE,             // string
  VTERM_PROP_ICONNAME,          // string
  VTERM_PROP_REVERSE,           // bool
  VTERM_PROP_CURSORSHAPE,       // number
  VTERM_PROP_MOUSE,             // number
  VTERM_PROP_FOCUSREPORT,       // bool

  VTERM_N_PROPS
} VTermProp;

enum {
  VTERM_PROP_CURSORSHAPE_BLOCK = 1,
  VTERM_PROP_CURSORSHAPE_UNDERLINE,
  VTERM_PROP_CURSORSHAPE_BAR_LEFT,

  VTERM_N_PROP_CURSORSHAPES
};

enum {
  VTERM_PROP_MOUSE_NONE = 0,
  VTERM_PROP_MOUSE_CLICK,
  VTERM_PROP_MOUSE_DRAG,
  VTERM_PROP_MOUSE_MOVE,

  VTERM_N_PROP_MOUSES
};

typedef enum {
  VTERM_SELECTION_CLIPBOARD = (1<<0),
  VTERM_SELECTION_PRIMARY   = (1<<1),
  VTERM_SELECTION_SECONDARY = (1<<2),
  VTERM_SELECTION_SELECT    = (1<<3),
  VTERM_SELECTION_CUT0      = (1<<4), /* also CUT1 .. CUT7 by bitshifting */
} VTermSelectionMask;

typedef struct {
  const uint32_t *chars;
  int             width;
  unsigned int    protected_cell:1;  /* DECSCA-protected against DECSEL/DECSED */
  unsigned int    dwl:1;             /* DECDWL or DECDHL double-width line */
  unsigned int    dhl:2;             /* DECDHL double-height line (1=top 2=bottom) */
} VTermGlyphInfo;

typedef struct {
  unsigned int    doublewidth:1;     /* DECDWL or DECDHL line */
  unsigned int    doubleheight:2;    /* DECDHL line (1=top 2=bottom) */
  unsigned int    continuation:1;    /* Line is a flow continuation of the previous */
} VTermLineInfo;

/* Copies of VTermState fields that the 'resize' callback might have reason to
 * edit. 'resize' callback gets total control of these fields and may
 * free-and-reallocate them if required. They will be copied back from the
 * struct after the callback has returned. A custom resize callback that
 * rejects the operation must not free old lineinfos or modify these fields.
 */
typedef struct {
  VTermPos pos;                /* current cursor position */
  VTermLineInfo *lineinfos[2]; /* [1] may be NULL */
} VTermStateFields;

typedef struct {
  /* Required callbacks. malloc returns suitably aligned storage or NULL.
   * libvterm zeroes storage itself; free must not fail. */
  void *(*malloc)(size_t size, void *allocdata);
  void  (*free)(void *ptr, void *allocdata);
} VTermAllocatorFunctions;

VTermStatus vterm_check_version(int major, int minor);

struct VTermBuilder {
  int ver; /* Must be VTERM_API_VERSION. */

  int rows, cols;

  const VTermAllocatorFunctions *allocator; /* Copied into the instance. */
  void *allocdata;
  VTermFatalCallback *fatal; /* Required in every build. */
  VTermLogCallback *log;     /* Optional; NULL disables diagnostics. */
  void *userdata;

  /* Override default sizes for various structures */
  size_t outbuffer_len;  /* default: 4096 */
  size_t tmpbuffer_len;  /* default: 4096 */
};

/* Both allocator functions and fatal are mandatory. outbuffer_len must be
 * >= tmpbuffer_len. On failure *out_vt is NULL and all allocations are freed. */
VTermStatus vterm_build(const struct VTermBuilder *builder, VTerm **out_vt);

void   vterm_free(VTerm* vt);

void vterm_get_size(const VTerm *vt, int *rowsp, int *colsp);
/* Validation/OOM failures leave the old size and cells unchanged. Host
 * notification failures can be reported after a consistent resize commits. */
VTermStatus vterm_set_size(VTerm *vt, int rows, int cols);

int  vterm_get_utf8(const VTerm *vt);
void vterm_set_utf8(VTerm *vt, int is_utf8);

/* consumed includes accepted partial UTF-8/escape sequences. On error, only
 * retry the suffix after consumed, never the whole input. Rejected output
 * stays queued: retry vterm_output_flush rather than replaying its input.
 * Host callbacks are synchronous and must not reenter the same instance. */
VTermStatus vterm_input_write(VTerm *vt, const char *bytes, size_t len, size_t *consumed);

/* Setting output callback will override the buffer logic */
/* All-or-nothing synchronous acceptance; failed output remains queued. */
typedef VTermStatus VTermOutputCallback(const char *s, size_t len, void *user);
void vterm_output_set_callback(VTerm *vt, VTermOutputCallback *func, void *user);
VTermStatus vterm_output_flush(VTerm *vt);

/* Queued bytes remain inspectable/readable even after a sink rejects output.
 * Reading removes bytes from the queue; it does not invoke the output sink. */
size_t vterm_output_get_buffer_size(const VTerm *vt);
size_t vterm_output_get_buffer_current(const VTerm *vt);
size_t vterm_output_get_buffer_remaining(const VTerm *vt);

size_t vterm_output_read(VTerm *vt, char *buffer, size_t len);

VTermStatus vterm_keyboard_unichar(VTerm *vt, uint32_t c, VTermModifier mod);
VTermStatus vterm_keyboard_key(VTerm *vt, VTermKey key, VTermModifier mod);

VTermStatus vterm_keyboard_start_paste(VTerm *vt);
VTermStatus vterm_keyboard_end_paste(VTerm *vt);

VTermStatus vterm_mouse_move(VTerm *vt, int row, int col, VTermModifier mod);
VTermStatus vterm_mouse_button(VTerm *vt, int button, bool pressed, VTermModifier mod);

// ------------
// Parser layer
// ------------

/* Flag to indicate non-final subparameters in a single CSI parameter.
 * Consider
 *   CSI 1;2:3:4;5a
 * 1 4 and 5 are final.
 * 2 and 3 are non-final and will have this bit set
 *
 * Don't confuse this with the final byte of the CSI escape; 'a' in this case.
 */
#define CSI_ARG_FLAG_MORE (1U<<31)
#define CSI_ARG_MASK      (~(1U<<31))

#define CSI_ARG_HAS_MORE(a) ((a) & CSI_ARG_FLAG_MORE)
#define CSI_ARG(a)          ((a) & CSI_ARG_MASK)

/* Can't use -1 to indicate a missing argument; use this instead */
#define CSI_ARG_MISSING ((1UL<<31)-1)

#define CSI_ARG_IS_MISSING(a) (CSI_ARG(a) == CSI_ARG_MISSING)
#define CSI_ARG_OR(a,def)     (CSI_ARG(a) == CSI_ARG_MISSING ? (def) : CSI_ARG(a))
#define CSI_ARG_COUNT(a)      (CSI_ARG(a) == CSI_ARG_MISSING || CSI_ARG(a) == 0 ? 1 : CSI_ARG(a))

typedef struct {
  VTermStatus (*text)(const char *bytes, size_t len, size_t *consumed, void *user);
  VTermCallbackResult (*control)(unsigned char control, void *user);
  VTermCallbackResult (*escape)(const char *bytes, size_t len, void *user);
  VTermCallbackResult (*csi)(const char *leader, const long args[], int argcount, const char *intermed, char command, void *user);
  VTermCallbackResult (*osc)(int command, VTermStringFragment frag, void *user);
  VTermCallbackResult (*dcs)(const char *command, size_t commandlen, VTermStringFragment frag, void *user);
  VTermCallbackResult (*apc)(VTermStringFragment frag, void *user);
  VTermCallbackResult (*pm)(VTermStringFragment frag, void *user);
  VTermCallbackResult (*sos)(VTermStringFragment frag, void *user);
  VTermCallbackResult (*resize)(int rows, int cols, void *user);
} VTermParserCallbacks;

void  vterm_parser_set_callbacks(VTerm *vt, const VTermParserCallbacks *callbacks, void *user);
void *vterm_parser_get_cbdata(VTerm *vt);

/* Normally NUL, CAN, SUB and DEL are ignored. Setting this true causes them
 * to be emitted by the 'control' callback
 */
void vterm_parser_set_emit_nul(VTerm *vt, bool emit);

// -----------
// State layer
// -----------

typedef struct {
  VTermCallbackResult (*putglyph)(VTermGlyphInfo *info, VTermPos pos, void *user);
  VTermCallbackResult (*movecursor)(VTermPos pos, VTermPos oldpos, int visible, void *user);
  VTermCallbackResult (*scrollrect)(VTermRect rect, int downward, int rightward, void *user);
  VTermCallbackResult (*moverect)(VTermRect dest, VTermRect src, void *user);
  VTermCallbackResult (*erase)(VTermRect rect, int selective, void *user);
  VTermCallbackResult (*initpen)(void *user);
  VTermCallbackResult (*setpenattr)(VTermAttr attr, VTermValue *val, void *user);
  VTermCallbackResult (*settermprop)(VTermProp prop, VTermValue *val, void *user);
  VTermCallbackResult (*bell)(void *user);
  VTermCallbackResult (*resize)(int rows, int cols, VTermStateFields *fields, void *user);
  VTermCallbackResult (*setlineinfo)(int row, const VTermLineInfo *newinfo, const VTermLineInfo *oldinfo, void *user);
  VTermCallbackResult (*sb_clear)(void *user);
  // ABI-compat only enabled if vterm_state_callbacks_has_premove() is invoked
  VTermCallbackResult (*premove)(VTermRect dest, void *user);
} VTermStateCallbacks;

typedef struct {
  VTermCallbackResult (*control)(unsigned char control, void *user);
  VTermCallbackResult (*csi)(const char *leader, const long args[], int argcount, const char *intermed, char command, void *user);
  VTermCallbackResult (*osc)(int command, VTermStringFragment frag, void *user);
  VTermCallbackResult (*dcs)(const char *command, size_t commandlen, VTermStringFragment frag, void *user);
  VTermCallbackResult (*apc)(VTermStringFragment frag, void *user);
  VTermCallbackResult (*pm)(VTermStringFragment frag, void *user);
  VTermCallbackResult (*sos)(VTermStringFragment frag, void *user);
} VTermStateFallbacks;

typedef struct {
  VTermCallbackResult (*set)(VTermSelectionMask mask, VTermStringFragment frag, void *user);
  VTermCallbackResult (*query)(VTermSelectionMask mask, void *user);
} VTermSelectionCallbacks;

VTermStatus vterm_obtain_state(VTerm *vt, VTermState **out_state);

VTermStatus vterm_state_set_callbacks(VTermState *state, const VTermStateCallbacks *callbacks, void *user);
void *vterm_state_get_cbdata(VTermState *state);

void vterm_state_callbacks_has_premove(VTermState *state);

void  vterm_state_set_unrecognised_fallbacks(VTermState *state, const VTermStateFallbacks *fallbacks, void *user);
void *vterm_state_get_unrecognised_fbdata(VTermState *state);

VTermStatus vterm_state_reset(VTermState *state, int hard);
void vterm_state_get_cursorpos(const VTermState *state, VTermPos *cursorpos);
void vterm_state_get_default_colors(const VTermState *state, VTermColor *default_fg, VTermColor *default_bg);
void vterm_state_get_palette_color(const VTermState *state, int index, VTermColor *col);
VTermStatus vterm_state_set_default_colors(VTermState *state, const VTermColor *default_fg, const VTermColor *default_bg);
VTermStatus vterm_state_set_palette_color(VTermState *state, int index, const VTermColor *col);
VTermStatus vterm_state_set_bold_highbright(VTermState *state, int bold_is_highbright);
int  vterm_state_get_penattr(const VTermState *state, VTermAttr attr, VTermValue *val);
VTermStatus vterm_state_set_termprop(VTermState *state, VTermProp prop, VTermValue *val, bool *handled);
VTermStatus vterm_state_focus_in(VTermState *state);
VTermStatus vterm_state_focus_out(VTermState *state);
const VTermLineInfo *vterm_state_get_lineinfo(const VTermState *state, int row);

/**
 * Makes sure that the given color `col` is indeed an RGB colour. After this
 * function returns, VTERM_COLOR_IS_RGB(col) will return true, while all other
 * flags stored in `col->type` will have been reset.
 *
 * @param state is the VTermState instance from which the colour palette should
 * be extracted.
 * @param col is a pointer at the VTermColor instance that should be converted
 * to an RGB colour.
 */
void vterm_state_convert_color_to_rgb(const VTermState *state, VTermColor *col);

VTermStatus vterm_state_set_selection_callbacks(VTermState *state, const VTermSelectionCallbacks *callbacks, void *user,
    char *buffer, size_t buflen);

VTermStatus vterm_state_send_selection(VTermState *state, VTermSelectionMask mask, VTermStringFragment frag);

// ------------
// Screen layer
// ------------

typedef struct {
    unsigned int bold      : 1;
    unsigned int underline : 2;
    unsigned int italic    : 1;
    unsigned int blink     : 1;
    unsigned int reverse   : 1;
    unsigned int conceal   : 1;
    unsigned int strike    : 1;
    unsigned int font      : 4; /* 0 to 9 */
    unsigned int dwl       : 1; /* On a DECDWL or DECDHL line */
    unsigned int dhl       : 2; /* On a DECDHL line (1=top 2=bottom) */
    unsigned int small     : 1;
    unsigned int baseline  : 2;
} VTermScreenCellAttrs;

enum {
  VTERM_UNDERLINE_OFF,
  VTERM_UNDERLINE_SINGLE,
  VTERM_UNDERLINE_DOUBLE,
  VTERM_UNDERLINE_CURLY,
};

enum {
  VTERM_BASELINE_NORMAL,
  VTERM_BASELINE_RAISE,
  VTERM_BASELINE_LOWER,
};

typedef struct {
  uint32_t chars[VTERM_MAX_CHARS_PER_CELL];
  char     width;
  VTermScreenCellAttrs attrs;
  VTermColor fg, bg;
} VTermScreenCell;

typedef struct {
  VTermCallbackResult (*damage)(VTermRect rect, void *user);
  VTermCallbackResult (*moverect)(VTermRect dest, VTermRect src, void *user);
  VTermCallbackResult (*movecursor)(VTermPos pos, VTermPos oldpos, int visible, void *user);
  VTermCallbackResult (*settermprop)(VTermProp prop, VTermValue *val, void *user);
  VTermCallbackResult (*bell)(void *user);
  VTermCallbackResult (*resize)(int rows, int cols, void *user);
  VTermCallbackResult (*sb_pushline)(int cols, const VTermScreenCell *cells, void *user);
  VTermCallbackResult (*sb_popline)(int cols, VTermScreenCell *cells, void *user);
  VTermCallbackResult (*sb_clear)(void* user);
  /* ABI-compat this is only used if vterm_screen_callbacks_has_pushline4() is called */
  VTermCallbackResult (*sb_pushline4)(int cols, const VTermScreenCell *cells, bool continuation, void *user);
} VTermScreenCallbacks;

VTermStatus vterm_obtain_screen(VTerm *vt, VTermScreen **out_screen);

void  vterm_screen_set_callbacks(VTermScreen *screen, const VTermScreenCallbacks *callbacks, void *user);
void *vterm_screen_get_cbdata(VTermScreen *screen);

void vterm_screen_callbacks_has_pushline4(VTermScreen *screen);

void  vterm_screen_set_unrecognised_fallbacks(VTermScreen *screen, const VTermStateFallbacks *fallbacks, void *user);
void *vterm_screen_get_unrecognised_fbdata(VTermScreen *screen);

void vterm_screen_enable_reflow(VTermScreen *screen, bool reflow);

// Back-compat alias for the brief time it was in 0.3-RC1
#define vterm_screen_set_reflow  vterm_screen_enable_reflow

VTermStatus vterm_screen_enable_altscreen(VTermScreen *screen, int altscreen);

typedef enum {
  VTERM_DAMAGE_CELL,    /* every cell */
  VTERM_DAMAGE_ROW,     /* entire rows */
  VTERM_DAMAGE_SCREEN,  /* entire screen */
  VTERM_DAMAGE_SCROLL,  /* entire screen + scrollrect */

  VTERM_N_DAMAGES
} VTermDamageSize;

VTermStatus vterm_screen_flush_damage(VTermScreen *screen);
VTermStatus vterm_screen_set_damage_merge(VTermScreen *screen, VTermDamageSize size);

VTermStatus vterm_screen_reset(VTermScreen *screen, int hard);

/* Neither of these functions NUL-terminate the buffer */
size_t vterm_screen_get_chars(const VTermScreen *screen, uint32_t *chars, size_t len, const VTermRect rect);
size_t vterm_screen_get_text(const VTermScreen *screen, char *str, size_t len, const VTermRect rect);

typedef enum {
  VTERM_ATTR_BOLD_MASK       = 1 << 0,
  VTERM_ATTR_UNDERLINE_MASK  = 1 << 1,
  VTERM_ATTR_ITALIC_MASK     = 1 << 2,
  VTERM_ATTR_BLINK_MASK      = 1 << 3,
  VTERM_ATTR_REVERSE_MASK    = 1 << 4,
  VTERM_ATTR_STRIKE_MASK     = 1 << 5,
  VTERM_ATTR_FONT_MASK       = 1 << 6,
  VTERM_ATTR_FOREGROUND_MASK = 1 << 7,
  VTERM_ATTR_BACKGROUND_MASK = 1 << 8,
  VTERM_ATTR_CONCEAL_MASK    = 1 << 9,
  VTERM_ATTR_SMALL_MASK      = 1 << 10,
  VTERM_ATTR_BASELINE_MASK   = 1 << 11,

  VTERM_ALL_ATTRS_MASK = (1 << 12) - 1
} VTermAttrMask;

int vterm_screen_get_attrs_extent(const VTermScreen *screen, VTermRect *extent, VTermPos pos, VTermAttrMask attrs);

int vterm_screen_get_cell(const VTermScreen *screen, VTermPos pos, VTermScreenCell *cell);

int vterm_screen_is_eol(const VTermScreen *screen, VTermPos pos);

/**
 * Same as vterm_state_convert_color_to_rgb(), but takes a `screen` instead of a `state`
 * instance.
 */
void vterm_screen_convert_color_to_rgb(const VTermScreen *screen, VTermColor *col);

/**
 * Similar to vterm_state_set_default_colors(), but also resets colours in the
 * screen buffer(s)
 */
VTermStatus vterm_screen_set_default_colors(VTermScreen *screen, const VTermColor *default_fg, const VTermColor *default_bg);

// ---------
// Utilities
// ---------

VTermValueType vterm_get_attr_type(VTermAttr attr);
VTermValueType vterm_get_prop_type(VTermProp prop);

VTermStatus vterm_scroll_rect(VTermRect rect,
                       int downward,
                       int rightward,
                       VTermCallbackResult (*moverect)(VTermRect src, VTermRect dest, void *user),
                       VTermCallbackResult (*eraserect)(VTermRect rect, int selective, void *user),
                       void *user);

void vterm_copy_cells(VTermRect dest,
                      VTermRect src,
                      void (*copycell)(VTermPos dest, VTermPos src, void *user),
                      void *user);

#ifdef __cplusplus
}
#endif

#endif
