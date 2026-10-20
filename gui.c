/* gui.c - NinoOS window manager: windows, taskbar, cursor, mouse and keyboard routing.
 * The apps (terminal, Capybara, viewer, Fastfetch) live in their own files. */
#include "gui.h"
#include "gfx.h"
#include "mouse.h"
#include "timer.h"
#include "rtc.h"
#include "util.h"
#include "string.h"
#include "term.h"
#include "capybara.h"
#include "viewer.h"
#include "fastfetch.h"
#include "paint.h"
#include "browser.h"
#include "editor.h"
#include "power.h"
#include "widgets.h"
#include "fs.h"
#include "persist.h"
#include <stdbool.h>
#include <stdint.h>

/* ---------- Layout constants ---------- */
#define FRAME           2
#define TITLE_H         24
#define CLOSE_SIZE      18
#define TASKBAR_H       32
#define TASK_BTN_H      24
#define DBLCLICK_TICKS  (400 * TIMER_HZ / 1000)   /* 400 ms, whatever the timer frequency is */

/* ---------- Colors (0x00RRGGBB) ---------- */
#define COL_FRAME       0xC0C0C0
#define COL_OUTLINE     0x000000
#define COL_TITLE_ON    0x1F4FA8
#define COL_TITLE_OFF   0x6B6B7B
#define COL_TITLE_TEXT  0xFFFFFF
#define COL_CLOSE       0xC83232
#define COL_ABOUT_BG    0xF4F4F4
#define COL_ABOUT_FG    0x202020
#define COL_TASKBAR     0xC0C0C0
#define COL_BTN_UP      0xD8D8D8
#define COL_BTN_DOWN    0x9A9A9A
#define COL_BTN_TEXT    0x000000

/* ---------- Windows ---------- */
typedef struct {
    int x, y, w, h;
    const char *title;
    bool visible;
    void (*draw)(int cx, int cy, int cw, int ch);   /* draws the client area */
    void (*click)(int rx, int ry, bool dbl);        /* click inside the client area (relative coords) */
    void (*key)(char c);                            /* keyboard input */
    void (*drag)(int rx, int ry);                   /* mouse moved with the left button held (after a click here) */
    void (*scroll)(int dir);                        /* mouse wheel, +1 = down */
    bool (*wants_key)(void);                        /* optional: only take keys while this returns true */
} window_t;

static window_t windows[WIN_COUNT];
static int      z_order[WIN_COUNT];     /* z_order[WIN_COUNT-1] is the front window */

static int screen_w, screen_h;
static int cursor_x, cursor_y;
static int drag_win = -1;
static int capture_win = -1;     /* window that receives drag events after a click inside it */
static int icon_selected = -1;
static int drag_dx, drag_dy;

static uint8_t clk_h, clk_m, clk_s;

/* Taskbar buttons */
typedef struct {
    int x, w;
    const char *label;
    int win;
} task_button_t;

static const task_button_t task_buttons[] = {
    {   8, 144, "Terminal",  WIN_TERMINAL  },
    { 160, 160, "Capybara",  WIN_CAPYBARA  },
    { 328,  80, "Draw",      WIN_DRAW      },
    { 416, 160, "Fastfetch", WIN_FASTFETCH },
    { 584,  96, "About",     WIN_ABOUT     },
    { 688, 128, "Browser",   WIN_BROWSER   },
    { 820,  58, "Off",       -1            }       /* -1 = shut down */
};
#define TASK_BUTTON_COUNT ((int)(sizeof(task_buttons) / sizeof(task_buttons[0])))

static const char *about_lines[] = {
    "NinoOS 0.7",
    "Powered by NinoKernel",
    "32-bit x86 bare metal",
    "Made with GCC + NASM"
};
#define ABOUT_LINE_COUNT ((int)(sizeof(about_lines) / sizeof(about_lines[0])))

/* ---------- Desktop icons (double-click opens the app) ---------- */
#define ICON_W   80
#define ICON_H   80

typedef struct {
    const char *label;
    int win;
} desktop_icon_t;

static const desktop_icon_t desktop_icons[] = {
    { "Terminal",  WIN_TERMINAL  },
    { "Capybara",  WIN_CAPYBARA  },
    { "Draw",      WIN_DRAW      },
    { "Fastfetch", WIN_FASTFETCH },
    { "Browser",   WIN_BROWSER   },
    { "Notepad",   WIN_EDITOR    }
};
#define ICON_COUNT ((int)(sizeof(desktop_icons) / sizeof(desktop_icons[0])))

/* Mouse cursor bitmap: X = black, O = white, space = transparent */
static const char *cursor_shape[16] = {
    "X           ",
    "XX          ",
    "XOX         ",
    "XOOX        ",
    "XOOOX       ",
    "XOOOOX      ",
    "XOOOOOX     ",
    "XOOOOOOX    ",
    "XOOOOOOOX   ",
    "XOOOOOXXXX  ",
    "XOOXOOX     ",
    "XOX XOOX    ",
    "XX  XOOX    ",
    "X    XOOX   ",
    "     XOOX   ",
    "      XX    "
};

/* ---------- Keyboard queue (IRQ -> main loop) ---------- */
#define KEY_QUEUE_SIZE 64
static volatile char    key_queue[KEY_QUEUE_SIZE];
static volatile uint8_t key_head;
static volatile uint8_t key_tail;

void gui_key_input(char c)
{
    uint8_t next = (uint8_t)((key_head + 1) % KEY_QUEUE_SIZE);
    if (next != key_tail) {                 /* drop the key if the queue is full */
        key_queue[key_head] = c;
        key_head = next;
    }
}

static int key_pop(void)
{
    if (key_head == key_tail) {
        return -1;
    }
    char c = key_queue[key_tail];
    key_tail = (uint8_t)((key_tail + 1) % KEY_QUEUE_SIZE);
    return (unsigned char)c;
}

/* ---------- Window helpers ---------- */
static int iabs(int v) { return v < 0 ? -v : v; }

static bool inside(int px, int py, int x, int y, int w, int h)
{
    return ui_inside(px, py, x, y, w, h);
}

static int close_x(const window_t *w) { return w->x + w->w - FRAME - 3 - CLOSE_SIZE; }
static int close_y(const window_t *w) { return w->y + FRAME + 3; }

static void raise_window(int idx)
{
    int pos = 0;
    while (z_order[pos] != idx) {
        pos++;
    }
    for (; pos < WIN_COUNT - 1; pos++) {
        z_order[pos] = z_order[pos + 1];
    }
    z_order[WIN_COUNT - 1] = idx;
}

static int active_window(void)
{
    for (int i = WIN_COUNT - 1; i >= 0; i--) {
        if (windows[z_order[i]].visible) {
            return z_order[i];
        }
    }
    return -1;
}

static int hit_test(int x, int y)
{
    for (int i = WIN_COUNT - 1; i >= 0; i--) {
        const window_t *w = &windows[z_order[i]];
        if (w->visible && inside(x, y, w->x, w->y, w->w, w->h)) {
            return z_order[i];
        }
    }
    return -1;
}

static void toggle_window(int idx)
{
    if (windows[idx].visible && active_window() == idx) {
        windows[idx].visible = false;
    } else {
        windows[idx].visible = true;
        raise_window(idx);
    }
}

void gui_open_window(int id)
{
    windows[id].visible = true;
    raise_window(id);
}

void gui_set_title(int id, const char *title)
{
    windows[id].title = title;
}

/* Keys go to the front window if it takes keyboard input, otherwise to the terminal */
static void route_key(char c)
{
    int a = active_window();
    if (a >= 0 && windows[a].key && (!windows[a].wants_key || windows[a].wants_key())) {
        windows[a].key(c);
    } else if (windows[WIN_TERMINAL].visible && windows[WIN_TERMINAL].key) {
        windows[WIN_TERMINAL].key(c);
    }
}

/* ---------- About window ---------- */
static void about_draw(int cx, int cy, int cw, int ch)
{
    gfx_fill_rect(cx, cy, cw, ch, COL_ABOUT_BG);
    for (int i = 0; i < ABOUT_LINE_COUNT; i++) {
        gfx_draw_text(cx + 12, cy + 12 + i * (CHAR_H + 4), about_lines[i], COL_ABOUT_FG, FONT_SCALE);
    }
}

/* ---------- Drawing ---------- */
static void draw_background(void)
{
    /* Vertical gradient */
    for (int y = 0; y < screen_h; y++) {
        uint32_t r = 0x14 + (0x30 - 0x14) * y / screen_h;
        uint32_t g = 0x2A + (0x70 - 0x2A) * y / screen_h;
        uint32_t b = 0x55 + (0xA0 - 0x55) * y / screen_h;
        gfx_fill_rect(0, y, screen_w, 1, (r << 16) | (g << 8) | b);
    }
}

static int icon_x(void) { return screen_w - ICON_W - 16; }
static int icon_y(int i) { return 16 + i * ICON_H; }

static void draw_icon_art(int win, int x, int y)       /* 48x48 picture */
{
    switch (win) {
    case WIN_TERMINAL:
        gfx_fill_rect(x + 2, y + 6, 44, 36, 0x101018);
        gfx_draw_rect(x + 2, y + 6, 44, 36, 0xC0C0C0);
        gfx_draw_text(x + 8, y + 16, ">_", 0xA0FFA0, 2);
        break;
    case WIN_CAPYBARA:
        gfx_fill_rect(x + 4, y + 18, 32, 20, 0xA0713A);          /* body */
        gfx_fill_rect(x + 30, y + 10, 14, 14, 0xB98046);         /* head */
        gfx_fill_rect(x + 40, y + 16, 5, 5, 0x6B4423);           /* nose */
        gfx_fill_rect(x + 34, y + 13, 3, 3, 0x000000);           /* eye */
        gfx_fill_rect(x + 31, y + 8, 4, 3, 0x7A5028);            /* ear */
        gfx_fill_rect(x + 7, y + 38, 6, 6, 0x7A5028);
        gfx_fill_rect(x + 26, y + 38, 6, 6, 0x7A5028);
        break;
    case WIN_DRAW:
        gfx_fill_rect(x + 4, y + 4, 40, 40, 0xFFFFFF);
        gfx_draw_rect(x + 4, y + 4, 40, 40, 0x606060);
        gfx_fill_rect(x + 10, y + 12, 14, 6, 0xE02020);
        gfx_fill_rect(x + 18, y + 22, 18, 6, 0x2050D0);
        gfx_fill_rect(x + 10, y + 32, 12, 6, 0x30B030);
        break;
    case WIN_EDITOR:
        gfx_fill_rect(x + 8, y + 2, 32, 44, 0xFFFFFF);
        gfx_draw_rect(x + 8, y + 2, 32, 44, 0x606060);
        for (int i = 0; i < 6; i++) gfx_fill_rect(x + 13, y + 9 + i * 6, 22, 2, 0x8090B0);
        gfx_fill_rect(x + 30, y + 30, 6, 14, 0xE0A020);       /* pencil */
        gfx_fill_rect(x + 30, y + 44, 6, 3, 0x404040);
        break;
    case WIN_BROWSER:
        gfx_fill_rect(x + 4, y + 4, 40, 40, 0x2A6FDB);
        gfx_draw_rect(x + 4, y + 4, 40, 40, 0xFFFFFF);
        gfx_fill_rect(x + 23, y + 4, 2, 40, 0xFFFFFF);        /* meridian */
        gfx_fill_rect(x + 4, y + 23, 40, 2, 0xFFFFFF);        /* equator */
        gfx_fill_rect(x + 12, y + 10, 24, 2, 0xFFFFFF);
        gfx_fill_rect(x + 12, y + 36, 24, 2, 0xFFFFFF);
        break;
    default:                                                      /* Fastfetch */
        gfx_fill_rect(x + 4, y + 6, 40, 36, 0x101830);
        gfx_draw_rect(x + 4, y + 6, 40, 36, 0x58C8F0);
        gfx_draw_char(x + 12, y + 12, 'N', 0x58C8F0, 3);
        break;
    }
}

static void draw_icons(void)
{
    for (int i = 0; i < ICON_COUNT; i++) {
        int x = icon_x();
        int y = icon_y(i);
        if (i == icon_selected) {
            gfx_fill_rect(x, y, ICON_W, ICON_H - 4, 0x3A6EA5);
        }
        draw_icon_art(desktop_icons[i].win, x + (ICON_W - 48) / 2, y + 4);
        int tw = (int)strlen(desktop_icons[i].label) * 8;
        gfx_draw_text(x + (ICON_W - tw) / 2 + 1, y + 58, desktop_icons[i].label, 0x000000, 1);
        gfx_draw_text(x + (ICON_W - tw) / 2, y + 57, desktop_icons[i].label, 0xFFFFFF, 1);
    }
}

static void draw_window(const window_t *w, bool active)
{
    gfx_fill_rect(w->x, w->y, w->w, w->h, COL_FRAME);
    gfx_draw_rect(w->x, w->y, w->w, w->h, COL_OUTLINE);

    gfx_fill_rect(w->x + FRAME, w->y + FRAME, w->w - 2 * FRAME, TITLE_H,
                  active ? COL_TITLE_ON : COL_TITLE_OFF);
    gfx_draw_text(w->x + FRAME + 8, w->y + FRAME + (TITLE_H - CHAR_H) / 2,
                  w->title, COL_TITLE_TEXT, FONT_SCALE);

    gfx_fill_rect(close_x(w), close_y(w), CLOSE_SIZE, CLOSE_SIZE, COL_CLOSE);
    gfx_draw_char(close_x(w) + 1, close_y(w) + 1, 'X', COL_TITLE_TEXT, FONT_SCALE);

    if (w->draw) {
        w->draw(w->x + FRAME, w->y + FRAME + TITLE_H,
                w->w - 2 * FRAME, w->h - 2 * FRAME - TITLE_H);
    }
}

static void draw_taskbar(void)
{
    int top = screen_h - TASKBAR_H;
    gfx_fill_rect(0, top, screen_w, TASKBAR_H, COL_TASKBAR);
    gfx_fill_rect(0, top, screen_w, 1, 0xFFFFFF);

    for (int i = 0; i < TASK_BUTTON_COUNT; i++) {
        const task_button_t *b = &task_buttons[i];
        bool pressed = b->win >= 0 && windows[b->win].visible;
        int by = top + 4;
        gfx_fill_rect(b->x, by, b->w, TASK_BTN_H, pressed ? COL_BTN_DOWN : COL_BTN_UP);
        gfx_draw_rect(b->x, by, b->w, TASK_BTN_H, COL_OUTLINE);
        gfx_draw_text(b->x + 8, by + (TASK_BTN_H - CHAR_H) / 2, b->label, COL_BTN_TEXT, FONT_SCALE);
    }

    /* Clock (HH:MM:SS) on the right */
    char buf[12];
    char *p = put_2digits(buf, clk_h);
    *p++ = ':';
    p = put_2digits(p, clk_m);
    *p++ = ':';
    put_2digits(p, clk_s);
    gfx_draw_text(screen_w - (int)strlen(buf) * CHAR_W - 12, top + (TASKBAR_H - CHAR_H) / 2,
                  buf, COL_BTN_TEXT, FONT_SCALE);
}

static void draw_cursor(void)
{
    for (int row = 0; row < 16; row++) {
        for (int col = 0; col < 12; col++) {
            char c = cursor_shape[row][col];
            if (c == 'X') {
                gfx_fill_rect(cursor_x + col, cursor_y + row, 1, 1, 0x000000);
            } else if (c == 'O') {
                gfx_fill_rect(cursor_x + col, cursor_y + row, 1, 1, 0xFFFFFF);
            }
        }
    }
}

static void gui_draw(void)
{
    draw_background();
    draw_icons();

    int active = active_window();
    for (int i = 0; i < WIN_COUNT; i++) {
        int idx = z_order[i];
        if (windows[idx].visible) {
            draw_window(&windows[idx], idx == active);
        }
    }

    draw_taskbar();
    draw_cursor();
}

/* ---------- Mouse handling ---------- */
static void on_mouse_down(int x, int y, bool dbl)
{
    /* Taskbar buttons */
    if (y >= screen_h - TASKBAR_H) {
        for (int i = 0; i < TASK_BUTTON_COUNT; i++) {
            const task_button_t *b = &task_buttons[i];
            if (inside(x, y, b->x, screen_h - TASKBAR_H + 4, b->w, TASK_BTN_H)) {
                if (b->win < 0) {
                    power_off();                  /* saves files first */
                }
                toggle_window(b->win);
            }
        }
        return;
    }

    int idx = hit_test(x, y);
    if (idx < 0) {
        icon_selected = -1;
        for (int i = 0; i < ICON_COUNT; i++) {
            if (inside(x, y, icon_x(), icon_y(i), ICON_W, ICON_H)) {
                icon_selected = i;
                if (dbl) {
                    gui_open_window(desktop_icons[i].win);
                }
            }
        }
        return;
    }
    icon_selected = -1;
    raise_window(idx);

    window_t *w = &windows[idx];
    if (inside(x, y, close_x(w), close_y(w), CLOSE_SIZE, CLOSE_SIZE)) {
        w->visible = false;
        return;
    }
    if (y < w->y + FRAME + TITLE_H) {        /* title bar: start dragging */
        drag_win = idx;
        drag_dx = x - w->x;
        drag_dy = y - w->y;
        return;
    }

    /* Client area: forward the click to the app */
    if (w->click) {
        w->click(x - (w->x + FRAME), y - (w->y + FRAME + TITLE_H), dbl);
    }
    capture_win = idx;
}

static void on_mouse_drag(int x, int y)
{
    window_t *w = &windows[drag_win];
    int nx = x - drag_dx;
    int ny = y - drag_dy;

    /* Keep part of the title bar reachable */
    if (nx < -(w->w - 100)) nx = -(w->w - 100);
    if (nx > screen_w - 100) nx = screen_w - 100;
    if (ny < 0) ny = 0;
    if (ny > screen_h - TASKBAR_H - TITLE_H) ny = screen_h - TASKBAR_H - TITLE_H;

    w->x = nx;
    w->y = ny;
}

void gui_redraw(void)
{
    gui_draw();
    gfx_flip();
}

static void on_mouse_wheel(int x, int y, int wheel)
{
    int idx = hit_test(x, y);
    if (idx >= 0 && windows[idx].scroll) {
        windows[idx].scroll(wheel > 0 ? 1 : -1);
    }
}

/* ---------- Public API ---------- */
void gui_init(void)
{
    screen_w = gfx_width();
    screen_h = gfx_height();
    cursor_x = screen_w / 2;
    cursor_y = screen_h / 2;

    term_init();
    capybara_init();
    paint_init();
    browser_init();
    editor_init();
    rtc_get_time(&clk_h, &clk_m, &clk_s);

    windows[WIN_TERMINAL] = (window_t){
        16, 16,
        2 * FRAME + 2 * TERM_PAD + TERM_COLS * CHAR_W,
        2 * FRAME + TITLE_H + 2 * TERM_PAD + TERM_ROWS * CHAR_H,
        "Terminal", true, term_draw, 0, term_key, 0, 0, 0
    };
    windows[WIN_CAPYBARA]  = (window_t){ 360, 340, 540, 380, "Capybara",      true,  capybara_draw,  capybara_click, capybara_key,
                                         0, capybara_scroll, capybara_wants_keys };
    windows[WIN_VIEWER]    = (window_t){ 200, 120, 560, 330, "Viewer",         false, viewer_draw,    0, 0, 0, viewer_scroll, 0 };
    windows[WIN_FASTFETCH] = (window_t){  40, 320, 940, 400, "Fastfetch",      false, fastfetch_draw, 0, 0, 0, 0, 0 };
    windows[WIN_DRAW]      = (window_t){  60, 280, 2 * FRAME + PAINT_CANVAS_W,
                                         2 * FRAME + TITLE_H + PAINT_TOOLBAR_H + PAINT_CANVAS_H,
                                         "Draw", false, paint_draw, paint_click, 0, paint_drag, 0, 0 };
    windows[WIN_BROWSER]   = (window_t){  20,  60, 2 * FRAME + BROWSER_CLIENT_W,
                                         2 * FRAME + TITLE_H + BROWSER_CLIENT_H,
                                         "Browser", false, browser_draw, browser_click, browser_key,
                                         0, browser_scroll, 0 };
    windows[WIN_EDITOR]    = (window_t){ 100,  70, 2 * FRAME + EDITOR_CLIENT_W,
                                         2 * FRAME + TITLE_H + EDITOR_CLIENT_H,
                                         "Notepad", false, editor_draw, editor_click, editor_key,
                                         0, editor_scroll, 0 };
    windows[WIN_ABOUT]     = (window_t){ 560,  60, 360, 140, "About NinoOS",   false, about_draw,     0, 0, 0, 0, 0 };

    /* Back to front */
    z_order[0] = WIN_ABOUT;
    z_order[1] = WIN_VIEWER;
    z_order[2] = WIN_FASTFETCH;
    z_order[3] = WIN_DRAW;
    z_order[4] = WIN_BROWSER;
    z_order[5] = WIN_EDITOR;
    z_order[6] = WIN_CAPYBARA;
    z_order[7] = WIN_TERMINAL;
}

void gui_run(void)
{
    bool prev_left = false;
    uint32_t last_click_tick = (uint32_t)-1000;
    int last_click_x = 0;
    int last_click_y = 0;
    uint32_t last_sec = timer_ticks() / TIMER_HZ;

    gui_draw();
    gfx_flip();

    for (;;) {
        int x = cursor_x;
        int y = cursor_y;
        static int save_wait;
        uint8_t buttons = 0;
        int wheel = 0;

        /* Check for events with interrupts off, then sleep atomically if there are none
         * ("sti; hlt" cannot miss an interrupt that arrives in between).
         * The timer wakes us up every 10 ms, which is how the clock stays current. */
        __asm__ volatile ("cli");
        bool moved = mouse_poll(&x, &y, &buttons, &wheel);
        bool have_key = (key_head != key_tail);
        uint32_t sec = timer_ticks() / TIMER_HZ;
        bool second_passed = (sec != last_sec);
        if (!moved && !have_key && !second_passed) {
            __asm__ volatile ("sti; hlt");
            continue;
        }
        __asm__ volatile ("sti");

        if (second_passed) {
            last_sec = sec;
            rtc_get_time(&clk_h, &clk_m, &clk_s);
            if (fs_changed()) {
                save_wait = 3;              /* save 3 seconds after the last change */
            } else if (save_wait > 0 && --save_wait == 0) {
                persist_save();             /* no disk: does nothing */
            }
        }

        if (moved) {
            cursor_x = x;
            cursor_y = y;
            bool left = (buttons & 0x01) != 0;

            if (left && !prev_left) {
                uint32_t now = timer_ticks();
                bool dbl = (now - last_click_tick) < DBLCLICK_TICKS
                           && iabs(x - last_click_x) <= 4
                           && iabs(y - last_click_y) <= 4;
                last_click_tick = dbl ? (uint32_t)-1000 : now;   /* no triple-click */
                last_click_x = x;
                last_click_y = y;
                on_mouse_down(x, y, dbl);
            } else if (left && drag_win >= 0) {
                on_mouse_drag(x, y);
            } else if (left && capture_win >= 0 && windows[capture_win].drag) {
                const window_t *w = &windows[capture_win];
                w->drag(x - (w->x + FRAME), y - (w->y + FRAME + TITLE_H));
            }
            if (!left) {
                drag_win = -1;
                capture_win = -1;
            }
            if (wheel != 0) {
                on_mouse_wheel(x, y, wheel);
            }
            prev_left = left;
        }

        int key;
        while ((key = key_pop()) >= 0) {
            route_key((char)key);
        }

        gui_draw();
        gfx_flip();
    }
}