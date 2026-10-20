/* term.c - built-in terminal with a small command shell */
#include "term.h"
#include "gui.h"
#include "gfx.h"
#include "fs.h"
#include "heap.h"
#include "timer.h"
#include "util.h"
#include "sysinfo.h"
#include "io.h"
#include "string.h"
#include "persist.h"
#include "net.h"
#include "http.h"
#include "pci.h"
#include "browser.h"
#include "editor.h"
#include "power.h"
#include "keyboard.h"
#include "sysreport.h"
#include "download.h"
#include "cookies.h"
#include "tls.h"
#include "rtc.h"

#define CAT_LIMIT 1500

static char term[TERM_ROWS][TERM_COLS + 1];      /* each row is NUL-terminated */
static int  term_row, term_col;
static char line[TERM_COLS];
static int  line_len;
static fs_node_t *cwd;

/* ---------- Output ---------- */
static void term_clear(void)
{
    memset(term, 0, sizeof(term));
    term_row = 0;
    term_col = 0;
}

static void term_newline(void)
{
    term_col = 0;
    if (++term_row >= TERM_ROWS) {
        memmove(term[0], term[1], (TERM_ROWS - 1) * (TERM_COLS + 1));
        memset(term[TERM_ROWS - 1], 0, TERM_COLS + 1);
        term_row = TERM_ROWS - 1;
    }
}

static void term_putc(char c)
{
    if (c == '\n') {
        term_newline();
        return;
    }
    term[term_row][term_col++] = c;
    if (term_col >= TERM_COLS) {
        term_newline();
    }
}

static void term_puts(const char *s)
{
    while (*s) {
        term_putc(*s++);
    }
}

static void term_prompt(void)
{
    term_puts("> ");
    line_len = 0;
}

/* ---------- Commands ---------- */
static void cmd_help(void)
{
    term_puts("Commands:\n");
    term_puts("  help  clear  ver  echo  uptime  mem\n");
    term_puts("  pwd  ls  cd  cat  mkdir  touch  rm\n");
    term_puts("  write <file> <text>   save text\n");
    term_puts("  mv <path> <newname>   rename\n");
    term_puts("  cp <file> <newname>   copy\n");
    term_puts("  time  whoami  disk  sync\n");
    term_puts("  net  ping <host>  nslookup <host>\n");
    term_puts("  wget <url> [name]   download to /downloads\n");
    term_puts("  cookies [clear]  edit [file]\n");
    term_puts("  shutdown  keymap [us|de|dvorak]\n");
    term_puts("  bugreport  factoryreset yes\n");
    term_puts("  browser [url]  pci\n");
    term_puts("  fastfetch  capybara  draw  reboot\n");
}

static void cmd_uptime(void)
{
    char buf[32];
    put_uptime(buf, timer_ticks() / TIMER_HZ);
    term_puts("Uptime: ");
    term_puts(buf);
    term_putc('\n');
}

static void cmd_mem(void)
{
    char buf[16];
    term_puts("RAM:  ");
    put_uint(buf, (g_sysinfo.ram_kb + 1024) / 1024);
    term_puts(buf);
    term_puts(" MiB\n");

    term_puts("Heap: ");
    put_uint(buf, heap_used() / 1024);
    term_puts(buf);
    term_puts(" / ");
    put_uint(buf, heap_total() / 1024);
    term_puts(buf);
    term_puts(" KiB used\n");
}

static void cmd_pwd(void)
{
    char path[96];
    fs_path(cwd, path, sizeof(path));
    term_puts(path);
    term_putc('\n');
}

static void cmd_ls(const char *arg)
{
    fs_node_t *dir = *arg ? fs_resolve(cwd, arg) : cwd;
    if (!dir) {
        term_puts("ls: not found\n");
        return;
    }
    if (dir->type == FS_FILE) {
        term_puts(dir->name);
        term_putc('\n');
        return;
    }
    if (!dir->first_child) {
        term_puts("(empty)\n");
        return;
    }
    for (fs_node_t *n = dir->first_child; n; n = n->next_sibling) {
        term_puts(n->name);
        if (n->type == FS_DIR) {
            term_putc('/');
        }
        term_putc('\n');
    }
}

static void cmd_cd(const char *arg)
{
    fs_node_t *target = *arg ? fs_resolve(cwd, arg) : fs_resolve(fs_root(), "/home/nino");
    if (!target) {
        term_puts("cd: not found\n");
    } else if (target->type != FS_DIR) {
        term_puts("cd: not a directory\n");
    } else {
        cwd = target;
    }
}

static void cmd_cat(const char *arg)
{
    if (!*arg) {
        term_puts("usage: cat <file>\n");
        return;
    }
    fs_node_t *f = fs_resolve(cwd, arg);
    if (!f) {
        term_puts("cat: not found\n");
        return;
    }
    if (f->type != FS_FILE) {
        term_puts("cat: is a directory\n");
        return;
    }
    if (f->size == 0) {
        term_puts("(empty)\n");
        return;
    }

    uint32_t n = f->size > CAT_LIMIT ? CAT_LIMIT : f->size;
    for (uint32_t i = 0; i < n; i++) {
        char c = (char)f->data[i];
        if (c == '\n') {
            term_putc('\n');
        } else if (c == '\t') {
            term_putc(' ');
        } else if (c >= 32 && c <= 126) {
            term_putc(c);
        }
    }
    if (f->data[n - 1] != '\n') {
        term_putc('\n');
    }
    if (f->size > n) {
        term_puts("...\n");
    }
}

static void cmd_mkdir(const char *arg)
{
    if (!*arg) {
        term_puts("usage: mkdir <name>\n");
    } else if (fs_find_child(cwd, arg)) {
        term_puts("mkdir: already exists\n");
    } else if (!fs_create(cwd, arg, FS_DIR)) {
        term_puts("mkdir: invalid name\n");
    }
}

static void cmd_touch(const char *arg)
{
    if (!*arg) {
        term_puts("usage: touch <name>\n");
    } else if (!fs_find_child(cwd, arg) && !fs_create(cwd, arg, FS_FILE)) {
        term_puts("touch: invalid name\n");
    }
}

static void cmd_write(const char *name, const char *text)
{
    if (!*name) {
        term_puts("usage: write <file> <text>\n");
        return;
    }
    fs_node_t *f = fs_find_child(cwd, name);
    if (!f) {
        f = fs_create(cwd, name, FS_FILE);
    }
    if (!f || f->type != FS_FILE) {
        term_puts("write: cannot write there\n");
        return;
    }

    char buf[TERM_COLS + 2];
    uint32_t n = (uint32_t)strlen(text);
    if (n > TERM_COLS) {
        n = TERM_COLS;
    }
    memcpy(buf, text, n);
    buf[n++] = '\n';
    if (!fs_write(f, buf, n)) {
        term_puts("write: out of memory\n");
    }
}

static void cmd_rm(const char *arg)
{
    if (!*arg) {
        term_puts("usage: rm <path>\n");
        return;
    }
    fs_node_t *n = fs_resolve(cwd, arg);
    if (!n) {
        term_puts("rm: not found\n");
    } else if (!fs_remove(n)) {
        term_puts("rm: cannot remove (folder not empty?)\n");
    }
}

static void cmd_mv(const char *src, const char *dst)
{
    if (!*src || !*dst) {
        term_puts("usage: mv <path> <newname>\n");
        return;
    }
    fs_node_t *n = fs_resolve(cwd, src);
    if (!n) {
        term_puts("mv: not found\n");
    } else if (!fs_rename(n, dst)) {
        term_puts("mv: failed (bad name or name already exists)\n");
    }
}

static void cmd_cp(const char *src, const char *dst)
{
    if (!*src || !*dst) {
        term_puts("usage: cp <file> <newname>\n");
        return;
    }
    fs_node_t *n = fs_resolve(cwd, src);
    if (!n || n->type != FS_FILE) {
        term_puts("cp: not a file\n");
        return;
    }
    fs_node_t *c = fs_create(cwd, dst, FS_FILE);
    if (!c) {
        term_puts("cp: cannot create (bad name or exists)\n");
    } else if (n->size && !fs_write(c, n->data, n->size)) {
        term_puts("cp: out of memory\n");
    }
}

static void cmd_time(void)
{
    uint8_t h, m, sec;
    char buf[12];
    rtc_get_time(&h, &m, &sec);
    char *p = put_2digits(buf, h);
    *p++ = ':';
    p = put_2digits(p, m);
    *p++ = ':';
    put_2digits(p, sec);
    term_puts(buf);
    term_putc('\n');
}

static void cmd_disk(void)
{
    if (!persist_present()) {
        term_puts("No disk: files are lost on reboot.\n");
        term_puts("Start with 'make run' to attach disk.img.\n");
        return;
    }
    char buf[48];
    char *p = put_str(buf, "Disk: ");
    p = put_uint(p, persist_disk_sectors() / 2048);
    p = put_str(p, " MiB, saves this session: ");
    put_uint(p, persist_save_count());
    term_puts(buf);
    term_putc('\n');
}

static void cmd_sync(void)
{
    if (!persist_present()) {
        term_puts("sync: no disk attached\n");
    } else if (persist_save()) {
        term_puts("Saved to disk.\n");
    } else {
        term_puts("sync: write failed (too much data?)\n");
    }
}

static void print_ip_line(const char *label, uint32_t ip)
{
    char buf[40];
    char *p = put_str(buf, label);
    net_ip_str(p, ip);
    term_puts(buf);
    term_putc('\n');
}

static bool need_net(void)
{
    if (net_init()) {
        return true;
    }
    term_puts("No network card found.\n");
    term_puts("Start QEMU with 'make run' (adds an rtl8139 card).\n");
    return false;
}

static void cmd_net(void)
{
    if (!need_net()) {
        return;
    }
    const uint8_t *m = net_mac();
    char buf[48];
    char *p = put_str(buf, "MAC:     ");
    static const char hex[] = "0123456789abcdef";
    for (int i = 0; i < 6; i++) {
        *p++ = hex[m[i] >> 4];
        *p++ = hex[m[i] & 15];
        if (i < 5) *p++ = ':';
    }
    *p = '\0';
    term_puts(buf);
    term_putc('\n');
    print_ip_line("IP:      ", net_ip());
    print_ip_line("Gateway: ", net_gateway());
    print_ip_line("DNS:     ", net_dns_server());
    term_puts(net_used_dhcp() ? "Address from DHCP.\n" : "DHCP failed: QEMU default addresses used.\n");
    term_puts(tls_status());
    term_putc('\n');

    const net_stats_t *st = net_stats();
    char line[96];
    char *q = put_str(line, "Frames in: ");
    q = put_uint(q, st->rx_frames);
    q = put_str(q, "  NIC resets: ");
    q = put_uint(q, net_nic_resets());
    q = put_str(q, "  fragments dropped: ");
    put_uint(q, st->frag_drops);
    term_puts(line);
    term_putc('\n');
    q = put_str(line, "TCP bytes in: ");
    q = put_uint(q, st->tcp_rx_bytes);
    q = put_str(q, "  out: ");
    q = put_uint(q, st->tcp_tx_bytes);
    q = put_str(q, "  RST: ");
    q = put_uint(q, st->tcp_rst);
    term_puts(line);
    term_putc('\n');
    q = put_str(line, "out-of-order: ");
    q = put_uint(q, st->tcp_ooo);
    q = put_str(q, "  ring full: ");
    q = put_uint(q, st->tcp_ring_full);
    q = put_str(q, "  resent: ");
    put_uint(q, st->tcp_retx);
    term_puts(line);
    term_putc('\n');
}

static void cmd_ping(const char *host)
{
    if (!*host) {
        term_puts("usage: ping <host or ip>\n");
        return;
    }
    if (!need_net()) {
        return;
    }
    uint32_t ip;
    if (!net_resolve(host, &ip)) {
        term_puts("ping: cannot resolve host\n");
        return;
    }
    char ipbuf[16];
    net_ip_str(ipbuf, ip);
    for (int i = 0; i < 4; i++) {
        int ms = net_ping(ip, 2000);
        char buf[64];
        char *p = put_str(buf, "Reply from ");
        p = put_str(p, ipbuf);
        if (ms >= 0) {
            p = put_str(p, ": time=");
            p = put_uint(p, (uint32_t)ms);
            put_str(p, " ms");
        } else {
            put_str(p, ": no reply");
        }
        term_puts(buf);
        term_putc('\n');
    }
    term_puts("(QEMU's user network often drops pings to the internet; the gateway answers.)\n");
}

static void cmd_nslookup(const char *host)
{
    if (!*host) {
        term_puts("usage: nslookup <host>\n");
        return;
    }
    if (!need_net()) {
        return;
    }
    uint32_t ip;
    if (net_resolve(host, &ip)) {
        print_ip_line("Address: ", ip);
    } else {
        term_puts("nslookup: not found\n");
    }
}

static void cmd_wget(const char *url, const char *name_arg)
{
    if (!*url) {
        term_puts("usage: wget <url> [filename]\n");
        return;
    }
    if (!need_net()) {
        return;
    }
    term_puts("Downloading... (the screen freezes until it is done)\n");
    gui_redraw();

    http_result_t res;
    http_opts_t opts = { 0, 0, 0, HTTP_MAX_DOWNLOAD };
    char err[96];
    int st = http_request(url, &opts, &res, err, sizeof(err));
    if (st < 0) {
        term_puts("wget: ");
        term_puts(err);
        term_putc('\n');
        return;
    }
    if (st < 200 || st >= 300) {
        char buf[40];
        char *p = put_str(buf, "wget: server answered HTTP ");
        put_uint(p, (uint32_t)st);
        term_puts(buf);
        term_putc('\n');
        kfree(res.data);
        return;
    }

    char name[FS_NAME_MAX + 1];
    if (!download_save(&res, name_arg, name, sizeof(name))) {
        term_puts("wget: cannot save the file (out of memory?)\n");
        kfree(res.data);
        return;
    }
    char buf[96];
    char *p = put_str(buf, "Saved /downloads/");
    p = put_str(p, name);
    p = put_str(p, " (");
    p = put_uint(p, res.size);
    put_str(p, " bytes)");
    term_puts(buf);
    term_putc('\n');
    if (res.truncated) {
        term_puts("Warning: file was cut at 6 MB.\n");
    }
    kfree(res.data);
}

static void cmd_cookies(const char *arg)
{
    if (strcmp(arg, "clear") == 0) {
        cookies_clear();
        term_puts("All cookies deleted.\n");
        return;
    }
    int n = cookies_count();
    if (n == 0) {
        term_puts("No cookies. ('cookies clear' deletes all)\n");
        return;
    }
    for (int i = 0; i < n; i++) {
        const cookie_t *c = cookies_get(i);
        if (!c) break;
        char buf[110];
        char *p = put_str(buf, c->domain);
        p = put_str(p, c->expires ? "  " : "  (session) ");
        p = put_str(p, c->name);
        if (c->secure) p = put_str(p, "  secure");
        term_puts(buf);
        term_putc('\n');
    }
    term_puts("('cookies clear' deletes all. Values are not shown.)\n");
}

static void cmd_pci(void)
{
    pci_dev_t list[32];
    int n = pci_scan(list, 32);
    static const char hex[] = "0123456789abcdef";
    for (int i = 0; i < n; i++) {
        char buf[48];
        char *p = buf;
        uint16_t v = list[i].vendor, d = list[i].device;
        for (int s = 12; s >= 0; s -= 4) *p++ = hex[(v >> s) & 15];
        *p++ = ':';
        for (int s = 12; s >= 0; s -= 4) *p++ = hex[(d >> s) & 15];
        p = put_str(p, " class ");
        *p++ = hex[list[i].class_code >> 4];
        *p++ = hex[list[i].class_code & 15];
        *p++ = hex[list[i].subclass >> 4];
        *p++ = hex[list[i].subclass & 15];
        if (v == 0x10EC && d == 0x8139) p = put_str(p, "  RTL8139");
        *p = '\0';
        term_puts(buf);
        term_putc('\n');
    }
}

static void cmd_edit(const char *arg)
{
    if (!*arg) {
        editor_open(0);                      /* a new, empty file */
        return;
    }
    fs_node_t *n = fs_resolve(cwd, arg);
    if (!n) {
        n = fs_create(cwd, arg, FS_FILE);    /* edit a file that does not exist yet: create it */
        if (!n) {
            term_puts("edit: cannot create that file (bad name?)\n");
            return;
        }
    }
    if (n->type != FS_FILE) {
        term_puts("edit: that is a folder\n");
        return;
    }
    editor_open(n);
}

static void cmd_keymap(const char *arg)
{
    if (!*arg) {
        term_puts("Keyboard layout: ");
        term_puts(keyboard_layout());
        term_puts("\nChange it with: keymap us | de | dvorak\n");
        return;
    }
    if (keyboard_set_layout(arg)) {
        term_puts("Layout set to ");
        term_puts(keyboard_layout());
        term_putc('\n');
    } else {
        term_puts("Unknown layout. Choose: us, de, dvorak\n");
    }
}

static void cmd_bugreport(void)
{
    term_puts("Collecting information (starting the network card)...\n");
    gui_redraw();
    net_init();
    char report[900];
    sysreport_text(report, sizeof(report));
    term_puts(report);
    fs_node_t *dir = fs_resolve(fs_root(), "/home/nino");
    if (dir) {
        fs_node_t *f = fs_find_child(dir, "bugreport.txt");
        if (!f) f = fs_create(dir, "bugreport.txt", FS_FILE);
        if (f && fs_write(f, report, (uint32_t)strlen(report))) {
            term_puts("(also saved as /home/nino/bugreport.txt)\n");
        }
    }
}

static void cmd_factoryreset(const char *arg)
{
    if (strcmp(arg, "yes") != 0) {
        term_puts("This deletes ALL your files and cookies and restores the\n");
        term_puts("original folders. To do it, type:  factoryreset yes\n");
        return;
    }
    fs_init();
    cookies_clear();
    cwd = fs_resolve(fs_root(), "/home/nino");
    if (!cwd) cwd = fs_root();
    if (persist_present()) persist_save();
    term_puts("Done: NinoOS is back to its original state.\n");
}

static void cmd_reboot(void)
{
    power_reboot();
}

static void run_command(void)
{
    line[line_len] = '\0';

    if (!fs_is_valid(cwd)) {                 /* our folder may have been deleted elsewhere */
        cwd = fs_root();
    }

    char *p = line;
    while (*p == ' ') {
        p++;
    }
    if (*p == '\0') {
        return;
    }

    char *cmd = p;
    while (*p && *p != ' ') {
        p++;
    }
    if (*p) {
        *p++ = '\0';
    }
    while (*p == ' ') {
        p++;
    }

    /* echo needs the untouched rest of the line */
    if (strcmp(cmd, "echo") == 0) {
        term_puts(p);
        term_putc('\n');
        return;
    }

    /* Split the rest into first argument and the remainder */
    char *arg1 = p;
    while (*p && *p != ' ') {
        p++;
    }
    if (*p) {
        *p++ = '\0';
    }
    while (*p == ' ') {
        p++;
    }
    char *arg2 = p;

    if      (strcmp(cmd, "help") == 0)      cmd_help();
    else if (strcmp(cmd, "clear") == 0)     term_clear();
    else if (strcmp(cmd, "ver") == 0)       term_puts("NinoOS 0.7 on NinoKernel\n");
    else if (strcmp(cmd, "uptime") == 0)    cmd_uptime();
    else if (strcmp(cmd, "mem") == 0)       cmd_mem();
    else if (strcmp(cmd, "pwd") == 0)       cmd_pwd();
    else if (strcmp(cmd, "ls") == 0)        cmd_ls(arg1);
    else if (strcmp(cmd, "cd") == 0)        cmd_cd(arg1);
    else if (strcmp(cmd, "cat") == 0)       cmd_cat(arg1);
    else if (strcmp(cmd, "mkdir") == 0)     cmd_mkdir(arg1);
    else if (strcmp(cmd, "touch") == 0)     cmd_touch(arg1);
    else if (strcmp(cmd, "write") == 0)     cmd_write(arg1, arg2);
    else if (strcmp(cmd, "rm") == 0)        cmd_rm(arg1);
    else if (strcmp(cmd, "mv") == 0)        cmd_mv(arg1, arg2);
    else if (strcmp(cmd, "cp") == 0)        cmd_cp(arg1, arg2);
    else if (strcmp(cmd, "time") == 0)      cmd_time();
    else if (strcmp(cmd, "whoami") == 0)    term_puts("nino\n");
    else if (strcmp(cmd, "disk") == 0)      cmd_disk();
    else if (strcmp(cmd, "sync") == 0)      cmd_sync();
    else if (strcmp(cmd, "draw") == 0)      gui_open_window(WIN_DRAW);
    else if (strcmp(cmd, "net") == 0)       cmd_net();
    else if (strcmp(cmd, "ping") == 0)      cmd_ping(arg1);
    else if (strcmp(cmd, "nslookup") == 0)  cmd_nslookup(arg1);
    else if (strcmp(cmd, "wget") == 0)      cmd_wget(arg1, arg2);
    else if (strcmp(cmd, "pci") == 0)       cmd_pci();
    else if (strcmp(cmd, "cookies") == 0)   cmd_cookies(arg1);
    else if (strcmp(cmd, "browser") == 0)   browser_open(arg1);
    else if (strcmp(cmd, "fastfetch") == 0 || strcmp(cmd, "neofetch") == 0)
                                            gui_open_window(WIN_FASTFETCH);
    else if (strcmp(cmd, "capybara") == 0)  gui_open_window(WIN_CAPYBARA);
    else if (strcmp(cmd, "reboot") == 0)    cmd_reboot();
    else if (strcmp(cmd, "shutdown") == 0 || strcmp(cmd, "poweroff") == 0) power_off();
    else if (strcmp(cmd, "edit") == 0 || strcmp(cmd, "notepad") == 0)      cmd_edit(arg1);
    else if (strcmp(cmd, "keymap") == 0)    cmd_keymap(arg1);
    else if (strcmp(cmd, "bugreport") == 0) cmd_bugreport();
    else if (strcmp(cmd, "factoryreset") == 0) cmd_factoryreset(arg1);
    else {
        term_puts("Unknown command: ");
        term_puts(cmd);
        term_putc('\n');
    }
}

/* ---------- Public API ---------- */
void term_init(void)
{
    cwd = fs_resolve(fs_root(), "/home/nino");
    if (!cwd) {
        cwd = fs_root();
    }
    term_clear();
    term_puts("Welcome to NinoOS! Type 'help'.\n");
    term_puts("Double-click the icons on the right.\n");
    if (!persist_present()) {
        term_puts("NOTE: no disk found, so files are\n");
        term_puts("lost when you turn the computer off.\n");
    }
    term_puts("\n");
    term_prompt();
}

void term_key(char c)
{
    if (c == '\n') {
        term_putc('\n');
        run_command();
        term_prompt();
    } else if (c == '\b') {
        if (line_len > 0) {
            line_len--;
            term_col--;
            term[term_row][term_col] = '\0';
        }
    } else if (c >= 32 && c <= 126) {
        if (line_len < TERM_COLS - 3) {      /* keep the input on one row */
            line[line_len++] = c;
            term_putc(c);
        }
    }
}

void term_draw(int cx, int cy, int cw, int ch)
{
    gfx_fill_rect(cx, cy, cw, ch, COL_TERM_BG);
    for (int r = 0; r < TERM_ROWS; r++) {
        if (term[r][0]) {
            gfx_draw_text(cx + TERM_PAD, cy + TERM_PAD + r * CHAR_H, term[r], COL_TERM_FG, FONT_SCALE);
        }
    }
    /* Cursor: an underscore at the input position */
    gfx_fill_rect(cx + TERM_PAD + term_col * CHAR_W,
                  cy + TERM_PAD + term_row * CHAR_H + CHAR_H - 3,
                  CHAR_W, 3, COL_TERM_FG);
}