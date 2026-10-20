#ifndef BB_UI_TEXT_H
#define BB_UI_TEXT_H

#include <stddef.h>
#include <wchar.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BB_UI_STYLE_NORMAL = 0,
    BB_UI_STYLE_STRONG = 1,
    BB_UI_STYLE_UNDERLINE = 2
} bb_ui_style;

/* Private control codes used by generated string tables in place of <b>, <u>
 * and the closing tag. bb_ui_format copies them unchanged; bb_ui_parse_runs
 * and bb_ui_wrap consume them. Never shown to the user. */
#define BB_UI_MARK_END       L'\x10'
#define BB_UI_MARK_STRONG    L'\x11'
#define BB_UI_MARK_UNDERLINE L'\x12'

enum {
    BB_UI_ALIGN_LEFT = 0,
    BB_UI_ALIGN_CENTER = 1,
    BB_UI_ALIGN_RIGHT = 2
};

enum {
    BB_UI_LAYER_BASE = 0,
    BB_UI_LAYER_MODAL = 1
};

typedef struct {
    const wchar_t *text;
    size_t length;
    bb_ui_style style;
} bb_ui_run;

typedef struct {
    bb_ui_run run;
    int x;
    int advance;
    int line;
} bb_ui_span;

typedef struct {
    const wchar_t *text;
    size_t length;
    int x;
    int y;
    int height;
    int first_span;
    int span_count;
    int width;       /* measured ink width, excluding collapsed spaces at the ends */
} bb_ui_line;

typedef unsigned (*bb_ui_measure_fn)(void *context, const wchar_t *text,
                                     size_t length, bb_ui_style style);

typedef struct {
    int ok;
    int error_offset;
    wchar_t unknown;
} bb_ui_format_result;

typedef struct {
    int left;
    int top;
    int right;
    int bottom;
    int disabled;
    int layer;
} bb_ui_hit_rect;

/* Replaces named placeholders only. Unknown names fail closed. */
bb_ui_format_result bb_ui_format(const wchar_t *source, wchar_t *out, size_t cap,
                                 const wchar_t *model, const wchar_t *version,
                                 int step, const wchar_t *filename,
                                 const wchar_t *app_version);

/* Splits only <b>...</b>, <u>...</u> and the equivalent BB_UI_MARK_* codes;
 * every other character is literal. */
int bb_ui_parse_runs(const wchar_t *source, bb_ui_run *runs, size_t run_cap,
                     int *error_offset);

/* Wraps text by measured width.
 * - Whitespace collapses to one measured space between words on a line and is
 *   dropped at line starts/ends; '\n' forces a break (blank lines are kept).
 * - Breaks at spaces and between CJK characters, with kinsoku: closing
 *   punctuation never starts a line and opening punctuation never ends one.
 * - A word wider than the line is split by characters, preferably after
 *   '-', '_', '.', '/'.
 * - Every line (blank ones included) gets a bb_ui_line, so the paragraph
 *   height is line_count * line_height. Spans never contain whitespace. */
int bb_ui_wrap(const wchar_t *source, int width, bb_ui_measure_fn measure,
               void *context, int line_height, int max_width,
               bb_ui_span *spans, size_t span_cap, int *span_count,
               bb_ui_line *lines, size_t line_cap, int *line_count,
               int *x_offset, int *total_width, int *error_offset);

/* Horizontal offset of a wrapped line inside width for BB_UI_ALIGN_*. */
int bb_ui_line_offset(const bb_ui_line *line, int width, int align);

static inline int bb_ui_clamp_scroll(int requested, int content_height, int viewport_height)
{
    int maximum = content_height > viewport_height ? content_height - viewport_height : 0;
    if (requested < 0) return 0;
    return requested > maximum ? maximum : requested;
}

typedef struct {
    int left;
    int top;
    int right;
    int bottom;
    int content_height;
    int max_scroll;
} bb_ui_modal_geometry;

bb_ui_modal_geometry bb_ui_modal_layout(int client_width, int client_height,
                                        int desired_width, int desired_height,
                                        int margin, int content_height);

/* Returns the topmost usable element, but only inside the active modal layer. */
int bb_ui_hit_element(const bb_ui_hit_rect *elements, size_t stride, size_t count,
                      int modal, int x, int y);

#ifdef __cplusplus
}
#endif

#endif
