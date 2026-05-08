/*
 * Orizon OS x86_64 - Minimal persistent block storage
 */

#ifndef _STORAGE_H
#define _STORAGE_H

#include "types.h"

/* Reserved by the deployment script after the 512 MiB EFI boot partition. */
#define ORIZON_PERSIST_LBA 1048576ULL
#define ORIZON_SECTOR_SIZE 512U

int storage_init(void);
int storage_available(void);
const char *storage_status(void);

int storage_read(uint64_t lba, void *buf, uint32_t sector_count);
int storage_write(uint64_t lba, const void *buf, uint32_t sector_count);

#endif /* _STORAGE_H */
