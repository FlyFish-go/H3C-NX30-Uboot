#include "bb_ui_text.h"

#include <limits.h>
#include <stdint.h>
#include <string.h>

static int is_space(wchar_t ch)
{
    return ch == L' ' || ch == L'\t' || ch == L'\r' || ch == L'\n';
}

static int is_cjk(wchar_t ch)
{
    return (ch >= 0x2E80 && ch <= 0x9FFF) || (ch >= 0xAC00 && ch <= 0xD7AF) ||
           (ch >= 0xF900 && ch <= 0xFAFF) || (ch >= 0xFF00 && ch <= 0xFFEF);
}

/* Typographic quotes and ellipsis behave like CJK punctuation: each one is a
 * separate piece whose break rules come from is_closing/is_opening. */
static int is_standalone(wchar_t ch)
{
    return is_cjk(ch) || (ch >= 0x2018 && ch <= 0x201F) || ch == 0x2026;
}

/* Must not start a line (simplified JIS X 4051 / GB/T 15834 kinsoku). */
static int is_closing(wchar_t ch)
{
    static const wchar_t list[] =
        L"\x3001\x3002\xFF0C\xFF0E\xFF1A\xFF1B\xFF01\xFF1F\xFF09\xFF3D\xFF5D"
        L"\x300D\x300F\x3009\x300B\x3011\x3015\x3017\x2019\x201D\x2026\xFF05"
        L"\x30FC\x3005\x309D\x309E\x30FD\x30FE\x00B7\x30FB"
        L"),.;:!?%]}";
    return ch && wcschr(list, ch) != NULL;
}

/* Must not end a line. */
static int is_opening(wchar_t ch)
{
    static const wchar_t list[] =
        L"\xFF08\xFF3B\xFF5B\x300C\x300E\x3008\x300A\x3010\x3014\x3016\x2018\x201C"
        L"([{";
    return ch && wcschr(list, ch) != NULL;
}

/* Preferred split points when one word is wider than the whole line. */
static int is_soft_break_after(wchar_t ch)
{
    return ch == L'-' || ch == L'_' || ch == L'.' || ch == L'/' || ch == L'\\' ||
           ch == L',' || ch == L';' || ch == L':';
}

static int is_high_surrogate(wchar_t ch)
{
    return (unsigned)ch >= 0xD800u && (unsigned)ch <= 0xDBFFu;
}

bb_ui_format_result bb_ui_format(const wchar_t *source, wchar_t *out, size_t cap,
                                 const wchar_t *model, const wchar_t *version,
                                 int step, const wchar_t *filename,
                                 const wchar_t *app_version)
{
    bb_ui_format_result result = {0, -1, 0};
    size_t used = 0;
    const wchar_t *p = source ? source : L"";
    if (!out || !cap) {
        result.error_offset = 0;
        return result;
    }
    while (*p) {
        if (*p == L'\x01' || *p == L'\x02' || *p == L'\x03' || *p == L'\x04' || *p == L'\x05') {
            const wchar_t *start = p;
            const wchar_t *value = NULL;
            wchar_t number[16];
            if (*p == L'\x01') value = model ? model : L"";
            else if (*p == L'\x02') value = version ? version : L"";
            else if (*p == L'\x03') {
                int n = step;
                wchar_t *q = number + sizeof(number) / sizeof(number[0]);
                *--q = 0;
                if (n <= 0) *--q = L'0';
                while (n > 0 && q > number) { *--q = (wchar_t)(L'0' + n % 10); n /= 10; }
                value = q;
            } else if (*p == L'\x04') value = filename ? filename : L"";
            else value = app_version ? app_version : L"";
            p++;
            while (*value) {
                if (used + 1 >= cap) { result.error_offset = (int)(start - source); return result; }
                out[used++] = *value++;
            }
        } else if (*p == L'{') {
            const wchar_t *start = p++;
            const wchar_t *value = NULL;
            wchar_t number[16];
            if (*p == L'm' && p[1] == L'}') { value = model ? model : L""; p += 2; }
            else if (*p == L'v' && p[1] == L'}') { value = version ? version : L""; p += 2; }
            else if (*p == L'f' && p[1] == L'}') { value = filename ? filename : L""; p += 2; }
            else if (*p == L'n' && p[1] == L'}') {
                int n = step;
                wchar_t *q = number + sizeof(number) / sizeof(number[0]);
                *--q = 0;
                if (n <= 0) *--q = L'0';
                while (n > 0 && q > number) { *--q = (wchar_t)(L'0' + n % 10); n /= 10; }
                value = q;
                p += 2;
            } else if (p[0] == L'v' && p[1] == L'e' && p[2] == L'r' && p[3] == L'}') {
                value = app_version ? app_version : L"";
                p += 4;
            } else {
                result.error_offset = (int)(start - (source ? source : L""));
                return result;
            }
            while (*value) {
                if (used + 1 >= cap) { result.error_offset = (int)(start - source); return result; }
                out[used++] = *value++;
            }
        } else {
            if (used + 1 >= cap) { result.error_offset = (int)(p - source); return result; }
            out[used++] = *p++;
        }
    }
    out[used] = 0;
    result.ok = 1;
    return result;
}

int bb_ui_parse_runs(const wchar_t *source, bb_ui_run *runs, size_t run_cap,
                     int *error_offset)
{
    const wchar_t *p = source ? source : L"";
    size_t count = 0;
    bb_ui_style style = BB_UI_STYLE_NORMAL;
    const wchar_t *segment = p;
    if (error_offset) *error_offset = -1;
    for (;;) {
        int strong = p[0] == L'<' && p[1] == L'b' && p[2] == L'>';
        int strong_end = p[0] == L'<' && p[1] == L'/' && p[2] == L'b' && p[3] == L'>';
        int under = p[0] == L'<' && p[1] == L'u' && p[2] == L'>';
        int under_end = p[0] == L'<' && p[1] == L'/' && p[2] == L'u' && p[3] == L'>';
        /* Generated tables carry markup as private control codes (no '<'). */
        int mark_strong = *p == BB_UI_MARK_STRONG;
        int mark_under = *p == BB_UI_MARK_UNDERLINE;
        int mark_end = *p == BB_UI_MARK_END;
        if (mark_end) {
            strong_end = style == BB_UI_STYLE_STRONG;
            under_end = !strong_end;
        }
        if (strong || strong_end || under || under_end || mark_strong || mark_under || *p == 0) {
            if (segment < p) {
                if (count >= run_cap) { if (error_offset) *error_offset = (int)(segment - source); return -1; }
                runs[count].text = segment;
                runs[count].length = (size_t)(p - segment);
                runs[count].style = style;
                count++;
            }
            if (!*p) break;
            if (strong || under || mark_strong || mark_under) {
                if (style != BB_UI_STYLE_NORMAL) { if (error_offset) *error_offset = (int)(p - source); return -1; }
                style = (strong || mark_strong) ? BB_UI_STYLE_STRONG : BB_UI_STYLE_UNDERLINE;
                p += (mark_strong || mark_under) ? 1 : 3;
            } else {
                bb_ui_style expected = strong_end ? BB_UI_STYLE_STRONG : BB_UI_STYLE_UNDERLINE;
                if (style != expected) { if (error_offset) *error_offset = (int)(p - source); return -1; }
                style = BB_UI_STYLE_NORMAL;
                p += mark_end ? 1 : 4;
            }
            segment = p;
        } else {
            p++;
        }
    }
    if (style != BB_UI_STYLE_NORMAL) {
        if (error_offset) *error_offset = (int)(p - source);
        return -1;
    }
    return (int)count;
}

typedef struct {
    const wchar_t *text;
    size_t length;
    bb_ui_style style;
    int newline;      /* hard line break marker, text unused */
    int space_before; /* collapsed whitespace precedes this piece */
    int glue;         /* no break opportunity before this piece */
} wrap_piece;

#define BB_UI_WRAP_MAX_PIECES 1024

typedef struct {
    bb_ui_measure_fn measure;
    void *context;
    int width;
    int line_height;
    bb_ui_span *spans;
    size_t span_cap;
    int *span_count;
    int x;
    int line;
    int max_seen;
} wrap_state;

static int add_span(wrap_state *w, const wchar_t *text, size_t length,
                    bb_ui_style style, int advance)
{
    bb_ui_span *span;
    if (*w->span_count >= (int)w->span_cap) return 0;
    span = &w->spans[*w->span_count];
    span->run.text = text;
    span->run.length = length;
    span->run.style = style;
    span->x = w->x;
    span->advance = advance;
    span->line = w->line;
    (*w->span_count)++;
    w->x += advance;
    if (w->x > w->max_seen) w->max_seen = w->x;
    return 1;
}

static void new_line(wrap_state *w)
{
    w->x = 0;
    w->line++;
}

static int measure_text(const wrap_state *w, const wchar_t *text, size_t length, bb_ui_style style)
{
    unsigned value = w->measure(w->context, text, length, style);
    return value > (unsigned)INT_MAX ? INT_MAX : (int)value;
}

/* Moves the tail of the previous span to a new line so a glued piece (for
 * example closing punctuation after a long file name) does not start a line
 * alone. Returns 0 if nothing could be moved. */
static int push_tail_to_next_line(wrap_state *w)
{
    bb_ui_span *last;
    if (*w->span_count == 0) return 0;
    last = &w->spans[*w->span_count - 1];
    if (last->line != w->line) return 0;
    if (last->run.length >= 2) {
        size_t keep = last->run.length - 1;
        const wchar_t *tail;
        size_t tail_length;
        bb_ui_style style = last->run.style;
        if (keep > 1 && is_high_surrogate(last->run.text[keep - 1])) keep--;
        tail = last->run.text + keep;
        tail_length = last->run.length - keep;
        last->run.length = keep;
        last->advance = measure_text(w, last->run.text, keep, style);
        new_line(w);
        return add_span(w, tail, tail_length, style, measure_text(w, tail, tail_length, style)) ? 1 : -1;
    }
    if (last->x == 0) return 0; /* the only span on its line: nothing to move */
    {
        bb_ui_span moved = *last;
        (*w->span_count)--;
        new_line(w);
        return add_span(w, moved.run.text, moved.run.length, moved.run.style, moved.advance) ? 1 : -1;
    }
}

/* Splits one over-long piece by characters, preferring a split after
 * '-', '_', '.', '/', etc. Always places at least one character per line. */
static int place_long_piece(wrap_state *w, const wrap_piece *piece, int glued)
{
    const wchar_t *p = piece->text;
    size_t left = piece->length;
    if (glued && w->x > 0 &&
        w->x + measure_text(w, p, left > 0 ? 1 : 0, piece->style) > w->width &&
        is_closing(*p)) {
        if (push_tail_to_next_line(w) < 0) return 0;
    }
    while (left > 0) {
        int whole = measure_text(w, p, left, piece->style);
        size_t fit = 0, soft = 0, k;
        if (w->x + whole <= w->width) return add_span(w, p, left, piece->style, whole);
        for (k = 1; k <= left; k++) {
            if (k < left && is_high_surrogate(p[k - 1])) continue;
            if (w->x + measure_text(w, p, k, piece->style) > w->width) break;
            fit = k;
            if (k < left && is_soft_break_after(p[k - 1])) soft = k;
        }
        if (fit == 0) {
            if (w->x > 0) { new_line(w); continue; }
            fit = (left > 1 && is_high_surrogate(p[0])) ? 2 : 1;
        } else if (soft > 0 && soft * 3 >= fit) {
            fit = soft;
        }
        if (!add_span(w, p, fit, piece->style, measure_text(w, p, fit, piece->style)))
            return 0;
        p += fit;
        left -= fit;
        if (left > 0) new_line(w);
    }
    return 1;
}

static int build_pieces(const bb_ui_run *runs, int run_count,
                        wrap_piece *pieces, int cap, int *count)
{
    int r, n = 0, pending_space = 0;
    for (r = 0; r < run_count; r++) {
        const wchar_t *p = runs[r].text;
        const wchar_t *end = p + runs[r].length;
        while (p < end) {
            const wchar_t *start = p;
            wrap_piece *piece;
            if (*p == L'\n' || is_space(*p)) {
                if (*p == L'\n') {
                    if (n >= cap) return -1;
                    pieces[n].text = p;
                    pieces[n].length = 0;
                    pieces[n].style = runs[r].style;
                    pieces[n].newline = 1;
                    pieces[n].space_before = 0;
                    pieces[n].glue = 0;
                    n++;
                    pending_space = 0;
                } else {
                    pending_space = 1;
                }
                p++;
                continue;
            }
            if (n >= cap) return -1;
            if (is_standalone(*p)) {
                p++;
            } else {
                while (p < end && !is_space(*p) && !is_standalone(*p)) p++;
            }
            piece = &pieces[n];
            piece->text = start;
            piece->length = (size_t)(p - start);
            piece->style = runs[r].style;
            piece->newline = 0;
            piece->space_before = pending_space;
            piece->glue = 0;
            if (!pending_space && n > 0 && !pieces[n - 1].newline) {
                const wrap_piece *prev = &pieces[n - 1];
                wchar_t prev_last = prev->text[prev->length - 1];
                int cjk_boundary = is_standalone(prev_last) || is_standalone(*start);
                /* Latin text split only by a style change stays one word. */
                piece->glue = !cjk_boundary || is_closing(*start) || is_opening(prev_last);
            }
            pending_space = 0;
            n++;
        }
    }
    *count = n;
    return 0;
}

int bb_ui_wrap(const wchar_t *source, int width, bb_ui_measure_fn measure,
               void *context, int line_height, int max_width,
               bb_ui_span *spans, size_t span_cap, int *span_count,
               bb_ui_line *lines, size_t line_cap, int *line_count,
               int *x_offset, int *total_width, int *error_offset)
{
    bb_ui_run runs[64];
    wrap_piece pieces[BB_UI_WRAP_MAX_PIECES];
    wrap_state w;
    int run_count, piece_count = 0, i, total_lines, span, line_number;
    if (error_offset) *error_offset = -1;
    if (!measure || width <= 0 || !spans || !lines || !span_count || !line_count)
        return -1;
    if (max_width > 0 && width > max_width) width = max_width;
    *span_count = 0;
    *line_count = 0;
    run_count = bb_ui_parse_runs(source, runs, sizeof(runs) / sizeof(runs[0]), error_offset);
    if (run_count < 0) return -1;
    if (build_pieces(runs, run_count, pieces, BB_UI_WRAP_MAX_PIECES, &piece_count) != 0)
        return -1;
    memset(&w, 0, sizeof(w));
    w.measure = measure;
    w.context = context;
    w.width = width;
    w.line_height = line_height;
    w.spans = spans;
    w.span_cap = span_cap;
    w.span_count = span_count;
    i = 0;
    while (i < piece_count) {
        int j, unit_width = 0, space = 0;
        if (pieces[i].newline) {
            new_line(&w);
            i++;
            continue;
        }
        /* A unit is the run of pieces with no break opportunity between them. */
        for (j = i; j < piece_count && !pieces[j].newline && (j == i || pieces[j].glue); j++)
            unit_width += measure_text(&w, pieces[j].text, pieces[j].length, pieces[j].style);
        if (pieces[i].space_before && w.x > 0)
            space = measure_text(&w, L" ", 1, pieces[i].style);
        if (w.x > 0 && w.x + space + unit_width > width) {
            new_line(&w);
            space = 0;
        }
        w.x += space;
        if (unit_width > width) {
            for (; i < j; i++)
                if (!place_long_piece(&w, &pieces[i], i > 0 && pieces[i].glue)) return -1;
            continue;
        }
        for (; i < j; i++) {
            if (!add_span(&w, pieces[i].text, pieces[i].length, pieces[i].style,
                          measure_text(&w, pieces[i].text, pieces[i].length, pieces[i].style)))
                return -1;
        }
    }
    /* Every line, including blank ones from consecutive '\n', gets an entry so
     * callers can use line_count * line_height (or last.y + line_height). */
    total_lines = piece_count > 0 ? w.line + 1 : 0;
    if (total_lines > (int)line_cap) return -1;
    span = 0;
    for (line_number = 0; line_number < total_lines; line_number++) {
        bb_ui_line *line = &lines[line_number];
        int first = span;
        while (span < *span_count && spans[span].line == line_number) span++;
        line->first_span = first;
        line->span_count = span - first;
        line->x = 0;
        line->y = line_number * line_height;
        line->height = line_height;
        if (span > first) {
            const bb_ui_span *last = &spans[span - 1];
            line->text = spans[first].run.text;
            line->length = (size_t)(last->run.text + last->run.length - spans[first].run.text);
            line->width = last->x + last->advance;
        } else {
            line->text = L"";
            line->length = 0;
            line->width = 0;
        }
    }
    *line_count = total_lines;
    if (x_offset) *x_offset = 0;
    if (total_width) *total_width = w.max_seen;
    return 0;
}

int bb_ui_line_offset(const bb_ui_line *line, int width, int align)
{
    int free_space;
    if (!line || align == BB_UI_ALIGN_LEFT) return 0;
    free_space = width - line->width;
    if (free_space <= 0) return 0;
    return align == BB_UI_ALIGN_CENTER ? free_space / 2 : free_space;
}

int bb_ui_hit_element(const bb_ui_hit_rect *elements, size_t stride, size_t count,
                      int modal, int x, int y)
{
    const unsigned char *base = (const unsigned char *)elements;
    size_t i;
    for (i = count; i > 0; i--) {
        const unsigned char *row = base + (i - 1) * stride;
        bb_ui_hit_rect rect;
        memcpy(&rect, row, sizeof(rect));
        if (modal && rect.layer != BB_UI_LAYER_MODAL) continue;
        if (!modal && rect.layer == BB_UI_LAYER_MODAL) continue;
        if (!rect.disabled && x >= rect.left && x < rect.right &&
            y >= rect.top && y < rect.bottom) return (int)(i - 1);
    }
    return -1;
}

bb_ui_modal_geometry bb_ui_modal_layout(int client_width, int client_height,
                                        int desired_width, int desired_height,
                                        int margin, int content_height)
{
    bb_ui_modal_geometry result;
    int width = desired_width;
    int height = desired_height;
    int available_width = client_width - margin * 2;
    int available_height = client_height - margin * 2;
    if (available_width < 1) available_width = 1;
    if (available_height < 1) available_height = 1;
    if (width > available_width) width = available_width;
    if (height > available_height) height = available_height;
    result.left = (client_width - width) / 2;
    result.top = (client_height - height) / 2;
    result.right = result.left + width;
    result.bottom = result.top + height;
    result.content_height = content_height;
    result.max_scroll = bb_ui_clamp_scroll(INT_MAX, content_height, height);
    return result;
}

