/* fs.c - in-memory file system */
#include "fs.h"
#include "heap.h"
#include "string.h"

static fs_node_t *root;
static uint32_t   dirty;               /* bumped on every change, see fs_changed() */

fs_node_t *fs_root(void)
{
    return root;
}

fs_node_t *fs_find_child(const fs_node_t *dir, const char *name)
{
    if (!dir || dir->type != FS_DIR) {
        return 0;
    }
    for (fs_node_t *c = dir->first_child; c; c = c->next_sibling) {
        if (strcmp(c->name, name) == 0) {
            return c;
        }
    }
    return 0;
}

fs_node_t *fs_resolve(fs_node_t *cwd, const char *path)
{
    fs_node_t *cur = (path[0] == '/') ? root : cwd;

    while (*path) {
        while (*path == '/') {
            path++;
        }
        if (!*path) {
            break;
        }

        char comp[FS_NAME_MAX + 1];
        int n = 0;
        while (*path && *path != '/') {
            if (n >= FS_NAME_MAX) {
                return 0;
            }
            comp[n++] = *path++;
        }
        comp[n] = '\0';

        if (strcmp(comp, ".") == 0) {
            continue;
        }
        if (strcmp(comp, "..") == 0) {
            if (cur->parent) {
                cur = cur->parent;
            }
            continue;
        }
        cur = fs_find_child(cur, comp);
        if (!cur) {
            return 0;
        }
    }
    return cur;
}

fs_node_t *fs_create(fs_node_t *dir, const char *name, fs_type_t type)
{
    if (!dir || dir->type != FS_DIR || !name[0]) {
        return 0;
    }
    size_t len = strlen(name);
    if (len > FS_NAME_MAX) {
        return 0;
    }
    for (size_t i = 0; i < len; i++) {
        if (name[i] == '/') {
            return 0;
        }
    }
    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
        return 0;
    }
    if (fs_find_child(dir, name)) {
        return 0;                          /* already exists */
    }

    fs_node_t *n = kmalloc(sizeof(fs_node_t));
    if (!n) {
        return 0;
    }
    memset(n, 0, sizeof(*n));
    memcpy(n->name, name, len);
    n->type = type;
    n->parent = dir;

    /* Append at the end so the order stays stable */
    if (!dir->first_child) {
        dir->first_child = n;
    } else {
        fs_node_t *c = dir->first_child;
        while (c->next_sibling) {
            c = c->next_sibling;
        }
        c->next_sibling = n;
    }
    dirty++;
    return n;
}

bool fs_write(fs_node_t *file, const void *data, uint32_t size)
{
    if (!file || file->type != FS_FILE) {
        return false;
    }
    uint8_t *copy = 0;
    if (size > 0) {
        copy = kmalloc(size);
        if (!copy) {
            return false;
        }
        memcpy(copy, data, size);
    }
    kfree(file->data);
    file->data = copy;
    file->size = size;
    dirty++;
    return true;
}

bool fs_remove(fs_node_t *node)
{
    if (!node || node == root || !node->parent) {
        return false;
    }
    if (node->type == FS_DIR && node->first_child) {
        return false;                      /* directory not empty */
    }

    fs_node_t *parent = node->parent;
    if (parent->first_child == node) {
        parent->first_child = node->next_sibling;
    } else {
        fs_node_t *c = parent->first_child;
        while (c && c->next_sibling != node) {
            c = c->next_sibling;
        }
        if (c) {
            c->next_sibling = node->next_sibling;
        }
    }
    kfree(node->data);
    kfree(node);
    dirty++;
    return true;
}

static bool valid_name(const char *name)
{
    size_t len = strlen(name);
    if (len == 0 || len > FS_NAME_MAX) {
        return false;
    }
    for (size_t i = 0; i < len; i++) {
        if (name[i] == '/') {
            return false;
        }
    }
    return strcmp(name, ".") != 0 && strcmp(name, "..") != 0;
}

bool fs_rename(fs_node_t *node, const char *new_name)
{
    if (!node || node == root || !node->parent || !valid_name(new_name)) {
        return false;
    }
    if (strcmp(node->name, new_name) == 0) {
        return true;                       /* nothing to do */
    }
    if (fs_find_child(node->parent, new_name)) {
        return false;                      /* name already taken */
    }
    memset(node->name, 0, sizeof(node->name));
    memcpy(node->name, new_name, strlen(new_name));
    dirty++;
    return true;
}

bool fs_changed(void)
{
    bool d = dirty != 0;
    dirty = 0;
    return d;
}

static void free_tree(fs_node_t *n)
{
    fs_node_t *c = n->first_child;
    while (c) {
        fs_node_t *next = c->next_sibling;
        free_tree(c);
        c = next;
    }
    kfree(n->data);
    kfree(n);
}

void fs_wipe(void)
{
    if (root) {
        free_tree(root);
    }
    root = kmalloc(sizeof(fs_node_t));
    memset(root, 0, sizeof(*root));
    root->type = FS_DIR;                   /* the root has an empty name and no parent */
    dirty++;
}

void fs_path(const fs_node_t *node, char *buf, int bufsize)
{
    const fs_node_t *stack[16];
    int depth = 0;
    while (node && node->parent && depth < 16) {
        stack[depth++] = node;
        node = node->parent;
    }

    if (depth == 0) {
        buf[0] = '/';
        buf[1] = '\0';
        return;
    }

    int pos = 0;
    while (depth--) {
        if (pos < bufsize - 1) {
            buf[pos++] = '/';
        }
        for (const char *s = stack[depth]->name; *s && pos < bufsize - 1; s++) {
            buf[pos++] = *s;
        }
    }
    buf[pos] = '\0';
}

static bool contains(const fs_node_t *dir, const fs_node_t *target)
{
    for (const fs_node_t *c = dir->first_child; c; c = c->next_sibling) {
        if (c == target) {
            return true;
        }
        if (c->type == FS_DIR && contains(c, target)) {
            return true;
        }
    }
    return false;
}

bool fs_is_valid(const fs_node_t *node)
{
    return node && (node == root || contains(root, node));
}

static void add_file(fs_node_t *dir, const char *name, const char *text)
{
    fs_node_t *f = fs_create(dir, name, FS_FILE);
    if (f) {
        fs_write(f, text, (uint32_t)strlen(text));
    }
}

void fs_init(void)
{
    fs_wipe();

    fs_node_t *home  = fs_create(root, "home", FS_DIR);
    fs_node_t *nino  = fs_create(home, "nino", FS_DIR);
    fs_node_t *docs  = fs_create(root, "docs", FS_DIR);
    fs_node_t *etc   = fs_create(root, "etc", FS_DIR);
    fs_create(root, "downloads", FS_DIR);  /* for the future wget/browser */

    add_file(nino, "readme.txt",
             "Welcome to NinoOS!\n\n"
             "This is your home folder.\n"
             "Files are saved to disk.img, so they\n"
             "survive a reboot (start with make run).\n\n"
             "Try the terminal: help, ls, cat, write\n");
    add_file(nino, "todo.txt",
             "- draw something in Draw\n"
             "- add networking (phase 3)\n");
    add_file(docs, "ideas.txt",
             "The NinoOS logo is a capybara holding an N.\n");
    add_file(etc, "os-release",
             "NAME=NinoOS\nVERSION=0.7\nKERNEL=NinoKernel\n");
}