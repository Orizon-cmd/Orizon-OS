/*
 * Minimal ACPI table parsing (RSDP -> RSDT/XSDT -> MADT)
 */

#include "../include/acpi.h"

static const acpi_madt_t *madt = NULL;

void acpi_init(void *rsdp_ptr) {
  madt = NULL;
  if (!rsdp_ptr) return;

  /* For now, skip full ACPI parsing since RSDT/XSDT addresses are physical
   * and we don't have HHDM offset available here. This avoids page faults.
   * USB HID keyboard doesn't require ACPI MADT. */
  (void)rsdp_ptr;
  return;
}

const acpi_madt_t *acpi_get_madt(void) {
  return madt;
}
