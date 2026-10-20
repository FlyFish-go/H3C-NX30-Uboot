#include "../bb_ui_text.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

static unsigned measure(void *context, const wchar_t *text, size_t length, bb_ui_style style)
{
    unsigned unit = *(const unsigned *)context;
    (void)text;
    (void)style;
    return (unsigned)(length * unit);
}

static void test_format(void)
{
    wchar_t out[256];
    bb_ui_format_result r = bb_ui_format(
        L"{m} {v} step={n} file={f} app={ver}", out, 256,
        L"NX30", L"R012", 5, L"a.bin", L"0.4.0.0");
    assert(r.ok);
    assert(wcscmp(out, L"NX30 R012 step=5 file=a.bin app=0.4.0.0") == 0);
    r = bb_ui_format(L"bad {x}", out, 256, L"", L"", 1, L"", L"");
    assert(!r.ok && r.error_offset == 4);
    r = bb_ui_format(L"\x01/\x02/\x03/\x04/\x05", out, 256,
                     L"NX30", L"R012", 5, L"a.bin", L"0.4.0.0");
    assert(r.ok);
    assert(wcscmp(out, L"NX30/R012/5/a.bin/0.4.0.0") == 0);
}

static void test_runs(void)
{
    bb_ui_run runs[8];
    int error = -1;
    int n = bb_ui_parse_runs(L"before <b>bold</b> and <u>under</u>", runs, 8, &error);
    assert(n == 4);
    assert(runs[0].style == BB_UI_STYLE_NORMAL);
    assert(runs[1].style == BB_UI_STYLE_STRONG);
    assert(runs[2].style == BB_UI_STYLE_NORMAL);
    assert(runs[3].style == BB_UI_STYLE_UNDERLINE);
    assert(bb_ui_parse_runs(L"<b>oops", runs, 8, &error) < 0);
    /* Generated tables use private control codes instead of angle brackets. */
    n = bb_ui_parse_runs(L"\x11" L"Bold\x10 normal \x12" L"under\x10", runs, 8, &error);
    assert(n == 3);
    assert(runs[0].style == BB_UI_STYLE_STRONG && runs[0].length == 4);
    assert(runs[1].style == BB_UI_STYLE_NORMAL && runs[1].length == 8);
    assert(runs[2].style == BB_UI_STYLE_UNDERLINE && runs[2].length == 5);
    assert(bb_ui_parse_runs(L"\x11" L"open", runs, 8, &error) < 0);
    assert(bb_ui_parse_runs(L"stray\x10", runs, 8, &error) < 0);
    assert(bb_ui_parse_runs(L"\x11" L"a\x12" L"b\x10\x10", runs, 8, &error) < 0);
}

static void test_wrap(void)
{
    bb_ui_span spans[96];
    bb_ui_line lines[16];
    int span_count = 0, line_count = 0;
    unsigned unit = 10;
    int rc = bb_ui_wrap(L"alpha beta gamma", 50, measure, &unit, 20, 600,
                        spans, 96, &span_count, lines, 16, &line_count, NULL, NULL, NULL);
    assert(rc == 0 && span_count == 3 && line_count >= 2);
    rc = bb_ui_wrap(L"中文字符测试", 20, measure, &unit, 20, 600,
                    spans, 96, &span_count, lines, 16, &line_count, NULL, NULL, NULL);
    assert(rc == 0 && span_count == 6 && line_count >= 3);
}

#define WRAP(text, width) \
    bb_ui_wrap((text), (width), measure, &unit, 20, 600, spans, 96, &span_count, \
               lines, 16, &line_count, NULL, &total, NULL)

/* Rebuilds the visible text: one space between spans on a line, '|' between lines. */
static void rebuild(const bb_ui_span *spans, int span_count, wchar_t *out, size_t cap)
{
    size_t used = 0;
    int i;
    out[0] = 0;
    for (i = 0; i < span_count; i++) {
        if (i > 0) {
            int gap = spans[i].line != spans[i - 1].line ? '|' :
                      spans[i].x > spans[i - 1].x + spans[i - 1].advance ? ' ' : 0;
            if (gap && used + 1 < cap) out[used++] = (wchar_t)gap;
        }
        if (used + spans[i].run.length < cap) {
            wmemcpy(out + used, spans[i].run.text, spans[i].run.length);
            used += spans[i].run.length;
        }
    }
    out[used] = 0;
}

static void assert_fits(const bb_ui_line *lines, int line_count, int width)
{
    int i;
    for (i = 0; i < line_count; i++) assert(lines[i].width <= width);
}

static void test_wrap_spaces(void)
{
    bb_ui_span spans[96];
    bb_ui_line lines[16];
    wchar_t text[256];
    int span_count = 0, line_count = 0, total = 0;
    unsigned unit = 10;
    /* R2-1: spaces are measured, so English words are not glued together. */
    assert(WRAP(L"Open the router's admin page", 1000) == 0);
    assert(line_count == 1 && span_count == 5);
    assert(spans[0].x == 0 && spans[1].x == 50 && spans[2].x == 90 && spans[3].x == 180 && spans[4].x == 240);
    assert(lines[0].width == 280 && total == 280);
    rebuild(spans, span_count, text, 256);
    assert(wcscmp(text, L"Open the router's admin page") == 0);
    /* Runs of whitespace collapse; leading/trailing whitespace is dropped. */
    assert(WRAP(L"  a \t  b  ", 1000) == 0);
    assert(span_count == 2 && spans[0].x == 0 && spans[1].x == 20 && lines[0].width == 30);
    /* The space is part of the fit check: "ab cd" = 50 fits 50, "ab cde" does not. */
    assert(WRAP(L"ab cd", 50) == 0 && line_count == 1);
    assert(WRAP(L"ab cde", 50) == 0 && line_count == 2);
    rebuild(spans, span_count, text, 256);
    assert(wcscmp(text, L"ab|cde") == 0);
    /* A style change inside one word is not a break opportunity. */
    assert(WRAP(L"ab <u>cd</u>.", 50) == 0);
    assert(span_count == 3 && spans[1].line == 1 && spans[2].line == 1);
    assert(spans[1].run.style == BB_UI_STYLE_UNDERLINE && spans[2].run.style == BB_UI_STYLE_NORMAL);
    assert(spans[2].x == spans[1].x + spans[1].advance);
    assert(WRAP(L"\x11" L"Upgrade first.\x10 Otherwise", 1000) == 0);
    rebuild(spans, span_count, text, 256);
    assert(wcscmp(text, L"Upgrade first. Otherwise") == 0);
    assert(spans[0].run.style == BB_UI_STYLE_STRONG && spans[2].run.style == BB_UI_STYLE_NORMAL);
}

static void test_wrap_long_and_blank(void)
{
    bb_ui_span spans[96];
    bb_ui_line lines[16];
    wchar_t text[256];
    int span_count = 0, line_count = 0, total = 0;
    unsigned unit = 10;
    /* R2-3: an over-long word is split instead of overflowing. */
    assert(WRAP(L"aaaaaaaaaaaaaaaaaaaa", 50) == 0);
    assert(line_count == 4);
    assert_fits(lines, line_count, 50);
    assert(WRAP(L"see path/to/some-file.bin now", 80) == 0);
    assert_fits(lines, line_count, 80);
    rebuild(spans, span_count, text, 256);
    assert(wcscmp(text, L"see|path/to/|some-|file.bin|now") == 0);
    /* Closing punctuation glued to an over-long word pulls one character with it. */
    assert(WRAP(L"aaaaa。", 50) == 0);
    rebuild(spans, span_count, text, 256);
    assert(wcscmp(text, L"aaaa|a。") == 0);
    assert_fits(lines, line_count, 50);
    /* Width smaller than one character still makes progress. */
    assert(WRAP(L"abc", 5) == 0 && line_count == 3 && span_count == 3);
    /* R2-4: blank lines count toward the height. */
    assert(WRAP(L"A\n\nB", 1000) == 0);
    assert(line_count == 3 && lines[1].span_count == 0 && lines[2].y == 40);
    assert(lines[line_count - 1].y + lines[line_count - 1].height == 60);
    assert(WRAP(L"", 1000) == 0 && line_count == 0);
}

static void test_wrap_cjk_and_align(void)
{
    bb_ui_span spans[96];
    bb_ui_line lines[16];
    wchar_t text[256];
    int span_count = 0, line_count = 0, total = 0, i;
    unsigned unit = 10;
    /* Closing punctuation never starts a line; its preceding character moves with it. */
    assert(WRAP(L"中文字符。", 40) == 0);
    rebuild(spans, span_count, text, 256);
    assert(wcscmp(text, L"中文字|符。") == 0);
    assert(WRAP(L"显示“暂不可安装”。好", 80) == 0);
    for (i = 0; i < span_count; i++) {
        if (i == 0 || spans[i].line != spans[i - 1].line) {
            wchar_t first = spans[i].run.text[0];
            assert(first != L'。' && first != L'”' && first != L'，');
        }
        if (i + 1 < span_count && spans[i + 1].line != spans[i].line)
            assert(spans[i].run.text[spans[i].run.length - 1] != L'“');
    }
    assert_fits(lines, line_count, 80);
    /* CJK next to Latin can break without a space. */
    assert(WRAP(L"版本R012。", 60) == 0);
    rebuild(spans, span_count, text, 256);
    assert(wcscmp(text, L"版本|R012。") == 0);
    /* R2-6: centred lines use the measured line width. */
    assert(WRAP(L"or browse", 200) == 0);
    assert(lines[0].width == 90);
    assert(bb_ui_line_offset(&lines[0], 200, BB_UI_ALIGN_CENTER) == 55);
    assert(bb_ui_line_offset(&lines[0], 200, BB_UI_ALIGN_RIGHT) == 110);
    assert(bb_ui_line_offset(&lines[0], 200, BB_UI_ALIGN_LEFT) == 0);
    assert(bb_ui_line_offset(&lines[0], 50, BB_UI_ALIGN_CENTER) == 0);
}

static void test_scroll(void)
{
    assert(bb_ui_clamp_scroll(-1, 900, 400) == 0);
    assert(bb_ui_clamp_scroll(200, 900, 400) == 200);
    assert(bb_ui_clamp_scroll(700, 900, 400) == 500);
    assert(bb_ui_clamp_scroll(100, 300, 400) == 0);
}

static void test_hit(void)
{
    bb_ui_hit_rect rows[] = {
        {0, 0, 100, 100, 0, BB_UI_LAYER_BASE},
        {20, 20, 80, 80, 0, BB_UI_LAYER_MODAL},
    };
    assert(bb_ui_hit_element(rows, sizeof(rows[0]), 2, 0, 50, 50) == 0);
    assert(bb_ui_hit_element(rows, sizeof(rows[0]), 2, 1, 50, 50) == 1);
    assert(bb_ui_hit_element(rows, sizeof(rows[0]), 2, 1, 5, 5) == -1);
}

static void test_modal_layout(void)
{
    bb_ui_modal_geometry small = bb_ui_modal_layout(400, 300, 520, 570, 16, 900);
    assert(small.left == 16 && small.top == 16);
    assert(small.right == 384 && small.bottom == 284);
    assert(small.max_scroll == 900 - (284 - 16));
    bb_ui_modal_geometry normal = bb_ui_modal_layout(1040, 700, 520, 570, 16, 500);
    assert(normal.right - normal.left == 520);
    assert(normal.bottom - normal.top == 570);
    assert(normal.max_scroll == 0);
}

int main(void)
{
    test_format();
    test_runs();
    test_wrap();
    test_wrap_spaces();
    test_wrap_long_and_blank();
    test_wrap_cjk_and_align();
    test_scroll();
    test_hit();
    test_modal_layout();
    puts("bb_ui_text host tests passed");
    return 0;
}
