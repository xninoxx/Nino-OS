/* persist.h - saves the whole file system to the ATA disk and loads it at boot */
#ifndef PERSIST_H
#define PERSIST_H

#include <stdint.h>
#include <stdbool.h>

void     persist_init(void);          /* detect the disk; load the saved tree or write the default one */
bool     persist_save(void);          /* write the whole tree to disk */
bool     persist_present(void);
uint32_t persist_disk_sectors(void);
uint32_t persist_save_count(void);

#endif /* PERSIST_H */
