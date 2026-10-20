/* paint.c - Draw: a small paint program. The canvas is one byte per pixel (palette index). */
#include "paint.h"
#include "gui.h"
#include "gfx.h"
#include "fs.h"
#include "heap.h"
#include "string.h"
#include "widgets.h"

#define SWATCH      18
#define PALETTE_N   10
#define WHITE_IDX   1

static const uint32_t palette[PALETTE_N] = {
    0x000000, 0xFFFFFF, 0xE02020, 0xF08020, 0xF0D820,
    0x30B030, 0x20B0B0, 0x2050D0, 0x9030C0, 0x805020
};

enum { B_SIZE, B_ERASE, B_CLEAR, B_SAVE, B_LOAD, B_COUNT };
static const struct { int x, w; } btn[B_COUNT] = {
    { 212, 48 }, { 264, 48 }, { 316, 48 }, { 368, 56 }, { 428, 56 }
};

static uint8_t *canvas;
static int  color = 0;
static int  brush = 3;
static bool drawing;
static int  last_x, last_y;
static const char *msg;

#define SAVE_PATH "/home/nino/drawing.npt"

static void clear_canvas(void)
{
    if (canvas) {
        memset(canvas, WHITE_IDX, PAINT_CANVAS_W * PAINT_CANVAS_H);
    }
}

void paint_init(void)
{
    canvas = kmalloc(PAINT_CANVAS_W * PAINT_CANVAS_H);
    clear_canvas();
}

static void stamp(int x, int y)
{
    int r0 = -(brush / 2);
    for (int dy = 0; dy < brush; dy++) {
        int py = y + r0 + dy;
        if (py < 0 || py >= PAINT_CANVAS_H) {
            continue;
        }
        for (int dx = 0; dx < brush; dx++) {
            int px = x + r0 + dx;
            if (px >= 0 && px < PAINT_CANVAS_W) {
                canvas[py * PAINT_CANVAS_W + px] = (uint8_t)color;
            }
        }
    }
}

static void line(int x0, int y0, int x1, int y1)      /* Bresenham */
{
    int dx = x1 > x0 ? x1 - x0 : x0 - x1;
    int dy = y1 > y0 ? y0 - y1 : y1 - y0;             /* negative */
    int sx = x0 < x1 ? 1 : -1;
    int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        stamp(x0, y0);
        if (x0 == x1 && y0 == y1) {
            break;
        }
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

static void save_drawing(void)
{
    fs_node_t *dir = fs_resolve(fs_root(), "/home/nino");
    if (!dir || !canvas) {
        msg = "Save failed";
        return;
    }
    fs_node_t *f = fs_find_child(dir, "drawing.npt");
    if (!f) {
        f = fs_create(dir, "drawing.npt", FS_FILE);
    }
    uint32_t total = 8 + PAINT_CANVAS_W * PAINT_CANVAS_H;
    uint8_t *buf = kmalloc(total);
    if (!f || !buf) {
        kfree(buf);
        msg = "Save failed (memory)";
        return;
    }
    memcpy(buf, "NPT1", 4);
    buf[4] = PAINT_CANVAS_W & 0xFF; buf[5] = PAINT_CANVAS_W >> 8;
    buf[6] = PAINT_CANVAS_H & 0xFF; buf[7] = PAINT_CANVAS_H >> 8;
    memcpy(buf + 8, canvas, PAINT_CANVAS_W * PAINT_CANVAS_H);
    msg = fs_write(f, buf, total) ? "Saved drawing.npt" : "Save failed (memory)";
    kfree(buf);
}

static void load_drawing(void)
{
    fs_node_t *f = fs_resolve(fs_root(), SAVE_PATH);
    uint32_t total = 8 + PAINT_CANVAS_W * PAINT_CANVAS_H;
    if (!f || f->type != FS_FILE || f->size != total || memcmp(f->data, "NPT1", 4) != 0) {
        msg = "No saved drawing";
        return;
    }
    memcpy(canvas, f->data + 8, PAINT_CANVAS_W * PAINT_CANVAS_H);
    msg = "Loaded drawing.npt";
}

void paint_draw(int cx, int cy, int cw, int ch)
{
    (void)ch;
    gfx_fill_rect(cx, cy, cw, PAINT_TOOLBAR_H, 0xE4E4E4);

    for (int i = 0; i < PALETTE_N; i++) {
        int sx = cx + 4 + i * (SWATCH + 2);
        gfx_fill_rect(sx, cy + 7, SWATCH, SWATCH, palette[i]);
        gfx_draw_rect(sx, cy + 7, SWATCH, SWATCH, i == color ? 0xFF0000 : 0x606060);
        if (i == color) {
            gfx_draw_rect(sx - 1, cy + 6, SWATCH + 2, SWATCH + 2, 0xFF0000);
        }
    }

    char sz[8] = { 'S', ':', (char)('0' + brush), 0 };
    ui_button(cx + btn[B_SIZE].x,  cy + 4, btn[B_SIZE].w,  24, sz,     false);
    ui_button(cx + btn[B_ERASE].x, cy + 4, btn[B_ERASE].w, 24, "Era",  color == WHITE_IDX);
    ui_button(cx + btn[B_CLEAR].x, cy + 4, btn[B_CLEAR].w, 24, "Clr",  false);
    ui_button(cx + btn[B_SAVE].x,  cy + 4, btn[B_SAVE].w,  24, "Save", false);
    ui_button(cx + btn[B_LOAD].x,  cy + 4, btn[B_LOAD].w,  24, "Load", false);

    int oy = cy + PAINT_TOOLBAR_H;
    if (!canvas) {
        gfx_fill_rect(cx, oy, PAINT_CANVAS_W, PAINT_CANVAS_H, 0xFFFFFF);
        return;
    }
    for (int y = 0; y < PAINT_CANVAS_H; y++) {          /* merge runs of equal color */
        const uint8_t *row = canvas + y * PAINT_CANVAS_W;
        int x = 0;
        while (x < PAINT_CANVAS_W) {
            int start = x;
            uint8_t c = row[x];
            while (x < PAINT_CANVAS_W && row[x] == c) {
                x++;
            }
            gfx_fill_rect(cx + start, oy + y, x - start, 1, palette[c]);
        }
    }
    if (msg) {
        gfx_fill_rect(cx + 4, oy + PAINT_CANVAS_H - 14, (int)strlen(msg) * 8 + 8, 12, 0x202020);
        gfx_draw_text(cx + 8, oy + PAINT_CANVAS_H - 12, msg, 0xFFFFFF, 1);
    }
}

void paint_click(int rx, int ry, bool dbl)
{
    (void)dbl;
    msg = 0;
    drawing = false;

    if (ry < PAINT_TOOLBAR_H) {
        for (int i = 0; i < PALETTE_N; i++) {
            if (ui_inside(rx, ry, 4 + i * (SWATCH + 2), 7, SWATCH, SWATCH)) {
                color = i;
            }
        }
        for (int i = 0; i < B_COUNT; i++) {
            if (!ui_inside(rx, ry, btn[i].x, 4, btn[i].w, 24)) {
                continue;
            }
            switch (i) {
            case B_SIZE:  brush = (brush == 1) ? 3 : (brush == 3) ? 6 : 1; break;
            case B_ERASE: color = WHITE_IDX; break;
            case B_CLEAR: clear_canvas(); break;
            case B_SAVE:  save_drawing(); break;
            case B_LOAD:  load_drawing(); break;
            }
        }
        return;
    }
    if (!canvas) {
        return;
    }
    last_x = rx;
    last_y = ry - PAINT_TOOLBAR_H;
    drawing = true;
    stamp(last_x, last_y);
}

void paint_drag(int rx, int ry)
{
    if (!drawing || !canvas) {
        return;
    }
    int x = rx;
    int y = ry - PAINT_TOOLBAR_H;
    if (x < 0) x = 0;
    if (x >= PAINT_CANVAS_W) x = PAINT_CANVAS_W - 1;
    if (y < 0) y = 0;
    if (y >= PAINT_CANVAS_H) y = PAINT_CANVAS_H - 1;
    line(last_x, last_y, x, y);
    last_x = x;
    last_y = y;
}
