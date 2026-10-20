/* css.c - tiny CSS parser and selector matcher */
#include "css.h"
#include "heap.h"
#include "string.h"

static char lower(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c; }
static bool is_ws(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f'; }
static bool is_ident(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_'; }

uint32_t css_hash(const char *s, int len)
{
    uint32_t h = 2166136261u;
    for (int i = 0; i < len; i++) {
        h ^= (uint8_t)lower(s[i]);
        h *= 16777619u;
    }
    return h ? h : 1;
}

bool css_sheet_init(css_sheet_t *sh)
{
    sh->rules = kmalloc(sizeof(css_rule_t) * CSS_MAX_RULES);
    sh->sorted = kmalloc(sizeof(uint16_t) * CSS_MAX_RULES);
    sh->n = 0;
    sh->next_order = 0;
    return sh->rules && sh->sorted;
}

void css_sheet_clear(css_sheet_t *sh)
{
    sh->n = 0;
    sh->next_order = 0;
}

/* ---------- Colors ---------- */
static const struct { const char *name; uint32_t rgb; } color_names[] = {
    { "black", 0x000000 }, { "white", 0xFFFFFF }, { "red", 0xFF0000 }, { "green", 0x008000 },
    { "blue", 0x0000FF }, { "yellow", 0xFFFF00 }, { "orange", 0xFFA500 }, { "gray", 0x808080 },
    { "grey", 0x808080 }, { "silver", 0xC0C0C0 }, { "purple", 0x800080 }, { "navy", 0x000080 },
    { "teal", 0x008080 }, { "maroon", 0x800000 }, { "lime", 0x00FF00 }, { "aqua", 0x00FFFF },
    { "cyan", 0x00FFFF }, { "fuchsia", 0xFF00FF }, { "magenta", 0xFF00FF }, { "pink", 0xFFC0CB },
    { "brown", 0xA52A2A }, { "olive", 0x808000 }, { "gold", 0xFFD700 }, { "darkgray", 0xA9A9A9 },
    { "darkgrey", 0xA9A9A9 }, { "lightgray", 0xD3D3D3 }, { "lightgrey", 0xD3D3D3 },
    { "darkblue", 0x00008B }, { "darkgreen", 0x006400 }, { "darkred", 0x8B0000 },
    { "lightblue", 0xADD8E6 }, { "lightgreen", 0x90EE90 }, { "whitesmoke", 0xF5F5F5 },
    { "crimson", 0xDC143C }, { "royalblue", 0x4169E1 }, { "steelblue", 0x4682B4 },
    { "dodgerblue", 0x1E90FF }, { "tomato", 0xFF6347 }, { "coral", 0xFF7F50 }, { "indigo", 0x4B0082 },
    { "violet", 0xEE82EE }, { "beige", 0xF5F5DC }, { "ivory", 0xFFFFF0 }, { "khaki", 0xF0E68C },
    { "salmon", 0xFA8072 }, { "tan", 0xD2B48C }, { "turquoise", 0x40E0D0 }, { "gainsboro", 0xDCDCDC }
};

static int hexval(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    c = lower(c);
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

bool css_parse_color(const char *s, int len, uint32_t *rgb)
{
    while (len > 0 && is_ws(*s)) { s++; len--; }
    while (len > 0 && is_ws(s[len - 1])) len--;
    if (len <= 0) return false;

    if (s[0] == '#') {
        int v[8], n = 0;
        for (int i = 1; i < len && n < 8; i++) {
            int h = hexval(s[i]);
            if (h < 0) return false;
            v[n++] = h;
        }
        if (n == 3) {
            *rgb = ((uint32_t)(v[0] * 17) << 16) | ((uint32_t)(v[1] * 17) << 8) | (uint32_t)(v[2] * 17);
            return true;
        }
        if (n == 6 || n == 8) {
            *rgb = ((uint32_t)(v[0] * 16 + v[1]) << 16) | ((uint32_t)(v[2] * 16 + v[3]) << 8) | (uint32_t)(v[4] * 16 + v[5]);
            return true;
        }
        return false;
    }
    if (len > 4 && lower(s[0]) == 'r' && lower(s[1]) == 'g' && lower(s[2]) == 'b') {
        const char *p = s + 3;
        const char *end = s + len;
        if (p < end && lower(*p) == 'a') p++;
        if (p >= end || *p != '(') return false;
        p++;
        uint32_t comp[3] = { 0, 0, 0 };
        for (int i = 0; i < 3; i++) {
            while (p < end && (is_ws(*p) || *p == ',' || *p == '/')) p++;
            uint32_t n = 0;
            bool any = false;
            while (p < end && *p >= '0' && *p <= '9') { n = n * 10 + (uint32_t)(*p++ - '0'); any = true; }
            if (!any) return false;
            if (p < end && *p == '%') { n = n * 255 / 100; p++; }
            comp[i] = n > 255 ? 255 : n;
        }
        *rgb = (comp[0] << 16) | (comp[1] << 8) | comp[2];
        return true;
    }
    for (unsigned i = 0; i < sizeof(color_names) / sizeof(color_names[0]); i++) {
        const char *nm = color_names[i].name;
        int nl = (int)strlen(nm);
        if (nl != len) continue;
        int k = 0;
        while (k < nl && lower(s[k]) == nm[k]) k++;
        if (k == nl) {
            *rgb = color_names[i].rgb;
            return true;
        }
    }
    return false;
}

/* ---------- Declarations ---------- */
static bool word_is(const char *s, int len, const char *w)
{
    int wl = (int)strlen(w);
    if (wl != len) return false;
    for (int i = 0; i < len; i++) if (lower(s[i]) != w[i]) return false;
    return true;
}

static bool contains_word(const char *s, int len, const char *w)
{
    int wl = (int)strlen(w);
    for (int i = 0; i + wl <= len; i++) {
        int k = 0;
        while (k < wl && lower(s[i + k]) == w[k]) k++;
        if (k == wl) return true;
    }
    return false;
}

static void apply_decl(css_decl_t *d, const char *name, int nl, const char *val, int vl)
{
    while (vl > 0 && is_ws(*val)) { val++; vl--; }
    while (vl > 0 && is_ws(val[vl - 1])) vl--;
    for (int i = 0; i + 9 <= vl; i++) {                        /* strip "!important" */
        if (val[i] == '!' ) { vl = i; while (vl > 0 && is_ws(val[vl - 1])) vl--; break; }
    }
    if (vl <= 0) return;

    uint32_t rgb;
    if (word_is(name, nl, "color")) {
        if (css_parse_color(val, vl, &rgb)) { d->fg = rgb; d->set |= CSS_FG; }
    } else if (word_is(name, nl, "background-color") || word_is(name, nl, "background")) {
        int i = 0;                                              /* the first token that is a color */
        while (i < vl) {
            while (i < vl && is_ws(val[i])) i++;
            int start = i;
            int depth = 0;
            while (i < vl && (depth > 0 || !is_ws(val[i]))) {
                if (val[i] == '(') depth++;
                if (val[i] == ')') depth--;
                i++;
            }
            if (i > start && css_parse_color(val + start, i - start, &rgb)) {
                d->bg = rgb;
                d->set |= CSS_BG;
                break;
            }
        }
    } else if (word_is(name, nl, "display")) {
        d->hidden = word_is(val, vl, "none");
        d->set |= CSS_HIDDEN;
    } else if (word_is(name, nl, "visibility")) {
        if (word_is(val, vl, "hidden") || word_is(val, vl, "collapse")) { d->hidden = 1; d->set |= CSS_HIDDEN; }
    } else if (word_is(name, nl, "font-weight")) {
        if (word_is(val, vl, "bold") || word_is(val, vl, "bolder")) { d->bold = 1; d->set |= CSS_BOLD; }
        else if (word_is(val, vl, "normal") || word_is(val, vl, "lighter")) { d->bold = 0; d->set |= CSS_BOLD; }
        else if (val[0] >= '1' && val[0] <= '9') {
            d->bold = (val[0] >= '6'); d->set |= CSS_BOLD;
        }
    } else if (word_is(name, nl, "font")) {
        if (contains_word(val, vl, "bold")) { d->bold = 1; d->set |= CSS_BOLD; }
    } else if (word_is(name, nl, "text-decoration") || word_is(name, nl, "text-decoration-line")) {
        if (contains_word(val, vl, "underline")) { d->underline = 1; d->set |= CSS_UNDERLINE; }
        else if (contains_word(val, vl, "none")) { d->underline = 0; d->set |= CSS_UNDERLINE; }
    }
}

bool css_parse_decls(const char *s, uint32_t len, css_decl_t *out)
{
    memset(out, 0, sizeof(*out));
    uint32_t i = 0;
    while (i < len) {
        while (i < len && (is_ws(s[i]) || s[i] == ';')) i++;
        uint32_t ns = i;
        while (i < len && s[i] != ':' && s[i] != ';') i++;
        if (i >= len || s[i] != ':') break;
        uint32_t ne = i;
        while (ne > ns && is_ws(s[ne - 1])) ne--;
        i++;
        uint32_t vs = i;
        int paren = 0;
        while (i < len && (paren > 0 || s[i] != ';')) {
            if (s[i] == '(') paren++;
            if (s[i] == ')') paren--;
            i++;
        }
        apply_decl(out, s + ns, (int)(ne - ns), s + vs, (int)(i - vs));
    }
    return out->set != 0;
}

/* ---------- Selectors ---------- */
/* Parses one selector ("div.menu > a") into a rule chain. Returns false for anything unsupported
 * (attribute selectors, pseudo classes, sibling combinators ...): such rules are dropped. */
static bool parse_selector(const char *s, int len, css_rule_t *r)
{
    int i = 0, n = 0;
    int ids = 0, classes = 0, tags = 0;
    bool child_next = false;
    r->n = 0;

    for (;;) {
        while (i < len && is_ws(s[i])) i++;
        if (i >= len) break;
        if (s[i] == '>') { child_next = true; i++; continue; }
        if (n >= CSS_MAX_CHAIN) return false;

        css_compound_t *c = &r->chain[n];
        memset(c, 0, sizeof(*c));
        c->child = child_next && n > 0;
        child_next = false;
        int ncls = 0;
        bool any = false;

        while (i < len && !is_ws(s[i]) && s[i] != '>') {
            char ch = s[i];
            if (ch == '*') {
                i++;
                any = true;
            } else if (ch == '.' || ch == '#' || is_ident(ch)) {
                bool is_class = (ch == '.'), is_id = (ch == '#');
                if (is_class || is_id) i++;
                int st = i;
                while (i < len && is_ident(s[i])) i++;
                if (i == st) return false;
                uint32_t h = css_hash(s + st, i - st);
                if (is_class) {
                    if (ncls >= CSS_MAX_CLASSES) return false;
                    c->cls[ncls++] = h;
                    classes++;
                } else if (is_id) {
                    c->id = h;
                    ids++;
                } else {
                    c->tag = h;
                    tags++;
                }
                any = true;
            } else {
                return false;                                   /* ':' '[' '+' '~' ... */
            }
        }
        if (!any) return false;
        n++;
    }
    if (n == 0 || child_next) return false;
    r->n = (uint8_t)n;
    int spec = ids * 100 + classes * 10 + tags;
    r->spec = (uint16_t)(spec > 65000 ? 65000 : spec);
    return true;
}

static void sort_sheet(css_sheet_t *sh)
{
    for (int i = 0; i < sh->n; i++) {
        sh->sorted[i] = (uint16_t)i;
    }
    for (int i = 1; i < sh->n; i++) {                           /* stable insertion sort */
        uint16_t v = sh->sorted[i];
        const css_rule_t *rv = &sh->rules[v];
        int j = i - 1;
        while (j >= 0 && sh->rules[sh->sorted[j]].spec > rv->spec) {
            sh->sorted[j + 1] = sh->sorted[j];
            j--;
        }
        sh->sorted[j + 1] = v;
    }
}

static uint32_t skip_block(const char *s, uint32_t i, uint32_t len)   /* i at '{' */
{
    int depth = 0;
    for (; i < len; i++) {
        if (s[i] == '{') depth++;
        else if (s[i] == '}' && --depth == 0) return i + 1;
    }
    return len;
}

void css_parse(css_sheet_t *sh, const char *css, uint32_t len)
{
    if (!sh->rules) return;
    uint32_t i = 0;
    while (i < len) {
        while (i < len && is_ws(css[i])) i++;
        if (i + 1 < len && css[i] == '/' && css[i + 1] == '*') {            /* comment */
            i += 2;
            while (i + 1 < len && !(css[i] == '*' && css[i + 1] == '/')) i++;
            i += 2;
            continue;
        }
        if (i >= len) break;
        if (css[i] == '<' || css[i] == '}') { i++; continue; }              /* stray characters */
        if (css[i] == '@') {                                                /* @media, @import, @font-face ... */
            while (i < len && css[i] != ';' && css[i] != '{') i++;
            if (i < len && css[i] == '{') i = skip_block(css, i, len); else i++;
            continue;
        }

        uint32_t sel_start = i;
        while (i < len && css[i] != '{' && css[i] != ';') i++;
        if (i >= len || css[i] == ';') { i++; continue; }
        uint32_t sel_end = i;
        uint32_t body_start = i + 1;
        uint32_t body_end = body_start;
        while (body_end < len && css[body_end] != '}') body_end++;
        i = body_end < len ? body_end + 1 : len;

        css_decl_t d;
        if (!css_parse_decls(css + body_start, body_end - body_start, &d)) continue;

        uint32_t a = sel_start;
        while (a <= sel_end) {                                              /* comma separated selector list */
            uint32_t b = a;
            while (b < sel_end && css[b] != ',') b++;
            if (sh->n < CSS_MAX_RULES) {
                css_rule_t *r = &sh->rules[sh->n];
                if (parse_selector(css + a, (int)(b - a), r)) {
                    r->d = d;
                    r->order = sh->next_order++;
                    sh->n++;
                }
            }
            a = b + 1;
        }
    }
    sort_sheet(sh);
}

/* ---------- Matching ---------- */
static bool compound_match(const css_compound_t *c, const css_elem_t *e)
{
    if (c->tag && c->tag != e->tag) return false;
    if (c->id && c->id != e->id) return false;
    for (int k = 0; k < CSS_MAX_CLASSES; k++) {
        if (!c->cls[k]) continue;
        bool found = false;
        for (int j = 0; j < e->ncls; j++) {
            if (e->cls[j] == c->cls[k]) { found = true; break; }
        }
        if (!found) return false;
    }
    return true;
}

static bool match_chain(const css_rule_t *r, int ci, const css_elem_t *st, int pos)
{
    if (!compound_match(&r->chain[ci], &st[pos])) return false;
    if (ci == 0) return true;
    if (r->chain[ci].child) {
        return pos > 0 && match_chain(r, ci - 1, st, pos - 1);
    }
    for (int p = pos - 1; p >= 0; p--) {
        if (match_chain(r, ci - 1, st, p)) return true;
    }
    return false;
}

bool css_match(const css_rule_t *r, const css_elem_t *stack, int depth)
{
    if (depth <= 0 || r->n == 0) return false;
    return match_chain(r, r->n - 1, stack, depth - 1);
}
