/* persist.c - serializes the in-memory file system to disk.
 *   sector 0     : header { magic, version, blob size, checksum }
 *   sector 1..N  : blob = recursive tree: type(1) namelen(1) name  [file: size(4) data | dir: count(4) children] */
#include "persist.h"
#include "ata.h"
#include "fs.h"
#include "heap.h"
#include "string.h"

#define MAGIC     0x4F4E494Eu            /* "NINO" */
#define VERSION   1u
#define MAX_BLOB  (12u * 1024u * 1024u)
#define MAX_DEPTH 16

typedef struct {
    uint32_t magic, version, size, checksum;
} header_t;

static uint32_t save_count;

static uint32_t checksum(const uint8_t *d, uint32_t n)
{
    uint32_t h = 2166136261u;
    for (uint32_t i = 0; i < n; i++) {
        h ^= d[i];
        h *= 16777619u;
    }
    return h;
}

/* ---------- Writing (out == 0 only measures) ---------- */
static uint32_t w8(uint8_t *o, uint32_t p, uint8_t v)
{
    if (o) {
        o[p] = v;
    }
    return p + 1;
}

static uint32_t w32(uint8_t *o, uint32_t p, uint32_t v)
{
    if (o) {
        o[p] = (uint8_t)v;
        o[p + 1] = (uint8_t)(v >> 8);
        o[p + 2] = (uint8_t)(v >> 16);
        o[p + 3] = (uint8_t)(v >> 24);
    }
    return p + 4;
}

static uint32_t wmem(uint8_t *o, uint32_t p, const void *s, uint32_t n)
{
    if (o && n) {
        memcpy(o + p, s, n);
    }
    return p + n;
}

static uint32_t ser_node(const fs_node_t *n, uint8_t *o, uint32_t p)
{
    uint32_t nl = (uint32_t)strlen(n->name);
    p = w8(o, p, (uint8_t)n->type);
    p = w8(o, p, (uint8_t)nl);
    p = wmem(o, p, n->name, nl);
    if (n->type == FS_FILE) {
        p = w32(o, p, n->size);
        p = wmem(o, p, n->data, n->size);
    } else {
        uint32_t cnt = 0;
        for (const fs_node_t *c = n->first_child; c; c = c->next_sibling) {
            cnt++;
        }
        p = w32(o, p, cnt);
        for (const fs_node_t *c = n->first_child; c; c = c->next_sibling) {
            p = ser_node(c, o, p);
        }
    }
    return p;
}

/* ---------- Reading ---------- */
typedef struct {
    const uint8_t *d;
    uint32_t len, pos;
    bool bad;
} rd_t;

static uint32_t r8(rd_t *r)
{
    if (r->pos + 1 > r->len) {
        r->bad = true;
        return 0;
    }
    return r->d[r->pos++];
}

static uint32_t r32(rd_t *r)
{
    if (r->pos + 4 > r->len) {
        r->bad = true;
        return 0;
    }
    uint32_t v = (uint32_t)r->d[r->pos] | ((uint32_t)r->d[r->pos + 1] << 8) |
                 ((uint32_t)r->d[r->pos + 2] << 16) | ((uint32_t)r->d[r->pos + 3] << 24);
    r->pos += 4;
    return v;
}

static const uint8_t *rmem(rd_t *r, uint32_t n)
{
    if (n > r->len || r->pos > r->len - n) {
        r->bad = true;
        return 0;
    }
    const uint8_t *p = r->d + r->pos;
    r->pos += n;
    return p;
}

static void load_children(rd_t *r, fs_node_t *dir, int depth)
{
    uint32_t count = r32(r);
    for (uint32_t i = 0; i < count && !r->bad; i++) {
        uint32_t type = r8(r);
        uint32_t nl = r8(r);
        const uint8_t *nm = rmem(r, nl);
        if (r->bad) {
            return;
        }
        if (nl > FS_NAME_MAX || (type != FS_FILE && type != FS_DIR)) {
            r->bad = true;
            return;
        }
        char name[FS_NAME_MAX + 1];
        memcpy(name, nm, nl);
        name[nl] = '\0';

        fs_node_t *n = fs_create(dir, name, (fs_type_t)type);
        if (!n) {
            r->bad = true;
            return;
        }
        if (type == FS_FILE) {
            uint32_t sz = r32(r);
            const uint8_t *data = rmem(r, sz);
            if (r->bad) {
                return;
            }
            if (sz && !fs_write(n, data, sz)) {
                r->bad = true;
                return;
            }
        } else {
            if (depth >= MAX_DEPTH) {
                r->bad = true;
                return;
            }
            load_children(r, n, depth + 1);
        }
    }
}

static bool persist_load(void)
{
    header_t h;
    uint8_t sec[512];
    if (!ata_read(0, 1, sec)) {
        return false;
    }
    memcpy(&h, sec, sizeof(h));
    if (h.magic != MAGIC || h.version != VERSION || h.size == 0 || h.size > MAX_BLOB) {
        return false;
    }
    uint32_t sectors = (h.size + 511) / 512;
    if (1 + sectors > ata_sectors()) {
        return false;
    }
    uint8_t *blob = kmalloc(sectors * 512);
    if (!blob) {
        return false;
    }
    if (!ata_read(1, sectors, blob) || checksum(blob, h.size) != h.checksum) {
        kfree(blob);
        return false;
    }

    rd_t r = { blob, h.size, 0, false };
    uint32_t type = r8(&r);
    uint32_t nl = r8(&r);
    bool ok = (type == FS_DIR && nl == 0);

    if (ok) {
        fs_wipe();                          /* replace the default tree with the saved one */
        load_children(&r, fs_root(), 0);
        ok = !r.bad;
        if (!ok) {
            fs_init();                      /* corrupt data: back to the defaults */
        }
    }
    kfree(blob);
    return ok;
}

/* ---------- Public API ---------- */
bool persist_save(void)
{
    if (!ata_present()) {
        return false;
    }
    uint32_t size = ser_node(fs_root(), 0, 0);
    uint32_t sectors = (size + 511) / 512;
    if (size > MAX_BLOB || 1 + sectors > ata_sectors()) {
        return false;
    }
    uint8_t *blob = kmalloc(sectors * 512);
    if (!blob) {
        return false;
    }
    memset(blob, 0, sectors * 512);
    ser_node(fs_root(), blob, 0);

    header_t h = { MAGIC, VERSION, size, checksum(blob, size) };
    uint8_t sec[512];
    memset(sec, 0, sizeof(sec));
    memcpy(sec, &h, sizeof(h));

    /* Data first, header last: an interrupted save leaves a header that fails the checksum */
    bool ok = ata_write(1, sectors, blob) && ata_write(0, 1, sec);
    kfree(blob);
    if (ok) {
        save_count++;
    }
    return ok;
}

void persist_init(void)
{
    if (!ata_init()) {
        return;
    }
    if (!persist_load()) {
        persist_save();                     /* blank or foreign disk: store the default tree */
    }
    (void)fs_changed();                     /* loading is not a user change */
}

bool persist_present(void)
{
    return ata_present();
}

uint32_t persist_disk_sectors(void)
{
    return ata_sectors();
}

uint32_t persist_save_count(void)
{
    return save_count;
}
