/* html.h - turns HTML (or plain text) into a grid of colored characters with links and form fields */
#ifndef HTML_H
#define HTML_H

#include <stdint.h>
#include <stdbool.h>

#define HTML_COLS         56
#define HTML_MAX_ROWS     2000
#define HTML_MAX_LINKS    250
#define HTML_URL_MAX      192
#define HTML_MAX_FIELDS   64
#define HTML_MAX_FORMS    8
#define HTML_FIELD_VALUE  256
#define HTML_MAX_CSS      4

enum { HF_BOLD = 1, HF_UNDERLINE = 2, HF_FG = 4, HF_BG = 8 };

typedef struct {
    char     ch;                /* 0 = empty cell */
    uint8_t  link;              /* 0 = no link, else 1-based index into links[] */
    uint8_t  field;             /* 0 = none, else 1-based index into fields[] */
    uint8_t  flags;             /* HF_* */
    uint16_t fg, bg;            /* RGB565, only valid with HF_FG / HF_BG */
} hcell_t;

enum { FT_TEXT, FT_PASSWORD, FT_HIDDEN, FT_SUBMIT, FT_CHECKBOX, FT_RADIO, FT_TEXTAREA, FT_SELECT };

typedef struct {
    uint8_t  type;
    int8_t   form;              /* index into forms[], -1 = not inside a form */
    uint8_t  checked;
    uint8_t  placed;            /* has cells in the grid */
    uint8_t  col, width;
    uint16_t row;
    char     name[32];
    char     value[HTML_FIELD_VALUE];
    char     label[32];         /* submit button text / placeholder */
} field_t;

typedef struct {
    char    action[HTML_URL_MAX];
    uint8_t post;
} form_t;

typedef struct {
    hcell_t *cells;                             /* HTML_COLS * HTML_MAX_ROWS */
    int      rows;
    int      nlinks;
    char     links[HTML_MAX_LINKS][HTML_URL_MAX];
    int      nfields, nforms;
    field_t  fields[HTML_MAX_FIELDS];
    form_t   forms[HTML_MAX_FORMS];
    int      focus_field;                       /* 1-based, 0 = none */
    char     title[64];
    char     refresh[HTML_URL_MAX];             /* <meta http-equiv=refresh> target, "" = none */
    int      ncss;
    char     css_links[HTML_MAX_CSS][HTML_URL_MAX];   /* external style sheets to fetch */
    bool     has_page_bg;
    uint32_t page_bg;                           /* 0xRRGGBB */
} page_t;

bool html_page_init(page_t *pg);                /* allocates the cell grid and the CSS rule table */

/* extra_css: style sheets fetched by the caller (may be 0). Applied before <style> blocks of the page. */
void html_render(page_t *pg, const char *base_url, const uint8_t *data, uint32_t len, bool is_html,
                 const char *extra_css, uint32_t extra_css_len);

/* Form interaction (the browser calls these) */
void html_set_focus(page_t *pg, int field_id);                 /* 0 removes focus */
void html_field_changed(page_t *pg, int field_id);             /* redraw the field after editing its value */
void html_toggle(page_t *pg, int field_id);                    /* checkbox / radio */
/* "a=1&b=2" for form `form`; submit_id = 1-based index of the clicked button (0 = none). Returns length. */
int  html_build_query(const page_t *pg, int form, int submit_id, char *out, int size);

#endif /* HTML_H */
