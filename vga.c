/* vga.c - VGA text mode terminal driver */
#include "vga.h"
#include "io.h"
#include <stddef.h>

#define VGA_WIDTH   80
#define VGA_HEIGHT  25
#define TAB_WIDTH   8

static volatile uint16_t *const vga_buffer = (volatile uint16_t *)0xB8000;

static size_t  cursor_row;
static size_t  cursor_col;
static uint8_t current_color;

/* One screen cell = character byte (low) + attribute byte (high) */
static inline uint16_t vga_entry(char c, uint8_t color)
{
    return (uint16_t)(uint8_t)c | ((uint16_t)color << 8);
}

/* Move the blinking hardware cursor to the current position */
static void update_hw_cursor(void)
{
    uint16_t pos = (uint16_t)(cursor_row * VGA_WIDTH + cursor_col);
    outb(0x3D4, 0x0F);
    outb(0x3D5, (uint8_t)(pos & 0xFF));
    outb(0x3D4, 0x0E);
    outb(0x3D5, (uint8_t)((pos >> 8) & 0xFF));
}

/* Scroll the screen up by one line and blank the last row */
static void scroll(void)
{
    for (size_t y = 1; y < VGA_HEIGHT; y++) {
        for (size_t x = 0; x < VGA_WIDTH; x++) {
            vga_buffer[(y - 1) * VGA_WIDTH + x] = vga_buffer[y * VGA_WIDTH + x];
        }
    }
    for (size_t x = 0; x < VGA_WIDTH; x++) {
        vga_buffer[(VGA_HEIGHT - 1) * VGA_WIDTH + x] = vga_entry(' ', current_color);
    }
    cursor_row = VGA_HEIGHT - 1;
}

/* Go to the start of the next line, scrolling when we pass row 25 */
static void new_line(void)
{
    cursor_col = 0;
    if (++cursor_row >= VGA_HEIGHT) {
        scroll();
    }
}

/* Write a visible character at the cursor and advance (with wrapping) */
static void put_visible(char c)
{
    vga_buffer[cursor_row * VGA_WIDTH + cursor_col] = vga_entry(c, current_color);
    if (++cursor_col >= VGA_WIDTH) {
        new_line();
    }
}

void vga_set_color(uint8_t fg, uint8_t bg)
{
    current_color = (uint8_t)((fg & 0x0F) | ((bg & 0x0F) << 4));
}

void vga_clear(void)
{
    for (size_t i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++) {
        vga_buffer[i] = vga_entry(' ', current_color);
    }
    cursor_row = 0;
    cursor_col = 0;
    update_hw_cursor();
}

void vga_init(void)
{
    vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    vga_clear();
}

void vga_putchar(char c)
{
    switch (c) {
    case '\n':
        new_line();
        break;
    case '\r':
        cursor_col = 0;
        break;
    case '\t': {
        size_t spaces = TAB_WIDTH - (cursor_col % TAB_WIDTH);
        while (spaces--) {
            put_visible(' ');
        }
        break;
    }
    case '\b':
        if (cursor_col > 0) {
            cursor_col--;
        } else if (cursor_row > 0) {
            cursor_row--;
            cursor_col = VGA_WIDTH - 1;
        } else {
            return;
        }
        vga_buffer[cursor_row * VGA_WIDTH + cursor_col] = vga_entry(' ', current_color);
        break;
    default:
        put_visible(c);
        break;
    }
    update_hw_cursor();
}

void vga_puts(const char *str)
{
    while (*str) {
        vga_putchar(*str++);
    }
}