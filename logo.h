/* logo.h - the NinoOS logo for the Fastfetch window: a capybara holding the letter N.
 * One character per cell, drawn at 16x16 pixels (font scale 2). Cell codes:
 *   '@' outline   '#' fur   '+' light fur   'B' N (blue)   'b' N (shadow)
 *   'o' white (eyes)   'p' pink (ears)   '.' dark gap   ' ' transparent */
#ifndef LOGO_H
#define LOGO_H

#define LOGO_LINES 20
#define LOGO_WIDTH 29    /* characters per line */

static const char *const nino_logo[LOGO_LINES] = {
    "    @@@@@           @@@@@    ",
    "    @ppp@           @ppp@    ",
    "    @@@@@@@@@@@@@@@@@@@@@    ",
    "   @@####################@   ",
    "   @@####################@   ",
    "   @@###oo.#######oo.####@   ",
    "   @@####..########..####@   ",
    "   @@###@@@@@@@@@@@@@@###@   ",
    "   @@###@+...+++...+@@###@   ",
    "   @@###@+++++++++++@@###@   ",
    "   @@###@+++@@@@@+++@@###@   ",
    "    @@@@@@@@@ooo@@@@@@@@@    ",
    "   @@@@BBBBBBBbb@@@BBBbb@@   ",
    "  @@###BBBBBBBbb###BBBbb##@  ",
    "  @@@@@@@@bbBBBbb##B@@@@@@@  ",
    "  @@@+++@@bbBBBbb##B@+++@@@  ",
    "  @@@+++@@bbbBBBbb#B@+++@@@  ",
    "  @@@@@@@@bb#BBBbb#B@@@@@@@  ",
    "  @@###BBBbbbbBBBbbBBBbb##@  ",
    "   @@@@BBBbb@bbBBBBBBBbb@@   "
};

/* Glyph and color for a cell code (glyph 0 = draw nothing) */
static inline char logo_glyph(char code)
{
    switch (code) {
    case '@': return '@';
    case '#': return '#';
    case '+': return '+';
    case 'B': return '#';
    case 'b': return '/';
    case 'o': return 'o';
    case 'p': return 'o';
    case '.': return '.';
    default:  return 0;
    }
}

static inline unsigned int logo_color(char code)
{
    switch (code) {
    case '@': return 0x784C2C;
    case '#': return 0xC8925A;
    case '+': return 0xECCC96;
    case 'B': return 0x4AAAF5;
    case 'b': return 0x1E50AA;
    case 'o': return 0xFFFFFF;
    case 'p': return 0xF496A4;
    default:  return 0x0A0A0A;
    }
}

#endif /* LOGO_H */
