/*
 * Orizon OS x86_64 - Minimal persistent block storage
 */

#ifndef _STORAGE_H
#define _STORAGE_H

#include "types.h"

/* Reserved by the deployment script after the 512 MiB EFI boot partition. */
#define ORIZON_PERSIST_LBA 1048576ULL
#define ORIZON_SECTOR_SIZE 512U
#define ORIZON_STORAGE_MAX_DEVICES 8

typedef struct {
  int index;
  int selected;
  int writable;
  char name[24];
  char driver[16];
  char model[64];
  uint64_t sectors;
} storage_device_info_t;

int storage_init(void);
int storage_available(void);
const char *storage_status(void);
uint64_t storage_sector_count(void);
void storage_format_capacity(char *out, size_t out_size);
void storage_format_size(uint64_t sectors, char *out, size_t out_size);

int storage_device_count(void);
int storage_selected_device(void);
int storage_get_device(int index, storage_device_info_t *out);
int storage_select_device(int index);

int storage_read(uint64_t lba, void *buf, uint32_t sector_count);
int storage_write(uint64_t lba, const void *buf, uint32_t sector_count);

#endif /* _STORAGE_H */
