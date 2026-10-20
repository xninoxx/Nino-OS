/* editor.c - Notepad. Arrow keys, Home/End, PgUp/PgDn, Delete, Backspace, mouse click to place the
 * cursor, wheel to scroll. Ctrl+S or the Save button saves. Long lines wrap at the window edge. */
#include "editor.h"
#include "gui.h"
#include "gfx.h"
#include "keyboard.h"
#include "heap.h"
#include "string.h"
#include "util.h"
#include "widgets.h"

#define ED_MAX      131072          /* bytes per file */
#define ED_MAXROWS  8192
#define ROW_H       16
#define LEFT_PAD    4
#define VISIBLE     20
#define COLS        ((EDITOR_CLIENT_W - SCROLLBAR_W - 2 * LEFT_PAD) / CHAR_W)

static char       *buf;
static uint32_t    len, cur;
static bool        dirty;
static bool        follow;          /* scroll so the cursor is visible on the next draw */
static fs_node_t  *node;
static char        name[FS_NAME_MAX + 1] = "untitled";
static char        title[64] = "Notepad";
static char        status[80];
static int         scroll_row;
static uint32_t    row_start[ED_MAXROWS + 1];
static int         nrows;

static void set_status(const char *s)
{
    int i = 0;
    while (s[i] && i < (int)sizeof(status) - 1) { status[i] = s[i]; i++; }
    status[i] = '\0';
}

static void update_title(void)
{
    char *p = put_str(title, "Notepad - ");
    p = put_str(p, name);
    if (dirty) put_str(p, " *");
    gui_set_title(WIN_EDITOR, title);
}

static void layout(void)
{
    nrows = 1;
    row_start[0] = 0;
    int col = 0;
    for (uint32_t i = 0; i < len && nrows < ED_MAXROWS; i++) {
        if (buf[i] == '\n') {
            row_start[nrows++] = i + 1;
            col = 0;
        } else if (++col >= COLS) {
            row_start[nrows++] = i + 1;
            col = 0;
        }
    }
}

static uint32_t row_len(int r)                       /* visible characters of row r */
{
    uint32_t end = (r + 1 < nrows) ? row_start[r + 1] : len;
    uint32_t n = end - row_start[r];
    if (r + 1 < nrows && n > 0 && buf[end - 1] == '\n') n--;
    return n;
}

static void cursor_rc(uint32_t pos, int *row, int *col)
{
    int r = 0;
    while (r + 1 < nrows && row_start[r + 1] <= pos) r++;
    *row = r;
    *col = (int)(pos - row_start[r]);
}

void editor_init(void)
{
    buf = kmalloc(ED_MAX + 1);
    len = cur = 0;
    update_title();
}

static void save(void)
{
    if (!buf) return;
    if (!node || !fs_is_valid(node)) {                        /* a new file: pick a free name in /home/nino */
        fs_node_t *dir = fs_resolve(fs_root(), "/home/nino");
        if (!dir) dir = fs_root();
        char n[FS_NAME_MAX + 1];
        for (int i = 1; i < 100 && !node; i++) {
            char *p = put_str(n, "untitled");
            if (i > 1) p = put_uint(p, (uint32_t)i);
            put_str(p, ".txt");
            if (!fs_find_child(dir, n)) {
                node = fs_create(dir, n, FS_FILE);
            }
        }
        if (!node) {
            set_status("Cannot create a file here");
            return;
        }
        put_str(name, node->name);
    }
    if (fs_write(node, len ? buf : "", len)) {
        dirty = false;
        set_status("Saved");
    } else {
        set_status("Save failed (out of memory?)");
    }
    update_title();
}

void editor_open(fs_node_t *file)
{
    gui_open_window(WIN_EDITOR);
    if (!buf) return;
    if (dirty) save();                                       /* never lose changes when switching files */

    len = cur = 0;
    scroll_row = 0;
    node = file;
    dirty = false;
    if (file && file->type == FS_FILE) {
        uint32_t n = file->size > ED_MAX ? ED_MAX : file->size;
        memcpy(buf, file->data, n);
        len = n;
        put_str(name, file->name);
        set_status(file->size > ED_MAX ? "File is larger than 128 KB: only the start was loaded" : "Opened");
    } else {
        node = 0;
        put_str(name, "untitled");
        set_status("New file");
    }
    follow = true;
    update_title();
}

static void insert(char c)
{
    if (len >= ED_MAX) {
        set_status("File is full (128 KB)");
        return;
    }
    memmove(buf + cur + 1, buf + cur, len - cur);
    buf[cur++] = c;
    len++;
    if (!dirty) { dirty = true; update_title(); }
}

static void erase(uint32_t pos)                              /* delete the character at pos */
{
    if (pos >= len) return;
    memmove(buf + pos, buf + pos + 1, len - pos - 1);
    len--;
    if (!dirty) { dirty = true; update_title(); }
}

void editor_key(char c)
{
    if (!buf) return;
    layout();
    int r, col;
    cursor_rc(cur, &r, &col);

    if (c == KEY_CTRL_S) {
        save();
        return;
    }
    switch (c) {
    case KEY_LEFT:   if (cur > 0) cur--; break;
    case KEY_RIGHT:  if (cur < len) cur++; break;
    case KEY_UP:     if (r > 0) { uint32_t l = row_len(r - 1); cur = row_start[r - 1] + ((uint32_t)col < l ? (uint32_t)col : l); } break;
    case KEY_DOWN:   if (r + 1 < nrows) { uint32_t l = row_len(r + 1); cur = row_start[r + 1] + ((uint32_t)col < l ? (uint32_t)col : l); } break;
    case KEY_HOME:   cur = row_start[r]; break;
    case KEY_END:    cur = row_start[r] + row_len(r); break;
    case KEY_PGUP:   { int nr = r - VISIBLE < 0 ? 0 : r - VISIBLE; uint32_t l = row_len(nr); cur = row_start[nr] + ((uint32_t)col < l ? (uint32_t)col : l); } break;
    case KEY_PGDN:   { int nr = r + VISIBLE >= nrows ? nrows - 1 : r + VISIBLE; uint32_t l = row_len(nr); cur = row_start[nr] + ((uint32_t)col < l ? (uint32_t)col : l); } break;
    case KEY_DELETE: erase(cur); break;
    case '\b':       if (cur > 0) { cur--; erase(cur); } break;
    case '\n':       insert('\n'); break;
    case '\t':       for (int i = 0; i < 4; i++) insert(' '); break;
    default:
        if (c >= 32 && c < 127) insert(c);
        break;
    }
    follow = true;
}

void editor_scroll(int dir)
{
    scroll_row += dir * 3;
    if (scroll_row < 0) scroll_row = 0;
}

void editor_click(int rx, int ry, bool dbl)
{
    (void)dbl;
    if (ry < 32) {
        if (ui_inside(rx, ry, 4, 4, 56, 24)) {              /* New */
            editor_open(0);
        } else if (ui_inside(rx, ry, 64, 4, 64, 24)) {      /* Save */
            save();
        }
        return;
    }
    if (!buf) return;
    layout();
    int col = (rx - LEFT_PAD) / CHAR_W;
    int row = (ry - 32) / ROW_H + scroll_row;
    if (row >= nrows) {
        cur = len;
    } else {
        uint32_t l = row_len(row);
        if (col < 0) col = 0;
        cur = row_start[row] + ((uint32_t)col < l ? (uint32_t)col : l);
    }
    follow = true;
}

void editor_draw(int cx, int cy, int cw, int ch)
{
    gfx_fill_rect(cx, cy, cw, 32, 0xE4E4E4);
    ui_button(cx + 4, cy + 4, 56, 24, "New", false);
    ui_button(cx + 64, cy + 4, 64, 24, "Save", false);
    gfx_draw_text(cx + 140, cy + 8, name, 0x303030, FONT_SCALE);
    if (dirty) gfx_draw_text(cx + 140 + (int)strlen(name) * CHAR_W + 8, cy + 8, "*", 0xC00000, FONT_SCALE);

    int py = cy + 32;
    int ph = ch - 32 - 18;
    int rows = ph / ROW_H;
    gfx_fill_rect(cx, py, cw, ph, 0xFFFFFF);
    if (!buf) {
        gfx_draw_text(cx + 8, py + 8, "Out of memory", 0xC00000, FONT_SCALE);
        return;
    }

    layout();
    if (cur > len) cur = len;
    int crow, ccol;
    cursor_rc(cur, &crow, &ccol);
    if (follow) {
        if (crow < scroll_row) scroll_row = crow;
        if (crow >= scroll_row + rows) scroll_row = crow - rows + 1;
        follow = false;
    }
    scroll_row = ui_clamp_scroll(scroll_row, nrows, rows);

    for (int r = 0; r < rows && scroll_row + r < nrows; r++) {
        int row = scroll_row + r;
        uint32_t n = row_len(row);
        int y = py + r * ROW_H;
        for (uint32_t i = 0; i < n; i++) {
            char c = buf[row_start[row] + i];
            if (c >= 32 && c < 127) gfx_draw_char(cx + LEFT_PAD + (int)i * CHAR_W, y, c, 0x202020, FONT_SCALE);
        }
    }
    if (crow >= scroll_row && crow < scroll_row + rows) {      /* the cursor: a vertical bar */
        gfx_fill_rect(cx + LEFT_PAD + ccol * CHAR_W, py + (crow - scroll_row) * ROW_H, 2, ROW_H, 0x1030D0);
    }
    ui_scrollbar(cx + cw - SCROLLBAR_W, py, ph, nrows, rows, scroll_row);

    gfx_fill_rect(cx, cy + ch - 18, cw, 18, 0xD0D0D0);
    char info[80];
    char *p = put_str(info, "Line ");
    p = put_uint(p, (uint32_t)crow + 1);
    p = put_str(p, ", col ");
    p = put_uint(p, (uint32_t)ccol + 1);
    p = put_str(p, "   ");
    p = put_uint(p, len);
    p = put_str(p, " bytes   Ctrl+S saves");
    gfx_draw_text(cx + 6, cy + ch - 13, status[0] ? status : info, 0x202020, 1);
    if (status[0]) gfx_draw_text(cx + cw - (int)strlen(info) * 8 - 10, cy + ch - 13, info, 0x606060, 1);
}
