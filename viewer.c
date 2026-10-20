/* viewer.c - read-only text viewer with mouse-wheel scrolling */
#include "viewer.h"
#include "gui.h"
#include "gfx.h"
#include "util.h"
#include "widgets.h"

#define VIEW_BG  0xFAFAFA
#define VIEW_FG  0x202020

static fs_node_t *file;
static char title[48];
static int  scroll_row;                 /* first visible row */
static int  total_rows;                /* rows in the whole file (from the last draw) */

void viewer_open(fs_node_t *node)
{
    file = node;
    scroll_row = 0;
    char *p = put_str(title, "Viewer - ");
    put_str(p, node->name);
    gui_set_title(WIN_VIEWER, title);
    gui_open_window(WIN_VIEWER);
}

void viewer_draw(int cx, int cy, int cw, int ch)
{
    gfx_fill_rect(cx, cy, cw, ch, VIEW_BG);

    if (file && !fs_is_valid(file)) {        /* file was deleted */
        file = 0;
    }
    if (!file) {
        gfx_draw_text(cx + 8, cy + 8, "No file open", VIEW_FG, FONT_SCALE);
        return;
    }
    if (file->size == 0) {
        gfx_draw_text(cx + 8, cy + 8, "(empty file)", VIEW_FG, FONT_SCALE);
        return;
    }

    int cols = (cw - 16 - SCROLLBAR_W) / CHAR_W;
    int rows = (ch - 16) / CHAR_H;
    int col = 0;
    int row = 0;                           /* absolute row of the text layout */

    scroll_row = ui_clamp_scroll(scroll_row, total_rows, rows);

    for (uint32_t i = 0; i < file->size; i++) {
        char c = (char)file->data[i];
        if (c == '\n') {
            col = 0;
            row++;
            continue;
        }
        if (c == '\t') {
            c = ' ';
        }
        if (c < 32 || c > 126) {
            continue;
        }
        int vis = row - scroll_row;
        if (vis >= 0 && vis < rows) {
            gfx_draw_char(cx + 8 + col * CHAR_W, cy + 8 + vis * CHAR_H, c, VIEW_FG, FONT_SCALE);
        }
        if (++col >= cols) {
            col = 0;
            row++;
        }
    }
    total_rows = row + (col > 0 ? 1 : 0);
    ui_scrollbar(cx + cw - SCROLLBAR_W, cy, ch, total_rows, rows, scroll_row);
}

void viewer_scroll(int dir)
{
    scroll_row += dir * 3;
    if (scroll_row < 0) {
        scroll_row = 0;                    /* the upper limit is applied while drawing */
    }
}
