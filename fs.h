/* fs.h - in-memory file system (RAM disk): directories and files as a tree */
#ifndef FS_H
#define FS_H

#include <stdint.h>
#include <stdbool.h>

#define FS_NAME_MAX 31

typedef enum { FS_FILE, FS_DIR } fs_type_t;

typedef struct fs_node {
    char            name[FS_NAME_MAX + 1];
    fs_type_t       type;
    struct fs_node *parent;
    struct fs_node *first_child;
    struct fs_node *next_sibling;
    uint8_t        *data;          /* file contents (kmalloc'ed), 0 if empty */
    uint32_t        size;
} fs_node_t;

void       fs_init(void);                                   /* builds the initial tree */
fs_node_t *fs_root(void);
fs_node_t *fs_find_child(const fs_node_t *dir, const char *name);
fs_node_t *fs_resolve(fs_node_t *cwd, const char *path);    /* absolute or relative; handles . and .. */
fs_node_t *fs_create(fs_node_t *dir, const char *name, fs_type_t type);
bool       fs_write(fs_node_t *file, const void *data, uint32_t size);   /* replaces contents */
bool       fs_remove(fs_node_t *node);                      /* files and empty directories */
bool       fs_rename(fs_node_t *node, const char *new_name);   /* same folder only */
bool       fs_changed(void);                                /* true if anything changed since the last call */
void       fs_wipe(void);                                   /* frees everything, leaves an empty root */
void       fs_path(const fs_node_t *node, char *buf, int bufsize);
bool       fs_is_valid(const fs_node_t *node);              /* does this node still exist? */

#endif /* FS_H */