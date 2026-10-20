/* keyboard.c - PS/2 keyboard driver, scancode set 1. Layouts: us, de, dvorak. */
#include "keyboard.h"
#include "idt.h"
#include "io.h"
#include "vga.h"
#include "string.h"
#include <stdbool.h>
#include <stddef.h>

#define KBD_DATA_PORT  0x60

#define SC_LCTRL       0x1D
#define SC_LSHIFT      0x2A
#define SC_RSHIFT      0x36
#define SC_CAPSLOCK    0x3A
#define SC_COUNT       0x58          /* includes 0x56, the extra key on ISO keyboards */

static bool shift_pressed;
static bool ctrl_pressed;
static bool altgr_pressed;
static bool caps_lock;
static bool extended_prefix;         /* previous byte was 0xE0 */
static void (*key_callback)(char);

/* Active layout tables (scancode -> character), rebuilt by keyboard_set_layout() */
static char tab_normal[SC_COUNT];
static char tab_shift[SC_COUNT];
static char tab_altgr[SC_COUNT];
static const char *layout_name = "us";

/* US QWERTY base layout */
static const char us_normal[58] = {
    0, 0, '1','2','3','4','5','6','7','8','9','0','-','=', '\b',        /* 0x00-0x0E */
    '\t', 'q','w','e','r','t','y','u','i','o','p','[',']', '\n',        /* 0x0F-0x1C */
    0, 'a','s','d','f','g','h','j','k','l',';','\'','`',                /* 0x1D-0x29 */
    0, '\\','z','x','c','v','b','n','m',',','.','/', 0,                 /* 0x2A-0x36 */
    '*', 0, ' '                                                         /* 0x37-0x39 */
};

static const char us_shifted[58] = {
    0, 0, '!','@','#','$','%','^','&','*','(',')','_','+', '\b',
    '\t', 'Q','W','E','R','T','Y','U','I','O','P','{','}', '\n',
    0, 'A','S','D','F','G','H','J','K','L',':','"','~',
    0, '|','Z','X','C','V','B','N','M','<','>','?', 0,
    '*', 0, ' '
};

typedef struct { uint8_t sc; char normal, shifted, altgr; } key_override_t;

static const key_override_t de_keys[] = {
    { 0x03, '2', '"', 0 }, { 0x04, '3', 0, 0 }, { 0x07, '6', '&', 0 }, { 0x08, '7', '/', '{' },
    { 0x09, '8', '(', '[' }, { 0x0A, '9', ')', ']' }, { 0x0B, '0', '=', '}' }, { 0x0C, 0, '?', '\\' },
    { 0x0D, '\'', '`', 0 }, { 0x10, 'q', 'Q', '@' }, { 0x15, 'z', 'Z', 0 }, { 0x1A, 0, 0, 0 },
    { 0x1B, '+', '*', '~' }, { 0x27, 0, 0, 0 }, { 0x28, 0, 0, 0 }, { 0x29, '^', 0, 0 },
    { 0x2B, '#', '\'', 0 }, { 0x2C, 'y', 'Y', 0 }, { 0x33, ',', ';', 0 }, { 0x34, '.', ':', 0 },
    { 0x35, '-', '_', 0 }, { 0x56, '<', '>', '|' }
};

static const key_override_t dvorak_keys[] = {
    { 0x0C, '[', '{', 0 }, { 0x0D, ']', '}', 0 },
    { 0x10, '\'', '"', 0 }, { 0x11, ',', '<', 0 }, { 0x12, '.', '>', 0 }, { 0x13, 'p', 'P', 0 },
    { 0x14, 'y', 'Y', 0 }, { 0x15, 'f', 'F', 0 }, { 0x16, 'g', 'G', 0 }, { 0x17, 'c', 'C', 0 },
    { 0x18, 'r', 'R', 0 }, { 0x19, 'l', 'L', 0 }, { 0x1A, '/', '?', 0 }, { 0x1B, '=', '+', 0 },
    { 0x1E, 'a', 'A', 0 }, { 0x1F, 'o', 'O', 0 }, { 0x20, 'e', 'E', 0 }, { 0x21, 'u', 'U', 0 },
    { 0x22, 'i', 'I', 0 }, { 0x23, 'd', 'D', 0 }, { 0x24, 'h', 'H', 0 }, { 0x25, 't', 'T', 0 },
    { 0x26, 'n', 'N', 0 }, { 0x27, 's', 'S', 0 }, { 0x28, '-', '_', 0 },
    { 0x2C, ';', ':', 0 }, { 0x2D, 'q', 'Q', 0 }, { 0x2E, 'j', 'J', 0 }, { 0x2F, 'k', 'K', 0 },
    { 0x30, 'x', 'X', 0 }, { 0x31, 'b', 'B', 0 }, { 0x32, 'm', 'M', 0 }, { 0x33, 'w', 'W', 0 },
    { 0x34, 'v', 'V', 0 }, { 0x35, 'z', 'Z', 0 }
};

static void load_base(void)
{
    memset(tab_normal, 0, sizeof(tab_normal));
    memset(tab_shift, 0, sizeof(tab_shift));
    memset(tab_altgr, 0, sizeof(tab_altgr));
    for (int i = 0; i < 58; i++) {
        tab_normal[i] = us_normal[i];
        tab_shift[i] = us_shifted[i];
    }
}

static void apply(const key_override_t *k, int n)
{
    for (int i = 0; i < n; i++) {
        tab_normal[k[i].sc] = k[i].normal;
        tab_shift[k[i].sc] = k[i].shifted;
        tab_altgr[k[i].sc] = k[i].altgr;
    }
}

static bool streq(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}

bool keyboard_set_layout(const char *name)
{
    if (streq(name, "us")) {
        load_base();
        layout_name = "us";
    } else if (streq(name, "de")) {
        load_base();
        apply(de_keys, (int)(sizeof(de_keys) / sizeof(de_keys[0])));
        layout_name = "de";
    } else if (streq(name, "dvorak")) {
        load_base();
        apply(dvorak_keys, (int)(sizeof(dvorak_keys) / sizeof(dvorak_keys[0])));
        layout_name = "dvorak";
    } else {
        return false;
    }
    return true;
}

const char *keyboard_layout(void)
{
    return layout_name;
}

void keyboard_set_callback(void (*cb)(char))
{
    key_callback = cb;
}

static void deliver(char c)
{
    if (key_callback) {
        key_callback(c);
    } else if (c >= 32 || c == '\n' || c == '\b' || c == '\t') {
        vga_putchar(c);
    }
}

static void keyboard_irq_handler(void)
{
    uint8_t sc = inb(KBD_DATA_PORT);

    if (sc == 0xE0) {
        extended_prefix = true;
        return;
    }

    if (extended_prefix) {                                   /* extended keys: arrows, AltGr, keypad Enter ... */
        extended_prefix = false;
        uint8_t key = sc & 0x7F;
        bool release = (sc & 0x80) != 0;
        if (key == 0x38) {                                   /* right Alt = AltGr */
            altgr_pressed = !release;
            return;
        }
        if (release) {
            return;
        }
        switch (key) {
        case 0x48: deliver(KEY_UP); break;
        case 0x50: deliver(KEY_DOWN); break;
        case 0x4B: deliver(KEY_LEFT); break;
        case 0x4D: deliver(KEY_RIGHT); break;
        case 0x47: deliver(KEY_HOME); break;
        case 0x4F: deliver(KEY_END); break;
        case 0x53: deliver(KEY_DELETE); break;
        case 0x49: deliver(KEY_PGUP); break;
        case 0x51: deliver(KEY_PGDN); break;
        case 0x1C: deliver('\n'); break;                     /* keypad Enter */
        case 0x35: deliver('/'); break;                      /* keypad / */
        default: break;
        }
        return;
    }

    /* Bit 7 set = key release */
    if (sc & 0x80) {
        uint8_t key = sc & 0x7F;
        if (key == SC_LSHIFT || key == SC_RSHIFT) shift_pressed = false;
        if (key == SC_LCTRL) ctrl_pressed = false;
        return;
    }

    if (sc == SC_LSHIFT || sc == SC_RSHIFT) { shift_pressed = true; return; }
    if (sc == SC_LCTRL) { ctrl_pressed = true; return; }
    if (sc == SC_CAPSLOCK) { caps_lock = !caps_lock; return; }
    if (sc >= SC_COUNT) {
        return;
    }

    char c;
    if (altgr_pressed && tab_altgr[sc]) {
        c = tab_altgr[sc];
    } else {
        c = shift_pressed ? tab_shift[sc] : tab_normal[sc];
        /* Caps Lock only affects letters, and inverts Shift for them */
        char base = tab_normal[sc];
        if (caps_lock && base >= 'a' && base <= 'z') {
            c = shift_pressed ? base : tab_shift[sc];
        }
    }

    if (ctrl_pressed) {                                      /* Ctrl+S = save; other Ctrl keys do nothing */
        if (tab_normal[sc] == 's') {
            deliver(KEY_CTRL_S);
        }
        return;
    }
    if (c != 0) {
        deliver(c);
    }
}

void keyboard_init(void)
{
    keyboard_set_layout("us");

    /* Flush any stale byte in the controller's output buffer */
    (void)inb(KBD_DATA_PORT);
    irq_register_handler(1, keyboard_irq_handler);
}
