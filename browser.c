/* browser.c - Nino Browser. Type a URL and press Enter; click links, form fields and buttons.
 * Pages: http:// and https://, file:/path (files in NinoOS), about:home.
 * Supports cookies, HTML forms (GET/POST, so you can log in to simple sites), a CSS subset, downloads
 * (non-text responses are saved to /downloads) and meta refresh. No JavaScript, no images yet. */
#include "browser.h"
#include "gui.h"
#include "gfx.h"
#include "fs.h"
#include "html.h"
#include "http.h"
#include "net.h"
#include "tls.h"
#include "cookies.h"
#include "download.h"
#include "heap.h"
#include "string.h"
#include "widgets.h"

#define ROW_H        16
#define LEFT_PAD     4
#define HIST_MAX     8
#define URL_MAX      256

static page_t page;
static bool   page_ok;
static bool   started;
static bool   use_css = true;
static bool   page_secure;
static int    scroll_row;
static char   url_buf[URL_MAX];
static int    url_len;
static char   cur_url[URL_MAX];
static char   history[HIST_MAX][URL_MAX];
static int    hist_n;
static char   status[110] = "Type a URL and press Enter";
static char   win_title[96] = "Browser";
static int    refresh_depth;

static const char home_html[] =
    "<html><head><title>NinoOS Browser</title></head><body>"
    "<h1>Welcome to Nino Browser</h1>"
    "<p>A small browser for NinoOS: text, links, colors, forms, cookies and downloads. "
    "There is no JavaScript and there are no images yet, so complex sites will look broken.</p>"
    "<h2>Pages to try</h2>"
    "<ul>"
    "<li><a href=\"http://example.com/\">example.com</a> (http)</li>"
    "<li><a href=\"https://en.wikipedia.org/wiki/Capybara\">Wikipedia: Capybara</a></li>"
    "<li><a href=\"https://news.ycombinator.com/\">Hacker News</a> - has a plain login form</li>"
    "<li><a href=\"https://lite.duckduckgo.com/lite/\">DuckDuckGo Lite</a> - search</li>"
    "<li><a href=\"https://text.npr.org/\">text.npr.org</a> - news without clutter</li>"
    "<li><a href=\"file:/docs/ideas.txt\">file:/docs/ideas.txt</a> - a file on this computer</li>"
    "</ul>"
    "<h2>How it works</h2>"
    "<ul><li>Click a text field, type, press Enter to send the form. Click a [ button ] to press it.</li>"
    "<li>Passwords are only sent over https. Cookies are kept (terminal: <b>cookies</b>).</li>"
    "<li>A link to a file (zip, pdf ...) is saved in /downloads. The CSS button turns style sheets on or off.</li>"
    "<li>Use a throw-away account: NinoOS is a hobby system, not a safe place for real passwords.</li></ul>"
    "</body></html>";

static void set_status(const char *s)
{
    int i = 0;
    while (s[i] && i < (int)sizeof(status) - 1) {
        status[i] = s[i];
        i++;
    }
    status[i] = '\0';
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

static bool starts(const char *s, const char *p)
{
    while (*p) {
        if (*s++ != *p++) return false;
    }
    return true;
}

static bool contains_ci(const char *s, const char *needle)
{
    int nl = (int)strlen(needle);
    for (; *s; s++) {
        int i = 0;
        while (i < nl && s[i]) {
            char a = s[i];
            if (a >= 'A' && a <= 'Z') a = (char)(a + 32);
            if (a != needle[i]) break;
            i++;
        }
        if (i == nl) return true;
    }
    return false;
}

static bool ends_ci(const char *s, const char *suffix)
{
    int sl = (int)strlen(s), xl = (int)strlen(suffix);
    if (sl < xl) return false;
    for (int i = 0; i < xl; i++) {
        char a = s[sl - xl + i];
        if (a >= 'A' && a <= 'Z') a = (char)(a + 32);
        if (a != suffix[i]) return false;
    }
    return true;
}

static bool has_scheme(const char *s)
{
    while (*s && *s != '/' && *s != '.' && *s != ':') s++;
    return *s == ':';
}

static void render_text(const char *url, const char *text)
{
    html_render(&page, url, (const uint8_t *)text, (uint32_t)strlen(text), false, 0, 0);
    scroll_row = 0;
}

static void set_title(void)
{
    if (page.title[0]) {
        char *p = win_title;
        const char *pre = "Browser - ";
        while (*pre) *p++ = *pre++;
        for (int i = 0; page.title[i] && p < win_title + sizeof(win_title) - 1; i++) *p++ = page.title[i];
        *p = '\0';
    } else {
        copy_str(win_title, sizeof(win_title), "Browser");
    }
    gui_set_title(WIN_BROWSER, win_title);
}

static void load_home(void)
{
    copy_str(cur_url, URL_MAX, "about:home");
    copy_str(url_buf, URL_MAX, "about:home");
    url_len = (int)strlen(url_buf);
    page_secure = false;
    html_render(&page, "http://home/", (const uint8_t *)home_html, (uint32_t)strlen(home_html), true, 0, 0);
    scroll_row = 0;
    set_title();
    set_status("Home");
}

static void push_history(void)
{
    if (!cur_url[0]) return;
    if (hist_n == HIST_MAX) {
        for (int i = 1; i < HIST_MAX; i++) memcpy(history[i - 1], history[i], URL_MAX);
        hist_n--;
    }
    copy_str(history[hist_n++], URL_MAX, cur_url);
}

/* Fetches the style sheets of the page (if any) and renders again with them */
static void apply_stylesheets(const http_result_t *res, bool is_html)
{
    if (!use_css || page.ncss == 0) return;
    char links[HTML_MAX_CSS][HTML_URL_MAX];
    int n = page.ncss;
    for (int i = 0; i < n; i++) copy_str(links[i], HTML_URL_MAX, page.css_links[i]);

    const uint32_t cap = 400 * 1024;
    char *css = kmalloc(cap);
    if (!css) return;
    uint32_t used = 0;
    for (int i = 0; i < n && i < 3; i++) {
        set_status("Loading style sheets...");
        gui_redraw();
        http_result_t cr;
        char err[64];
        http_opts_t o = { 0, 0, cur_url, 160 * 1024 };
        int st = http_request(links[i], &o, &cr, err, sizeof(err));
        if (st == 200 && cr.size && used + cr.size + 1 < cap) {
            memcpy(css + used, cr.data, cr.size);
            used += cr.size;
            css[used++] = '\n';
        }
        if (st >= 0) kfree(cr.data);
    }
    if (used) {
        uint32_t len = res->size > 1024u * 1024u ? 1024u * 1024u : res->size;
        html_render(&page, cur_url, res->data, len, is_html, css, used);
    }
    kfree(css);
}

static void load_request(const char *url_in, const char *post_body, bool push)
{
    char url[URL_MAX];
    copy_str(url, URL_MAX, url_in);
    if (!url[0] || !page_ok) return;
    if (push && cur_url[0]) push_history();
    page.focus_field = 0;

    if (strcmp(url, "about:home") == 0) {
        load_home();
        return;
    }

    copy_str(url_buf, URL_MAX, url);
    url_len = (int)strlen(url_buf);

    if (starts(url, "file:")) {                          /* a file inside NinoOS */
        const char *path = url + 5;
        while (path[0] == '/' && path[1] == '/') path++;
        fs_node_t *f = fs_resolve(fs_root(), path);
        page_secure = false;
        if (!f || f->type != FS_FILE) {
            render_text(url, "File not found.");
            set_status("file: not found");
        } else {
            bool html = ends_ci(path, ".html") || ends_ci(path, ".htm");
            uint32_t len = f->size > 1024u * 1024u ? 1024u * 1024u : f->size;
            html_render(&page, url, f->data, len, html, 0, 0);
            scroll_row = 0;
            set_status("Opened file");
        }
        copy_str(cur_url, URL_MAX, url);
        set_title();
        return;
    }

    if (!starts(url, "http://") && !starts(url, "https://") && has_scheme(url)) {
        render_text(url, "Unsupported address. Use http://, https://, file:/ or about:home.");
        set_status("Unsupported URL");
        copy_str(cur_url, URL_MAX, url);
        return;
    }

    set_status(post_body ? "Sending form... (the screen freezes until the answer is here)"
                         : "Loading... (the screen freezes until the page is here)");
    gui_redraw();

    http_result_t res;
    char err[96];
    http_opts_t o;
    o.post_data = post_body;
    o.post_len = post_body ? (uint32_t)strlen(post_body) : 0;
    o.referer = cur_url[0] && cur_url[0] != 'a' && cur_url[0] != 'f' ? cur_url : 0;
    o.max_body = HTTP_MAX_DOWNLOAD;
    int st = http_request(url, &o, &res, err, sizeof(err));
    copy_str(cur_url, URL_MAX, url);

    if (st < 0) {
        page_secure = false;
        render_text(url, err);
        page.title[0] = '\0';
        set_status(err);
        set_title();
        return;
    }

    copy_str(cur_url, URL_MAX, res.final_url);
    copy_str(url_buf, URL_MAX, res.final_url);
    url_len = (int)strlen(url_buf);
    page_secure = res.secure != 0;

    bool html = contains_ci(res.content_type, "html") ||
                (res.content_type[0] == '\0' && res.size && res.data[0] == '<');
    bool text = html || res.content_type[0] == '\0' || contains_ci(res.content_type, "text/") ||
                contains_ci(res.content_type, "json") || contains_ci(res.content_type, "xml");
    bool attachment = res.filename[0] != '\0' && !html;

    char msg[110];
    if (!text || attachment) {
        char saved[40];
        if (st >= 200 && st < 300 && download_save(&res, 0, saved, sizeof(saved))) {
            char *p = msg;
            const char *a = "Downloaded to /downloads/";
            while (*a) *p++ = *a++;
            for (const char *q = saved; *q; q++) *p++ = *q;
            *p = '\0';
            char body[200];
            char *b = body;
            const char *h = "Download complete.\n\nSaved as /downloads/";
            while (*h) *b++ = *h++;
            for (const char *q = saved; *q; q++) *b++ = *q;
            *b = '\0';
            render_text(cur_url, body);
            set_status(msg);
        } else {
            render_text(cur_url, "This is not a text page and it could not be saved.");
            set_status("Download failed");
        }
        page.title[0] = '\0';
        kfree(res.data);
        set_title();
        return;
    }

    uint32_t len = res.size > 1024u * 1024u ? 1024u * 1024u : res.size;
    html_render(&page, cur_url, res.data, len, html, 0, 0);
    scroll_row = 0;
    if (html) {
        apply_stylesheets(&res, html);
    }

    char *p = msg;
    const char *a = page_secure ? "[HTTPS] HTTP " : "[not secure] HTTP ";
    while (*a) *p++ = *a++;
    char num[8];
    int n = 0, v = st;
    do { num[n++] = (char)('0' + v % 10); v /= 10; } while (v && n < 7);
    while (n) *p++ = num[--n];
    const char *b = res.truncated ? " (page cut)" : (st >= 400 ? " (error page)" : " OK");
    while (*b) *p++ = *b++;
    *p = '\0';
    set_status(msg);
    kfree(res.data);
    set_title();

    if (page.refresh[0] && refresh_depth < 3) {          /* <meta http-equiv=refresh> */
        char next[URL_MAX];
        copy_str(next, URL_MAX, page.refresh);
        refresh_depth++;
        load_request(next, 0, false);
        refresh_depth--;
    }
}

static void submit_form(int form, int submit_id)
{
    if (form < 0 || form >= page.nforms) return;
    char action[HTML_URL_MAX];
    copy_str(action, HTML_URL_MAX, page.forms[form].action);
    bool post = page.forms[form].post != 0;

    bool has_password = false;
    for (int i = 0; i < page.nfields; i++) {
        if (page.fields[i].form == form && page.fields[i].type == FT_PASSWORD && page.fields[i].value[0]) {
            has_password = true;
        }
    }
    if (has_password && !starts(action, "https://")) {
        set_status("Blocked: NinoOS never sends a password over plain http://");
        return;
    }

    char query[1500];
    html_build_query(&page, form, submit_id, query, sizeof(query));
    if (post) {
        load_request(action, query, true);
    } else {
        char target[URL_MAX];
        int n = 0;
        for (const char *q = action; *q && *q != '?' && *q != '#' && n < URL_MAX - 2; q++) target[n++] = *q;
        target[n++] = '?';
        for (const char *q = query; *q && n < URL_MAX - 1; q++) target[n++] = *q;
        target[n] = '\0';
        load_request(target, 0, true);
    }
}

void browser_init(void)
{
    page_ok = html_page_init(&page);
    cookies_load();
}

void browser_open(const char *url)
{
    gui_open_window(WIN_BROWSER);
    if (!started) {
        started = true;
        load_home();
    }
    if (url && url[0]) {
        char full[URL_MAX];
        if (starts(url, "http://") || starts(url, "https://") || starts(url, "file:") || starts(url, "about:")) {
            copy_str(full, URL_MAX, url);
        } else {
            char *p = full;
            const char *h = "https://";
            while (*h) *p++ = *h++;
            copy_str(p, URL_MAX - 8, url);
        }
        load_request(full, 0, true);
    }
}

#define BACK_X   4
#define BACK_W   56
#define CSS_X    (BACK_X + BACK_W + 4)
#define CSS_W    72
#define GO_W     48

static uint32_t c565(uint16_t c)
{
    uint32_t r = ((uint32_t)(c >> 11) & 31) * 255 / 31;
    uint32_t g = ((uint32_t)(c >> 5) & 63) * 255 / 63;
    uint32_t b = ((uint32_t)c & 31) * 255 / 31;
    return (r << 16) | (g << 8) | b;
}

void browser_draw(int cx, int cy, int cw, int ch)
{
    if (!started) {
        started = true;
        if (page_ok) load_home();
    }

    gfx_fill_rect(cx, cy, cw, 32, 0xE4E4E4);
    ui_button(cx + BACK_X, cy + 4, BACK_W, 24, "Back", false);
    ui_button(cx + CSS_X, cy + 4, CSS_W, 24, use_css ? "CSS:on" : "CSS:off", !use_css);
    ui_button(cx + cw - GO_W - 4, cy + 4, GO_W, 24, "Go", false);

    int bx = cx + CSS_X + CSS_W + 4;
    int bw = cw - (CSS_X + CSS_W + 4) - (GO_W + 8);
    gfx_fill_rect(bx, cy + 4, bw, 24, 0xFFFFFF);
    gfx_draw_rect(bx, cy + 4, bw, 24, page_secure ? 0x1E8A1E : 0x606060);
    int maxc = (bw - 8) / CHAR_W - 1;
    const char *shown = url_buf;
    if (url_len > maxc) shown = url_buf + (url_len - maxc);
    gfx_draw_text(bx + 4, cy + 8, shown, 0x000000, FONT_SCALE);
    if (page.focus_field == 0) {
        gfx_draw_text(bx + 4 + (int)strlen(shown) * CHAR_W, cy + 8, "_", 0x808080, FONT_SCALE);
    }

    int py = cy + 32;
    int ph = ch - 32 - 18;
    int rows = ph / ROW_H;
    gfx_fill_rect(cx, py, cw, ph, (page_ok && page.has_page_bg) ? page.page_bg : 0xFFFFFF);

    if (page_ok) {
        scroll_row = ui_clamp_scroll(scroll_row, page.rows, rows);
        for (int r = 0; r < rows && scroll_row + r < page.rows; r++) {
            const hcell_t *line = &page.cells[(scroll_row + r) * HTML_COLS];
            int y = py + r * ROW_H;
            for (int c = 0; c < HTML_COLS; c++) {
                const hcell_t *cell = &line[c];
                if (!cell->ch) continue;
                int x = cx + LEFT_PAD + c * CHAR_W;
                if (cell->flags & HF_BG) {
                    gfx_fill_rect(x, y, CHAR_W, ROW_H, c565(cell->bg));
                }
                uint32_t fg = (cell->flags & HF_FG) ? c565(cell->fg) : 0x202020;
                if (cell->ch != ' ') {
                    gfx_draw_char(x, y, cell->ch, fg, FONT_SCALE);
                    if (cell->flags & HF_BOLD) {
                        gfx_draw_char(x + 1, y, cell->ch, fg, FONT_SCALE);
                    }
                }
                if (cell->flags & HF_UNDERLINE) {
                    gfx_fill_rect(x, y + ROW_H - 2, CHAR_W, 1, fg);
                }
            }
        }
        ui_scrollbar(cx + cw - SCROLLBAR_W, py, ph, page.rows, rows, scroll_row);
    } else {
        gfx_draw_text(cx + 8, py + 8, "Out of memory", 0xC00000, FONT_SCALE);
    }

    gfx_fill_rect(cx, cy + ch - 18, cw, 18, 0xD0D0D0);
    gfx_draw_text(cx + 6, cy + ch - 13, status, 0x202020, 1);
}

void browser_click(int rx, int ry, bool dbl)
{
    (void)dbl;
    int cw = BROWSER_CLIENT_W;
    if (ry < 32) {
        if (page_ok) html_set_focus(&page, 0);
        if (ui_inside(rx, ry, BACK_X, 4, BACK_W, 24)) {
            if (hist_n > 0) {
                char prev[URL_MAX];
                copy_str(prev, URL_MAX, history[--hist_n]);
                load_request(prev, 0, false);
            }
        } else if (ui_inside(rx, ry, CSS_X, 4, CSS_W, 24)) {
            use_css = !use_css;
            if (cur_url[0] && strcmp(cur_url, "about:home") != 0) {
                char again[URL_MAX];
                copy_str(again, URL_MAX, cur_url);
                load_request(again, 0, false);
            }
        } else if (ui_inside(rx, ry, cw - GO_W - 4, 4, GO_W, 24)) {
            load_request(url_buf, 0, true);
        }
        return;
    }
    if (!page_ok) return;

    int col = (rx - LEFT_PAD) / CHAR_W;
    int row = (ry - 32) / ROW_H + scroll_row;
    if (rx < LEFT_PAD || col < 0 || col >= HTML_COLS || row < 0 || row >= page.rows) {
        html_set_focus(&page, 0);
        return;
    }
    const hcell_t *cell = &page.cells[row * HTML_COLS + col];

    if (cell->field) {
        int id = cell->field;
        const field_t *f = &page.fields[id - 1];
        switch (f->type) {
        case FT_TEXT: case FT_PASSWORD: case FT_TEXTAREA:
            html_set_focus(&page, id);
            set_status("Type to fill in the field. Enter sends the form.");
            break;
        case FT_CHECKBOX: case FT_RADIO:
            html_set_focus(&page, 0);
            html_toggle(&page, id);
            break;
        case FT_SUBMIT:
            html_set_focus(&page, 0);
            submit_form(f->form, id);
            break;
        default:
            html_set_focus(&page, 0);
            break;
        }
        return;
    }
    html_set_focus(&page, 0);
    if (cell->link) {
        char target[URL_MAX];
        copy_str(target, URL_MAX, page.links[cell->link - 1]);   /* copy: loading rebuilds the page */
        load_request(target, 0, true);
    }
}

static void focus_next_field(void)
{
    int start = page.focus_field;                         /* 1-based, 0 = none */
    for (int k = 1; k <= page.nfields; k++) {
        int id = (start + k - 1) % page.nfields + 1;
        int t = page.fields[id - 1].type;
        if (page.fields[id - 1].placed && (t == FT_TEXT || t == FT_PASSWORD || t == FT_TEXTAREA)) {
            html_set_focus(&page, id);
            return;
        }
    }
}

void browser_key(char c)
{
    if (page_ok && page.focus_field > 0) {                /* typing into a form field */
        int id = page.focus_field;
        field_t *f = &page.fields[id - 1];
        int n = (int)strlen(f->value);
        if (c == '\n') {
            if (f->type == FT_TEXTAREA) {
                if (n < HTML_FIELD_VALUE - 1) { f->value[n] = '\n'; f->value[n + 1] = '\0'; }
            } else {
                int submit = 0;                           /* the form's first submit button */
                for (int i = 0; i < page.nfields; i++) {
                    if (page.fields[i].form == f->form && page.fields[i].type == FT_SUBMIT) { submit = i + 1; break; }
                }
                submit_form(f->form, submit);
                return;
            }
        } else if (c == '\b') {
            if (n > 0) f->value[n - 1] = '\0';
        } else if (c == '\t') {
            focus_next_field();
            return;
        } else if (c >= 32 && c < 127 && n < HTML_FIELD_VALUE - 1) {
            f->value[n] = c;
            f->value[n + 1] = '\0';
        }
        html_field_changed(&page, id);
        return;
    }

    if (c == '\n') {                                      /* the URL bar */
        if (url_len > 0) {
            char u[URL_MAX];
            copy_str(u, URL_MAX, url_buf);
            if (!starts(u, "http://") && !starts(u, "https://") && !starts(u, "file:") && !starts(u, "about:")) {
                char full[URL_MAX];
                char *p = full;
                const char *h = "https://";
                while (*h) *p++ = *h++;
                copy_str(p, URL_MAX - 8, u);
                copy_str(u, URL_MAX, full);
            }
            load_request(u, 0, true);
        }
    } else if (c == '\b') {
        if (url_len > 0) url_buf[--url_len] = '\0';
    } else if (c >= 32 && c < 127 && url_len < URL_MAX - 1) {
        url_buf[url_len++] = c;
        url_buf[url_len] = '\0';
    }
}

void browser_scroll(int dir)
{
    scroll_row += dir * 3;
    if (scroll_row < 0) {
        scroll_row = 0;                    /* the upper limit is applied while drawing */
    }
}
