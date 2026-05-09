/*
 * Minimal ACPI table parsing (RSDP -> RSDT/XSDT -> MADT)
 */

#include "../include/acpi.h"
#include "../include/string.h"

#define ACPI_MAX_TABLE_LENGTH (1024U * 1024U)

extern uint64_t hhdm_offset;

typedef struct {
  uint8_t type;
  uint8_t length;
} __attribute__((packed)) acpi_madt_entry_t;

typedef struct {
  acpi_madt_entry_t header;
  uint16_t reserved;
  uint64_t lapic_address;
} __attribute__((packed)) acpi_madt_lapic_override_t;

static const acpi_madt_t *madt = NULL;
static uint64_t lapic_addr = 0xFEE00000ULL;
static int madt_found = 0;
static int lapic_override_found = 0;
static const char *acpi_status = "ACPI not initialized";

static const void *acpi_phys_to_virt(uint64_t address) {
  if (address == 0) {
    return NULL;
  }
  if (hhdm_offset && address < hhdm_offset) {
    return (const void *)(uintptr_t)(hhdm_offset + address);
  }
  return (const void *)(uintptr_t)address;
}

static int acpi_checksum_ok(const void *ptr, size_t length) {
  const uint8_t *p = (const uint8_t *)ptr;
  uint8_t sum = 0;

  if (!ptr || length == 0 || length > ACPI_MAX_TABLE_LENGTH) {
    return 0;
  }
  for (size_t i = 0; i < length; i++) {
    sum = (uint8_t)(sum + p[i]);
  }
  return sum == 0;
}

static int acpi_sdt_valid(const acpi_sdt_header_t *header,
                          const char signature[4]) {
  if (!header || header->length < sizeof(acpi_sdt_header_t) ||
      header->length > ACPI_MAX_TABLE_LENGTH) {
    return 0;
  }
  if (memcmp(header->signature, signature, 4) != 0) {
    return 0;
  }
  return acpi_checksum_ok(header, header->length);
}

static void acpi_parse_madt_entries(const acpi_madt_t *table) {
  const uint8_t *p;
  const uint8_t *end;

  if (!table || table->header.length < sizeof(acpi_madt_t)) {
    return;
  }

  lapic_addr = table->lapic_addr ? table->lapic_addr : 0xFEE00000ULL;
  p = (const uint8_t *)table + sizeof(acpi_madt_t);
  end = (const uint8_t *)table + table->header.length;
  while (p + sizeof(acpi_madt_entry_t) <= end) {
    const acpi_madt_entry_t *entry = (const acpi_madt_entry_t *)p;
    if (entry->length < sizeof(acpi_madt_entry_t) || p + entry->length > end) {
      break;
    }
    if (entry->type == 5 &&
        entry->length >= sizeof(acpi_madt_lapic_override_t)) {
      const acpi_madt_lapic_override_t *override =
          (const acpi_madt_lapic_override_t *)entry;
      lapic_addr = override->lapic_address;
      lapic_override_found = 1;
    }
    p += entry->length;
  }
}

static int acpi_find_madt_xsdt(uint64_t xsdt_phys) {
  const acpi_sdt_header_t *xsdt =
      (const acpi_sdt_header_t *)acpi_phys_to_virt(xsdt_phys);
  uint64_t entries;
  const uint64_t *table_entries;

  if (!acpi_sdt_valid(xsdt, "XSDT")) {
    return -1;
  }
  entries = (xsdt->length - sizeof(acpi_sdt_header_t)) / sizeof(uint64_t);
  table_entries = (const uint64_t *)((const uint8_t *)xsdt +
                                     sizeof(acpi_sdt_header_t));
  for (uint64_t i = 0; i < entries; i++) {
    const acpi_sdt_header_t *header =
        (const acpi_sdt_header_t *)acpi_phys_to_virt(table_entries[i]);
    if (acpi_sdt_valid(header, "APIC")) {
      madt = (const acpi_madt_t *)header;
      return 0;
    }
  }
  return -1;
}

static int acpi_find_madt_rsdt(uint32_t rsdt_phys) {
  const acpi_sdt_header_t *rsdt =
      (const acpi_sdt_header_t *)acpi_phys_to_virt(rsdt_phys);
  uint32_t entries;
  const uint32_t *table_entries;

  if (!acpi_sdt_valid(rsdt, "RSDT")) {
    return -1;
  }
  entries = (rsdt->length - sizeof(acpi_sdt_header_t)) / sizeof(uint32_t);
  table_entries = (const uint32_t *)((const uint8_t *)rsdt +
                                     sizeof(acpi_sdt_header_t));
  for (uint32_t i = 0; i < entries; i++) {
    const acpi_sdt_header_t *header =
        (const acpi_sdt_header_t *)acpi_phys_to_virt(table_entries[i]);
    if (acpi_sdt_valid(header, "APIC")) {
      madt = (const acpi_madt_t *)header;
      return 0;
    }
  }
  return -1;
}

void acpi_init(void *rsdp_ptr) {
  const acpi_rsdp_t *rsdp;

  madt = NULL;
  madt_found = 0;
  lapic_override_found = 0;
  lapic_addr = 0xFEE00000ULL;

  if (!rsdp_ptr) {
    acpi_status = "ACPI RSDP missing";
    return;
  }

  rsdp = (const acpi_rsdp_t *)acpi_phys_to_virt((uint64_t)(uintptr_t)rsdp_ptr);
  if (!rsdp || memcmp(rsdp->signature, "RSD PTR ", 8) != 0 ||
      !acpi_checksum_ok(rsdp, 20)) {
    acpi_status = "ACPI RSDP invalid";
    return;
  }

  if (rsdp->revision >= 2 && rsdp->length >= sizeof(acpi_rsdp_t) &&
      acpi_checksum_ok(rsdp, rsdp->length) && rsdp->xsdt_address &&
      acpi_find_madt_xsdt(rsdp->xsdt_address) == 0) {
    madt_found = 1;
  } else if (rsdp->rsdt_address &&
             acpi_find_madt_rsdt(rsdp->rsdt_address) == 0) {
    madt_found = 1;
  }

  if (!madt_found) {
    acpi_status = "ACPI MADT missing";
    return;
  }

  acpi_parse_madt_entries(madt);
  acpi_status = lapic_override_found ? "ACPI MADT ready, LAPIC override"
                                     : "ACPI MADT ready";
}

const acpi_madt_t *acpi_get_madt(void) {
  return madt;
}

uint64_t acpi_lapic_address(void) {
  return lapic_addr;
}

int acpi_has_madt(void) {
  return madt_found;
}

void acpi_format_status(char *buf, size_t size) {
  if (!buf || size == 0) {
    return;
  }
  snprintf(buf, size, "%s lapic=0x%lx", acpi_status,
           (unsigned long)lapic_addr);
}
