/* css.h - a small CSS engine: selectors (tag, .class, #id, descendant ' ' and child '>' combinators)
 * and the properties that make sense in a text-cell browser: color, background, display/visibility,
 * font-weight, text-decoration. Everything else is ignored. */
#ifndef CSS_H
#define CSS_H

#include <stdint.h>
#include <stdbool.h>

#define CSS_MAX_RULES   1200
#define CSS_MAX_CHAIN   4
#define CSS_MAX_CLASSES 2
#define CSS_ELEM_CLASSES 6

enum { CSS_FG = 1, CSS_BG = 2, CSS_BOLD = 4, CSS_UNDERLINE = 8, CSS_HIDDEN = 16 };

typedef struct {
    uint8_t  set;                       /* which properties are present (CSS_*) */
    uint8_t  bold, underline, hidden;   /* their values: 1 = on, 0 = off */
    uint32_t fg, bg;                    /* 0xRRGGBB */
} css_decl_t;

typedef struct {
    uint32_t tag, id;                   /* name hashes, 0 = any */
    uint32_t cls[CSS_MAX_CLASSES];      /* class hashes, 0 = unused */
    uint8_t  child;                     /* 1: this compound must be a direct child of the previous one */
} css_compound_t;

typedef struct {
    css_compound_t chain[CSS_MAX_CHAIN];   /* chain[n-1] is the element itself */
    uint8_t  n;
    uint16_t spec, order;
    css_decl_t d;
} css_rule_t;

typedef struct {
    css_rule_t *rules;
    uint16_t   *sorted;                 /* rule indices ordered by (specificity, source order) */
    int         n;
    uint16_t    next_order;
} css_sheet_t;

/* What the matcher needs to know about an element */
typedef struct {
    uint32_t tag, id;
    uint32_t cls[CSS_ELEM_CLASSES];
    int      ncls;
} css_elem_t;

uint32_t css_hash(const char *s, int len);                 /* case-insensitive FNV-1a, never 0 */
bool     css_sheet_init(css_sheet_t *sh);
void     css_sheet_clear(css_sheet_t *sh);
void     css_parse(css_sheet_t *sh, const char *css, uint32_t len);   /* adds rules, keeps order sorted */
bool     css_parse_decls(const char *s, uint32_t len, css_decl_t *out);   /* style="..." contents */
bool     css_parse_color(const char *s, int len, uint32_t *rgb);
bool     css_match(const css_rule_t *r, const css_elem_t *stack, int depth);  /* stack[depth-1] = the element */

#endif /* CSS_H */
