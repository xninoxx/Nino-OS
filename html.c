/* html.c - a forgiving HTML-to-text layout engine with a CSS subset and forms.
 * Output is a grid of characters (HTML_COLS wide). Supported: headings, paragraphs, lists, preformatted
 * text, links, colors/backgrounds/bold/hidden from CSS, forms (text, password, checkbox, radio, hidden,
 * submit, textarea, select), meta refresh, external style sheet discovery. No images, no scripts. */
#include "html.h"
#include "css.h"
#include "url.h"
#include "heap.h"
#include "string.h"

#define MAX_DEPTH 64

typedef struct {
    uint32_t fg, bg;
    uint8_t  flags;             /* HF_* */
    uint8_t  hidden;
    uint8_t  link;
    uint8_t  pre;
} est_t;

typedef struct {
    page_t *pg;
    int     col, row;
    hcell_t wc[80];             /* the word being built */
    int     wlen;
    bool    glue;
    bool    gray;               /* draw the next characters in gray (image alt text) */

    css_elem_t el[MAX_DEPTH];
    est_t      es[MAX_DEPTH];
    uint32_t   tagh[MAX_DEPTH];
    int        depth;

    int cur_form;
    int btn_field;              /* <button> being collected, -1 = none */
    int sel_field;              /* <select> being collected, -1 = none */
    bool sel_has_value;
} lay_t;

static css_sheet_t sheet;
static bool        sheet_ok;

static char lower(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c; }
static bool is_alpha(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
static bool is_digit(char c) { return c >= '0' && c <= '9'; }

static uint16_t rgb565(uint32_t rgb)
{
    return (uint16_t)(((rgb >> 8) & 0xF800) | ((rgb >> 5) & 0x07E0) | ((rgb >> 3) & 0x001F));
}

static void copy_str(char *dst, int size, const char *src)
{
    int i = 0;
    while (src[i] && i < size - 1) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

/* ---------- Grid output ---------- */
static est_t root_style;

static const est_t *cur_style(const lay_t *L)
{
    return L->depth > 0 ? &L->es[L->depth - 1] : &root_style;
}

static void put_cell(lay_t *L, hcell_t c)
{
    if (L->row >= HTML_MAX_ROWS || L->col >= HTML_COLS) {
        return;
    }
    L->pg->cells[L->row * HTML_COLS + L->col] = c;
    L->col++;
}

static void put_plain(lay_t *L, char ch)             /* a space or filler with the current background */
{
    const est_t *s = cur_style(L);
    hcell_t c = { ch, 0, 0, (uint8_t)(s->flags & HF_BG), 0, rgb565(s->bg) };
    put_cell(L, c);
}

static void newline(lay_t *L)
{
    if (L->row < HTML_MAX_ROWS) {
        L->row++;
    }
    L->col = 0;
}

static void flush_word(lay_t *L)
{
    if (!L->wlen) {
        return;
    }
    int need = L->wlen + (L->col > 0 ? 1 : 0);
    if (L->glue) {
        L->glue = false;
    } else if (L->col > 0 && L->col + need > HTML_COLS) {
        newline(L);
    } else if (L->col > 0) {
        put_plain(L, ' ');
    }
    for (int i = 0; i < L->wlen; i++) {
        if (L->col >= HTML_COLS) {
            newline(L);
        }
        put_cell(L, L->wc[i]);
    }
    L->wlen = 0;
}

static void add_char(lay_t *L, char ch)
{
    if (L->wlen >= (int)(sizeof(L->wc) / sizeof(L->wc[0]))) {
        flush_word(L);
        L->glue = true;
    }
    const est_t *s = cur_style(L);
    hcell_t c;
    c.ch = ch;
    c.link = s->link;
    c.field = 0;
    c.flags = (uint8_t)(s->flags & (HF_BOLD | HF_FG | HF_BG));
    c.fg = rgb565(s->fg);
    c.bg = rgb565(s->bg);
    if (s->flags & HF_UNDERLINE) {
        c.flags |= HF_UNDERLINE;
    }
    if (L->gray) {
        c.flags |= HF_FG;
        c.fg = rgb565(0x808080);
    }
    L->wc[L->wlen++] = c;
}

static bool row_blank(const lay_t *L, int row)
{
    for (int i = 0; i < HTML_COLS; i++) {
        if (L->pg->cells[row * HTML_COLS + i].ch) {
            return false;
        }
    }
    return true;
}

static void block_break(lay_t *L, bool blank)
{
    flush_word(L);
    if (L->col > 0) {
        newline(L);
    }
    if (blank && L->row > 0 && !row_blank(L, L->row - 1)) {
        newline(L);
    }
}

/* ---------- Forms ---------- */
static void field_text(const page_t *pg, const field_t *f, char *out, uint8_t *gray_from, uint8_t *gray_to)
{
    int w = f->width;
    *gray_from = *gray_to = 0;
    for (int i = 0; i < w; i++) out[i] = ' ';
    out[w] = '\0';

    switch (f->type) {
    case FT_CHECKBOX:
        out[0] = '['; out[1] = f->checked ? 'x' : ' '; out[2] = ']';
        break;
    case FT_RADIO:
        out[0] = '('; out[1] = f->checked ? 'o' : ' '; out[2] = ')';
        break;
    case FT_SUBMIT: {
        out[0] = '['; out[1] = ' ';
        int ll = (int)strlen(f->label);
        for (int i = 0; i < ll && 2 + i < w - 2; i++) out[2 + i] = f->label[i];
        out[w - 2] = ' '; out[w - 1] = ']';
        break;
    }
    case FT_SELECT: {
        out[0] = '[';
        int vl = (int)strlen(f->value);
        for (int i = 0; i < vl && 1 + i < w - 3; i++) out[1 + i] = f->value[i];
        out[w - 3] = ' '; out[w - 2] = 'v'; out[w - 1] = ']';
        break;
    }
    default: {                                   /* text, password, textarea */
        int body = w - 2;
        out[0] = '['; out[w - 1] = ']';
        int vl = (int)strlen(f->value);
        bool focused = (pg->focus_field > 0 && &pg->fields[pg->focus_field - 1] == f);
        if (vl == 0 && f->label[0] && !focused) {            /* placeholder */
            int ll = (int)strlen(f->label);
            for (int i = 0; i < ll && i < body; i++) out[1 + i] = f->label[i];
            *gray_from = 1;
            *gray_to = (uint8_t)(1 + (ll < body ? ll : body));
        } else {
            int show = vl;
            int start = 0;
            int room = focused ? body - 1 : body;
            if (show > room) { start = show - room; show = room; }
            for (int i = 0; i < show; i++) {
                char vc = f->value[start + i];
                out[1 + i] = (f->type == FT_PASSWORD) ? '*' : (vc >= 32 && vc < 127 ? vc : ' ');
            }
            if (focused) out[1 + show] = '_';
        }
        break;
    }
    }
}

static void draw_field(page_t *pg, int idx)
{
    field_t *f = &pg->fields[idx];
    if (!f->placed || f->row >= HTML_MAX_ROWS) return;
    char text[HTML_COLS + 1];
    uint8_t g0, g1;
    field_text(pg, f, text, &g0, &g1);
    for (int i = 0; i < f->width && f->col + i < HTML_COLS; i++) {
        hcell_t *c = &pg->cells[f->row * HTML_COLS + f->col + i];
        c->ch = text[i];
        c->link = 0;
        c->field = (uint8_t)(idx + 1);
        c->flags = HF_BG | HF_FG | (f->type == FT_SUBMIT ? HF_BOLD : 0);
        c->bg = rgb565(f->type == FT_SUBMIT ? 0xC8C8C8 : 0xE6E6E6);
        c->fg = rgb565((g1 && i >= g0 && i < g1) ? 0x808080 : 0x101010);
    }
}

static int field_width(const field_t *f)
{
    switch (f->type) {
    case FT_CHECKBOX:
    case FT_RADIO:  return 3;
    case FT_SUBMIT: { int l = (int)strlen(f->label); return (l > 30 ? 30 : l) + 4; }
    case FT_SELECT: { int l = (int)strlen(f->value); return (l > 24 ? 24 : l) + 5 < 8 ? 8 : (l > 24 ? 24 : l) + 5; }
    case FT_TEXTAREA: return 32;
    default:        return 24;
    }
}

static void place_field(lay_t *L, int idx)
{
    field_t *f = &L->pg->fields[idx];
    if (f->type == FT_HIDDEN || L->row >= HTML_MAX_ROWS) return;
    flush_word(L);
    int w = field_width(f);
    if (w > HTML_COLS) w = HTML_COLS;
    f->width = (uint8_t)w;
    if (L->col > 0 && L->col + 1 + w > HTML_COLS) {
        newline(L);
    } else if (L->col > 0) {
        put_plain(L, ' ');
    }
    f->row = (uint16_t)L->row;
    f->col = (uint8_t)L->col;
    f->placed = 1;
    L->col += w;
    draw_field(L->pg, idx);
}

static int new_field(lay_t *L, int type)
{
    page_t *pg = L->pg;
    if (pg->nfields >= HTML_MAX_FIELDS) return -1;
    field_t *f = &pg->fields[pg->nfields];
    memset(f, 0, sizeof(*f));
    f->type = (uint8_t)type;
    f->form = (int8_t)L->cur_form;
    return pg->nfields++;
}

void html_set_focus(page_t *pg, int id)
{
    int old = pg->focus_field;
    pg->focus_field = id;
    if (old > 0 && old <= pg->nfields) draw_field(pg, old - 1);
    if (id > 0 && id <= pg->nfields) draw_field(pg, id - 1);
}

void html_field_changed(page_t *pg, int id)
{
    if (id > 0 && id <= pg->nfields) draw_field(pg, id - 1);
}

void html_toggle(page_t *pg, int id)
{
    if (id < 1 || id > pg->nfields) return;
    field_t *f = &pg->fields[id - 1];
    if (f->type == FT_CHECKBOX) {
        f->checked = !f->checked;
    } else if (f->type == FT_RADIO) {
        for (int i = 0; i < pg->nfields; i++) {
            field_t *o = &pg->fields[i];
            if (o->type == FT_RADIO && o->form == f->form && strcmp(o->name, f->name) == 0 && o->checked) {
                o->checked = 0;
                draw_field(pg, i);
            }
        }
        f->checked = 1;
    }
    draw_field(pg, id - 1);
}

static void enc_append(char *out, int size, int *pos, const char *s)
{
    static const char hex[] = "0123456789ABCDEF";
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        bool plain = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                     c == '-' || c == '_' || c == '.' || c == '~';
        if (plain || c == ' ') {
            if (*pos < size - 1) out[(*pos)++] = (c == ' ') ? '+' : (char)c;
        } else if (*pos < size - 3) {
            out[(*pos)++] = '%';
            out[(*pos)++] = hex[c >> 4];
            out[(*pos)++] = hex[c & 15];
        }
    }
    out[*pos] = '\0';
}

int html_build_query(const page_t *pg, int form, int submit_id, char *out, int size)
{
    int pos = 0;
    out[0] = '\0';
    for (int i = 0; i < pg->nfields; i++) {
        const field_t *f = &pg->fields[i];
        if (f->form != form || !f->name[0]) continue;
        bool include = false;
        switch (f->type) {
        case FT_TEXT: case FT_PASSWORD: case FT_HIDDEN: case FT_TEXTAREA: case FT_SELECT: include = true; break;
        case FT_CHECKBOX: case FT_RADIO: include = f->checked; break;
        case FT_SUBMIT: include = (i + 1 == submit_id); break;
        }
        if (!include) continue;
        if (pos > 0 && pos < size - 1) out[pos++] = '&';
        enc_append(out, size, &pos, f->name);
        if (pos < size - 1) out[pos++] = '=';
        enc_append(out, size, &pos, (f->type == FT_CHECKBOX || f->type == FT_RADIO) && !f->value[0] ? "on" : f->value);
    }
    out[pos] = '\0';
    return pos;
}

/* ---------- Text, entities, attributes ---------- */
static void text_char(lay_t *L, char c)
{
    if (L->btn_field >= 0) {                              /* collecting a button label */
        field_t *f = &L->pg->fields[L->btn_field];
        int n = (int)strlen(f->label);
        if (c == '\n' || c == '\r' || c == '\t') c = ' ';
        if (c >= 32 && c < 127 && n < (int)sizeof(f->label) - 1 && !(c == ' ' && (n == 0 || f->label[n - 1] == ' '))) {
            f->label[n] = c;
            f->label[n + 1] = '\0';
        }
        return;
    }
    if (cur_style(L)->hidden || L->sel_field >= 0) {   /* hidden, or the text of an <option> */
        return;
    }
    if (c == '\n' || c == '\r' || c == ' ' || c == '\t') {
        if (cur_style(L)->pre) {
            flush_word(L);
            if (c == '\n') {
                newline(L);
            } else {
                put_plain(L, ' ');
            }
        } else {
            flush_word(L);
        }
        return;
    }
    if ((unsigned char)c < 32 || (unsigned char)c > 126) {
        return;
    }
    if (cur_style(L)->pre) {
        if (L->col >= HTML_COLS) newline(L);
        L->glue = true;                      /* no extra space between characters */
        add_char(L, c);
        flush_word(L);
        return;
    }
    add_char(L, c);
}

static const char *entity(const char *name, int n)
{
    static const struct { const char *name; const char *text; } table[] = {
        { "amp", "&" }, { "lt", "<" }, { "gt", ">" }, { "quot", "\"" }, { "apos", "'" },
        { "nbsp", " " }, { "copy", "(c)" }, { "reg", "(R)" }, { "mdash", "-" }, { "ndash", "-" },
        { "hellip", "..." }, { "laquo", "<<" }, { "raquo", ">>" }, { "middot", "." },
        { "lsquo", "'" }, { "rsquo", "'" }, { "ldquo", "\"" }, { "rdquo", "\"" }, { "bull", "*" },
        { "euro", "EUR" }, { "pound", "GBP" }, { "times", "x" }, { "larr", "<-" }, { "rarr", "->" }
    };
    for (unsigned i = 0; i < sizeof(table) / sizeof(table[0]); i++) {
        if ((int)strlen(table[i].name) == n && memcmp(table[i].name, name, (size_t)n) == 0) {
            return table[i].text;
        }
    }
    return 0;
}

static bool match_ci(const char *s, const char *end, const char *what)
{
    while (*what) {
        if (s >= end || lower(*s) != *what) {
            return false;
        }
        s++;
        what++;
    }
    return true;
}

static void decode_attr(char *s)
{
    char *w = s;
    while (*s) {
        if (*s == '&') {
            static const struct { const char *e; char c; } t[] = {
                { "&amp;", '&' }, { "&lt;", '<' }, { "&gt;", '>' }, { "&quot;", '"' }, { "&#39;", '\'' },
                { "&apos;", '\'' }, { "&#x27;", '\'' }, { "&nbsp;", ' ' }, { "&#43;", '+' }, { "&#x2F;", '/' }
            };
            bool done = false;
            for (unsigned i = 0; i < sizeof(t) / sizeof(t[0]); i++) {
                int l = (int)strlen(t[i].e);
                if (strncmp(s, t[i].e, (size_t)l) == 0) {
                    *w++ = t[i].c;
                    s += l;
                    done = true;
                    break;
                }
            }
            if (done) continue;
        }
        *w++ = *s++;
    }
    *w = '\0';
}

/* Finds attribute `name` inside [s, end) and copies its (entity-decoded) value */
static bool get_attr(const char *s, const char *end, const char *name, char *out, int outsize)
{
    int nl = (int)strlen(name);
    for (; s < end; s++) {
        bool boundary = (s[-1] == ' ' || s[-1] == '\t' || s[-1] == '\n' || s[-1] == '"' || s[-1] == '\'' || s[-1] == '/');
        if (!boundary || !match_ci(s, end, name)) {
            continue;
        }
        const char *p = s + nl;
        while (p < end && (*p == ' ' || *p == '\t')) p++;
        if (p >= end || *p != '=') {
            if (p >= end || *p == ' ' || *p == '/' || *p == '>' || *p == '\n') {   /* boolean attribute: checked, hidden */
                out[0] = '\0';
                return true;
            }
            continue;
        }
        p++;
        while (p < end && (*p == ' ' || *p == '\t')) p++;
        char q = 0;
        if (p < end && (*p == '"' || *p == '\'')) {
            q = *p++;
        }
        int i = 0;
        while (p < end && i < outsize - 1) {
            if (q ? (*p == q) : (*p == ' ' || *p == '>' || *p == '\t' || *p == '\n')) {
                break;
            }
            out[i++] = *p++;
        }
        out[i] = '\0';
        decode_attr(out);
        return true;
    }
    return false;
}

static bool is_void_tag(const char *t)
{
    static const char *const names[] = {
        "br", "hr", "img", "input", "meta", "link", "area", "base", "col", "embed", "source", "track", "wbr", "param"
    };
    for (unsigned i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        if (strcmp(t, names[i]) == 0) return true;
    }
    return false;
}

static bool is_block_tag(const char *t)
{
    static const char *const names[] = {
        "p", "div", "ul", "ol", "tr", "table", "blockquote", "section", "article", "header", "footer",
        "nav", "form", "dl", "dt", "dd", "center", "address", "main", "aside", "figure", "figcaption",
        "body", "html", "head", "fieldset", "details", "summary", "li", "pre", "h1", "h2", "h3", "h4", "h5", "h6"
    };
    for (unsigned i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        if (strcmp(t, names[i]) == 0) return true;
    }
    return false;
}

static bool is_blank_block(const char *t)       /* blocks that also get an empty line around them */
{
    return strcmp(t, "p") == 0 || strcmp(t, "table") == 0 || strcmp(t, "blockquote") == 0 ||
           strcmp(t, "pre") == 0 || strcmp(t, "ul") == 0 || strcmp(t, "ol") == 0 ||
           (t[0] == 'h' && t[1] >= '1' && t[1] <= '6' && t[2] == '\0');
}

static bool skip_content_tag(const char *t)     /* never visible */
{
    return strcmp(t, "template") == 0 || strcmp(t, "svg") == 0 || strcmp(t, "canvas") == 0 ||
           strcmp(t, "iframe") == 0 || strcmp(t, "object") == 0 || strcmp(t, "audio") == 0 ||
           strcmp(t, "video") == 0 || strcmp(t, "noscript_never") == 0;
}

/* ---------- Style stack ---------- */
static void apply_decl_to(est_t *s, const css_decl_t *d)
{
    if (d->set & CSS_FG) { s->fg = d->fg; s->flags |= HF_FG; }
    if (d->set & CSS_BG) { s->bg = d->bg; s->flags |= HF_BG; }
    if (d->set & CSS_BOLD) { if (d->bold) s->flags |= HF_BOLD; else s->flags &= (uint8_t)~HF_BOLD; }
    if (d->set & CSS_UNDERLINE) { if (d->underline) s->flags |= HF_UNDERLINE; else s->flags &= (uint8_t)~HF_UNDERLINE; }
    if (d->set & CSS_HIDDEN) s->hidden = d->hidden;
}

static void push_element(lay_t *L, const char *name, const char *attrs, const char *attrs_end, bool *is_new_link)
{
    (void)is_new_link;
    if (L->depth >= MAX_DEPTH) {
        return;
    }
    int d = L->depth;
    const est_t *parent = d > 0 ? &L->es[d - 1] : &root_style;

    /* identify the element for the CSS matcher */
    css_elem_t *e = &L->el[d];
    memset(e, 0, sizeof(*e));
    e->tag = css_hash(name, (int)strlen(name));
    L->tagh[d] = e->tag;
    char buf[160];
    if (get_attr(attrs, attrs_end, "id", buf, 40)) {
        e->id = css_hash(buf, (int)strlen(buf));
    }
    if (get_attr(attrs, attrs_end, "class", buf, sizeof(buf))) {
        const char *p = buf;
        while (*p && e->ncls < CSS_ELEM_CLASSES) {
            while (*p == ' ' || *p == '\t' || *p == '\n') p++;
            const char *st = p;
            while (*p && *p != ' ' && *p != '\t' && *p != '\n') p++;
            if (p > st) e->cls[e->ncls++] = css_hash(st, (int)(p - st));
        }
    }

    est_t s = *parent;
    s.hidden = parent->hidden;

    /* browser defaults per tag */
    if (strcmp(name, "b") == 0 || strcmp(name, "strong") == 0 || strcmp(name, "th") == 0 ||
        (name[0] == 'h' && name[1] >= '1' && name[1] <= '6' && name[2] == '\0') || strcmp(name, "dt") == 0) {
        s.flags |= HF_BOLD;
    }
    if (strcmp(name, "u") == 0 || strcmp(name, "ins") == 0) s.flags |= HF_UNDERLINE;
    if (strcmp(name, "mark") == 0) { s.flags |= HF_BG; s.bg = 0xFFF176; }
    if (strcmp(name, "pre") == 0) s.pre = 1;
    if (strcmp(name, "a") == 0) {
        s.flags |= HF_FG | HF_UNDERLINE;
        s.fg = 0x1030D0;
    }
    if (skip_content_tag(name) || strcmp(name, "head") == 0) s.hidden = 1;
    if (strcmp(name, "font") == 0 && get_attr(attrs, attrs_end, "color", buf, 24)) {
        uint32_t rgb;
        if (css_parse_color(buf, (int)strlen(buf), &rgb)) { s.fg = rgb; s.flags |= HF_FG; }
    }
    if (get_attr(attrs, attrs_end, "hidden", buf, 8)) s.hidden = 1;

    /* style sheet rules, lowest specificity first */
    if (sheet_ok) {
        for (int i = 0; i < sheet.n; i++) {
            const css_rule_t *r = &sheet.rules[sheet.sorted[i]];
            if (r->chain[r->n - 1].tag && r->chain[r->n - 1].tag != e->tag) continue;   /* quick reject */
            if (css_match(r, L->el, d + 1)) apply_decl_to(&s, &r->d);
        }
    }
    /* inline style="" */
    char style[256];
    if (get_attr(attrs, attrs_end, "style", style, sizeof(style))) {
        css_decl_t dec;
        if (css_parse_decls(style, (uint32_t)strlen(style), &dec)) apply_decl_to(&s, &dec);
    }
    s.hidden |= parent->hidden;

    L->es[d] = s;
    L->depth++;
}

/* ---------- Main HTML walk ---------- */
static void pop_to(lay_t *L, uint32_t tag_hash)
{
    for (int i = L->depth - 1; i >= 0; i--) {
        if (L->tagh[i] == tag_hash) {
            L->depth = i;
            return;
        }
    }
}

static void render_plain(lay_t *L, const uint8_t *data, uint32_t len)
{
    for (uint32_t i = 0; i < len && L->row < HTML_MAX_ROWS; i++) {
        char c = (char)data[i];
        if (c == '\n') {
            newline(L);
        } else if (c == '\t') {
            for (int k = 0; k < 4 && L->col < HTML_COLS; k++) put_plain(L, ' ');
        } else if (c >= 32 && c < 127) {
            if (L->col >= HTML_COLS) newline(L);
            hcell_t cell = { c, 0, 0, 0, 0, 0 };
            put_cell(L, cell);
        }
    }
}

static void finish_button(lay_t *L)
{
    if (L->btn_field < 0) return;
    field_t *f = &L->pg->fields[L->btn_field];
    if (!f->label[0]) copy_str(f->label, sizeof(f->label), f->value[0] ? f->value : "Submit");
    int idx = L->btn_field;
    L->btn_field = -1;
    if (!cur_style(L)->hidden) place_field(L, idx);
}

static void finish_select(lay_t *L)
{
    if (L->sel_field < 0) return;
    int idx = L->sel_field;
    L->sel_field = -1;
    if (!cur_style(L)->hidden) place_field(L, idx);
}

static void handle_input(lay_t *L, const char *base, const char *attrs, const char *end)
{
    (void)base;
    char type[16] = "text";
    get_attr(attrs, end, "type", type, sizeof(type));
    for (char *q = type; *q; q++) *q = lower(*q);

    int ft;
    if (strcmp(type, "password") == 0) ft = FT_PASSWORD;
    else if (strcmp(type, "hidden") == 0) ft = FT_HIDDEN;
    else if (strcmp(type, "submit") == 0 || strcmp(type, "image") == 0) ft = FT_SUBMIT;
    else if (strcmp(type, "checkbox") == 0) ft = FT_CHECKBOX;
    else if (strcmp(type, "radio") == 0) ft = FT_RADIO;
    else if (strcmp(type, "button") == 0 || strcmp(type, "reset") == 0 || strcmp(type, "file") == 0) return;
    else ft = FT_TEXT;

    int idx = new_field(L, ft);
    if (idx < 0) return;
    field_t *f = &L->pg->fields[idx];
    char tmp[HTML_FIELD_VALUE];
    if (get_attr(attrs, end, "name", tmp, sizeof(f->name))) copy_str(f->name, sizeof(f->name), tmp);
    if (get_attr(attrs, end, "value", tmp, sizeof(tmp))) copy_str(f->value, sizeof(f->value), tmp);
    if (get_attr(attrs, end, "checked", tmp, 8)) f->checked = 1;
    if (ft == FT_SUBMIT) {
        copy_str(f->label, sizeof(f->label), f->value[0] ? f->value : "Submit");
    } else if (get_attr(attrs, end, "placeholder", tmp, sizeof(f->label))) {
        copy_str(f->label, sizeof(f->label), tmp);
    }
    if (!cur_style(L)->hidden) place_field(L, idx);
}

static void render_html(lay_t *L, const char *base_url, const char *s, uint32_t len)
{
    const char *end = s + len;
    const char *p = s;
    page_t *pg = L->pg;

    while (p < end && L->row < HTML_MAX_ROWS) {
        char c = *p;

        if (c == '<') {
            if (p + 3 < end && p[1] == '!' && p[2] == '-' && p[3] == '-') {      /* comment */
                const char *q = p + 4;
                while (q + 2 < end && !(q[0] == '-' && q[1] == '-' && q[2] == '>')) q++;
                p = (q + 2 < end) ? q + 3 : end;
                continue;
            }
            if (p + 1 < end && (is_alpha(p[1]) || p[1] == '/' || p[1] == '!')) {
                const char *tag = p + 1;
                const char *q = tag;
                char quote = 0;
                while (q < end && (quote || *q != '>')) {
                    if (quote) { if (*q == quote) quote = 0; }
                    else if (*q == '"' || *q == '\'') quote = *q;
                    q++;
                }
                const char *tag_end = q;
                p = (q < end) ? q + 1 : end;

                bool closing = (*tag == '/');
                if (closing) tag++;
                char name[16];
                int nl = 0;
                while (tag < tag_end && (is_alpha(*tag) || is_digit(*tag)) && nl < 15) {
                    name[nl++] = lower(*tag++);
                }
                name[nl] = '\0';
                if (nl == 0) continue;                           /* <!DOCTYPE ...> */
                bool self_closing = (tag_end > tag && tag_end[-1] == '/');
                const est_t *cs = cur_style(L);
                bool hidden = cs->hidden;

                /* ---- elements whose content is skipped or captured ---- */
                if (!closing && (strcmp(name, "script") == 0 || strcmp(name, "style") == 0 || strcmp(name, "textarea") == 0 ||
                                 strcmp(name, "title") == 0)) {
                    const char *needle = strcmp(name, "script") == 0 ? "</script" :
                                         strcmp(name, "style") == 0 ? "</style" :
                                         strcmp(name, "title") == 0 ? "</title" : "</textarea";
                    const char *cs0 = p;
                    while (p < end && !match_ci(p, end, needle)) p++;
                    if (name[1] == 't' && name[0] == 's') {                       /* <style> */
                        if (sheet_ok) css_parse(&sheet, cs0, (uint32_t)(p - cs0));
                    } else if (name[0] == 't' && name[1] == 'i') {                /* <title> */
                        int i = 0;
                        for (const char *t = cs0; t < p && i < (int)sizeof(pg->title) - 1; t++) {
                            if (*t >= 32 && *t < 127) pg->title[i++] = *t;
                        }
                        pg->title[i] = '\0';
                    } else if (name[0] == 't' && name[1] == 'e') {                /* <textarea> */
                        int idx = new_field(L, FT_TEXTAREA);
                        if (idx >= 0) {
                            field_t *f = &pg->fields[idx];
                            char tmp[40];
                            if (get_attr(tag, tag_end, "name", tmp, sizeof(f->name))) copy_str(f->name, sizeof(f->name), tmp);
                            int n = 0;
                            for (const char *t = cs0; t < p && n < HTML_FIELD_VALUE - 1; t++) {
                                if (*t == '\r') continue;
                                f->value[n++] = *t;
                            }
                            f->value[n] = '\0';
                            decode_attr(f->value);
                            if (!hidden) place_field(L, idx);
                        }
                    }
                    continue;
                }

                if (closing) {
                    if (strcmp(name, "button") == 0) finish_button(L);
                    else if (strcmp(name, "select") == 0) finish_select(L);
                    else if (strcmp(name, "form") == 0) { finish_button(L); L->cur_form = -1; }
                    pop_to(L, css_hash(name, nl));
                    if (is_block_tag(name)) block_break(L, is_blank_block(name));
                    continue;
                }

                /* ---- opening (or void) tag ---- */
                if (is_void_tag(name)) {
                    /* void elements: style is only needed for a few of them */
                    if (strcmp(name, "br") == 0) {
                        if (!hidden) { flush_word(L); newline(L); }
                    } else if (strcmp(name, "hr") == 0) {
                        if (!hidden) {
                            block_break(L, false);
                            for (int i = 0; i < HTML_COLS - 8; i++) put_plain(L, '-');
                            newline(L);
                        }
                    } else if (strcmp(name, "input") == 0) {
                        handle_input(L, base_url, tag, tag_end);
                    } else if (strcmp(name, "img") == 0) {
                        char alt[48];
                        if (!hidden && get_attr(tag, tag_end, "alt", alt, sizeof(alt)) && alt[0]) {
                            L->gray = true;
                            add_char(L, '[');
                            for (char *a = alt; *a; a++) {
                                char ch = (*a >= 32 && *a < 127) ? *a : '?';
                                if (ch == ' ') { add_char(L, ']'); flush_word(L); add_char(L, '['); }   /* keep words wrappable */
                                else add_char(L, ch);
                            }
                            add_char(L, ']');
                            L->gray = false;
                        }
                    } else if (strcmp(name, "meta") == 0) {
                        char he[24], content[HTML_URL_MAX + 24];
                        if (get_attr(tag, tag_end, "http-equiv", he, sizeof(he)) && match_ci(he, he + sizeof(he), "refresh") &&
                            get_attr(tag, tag_end, "content", content, sizeof(content)) && !pg->refresh[0]) {
                            const char *u = content;
                            while (*u && !(lower(u[0]) == 'u' && lower(u[1]) == 'r' && lower(u[2]) == 'l' && u[3] == '=')) u++;
                            if (*u) {
                                u += 4;
                                if (*u == '\'' || *u == '"') u++;
                                char target[HTML_URL_MAX];
                                int i = 0;
                                while (*u && *u != '\'' && *u != '"' && i < (int)sizeof(target) - 1) target[i++] = *u++;
                                target[i] = '\0';
                                if (i) url_join(base_url, target, pg->refresh, HTML_URL_MAX);
                            }
                        }
                    } else if (strcmp(name, "link") == 0) {
                        char rel[32], href[HTML_URL_MAX + 32], media[24];
                        if (get_attr(tag, tag_end, "rel", rel, sizeof(rel)) && match_ci(rel, rel + sizeof(rel), "stylesheet") &&
                            get_attr(tag, tag_end, "href", href, sizeof(href)) && pg->ncss < HTML_MAX_CSS) {
                            bool print = get_attr(tag, tag_end, "media", media, sizeof(media)) && match_ci(media, media + sizeof(media), "print");
                            if (!print) url_join(base_url, href, pg->css_links[pg->ncss++], HTML_URL_MAX);
                        }
                    }
                    continue;
                }

                /* implied end tags: <li>a<li>b, <p>a<p>b, <td>... */
                if (L->depth > 0 && (strcmp(name, "li") == 0 || strcmp(name, "p") == 0 || strcmp(name, "td") == 0 ||
                                     strcmp(name, "th") == 0 || strcmp(name, "tr") == 0 || strcmp(name, "dt") == 0 ||
                                     strcmp(name, "dd") == 0 || strcmp(name, "option") == 0)) {
                    if (L->tagh[L->depth - 1] == css_hash(name, nl)) L->depth--;
                }

                bool dummy = false;
                push_element(L, name, tag, tag_end, &dummy);
                cs = cur_style(L);
                hidden = cs->hidden;

                if (strcmp(name, "body") == 0 || strcmp(name, "html") == 0) {
                    if (cs->flags & HF_BG) { pg->has_page_bg = true; pg->page_bg = cs->bg; }
                }

                if (strcmp(name, "a") == 0 && !hidden) {
                    char href[HTML_URL_MAX + 64];
                    if (L->depth > 0 && get_attr(tag, tag_end, "href", href, sizeof(href)) && href[0] != '#' &&
                        !match_ci(href, href + sizeof(href), "javascript:") &&
                        !match_ci(href, href + sizeof(href), "mailto:") && pg->nlinks < HTML_MAX_LINKS) {
                        url_join(base_url, href, pg->links[pg->nlinks], HTML_URL_MAX);
                        pg->nlinks++;
                        L->es[L->depth - 1].link = (uint8_t)pg->nlinks;
                    } else if (L->depth > 0) {
                        L->es[L->depth - 1].link = 0;
                        L->es[L->depth - 1].flags &= (uint8_t)~HF_UNDERLINE;
                    }
                } else if (strcmp(name, "form") == 0) {
                    block_break(L, false);
                    if (pg->nforms < HTML_MAX_FORMS) {
                        form_t *f = &pg->forms[pg->nforms];
                        memset(f, 0, sizeof(*f));
                        char action[HTML_URL_MAX + 64], method[12];
                        if (get_attr(tag, tag_end, "action", action, sizeof(action)) && action[0] && action[0] != '#') {
                            url_join(base_url, action, f->action, HTML_URL_MAX);
                        } else {
                            copy_str(f->action, HTML_URL_MAX, base_url);
                            for (char *q = f->action; *q; q++) if (*q == '#') { *q = '\0'; break; }
                        }
                        f->post = get_attr(tag, tag_end, "method", method, sizeof(method)) && lower(method[0]) == 'p';
                        L->cur_form = pg->nforms++;
                    }
                } else if (strcmp(name, "button") == 0) {
                    char type[12] = "submit", tmp[HTML_FIELD_VALUE];
                    get_attr(tag, tag_end, "type", type, sizeof(type));
                    if (lower(type[0]) == 's') {
                        int idx = new_field(L, FT_SUBMIT);
                        if (idx >= 0) {
                            field_t *f = &pg->fields[idx];
                            if (get_attr(tag, tag_end, "name", tmp, sizeof(f->name))) copy_str(f->name, sizeof(f->name), tmp);
                            if (get_attr(tag, tag_end, "value", tmp, sizeof(tmp))) copy_str(f->value, sizeof(f->value), tmp);
                            L->btn_field = idx;
                        }
                    }
                } else if (strcmp(name, "select") == 0) {
                    int idx = new_field(L, FT_SELECT);
                    if (idx >= 0) {
                        field_t *f = &pg->fields[idx];
                        char tmp[40];
                        if (get_attr(tag, tag_end, "name", tmp, sizeof(f->name))) copy_str(f->name, sizeof(f->name), tmp);
                        L->sel_field = idx;
                        L->sel_has_value = false;
                    }
                } else if (strcmp(name, "option") == 0 && L->sel_field >= 0) {
                    field_t *f = &pg->fields[L->sel_field];
                    char val[HTML_FIELD_VALUE], sel[8];
                    bool selected = get_attr(tag, tag_end, "selected", sel, sizeof(sel));
                    if (!L->sel_has_value || selected) {
                        if (!get_attr(tag, tag_end, "value", val, sizeof(val))) {
                            int i = 0;                                       /* option text as the value */
                            for (const char *t = p; t < end && *t != '<' && i < 60; t++) {
                                if (*t >= 32 && *t < 127) val[i++] = *t;
                            }
                            val[i] = '\0';
                        }
                        copy_str(f->value, sizeof(f->value), val);
                        L->sel_has_value = true;
                    }
                } else if (strcmp(name, "li") == 0 && !hidden) {
                    block_break(L, false);
                    add_char(L, '*');
                } else if (strcmp(name, "td") == 0 || strcmp(name, "th") == 0) {
                    flush_word(L);
                } else if (is_block_tag(name) && !hidden) {
                    block_break(L, is_blank_block(name));
                }
                if (self_closing) {
                    pop_to(L, css_hash(name, nl));
                }
                continue;
            }
            /* a lone '<' is just text */
        }

        if (c == '&') {
            const char *q = p + 1;
            while (q < end && q - p < 10 && *q != ';' && *q != ' ' && *q != '<' && *q != '&') q++;
            if (q < end && *q == ';') {
                const char *ent = p + 1;
                int n = (int)(q - ent);
                if (n > 1 && ent[0] == '#') {
                    uint32_t v = 0;
                    bool hex = (ent[1] == 'x' || ent[1] == 'X');
                    for (int i = hex ? 2 : 1; i < n; i++) {
                        char d = lower(ent[i]);
                        int dv = is_digit(d) ? d - '0' : (hex && d >= 'a' && d <= 'f') ? d - 'a' + 10 : -1;
                        if (dv < 0) { v = 0; break; }
                        v = v * (hex ? 16u : 10u) + (uint32_t)dv;
                    }
                    if (v >= 32 && v < 127) text_char(L, (char)v);
                    else if (v == 160) text_char(L, ' ');
                    else if (v == 8211 || v == 8212) text_char(L, '-');
                    else if (v == 8216 || v == 8217) text_char(L, '\'');
                    else if (v == 8220 || v == 8221) text_char(L, '"');
                    p = q + 1;
                    continue;
                }
                const char *rep = entity(ent, n);
                if (rep) {
                    for (; *rep; rep++) text_char(L, *rep);
                    p = q + 1;
                    continue;
                }
            }
        }

        text_char(L, c);
        p++;
    }
    flush_word(L);
}

bool html_page_init(page_t *pg)
{
    memset(pg, 0, sizeof(*pg));
    pg->cells = kmalloc((size_t)HTML_COLS * HTML_MAX_ROWS * sizeof(hcell_t));
    sheet_ok = css_sheet_init(&sheet);
    return pg->cells != 0;
}

void html_render(page_t *pg, const char *base_url, const uint8_t *data, uint32_t len, bool is_html,
                 const char *extra_css, uint32_t extra_css_len)
{
    if (!pg->cells) {
        return;
    }
    memset(pg->cells, 0, (size_t)HTML_COLS * HTML_MAX_ROWS * sizeof(hcell_t));
    pg->nlinks = pg->nfields = pg->nforms = pg->ncss = 0;
    pg->focus_field = 0;
    pg->title[0] = '\0';
    pg->refresh[0] = '\0';
    pg->has_page_bg = false;

    lay_t *L = kmalloc(sizeof(lay_t));
    if (!L) {
        return;
    }
    memset(L, 0, sizeof(*L));
    L->pg = pg;
    L->cur_form = -1;
    L->btn_field = -1;
    L->sel_field = -1;
    root_style.fg = 0x202020;

    if (sheet_ok) {
        css_sheet_clear(&sheet);
        if (is_html && extra_css && extra_css_len) {
            css_parse(&sheet, extra_css, extra_css_len);
        }
    }
    if (is_html) {
        render_html(L, base_url, (const char *)data, len);
    } else {
        render_plain(L, data, len);
    }
    pg->rows = L->row + (L->col > 0 ? 1 : 0);
    if (pg->rows > HTML_MAX_ROWS) {
        pg->rows = HTML_MAX_ROWS;
    }
    kfree(L);
}
