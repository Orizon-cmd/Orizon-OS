/*
 * Minimal xHCI bring-up + HID boot keyboard (polling)
 */

#include "../include/pci.h"
#include "../include/idt.h"
#include "../include/kmalloc.h"
#include "../include/usb.h"
#include "../include/mmio.h"
#include "../include/string.h"
#include "../include/gui.h"

#define XHCI_TRB_TYPE_NORMAL 1
#define XHCI_TRB_TYPE_SETUP 2
#define XHCI_TRB_TYPE_DATA 3
#define XHCI_TRB_TYPE_STATUS 4
#define XHCI_TRB_TYPE_LINK 6
#define XHCI_TRB_TYPE_ENABLE_SLOT 9
#define XHCI_TRB_TYPE_ADDRESS_DEVICE 11
#define XHCI_TRB_TYPE_CONFIGURE_ENDPOINT 12
#define XHCI_TRB_TYPE_EVALUATE_CONTEXT 13
#define XHCI_TRB_TYPE_TRANSFER_EVENT 32
#define XHCI_TRB_TYPE_CMD_COMPLETION 33
#define XHCI_TRB_TYPE_PORT_STATUS 34

#define XHCI_SPEED_FULL 1
#define XHCI_SPEED_LOW 2
#define XHCI_SPEED_HIGH 3
#define XHCI_SPEED_SUPER 4

#define XHCI_PORTSC_CCS (1U << 0)
#define XHCI_PORTSC_PED (1U << 1)
#define XHCI_PORTSC_PR (1U << 4)
#define XHCI_PORTSC_CSC (1U << 17)
#define XHCI_PORTSC_PRC (1U << 21)
#define XHCI_MAX_TRACKED_PORTS 64

typedef struct {
  uint32_t dword0;
  uint32_t dword1;
  uint32_t dword2;
  uint32_t dword3;
} xhci_trb_t;

typedef struct {
  uint64_t ring_base;
  uint32_t ring_size;
  uint32_t rsvd;
} xhci_erst_entry_t;

static int xhci_present = 0;
static uint64_t xhci_mmio = 0;
static uint8_t xhci_irq = 0xFF;
static volatile int xhci_irq_seen = 0;

static uint32_t xhci_op_base = 0;
static uint32_t xhci_rt_base = 0;
static uint32_t xhci_db_base = 0;
static int xhci_context_size = 32;
static uint32_t xhci_ports = 0;
static uint32_t xhci_max_slots = 1;
static uint32_t xhci_controller_count = 0;
static uint32_t xhci_controller_index = 0;
static uint8_t xhci_pci_bus = 0;
static uint8_t xhci_pci_device = 0;
static uint8_t xhci_pci_function = 0;
static uint16_t xhci_pci_vendor_id = 0;
static uint16_t xhci_pci_device_id = 0;
static uint32_t xhci_selected_preinit_connected = 0;
static uint8_t xhci_candidate_bus[8];
static uint8_t xhci_candidate_device[8];
static uint8_t xhci_candidate_function[8];
static uint16_t xhci_candidate_vendor_id[8];
static uint16_t xhci_candidate_device_id[8];
static uint32_t xhci_candidate_ports[8];
static uint32_t xhci_candidate_connected[8];
static unsigned long xhci_scan_count = 0;
static unsigned long xhci_scan_attempts = 0;
static uint8_t xhci_keyboard_ready = 0;
static uint8_t xhci_port_attempted[XHCI_MAX_TRACKED_PORTS];
static uint8_t xhci_port_connected[XHCI_MAX_TRACKED_PORTS];
static int8_t xhci_port_result[XHCI_MAX_TRACKED_PORTS];
static uint32_t xhci_portsc_snapshot[XHCI_MAX_TRACKED_PORTS];

static xhci_trb_t *cmd_ring = NULL;
static uint32_t cmd_ring_idx = 0;
static uint8_t cmd_cycle = 1;

static xhci_trb_t *event_ring = NULL;
static uint32_t event_ring_idx = 0;
static uint8_t event_cycle = 1;
static xhci_erst_entry_t *erst = NULL;

static uint32_t scratchpad_count = 0;
static uint64_t *scratchpad_array = NULL;
static void **scratchpad_bufs = NULL;

static uint64_t *dcbaa = NULL;
static void *dev_ctx = NULL;
static xhci_trb_t *ep0_ring = NULL;
static uint8_t ep0_cycle = 1;
static uint32_t ep0_idx = 0;

static xhci_trb_t *intr_ring = NULL;
static uint8_t intr_cycle = 1;
static uint32_t intr_idx = 0;
static uint8_t *intr_buf = NULL;
static uint16_t intr_mps = 8;
static uint8_t intr_ep = 1;
static uint8_t intr_interval = 10;
static uint8_t device_slot = 0;
static uint8_t device_address = 0;
static uint8_t interface_num = 0;
static uint8_t config_value = 1;

static uint32_t xhci_tracked_ports(void) {
  return xhci_ports < XHCI_MAX_TRACKED_PORTS ? xhci_ports : XHCI_MAX_TRACKED_PORTS;
}

static void usb_xhci_irq_handler(interrupt_frame_t *frame) {
  UNUSED(frame);
  xhci_irq_seen = 1;
}

static inline void xhci_write32(uint32_t off, uint32_t val) {
  volatile uint32_t *reg = (volatile uint32_t *)(uintptr_t)(xhci_mmio + off);
  *reg = val;
}

static inline uint32_t xhci_read32(uint32_t off) {
  volatile uint32_t *reg = (volatile uint32_t *)(uintptr_t)(xhci_mmio + off);
  return *reg;
}

static inline uint32_t xhci_read32_from(uint64_t base, uint32_t off) {
  volatile uint32_t *reg = (volatile uint32_t *)(uintptr_t)(base + off);
  return *reg;
}

static inline void xhci_write64(uint32_t off, uint64_t val) {
  /* Some controllers require low dword then high dword writes */
  xhci_write32(off + 0, (uint32_t)(val & 0xFFFFFFFFULL));
  xhci_write32(off + 4, (uint32_t)(val >> 32));
}

static void *xhci_alloc_aligned(size_t size, size_t align) {
  size_t total = size + align;
  uint8_t *raw = (uint8_t *)kzalloc(total);
  if (!raw) {
    return NULL;
  }
  uintptr_t addr = (uintptr_t)raw;
  uintptr_t aligned = (addr + (align - 1)) & ~(align - 1);
  return (void *)aligned;
}

static void xhci_delay(int loops) {
  for (volatile int i = 0; i < loops; i++) {
    __asm__ volatile("pause");
  }
}

static int xhci_wait_usbsts(uint32_t mask, uint32_t expect, int loops) {
  while (loops-- > 0) {
    uint32_t st = xhci_read32(xhci_op_base + 0x04);
    if ((st & mask) == expect) {
      return 0;
    }
    xhci_delay(2000);
  }
  return -1;
}

static inline uint64_t xhci_phys_addr(const void *ptr) {
  uint64_t v = (uint64_t)(uintptr_t)ptr;
  if (kernel_phys_base && kernel_virt_base && v >= kernel_virt_base) {
    return kernel_phys_base + (v - kernel_virt_base);
  }
  if (v >= hhdm_offset) {
    return v - hhdm_offset;
  }
  return v;
}

static uint64_t xhci_bar_phys(const pci_device_info_t *dev) {
  uint32_t bar0;
  uint64_t phys;

  if (!dev) {
    return 0;
  }
  bar0 = dev->bar[0];
  if ((bar0 & 0x01) != 0 || bar0 == 0 || bar0 == 0xFFFFFFFF) {
    return 0;
  }

  phys = (uint64_t)(bar0 & ~0x0FULL);
  if ((bar0 & 0x06) == 0x04) {
    uint32_t bar1 = dev->bar[1];
    if (bar1 != 0xFFFFFFFF) {
      phys |= ((uint64_t)bar1) << 32;
    }
  }
  if (phys == 0 || phys > 0x100000000000ULL) {
    return 0;
  }
  return phys;
}

static uint32_t xhci_probe_connected_ports(uint64_t mmio_base,
                                           uint32_t ports,
                                           uint8_t caplength) {
  uint32_t connected = 0;
  uint32_t tracked = ports < XHCI_MAX_TRACKED_PORTS ? ports : XHCI_MAX_TRACKED_PORTS;

  for (uint32_t port = 0; port < tracked; port++) {
    uint32_t portsc = xhci_read32_from(mmio_base,
                                       (uint32_t)caplength + 0x400 + (port * 0x10));
    if (portsc & XHCI_PORTSC_CCS) {
      connected++;
    }
  }
  return connected;
}

/* Request BIOS->OS ownership if legacy support is enabled */
static void xhci_legacy_handoff(void) {
  uint32_t hccparams1 = xhci_read32(0x10);
  uint32_t xecp = (hccparams1 >> 16) & 0xFFFF;
  if (xecp == 0) {
    return;
  }
  
  uint32_t xecp_offset = xecp * 4;
  while (xecp_offset && xecp_offset < 0x2000) {
    uint32_t cap = xhci_read32(xecp_offset);
    uint8_t cap_id = (uint8_t)(cap & 0xFF);
    uint8_t next = (uint8_t)((cap >> 8) & 0xFF);
    
    if (cap_id == 1) { /* USB Legacy Support */
      /* Force OS ownership */
      uint32_t val = xhci_read32(xecp_offset);
      val |= (1U << 24); /* Set OS Owned */
      xhci_write32(xecp_offset, val);
      
      /* Wait for BIOS to release (with timeout) */
      int timeout = 1000;
      while (timeout-- > 0) {
        val = xhci_read32(xecp_offset);
        if ((val & (1U << 16)) == 0) { /* BIOS cleared ownership */
          break;
        }
        xhci_delay(1000);
      }
      
      /* Aggressively disable ALL legacy SMI/IRQ */
      xhci_write32(xecp_offset + 0x04, 0); /* USBLEGCTLSTS = 0 */
      
      /* [2] YELLOW BLOCK - ownership taken */
      for (int y = 0; y < 100; y++) {
        for (int x = 0; x < 100; x++) {
          debug_rect(x, 100 + y, 1, 1, 0xFFFFFF00);
        }
      }
      return;
    }
    
    if (next == 0) {
      break;
    }
    xecp_offset += (uint32_t)next * 4;
  }
}

static void xhci_ring_doorbell(uint8_t slot, uint8_t target) {
  volatile uint32_t *db = (volatile uint32_t *)(uintptr_t)(xhci_mmio + xhci_db_base + (slot * 4));
  *db = target;
}

static void xhci_set_trb(xhci_trb_t *trb, uint64_t param, uint32_t status, uint32_t ctrl) {
  trb->dword0 = (uint32_t)(param & 0xFFFFFFFF);
  trb->dword1 = (uint32_t)((param >> 32) & 0xFFFFFFFF);
  trb->dword2 = status;
  trb->dword3 = ctrl;
}

static int xhci_get_port_speed(uint32_t portsc);

static uint32_t *xhci_ctx_ptr(void *base, uint32_t index) {
  return (uint32_t *)((uint8_t *)base + (index * (uint32_t)xhci_context_size));
}

static void xhci_ctx_zero(void *base, uint32_t index) {
  memset(xhci_ctx_ptr(base, index), 0, (size_t)xhci_context_size);
}

static void xhci_input_set_flags(void *input, uint32_t add_flags, uint32_t drop_flags) {
  uint32_t *ctrl = xhci_ctx_ptr(input, 0);
  ctrl[0] = drop_flags;
  ctrl[1] = add_flags;
}

static void xhci_setup_slot_ctx(void *input, uint32_t portsc, uint8_t port, uint32_t ctx_entries) {
  uint32_t *slot = xhci_ctx_ptr(input, 1);
  xhci_ctx_zero(input, 1);
  uint32_t speed = (uint32_t)xhci_get_port_speed(portsc);
  slot[0] = (speed << 20) | (ctx_entries << 27);
  slot[1] = (uint32_t)port << 16;
}

static void xhci_setup_ep_ctx(void *input, uint32_t index, uint8_t ep_type,
                              uint64_t ring, uint16_t mps, uint8_t interval) {
  uint32_t *ep = xhci_ctx_ptr(input, index);
  xhci_ctx_zero(input, index);
  ep[0] = ((uint32_t)interval << 16);
  ep[1] = ((uint32_t)ep_type << 3) | ((uint32_t)mps << 16);
  uint64_t ring_phys = xhci_phys_addr((void *)(uintptr_t)ring);
  ep[2] = (uint32_t)((ring_phys & ~0xF) | 1U);
  ep[3] = (uint32_t)(ring_phys >> 32);
  ep[4] = mps;
}

static void xhci_cmd_enqueue(uint64_t param, uint32_t status, uint32_t ctrl_type) {
  uint32_t ctrl = ctrl_type << 10;
  ctrl |= cmd_cycle;
  xhci_set_trb(&cmd_ring[cmd_ring_idx], param, status, ctrl);
  cmd_ring_idx++;
  if (cmd_ring_idx >= 255) {
    uint64_t ring_phys = xhci_phys_addr(cmd_ring);
    xhci_set_trb(&cmd_ring[cmd_ring_idx], ring_phys, 0,
                 (XHCI_TRB_TYPE_LINK << 10) | cmd_cycle | (1U << 1));
    cmd_ring_idx = 0;
    cmd_cycle ^= 1;
  }
  xhci_ring_doorbell(0, 0);
}

static int xhci_pop_event(xhci_trb_t *out_evt) {
  xhci_trb_t *evt = &event_ring[event_ring_idx];
  uint32_t cycle = evt->dword3 & 1U;
  if (cycle != event_cycle) {
    return -1;
  }
  if (out_evt) {
    *out_evt = *evt;
  }
  event_ring_idx++;
  if (event_ring_idx >= 256) {
    event_ring_idx = 0;
    event_cycle ^= 1;
  }
  uint64_t erdp = xhci_phys_addr(&event_ring[event_ring_idx]);
  xhci_write64(xhci_rt_base + 0x38, erdp | 0x1);
  return 0;
}

static int xhci_wait_for_cmd(xhci_trb_t *out_evt, int timeout) {
  while (timeout-- > 0) {
    xhci_trb_t evt;
    if (xhci_pop_event(&evt) == 0) {
      uint32_t type = (evt.dword3 >> 10) & 0x3F;
      if (type == XHCI_TRB_TYPE_CMD_COMPLETION) {
        if (out_evt) {
          *out_evt = evt;
        }
        return 0;
      }
    }
    xhci_delay(2000);
  }
  return -1;
}

static int xhci_wait_for_transfer(uint8_t ep_id, int timeout) {
  while (timeout-- > 0) {
    xhci_trb_t evt;
    if (xhci_pop_event(&evt) == 0) {
      uint32_t type = (evt.dword3 >> 10) & 0x3F;
      if (type == XHCI_TRB_TYPE_TRANSFER_EVENT) {
        uint8_t evt_ep = (uint8_t)((evt.dword3 >> 16) & 0x1F);
        uint8_t cc = (uint8_t)((evt.dword2 >> 24) & 0xFF);
        if (evt_ep == ep_id) {
          return (cc == 1) ? 0 : -1;
        }
      }
    }
    xhci_delay(2000);
  }
  return -1;
}

static int xhci_cmd_enable_slot(void) {
  xhci_trb_t evt;
  xhci_cmd_enqueue(0, 0, XHCI_TRB_TYPE_ENABLE_SLOT);
  if (xhci_wait_for_cmd(&evt, 10000) != 0) {
    return -1;
  }
  uint32_t type = (evt.dword3 >> 10) & 0x3F;
  if (type != XHCI_TRB_TYPE_CMD_COMPLETION) {
    return -1;
  }
  if (((evt.dword2 >> 24) & 0xFF) != 1) {
    return -1;
  }
  return (int)((evt.dword3 >> 24) & 0xFF);
}

static int xhci_cmd_address_device(uint8_t slot, void *input) {
  xhci_trb_t evt;
  uint64_t param = xhci_phys_addr(input);
  UNUSED(slot);
  xhci_cmd_enqueue(param, 0, XHCI_TRB_TYPE_ADDRESS_DEVICE);
  if (xhci_wait_for_cmd(&evt, 20000) != 0) {
    return -1;
  }
  uint32_t type = (evt.dword3 >> 10) & 0x3F;
  if (type != XHCI_TRB_TYPE_CMD_COMPLETION) {
    return -1;
  }
  if (((evt.dword2 >> 24) & 0xFF) != 1) {
    return -1;
  }
  device_address = slot;
  return 0;
}

static int xhci_cmd_evaluate_ctx(uint8_t slot, void *input) {
  xhci_trb_t evt;
  uint64_t param = xhci_phys_addr(input);
  UNUSED(slot);
  xhci_cmd_enqueue(param, 0, XHCI_TRB_TYPE_EVALUATE_CONTEXT);
  if (xhci_wait_for_cmd(&evt, 20000) != 0) {
    return -1;
  }
  uint32_t type = (evt.dword3 >> 10) & 0x3F;
  if (type != XHCI_TRB_TYPE_CMD_COMPLETION) {
    return -1;
  }
  if (((evt.dword2 >> 24) & 0xFF) != 1) {
    return -1;
  }
  return 0;
}

static int xhci_cmd_configure_ep(uint8_t slot, void *input) {
  xhci_trb_t evt;
  uint64_t param = xhci_phys_addr(input);
  UNUSED(slot);
  xhci_cmd_enqueue(param, 0, XHCI_TRB_TYPE_CONFIGURE_ENDPOINT);
  if (xhci_wait_for_cmd(&evt, 20000) != 0) {
    return -1;
  }
  uint32_t type = (evt.dword3 >> 10) & 0x3F;
  if (type != XHCI_TRB_TYPE_CMD_COMPLETION) {
    return -1;
  }
  if (((evt.dword2 >> 24) & 0xFF) != 1) {
    return -1;
  }
  return 0;
}

static int xhci_get_port_speed(uint32_t portsc) {
  return (int)((portsc >> 10) & 0x0F);
}

static void xhci_wait_port_ready(uint32_t port) {
  uint32_t portsc_off = xhci_op_base + 0x400 + (port * 0x10);
  int timeout = 50000;
  uint32_t val = xhci_read32(portsc_off);
  if ((val & XHCI_PORTSC_CCS) == 0) {
    return;
  }
  xhci_write32(portsc_off, val | XHCI_PORTSC_PR);
  while (timeout-- > 0) {
    val = xhci_read32(portsc_off);
    if ((val & XHCI_PORTSC_PR) == 0) {
      break;
    }
    xhci_delay(500);
  }
}

static int xhci_ctrl_transfer(uint8_t req_type, uint8_t req, uint16_t value,
                              uint16_t index, void *data, uint16_t len) {
  xhci_trb_t *trb = &ep0_ring[ep0_idx];
  uint64_t setup = ((uint64_t)req_type) |
                   ((uint64_t)req << 8) |
                   ((uint64_t)value << 16) |
                   ((uint64_t)index << 32) |
                   ((uint64_t)len << 48);
  uint32_t ctrl = (XHCI_TRB_TYPE_SETUP << 10) | ep0_cycle | (1U << 5);
  xhci_set_trb(trb, setup, 8, ctrl);
  ep0_idx++;

  if (len > 0) {
    uint64_t param = xhci_phys_addr(data);
    uint32_t status = len;
    uint32_t ctrl_data = (XHCI_TRB_TYPE_DATA << 10) | ep0_cycle | (1U << 5);
    if ((req_type & 0x80) == 0) {
      ctrl_data |= (1U << 16);
    }
    xhci_set_trb(&ep0_ring[ep0_idx], param, status, ctrl_data);
    ep0_idx++;
  }

  uint32_t status_dir = (req_type & 0x80) ? 0 : 1;
  uint32_t ctrl_status = (XHCI_TRB_TYPE_STATUS << 10) | ep0_cycle | (1U << 5);
  ctrl_status |= (status_dir << 16);
  xhci_set_trb(&ep0_ring[ep0_idx], 0, 0, ctrl_status);
  ep0_idx++;

  xhci_ring_doorbell(device_slot, 1);

  return xhci_wait_for_transfer(1, 20000);
}

static int xhci_fetch_descriptor(uint8_t type, uint8_t index, void *buf, uint16_t len) {
  return xhci_ctrl_transfer(0x80, 6, (uint16_t)((type << 8) | index), 0, buf, len);
}

static int xhci_parse_hid(uint8_t *buf, uint16_t len, uint8_t *out_ep, uint16_t *out_mps,
                          uint8_t *out_interval, uint8_t *out_iface, uint8_t *out_cfg) {
  uint16_t i = 0;
  uint8_t cur_iface = 0xFF;
  uint8_t cur_cfg = 1;
  while (i + 2 <= len) {
    uint8_t dlen = buf[i];
    uint8_t dtype = buf[i + 1];
    if (dlen == 0 || i + dlen > len) {
      break;
    }
    if (dtype == 2 && dlen >= 9) {
      cur_cfg = buf[i + 5];
    } else if (dtype == 4 && dlen >= 9) {
      uint8_t cls = buf[i + 5];
      uint8_t proto = buf[i + 7];
      /* Relaxed check: Accept any HID Keyboard (Proto 1) or just HID (Class 3) */
      if (cls == 3 && (proto == 1 || proto == 0)) {
        cur_iface = buf[i + 2];
      } else {
        cur_iface = 0xFF;
      }
    } else if (dtype == 5 && dlen >= 7) {
      if (cur_iface != 0xFF) {
        uint8_t addr = buf[i + 2];
        uint8_t attr = buf[i + 3];
        /* Relaxed check: Accept Interrupt IN (0x80 | x) */
        if ((addr & 0x80) && ((attr & 0x3) == 3)) {
          *out_ep = addr & 0x0F;
          *out_mps = (uint16_t)(buf[i + 4] | (buf[i + 5] << 8));
          *out_interval = buf[i + 6];
          *out_iface = cur_iface;
          *out_cfg = cur_cfg;
          return 0;
        }
      }
    }
    i = (uint16_t)(i + dlen);
  }
  return -1;
}

static int xhci_setup_keyboard(uint32_t port) {
  uint8_t saved_device_slot = device_slot;
  xhci_trb_t *saved_ep0_ring = ep0_ring;
  uint8_t saved_ep0_cycle = ep0_cycle;
  uint32_t saved_ep0_idx = ep0_idx;
  void *saved_dev_ctx = dev_ctx;
  void *enum_dev_ctx = NULL;

  serial_puts("[xHCI] Setup keyboard on port ");
  serial_puthex(port);
  serial_puts("\n");
  
  uint32_t portsc_off = xhci_op_base + 0x400 + (port * 0x10);
  uint32_t portsc = xhci_read32(portsc_off);
  serial_puts("[xHCI] PORTSC: ");
  serial_puthex(portsc);
  serial_puts("\n");
  
  if ((portsc & XHCI_PORTSC_CCS) == 0) {
    serial_puts("[xHCI] Port not connected\n");
    return -1;
  }

  xhci_wait_port_ready(port);
  portsc = xhci_read32(portsc_off);
  if ((portsc & XHCI_PORTSC_PED) == 0) {
    serial_puts("[xHCI] Port reset failed\n");
    return -1;
  }

  int slot = xhci_cmd_enable_slot();
  if (slot <= 0) {
    return -1;
  }
  device_slot = (uint8_t)slot;

  size_t ctx_bytes = (size_t)xhci_context_size * 4;
  void *input = xhci_alloc_aligned(ctx_bytes, 64);
  if (!input) {
    return -1;
  }
  memset(input, 0, ctx_bytes);
  xhci_input_set_flags(input, 0x3, 0);
  xhci_setup_slot_ctx(input, portsc, (uint8_t)(port + 1), 1);

  ep0_ring = (xhci_trb_t *)xhci_alloc_aligned(sizeof(xhci_trb_t) * 256, 16);
  if (!ep0_ring) {
    return -1;
  }
  memset(ep0_ring, 0, sizeof(xhci_trb_t) * 256);
  ep0_idx = 0;
  ep0_cycle = 1;

  xhci_setup_ep_ctx(input, 2, 4, (uint64_t)(uintptr_t)ep0_ring, 8, 0);

  enum_dev_ctx = xhci_alloc_aligned((size_t)xhci_context_size * 4, 64);
  if (!enum_dev_ctx) {
    device_slot = saved_device_slot;
    ep0_ring = saved_ep0_ring;
    ep0_cycle = saved_ep0_cycle;
    ep0_idx = saved_ep0_idx;
    dev_ctx = saved_dev_ctx;
    return -1;
  }
  memset(enum_dev_ctx, 0, (size_t)xhci_context_size * 4);
  dcbaa[device_slot] = xhci_phys_addr(enum_dev_ctx);

  if (xhci_cmd_address_device(device_slot, input) != 0) {
    device_slot = saved_device_slot;
    ep0_ring = saved_ep0_ring;
    ep0_cycle = saved_ep0_cycle;
    ep0_idx = saved_ep0_idx;
    dev_ctx = saved_dev_ctx;
    return -1;
  }

  uint8_t *dev_desc = (uint8_t *)kzalloc(18);
  if (!dev_desc) {
    device_slot = saved_device_slot;
    ep0_ring = saved_ep0_ring;
    ep0_cycle = saved_ep0_cycle;
    ep0_idx = saved_ep0_idx;
    dev_ctx = saved_dev_ctx;
    return -1;
  }
  if (xhci_fetch_descriptor(1, 0, dev_desc, 8) != 0) {
    device_slot = saved_device_slot;
    ep0_ring = saved_ep0_ring;
    ep0_cycle = saved_ep0_cycle;
    ep0_idx = saved_ep0_idx;
    dev_ctx = saved_dev_ctx;
    return -1;
  }
  uint8_t mps = dev_desc[7];
  if (mps == 0) {
    mps = 8;
  }
  xhci_input_set_flags(input, 0x2, 0);
  xhci_setup_ep_ctx(input, 2, 4, (uint64_t)(uintptr_t)ep0_ring, mps, 0);
  if (xhci_cmd_evaluate_ctx(device_slot, input) != 0) {
    device_slot = saved_device_slot;
    ep0_ring = saved_ep0_ring;
    ep0_cycle = saved_ep0_cycle;
    ep0_idx = saved_ep0_idx;
    dev_ctx = saved_dev_ctx;
    return -1;
  }

  if (xhci_fetch_descriptor(1, 0, dev_desc, 18) != 0) {
    device_slot = saved_device_slot;
    ep0_ring = saved_ep0_ring;
    ep0_cycle = saved_ep0_cycle;
    ep0_idx = saved_ep0_idx;
    dev_ctx = saved_dev_ctx;
    return -1;
  }

  uint8_t *cfg_hdr = (uint8_t *)kzalloc(9);
  if (!cfg_hdr) {
    device_slot = saved_device_slot;
    ep0_ring = saved_ep0_ring;
    ep0_cycle = saved_ep0_cycle;
    ep0_idx = saved_ep0_idx;
    dev_ctx = saved_dev_ctx;
    return -1;
  }
  if (xhci_fetch_descriptor(2, 0, cfg_hdr, 9) != 0) {
    device_slot = saved_device_slot;
    ep0_ring = saved_ep0_ring;
    ep0_cycle = saved_ep0_cycle;
    ep0_idx = saved_ep0_idx;
    dev_ctx = saved_dev_ctx;
    return -1;
  }
  uint16_t total = (uint16_t)(cfg_hdr[2] | (cfg_hdr[3] << 8));
  if (total < 9 || total > 512) {
    device_slot = saved_device_slot;
    ep0_ring = saved_ep0_ring;
    ep0_cycle = saved_ep0_cycle;
    ep0_idx = saved_ep0_idx;
    dev_ctx = saved_dev_ctx;
    return -1;
  }
  uint8_t *cfg = (uint8_t *)kzalloc(total);
  if (!cfg) {
    device_slot = saved_device_slot;
    ep0_ring = saved_ep0_ring;
    ep0_cycle = saved_ep0_cycle;
    ep0_idx = saved_ep0_idx;
    dev_ctx = saved_dev_ctx;
    return -1;
  }
  if (xhci_fetch_descriptor(2, 0, cfg, total) != 0) {
    device_slot = saved_device_slot;
    ep0_ring = saved_ep0_ring;
    ep0_cycle = saved_ep0_cycle;
    ep0_idx = saved_ep0_idx;
    dev_ctx = saved_dev_ctx;
    return -1;
  }
  usb_note_device("xhci", (uint8_t)(port + 1), dev_desc, cfg, total);

  if (xhci_parse_hid(cfg, total, &intr_ep, &intr_mps, &intr_interval, &interface_num, &config_value) != 0) {
    device_slot = saved_device_slot;
    ep0_ring = saved_ep0_ring;
    ep0_cycle = saved_ep0_cycle;
    ep0_idx = saved_ep0_idx;
    dev_ctx = saved_dev_ctx;
    return -1;
  }

  if (xhci_keyboard_ready) {
    device_slot = saved_device_slot;
    ep0_ring = saved_ep0_ring;
    ep0_cycle = saved_ep0_cycle;
    ep0_idx = saved_ep0_idx;
    dev_ctx = saved_dev_ctx;
    return 0;
  }

  if (xhci_ctrl_transfer(0x00, 9, config_value, 0, NULL, 0) != 0) {
    serial_puts("[xHCI] Set Configuration failed\n");
    device_slot = saved_device_slot;
    ep0_ring = saved_ep0_ring;
    ep0_cycle = saved_ep0_cycle;
    ep0_idx = saved_ep0_idx;
    dev_ctx = saved_dev_ctx;
    return -1;
  }
  
  /* Try Set_Protocol and Set_Idle (ignore errors - some keyboards don't support them) */
  serial_puts("[xHCI] Sending Set_Protocol...\n");
  int proto_result = xhci_ctrl_transfer(0x21, 0x0B, 0, interface_num, NULL, 0);
  serial_puts(proto_result == 0 ? "[xHCI] Set_Protocol OK\n" : "[xHCI] Set_Protocol failed (ignored)\n");
  
  serial_puts("[xHCI] Sending Set_Idle...\n");
  int idle_result = xhci_ctrl_transfer(0x21, 0x0A, 0, interface_num, NULL, 0);
  serial_puts(idle_result == 0 ? "[xHCI] Set_Idle OK\n" : "[xHCI] Set_Idle failed (ignored)\n");

  intr_ring = (xhci_trb_t *)xhci_alloc_aligned(sizeof(xhci_trb_t) * 256, 64);
  if (!intr_ring) {
    device_slot = saved_device_slot;
    ep0_ring = saved_ep0_ring;
    ep0_cycle = saved_ep0_cycle;
    ep0_idx = saved_ep0_idx;
    dev_ctx = saved_dev_ctx;
    return -1;
  }
  memset(intr_ring, 0, sizeof(xhci_trb_t) * 256);
  intr_idx = 0;
  intr_cycle = 1; /* Producer cycle starts at 1 */
  intr_buf = (uint8_t *)kzalloc(intr_mps);
  if (!intr_buf) {
    device_slot = saved_device_slot;
    ep0_ring = saved_ep0_ring;
    ep0_cycle = saved_ep0_cycle;
    ep0_idx = saved_ep0_idx;
    dev_ctx = saved_dev_ctx;
    return -1;
  }

  uint32_t intr_ep_id = (uint32_t)((intr_ep * 2) + 1);
  xhci_input_set_flags(input, (1U << 0) | (1U << intr_ep_id), 0);
  xhci_setup_slot_ctx(input, portsc, (uint8_t)(port + 1), intr_ep_id);
  
  /* Setup endpoint with DCS=1, Error Count=3, Max Burst=0 */
  uint32_t *ep = xhci_ctx_ptr(input, intr_ep_id);
  xhci_ctx_zero(input, intr_ep_id);
  ep[0] = ((uint32_t)intr_interval << 16) | (3U << 8); /* Interval + Mult=0 + MaxBurst=0 */
  ep[1] = (7U << 3) | (3U << 1) | ((uint32_t)intr_mps << 16); /* EP Type=7 (Intr IN) + CErr=3 + MaxPacketSize */
  uint64_t ring_phys = xhci_phys_addr(intr_ring);
  ep[2] = (uint32_t)((ring_phys & ~0xFULL) | 1U); /* TR Dequeue + DCS=1 */
  ep[3] = (uint32_t)(ring_phys >> 32);
  ep[4] = (uint32_t)intr_mps; /* Average TRB Length */
  if (xhci_cmd_configure_ep(device_slot, input) != 0) {
    serial_puts("[xHCI] Configure EP failed\n");
    device_slot = saved_device_slot;
    ep0_ring = saved_ep0_ring;
    ep0_cycle = saved_ep0_cycle;
    ep0_idx = saved_ep0_idx;
    dev_ctx = saved_dev_ctx;
    return -1;
  }

  dev_ctx = enum_dev_ctx;
  xhci_keyboard_ready = 1;
  serial_puts("[xHCI] Keyboard setup complete!\n");
  return 0;
}

static void xhci_queue_interrupt_in(void) {
  if (!intr_ring || !intr_buf) {
    serial_puts("[xHCI] queue_intr: no ring/buf\n");
    return;
  }
  
  static int first_queue = 1;
  if (first_queue) {
    serial_puts("[xHCI] Queuing first interrupt IN\n");
    first_queue = 0;
  }
  
  uint64_t param = xhci_phys_addr(intr_buf);
  uint32_t status = intr_mps;
  /* TRB Type=Normal, Cycle, IOC (Interrupt on Completion), ISP (Interrupt on Short Packet) */
  uint32_t ctrl = (XHCI_TRB_TYPE_NORMAL << 10) | intr_cycle | (1U << 5) | (1U << 2);
  xhci_set_trb(&intr_ring[intr_idx], param, status, ctrl);
  
  serial_puts("[xHCI] Queued TRB at idx=");
  serial_puthex(intr_idx);
  serial_puts(" cycle=");
  serial_puthex(intr_cycle);
  serial_puts(" buf_phys=");
  serial_puthex(param);
  serial_puts("\n");
  intr_idx++;
  if (intr_idx >= 255) {
    uint64_t ring_phys = xhci_phys_addr(intr_ring);
    xhci_set_trb(&intr_ring[intr_idx], ring_phys, 0,
                 (XHCI_TRB_TYPE_LINK << 10) | intr_cycle | (1U << 1));
    intr_idx = 0;
    intr_cycle ^= 1;
  }
  uint8_t db_target = (uint8_t)((intr_ep * 2) + 1);
  xhci_ring_doorbell(device_slot, db_target);
  
  serial_puts("[xHCI] Rang doorbell slot=");
  serial_puthex(device_slot);
  serial_puts(" ep=");
  serial_puthex(db_target);
  serial_puts("\n");
}

static uint32_t xhci_port_change_bits(void) {
  return XHCI_PORTSC_CSC | XHCI_PORTSC_PRC | (1U << 18) | (1U << 20) |
         (1U << 22);
}

static void xhci_prepare_port(uint32_t port) {
  uint32_t portsc_off = xhci_op_base + 0x400 + (port * 0x10);
  uint32_t portsc = xhci_read32(portsc_off);
  uint32_t change_bits = xhci_port_change_bits();

  xhci_portsc_snapshot[port] = portsc;
  xhci_port_connected[port] = (portsc & XHCI_PORTSC_CCS) ? 1 : 0;

  if (portsc & change_bits) {
    xhci_write32(portsc_off, (portsc & 0x0E01C3E0U) | change_bits);
    xhci_delay(5000);
    portsc = xhci_read32(portsc_off);
  }

  if ((portsc & (1U << 9)) == 0) {
    xhci_write32(portsc_off, (portsc & 0x0E01C3E0U) | (1U << 9));
    xhci_delay(20000);
    portsc = xhci_read32(portsc_off);
  }

  xhci_portsc_snapshot[port] = portsc;
  xhci_port_connected[port] = (portsc & XHCI_PORTSC_CCS) ? 1 : 0;
  if ((portsc & XHCI_PORTSC_CCS) == 0) {
    xhci_port_attempted[port] = 0;
    xhci_port_result[port] = 0;
  }
}

static void xhci_scan_ports_internal(int force) {
  uint32_t tracked;

  if (!xhci_present || xhci_ports == 0) {
    return;
  }

  tracked = xhci_tracked_ports();
  xhci_scan_count++;
  if (force) {
    for (uint32_t port = 0; port < tracked; port++) {
      xhci_port_attempted[port] = 0;
      xhci_port_result[port] = 0;
    }
  }

  for (uint32_t port = 0; port < tracked; port++) {
    xhci_prepare_port(port);
  }

  for (uint32_t port = 0; port < tracked; port++) {
    if (!xhci_port_connected[port] || xhci_port_attempted[port]) {
      continue;
    }
    xhci_scan_attempts++;
    xhci_port_attempted[port] = 1;
    xhci_port_result[port] = (int8_t)xhci_setup_keyboard(port);
    xhci_portsc_snapshot[port] =
        xhci_read32(xhci_op_base + 0x400 + (port * 0x10));
    if (xhci_port_result[port] == 0 && xhci_keyboard_ready) {
      for (int y = 0; y < 100; y++) {
        for (int x = 0; x < 100; x++) {
          debug_rect(x, 200 + y, 1, 1, 0xFF00FF00);
        }
      }
      xhci_queue_interrupt_in();
    } else {
      for (int y = 0; y < 100; y++) {
        for (int x = 0; x < 100; x++) {
          debug_rect(x, 200 + y, 1, 1, 0xFFFF0000);
        }
      }
    }
  }
}

void usb_xhci_rescan(void) {
  xhci_scan_ports_internal(1);
}

void usb_xhci_format_ports(char *buf, size_t size) {
  uint32_t tracked;
  uint32_t connected = 0;
  int shown = 0;
  int used;

  if (!buf || size == 0) {
    return;
  }
  if (!xhci_present) {
    snprintf(buf, size, "xhci-ports present=no");
    return;
  }

  tracked = xhci_tracked_ports();
  for (uint32_t port = 0; port < tracked; port++) {
    if (xhci_port_connected[port]) {
      connected++;
    }
  }

  used = snprintf(buf, size,
                  "xhci-ports present=yes controllers=%u selected=%u "
                  "pci=%02x:%02x.%u id=%04x:%04x ports=%u tracked=%u "
                  "slots=%u preinit-connected=%u scans=%lu attempts=%lu "
                  "connected=%u keyboard=%s",
                  xhci_controller_count, xhci_controller_index,
                  xhci_pci_bus, xhci_pci_device, xhci_pci_function,
                  xhci_pci_vendor_id, xhci_pci_device_id, xhci_ports,
                  tracked, xhci_max_slots, xhci_selected_preinit_connected,
                  xhci_scan_count, xhci_scan_attempts, connected,
                  xhci_keyboard_ready ? "yes" : "no");
  if (used < 0 || (size_t)used >= size) {
    return;
  }

  for (uint32_t port = 0; port < tracked && shown < 8; port++) {
    if (!xhci_port_connected[port] && !xhci_port_attempted[port]) {
      continue;
    }
    int n = snprintf(buf + used, size - (size_t)used,
                     " p%u=0x%08x/%s/try=%u/rc=%d", port + 1,
                     xhci_portsc_snapshot[port],
                     xhci_port_connected[port] ? "conn" : "empty",
                     xhci_port_attempted[port], xhci_port_result[port]);
    if (n < 0 || (size_t)n >= size - (size_t)used) {
      return;
    }
    used += n;
    shown++;
  }
  if (shown == 0 && (size_t)used < size) {
    int n = snprintf(buf + used, size - (size_t)used, " no-active-root-ports");
    if (n < 0 || (size_t)n >= size - (size_t)used) {
      return;
    }
    used += n;
  }

  for (uint32_t i = 0; i < xhci_controller_count && i < 4; i++) {
    int n;
    if (xhci_candidate_vendor_id[i] == 0) {
      continue;
    }
    n = snprintf(buf + used, size - (size_t)used,
                 " cand%u=%02x:%02x.%u/%04x:%04x/p%u/c%u", i,
                 xhci_candidate_bus[i], xhci_candidate_device[i],
                 xhci_candidate_function[i], xhci_candidate_vendor_id[i],
                 xhci_candidate_device_id[i], xhci_candidate_ports[i],
                 xhci_candidate_connected[i]);
    if (n < 0 || (size_t)n >= size - (size_t)used) {
      return;
    }
    used += n;
  }
}

static void xhci_maybe_rescan_ports(void) {
  static unsigned int poll_divider = 0;
  uint32_t tracked;
  uint32_t change_bits;

  if (!xhci_present || xhci_ports == 0) {
    return;
  }
  poll_divider++;
  if ((poll_divider & 0x3F) != 0) {
    return;
  }

  tracked = xhci_tracked_ports();
  change_bits = xhci_port_change_bits();
  for (uint32_t port = 0; port < tracked; port++) {
    uint32_t portsc = xhci_read32(xhci_op_base + 0x400 + (port * 0x10));
    if ((portsc & change_bits) ||
        ((portsc & XHCI_PORTSC_CCS) && !xhci_port_attempted[port])) {
      xhci_scan_ports_internal(0);
      return;
    }
  }
}

static void xhci_handle_event(xhci_trb_t *evt) {
  uint32_t type = (evt->dword3 >> 10) & 0x3F;
  
  static int first_event = 1;
  if (first_event) {
    serial_puts("[xHCI] First event type: ");
    serial_puthex(type);
    serial_puts(" dw2=");
    serial_puthex(evt->dword2);
    serial_puts(" dw3=");
    serial_puthex(evt->dword3);
    serial_puts("\n");
    first_event = 0;
    
    /* [4] CYAN BLOCK - first event received */
    for (int y = 0; y < 100; y++) {
      for (int x = 0; x < 100; x++) {
        debug_rect(x, 300 + y, 1, 1, 0xFF00FFFF);
      }
    }
  }
  
  if (type == XHCI_TRB_TYPE_TRANSFER_EVENT) {
    uint8_t ep_id = (uint8_t)((evt->dword3 >> 16) & 0x1F);
    uint8_t cc = (uint8_t)((evt->dword2 >> 24) & 0xFF);
    uint8_t slot_id = (uint8_t)((evt->dword3 >> 24) & 0xFF);
    
    serial_puts("[xHCI] Transfer event: slot=");
    serial_puthex(slot_id);
    serial_puts(" ep=");
    serial_puthex(ep_id);
    serial_puts(" cc=");
    serial_puthex(cc);
    serial_puts(" expected_ep=");
    serial_puthex((intr_ep * 2) + 1);
    serial_puts("\n");
    
    if (ep_id == (uint8_t)((intr_ep * 2) + 1) && cc == 1) {
      usb_submit_hid_report(intr_buf, intr_mps);
      xhci_queue_interrupt_in();
    } else if (ep_id == (uint8_t)((intr_ep * 2) + 1)) {
      serial_puts("[xHCI] Transfer CC not success, re-queuing\n");
      xhci_queue_interrupt_in();
    }
  } else if (type == XHCI_TRB_TYPE_PORT_STATUS) {
    serial_puts("[xHCI] Port status change event\n");
  } else {
    serial_puts("[xHCI] Other event type: ");
    serial_puthex(type);
    serial_puts("\n");
  }
}

void usb_xhci_init(void) {
  serial_puts("\n[xHCI] Starting init...\n");
  pci_device_info_t devs[8];
  int count = pci_scan_class(0x0C, 0x03, 0x30, devs, 8);
  int selected = -1;
  uint64_t selected_phys = 0;
  uint64_t selected_mmio = 0;
  uint32_t selected_ports = 0;
  uint32_t best_score = 0;
  serial_puts("[xHCI] Found ");
  serial_puthex(count);
  serial_puts(" controller(s)\n");
  if (count <= 0) {
    return;
  }
  xhci_controller_count = (uint32_t)count;

  for (int i = 0; i < count; i++) {
    uint64_t phys = xhci_bar_phys(&devs[i]);
    uint64_t mmio;
    uint32_t cap;
    uint32_t hcs1;
    uint32_t ports;
    uint8_t caplength;
    uint32_t connected;
    uint32_t score;

    if (!phys) {
      continue;
    }
    if (i < 8) {
      xhci_candidate_bus[i] = devs[i].bus;
      xhci_candidate_device[i] = devs[i].device;
      xhci_candidate_function[i] = devs[i].function;
      xhci_candidate_vendor_id[i] = devs[i].vendor_id;
      xhci_candidate_device_id[i] = devs[i].device_id;
    }
    mmio = mmio_map_range(phys, 0x20000);
    if (!mmio) {
      continue;
    }
    cap = xhci_read32_from(mmio, 0x00);
    if (cap == 0 || cap == 0xFFFFFFFF) {
      continue;
    }
    caplength = (uint8_t)(cap & 0xFF);
    hcs1 = xhci_read32_from(mmio, 0x04);
    ports = (hcs1 >> 24) & 0xFF;
    connected = xhci_probe_connected_ports(mmio, ports, caplength);
    if (i < 8) {
      xhci_candidate_ports[i] = ports;
      xhci_candidate_connected[i] = connected;
    }
    score = (connected * 1024U) + ports;

    serial_puts("[xHCI] Candidate ");
    serial_puthex((uint32_t)i);
    serial_puts(" pci=");
    serial_puthex(devs[i].bus);
    serial_puts(":");
    serial_puthex(devs[i].device);
    serial_puts(".");
    serial_puthex(devs[i].function);
    serial_puts(" ports=");
    serial_puthex(ports);
    serial_puts(" connected=");
    serial_puthex(connected);
    serial_puts("\n");

    if (selected < 0 || score > best_score) {
      selected = i;
      selected_phys = phys;
      selected_mmio = mmio;
      selected_ports = ports;
      xhci_selected_preinit_connected = connected;
      best_score = score;
    }
  }

  if (selected < 0 || selected_mmio == 0) {
    return;
  }
  xhci_controller_index = (uint32_t)selected;
  xhci_pci_bus = devs[selected].bus;
  xhci_pci_device = devs[selected].device;
  xhci_pci_function = devs[selected].function;
  xhci_pci_vendor_id = devs[selected].vendor_id;
  xhci_pci_device_id = devs[selected].device_id;
  
  serial_puts("[xHCI] BAR phys: ");
  serial_puthex(selected_phys);
  serial_puts("\n");
  
  xhci_mmio = selected_mmio;
  serial_puts("[xHCI] MMIO mapped to: ");
  serial_puthex(selected_mmio);
  serial_puts(" selected-ports=");
  serial_puthex(selected_ports);
  serial_puts("\n");
  
  /* Enable bus mastering and memory space before MMIO access */
  uint32_t cmd = pci_read32(devs[selected].bus, devs[selected].device,
                            devs[selected].function, 0x04);
  cmd |= (1U << 2) | (1U << 1);
  pci_write32(devs[selected].bus, devs[selected].device,
              devs[selected].function, 0x04, cmd);

  /* Legacy BIOS handoff (if enabled) */
  serial_puts("[xHCI] Attempting legacy handoff...\n");
  xhci_legacy_handoff();
  serial_puts("[xHCI] Legacy handoff done\n");

  /* Test MMIO access - read capability register */
  uint32_t cap = xhci_read32(0x00);
  serial_puts("[xHCI] Capability: ");
  serial_puthex(cap);
  serial_puts("\n");
  if (cap == 0xFFFFFFFF || cap == 0) {
    serial_puts("[xHCI] MMIO not responding\n");
    return; /* MMIO not responding */
  }
  
  xhci_present = 1;

  /* [1] WHITE BLOCK - xHCI found (left edge, top) */
  for (int y = 0; y < 100; y++) {
    for (int x = 0; x < 100; x++) {
      debug_rect(x, y, 1, 1, 0xFFFFFFFF);
    }
  }
  serial_puts("[xHCI] Controller present\n");

  uint32_t irq_reg = pci_read32(devs[selected].bus, devs[selected].device,
                                devs[selected].function, 0x3C);
  xhci_irq = (uint8_t)(irq_reg & 0xFF);
  if (xhci_irq < 16) {
    idt_register_handler((uint8_t)(0x20 + xhci_irq), usb_xhci_irq_handler);
    pic_clear_mask(xhci_irq);
  }

  uint8_t caplength = (uint8_t)(cap & 0xFF);
  xhci_op_base = caplength;
  uint32_t hcs1 = xhci_read32(0x04);
  xhci_max_slots = hcs1 & 0xFF;
  if (xhci_max_slots == 0) {
    xhci_max_slots = 1;
  }
  xhci_ports = (hcs1 >> 24) & 0xFF;

  uint32_t hcs2 = xhci_read32(0x08);
  scratchpad_count = (uint32_t)(((hcs2 >> 16) & 0x3E0) | ((hcs2 >> 27) & 0x1F));

  uint32_t hcc = xhci_read32(0x10);
  xhci_context_size = ((hcc >> 2) & 0x1) ? 64 : 32;

  xhci_db_base = xhci_read32(0x14);
  xhci_rt_base = xhci_read32(0x18);

  uint32_t usbcmd = xhci_read32(xhci_op_base + 0x00);
  usbcmd &= ~1U;
  xhci_write32(xhci_op_base + 0x00, usbcmd);
  xhci_wait_usbsts(1U << 0, 1U << 0, 20000); /* HCHalted */

  /* Host controller reset (USBCMD bit 1) */
  usbcmd = xhci_read32(xhci_op_base + 0x00);
  usbcmd |= (1U << 1);
  xhci_write32(xhci_op_base + 0x00, usbcmd);
  xhci_wait_usbsts(1U << 11, 0, 50000); /* CNR cleared */
  xhci_wait_usbsts(1U << 0, 1U << 0, 20000); /* HCHalted */

  /* Set page size to 4K */
  xhci_write32(xhci_op_base + 0x08, 1);

  dcbaa = (uint64_t *)xhci_alloc_aligned(256 * sizeof(uint64_t), 64);
  if (!dcbaa) {
    return;
  }
  memset(dcbaa, 0, 256 * sizeof(uint64_t));
  if (scratchpad_count > 0) {
    scratchpad_array = (uint64_t *)xhci_alloc_aligned((size_t)scratchpad_count * sizeof(uint64_t), 64);
    scratchpad_bufs = (void **)kzalloc((size_t)scratchpad_count * sizeof(void *));
    if (!scratchpad_array || !scratchpad_bufs) {
      return;
    }
    for (uint32_t i = 0; i < scratchpad_count; i++) {
      void *buf = xhci_alloc_aligned(4096, 4096);
      if (!buf) {
        return;
      }
      scratchpad_bufs[i] = buf;
      scratchpad_array[i] = xhci_phys_addr(buf);
    }
    dcbaa[0] = xhci_phys_addr(scratchpad_array);
  }
  xhci_write64(xhci_op_base + 0x30, xhci_phys_addr(dcbaa));

  cmd_ring = (xhci_trb_t *)xhci_alloc_aligned(sizeof(xhci_trb_t) * 256, 16);
  if (!cmd_ring) {
    return;
  }
  memset(cmd_ring, 0, sizeof(xhci_trb_t) * 256);
  cmd_ring_idx = 0;
  cmd_cycle = 1;
  uint64_t cmd_ring_phys = xhci_phys_addr(cmd_ring);
  xhci_set_trb(&cmd_ring[255], cmd_ring_phys, 0,
               (XHCI_TRB_TYPE_LINK << 10) | cmd_cycle | (1U << 1));

  xhci_write64(xhci_op_base + 0x18, cmd_ring_phys | cmd_cycle);

  event_ring = (xhci_trb_t *)xhci_alloc_aligned(sizeof(xhci_trb_t) * 256, 16);
  if (!event_ring) {
    return;
  }
  memset(event_ring, 0, sizeof(xhci_trb_t) * 256);
  event_ring_idx = 0;
  event_cycle = 1;

  erst = (xhci_erst_entry_t *)xhci_alloc_aligned(sizeof(xhci_erst_entry_t), 64);
  if (!erst) {
    return;
  }
  erst[0].ring_base = xhci_phys_addr(event_ring);
  erst[0].ring_size = 256;
  erst[0].rsvd = 0;
  /* Enable interrupter 0 */
  xhci_write32(xhci_rt_base + 0x20, (1U << 1) | (1U << 0));
  xhci_write32(xhci_rt_base + 0x24, 0);
  xhci_write32(xhci_rt_base + 0x28, 1);
  xhci_write64(xhci_rt_base + 0x30, xhci_phys_addr(erst));
  xhci_write64(xhci_rt_base + 0x38, xhci_phys_addr(event_ring));

  uint32_t config = xhci_read32(xhci_op_base + 0x38);
  uint32_t enabled_slots = xhci_max_slots;
  if (enabled_slots > 32) {
    enabled_slots = 32;
  }
  config &= ~0xFF;
  config |= enabled_slots;
  xhci_write32(xhci_op_base + 0x38, config);

  usbcmd = xhci_read32(xhci_op_base + 0x00);
  usbcmd |= 1U | (1U << 2); /* Run + Interrupter Enable */
  xhci_write32(xhci_op_base + 0x00, usbcmd);
  xhci_wait_usbsts(1U << 0, 0, 20000); /* Wait for HCHalted cleared */

  usb_xhci_rescan();
}

int usb_xhci_ready(void) {
  return xhci_present;
}

void usb_xhci_poll(void) {
  if (!xhci_present) {
    return;
  }

  static int event_count = 0;
  
  while (1) {
    xhci_trb_t evt;
    if (xhci_pop_event(&evt) != 0) {
      break;
    }
    event_count++;
    xhci_handle_event(&evt);
    
    /* Show event count visually - small white dots */
    if (event_count <= 10) {
      debug_rect(210 + (event_count - 1) * 5, 0, 4, 4, 0xFFFFFFFF);
    }
  }

  if (xhci_irq_seen) {
    xhci_irq_seen = 0;
  }
  xhci_maybe_rescan_ports();
}
