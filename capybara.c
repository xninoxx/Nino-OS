/* capybara.c - Capybara, the NinoOS file explorer.
 * Click = select, double-click = open folder / view file, wheel = scroll,
 * Ren = rename (type the new name, Enter to confirm). */
#include "capybara.h"
#include "gui.h"
#include "gfx.h"
#include "fs.h"
#include "viewer.h"
#include "util.h"
#include "string.h"
#include "widgets.h"
#include "editor.h"

#define TB_H      32       /* toolbar */
#define PATH_H    24       /* path bar */
#define STATUS_H  24       /* status bar */
#define ROW_H     20
#define NAME_MAX_SHOWN 22

typedef struct {
    int x, w;
    const char *label;
} button_t;

enum { BTN_UP, BTN_DIR, BTN_FILE, BTN_REN, BTN_DEL, BTN_EDIT, BTN_COUNT };

static const button_t buttons[BTN_COUNT] = {
    {   8, 48, "Up"    },
    {  64, 80, "+Dir"  },
    { 152, 96, "+File" },
    { 256, 64, "Ren"   },
    { 328, 64, "Del"   },
    { 396, 64, "Edit"  }
};

static fs_node_t  *cwd;
static fs_node_t  *selected;
static const char *status_msg;
static int         rows_shown = 13;
static int         top_row;                 /* scroll offset in rows */
static int         total_rows;
static bool        renaming;
static char        rename_buf[FS_NAME_MAX + 1];
static int         rename_len;

/* ---------- Helpers ---------- */
static void validate(void)
{
    if (!cwd || !fs_is_valid(cwd) || cwd->type != FS_DIR) {
        cwd = fs_root();
        selected = 0;
    }
    if (selected && (!fs_is_valid(selected) || selected->parent != cwd)) {
        selected = 0;
    }
    if (!selected) {
        renaming = false;
    }
}

static int child_count(void)
{
    int n = 0;
    for (fs_node_t *c = cwd->first_child; c; c = c->next_sibling) {
        n++;
    }
    return n;
}

/* Folders first, then files */
static fs_node_t *entry_at(int index)
{
    for (int pass = 0; pass < 2; pass++) {
        fs_type_t want = (pass == 0) ? FS_DIR : FS_FILE;
        for (fs_node_t *c = cwd->first_child; c; c = c->next_sibling) {
            if (c->type != want) {
                continue;
            }
            if (index == 0) {
                return c;
            }
            index--;
        }
    }
    return 0;
}

static void draw_folder_icon(int x, int y)
{
    gfx_fill_rect(x, y + 1, 7, 3, 0xE8B84A);
    gfx_fill_rect(x, y + 3, 16, 11, 0xE8B84A);
    gfx_draw_rect(x, y + 3, 16, 11, 0x8A6A1A);
}

static void draw_file_icon(int x, int y)
{
    gfx_fill_rect(x + 2, y, 12, 15, 0xFFFFFF);
    gfx_draw_rect(x + 2, y, 12, 15, 0x555555);
    gfx_fill_rect(x + 4, y + 4, 8, 1, 0x888888);
    gfx_fill_rect(x + 4, y + 7, 8, 1, 0x888888);
    gfx_fill_rect(x + 4, y + 10, 8, 1, 0x888888);
}

static void unique_name(char *out, const char *base, const char *ext)
{
    for (int n = 1; n < 100; n++) {
        char *p = put_str(out, base);
        if (n > 1) {
            p = put_uint(p, (uint32_t)n);
        }
        put_str(p, ext);
        if (!fs_find_child(cwd, out)) {
            return;
        }
    }
}

static void do_button(int which)
{
    char name[FS_NAME_MAX + 1];

    switch (which) {
    case BTN_UP:
        if (cwd->parent) {
            cwd = cwd->parent;
            selected = 0;
            top_row = 0;
        }
        break;
    case BTN_DIR:
        unique_name(name, "newfolder", "");
        selected = fs_create(cwd, name, FS_DIR);
        if (!selected) {
            status_msg = "Could not create folder";
        }
        break;
    case BTN_FILE:
        unique_name(name, "newfile", ".txt");
        selected = fs_create(cwd, name, FS_FILE);
        if (!selected) {
            status_msg = "Could not create file";
        }
        break;
    case BTN_REN:
        if (renaming) {
            renaming = false;              /* second click cancels */
        } else if (!selected) {
            status_msg = "Select something first";
        } else {
            renaming = true;
            rename_len = (int)strlen(selected->name);
            memcpy(rename_buf, selected->name, (size_t)rename_len + 1);
        }
        break;
    case BTN_EDIT:
        if (!selected || selected->type != FS_FILE) {
            status_msg = "Select a file first";
        } else {
            editor_open(selected);
        }
        break;
    case BTN_DEL:
        if (!selected) {
            status_msg = "Nothing selected";
        } else if (fs_remove(selected)) {
            selected = 0;
            status_msg = "Deleted";
        } else {
            status_msg = "Cannot delete (folder not empty)";
        }
        break;
    }
}

/* ---------- Public API ---------- */
void capybara_init(void)
{
    cwd = fs_resolve(fs_root(), "/home/nino");
    if (!cwd) {
        cwd = fs_root();
    }
    selected = 0;
}

void capybara_draw(int cx, int cy, int cw, int ch)
{
    validate();
    gfx_fill_rect(cx, cy, cw, ch, 0xFFFFFF);

    /* Toolbar */
    gfx_fill_rect(cx, cy, cw, TB_H, 0xE4E4E4);
    for (int i = 0; i < BTN_COUNT; i++) {
        ui_button(cx + buttons[i].x, cy + 4, buttons[i].w, 24, buttons[i].label,
                  i == BTN_REN && renaming);
    }

    /* Path bar (shows the tail of long paths) */
    char path[96];
    fs_path(cwd, path, sizeof(path));
    int max_chars = (cw - 16) / CHAR_W;
    int len = (int)strlen(path);
    const char *shown = (len > max_chars) ? path + (len - max_chars) : path;
    gfx_fill_rect(cx, cy + TB_H, cw, PATH_H, 0xF4F4F4);
    gfx_draw_text(cx + 8, cy + TB_H + 4, shown, 0x404040, FONT_SCALE);

    /* File list */
    int list_y = cy + TB_H + PATH_H;
    rows_shown = (ch - TB_H - PATH_H - STATUS_H) / ROW_H;
    if (rows_shown < 0) {
        rows_shown = 0;
    }
    int count = child_count();
    total_rows = count;
    top_row = ui_clamp_scroll(top_row, count, rows_shown);

    for (int i = 0; i < rows_shown && top_row + i < count; i++) {
        fs_node_t *n = entry_at(top_row + i);
        int y = list_y + i * ROW_H;
        bool sel = (n == selected);
        uint32_t fg = sel ? 0xFFFFFF : 0x202020;

        if (sel) {
            gfx_fill_rect(cx, y, cw, ROW_H, 0x3A6EA5);
        }
        if (n->type == FS_DIR) {
            draw_folder_icon(cx + 6, y + 2);
        } else {
            draw_file_icon(cx + 6, y + 2);
        }

        char name[NAME_MAX_SHOWN + 1];
        size_t nl = strlen(n->name);
        if (nl > NAME_MAX_SHOWN) {
            nl = NAME_MAX_SHOWN;
        }
        memcpy(name, n->name, nl);
        name[nl] = '\0';
        gfx_draw_text(cx + 30, y + 2, name, fg, FONT_SCALE);

        if (n->type == FS_FILE) {
            char sz[16];
            char *p = put_uint(sz, n->size);
            put_str(p, " B");
            gfx_draw_text(cx + cw - 8 - SCROLLBAR_W - (int)strlen(sz) * CHAR_W, y + 2, sz, fg, FONT_SCALE);
        }
    }
    ui_scrollbar(cx + cw - SCROLLBAR_W, list_y, rows_shown * ROW_H, count, rows_shown, top_row);

    /* Status bar */
    int sy = cy + ch - STATUS_H;
    gfx_fill_rect(cx, sy, cw, STATUS_H, 0xE4E4E4);
    char status[64 + FS_NAME_MAX];
    if (renaming) {
        char *p = put_str(status, "Rename: ");
        p = put_str(p, rename_buf);
        put_str(p, "_");
    } else if (status_msg) {
        put_str(status, status_msg);
    } else if (selected) {
        char *p = put_str(status, selected->name);
        if (selected->type == FS_DIR) {
            put_str(p, "  (folder)");
        }
    } else {
        char *p = put_uint(status, (uint32_t)count);
        put_str(p, " items");
    }
    gfx_draw_text(cx + 8, sy + 4, status, 0x202020, FONT_SCALE);
}

void capybara_click(int rx, int ry, bool dbl)
{
    validate();
    status_msg = 0;
    if (ry >= 4 && ry < 28) {
        /* a toolbar click keeps or starts rename mode itself */
    } else {
        renaming = false;
    }

    /* Toolbar buttons */
    if (ry >= 4 && ry < 28) {
        for (int i = 0; i < BTN_COUNT; i++) {
            if (rx >= buttons[i].x && rx < buttons[i].x + buttons[i].w) {
                if (i != BTN_REN) {
                    renaming = false;
                }
                do_button(i);
            }
        }
        return;
    }

    /* File list */
    int list_y = TB_H + PATH_H;
    if (ry < list_y) {
        return;
    }
    int idx = (ry - list_y) / ROW_H;
    if (idx >= rows_shown) {
        return;
    }
    fs_node_t *n = entry_at(top_row + idx);
    if (!n) {
        selected = 0;
        return;
    }
    selected = n;

    if (dbl) {
        if (n->type == FS_DIR) {
            cwd = n;
            selected = 0;
            top_row = 0;
        } else {
            viewer_open(n);
        }
    }
}

void capybara_scroll(int dir)
{
    top_row += dir * 2;
    if (top_row < 0) {
        top_row = 0;                       /* the upper limit is applied while drawing */
    }
}

bool capybara_wants_keys(void)
{
    return renaming;
}

void capybara_key(char c)
{
    if (!renaming) {
        return;
    }
    if (c == '\n') {
        renaming = false;
        if (selected && fs_is_valid(selected)) {
            status_msg = fs_rename(selected, rename_buf) ? "Renamed" : "Rename failed (name empty or taken)";
        }
    } else if (c == '\b') {
        if (rename_len > 0) {
            rename_buf[--rename_len] = '\0';
        }
    } else if (c >= 32 && c < 127 && c != '/' && rename_len < FS_NAME_MAX) {
        rename_buf[rename_len++] = c;
        rename_buf[rename_len] = '\0';
    }
}
