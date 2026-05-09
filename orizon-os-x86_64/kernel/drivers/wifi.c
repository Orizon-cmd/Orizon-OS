/*
 * Orizon OS x86_64 - Wi-Fi driver staging
 *
 * This file stages Intel Wi-Fi support incrementally. Modern Intel CNVi
 * devices require firmware loading, DMA command queues, MAC/radio setup,
 * 802.11 management frames, and WPA handshakes. For real hardware safety,
 * each milestone exposes explicit diagnostics before enabling the next layer.
 */

#include "../include/wifi.h"
#include "../include/bootinfo.h"
#include "../include/gui.h"
#include "../include/mmio.h"
#include "../include/pci.h"
#include "../include/string.h"
#include "../include/vfs.h"

static wifi_status_t wifi_status_state = {
    .present = 0,
    .driver_ready = 0,
    .associated = 0,
    .bus = 0,
    .device = 0,
    .function = 0,
    .vendor_id = 0,
    .device_id = 0,
    .mmio_probed = 0,
    .mmio_ready = 0,
    .mmio_phys = 0,
    .pci_command = 0,
    .csr_hw_if_config = 0,
    .csr_hw_rev = 0,
    .csr_hw_rf_id = 0,
    .csr_gp_cntrl = 0,
    .csr_gpio_in = 0,
    .csr_reset = 0,
    .csr_int = 0,
    .csr_int_mask = 0,
    .csr_fh_int_status = 0,
    .mmio_errors = 0,
    .apm_ready = 0,
    .apm_timeout = 0,
    .apm_attempts = 0,
    .apm_poll_ready_mask = 0,
    .apm_last_gp = 0,
    .apm_last_hw_if = 0,
    .apm_last_gio = 0,
    .apm_last_hpet = 0,
    .alive_seen = 0,
    .alive_timeout = 0,
    .alive_polls = 0,
    .alive_errors = 0,
    .alive_last_csr_int = 0,
    .alive_last_fh_int = 0,
    .alive_last_gp = 0,
    .boot_ready = 0,
    .boot_released = 0,
    .boot_failed = 0,
    .boot_attempts = 0,
    .boot_cpu1_sections = 0,
    .boot_cpu2_sections = 0,
    .boot_paging_sections_skipped = 0,
    .boot_sections_loaded = 0,
    .boot_release_value = 0,
    .boot_fh_status = 0,
    .boot_ureg_status = 0,
    .boot_first_cpu2_index = 0,
    .boot_paging_index = 0,
    .boot_last_gp = 0,
    .queues_ready = 0,
    .queues_armed = 0,
    .queues_failed = 0,
    .queue_errors = 0,
    .queue_generation = 0,
    .cmd_queue_entries = 0,
    .tx_queue_entries = 0,
    .rx_queue_entries = 0,
    .cmd_buffer_bytes = 0,
    .tx_buffer_bytes = 0,
    .rx_buffer_bytes = 0,
    .cmd_tfd_bytes = 0,
    .tx_tfd_bytes = 0,
    .rx_desc_bytes = 0,
    .cmd_tfd_phys = 0,
    .cmd_buffer_phys = 0,
    .tx_tfd_phys = 0,
    .tx_buffer_phys = 0,
    .rx_desc_phys = 0,
    .rx_used_desc_phys = 0,
    .rx_status_phys = 0,
    .rx_buffer_phys = 0,
    .cmd_write_ptr = 0,
    .tx_write_ptr = 0,
    .rx_write_ptr = 0,
    .context_ready = 0,
    .context_armed = 0,
    .context_failed = 0,
    .context_mode = 0,
    .context_errors = 0,
    .context_generation = 0,
    .context_lmac_sections = 0,
    .context_umac_sections = 0,
    .context_paging_sections = 0,
    .legacy_context_phys = 0,
    .v2_context_phys = 0,
    .prph_info_phys = 0,
    .prph_scratch_phys = 0,
    .context_size = 0,
    .context_v2_size = 0,
    .prph_scratch_size = 0,
    .context_control_flags = 0,
    .context_v2_control_flags = 0,
    .context_csr_target = 0,
    .context_csr_value_lo = 0,
    .context_csr_value_hi = 0,
    .scheduler_ready = 0,
    .scheduler_armed = 0,
    .scheduler_failed = 0,
    .scheduler_errors = 0,
    .scheduler_generation = 0,
    .cmd_bc_phys = 0,
    .tx_bc_phys = 0,
    .mtr_ring_phys = 0,
    .mcr_ring_phys = 0,
    .cr_head_phys = 0,
    .cr_tail_phys = 0,
    .tr_head_phys = 0,
    .tr_tail_phys = 0,
    .mtr_entries = 0,
    .mcr_entries = 0,
    .msg_ring_bytes = 0,
    .scheduler_cmd_id = 0,
    .scheduler_cmd_group = 0,
    .scheduler_cmd_version = 0,
    .scheduler_cmd_len = 0,
    .scheduler_cmd_sequence = 0,
    .scheduler_cmd_queue = 0,
    .scheduler_cmd_index = 0,
    .scheduler_cmd_tbs = 0,
    .scheduler_cmd_tb_len = 0,
    .scheduler_wptr_value = 0,
    .rx_path_ready = 0,
    .rx_path_failed = 0,
    .rx_path_errors = 0,
    .rx_polls = 0,
    .rx_packets = 0,
    .rx_closed_rb = 0,
    .rx_read_ptr = 0,
    .rx_last_rbid = 0,
    .rx_last_flags = 0,
    .rx_last_len_n_flags = 0,
    .rx_last_len = 0,
    .rx_last_queue = 0,
    .rx_last_cmd = 0,
    .rx_last_group = 0,
    .rx_last_sequence = 0,
    .rx_last_index = 0,
    .command_ready = 0,
    .command_sent = 0,
    .command_failed = 0,
    .command_attempts = 0,
    .command_errors = 0,
    .command_doorbell_value = 0,
    .command_last_csr_int = 0,
    .command_last_fh_int = 0,
    .command_last_closed_rb = 0,
    .command_poll_loops = 0,
    .chipset = "none",
    .driver = "none",
    .status = "wifi: not initialized",
    .firmware_present = 0,
    .firmware_valid = 0,
    .firmware_size = 0,
    .firmware_version = 0,
    .firmware_build = 0,
    .firmware_tlv_count = 0,
    .firmware_inst_bytes = 0,
    .firmware_data_bytes = 0,
    .firmware_section_count = 0,
    .firmware_runtime_sections = 0,
    .firmware_init_sections = 0,
    .firmware_wowlan_sections = 0,
    .firmware_secure_sections = 0,
    .firmware_load_bytes = 0,
    .firmware_load_chunks = 0,
    .firmware_largest_section = 0,
    .firmware_api_count = 0,
    .firmware_capa_count = 0,
    .firmware_parse_errors = 0,
    .firmware_cpu_count = 0,
    .firmware_first_dst = 0,
    .firmware_load_plan_ready = 0,
    .firmware_human = "",
    .firmware_name = "none",
    .firmware_source = "none",
    .dma_ready = 0,
    .dma_phys = 0,
    .dma_chunk_bytes = 0,
    .dma_staged_bytes = 0,
    .firmware_load_attempts = 0,
    .fh_plan_ready = 0,
    .fh_armed = 0,
    .fh_complete = 0,
    .fh_timeout = 0,
    .fh_errors = 0,
    .fh_channel = 0,
    .fh_dst_addr = 0,
    .fh_byte_count = 0,
    .fh_ctrl0_value = 0,
    .fh_ctrl1_value = 0,
    .fh_buf_status_value = 0,
    .fh_tx_config_value = 0,
    .fh_last_csr_int = 0,
    .fh_last_fh_int = 0,
    .fh_last_tx_status = 0,
    .fh_last_tx_error = 0,
    .fh_total_chunks = 0,
    .fh_uploaded_chunks = 0,
    .fh_uploaded_bytes = 0,
    .fh_sections_done = 0,
    .fh_last_section = 0,
    .fh_last_offset = 0,
    .fh_last_len = 0,
    .fh_all_plan_ready = 0,
    .load_state = "wifi: firmware loader idle",
};

#define IWL_TLV_UCODE_MAGIC 0x0a4c5749U
#define IWL_TLV_HEADER_SIZE 88U
#define IWL_UCODE_TLV_INST 1U
#define IWL_UCODE_TLV_DATA 2U
#define IWL_UCODE_TLV_INIT 3U
#define IWL_UCODE_TLV_INIT_DATA 4U
#define IWL_UCODE_TLV_BOOT 5U
#define IWL_UCODE_TLV_WOWLAN_INST 16U
#define IWL_UCODE_TLV_WOWLAN_DATA 17U
#define IWL_UCODE_TLV_SEC_RT 19U
#define IWL_UCODE_TLV_SEC_INIT 20U
#define IWL_UCODE_TLV_SEC_WOWLAN 21U
#define IWL_UCODE_TLV_SECURE_SEC_RT 24U
#define IWL_UCODE_TLV_SECURE_SEC_INIT 25U
#define IWL_UCODE_TLV_SECURE_SEC_WOWLAN 26U
#define IWL_UCODE_TLV_NUM_OF_CPU 27U
#define IWL_UCODE_TLV_API_CHANGES_SET 29U
#define IWL_UCODE_TLV_ENABLED_CAPABILITIES 30U

#define IWLAGN_RTC_INST_LOWER_BOUND 0x00000000U
#define IWLAGN_RTC_DATA_LOWER_BOUND 0x00800000U
#define CPU1_CPU2_SEPARATOR_SECTION 0xffffccccU
#define PAGING_SEPARATOR_SECTION 0xaaaabbbbU
#define WIFI_FW_MAX_SECTIONS 96
#define WIFI_DMA_CHUNK_BYTES (128U * 1024U)

#define PCI_COMMAND_REG 0x04U
#define PCI_COMMAND_MEMORY_SPACE (1U << 1)
#define PCI_COMMAND_BUS_MASTER (1U << 2)

#define CSR_HW_IF_CONFIG_REG 0x000U
#define CSR_INT 0x008U
#define CSR_INT_MASK 0x00cU
#define CSR_FH_INT_STATUS 0x010U
#define CSR_RESET 0x020U
#define CSR_GP_CNTRL 0x024U
#define CSR_HW_REV 0x028U
#define CSR_HW_RF_ID 0x09cU
#define CSR_GPIO_IN 0x0a0U
#define CSR_GIO_CHICKEN_BITS 0x100U
#define CSR_DBG_HPET_MEM_REG 0x240U
#define HBUS_TARG_PRPH_WADDR 0x444U
#define HBUS_TARG_PRPH_RADDR 0x448U
#define HBUS_TARG_PRPH_WDAT 0x44cU
#define HBUS_TARG_PRPH_RDAT 0x450U

#define CSR_HW_IF_CONFIG_REG_HAP_WAKE 0x00080000U

#define CSR_INT_BIT_ALIVE (1U << 0)
#define CSR_INT_BIT_SCD (1U << 26)
#define CSR_INT_BIT_FH_TX (1U << 27)
#define CSR_INT_BIT_HW_ERR (1U << 29)
#define CSR_INT_BIT_SW_ERR (1U << 25)

#define CSR_GP_CNTRL_MAC_CLOCK_READY (1U << 0)
#define CSR_GP_CNTRL_INIT_DONE (1U << 2)
#define CSR_GP_CNTRL_MAC_ACCESS_REQ (1U << 3)
#define CSR_GP_CNTRL_GOING_TO_SLEEP (1U << 4)
#define CSR_GP_CNTRL_MAC_INIT (1U << 6)
#define CSR_GP_CNTRL_MAC_STATUS (1U << 20)
#define CSR_GP_CNTRL_BZ_MAC_ACCESS_REQ (1U << 21)
#define CSR_GP_CNTRL_BUS_MASTER_DISABLED (1U << 28)
#define CSR_GP_CNTRL_HW_RF_KILL_SW (1U << 27)

#define CSR_GIO_CHICKEN_BITS_L1A_NO_L0S_RX 0x00800000U
#define CSR_DBG_HPET_MEM_REG_VAL 0xffff0000U
#define PRPH_ADDR_SPACE_3 (3U << 24)
#define PRPH_ADDR_MASK_LEGACY 0x000fffffU
#define PRPH_ADDR_MASK_MODERN 0x00ffffffU
#define RELEASE_CPU_RESET 0x300cU
#define RELEASE_CPU_RESET_BIT (1U << 24)
#define FH_UCODE_LOAD_STATUS 0x1af0U
#define UREG_UCODE_LOAD_STATUS 0xa05c40U
#define WFPM_GP2 0xa030b4U

#define FH_MEM_LOWER_BOUND 0x1000U
#define FH_SRVC_CHNL 9U
#define FH_TFDIB_CTRL0_REG(ch) (FH_MEM_LOWER_BOUND + 0x900U + (0x8U * (ch)))
#define FH_TFDIB_CTRL1_REG(ch) (FH_MEM_LOWER_BOUND + 0x904U + (0x8U * (ch)))
#define FH_SRVC_CHNL_SRAM_ADDR_REG(ch) \
  (FH_MEM_LOWER_BOUND + 0x9c8U + (0x4U * ((ch) - FH_SRVC_CHNL)))
#define FH_TCSR_CHNL_TX_CONFIG_REG(ch) \
  (FH_MEM_LOWER_BOUND + 0xd00U + (0x20U * (ch)))
#define FH_TCSR_CHNL_TX_BUF_STS_REG(ch) \
  (FH_MEM_LOWER_BOUND + 0xd08U + (0x20U * (ch)))
#define FH_TCSR_CHNL_TX_ERROR_REG(ch) \
  (FH_MEM_LOWER_BOUND + 0xd18U + (0x20U * (ch)))

#define FH_TCSR_TX_CONFIG_REG_VAL_DMA_CHNL_ENABLE (1U << 31)
#define FH_TCSR_TX_CONFIG_REG_VAL_DMA_CREDIT_DISABLE (1U << 3)
#define FH_TCSR_TX_CONFIG_REG_VAL_CIRQ_HOST_ENDTFD (1U << 20)
#define FH_TCSR_TX_CONFIG_REG_VAL_CIRQ_RTC_NOINT 0U
#define FH_TCSR_TX_CONFIG_REG_VAL_DMA_CHNL_PAUSE 0U
#define FH_TCSR_CHNL_TX_BUF_STS_REG_VAL_TFDB_VALID (1U << 20)
#define FH_TCSR_CHNL_TX_BUF_STS_REG_BIT_TFDB_WPTR 12U
#define FH_TCSR_CHNL_TX_BUF_STS_REG_POS_TB_NUM 0U
#define FH_TCSR_CHNL_TX_BUF_STS_REG_POS_TB_IDX 3U
#define FH_MEM_TFDIB_DRAM_ADDR_LSB_MSK 0xffffffffU
#define FH_MEM_TFDIB_REG1_ADDR_BITSHIFT 28U
#define FH_MEM_TFDIB_REG1_ADDR_MASK 0xfU
#define FH_MEM_TFDIB_REG1_LENGTH_MASK 0x000fffffU
#define FH_INT_TX_MASK 0x0000ffffU
#define FH_INT_ERR_MASK 0xfff00000U
#define WIFI_FH_POLL_LOOPS 1000000U
#define WIFI_APM_POLL_LOOPS 250000U
#define WIFI_ALIVE_POLL_LOOPS 2000000U
#define WIFI_COMMAND_POLL_LOOPS 200000U
#define WIFI_NIC_ACCESS_POLL_LOOPS 150000U

#define WIFI_CMD_QUEUE_ENTRIES 32U
#define WIFI_TX_QUEUE_ENTRIES 64U
#define WIFI_RX_QUEUE_ENTRIES 64U
#define WIFI_CMD_TFD_BYTES 256U
#define WIFI_TX_TFD_BYTES 256U
#define WIFI_RX_DESC_BYTES 16U
#define WIFI_CMD_BUFFER_BYTES 1024U
#define WIFI_TX_BUFFER_BYTES 2048U
#define WIFI_RX_BUFFER_BYTES 2048U
#define WIFI_RX_STATUS_WORDS 16U
#define WIFI_CONTEXT_DRAM_ENTRIES 64U
#define WIFI_CONTEXT_DRAM_CHUNK_BYTES (32U * 1024U)
#define WIFI_CONTEXT_MODE_LEGACY 1U
#define WIFI_CONTEXT_MODE_V2_LITE 2U
#define WIFI_MSG_RING_ENTRIES 32U
#define WIFI_MSG_RING_BYTES 256U
#define WIFI_TFH_NUM_TBS 25U
#define WIFI_DQA_CMD_QUEUE 0U
#define WIFI_CMD_SCD_QUEUE_CFG 0x1dU
#define WIFI_CMD_GROUP_LONG 0x01U
#define WIFI_CMD_VERSION_TX_QUEUE_CFG 2U
#define WIFI_TX_QUEUE_CFG_ENABLE_QUEUE 0x0001U
#define WIFI_CMD_QUEUE_CB_SIZE_VALUE 2U
#define HBUS_TARG_WRPTR 0x460U
#define HBUS_TARG_WRPTR_Q_SHIFT 16U
#define FH_RSCSR_FRAME_SIZE_MSK 0x00003fffU
#define FH_RSCSR_FRAME_INVALID 0x55550000U
#define FH_RSCSR_RXQ_POS 16U
#define FH_RSCSR_RXQ_MASK 0x003f0000U

#define CSR_CTXT_INFO_BA 0x040U
#define CSR_CTXT_INFO_BOOT_CTRL 0x000U
#define CSR_CTXT_INFO_ADDR 0x118U
#define CSR_AUTO_FUNC_BOOT_ENA (1U << 1)
#define CSR_AUTO_FUNC_INIT (1U << 7)
#define IWL_CTXT_INFO_TFD_FORMAT_LONG 0x0100U
#define IWL_CTXT_INFO_RB_CB_SIZE_SHIFT 4U
#define IWL_CTXT_INFO_RB_SIZE_SHIFT 9U
#define IWL_CTXT_INFO_RB_SIZE_2K 2U
#define IWL_PRPH_SCRATCH_MTR_MODE (1U << 17)
#define IWL_PRPH_MTR_FORMAT_256B 0x000c0000U

typedef enum {
  WIFI_FW_IMAGE_RUNTIME = 0,
  WIFI_FW_IMAGE_INIT = 1,
  WIFI_FW_IMAGE_WOWLAN = 2,
  WIFI_FW_IMAGE_BOOT = 3,
} wifi_fw_image_t;

typedef struct {
  uint32_t tlv_type;
  wifi_fw_image_t image;
  uint32_t dst;
  uint32_t len;
  const uint8_t *data;
  int secure;
} wifi_fw_section_t;

typedef struct {
  uint64_t phys;
  uint32_t bytes;
  uint32_t flags;
} wifi_queue_desc_t;

typedef struct __attribute__((packed)) {
  uint16_t rbid;
  uint16_t reserved[3];
  uint64_t addr;
} wifi_rx_transfer_desc_t;

typedef struct __attribute__((packed)) {
  uint32_t reserved1;
  uint16_t rbid;
  uint8_t flags;
  uint8_t reserved2[25];
} wifi_rx_completion_desc_t;

typedef struct __attribute__((packed)) {
  uint8_t cmd;
  uint8_t group_id;
  uint16_t sequence;
} wifi_cmd_header_t;

typedef struct __attribute__((packed)) {
  uint32_t len_n_flags;
  wifi_cmd_header_t header;
} wifi_rx_packet_t;

typedef struct __attribute__((packed)) {
  uint16_t tb_len;
  uint64_t addr;
} wifi_tfh_tb_t;

typedef struct __attribute__((packed)) {
  uint16_t num_tbs;
  wifi_tfh_tb_t tbs[WIFI_TFH_NUM_TBS];
  uint32_t pad;
} wifi_tfh_tfd_t;

typedef struct __attribute__((packed)) {
  uint16_t tfd_offset;
} wifi_bc_tbl_entry_t;

typedef struct __attribute__((packed)) {
  uint8_t cmd;
  uint8_t group_id;
  uint16_t sequence;
  uint16_t length;
  uint8_t reserved;
  uint8_t version;
} wifi_cmd_header_wide_t;

typedef struct __attribute__((packed)) {
  uint8_t sta_id;
  uint8_t tid;
  uint16_t flags;
  uint32_t cb_size;
  uint64_t byte_cnt_addr;
  uint64_t tfdq_addr;
} wifi_tx_queue_cfg_cmd_t;

typedef struct __attribute__((packed)) {
  wifi_cmd_header_wide_t header;
  wifi_tx_queue_cfg_cmd_t queue_cfg;
} wifi_scheduler_cmd_frame_t;

typedef struct __attribute__((packed)) {
  uint16_t mac_id;
  uint16_t version;
  uint16_t size;
  uint16_t reserved;
} wifi_context_version_t;

typedef struct __attribute__((packed)) {
  uint32_t control_flags;
  uint32_t reserved;
} wifi_context_control_t;

typedef struct __attribute__((packed)) {
  uint64_t free_rbd_addr;
  uint64_t used_rbd_addr;
  uint64_t status_wr_ptr;
} wifi_context_rbd_cfg_t;

typedef struct __attribute__((packed)) {
  uint64_t cmd_queue_addr;
  uint8_t cmd_queue_size;
  uint8_t reserved[7];
} wifi_context_hcmd_cfg_t;

typedef struct __attribute__((packed)) {
  uint64_t umac_img[WIFI_CONTEXT_DRAM_ENTRIES];
  uint64_t lmac_img[WIFI_CONTEXT_DRAM_ENTRIES];
  uint64_t virtual_img[WIFI_CONTEXT_DRAM_ENTRIES];
} wifi_context_dram_map_t;

typedef struct __attribute__((packed)) {
  wifi_context_version_t version;
  wifi_context_control_t control;
  uint64_t reserved0;
  wifi_context_rbd_cfg_t rbd_cfg;
  wifi_context_hcmd_cfg_t hcmd_cfg;
  uint32_t reserved1[4];
  uint64_t dump_addr;
  uint32_t dump_size;
  uint32_t dump_reserved;
  uint64_t early_debug_addr;
  uint32_t early_debug_size;
  uint32_t early_debug_reserved;
  uint64_t pnvm_addr;
  uint32_t pnvm_size;
  uint32_t pnvm_reserved;
  uint32_t reserved2[16];
  wifi_context_dram_map_t dram;
  uint32_t reserved3[16];
} wifi_legacy_context_info_t;

typedef struct __attribute__((packed)) {
  uint64_t pnvm_base_addr;
  uint32_t pnvm_size;
  uint32_t reserved;
} wifi_prph_scratch_pnvm_cfg_t;

typedef struct __attribute__((packed)) {
  uint64_t hwm_base_addr;
  uint32_t hwm_size;
  uint32_t debug_token_config;
} wifi_prph_scratch_hwm_cfg_t;

typedef struct __attribute__((packed)) {
  uint64_t free_rbd_addr;
  uint32_t reserved;
} wifi_prph_scratch_rbd_cfg_t;

typedef struct __attribute__((packed)) {
  uint64_t base_addr;
  uint32_t size;
  uint32_t reserved;
} wifi_prph_scratch_uefi_cfg_t;

typedef struct __attribute__((packed)) {
  uint32_t mbx_addr_0;
  uint32_t mbx_addr_1;
} wifi_prph_scratch_step_cfg_t;

typedef struct __attribute__((packed)) {
  wifi_context_version_t version;
  wifi_context_control_t control;
  wifi_prph_scratch_pnvm_cfg_t pnvm_cfg;
  wifi_prph_scratch_hwm_cfg_t hwm_cfg;
  wifi_prph_scratch_rbd_cfg_t rbd_cfg;
  wifi_prph_scratch_uefi_cfg_t reduce_power_cfg;
  wifi_prph_scratch_step_cfg_t step_cfg;
} wifi_prph_scratch_ctrl_cfg_t;

typedef struct __attribute__((packed)) {
  wifi_context_dram_map_t common;
  uint64_t fseq_img[8];
} wifi_context_dram_fseq_t;

typedef struct __attribute__((packed)) {
  wifi_prph_scratch_ctrl_cfg_t ctrl_cfg;
  uint32_t fseq_override;
  uint32_t step_analog_params;
  uint32_t reserved[8];
  wifi_context_dram_fseq_t dram;
} wifi_prph_scratch_v2_t;

typedef struct __attribute__((packed)) {
  uint32_t boot_stage_mirror;
  uint32_t ipc_status_mirror;
  uint32_t sleep_notif;
  uint32_t reserved;
} wifi_prph_info_v2_t;

typedef struct __attribute__((packed)) {
  uint16_t version;
  uint16_t size;
  uint32_t config;
  uint64_t prph_info_base_addr;
  uint64_t cr_head_idx_arr_base_addr;
  uint64_t tr_tail_idx_arr_base_addr;
  uint64_t cr_tail_idx_arr_base_addr;
  uint64_t tr_head_idx_arr_base_addr;
  uint16_t cr_idx_arr_size;
  uint16_t tr_idx_arr_size;
  uint64_t mtr_base_addr;
  uint64_t mcr_base_addr;
  uint16_t mtr_size;
  uint16_t mcr_size;
  uint16_t mtr_doorbell_vec;
  uint16_t mcr_doorbell_vec;
  uint16_t mtr_msi_vec;
  uint16_t mcr_msi_vec;
  uint8_t mtr_opt_header_size;
  uint8_t mtr_opt_footer_size;
  uint8_t mcr_opt_header_size;
  uint8_t mcr_opt_footer_size;
  uint16_t msg_rings_ctrl_flags;
  uint16_t prph_info_msi_vec;
  uint64_t prph_scratch_base_addr;
  uint32_t prph_scratch_size;
  uint32_t reserved;
} wifi_context_info_v2_t;

static volatile uint8_t *wifi_mmio = NULL;
static const uint8_t *wifi_firmware_blob = NULL;
static size_t wifi_firmware_blob_size = 0;
static wifi_fw_section_t wifi_fw_sections[WIFI_FW_MAX_SECTIONS];
static int wifi_fw_section_count = 0;
static uint8_t wifi_dma_chunk[WIFI_DMA_CHUNK_BYTES]
    __attribute__((aligned(4096)));
static uint8_t wifi_cmd_tfd[WIFI_CMD_QUEUE_ENTRIES][WIFI_CMD_TFD_BYTES]
    __attribute__((aligned(4096)));
static uint8_t wifi_tx_tfd[WIFI_TX_QUEUE_ENTRIES][WIFI_TX_TFD_BYTES]
    __attribute__((aligned(4096)));
static wifi_rx_transfer_desc_t wifi_rx_desc[WIFI_RX_QUEUE_ENTRIES]
    __attribute__((aligned(4096)));
static wifi_rx_completion_desc_t wifi_rx_used_desc[WIFI_RX_QUEUE_ENTRIES]
    __attribute__((aligned(4096)));
static uint32_t wifi_rx_status[WIFI_RX_STATUS_WORDS]
    __attribute__((aligned(4096)));
static wifi_bc_tbl_entry_t wifi_cmd_bc_tbl[WIFI_CMD_QUEUE_ENTRIES]
    __attribute__((aligned(4096)));
static wifi_bc_tbl_entry_t wifi_tx_bc_tbl[WIFI_TX_QUEUE_ENTRIES]
    __attribute__((aligned(4096)));
static uint8_t wifi_cmd_buffers[WIFI_CMD_QUEUE_ENTRIES][WIFI_CMD_BUFFER_BYTES]
    __attribute__((aligned(4096)));
static uint8_t wifi_tx_buffers[WIFI_TX_QUEUE_ENTRIES][WIFI_TX_BUFFER_BYTES]
    __attribute__((aligned(4096)));
static uint8_t wifi_rx_buffers[WIFI_RX_QUEUE_ENTRIES][WIFI_RX_BUFFER_BYTES]
    __attribute__((aligned(4096)));
static wifi_legacy_context_info_t wifi_legacy_context
    __attribute__((aligned(4096)));
static wifi_context_info_v2_t wifi_context_v2 __attribute__((aligned(4096)));
static wifi_prph_info_v2_t wifi_prph_info_v2 __attribute__((aligned(4096)));
static wifi_prph_scratch_v2_t wifi_prph_scratch_v2
    __attribute__((aligned(4096)));
static uint8_t wifi_mtr_ring[WIFI_MSG_RING_ENTRIES][WIFI_MSG_RING_BYTES]
    __attribute__((aligned(4096)));
static uint8_t wifi_mcr_ring[WIFI_MSG_RING_ENTRIES][WIFI_MSG_RING_BYTES]
    __attribute__((aligned(4096)));
static uint16_t wifi_cr_head_idx[1] __attribute__((aligned(4096)));
static uint16_t wifi_cr_tail_idx[1] __attribute__((aligned(4096)));
static uint16_t wifi_tr_head_idx[1] __attribute__((aligned(4096)));
static uint16_t wifi_tr_tail_idx[1] __attribute__((aligned(4096)));

extern const uint8_t orizon_iwlwifi_so_a0_hr_b0_89_ucode_start[];
extern const uint8_t orizon_iwlwifi_so_a0_hr_b0_89_ucode_end[];

static const char *intel_fw_candidates[] = {
    "iwlwifi-so-a0-hr-b0-89.ucode", "iwlwifi-so-a0-hr-b0-86.ucode",
    "iwlwifi-so-a0-hr-b0-83.ucode", "iwlwifi-so-a0-hr-b0-77.ucode",
    "iwlwifi-so-a0-hr-b0-74.ucode", "iwlwifi-so-a0-gf-a0-89.ucode",
    "iwlwifi-so-a0-gf-a0-86.ucode", "iwlwifi-so-a0-gf-a0-83.ucode",
    "iwlwifi-QuZ-a0-hr-b0-77.ucode", "iwlwifi-QuZ-a0-hr-b0-74.ucode",
    "iwlwifi-cc-a0-77.ucode",       "iwlwifi-cc-a0-72.ucode",
};

static const char *intel_wifi_name(uint16_t device_id) {
  switch (device_id) {
    case 0x54F0:
      return "Intel Alder Lake-N CNVi Wi-Fi";
    case 0x4DF0:
      return "Intel Wi-Fi 6 AX201/AX211 family";
    case 0xA0F0:
      return "Intel Wi-Fi 6 AX201 family";
    case 0x51F0:
    case 0x7AF0:
      return "Intel Wi-Fi 6E AX211 family";
    case 0x2723:
      return "Intel Wi-Fi 6 AX200/AX201 family";
    case 0x2725:
      return "Intel Wi-Fi 6E AX210 family";
    default:
      return "Intel wireless controller";
  }
}

static int wifi_uses_bz_apm_profile(void);

static void wifi_set_status(const char *status) {
  wifi_status_state.status = status;
  serial_puts("[wifi] ");
  serial_puts(status);
  serial_puts("\n");
}

static int name_has_ucode_suffix(const char *name) {
  size_t len = name ? strlen(name) : 0;
  return len > 6 && strcmp(name + len - 6, ".ucode") == 0;
}

static uint32_t wifi_csr_read32(uint32_t reg) {
  return *(volatile uint32_t *)(uintptr_t)(wifi_mmio + reg);
}

static void wifi_csr_write32(uint32_t reg, uint32_t value) {
  *(volatile uint32_t *)(uintptr_t)(wifi_mmio + reg) = value;
}

static uint32_t wifi_prph_mask(void) {
  switch (wifi_status_state.device_id) {
    case 0x54F0:
    case 0x51F0:
    case 0x7AF0:
    case 0x2725:
    case 0x7740:
    case 0x4D40:
    case 0xA840:
      return PRPH_ADDR_MASK_MODERN;
    default:
      return PRPH_ADDR_MASK_LEGACY;
  }
}

static int wifi_grab_nic_access(void) {
  uint32_t write;
  uint32_t mask;
  uint32_t poll;

  if (!wifi_mmio || !wifi_status_state.apm_ready) {
    return -1;
  }

  if (wifi_uses_bz_apm_profile()) {
    write = CSR_GP_CNTRL_BZ_MAC_ACCESS_REQ;
    mask = CSR_GP_CNTRL_MAC_STATUS;
    poll = CSR_GP_CNTRL_MAC_STATUS;
  } else {
    write = CSR_GP_CNTRL_MAC_ACCESS_REQ;
    mask = CSR_GP_CNTRL_MAC_CLOCK_READY | CSR_GP_CNTRL_GOING_TO_SLEEP;
    poll = CSR_GP_CNTRL_MAC_CLOCK_READY;
  }

  wifi_csr_write32(CSR_GP_CNTRL, wifi_csr_read32(CSR_GP_CNTRL) | write);
  for (uint32_t i = 0; i < WIFI_NIC_ACCESS_POLL_LOOPS; i++) {
    uint32_t gp = wifi_csr_read32(CSR_GP_CNTRL);
    wifi_status_state.boot_last_gp = gp;
    if ((gp & mask) == poll) {
      return 0;
    }
    __asm__ volatile("pause");
  }
  return -1;
}

static void wifi_release_nic_access(void) {
  uint32_t clear_bit;
  uint32_t gp;

  if (!wifi_mmio) {
    return;
  }

  clear_bit = wifi_uses_bz_apm_profile() ? CSR_GP_CNTRL_BZ_MAC_ACCESS_REQ
                                         : CSR_GP_CNTRL_MAC_ACCESS_REQ;
  gp = wifi_csr_read32(CSR_GP_CNTRL) & ~clear_bit;
  wifi_csr_write32(CSR_GP_CNTRL, gp);
  wifi_status_state.boot_last_gp = wifi_csr_read32(CSR_GP_CNTRL);
}

static int wifi_prph_read32(uint32_t addr, uint32_t *value) {
  uint32_t mask;

  if (!value || wifi_grab_nic_access() != 0) {
    return -1;
  }

  mask = wifi_prph_mask();
  wifi_csr_write32(HBUS_TARG_PRPH_RADDR,
                   (addr & mask) | PRPH_ADDR_SPACE_3);
  *value = wifi_csr_read32(HBUS_TARG_PRPH_RDAT);
  wifi_release_nic_access();
  return 0;
}

static int wifi_prph_write32(uint32_t addr, uint32_t value) {
  uint32_t mask;

  if (wifi_grab_nic_access() != 0) {
    return -1;
  }

  mask = wifi_prph_mask();
  wifi_csr_write32(HBUS_TARG_PRPH_WADDR,
                   (addr & mask) | PRPH_ADDR_SPACE_3);
  wifi_csr_write32(HBUS_TARG_PRPH_WDAT, value);
  wifi_release_nic_access();
  return 0;
}

static int wifi_direct_read32(uint32_t reg, uint32_t *value) {
  if (!value || wifi_grab_nic_access() != 0) {
    return -1;
  }

  *value = wifi_csr_read32(reg);
  wifi_release_nic_access();
  return 0;
}

static int wifi_direct_write32(uint32_t reg, uint32_t value) {
  if (wifi_grab_nic_access() != 0) {
    return -1;
  }

  wifi_csr_write32(reg, value);
  wifi_release_nic_access();
  return 0;
}

static uint64_t wifi_phys_addr(const void *ptr) {
  uint64_t v = (uint64_t)(uintptr_t)ptr;
  if (kernel_phys_base && kernel_virt_base && v >= kernel_virt_base) {
    return kernel_phys_base + (v - kernel_virt_base);
  }
  if (hhdm_offset && v >= hhdm_offset) {
    return v - hhdm_offset;
  }
  return 0;
}

static uint32_t wifi_read_le32(const uint8_t *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
         ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static size_t wifi_align4(size_t value) {
  return (value + 3U) & ~(size_t)3U;
}

static int wifi_is_printable_ascii(uint8_t c) {
  return c >= 32 && c <= 126;
}

static void wifi_reset_firmware_parse(void) {
  wifi_status_state.firmware_valid = 0;
  wifi_status_state.firmware_version = 0;
  wifi_status_state.firmware_build = 0;
  wifi_status_state.firmware_tlv_count = 0;
  wifi_status_state.firmware_inst_bytes = 0;
  wifi_status_state.firmware_data_bytes = 0;
  wifi_status_state.firmware_section_count = 0;
  wifi_status_state.firmware_runtime_sections = 0;
  wifi_status_state.firmware_init_sections = 0;
  wifi_status_state.firmware_wowlan_sections = 0;
  wifi_status_state.firmware_secure_sections = 0;
  wifi_status_state.firmware_load_bytes = 0;
  wifi_status_state.firmware_load_chunks = 0;
  wifi_status_state.firmware_largest_section = 0;
  wifi_status_state.firmware_api_count = 0;
  wifi_status_state.firmware_capa_count = 0;
  wifi_status_state.firmware_parse_errors = 0;
  wifi_status_state.firmware_cpu_count = 0;
  wifi_status_state.firmware_first_dst = 0;
  wifi_status_state.firmware_load_plan_ready = 0;
  wifi_status_state.dma_ready = 0;
  wifi_status_state.dma_phys = 0;
  wifi_status_state.dma_chunk_bytes = WIFI_DMA_CHUNK_BYTES;
  wifi_status_state.dma_staged_bytes = 0;
  wifi_status_state.apm_ready = 0;
  wifi_status_state.apm_timeout = 0;
  wifi_status_state.apm_poll_ready_mask = 0;
  wifi_status_state.apm_last_gp = 0;
  wifi_status_state.apm_last_hw_if = 0;
  wifi_status_state.apm_last_gio = 0;
  wifi_status_state.apm_last_hpet = 0;
  wifi_status_state.alive_seen = 0;
  wifi_status_state.alive_timeout = 0;
  wifi_status_state.alive_polls = 0;
  wifi_status_state.alive_errors = 0;
  wifi_status_state.alive_last_csr_int = 0;
  wifi_status_state.alive_last_fh_int = 0;
  wifi_status_state.alive_last_gp = 0;
  wifi_status_state.boot_ready = 0;
  wifi_status_state.boot_released = 0;
  wifi_status_state.boot_failed = 0;
  wifi_status_state.boot_cpu1_sections = 0;
  wifi_status_state.boot_cpu2_sections = 0;
  wifi_status_state.boot_paging_sections_skipped = 0;
  wifi_status_state.boot_sections_loaded = 0;
  wifi_status_state.boot_release_value = 0;
  wifi_status_state.boot_fh_status = 0;
  wifi_status_state.boot_ureg_status = 0;
  wifi_status_state.boot_first_cpu2_index = 0;
  wifi_status_state.boot_paging_index = 0;
  wifi_status_state.boot_last_gp = 0;
  wifi_status_state.queues_ready = 0;
  wifi_status_state.queues_armed = 0;
  wifi_status_state.queues_failed = 0;
  wifi_status_state.queue_errors = 0;
  wifi_status_state.queue_generation = 0;
  wifi_status_state.cmd_queue_entries = 0;
  wifi_status_state.tx_queue_entries = 0;
  wifi_status_state.rx_queue_entries = 0;
  wifi_status_state.cmd_buffer_bytes = 0;
  wifi_status_state.tx_buffer_bytes = 0;
  wifi_status_state.rx_buffer_bytes = 0;
  wifi_status_state.cmd_tfd_bytes = 0;
  wifi_status_state.tx_tfd_bytes = 0;
  wifi_status_state.rx_desc_bytes = 0;
  wifi_status_state.cmd_tfd_phys = 0;
  wifi_status_state.cmd_buffer_phys = 0;
  wifi_status_state.tx_tfd_phys = 0;
  wifi_status_state.tx_buffer_phys = 0;
  wifi_status_state.rx_desc_phys = 0;
  wifi_status_state.rx_used_desc_phys = 0;
  wifi_status_state.rx_status_phys = 0;
  wifi_status_state.rx_buffer_phys = 0;
  wifi_status_state.cmd_write_ptr = 0;
  wifi_status_state.tx_write_ptr = 0;
  wifi_status_state.rx_write_ptr = 0;
  wifi_status_state.context_ready = 0;
  wifi_status_state.context_armed = 0;
  wifi_status_state.context_failed = 0;
  wifi_status_state.context_mode = 0;
  wifi_status_state.context_errors = 0;
  wifi_status_state.context_generation = 0;
  wifi_status_state.context_lmac_sections = 0;
  wifi_status_state.context_umac_sections = 0;
  wifi_status_state.context_paging_sections = 0;
  wifi_status_state.legacy_context_phys = 0;
  wifi_status_state.v2_context_phys = 0;
  wifi_status_state.prph_info_phys = 0;
  wifi_status_state.prph_scratch_phys = 0;
  wifi_status_state.context_size = 0;
  wifi_status_state.context_v2_size = 0;
  wifi_status_state.prph_scratch_size = 0;
  wifi_status_state.context_control_flags = 0;
  wifi_status_state.context_v2_control_flags = 0;
  wifi_status_state.context_csr_target = 0;
  wifi_status_state.context_csr_value_lo = 0;
  wifi_status_state.context_csr_value_hi = 0;
  wifi_status_state.scheduler_ready = 0;
  wifi_status_state.scheduler_armed = 0;
  wifi_status_state.scheduler_failed = 0;
  wifi_status_state.scheduler_errors = 0;
  wifi_status_state.scheduler_generation = 0;
  wifi_status_state.cmd_bc_phys = 0;
  wifi_status_state.tx_bc_phys = 0;
  wifi_status_state.mtr_ring_phys = 0;
  wifi_status_state.mcr_ring_phys = 0;
  wifi_status_state.cr_head_phys = 0;
  wifi_status_state.cr_tail_phys = 0;
  wifi_status_state.tr_head_phys = 0;
  wifi_status_state.tr_tail_phys = 0;
  wifi_status_state.mtr_entries = 0;
  wifi_status_state.mcr_entries = 0;
  wifi_status_state.msg_ring_bytes = 0;
  wifi_status_state.scheduler_cmd_id = 0;
  wifi_status_state.scheduler_cmd_group = 0;
  wifi_status_state.scheduler_cmd_version = 0;
  wifi_status_state.scheduler_cmd_len = 0;
  wifi_status_state.scheduler_cmd_sequence = 0;
  wifi_status_state.scheduler_cmd_queue = 0;
  wifi_status_state.scheduler_cmd_index = 0;
  wifi_status_state.scheduler_cmd_tbs = 0;
  wifi_status_state.scheduler_cmd_tb_len = 0;
  wifi_status_state.scheduler_wptr_value = 0;
  wifi_status_state.rx_path_ready = 0;
  wifi_status_state.rx_path_failed = 0;
  wifi_status_state.rx_path_errors = 0;
  wifi_status_state.rx_polls = 0;
  wifi_status_state.rx_packets = 0;
  wifi_status_state.rx_closed_rb = 0;
  wifi_status_state.rx_read_ptr = 0;
  wifi_status_state.rx_last_rbid = 0;
  wifi_status_state.rx_last_flags = 0;
  wifi_status_state.rx_last_len_n_flags = 0;
  wifi_status_state.rx_last_len = 0;
  wifi_status_state.rx_last_queue = 0;
  wifi_status_state.rx_last_cmd = 0;
  wifi_status_state.rx_last_group = 0;
  wifi_status_state.rx_last_sequence = 0;
  wifi_status_state.rx_last_index = 0;
  wifi_status_state.command_ready = 0;
  wifi_status_state.command_sent = 0;
  wifi_status_state.command_failed = 0;
  wifi_status_state.command_attempts = 0;
  wifi_status_state.command_errors = 0;
  wifi_status_state.command_doorbell_value = 0;
  wifi_status_state.command_last_csr_int = 0;
  wifi_status_state.command_last_fh_int = 0;
  wifi_status_state.command_last_closed_rb = 0;
  wifi_status_state.command_poll_loops = 0;
  wifi_status_state.fh_plan_ready = 0;
  wifi_status_state.fh_armed = 0;
  wifi_status_state.fh_complete = 0;
  wifi_status_state.fh_timeout = 0;
  wifi_status_state.fh_errors = 0;
  wifi_status_state.fh_channel = 0;
  wifi_status_state.fh_dst_addr = 0;
  wifi_status_state.fh_byte_count = 0;
  wifi_status_state.fh_ctrl0_value = 0;
  wifi_status_state.fh_ctrl1_value = 0;
  wifi_status_state.fh_buf_status_value = 0;
  wifi_status_state.fh_tx_config_value = 0;
  wifi_status_state.fh_last_csr_int = 0;
  wifi_status_state.fh_last_fh_int = 0;
  wifi_status_state.fh_last_tx_status = 0;
  wifi_status_state.fh_last_tx_error = 0;
  wifi_status_state.fh_total_chunks = 0;
  wifi_status_state.fh_uploaded_chunks = 0;
  wifi_status_state.fh_uploaded_bytes = 0;
  wifi_status_state.fh_sections_done = 0;
  wifi_status_state.fh_last_section = 0;
  wifi_status_state.fh_last_offset = 0;
  wifi_status_state.fh_last_len = 0;
  wifi_status_state.fh_all_plan_ready = 0;
  wifi_status_state.load_state = "wifi: firmware loader idle";
  memset(wifi_status_state.firmware_human, 0,
         sizeof(wifi_status_state.firmware_human));
  wifi_fw_section_count = 0;
  memset(wifi_fw_sections, 0, sizeof(wifi_fw_sections));
}

static const char *wifi_bit_text(uint32_t value, uint32_t bit) {
  return (value & bit) ? "set" : "clear";
}

static const char *wifi_fw_image_name(wifi_fw_image_t image) {
  switch (image) {
    case WIFI_FW_IMAGE_RUNTIME:
      return "runtime";
    case WIFI_FW_IMAGE_INIT:
      return "init";
    case WIFI_FW_IMAGE_WOWLAN:
      return "wowlan";
    case WIFI_FW_IMAGE_BOOT:
      return "boot";
    default:
      return "unknown";
  }
}

static void wifi_count_fw_image(wifi_fw_image_t image) {
  switch (image) {
    case WIFI_FW_IMAGE_RUNTIME:
      wifi_status_state.firmware_runtime_sections++;
      break;
    case WIFI_FW_IMAGE_INIT:
      wifi_status_state.firmware_init_sections++;
      break;
    case WIFI_FW_IMAGE_WOWLAN:
      wifi_status_state.firmware_wowlan_sections++;
      break;
    default:
      break;
  }
}

static int wifi_fw_section_kind(uint32_t type, wifi_fw_image_t *image,
                                uint32_t *dst, int *secure,
                                int *is_data) {
  *secure = 0;
  *is_data = 0;
  switch (type) {
    case IWL_UCODE_TLV_INST:
      *image = WIFI_FW_IMAGE_RUNTIME;
      *dst = IWLAGN_RTC_INST_LOWER_BOUND;
      return 1;
    case IWL_UCODE_TLV_DATA:
      *image = WIFI_FW_IMAGE_RUNTIME;
      *dst = IWLAGN_RTC_DATA_LOWER_BOUND;
      *is_data = 1;
      return 1;
    case IWL_UCODE_TLV_INIT:
      *image = WIFI_FW_IMAGE_INIT;
      *dst = IWLAGN_RTC_INST_LOWER_BOUND;
      return 1;
    case IWL_UCODE_TLV_INIT_DATA:
      *image = WIFI_FW_IMAGE_INIT;
      *dst = IWLAGN_RTC_DATA_LOWER_BOUND;
      *is_data = 1;
      return 1;
    case IWL_UCODE_TLV_WOWLAN_INST:
      *image = WIFI_FW_IMAGE_WOWLAN;
      *dst = IWLAGN_RTC_INST_LOWER_BOUND;
      return 1;
    case IWL_UCODE_TLV_WOWLAN_DATA:
      *image = WIFI_FW_IMAGE_WOWLAN;
      *dst = IWLAGN_RTC_DATA_LOWER_BOUND;
      *is_data = 1;
      return 1;
    case IWL_UCODE_TLV_SEC_RT:
    case IWL_UCODE_TLV_SECURE_SEC_RT:
      *image = WIFI_FW_IMAGE_RUNTIME;
      *secure = 1;
      return 1;
    case IWL_UCODE_TLV_SEC_INIT:
    case IWL_UCODE_TLV_SECURE_SEC_INIT:
      *image = WIFI_FW_IMAGE_INIT;
      *secure = 1;
      return 1;
    case IWL_UCODE_TLV_SEC_WOWLAN:
    case IWL_UCODE_TLV_SECURE_SEC_WOWLAN:
      *image = WIFI_FW_IMAGE_WOWLAN;
      *secure = 1;
      return 1;
    default:
      return 0;
  }
}

static void wifi_record_fw_section(uint32_t type, const uint8_t *data,
                                   uint32_t len) {
  wifi_fw_image_t image = WIFI_FW_IMAGE_RUNTIME;
  uint32_t dst = 0;
  int secure = 0;
  int is_data = 0;
  uint32_t payload_len = len;
  const uint8_t *payload = data;

  if (!wifi_fw_section_kind(type, &image, &dst, &secure, &is_data)) {
    return;
  }

  if (secure) {
    if (len < 4) {
      wifi_status_state.firmware_parse_errors++;
      return;
    }
    dst = wifi_read_le32(data);
    payload = data + 4;
    payload_len = len - 4;
    wifi_status_state.firmware_secure_sections++;
  }

  if (payload_len == 0) {
    return;
  }

  if (wifi_fw_section_count >= WIFI_FW_MAX_SECTIONS) {
    wifi_status_state.firmware_parse_errors++;
    return;
  }

  wifi_fw_sections[wifi_fw_section_count].tlv_type = type;
  wifi_fw_sections[wifi_fw_section_count].image = image;
  wifi_fw_sections[wifi_fw_section_count].dst = dst;
  wifi_fw_sections[wifi_fw_section_count].len = payload_len;
  wifi_fw_sections[wifi_fw_section_count].data = payload;
  wifi_fw_sections[wifi_fw_section_count].secure = secure;
  wifi_fw_section_count++;

  wifi_status_state.firmware_section_count++;
  wifi_count_fw_image(image);
  wifi_status_state.firmware_load_bytes += payload_len;
  wifi_status_state.firmware_load_chunks +=
      (payload_len + WIFI_DMA_CHUNK_BYTES - 1U) / WIFI_DMA_CHUNK_BYTES;
  if (payload_len > wifi_status_state.firmware_largest_section) {
    wifi_status_state.firmware_largest_section = payload_len;
  }
  if (wifi_fw_section_count == 1) {
    wifi_status_state.firmware_first_dst = dst;
  }
  if (secure || !is_data) {
    wifi_status_state.firmware_inst_bytes += payload_len;
  } else {
    wifi_status_state.firmware_data_bytes += payload_len;
  }
}

static void wifi_record_fw_tlv(uint32_t type, const uint8_t *data,
                               uint32_t len) {
  if (type == IWL_UCODE_TLV_API_CHANGES_SET) {
    wifi_status_state.firmware_api_count++;
  } else if (type == IWL_UCODE_TLV_ENABLED_CAPABILITIES) {
    wifi_status_state.firmware_capa_count++;
  } else if (type == IWL_UCODE_TLV_NUM_OF_CPU && len >= 4) {
    wifi_status_state.firmware_cpu_count = wifi_read_le32(data);
  }
  wifi_record_fw_section(type, data, len);
}

static void wifi_parse_firmware_blob(const void *data, size_t size) {
  const uint8_t *bytes = (const uint8_t *)data;
  size_t offset;
  uint32_t zero;
  uint32_t magic;

  wifi_reset_firmware_parse();

  if (!bytes || size < IWL_TLV_HEADER_SIZE) {
    wifi_status_state.firmware_parse_errors++;
    return;
  }

  zero = wifi_read_le32(bytes);
  magic = wifi_read_le32(bytes + 4);
  if (zero != 0 || magic != IWL_TLV_UCODE_MAGIC) {
    wifi_status_state.firmware_parse_errors++;
    return;
  }

  for (size_t i = 0; i < 64; i++) {
    uint8_t c = bytes[8 + i];
    wifi_status_state.firmware_human[i] =
        wifi_is_printable_ascii(c) ? (char)c : '\0';
    if (c == '\0') {
      break;
    }
  }
  for (int i = 63; i >= 0; i--) {
    if (wifi_status_state.firmware_human[i] == ' ') {
      wifi_status_state.firmware_human[i] = '\0';
      continue;
    }
    if (wifi_status_state.firmware_human[i] != '\0') {
      break;
    }
  }

  wifi_status_state.firmware_version = wifi_read_le32(bytes + 72);
  wifi_status_state.firmware_build = wifi_read_le32(bytes + 76);

  offset = IWL_TLV_HEADER_SIZE;
  while (offset + 8 <= size) {
    uint32_t type = wifi_read_le32(bytes + offset);
    uint32_t len = wifi_read_le32(bytes + offset + 4);
    const uint8_t *tlv_data;
    size_t next;

    offset += 8;
    if ((size_t)len > size - offset) {
      wifi_status_state.firmware_parse_errors++;
      break;
    }

    tlv_data = bytes + offset;
    wifi_status_state.firmware_tlv_count++;
    wifi_record_fw_tlv(type, tlv_data, len);

    next = offset + wifi_align4((size_t)len);
    if (next < offset || next > size) {
      wifi_status_state.firmware_parse_errors++;
      break;
    }
    offset = next;
  }

  if (offset != size && offset < size) {
    wifi_status_state.firmware_parse_errors++;
  }

  wifi_status_state.firmware_valid =
      wifi_status_state.firmware_tlv_count > 0 &&
      wifi_status_state.firmware_parse_errors == 0;
  wifi_status_state.firmware_load_plan_ready =
      wifi_status_state.firmware_valid && wifi_fw_section_count > 0 &&
      wifi_status_state.firmware_load_bytes > 0;
}

static int firmware_name_is_candidate(const char *name) {
  if (!name || !name[0]) {
    return 0;
  }
  for (size_t i = 0; i < sizeof(intel_fw_candidates) /
                             sizeof(intel_fw_candidates[0]);
       i++) {
    if (strcmp(name, intel_fw_candidates[i]) == 0) {
      return 1;
    }
  }
  return strstr(name, "iwlwifi-") != NULL && name_has_ucode_suffix(name);
}

static int wifi_find_firmware_module(void) {
  const void *addr = NULL;
  size_t size = 0;
  const char *path = NULL;
  const char *cmdline = NULL;

  for (size_t i = 0; i < sizeof(intel_fw_candidates) /
                             sizeof(intel_fw_candidates[0]);
       i++) {
    if (boot_find_module(intel_fw_candidates[i], &addr, &size, &path,
                         &cmdline) == 0) {
      UNUSED(cmdline);
      wifi_status_state.firmware_present = 1;
      wifi_status_state.firmware_size = size;
      wifi_status_state.firmware_name = intel_fw_candidates[i];
      wifi_status_state.firmware_source = path && path[0] ? path : "boot module";
      wifi_firmware_blob = (const uint8_t *)addr;
      wifi_firmware_blob_size = size;
      wifi_parse_firmware_blob(addr, size);
      return 0;
    }
  }

  if (boot_find_module("iwlwifi-", &addr, &size, &path, &cmdline) == 0 &&
      path && name_has_ucode_suffix(path)) {
    UNUSED(cmdline);
    wifi_status_state.firmware_present = 1;
    wifi_status_state.firmware_size = size;
    wifi_status_state.firmware_name = path;
    wifi_status_state.firmware_source = "boot module";
    wifi_firmware_blob = (const uint8_t *)addr;
    wifi_firmware_blob_size = size;
    wifi_parse_firmware_blob(addr, size);
    return 0;
  }
  return -1;
}

static int wifi_find_firmware_embedded(void) {
  const uint8_t *start = orizon_iwlwifi_so_a0_hr_b0_89_ucode_start;
  const uint8_t *end = orizon_iwlwifi_so_a0_hr_b0_89_ucode_end;
  size_t size;

  if (!start || !end || end <= start) {
    return -1;
  }

  size = (size_t)(end - start);
  if (size < IWL_TLV_HEADER_SIZE) {
    return -1;
  }

  wifi_status_state.firmware_present = 1;
  wifi_status_state.firmware_size = size;
  wifi_status_state.firmware_name = "iwlwifi-so-a0-hr-b0-89.ucode";
  wifi_status_state.firmware_source = "kernel embedded";
  wifi_firmware_blob = start;
  wifi_firmware_blob_size = size;
  wifi_parse_firmware_blob(start, size);
  return wifi_status_state.firmware_valid ? 0 : -1;
}

static int wifi_find_firmware_vfs_dir(const char *dir) {
  dirent_t entries[24];
  int count = vfs_readdir(dir, entries, 24);

  if (count <= 0) {
    return -1;
  }

  for (int i = 0; i < count; i++) {
    char path[MAX_PATH];
    size_t size = 0;
    int is_dir = 0;

    if (entries[i].type != 0 || !firmware_name_is_candidate(entries[i].name)) {
      continue;
    }
    snprintf(path, sizeof(path), "%s/%s", dir, entries[i].name);
    if (vfs_stat(path, &size, &is_dir) == 0 && !is_dir && size > 0) {
      wifi_status_state.firmware_present = 1;
      wifi_status_state.firmware_size = size;
      wifi_status_state.firmware_name = entries[i].name;
      wifi_status_state.firmware_source = dir;
      wifi_firmware_blob = NULL;
      wifi_firmware_blob_size = 0;
      wifi_reset_firmware_parse();
      return 0;
    }
  }
  return -1;
}

static int wifi_find_firmware(void) {
  wifi_status_state.firmware_present = 0;
  wifi_status_state.firmware_valid = 0;
  wifi_status_state.firmware_size = 0;
  wifi_status_state.firmware_name = "none";
  wifi_status_state.firmware_source = "none";
  wifi_firmware_blob = NULL;
  wifi_firmware_blob_size = 0;
  wifi_reset_firmware_parse();

  if (wifi_find_firmware_module() == 0) {
    return 0;
  }
  if (wifi_find_firmware_embedded() == 0) {
    return 0;
  }
  if (wifi_find_firmware_vfs_dir("/system/firmware") == 0 ||
      wifi_find_firmware_vfs_dir("/packages/firmware") == 0) {
    return 0;
  }
  return -1;
}

static uint64_t wifi_bar0_phys(uint32_t bar0, uint32_t bar1) {
  uint64_t phys;

  if (bar0 & 0x1U) {
    return 0;
  }

  phys = (uint64_t)(bar0 & ~0xFULL);
  if ((bar0 & 0x6U) == 0x4U) {
    phys |= ((uint64_t)bar1 << 32);
  }
  return phys;
}

static void wifi_capture_csr_registers(void) {
  wifi_status_state.csr_hw_if_config = wifi_csr_read32(CSR_HW_IF_CONFIG_REG);
  wifi_status_state.csr_hw_rev = wifi_csr_read32(CSR_HW_REV);
  wifi_status_state.csr_hw_rf_id = wifi_csr_read32(CSR_HW_RF_ID);
  wifi_status_state.csr_gp_cntrl = wifi_csr_read32(CSR_GP_CNTRL);
  wifi_status_state.csr_gpio_in = wifi_csr_read32(CSR_GPIO_IN);
  wifi_status_state.csr_reset = wifi_csr_read32(CSR_RESET);
  wifi_status_state.csr_int = wifi_csr_read32(CSR_INT);
  wifi_status_state.csr_int_mask = wifi_csr_read32(CSR_INT_MASK);
  wifi_status_state.csr_fh_int_status = wifi_csr_read32(CSR_FH_INT_STATUS);
}

static int wifi_probe_mmio(void) {
  uint32_t bar0;
  uint32_t bar1;
  uint32_t cmd;
  uint64_t phys;

  wifi_status_state.mmio_probed = 1;

  if (!wifi_status_state.present || wifi_status_state.vendor_id != 0x8086) {
    wifi_status_state.mmio_ready = 0;
    wifi_status_state.mmio_errors++;
    return -1;
  }

  bar0 = pci_read32(wifi_status_state.bus, wifi_status_state.device,
                    wifi_status_state.function, 0x10);
  bar1 = pci_read32(wifi_status_state.bus, wifi_status_state.device,
                    wifi_status_state.function, 0x14);
  phys = wifi_bar0_phys(bar0, bar1);
  wifi_status_state.mmio_phys = phys;
  if (!phys) {
    wifi_status_state.mmio_ready = 0;
    wifi_status_state.mmio_errors++;
    wifi_set_status("wifi: Intel MMIO BAR missing");
    return -1;
  }

  cmd = pci_read32(wifi_status_state.bus, wifi_status_state.device,
                   wifi_status_state.function, PCI_COMMAND_REG);
  if ((cmd & PCI_COMMAND_MEMORY_SPACE) == 0) {
    pci_write32(wifi_status_state.bus, wifi_status_state.device,
                wifi_status_state.function, PCI_COMMAND_REG,
                cmd | PCI_COMMAND_MEMORY_SPACE);
    cmd = pci_read32(wifi_status_state.bus, wifi_status_state.device,
                     wifi_status_state.function, PCI_COMMAND_REG);
  }
  wifi_status_state.pci_command = cmd & 0xFFFFU;

  if (!wifi_mmio) {
    wifi_mmio = (volatile uint8_t *)(uintptr_t)mmio_map_range(phys, 0x2000);
  }
  if (!wifi_mmio) {
    wifi_status_state.mmio_ready = 0;
    wifi_status_state.mmio_errors++;
    wifi_set_status("wifi: Intel MMIO map failed");
    return -1;
  }

  /*
   * Keep interrupts masked during staging. The actual IRQ/MSI path should only
   * be enabled after firmware upload, queue setup, and an alive notification.
   */
  wifi_csr_write32(CSR_INT_MASK, 0);
  wifi_capture_csr_registers();
  wifi_status_state.mmio_ready = 1;
  wifi_status_state.status =
      "wifi: Intel CSR MMIO ready; firmware upload pending";
  return 0;
}

static int wifi_enable_bus_master_for_loader(void) {
  uint32_t cmd;

  cmd = pci_read32(wifi_status_state.bus, wifi_status_state.device,
                   wifi_status_state.function, PCI_COMMAND_REG);
  if ((cmd & (PCI_COMMAND_MEMORY_SPACE | PCI_COMMAND_BUS_MASTER)) !=
      (PCI_COMMAND_MEMORY_SPACE | PCI_COMMAND_BUS_MASTER)) {
    pci_write32(wifi_status_state.bus, wifi_status_state.device,
                wifi_status_state.function, PCI_COMMAND_REG,
                cmd | PCI_COMMAND_MEMORY_SPACE | PCI_COMMAND_BUS_MASTER);
    cmd = pci_read32(wifi_status_state.bus, wifi_status_state.device,
                     wifi_status_state.function, PCI_COMMAND_REG);
  }
  wifi_status_state.pci_command = cmd & 0xFFFFU;
  return ((cmd & (PCI_COMMAND_MEMORY_SPACE | PCI_COMMAND_BUS_MASTER)) ==
          (PCI_COMMAND_MEMORY_SPACE | PCI_COMMAND_BUS_MASTER))
             ? 0
             : -1;
}

static int wifi_uses_bz_apm_profile(void) {
  switch (wifi_status_state.device_id) {
    case 0xA840:
    case 0x7740:
    case 0x4D40:
      return 1;
    default:
      return 0;
  }
}

static void wifi_capture_apm_registers(void) {
  if (!wifi_mmio) {
    return;
  }

  wifi_status_state.apm_last_gp = wifi_csr_read32(CSR_GP_CNTRL);
  wifi_status_state.apm_last_hw_if = wifi_csr_read32(CSR_HW_IF_CONFIG_REG);
  wifi_status_state.apm_last_gio = wifi_csr_read32(CSR_GIO_CHICKEN_BITS);
  wifi_status_state.apm_last_hpet = wifi_csr_read32(CSR_DBG_HPET_MEM_REG);
  wifi_capture_csr_registers();
}

static void wifi_apply_safe_apm_config(void) {
  uint32_t value;

  /*
   * Keep this intentionally small: these are the Linux iwlwifi PCIe staging
   * knobs that do not require command queues or firmware-side protocols yet.
   */
  value = wifi_csr_read32(CSR_GIO_CHICKEN_BITS);
  wifi_csr_write32(CSR_GIO_CHICKEN_BITS,
                   value | CSR_GIO_CHICKEN_BITS_L1A_NO_L0S_RX);

  value = wifi_csr_read32(CSR_DBG_HPET_MEM_REG);
  wifi_csr_write32(CSR_DBG_HPET_MEM_REG, value | CSR_DBG_HPET_MEM_REG_VAL);

  value = wifi_csr_read32(CSR_HW_IF_CONFIG_REG);
  wifi_csr_write32(CSR_HW_IF_CONFIG_REG,
                   value | CSR_HW_IF_CONFIG_REG_HAP_WAKE);
}

static int wifi_wait_apm_ready(uint32_t ready_mask) {
  for (uint32_t i = 0; i < WIFI_APM_POLL_LOOPS; i++) {
    uint32_t gp = wifi_csr_read32(CSR_GP_CNTRL);
    wifi_status_state.apm_last_gp = gp;
    if (gp & ready_mask) {
      return 0;
    }
    __asm__ volatile("pause");
  }
  return -1;
}

static int wifi_activate_nic(void) {
  uint32_t request_bits;
  uint32_t ready_mask;

  if (wifi_probe_mmio() != 0) {
    return -1;
  }

  if (wifi_enable_bus_master_for_loader() != 0) {
    wifi_status_state.load_state =
        "wifi: PCI bus mastering failed during APM wake";
    wifi_status_state.apm_timeout = 1;
    wifi_capture_apm_registers();
    return -1;
  }

  wifi_status_state.apm_attempts++;
  wifi_status_state.apm_ready = 0;
  wifi_status_state.apm_timeout = 0;

  wifi_apply_safe_apm_config();

  if (wifi_uses_bz_apm_profile()) {
    request_bits = CSR_GP_CNTRL_BZ_MAC_ACCESS_REQ | CSR_GP_CNTRL_MAC_INIT;
    ready_mask = CSR_GP_CNTRL_MAC_STATUS;
  } else {
    request_bits = CSR_GP_CNTRL_INIT_DONE;
    ready_mask = CSR_GP_CNTRL_MAC_CLOCK_READY;
  }

  wifi_status_state.apm_poll_ready_mask = ready_mask;
  wifi_csr_write32(CSR_INT,
                   CSR_INT_BIT_ALIVE | CSR_INT_BIT_FH_TX |
                       CSR_INT_BIT_HW_ERR | CSR_INT_BIT_SW_ERR);
  wifi_csr_write32(CSR_FH_INT_STATUS, FH_INT_TX_MASK | FH_INT_ERR_MASK);
  wifi_csr_write32(CSR_GP_CNTRL,
                   wifi_csr_read32(CSR_GP_CNTRL) | request_bits);

  if (wifi_wait_apm_ready(ready_mask) == 0) {
    wifi_status_state.apm_ready = 1;
    wifi_status_state.apm_timeout = 0;
    wifi_status_state.status =
        "wifi: NIC APM awake; firmware service DMA can be tested";
    wifi_capture_apm_registers();
    return 0;
  }

  wifi_status_state.apm_ready = 0;
  wifi_status_state.apm_timeout = 1;
  wifi_status_state.status = "wifi: NIC APM wake timed out";
  wifi_capture_apm_registers();
  return -1;
}

static const wifi_fw_section_t *wifi_first_upload_section(void) {
  if (wifi_fw_section_count <= 0) {
    return NULL;
  }

  for (int i = 0; i < wifi_fw_section_count; i++) {
    if (wifi_fw_sections[i].image == WIFI_FW_IMAGE_INIT) {
      return &wifi_fw_sections[i];
    }
  }
  return &wifi_fw_sections[0];
}

static int wifi_section_index(const wifi_fw_section_t *section) {
  if (!section) {
    return -1;
  }
  for (int i = 0; i < wifi_fw_section_count; i++) {
    if (section == &wifi_fw_sections[i]) {
      return i;
    }
  }
  return -1;
}

static int wifi_section_is_separator(const wifi_fw_section_t *section) {
  return section &&
         (section->dst == CPU1_CPU2_SEPARATOR_SECTION ||
          section->dst == PAGING_SEPARATOR_SECTION);
}

static unsigned long wifi_count_total_fw_chunks(void) {
  unsigned long chunks = 0;
  for (int i = 0; i < wifi_fw_section_count; i++) {
    if (wifi_section_is_separator(&wifi_fw_sections[i])) {
      continue;
    }
    chunks += (wifi_fw_sections[i].len + WIFI_DMA_CHUNK_BYTES - 1U) /
              WIFI_DMA_CHUNK_BYTES;
  }
  return chunks;
}

static unsigned long wifi_count_boot_fw_chunks(void) {
  unsigned long chunks = 0;
  for (int i = 0; i < wifi_fw_section_count; i++) {
    if (wifi_fw_sections[i].dst == PAGING_SEPARATOR_SECTION) {
      break;
    }
    if (wifi_section_is_separator(&wifi_fw_sections[i])) {
      continue;
    }
    chunks += (wifi_fw_sections[i].len + WIFI_DMA_CHUNK_BYTES - 1U) /
              WIFI_DMA_CHUNK_BYTES;
  }
  return chunks;
}

static unsigned long wifi_count_boot_fw_bytes(void) {
  unsigned long bytes = 0;
  for (int i = 0; i < wifi_fw_section_count; i++) {
    if (wifi_fw_sections[i].dst == PAGING_SEPARATOR_SECTION) {
      break;
    }
    if (wifi_section_is_separator(&wifi_fw_sections[i])) {
      continue;
    }
    bytes += wifi_fw_sections[i].len;
  }
  return bytes;
}

static int wifi_stage_dma_chunk(const wifi_fw_section_t *section,
                                uint32_t offset) {
  uint32_t copy_len;
  uint64_t phys;

  if (!section || offset >= section->len) {
    return -1;
  }

  copy_len = section->len - offset;
  if (copy_len > WIFI_DMA_CHUNK_BYTES) {
    copy_len = WIFI_DMA_CHUNK_BYTES;
  }

  memset(wifi_dma_chunk, 0, sizeof(wifi_dma_chunk));
  memcpy(wifi_dma_chunk, section->data + offset, copy_len);

  phys = wifi_phys_addr(wifi_dma_chunk);
  if (!phys) {
    wifi_status_state.dma_ready = 0;
    return -1;
  }

  wifi_status_state.dma_ready = 1;
  wifi_status_state.dma_phys = phys;
  wifi_status_state.dma_chunk_bytes = WIFI_DMA_CHUNK_BYTES;
  wifi_status_state.dma_staged_bytes = copy_len;
  wifi_status_state.fh_dst_addr = section->dst + offset;
  wifi_status_state.fh_byte_count = copy_len;
  wifi_status_state.fh_last_offset = offset;
  wifi_status_state.fh_last_len = copy_len;
  return 0;
}

static int wifi_stage_first_dma_chunk(const wifi_fw_section_t **section_out) {
  const wifi_fw_section_t *section = wifi_first_upload_section();

  if (!section || wifi_stage_dma_chunk(section, 0) != 0) {
    return -1;
  }
  if (section_out) {
    *section_out = section;
  }
  return 0;
}

static uint32_t wifi_dma_hi_addr(uint64_t phys) {
  return (uint32_t)((phys >> 32) & FH_MEM_TFDIB_REG1_ADDR_MASK);
}

static uint32_t wifi_fh_buf_status_value(void) {
  return FH_TCSR_CHNL_TX_BUF_STS_REG_VAL_TFDB_VALID |
         (1U << FH_TCSR_CHNL_TX_BUF_STS_REG_BIT_TFDB_WPTR) |
         (1U << FH_TCSR_CHNL_TX_BUF_STS_REG_POS_TB_NUM) |
         (1U << FH_TCSR_CHNL_TX_BUF_STS_REG_POS_TB_IDX);
}

static uint32_t wifi_fh_tx_config_enable_value(void) {
  return FH_TCSR_TX_CONFIG_REG_VAL_DMA_CHNL_ENABLE |
         FH_TCSR_TX_CONFIG_REG_VAL_DMA_CREDIT_DISABLE |
         FH_TCSR_TX_CONFIG_REG_VAL_CIRQ_HOST_ENDTFD |
         FH_TCSR_TX_CONFIG_REG_VAL_CIRQ_RTC_NOINT;
}

static int wifi_prepare_fh_plan_for(const wifi_fw_section_t *section,
                                    uint32_t offset,
                                    const wifi_fw_section_t **section_out) {
  uint32_t byte_count;
  int section_index;

  if (!section || wifi_section_is_separator(section) ||
      wifi_stage_dma_chunk(section, offset) != 0) {
    return -1;
  }

  byte_count = (uint32_t)wifi_status_state.dma_staged_bytes;
  if (byte_count == 0 || byte_count > FH_MEM_TFDIB_REG1_LENGTH_MASK) {
    wifi_status_state.fh_errors++;
    return -1;
  }

  section_index = wifi_section_index(section);
  wifi_status_state.fh_channel = FH_SRVC_CHNL;
  wifi_status_state.fh_byte_count = byte_count;
  wifi_status_state.fh_last_section =
      section_index >= 0 ? (uint32_t)section_index : 0;
  wifi_status_state.fh_ctrl0_value =
      (uint32_t)(wifi_status_state.dma_phys &
                 FH_MEM_TFDIB_DRAM_ADDR_LSB_MSK);
  wifi_status_state.fh_ctrl1_value =
      ((wifi_dma_hi_addr(wifi_status_state.dma_phys)
        & FH_MEM_TFDIB_REG1_ADDR_MASK)
       << FH_MEM_TFDIB_REG1_ADDR_BITSHIFT) |
      (byte_count & FH_MEM_TFDIB_REG1_LENGTH_MASK);
  wifi_status_state.fh_buf_status_value = wifi_fh_buf_status_value();
  wifi_status_state.fh_tx_config_value = wifi_fh_tx_config_enable_value();
  wifi_status_state.fh_plan_ready = 1;
  wifi_status_state.fh_armed = 0;
  wifi_status_state.fh_complete = 0;
  wifi_status_state.fh_timeout = 0;
  wifi_status_state.fh_last_csr_int = wifi_csr_read32(CSR_INT);
  wifi_status_state.fh_last_fh_int = wifi_csr_read32(CSR_FH_INT_STATUS);
  wifi_status_state.fh_last_tx_status =
      wifi_csr_read32(FH_TCSR_CHNL_TX_BUF_STS_REG(FH_SRVC_CHNL));
  wifi_status_state.fh_last_tx_error =
      wifi_csr_read32(FH_TCSR_CHNL_TX_ERROR_REG(FH_SRVC_CHNL));

  if (section_out) {
    *section_out = section;
  }
  return 0;
}

static int wifi_prepare_fh_plan(const wifi_fw_section_t **section_out) {
  const wifi_fw_section_t *section = wifi_first_upload_section();
  return wifi_prepare_fh_plan_for(section, 0, section_out);
}

static int wifi_arm_fh_current_chunk(void) {
  uint32_t done = CSR_INT_BIT_FH_TX;
  uint32_t errors = CSR_INT_BIT_HW_ERR | CSR_INT_BIT_SW_ERR;

  if (!wifi_status_state.fh_plan_ready || !wifi_mmio) {
    return -1;
  }

  wifi_status_state.fh_armed = 1;
  wifi_status_state.fh_complete = 0;
  wifi_status_state.fh_timeout = 0;

  /* Ack stale status before arming the service DMA channel. */
  wifi_csr_write32(CSR_INT, CSR_INT_BIT_FH_TX | CSR_INT_BIT_HW_ERR |
                            CSR_INT_BIT_SW_ERR);
  wifi_csr_write32(CSR_FH_INT_STATUS, FH_INT_TX_MASK | FH_INT_ERR_MASK);

  wifi_csr_write32(FH_TCSR_CHNL_TX_CONFIG_REG(FH_SRVC_CHNL),
                   FH_TCSR_TX_CONFIG_REG_VAL_DMA_CHNL_PAUSE);
  wifi_csr_write32(FH_SRVC_CHNL_SRAM_ADDR_REG(FH_SRVC_CHNL),
                   wifi_status_state.fh_dst_addr);
  wifi_csr_write32(FH_TFDIB_CTRL0_REG(FH_SRVC_CHNL),
                   wifi_status_state.fh_ctrl0_value);
  wifi_csr_write32(FH_TFDIB_CTRL1_REG(FH_SRVC_CHNL),
                   wifi_status_state.fh_ctrl1_value);
  wifi_csr_write32(FH_TCSR_CHNL_TX_BUF_STS_REG(FH_SRVC_CHNL),
                   wifi_status_state.fh_buf_status_value);
  wifi_csr_write32(FH_TCSR_CHNL_TX_CONFIG_REG(FH_SRVC_CHNL),
                   wifi_status_state.fh_tx_config_value);

  for (uint32_t i = 0; i < WIFI_FH_POLL_LOOPS; i++) {
    uint32_t csr_int = wifi_csr_read32(CSR_INT);
    uint32_t fh_int = wifi_csr_read32(CSR_FH_INT_STATUS);
    uint32_t tx_error = wifi_csr_read32(FH_TCSR_CHNL_TX_ERROR_REG(FH_SRVC_CHNL));
    wifi_status_state.fh_last_csr_int = csr_int;
    wifi_status_state.fh_last_fh_int = fh_int;
    wifi_status_state.fh_last_tx_status =
        wifi_csr_read32(FH_TCSR_CHNL_TX_BUF_STS_REG(FH_SRVC_CHNL));
    wifi_status_state.fh_last_tx_error = tx_error;

    if ((csr_int & errors) || (fh_int & FH_INT_ERR_MASK) || tx_error) {
      wifi_status_state.fh_errors++;
      wifi_csr_write32(FH_TCSR_CHNL_TX_CONFIG_REG(FH_SRVC_CHNL),
                       FH_TCSR_TX_CONFIG_REG_VAL_DMA_CHNL_PAUSE);
      return -1;
    }

    if ((csr_int & done) || (fh_int & FH_INT_TX_MASK)) {
      wifi_status_state.fh_complete = 1;
      wifi_csr_write32(CSR_INT, CSR_INT_BIT_FH_TX);
      wifi_csr_write32(CSR_FH_INT_STATUS, fh_int & FH_INT_TX_MASK);
      wifi_csr_write32(FH_TCSR_CHNL_TX_CONFIG_REG(FH_SRVC_CHNL),
                       FH_TCSR_TX_CONFIG_REG_VAL_DMA_CHNL_PAUSE);
      return 0;
    }

    __asm__ volatile("pause");
  }

  wifi_status_state.fh_timeout = 1;
  wifi_status_state.fh_errors++;
  wifi_csr_write32(FH_TCSR_CHNL_TX_CONFIG_REG(FH_SRVC_CHNL),
                   FH_TCSR_TX_CONFIG_REG_VAL_DMA_CHNL_PAUSE);
  return -1;
}

static int wifi_uses_gen2_fw_load_status(void) {
  switch (wifi_status_state.device_id) {
    case 0x54F0:
    case 0x4DF0:
    case 0xA0F0:
    case 0x51F0:
    case 0x7AF0:
    case 0x2723:
    case 0x2725:
    case 0xA840:
    case 0x7740:
    case 0x4D40:
      return 1;
    default:
      return 0;
  }
}

static void wifi_reset_boot_runtime(void) {
  wifi_status_state.boot_ready = 0;
  wifi_status_state.boot_released = 0;
  wifi_status_state.boot_failed = 0;
  wifi_status_state.boot_cpu1_sections = 0;
  wifi_status_state.boot_cpu2_sections = 0;
  wifi_status_state.boot_paging_sections_skipped = 0;
  wifi_status_state.boot_sections_loaded = 0;
  wifi_status_state.boot_release_value = 0;
  wifi_status_state.boot_fh_status = 0;
  wifi_status_state.boot_ureg_status = 0;
  wifi_status_state.boot_first_cpu2_index = 0;
  wifi_status_state.boot_paging_index = 0;
  wifi_status_state.boot_last_gp = 0;
}

static void wifi_count_boot_plan(unsigned long *cpu1, unsigned long *cpu2,
                                 unsigned long *paging,
                                 uint32_t *cpu2_index,
                                 uint32_t *paging_index) {
  int phase = 1;

  if (cpu1) {
    *cpu1 = 0;
  }
  if (cpu2) {
    *cpu2 = 0;
  }
  if (paging) {
    *paging = 0;
  }
  if (cpu2_index) {
    *cpu2_index = 0;
  }
  if (paging_index) {
    *paging_index = 0;
  }

  for (int i = 0; i < wifi_fw_section_count; i++) {
    const wifi_fw_section_t *section = &wifi_fw_sections[i];

    if (section->dst == CPU1_CPU2_SEPARATOR_SECTION) {
      phase = 2;
      if (cpu2_index) {
        *cpu2_index = (uint32_t)(i + 1);
      }
      continue;
    }
    if (section->dst == PAGING_SEPARATOR_SECTION) {
      phase = 3;
      if (paging_index) {
        *paging_index = (uint32_t)i;
      }
      continue;
    }

    if (phase == 1 && cpu1) {
      (*cpu1)++;
    } else if (phase == 2 && cpu2) {
      (*cpu2)++;
    } else if (phase == 3 && paging) {
      (*paging)++;
    }
  }
}

static int wifi_boot_prepare_cpu_release(void) {
  uint32_t release_value = 0;

  if (wifi_prph_write32(WFPM_GP2, 0x01010101U) != 0) {
    return -1;
  }
  if (wifi_prph_write32(RELEASE_CPU_RESET, RELEASE_CPU_RESET_BIT) != 0) {
    return -1;
  }
  if (wifi_prph_read32(RELEASE_CPU_RESET, &release_value) == 0) {
    wifi_status_state.boot_release_value = release_value;
  } else {
    wifi_status_state.boot_release_value = RELEASE_CPU_RESET_BIT;
  }
  wifi_status_state.boot_released = 1;
  return 0;
}

static int wifi_boot_mark_section_loaded(int cpu, uint32_t section_mask) {
  uint32_t status = 0;
  uint32_t shift = cpu == 1 ? 0U : 16U;

  if (wifi_direct_read32(FH_UCODE_LOAD_STATUS, &status) != 0) {
    return -1;
  }
  status |= section_mask << shift;
  if (wifi_direct_write32(FH_UCODE_LOAD_STATUS, status) != 0) {
    return -1;
  }
  wifi_status_state.boot_fh_status = status;
  return 0;
}

static int wifi_boot_mark_cpu_done(int cpu) {
  uint32_t value = cpu == 1 ? 0x0000ffffU : 0xffffffffU;

  if (wifi_uses_gen2_fw_load_status()) {
    if (wifi_prph_write32(UREG_UCODE_LOAD_STATUS, value) != 0) {
      return -1;
    }
    wifi_status_state.boot_ureg_status = value;
    return 0;
  }

  if (wifi_direct_write32(FH_UCODE_LOAD_STATUS, value) != 0) {
    return -1;
  }
  wifi_status_state.boot_fh_status = value;
  return 0;
}

static int wifi_boot_upload_section(const wifi_fw_section_t *section) {
  uint32_t offset = 0;

  while (offset < section->len) {
    if (wifi_prepare_fh_plan_for(section, offset, NULL) != 0) {
      return -1;
    }
    if (wifi_arm_fh_current_chunk() != 0) {
      return -1;
    }
    if (wifi_status_state.fh_last_len == 0) {
      return -1;
    }
    wifi_status_state.fh_uploaded_chunks++;
    wifi_status_state.fh_uploaded_bytes += wifi_status_state.fh_last_len;
    offset += wifi_status_state.fh_last_len;
  }

  wifi_status_state.fh_sections_done++;
  wifi_status_state.boot_sections_loaded++;
  return 0;
}

static int wifi_boot_load_cpu_sections(int cpu, int start_index,
                                       int *next_index) {
  uint32_t section_mask = 1;
  int i;

  for (i = start_index; i < wifi_fw_section_count; i++) {
    const wifi_fw_section_t *section = &wifi_fw_sections[i];

    if (section->dst == CPU1_CPU2_SEPARATOR_SECTION ||
        section->dst == PAGING_SEPARATOR_SECTION) {
      break;
    }

    if (wifi_boot_upload_section(section) != 0) {
      return -1;
    }
    if (wifi_boot_mark_section_loaded(cpu, section_mask) != 0) {
      return -1;
    }

    if (cpu == 1) {
      wifi_status_state.boot_cpu1_sections++;
    } else {
      wifi_status_state.boot_cpu2_sections++;
    }

    section_mask = (section_mask << 1) | 0x1U;
  }

  if (next_index) {
    *next_index = i;
  }
  return 0;
}

static void wifi_reset_queue_runtime(void) {
  wifi_status_state.queues_ready = 0;
  wifi_status_state.queues_armed = 0;
  wifi_status_state.queues_failed = 0;
  wifi_status_state.cmd_queue_entries = WIFI_CMD_QUEUE_ENTRIES;
  wifi_status_state.tx_queue_entries = WIFI_TX_QUEUE_ENTRIES;
  wifi_status_state.rx_queue_entries = WIFI_RX_QUEUE_ENTRIES;
  wifi_status_state.cmd_buffer_bytes = sizeof(wifi_cmd_buffers);
  wifi_status_state.tx_buffer_bytes = sizeof(wifi_tx_buffers);
  wifi_status_state.rx_buffer_bytes = sizeof(wifi_rx_buffers);
  wifi_status_state.cmd_tfd_bytes = sizeof(wifi_cmd_tfd);
  wifi_status_state.tx_tfd_bytes = sizeof(wifi_tx_tfd);
  wifi_status_state.rx_desc_bytes = sizeof(wifi_rx_desc);
  wifi_status_state.cmd_tfd_phys = 0;
  wifi_status_state.cmd_buffer_phys = 0;
  wifi_status_state.tx_tfd_phys = 0;
  wifi_status_state.tx_buffer_phys = 0;
  wifi_status_state.rx_desc_phys = 0;
  wifi_status_state.rx_used_desc_phys = 0;
  wifi_status_state.rx_status_phys = 0;
  wifi_status_state.rx_buffer_phys = 0;
  wifi_status_state.cmd_write_ptr = 0;
  wifi_status_state.tx_write_ptr = 0;
  wifi_status_state.rx_write_ptr = 0;
  wifi_status_state.context_ready = 0;
  wifi_status_state.context_armed = 0;
  wifi_status_state.context_failed = 0;
  wifi_status_state.context_mode = 0;
  wifi_status_state.context_lmac_sections = 0;
  wifi_status_state.context_umac_sections = 0;
  wifi_status_state.context_paging_sections = 0;
  wifi_status_state.legacy_context_phys = 0;
  wifi_status_state.v2_context_phys = 0;
  wifi_status_state.prph_info_phys = 0;
  wifi_status_state.prph_scratch_phys = 0;
  wifi_status_state.context_size = 0;
  wifi_status_state.context_v2_size = 0;
  wifi_status_state.prph_scratch_size = 0;
  wifi_status_state.context_control_flags = 0;
  wifi_status_state.context_v2_control_flags = 0;
  wifi_status_state.context_csr_target = 0;
  wifi_status_state.context_csr_value_lo = 0;
  wifi_status_state.context_csr_value_hi = 0;
  wifi_status_state.scheduler_ready = 0;
  wifi_status_state.scheduler_armed = 0;
  wifi_status_state.scheduler_failed = 0;
  wifi_status_state.cmd_bc_phys = 0;
  wifi_status_state.tx_bc_phys = 0;
  wifi_status_state.mtr_ring_phys = 0;
  wifi_status_state.mcr_ring_phys = 0;
  wifi_status_state.cr_head_phys = 0;
  wifi_status_state.cr_tail_phys = 0;
  wifi_status_state.tr_head_phys = 0;
  wifi_status_state.tr_tail_phys = 0;
  wifi_status_state.mtr_entries = 0;
  wifi_status_state.mcr_entries = 0;
  wifi_status_state.msg_ring_bytes = 0;
  wifi_status_state.scheduler_cmd_id = 0;
  wifi_status_state.scheduler_cmd_group = 0;
  wifi_status_state.scheduler_cmd_version = 0;
  wifi_status_state.scheduler_cmd_len = 0;
  wifi_status_state.scheduler_cmd_sequence = 0;
  wifi_status_state.scheduler_cmd_queue = 0;
  wifi_status_state.scheduler_cmd_index = 0;
  wifi_status_state.scheduler_cmd_tbs = 0;
  wifi_status_state.scheduler_cmd_tb_len = 0;
  wifi_status_state.scheduler_wptr_value = 0;
  wifi_status_state.rx_path_ready = 0;
  wifi_status_state.rx_path_failed = 0;
  wifi_status_state.rx_closed_rb = 0;
  wifi_status_state.rx_read_ptr = 0;
  wifi_status_state.rx_last_rbid = 0;
  wifi_status_state.rx_last_flags = 0;
  wifi_status_state.rx_last_len_n_flags = 0;
  wifi_status_state.rx_last_len = 0;
  wifi_status_state.rx_last_queue = 0;
  wifi_status_state.rx_last_cmd = 0;
  wifi_status_state.rx_last_group = 0;
  wifi_status_state.rx_last_sequence = 0;
  wifi_status_state.rx_last_index = 0;
  wifi_status_state.command_ready = 0;
  wifi_status_state.command_sent = 0;
  wifi_status_state.command_failed = 0;
  wifi_status_state.command_doorbell_value = 0;
  wifi_status_state.command_last_csr_int = 0;
  wifi_status_state.command_last_fh_int = 0;
  wifi_status_state.command_last_closed_rb = 0;
  wifi_status_state.command_poll_loops = 0;
}

static int wifi_prepare_host_queues(void) {
  memset(wifi_cmd_tfd, 0, sizeof(wifi_cmd_tfd));
  memset(wifi_tx_tfd, 0, sizeof(wifi_tx_tfd));
  memset(wifi_rx_desc, 0, sizeof(wifi_rx_desc));
  memset(wifi_rx_used_desc, 0, sizeof(wifi_rx_used_desc));
  memset(wifi_rx_status, 0, sizeof(wifi_rx_status));
  memset(wifi_cmd_bc_tbl, 0, sizeof(wifi_cmd_bc_tbl));
  memset(wifi_tx_bc_tbl, 0, sizeof(wifi_tx_bc_tbl));
  memset(wifi_cmd_buffers, 0, sizeof(wifi_cmd_buffers));
  memset(wifi_tx_buffers, 0, sizeof(wifi_tx_buffers));
  memset(wifi_rx_buffers, 0, sizeof(wifi_rx_buffers));

  wifi_reset_queue_runtime();
  wifi_status_state.cmd_tfd_phys = wifi_phys_addr(wifi_cmd_tfd);
  wifi_status_state.cmd_buffer_phys = wifi_phys_addr(wifi_cmd_buffers);
  wifi_status_state.tx_tfd_phys = wifi_phys_addr(wifi_tx_tfd);
  wifi_status_state.tx_buffer_phys = wifi_phys_addr(wifi_tx_buffers);
  wifi_status_state.rx_desc_phys = wifi_phys_addr(wifi_rx_desc);
  wifi_status_state.rx_used_desc_phys = wifi_phys_addr(wifi_rx_used_desc);
  wifi_status_state.rx_status_phys = wifi_phys_addr(wifi_rx_status);
  wifi_status_state.rx_buffer_phys = wifi_phys_addr(wifi_rx_buffers);
  wifi_status_state.cmd_bc_phys = wifi_phys_addr(wifi_cmd_bc_tbl);
  wifi_status_state.tx_bc_phys = wifi_phys_addr(wifi_tx_bc_tbl);

  if (!wifi_status_state.cmd_tfd_phys || !wifi_status_state.cmd_buffer_phys ||
      !wifi_status_state.tx_tfd_phys || !wifi_status_state.tx_buffer_phys ||
      !wifi_status_state.rx_desc_phys ||
      !wifi_status_state.rx_used_desc_phys ||
      !wifi_status_state.rx_status_phys ||
      !wifi_status_state.cmd_bc_phys ||
      !wifi_status_state.tx_bc_phys ||
      !wifi_status_state.rx_buffer_phys) {
    wifi_status_state.queues_failed = 1;
    wifi_status_state.queue_errors++;
    wifi_status_state.status =
        "wifi: host queue DMA address translation failed";
    return -1;
  }

  for (uint32_t i = 0; i < WIFI_RX_QUEUE_ENTRIES; i++) {
    uint64_t phys = wifi_phys_addr(wifi_rx_buffers[i]);
    wifi_rx_desc[i].rbid = (uint16_t)(i + 1U);
    wifi_rx_desc[i].addr = phys;
    if (!phys) {
      wifi_status_state.queues_failed = 1;
      wifi_status_state.queue_errors++;
      wifi_status_state.status = "wifi: RX buffer DMA address missing";
      return -1;
    }
  }

  wifi_status_state.rx_path_ready = 1;
  wifi_status_state.rx_path_failed = 0;
  wifi_status_state.queue_generation++;
  wifi_status_state.queues_ready = 1;
  wifi_status_state.queues_failed = 0;
  wifi_status_state.status =
      "wifi: host command/RX/TX queue memory staged";
  return 0;
}

static int wifi_prefers_context_v2(void) {
  switch (wifi_status_state.device_id) {
    case 0x54F0:
    case 0x4DF0:
    case 0xA0F0:
    case 0x51F0:
    case 0x7AF0:
    case 0x2723:
    case 0x2725:
      return 1;
    default:
      return 0;
  }
}

static int wifi_context_add_chunks(uint64_t *map, unsigned long *count,
                                   const uint8_t *data, uint32_t len) {
  uint32_t offset = 0;

  while (offset < len) {
    uint32_t chunk = len - offset;
    uint64_t phys;

    if (*count >= WIFI_CONTEXT_DRAM_ENTRIES) {
      return -1;
    }
    if (chunk > WIFI_CONTEXT_DRAM_CHUNK_BYTES) {
      chunk = WIFI_CONTEXT_DRAM_CHUNK_BYTES;
    }

    phys = wifi_phys_addr(data + offset);
    if (!phys) {
      return -1;
    }

    map[*count] = phys;
    (*count)++;
    offset += chunk;
  }

  return 0;
}

static int wifi_context_stage_firmware_maps(void) {
  int phase = 1;
  unsigned long lmac = 0;
  unsigned long umac = 0;
  unsigned long paging = 0;

  if (wifi_fw_section_count <= 0) {
    return -1;
  }

  for (int i = 0; i < wifi_fw_section_count; i++) {
    const wifi_fw_section_t *section = &wifi_fw_sections[i];
    uint64_t *map;
    unsigned long *count;

    if (section->dst == CPU1_CPU2_SEPARATOR_SECTION) {
      phase = 2;
      continue;
    }
    if (section->dst == PAGING_SEPARATOR_SECTION) {
      phase = 3;
      continue;
    }
    if (!section->data || section->len == 0) {
      continue;
    }

    if (phase == 1) {
      map = wifi_legacy_context.dram.lmac_img;
      count = &lmac;
    } else if (phase == 2) {
      map = wifi_legacy_context.dram.umac_img;
      count = &umac;
    } else {
      map = wifi_legacy_context.dram.virtual_img;
      count = &paging;
    }

    if (wifi_context_add_chunks(map, count, section->data,
                                section->len) != 0) {
      return -1;
    }
  }

  memcpy(&wifi_prph_scratch_v2.dram.common, &wifi_legacy_context.dram,
         sizeof(wifi_legacy_context.dram));
  wifi_status_state.context_lmac_sections = lmac;
  wifi_status_state.context_umac_sections = umac;
  wifi_status_state.context_paging_sections = paging;
  return 0;
}

static void wifi_context_mark_failure(const char *status) {
  wifi_status_state.context_ready = 0;
  wifi_status_state.context_armed = 0;
  wifi_status_state.context_failed = 1;
  wifi_status_state.context_errors++;
  wifi_status_state.status = status;
}

static int wifi_prepare_message_rings(void) {
  memset(wifi_mtr_ring, 0, sizeof(wifi_mtr_ring));
  memset(wifi_mcr_ring, 0, sizeof(wifi_mcr_ring));
  memset(wifi_cr_head_idx, 0, sizeof(wifi_cr_head_idx));
  memset(wifi_cr_tail_idx, 0, sizeof(wifi_cr_tail_idx));
  memset(wifi_tr_head_idx, 0, sizeof(wifi_tr_head_idx));
  memset(wifi_tr_tail_idx, 0, sizeof(wifi_tr_tail_idx));

  wifi_status_state.mtr_ring_phys = wifi_phys_addr(wifi_mtr_ring);
  wifi_status_state.mcr_ring_phys = wifi_phys_addr(wifi_mcr_ring);
  wifi_status_state.cr_head_phys = wifi_phys_addr(wifi_cr_head_idx);
  wifi_status_state.cr_tail_phys = wifi_phys_addr(wifi_cr_tail_idx);
  wifi_status_state.tr_head_phys = wifi_phys_addr(wifi_tr_head_idx);
  wifi_status_state.tr_tail_phys = wifi_phys_addr(wifi_tr_tail_idx);

  if (!wifi_status_state.mtr_ring_phys || !wifi_status_state.mcr_ring_phys ||
      !wifi_status_state.cr_head_phys || !wifi_status_state.cr_tail_phys ||
      !wifi_status_state.tr_head_phys || !wifi_status_state.tr_tail_phys) {
    return -1;
  }

  wifi_status_state.mtr_entries = WIFI_MSG_RING_ENTRIES;
  wifi_status_state.mcr_entries = WIFI_MSG_RING_ENTRIES;
  wifi_status_state.msg_ring_bytes = WIFI_MSG_RING_BYTES;
  return 0;
}

static int wifi_prepare_context_info(void) {
  uint32_t rx_cb_exponent = 6U;
  uint32_t control_flags;
  uint16_t mac_id;

  if ((!wifi_status_state.firmware_valid || wifi_fw_section_count <= 0) &&
      wifi_find_firmware() != 0) {
    wifi_context_mark_failure("wifi: context-info needs valid firmware TLVs");
    return -1;
  }
  if (!wifi_status_state.firmware_valid || wifi_fw_section_count <= 0) {
    wifi_context_mark_failure("wifi: context-info needs valid firmware TLVs");
    return -1;
  }

  if (!wifi_status_state.queues_ready && wifi_prepare_host_queues() != 0) {
    wifi_context_mark_failure(
        "wifi: context-info needs valid host queues first");
    return -1;
  }

  memset(&wifi_legacy_context, 0, sizeof(wifi_legacy_context));
  memset(&wifi_context_v2, 0, sizeof(wifi_context_v2));
  memset(&wifi_prph_info_v2, 0, sizeof(wifi_prph_info_v2));
  memset(&wifi_prph_scratch_v2, 0, sizeof(wifi_prph_scratch_v2));

  wifi_status_state.legacy_context_phys = wifi_phys_addr(&wifi_legacy_context);
  wifi_status_state.v2_context_phys = wifi_phys_addr(&wifi_context_v2);
  wifi_status_state.prph_info_phys = wifi_phys_addr(&wifi_prph_info_v2);
  wifi_status_state.prph_scratch_phys = wifi_phys_addr(&wifi_prph_scratch_v2);

  if (!wifi_status_state.legacy_context_phys ||
      !wifi_status_state.v2_context_phys ||
      !wifi_status_state.prph_info_phys ||
      !wifi_status_state.prph_scratch_phys) {
    wifi_context_mark_failure(
        "wifi: context-info DMA address translation failed");
    return -1;
  }

  mac_id = (uint16_t)(wifi_status_state.csr_hw_rev
                          ? wifi_status_state.csr_hw_rev
                          : wifi_status_state.device_id);
  control_flags = IWL_CTXT_INFO_TFD_FORMAT_LONG |
                  (rx_cb_exponent << IWL_CTXT_INFO_RB_CB_SIZE_SHIFT) |
                  (IWL_CTXT_INFO_RB_SIZE_2K << IWL_CTXT_INFO_RB_SIZE_SHIFT);

  wifi_legacy_context.version.mac_id = mac_id;
  wifi_legacy_context.version.version = 0;
  wifi_legacy_context.version.size =
      (uint16_t)(sizeof(wifi_legacy_context) / sizeof(uint32_t));
  wifi_legacy_context.control.control_flags = control_flags;
  wifi_legacy_context.rbd_cfg.free_rbd_addr = wifi_status_state.rx_desc_phys;
  wifi_legacy_context.rbd_cfg.used_rbd_addr =
      wifi_status_state.rx_used_desc_phys;
  wifi_legacy_context.rbd_cfg.status_wr_ptr =
      wifi_status_state.rx_status_phys;
  wifi_legacy_context.hcmd_cfg.cmd_queue_addr =
      wifi_status_state.cmd_tfd_phys;
  wifi_legacy_context.hcmd_cfg.cmd_queue_size =
      (uint8_t)WIFI_CMD_QUEUE_ENTRIES;

  wifi_prph_scratch_v2.ctrl_cfg.version.mac_id = mac_id;
  wifi_prph_scratch_v2.ctrl_cfg.version.version = 0;
  wifi_prph_scratch_v2.ctrl_cfg.version.size =
      (uint16_t)(sizeof(wifi_prph_scratch_v2) / sizeof(uint32_t));
  wifi_prph_scratch_v2.ctrl_cfg.control.control_flags =
      IWL_PRPH_SCRATCH_MTR_MODE | IWL_PRPH_MTR_FORMAT_256B;
  wifi_prph_scratch_v2.ctrl_cfg.rbd_cfg.free_rbd_addr =
      wifi_status_state.rx_desc_phys;

  if (wifi_context_stage_firmware_maps() != 0) {
    wifi_context_mark_failure(
        "wifi: context-info firmware DRAM map overflow/translation failed");
    return -1;
  }
  if (wifi_prepare_message_rings() != 0) {
    wifi_context_mark_failure(
        "wifi: context-info message-ring DMA translation failed");
    return -1;
  }

  wifi_context_v2.version = 0;
  wifi_context_v2.size =
      (uint16_t)(sizeof(wifi_context_v2) / sizeof(uint32_t));
  wifi_context_v2.config = 0;
  wifi_context_v2.prph_info_base_addr = wifi_status_state.prph_info_phys;
  wifi_context_v2.cr_head_idx_arr_base_addr =
      wifi_status_state.cr_head_phys;
  wifi_context_v2.tr_tail_idx_arr_base_addr =
      wifi_status_state.tr_tail_phys;
  wifi_context_v2.cr_tail_idx_arr_base_addr =
      wifi_status_state.cr_tail_phys;
  wifi_context_v2.tr_head_idx_arr_base_addr =
      wifi_status_state.tr_head_phys;
  wifi_context_v2.cr_idx_arr_size = 1;
  wifi_context_v2.tr_idx_arr_size = 1;
  wifi_context_v2.mtr_base_addr = wifi_status_state.mtr_ring_phys;
  wifi_context_v2.mcr_base_addr = wifi_status_state.mcr_ring_phys;
  wifi_context_v2.mtr_size = WIFI_MSG_RING_ENTRIES;
  wifi_context_v2.mcr_size = WIFI_MSG_RING_ENTRIES;
  wifi_context_v2.prph_scratch_base_addr =
      wifi_status_state.prph_scratch_phys;
  wifi_context_v2.prph_scratch_size =
      (uint32_t)(sizeof(wifi_prph_scratch_v2) / sizeof(uint32_t));

  wifi_status_state.context_mode =
      wifi_prefers_context_v2() ? WIFI_CONTEXT_MODE_V2_LITE
                                : WIFI_CONTEXT_MODE_LEGACY;
  wifi_status_state.context_size = sizeof(wifi_legacy_context);
  wifi_status_state.context_v2_size = sizeof(wifi_context_v2);
  wifi_status_state.prph_scratch_size = sizeof(wifi_prph_scratch_v2);
  wifi_status_state.context_control_flags = control_flags;
  wifi_status_state.context_v2_control_flags =
      wifi_prph_scratch_v2.ctrl_cfg.control.control_flags;
  if (wifi_status_state.context_mode == WIFI_CONTEXT_MODE_V2_LITE) {
    wifi_status_state.context_csr_target = CSR_CTXT_INFO_ADDR;
    wifi_status_state.context_csr_value_lo =
        (uint32_t)wifi_status_state.v2_context_phys;
    wifi_status_state.context_csr_value_hi =
        (uint32_t)(wifi_status_state.v2_context_phys >> 32);
  } else {
    wifi_status_state.context_csr_target = CSR_CTXT_INFO_BA;
    wifi_status_state.context_csr_value_lo =
        (uint32_t)wifi_status_state.legacy_context_phys;
    wifi_status_state.context_csr_value_hi =
        (uint32_t)(wifi_status_state.legacy_context_phys >> 32);
  }
  wifi_status_state.context_generation++;
  wifi_status_state.context_ready = 1;
  wifi_status_state.context_failed = 0;
  wifi_status_state.status =
      "wifi: firmware context-info and PRPH scratch staged";
  return 0;
}

int wifi_init(void) {
  pci_device_info_t devs[8];
  int count;

  if (wifi_status_state.present || wifi_status_state.driver_ready) {
    return wifi_status_state.driver_ready ? 0 : -1;
  }

  count = pci_scan_class(0x02, 0x80, 0xFF, devs, 8);
  if (count <= 0) {
    wifi_set_status("wifi: no PCI wireless controller detected");
    return -1;
  }

  for (int i = 0; i < count; i++) {
    if (devs[i].vendor_id != 0x8086) {
      continue;
    }

    wifi_status_state.present = 1;
    wifi_status_state.bus = devs[i].bus;
    wifi_status_state.device = devs[i].device;
    wifi_status_state.function = devs[i].function;
    wifi_status_state.vendor_id = devs[i].vendor_id;
    wifi_status_state.device_id = devs[i].device_id;
    wifi_status_state.chipset = intel_wifi_name(devs[i].device_id);
    wifi_status_state.driver = "intel-iwlwifi-stage0";
    wifi_status_state.driver_ready = 0;
    wifi_status_state.associated = 0;
    if (wifi_find_firmware() == 0) {
      wifi_set_status(
          "wifi: Intel controller and firmware detected; command queues pending");
    } else {
      wifi_set_status(
          "wifi: Intel controller detected; firmware missing");
    }
    return -1;
  }

  wifi_status_state.present = 1;
  wifi_status_state.bus = devs[0].bus;
  wifi_status_state.device = devs[0].device;
  wifi_status_state.function = devs[0].function;
  wifi_status_state.vendor_id = devs[0].vendor_id;
  wifi_status_state.device_id = devs[0].device_id;
  wifi_status_state.chipset = "unsupported wireless controller";
  wifi_status_state.driver = "unsupported";
  wifi_set_status("wifi: wireless controller unsupported");
  return -1;
}

void wifi_poll(void) {
  /* Real RX/TX polling starts once the firmware-backed driver exists. */
}

const wifi_status_t *wifi_get_status(void) {
  if (!wifi_status_state.present && !wifi_status_state.driver_ready) {
    wifi_init();
  }
  return &wifi_status_state;
}

void wifi_format_status(char *buf, size_t size) {
  const wifi_status_t *s;

  if (!buf || size == 0) {
    return;
  }

  s = wifi_get_status();
  snprintf(buf, size,
           "driver=%s present=%s ready=%s associated=%s pci=%04x:%04x "
           "slot=%02x:%02x.%u chipset=%s mmio=%s phys=0x%lx "
           "firmware=%s source=%s size=%lu valid=%s tlvs=%lu sections=%lu "
           "plan=%s dma=%s apm=%s boot=%s alive=%s queues=%s context=%s "
           "scheduler=%s rx=%s command=%s status=%s",
           s->driver, s->present ? "yes" : "no",
           s->driver_ready ? "yes" : "no", s->associated ? "yes" : "no",
           s->vendor_id, s->device_id, s->bus, s->device,
           (unsigned int)s->function, s->chipset,
           s->mmio_probed ? (s->mmio_ready ? "ready" : "failed")
                           : "untested",
           (unsigned long)s->mmio_phys,
           s->firmware_present ? s->firmware_name : "missing",
           s->firmware_source, (unsigned long)s->firmware_size,
           s->firmware_valid ? "yes" : "no", s->firmware_tlv_count,
           s->firmware_section_count,
           s->firmware_load_plan_ready ? "ready" : "no",
           s->dma_ready ? "staged" : "idle",
           s->apm_ready ? "awake" : (s->apm_timeout ? "timeout" : "idle"),
           s->boot_ready ? "ready" : (s->boot_failed ? "failed" : "idle"),
           s->alive_seen ? "seen" : (s->alive_timeout ? "timeout" : "idle"),
           s->queues_ready ? (s->queues_armed ? "armed" : "staged")
                            : (s->queues_failed ? "failed" : "idle"),
           s->context_ready ? (s->context_armed ? "armed" : "staged")
                            : (s->context_failed ? "failed" : "idle"),
           s->scheduler_ready ? (s->scheduler_armed ? "armed" : "staged")
                              : (s->scheduler_failed ? "failed" : "idle"),
           s->rx_path_ready ? "ready"
                            : (s->rx_path_failed ? "failed" : "idle"),
           s->command_sent ? "sent"
                           : (s->command_ready
                                  ? "ready"
                                  : (s->command_failed ? "failed" : "idle")),
           s->status);
}

int wifi_firmware_probe(char *report, size_t report_size) {
  const wifi_status_t *s;

  if (!report || report_size == 0) {
    return -1;
  }

  wifi_init();
  wifi_find_firmware();
  s = &wifi_status_state;

  if (!s->present) {
    snprintf(report, report_size,
             "wifi firmware: no Intel Wi-Fi controller detected\n");
    return -1;
  }

  if (s->firmware_present) {
    wifi_status_state.status =
        "wifi: firmware detected; command queues pending";
    if (s->firmware_valid) {
      snprintf(report, report_size,
               "wifi firmware: valid Intel TLV firmware\n"
               "name: %s\n"
               "source: %s\n"
               "size: %lu bytes\n"
               "human: %s\n"
               "version: 0x%08x build=%u\n"
               "tlvs: %lu sections=%lu runtime=%lu init=%lu wowlan=%lu secure=%lu\n"
               "payload: inst=%lu bytes data=%lu bytes load=%lu bytes chunks=%lu\n"
               "plan: %s first-dst=0x%08x cpus=%u largest=%lu\n"
               "next: run wifi load to stage DMA, then implement FH upload/alive\n",
               s->firmware_name, s->firmware_source,
               (unsigned long)s->firmware_size,
               s->firmware_human[0] ? s->firmware_human : "unknown",
               s->firmware_version, s->firmware_build, s->firmware_tlv_count,
               s->firmware_section_count, s->firmware_runtime_sections,
               s->firmware_init_sections, s->firmware_wowlan_sections,
               s->firmware_secure_sections, s->firmware_inst_bytes,
               s->firmware_data_bytes, s->firmware_load_bytes,
               s->firmware_load_chunks,
               s->firmware_load_plan_ready ? "ready" : "not-ready",
               s->firmware_first_dst, s->firmware_cpu_count,
               s->firmware_largest_section);
      return 0;
    }

    snprintf(report, report_size,
             "wifi firmware: found %s, but TLV validation is unavailable\n"
             "source: %s\n"
             "size: %lu bytes\n"
             "parse-errors: %lu\n"
             "hint: firmware loaded from boot module gives full validation\n",
             s->firmware_name, s->firmware_source,
             (unsigned long)s->firmware_size, s->firmware_parse_errors);
    return -1;
  }

  snprintf(report, report_size,
           "wifi firmware: missing for %s (%04x:%04x)\n"
           "expected examples: %s, %s\n"
           "place firmware in /system/firmware or local ISO folder "
           "orizon-os-x86_64/firmware before build\n",
           s->chipset, s->vendor_id, s->device_id, intel_fw_candidates[0],
           intel_fw_candidates[1]);
  return -1;
}

int wifi_hw_probe(char *report, size_t report_size) {
  const wifi_status_t *s;

  if (!report || report_size == 0) {
    return -1;
  }

  wifi_init();
  wifi_find_firmware();
  s = &wifi_status_state;

  if (!s->present) {
    snprintf(report, report_size,
             "wifi hw: no PCI wireless controller detected\n");
    return -1;
  }

  if (s->vendor_id != 0x8086) {
    snprintf(report, report_size,
             "wifi hw: unsupported controller %04x:%04x\n",
             s->vendor_id, s->device_id);
    return -1;
  }

  if (wifi_probe_mmio() != 0) {
    s = &wifi_status_state;
    snprintf(report, report_size,
             "wifi hw: Intel controller detected, but MMIO probe failed\n"
             "pci: %02x:%02x.%u %04x:%04x\n"
             "bar0-phys: 0x%lx\n"
             "errors: %lu\n"
             "status: %s\n",
             s->bus, s->device, (unsigned int)s->function, s->vendor_id,
             s->device_id, (unsigned long)s->mmio_phys, s->mmio_errors,
             s->status);
    return -1;
  }

  s = &wifi_status_state;
  snprintf(report, report_size,
           "wifi hw: Intel CSR MMIO ready\n"
           "pci: %02x:%02x.%u %04x:%04x command=0x%04x\n"
           "bar0: phys=0x%lx mapped=yes\n"
           "csr: hw-if=0x%08x hw-rev=0x%08x rf-id=0x%08x\n"
           "csr: gp=0x%08x reset=0x%08x gpio=0x%08x\n"
           "irq: int=0x%08x mask=0x%08x fh=0x%08x masked=staging\n"
           "flags: mac-clock=%s init-done=%s sleep=%s mac-status=%s "
           "bus-master-disabled=%s rfkill-bit=%s\n"
           "firmware: %s valid=%s tlvs=%lu plan=%s\n"
           "next: run wifi load to stage firmware DMA\n",
           s->bus, s->device, (unsigned int)s->function, s->vendor_id,
           s->device_id, s->pci_command, (unsigned long)s->mmio_phys,
           s->csr_hw_if_config, s->csr_hw_rev, s->csr_hw_rf_id,
           s->csr_gp_cntrl, s->csr_reset, s->csr_gpio_in, s->csr_int,
           s->csr_int_mask, s->csr_fh_int_status,
           wifi_bit_text(s->csr_gp_cntrl, CSR_GP_CNTRL_MAC_CLOCK_READY),
           wifi_bit_text(s->csr_gp_cntrl, CSR_GP_CNTRL_INIT_DONE),
           wifi_bit_text(s->csr_gp_cntrl, CSR_GP_CNTRL_GOING_TO_SLEEP),
           wifi_bit_text(s->csr_gp_cntrl, CSR_GP_CNTRL_MAC_STATUS),
           wifi_bit_text(s->csr_gp_cntrl,
                         CSR_GP_CNTRL_BUS_MASTER_DISABLED),
           wifi_bit_text(s->csr_gp_cntrl, CSR_GP_CNTRL_HW_RF_KILL_SW),
           s->firmware_present ? s->firmware_name : "missing",
           s->firmware_valid ? "yes" : "no", s->firmware_tlv_count,
           s->firmware_load_plan_ready ? "ready" : "no");
  return 0;
}

int wifi_apm_probe(char *report, size_t report_size) {
  const wifi_status_t *s;
  int rc;

  if (!report || report_size == 0) {
    return -1;
  }

  wifi_init();
  wifi_find_firmware();
  s = &wifi_status_state;

  if (!s->present) {
    snprintf(report, report_size,
             "wifi apm: no PCI wireless controller detected\n");
    return -1;
  }

  if (s->vendor_id != 0x8086) {
    snprintf(report, report_size,
             "wifi apm: unsupported controller %04x:%04x\n",
             s->vendor_id, s->device_id);
    return -1;
  }

  rc = wifi_activate_nic();
  s = &wifi_status_state;
  snprintf(report, report_size,
           "wifi apm: %s\n"
           "profile: %s pci=%02x:%02x.%u command=0x%04x\n"
           "bar0: phys=0x%lx mmio=%s\n"
           "poll: ready-mask=0x%08x ready=%s timeout=%s attempts=%lu\n"
           "csr: gp=0x%08x hw-if=0x%08x gio=0x%08x hpet=0x%08x\n"
           "irq: int=0x%08x mask=0x%08x fh=0x%08x\n"
           "next: run 'wifi upload all arm', then 'wifi alive'\n",
           rc == 0 ? "NIC awake" : "NIC wake failed",
           wifi_uses_bz_apm_profile() ? "bz/mac-init" : "legacy/init-done",
           s->bus, s->device, (unsigned int)s->function, s->pci_command,
           (unsigned long)s->mmio_phys, s->mmio_ready ? "ready" : "failed",
           s->apm_poll_ready_mask, s->apm_ready ? "yes" : "no",
           s->apm_timeout ? "yes" : "no", s->apm_attempts,
           s->apm_last_gp, s->apm_last_hw_if, s->apm_last_gio,
           s->apm_last_hpet, s->csr_int, s->csr_int_mask,
           s->csr_fh_int_status);
  return rc;
}

int wifi_load_firmware(char *report, size_t report_size) {
  const wifi_status_t *s;
  const wifi_fw_section_t *section = NULL;

  if (!report || report_size == 0) {
    return -1;
  }

  wifi_status_state.firmware_load_attempts++;
  wifi_init();
  wifi_find_firmware();
  s = &wifi_status_state;

  if (!s->present) {
    snprintf(report, report_size,
             "wifi load: no PCI wireless controller detected\n");
    return -1;
  }

  if (!s->firmware_present) {
    snprintf(report, report_size,
             "wifi load: firmware missing for %s (%04x:%04x)\n"
             "run: wifi firmware\n",
             s->chipset, s->vendor_id, s->device_id);
    return -1;
  }

  if (!s->firmware_valid || !s->firmware_load_plan_ready ||
      !wifi_firmware_blob || wifi_firmware_blob_size == 0) {
    snprintf(report, report_size,
             "wifi load: firmware is present but not load-plannable\n"
             "valid=%s plan=%s source=%s parse-errors=%lu\n"
             "hint: boot-module firmware is required for DMA upload staging\n",
             s->firmware_valid ? "yes" : "no",
             s->firmware_load_plan_ready ? "yes" : "no",
             s->firmware_source, s->firmware_parse_errors);
    return -1;
  }

  if (wifi_probe_mmio() != 0) {
    s = &wifi_status_state;
    snprintf(report, report_size,
             "wifi load: MMIO probe failed before loader setup\n"
             "bar0-phys: 0x%lx errors=%lu status=%s\n",
             (unsigned long)s->mmio_phys, s->mmio_errors, s->status);
    return -1;
  }

  if (wifi_enable_bus_master_for_loader() != 0) {
    s = &wifi_status_state;
    wifi_status_state.load_state =
        "wifi: PCI bus mastering failed for firmware loader";
    snprintf(report, report_size,
             "wifi load: cannot enable PCI bus mastering\n"
             "pci-command=0x%04x\n",
             s->pci_command);
    return -1;
  }

  if (wifi_stage_first_dma_chunk(&section) != 0 || !section) {
    s = &wifi_status_state;
    wifi_status_state.load_state =
        "wifi: DMA staging failed for firmware loader";
    snprintf(report, report_size,
             "wifi load: DMA staging failed\n"
             "sections=%lu bytes=%lu dma-phys=0x%lx\n",
             s->firmware_section_count, s->firmware_load_bytes,
             (unsigned long)s->dma_phys);
    return -1;
  }

  wifi_status_state.load_state =
      "wifi: firmware DMA staged; FH transfer not armed yet";
  s = &wifi_status_state;
  snprintf(report, report_size,
           "wifi load: firmware loader staged\n"
           "firmware: %s size=%lu human=%s\n"
           "plan: sections=%lu runtime=%lu init=%lu wowlan=%lu secure=%lu "
           "cpus=%u chunks=%lu bytes=%lu largest=%lu\n"
           "first-dst=0x%08x first-section=%s tlv=%u dst=0x%08x len=%u\n"
           "dma: phys=0x%lx chunk=%lu staged=%lu pci-command=0x%04x\n"
           "state: staged only, hardware upload/alive IRQ still guarded\n"
           "next: program FH transfer channel and wait for firmware alive\n",
           s->firmware_name, (unsigned long)s->firmware_size,
           s->firmware_human[0] ? s->firmware_human : "unknown",
           s->firmware_section_count, s->firmware_runtime_sections,
           s->firmware_init_sections, s->firmware_wowlan_sections,
           s->firmware_secure_sections, s->firmware_cpu_count,
           s->firmware_load_chunks, s->firmware_load_bytes,
           s->firmware_largest_section, s->firmware_first_dst,
           wifi_fw_image_name(section->image), section->tlv_type, section->dst,
           section->len, (unsigned long)s->dma_phys, s->dma_chunk_bytes,
           s->dma_staged_bytes, s->pci_command);
  return 0;
}

int wifi_upload_firmware(int arm, char *report, size_t report_size) {
  const wifi_status_t *s;
  const wifi_fw_section_t *section = NULL;
  int rc = 0;

  if (!report || report_size == 0) {
    return -1;
  }

  if (wifi_load_firmware(report, report_size) != 0) {
    return -1;
  }

  if (wifi_prepare_fh_plan(&section) != 0 || !section) {
    s = &wifi_status_state;
    wifi_status_state.load_state = "wifi: FH plan failed";
    snprintf(report, report_size,
             "wifi upload: FH transfer plan failed\n"
             "dma=%s phys=0x%lx staged=%lu errors=%lu\n",
             s->dma_ready ? "ready" : "not-ready",
             (unsigned long)s->dma_phys, s->dma_staged_bytes,
             s->fh_errors);
    return -1;
  }

  if (arm) {
    if (wifi_activate_nic() != 0) {
      s = &wifi_status_state;
      wifi_status_state.load_state =
          "wifi: NIC APM wake failed before first FH transfer";
      snprintf(report, report_size,
               "wifi upload: NIC APM wake failed before transfer\n"
               "ready=%s timeout=%s gp=0x%08x hw-if=0x%08x gio=0x%08x "
               "hpet=0x%08x\n"
               "hint: run 'wifi apm' for detailed register state\n",
               s->apm_ready ? "yes" : "no",
               s->apm_timeout ? "yes" : "no", s->apm_last_gp,
               s->apm_last_hw_if, s->apm_last_gio, s->apm_last_hpet);
      return -1;
    }
    rc = wifi_arm_fh_current_chunk();
    wifi_status_state.load_state =
        rc == 0 ? "wifi: first FH firmware chunk completed"
                : "wifi: first FH firmware chunk failed";
  } else {
    wifi_status_state.load_state =
        "wifi: FH firmware upload plan ready; not armed";
  }

  s = &wifi_status_state;
  snprintf(report, report_size,
           "wifi upload: %s\n"
           "section: %s tlv=%u dst=0x%08x bytes=%u secure=%s\n"
           "dma: phys=0x%lx staged=%lu chunk=%lu\n"
           "fh: ch=%u ctrl0=0x%08x ctrl1=0x%08x buf=0x%08x cfg=0x%08x\n"
           "regs: csr-int=0x%08x fh-int=0x%08x tx-sts=0x%08x tx-err=0x%08x\n"
           "result: armed=%s complete=%s timeout=%s errors=%lu\n"
           "state: %s\n"
           "next: full section loop, boot CPU release, firmware alive event\n",
           arm ? "first FH transfer armed" : "FH transfer plan only",
           wifi_fw_image_name(section->image), section->tlv_type, section->dst,
           section->len, section->secure ? "yes" : "no",
           (unsigned long)s->dma_phys, s->dma_staged_bytes,
           s->dma_chunk_bytes, s->fh_channel, s->fh_ctrl0_value,
           s->fh_ctrl1_value, s->fh_buf_status_value, s->fh_tx_config_value,
           s->fh_last_csr_int, s->fh_last_fh_int, s->fh_last_tx_status,
           s->fh_last_tx_error, s->fh_armed ? "yes" : "no",
           s->fh_complete ? "yes" : "no", s->fh_timeout ? "yes" : "no",
           s->fh_errors, s->load_state);
  return rc;
}

int wifi_upload_all_firmware(int arm, char *report, size_t report_size) {
  const wifi_status_t *s;
  int rc = 0;

  if (!report || report_size == 0) {
    return -1;
  }

  if (wifi_load_firmware(report, report_size) != 0) {
    return -1;
  }

  wifi_status_state.fh_total_chunks = wifi_count_total_fw_chunks();
  wifi_status_state.fh_uploaded_chunks = 0;
  wifi_status_state.fh_uploaded_bytes = 0;
  wifi_status_state.fh_sections_done = 0;
  wifi_status_state.fh_all_plan_ready =
      wifi_status_state.fh_total_chunks > 0 && wifi_fw_section_count > 0;

  if (!wifi_status_state.fh_all_plan_ready) {
    s = &wifi_status_state;
    snprintf(report, report_size,
             "wifi upload all: no firmware chunks to upload\n"
             "sections=%lu chunks=%lu bytes=%lu\n",
             s->firmware_section_count, s->fh_total_chunks,
             s->firmware_load_bytes);
    return -1;
  }

  if (!arm) {
    const wifi_fw_section_t *first = wifi_first_upload_section();
    if (first) {
      wifi_prepare_fh_plan_for(first, 0, NULL);
    }
    s = &wifi_status_state;
    wifi_status_state.load_state =
        "wifi: full FH upload plan ready; not armed";
    snprintf(report, report_size,
             "wifi upload all: full FH plan ready\n"
             "firmware: %s source=%s\n"
             "sections=%lu runtime=%lu init=%lu wowlan=%lu secure=%lu\n"
             "chunks=%lu bytes=%lu chunk-size=%lu first-dst=0x%08x\n"
             "first-fh: ch=%u ctrl0=0x%08x ctrl1=0x%08x buf=0x%08x cfg=0x%08x\n"
             "state: plan only; use 'wifi upload all arm' for guarded transfer\n",
             s->firmware_name, s->firmware_source, s->firmware_section_count,
             s->firmware_runtime_sections, s->firmware_init_sections,
             s->firmware_wowlan_sections, s->firmware_secure_sections,
             s->fh_total_chunks, s->firmware_load_bytes, s->dma_chunk_bytes,
             s->firmware_first_dst, s->fh_channel, s->fh_ctrl0_value,
             s->fh_ctrl1_value, s->fh_buf_status_value,
             s->fh_tx_config_value);
    return 0;
  }

  if (wifi_activate_nic() != 0) {
    s = &wifi_status_state;
    wifi_status_state.load_state =
        "wifi: NIC APM wake failed before full FH transfer";
    snprintf(report, report_size,
             "wifi upload all: NIC APM wake failed before transfer\n"
             "ready=%s timeout=%s gp=0x%08x hw-if=0x%08x gio=0x%08x "
             "hpet=0x%08x\n"
             "progress: sections=0/%lu chunks=0/%lu bytes=0/%lu\n"
             "hint: run 'wifi apm' for detailed register state\n",
             s->apm_ready ? "yes" : "no",
             s->apm_timeout ? "yes" : "no", s->apm_last_gp,
             s->apm_last_hw_if, s->apm_last_gio, s->apm_last_hpet,
             s->firmware_section_count, s->fh_total_chunks,
             s->firmware_load_bytes);
    return -1;
  }

  for (int i = 0; i < wifi_fw_section_count; i++) {
    const wifi_fw_section_t *section = &wifi_fw_sections[i];
    uint32_t offset = 0;

    if (wifi_section_is_separator(section)) {
      continue;
    }

    while (offset < section->len) {
      if (wifi_prepare_fh_plan_for(section, offset, NULL) != 0) {
        rc = -1;
        break;
      }
      rc = wifi_arm_fh_current_chunk();
      if (rc != 0) {
        break;
      }
      wifi_status_state.fh_uploaded_chunks++;
      wifi_status_state.fh_uploaded_bytes += wifi_status_state.fh_last_len;
      offset += wifi_status_state.fh_last_len;
    }

    if (rc != 0) {
      break;
    }
    wifi_status_state.fh_sections_done++;
  }

  wifi_status_state.load_state =
      rc == 0 ? "wifi: all staged firmware FH chunks completed; alive pending"
              : "wifi: full FH firmware upload stopped on error";
  s = &wifi_status_state;
  snprintf(report, report_size,
           "wifi upload all: %s\n"
           "progress: sections=%lu/%lu chunks=%lu/%lu bytes=%lu/%lu\n"
           "last: section=%u offset=%u len=%u dst=0x%08x\n"
           "fh: ch=%u ctrl0=0x%08x ctrl1=0x%08x buf=0x%08x cfg=0x%08x\n"
           "regs: csr-int=0x%08x fh-int=0x%08x tx-sts=0x%08x tx-err=0x%08x\n"
           "result: complete=%s timeout=%s errors=%lu\n"
           "state: %s\n"
           "next: boot/release device CPU and wait for firmware alive event\n",
           rc == 0 ? "guarded transfer completed" : "guarded transfer failed",
           s->fh_sections_done, s->firmware_section_count,
           s->fh_uploaded_chunks, s->fh_total_chunks, s->fh_uploaded_bytes,
           s->firmware_load_bytes, s->fh_last_section, s->fh_last_offset,
           s->fh_last_len, s->fh_dst_addr, s->fh_channel, s->fh_ctrl0_value,
           s->fh_ctrl1_value, s->fh_buf_status_value, s->fh_tx_config_value,
           s->fh_last_csr_int, s->fh_last_fh_int, s->fh_last_tx_status,
           s->fh_last_tx_error, s->fh_complete ? "yes" : "no",
           s->fh_timeout ? "yes" : "no", s->fh_errors, s->load_state);
  return rc;
}

int wifi_boot_firmware(int arm, char *report, size_t report_size) {
  const wifi_status_t *s;
  unsigned long plan_cpu1 = 0;
  unsigned long plan_cpu2 = 0;
  unsigned long plan_paging = 0;
  unsigned long boot_chunks = 0;
  unsigned long boot_bytes = 0;
  uint32_t plan_cpu2_index = 0;
  uint32_t plan_paging_index = 0;
  int next_index = 0;
  int rc = 0;

  if (!report || report_size == 0) {
    return -1;
  }

  if (wifi_load_firmware(report, report_size) != 0) {
    return -1;
  }

  wifi_count_boot_plan(&plan_cpu1, &plan_cpu2, &plan_paging,
                       &plan_cpu2_index, &plan_paging_index);
  boot_chunks = wifi_count_boot_fw_chunks();
  boot_bytes = wifi_count_boot_fw_bytes();

  if (!arm) {
    s = &wifi_status_state;
    snprintf(report, report_size,
             "wifi boot: firmware CPU boot plan ready\n"
             "firmware: %s source=%s cpus=%u\n"
             "sections: cpu1=%lu cpu2=%lu paging-skipped=%lu total=%lu\n"
             "payload: boot-chunks=%lu boot-bytes=%lu\n"
             "separators: cpu2-index=%u paging-index=%u\n"
             "status-regs: fh=0x%08x ureg=0x%08x release=0x%08x\n"
             "state: plan only; use 'wifi boot arm' for guarded CPU release/load\n",
             s->firmware_name, s->firmware_source, s->firmware_cpu_count,
             plan_cpu1, plan_cpu2, plan_paging, s->firmware_section_count,
             boot_chunks, boot_bytes, plan_cpu2_index, plan_paging_index,
             s->boot_fh_status, s->boot_ureg_status, s->boot_release_value);
    return 0;
  }

  if (plan_cpu1 == 0) {
    snprintf(report, report_size,
             "wifi boot: no CPU1 firmware sections found\n"
             "sections=%lu parse-errors=%lu valid=%s\n",
             wifi_status_state.firmware_section_count,
             wifi_status_state.firmware_parse_errors,
             wifi_status_state.firmware_valid ? "yes" : "no");
    return -1;
  }

  wifi_status_state.boot_attempts++;
  wifi_reset_boot_runtime();
  wifi_status_state.fh_total_chunks = boot_chunks;
  wifi_status_state.fh_uploaded_chunks = 0;
  wifi_status_state.fh_uploaded_bytes = 0;
  wifi_status_state.fh_sections_done = 0;
  wifi_status_state.boot_first_cpu2_index = plan_cpu2_index;
  wifi_status_state.boot_paging_index = plan_paging_index;
  wifi_status_state.boot_paging_sections_skipped = plan_paging;

  if (wifi_activate_nic() != 0) {
    s = &wifi_status_state;
    wifi_status_state.boot_failed = 1;
    wifi_status_state.load_state =
        "wifi: NIC APM wake failed before firmware CPU boot";
    snprintf(report, report_size,
             "wifi boot: NIC APM wake failed\n"
             "apm: ready=%s timeout=%s gp=0x%08x hw-if=0x%08x gio=0x%08x\n"
             "hint: run 'wifi apm' for detailed register state\n",
             s->apm_ready ? "yes" : "no",
             s->apm_timeout ? "yes" : "no", s->apm_last_gp,
             s->apm_last_hw_if, s->apm_last_gio);
    return -1;
  }

  if (wifi_boot_prepare_cpu_release() != 0) {
    s = &wifi_status_state;
    wifi_status_state.boot_failed = 1;
    wifi_status_state.load_state =
        "wifi: firmware CPU release register write failed";
    snprintf(report, report_size,
             "wifi boot: CPU release failed\n"
             "release=0x%08x gp=0x%08x prph-mask=0x%08x\n"
             "hint: capture this output; PRPH access may be blocked\n",
             s->boot_release_value, s->boot_last_gp, wifi_prph_mask());
    return -1;
  }

  rc = wifi_boot_load_cpu_sections(1, 0, &next_index);
  if (rc == 0) {
    rc = wifi_boot_mark_cpu_done(1);
  }

  if (rc == 0 && plan_cpu2 > 0) {
    int cpu2_start = next_index + 1;
    rc = wifi_boot_load_cpu_sections(2, cpu2_start, &next_index);
    if (rc == 0) {
      rc = wifi_boot_mark_cpu_done(2);
    }
  }

  wifi_status_state.boot_ready = rc == 0;
  wifi_status_state.boot_failed = rc != 0;
  wifi_status_state.load_state =
      rc == 0 ? "wifi: firmware CPU boot/load sequence completed; alive pending"
              : "wifi: firmware CPU boot/load sequence failed";

  s = &wifi_status_state;
  snprintf(report, report_size,
           "wifi boot: %s\n"
           "progress: cpu1=%lu/%lu cpu2=%lu/%lu paging-skipped=%lu\n"
           "fh: sections=%lu chunks=%lu/%lu bytes=%lu/%lu errors=%lu\n"
           "last: section=%u offset=%u len=%u dst=0x%08x\n"
           "regs: release=0x%08x fh-status=0x%08x ureg-status=0x%08x "
           "gp=0x%08x csr-int=0x%08x fh-int=0x%08x\n"
           "result: released=%s boot-ready=%s failed=%s\n"
           "next: run 'wifi alive' to wait for firmware ALIVE\n",
           rc == 0 ? "CPU release/load sequence completed"
                   : "CPU release/load sequence failed",
           s->boot_cpu1_sections, plan_cpu1, s->boot_cpu2_sections,
           plan_cpu2, s->boot_paging_sections_skipped,
           s->fh_sections_done, s->fh_uploaded_chunks, s->fh_total_chunks,
           s->fh_uploaded_bytes, boot_bytes, s->fh_errors,
           s->fh_last_section, s->fh_last_offset, s->fh_last_len,
           s->fh_dst_addr, s->boot_release_value, s->boot_fh_status,
           s->boot_ureg_status, s->boot_last_gp, s->fh_last_csr_int,
           s->fh_last_fh_int, s->boot_released ? "yes" : "no",
           s->boot_ready ? "yes" : "no", s->boot_failed ? "yes" : "no");
  return rc;
}

int wifi_alive_probe(char *report, size_t report_size) {
  const wifi_status_t *s;

  if (!report || report_size == 0) {
    return -1;
  }

  wifi_init();
  s = &wifi_status_state;

  if (!s->present) {
    snprintf(report, report_size,
             "wifi alive: no PCI wireless controller detected\n");
    return -1;
  }

  if (!s->firmware_present) {
    wifi_find_firmware();
    s = &wifi_status_state;
  }

  if (!s->firmware_present) {
    snprintf(report, report_size,
             "wifi alive: firmware missing for %s (%04x:%04x)\n"
             "run: wifi firmware\n",
             s->chipset, s->vendor_id, s->device_id);
    return -1;
  }

  if (!s->boot_ready) {
    snprintf(report, report_size,
             "wifi alive: firmware CPU boot sequence has not completed yet\n"
             "boot: released=%s ready=%s failed=%s cpu1=%lu cpu2=%lu "
             "fh=%lu/%lu chunks\n"
             "run: wifi boot arm\n",
             s->boot_released ? "yes" : "no", s->boot_ready ? "yes" : "no",
             s->boot_failed ? "yes" : "no", s->boot_cpu1_sections,
             s->boot_cpu2_sections, s->fh_uploaded_chunks,
             s->fh_total_chunks);
    return -1;
  }

  if (!s->apm_ready && wifi_activate_nic() != 0) {
    s = &wifi_status_state;
    snprintf(report, report_size,
             "wifi alive: NIC is not awake, cannot wait for firmware alive\n"
             "apm: ready=%s timeout=%s gp=0x%08x hw-if=0x%08x\n"
             "hint: run 'wifi apm' then 'wifi boot arm'\n",
             s->apm_ready ? "yes" : "no",
             s->apm_timeout ? "yes" : "no", s->apm_last_gp,
             s->apm_last_hw_if);
    return -1;
  }

  wifi_status_state.alive_seen = 0;
  wifi_status_state.alive_timeout = 0;
  wifi_status_state.alive_polls = 0;

  for (uint32_t i = 0; i < WIFI_ALIVE_POLL_LOOPS; i++) {
    uint32_t csr_int = wifi_csr_read32(CSR_INT);
    uint32_t fh_int = wifi_csr_read32(CSR_FH_INT_STATUS);
    uint32_t gp = wifi_csr_read32(CSR_GP_CNTRL);

    wifi_status_state.alive_last_csr_int = csr_int;
    wifi_status_state.alive_last_fh_int = fh_int;
    wifi_status_state.alive_last_gp = gp;
    wifi_status_state.alive_polls++;

    if (csr_int & CSR_INT_BIT_ALIVE) {
      wifi_status_state.alive_seen = 1;
      wifi_status_state.alive_timeout = 0;
      wifi_status_state.status =
          "wifi: firmware alive interrupt observed; queues pending";
      wifi_csr_write32(CSR_INT, CSR_INT_BIT_ALIVE);
      s = &wifi_status_state;
      snprintf(report, report_size,
               "wifi alive: firmware alive interrupt observed\n"
               "polls=%lu csr-int=0x%08x fh-int=0x%08x gp=0x%08x\n"
               "apm: ready=%s profile=%s mask=0x%08x\n"
               "fh: uploaded=%lu/%lu chunks bytes=%lu/%lu complete=%s errors=%lu\n"
               "state: firmware signalled alive; run 'wifi queues arm' for host rings\n",
               s->alive_polls, s->alive_last_csr_int, s->alive_last_fh_int,
               s->alive_last_gp, s->apm_ready ? "yes" : "no",
               wifi_uses_bz_apm_profile() ? "bz/mac-init" : "legacy/init-done",
               s->apm_poll_ready_mask, s->fh_uploaded_chunks,
               s->fh_total_chunks, s->fh_uploaded_bytes,
               s->firmware_load_bytes, s->fh_complete ? "yes" : "no",
               s->fh_errors);
      return 0;
    }

    if (csr_int & (CSR_INT_BIT_HW_ERR | CSR_INT_BIT_SW_ERR)) {
      wifi_status_state.alive_errors++;
      wifi_status_state.status =
          "wifi: firmware alive wait stopped on hardware/software error";
      s = &wifi_status_state;
      snprintf(report, report_size,
               "wifi alive: stopped on hardware/software error\n"
               "polls=%lu csr-int=0x%08x fh-int=0x%08x gp=0x%08x\n"
               "errors=%lu hw-err=%s sw-err=%s alive=%s\n"
               "hint: capture this output; next step is queue/context setup diagnostics\n",
               s->alive_polls, s->alive_last_csr_int, s->alive_last_fh_int,
               s->alive_last_gp, s->alive_errors,
               wifi_bit_text(s->alive_last_csr_int, CSR_INT_BIT_HW_ERR),
               wifi_bit_text(s->alive_last_csr_int, CSR_INT_BIT_SW_ERR),
               wifi_bit_text(s->alive_last_csr_int, CSR_INT_BIT_ALIVE));
      return -1;
    }

    __asm__ volatile("pause");
  }

  wifi_status_state.alive_timeout = 1;
  wifi_status_state.status = "wifi: firmware alive wait timed out";
  s = &wifi_status_state;
  snprintf(report, report_size,
           "wifi alive: timeout waiting for firmware alive\n"
           "polls=%lu csr-int=0x%08x fh-int=0x%08x gp=0x%08x\n"
           "apm: ready=%s timeout=%s mask=0x%08x\n"
           "fh: uploaded=%lu/%lu chunks bytes=%lu/%lu complete=%s errors=%lu\n"
           "next: run 'wifi queues' to inspect host rings, then hardware context setup\n",
           s->alive_polls, s->alive_last_csr_int, s->alive_last_fh_int,
           s->alive_last_gp, s->apm_ready ? "yes" : "no",
           s->apm_timeout ? "yes" : "no", s->apm_poll_ready_mask,
           s->fh_uploaded_chunks, s->fh_total_chunks, s->fh_uploaded_bytes,
           s->firmware_load_bytes, s->fh_complete ? "yes" : "no",
           s->fh_errors);
  return -1;
}

int wifi_queue_probe(int arm, char *report, size_t report_size) {
  const wifi_status_t *s;
  int rc;

  if (!report || report_size == 0) {
    return -1;
  }

  wifi_init();
  s = &wifi_status_state;

  if (!s->present) {
    snprintf(report, report_size,
             "wifi queues: no PCI wireless controller detected\n");
    return -1;
  }

  if (s->vendor_id != 0x8086) {
    snprintf(report, report_size,
             "wifi queues: unsupported controller %04x:%04x\n",
             s->vendor_id, s->device_id);
    return -1;
  }

  rc = wifi_prepare_host_queues();
  s = &wifi_status_state;
  if (rc != 0) {
    snprintf(report, report_size,
             "wifi queues: host queue staging failed\n"
             "errors=%lu cmd-tfd=0x%lx cmd-buf=0x%lx tx-tfd=0x%lx "
             "tx-buf=0x%lx rx-desc=0x%lx rx-buf=0x%lx\n"
             "hint: DMA address translation must be fixed before queue setup\n",
             s->queue_errors, (unsigned long)s->cmd_tfd_phys,
             (unsigned long)s->cmd_buffer_phys,
             (unsigned long)s->tx_tfd_phys,
             (unsigned long)s->tx_buffer_phys,
             (unsigned long)s->rx_desc_phys,
             (unsigned long)s->rx_buffer_phys);
    return -1;
  }

  if (arm) {
    if (!s->boot_ready) {
      wifi_status_state.queues_failed = 1;
      wifi_status_state.queue_errors++;
      snprintf(report, report_size,
               "wifi queues: host queues staged, but firmware boot is not ready\n"
               "boot: released=%s ready=%s failed=%s alive=%s\n"
               "run: wifi boot arm, then wifi queues arm\n",
               s->boot_released ? "yes" : "no",
               s->boot_ready ? "yes" : "no",
               s->boot_failed ? "yes" : "no",
               s->alive_seen ? "yes" : "no");
      return -1;
    }
    wifi_status_state.queues_armed = 1;
    wifi_status_state.status =
        "wifi: host queues armed; hardware scheduler programming pending";
  }

  s = &wifi_status_state;
  snprintf(report, report_size,
           "wifi queues: %s\n"
           "rings: cmd=%lu tx=%lu rx=%lu generation=%lu\n"
           "bytes: cmd-tfd=%lu cmd-buf=%lu tx-tfd=%lu tx-buf=%lu "
           "rx-desc=%lu rx-buf=%lu\n"
           "dma: cmd-tfd=0x%lx cmd-buf=0x%lx tx-tfd=0x%lx tx-buf=0x%lx\n"
           "dma: rx-desc=0x%lx rx-used=0x%lx rx-status=0x%lx "
           "rx-buf=0x%lx cmd-bc=0x%lx tx-bc=0x%lx first-rx=0x%lx\n"
           "ptrs: cmd-w=%u tx-w=%u rx-w=%u armed=%s errors=%lu\n"
           "state: host-side rings only; hardware context/scheduler registers "
           "are not programmed yet\n"
           "next: run 'wifi context' to stage firmware context-info\n",
           arm ? "host queues armed" : "host queues staged",
           s->cmd_queue_entries, s->tx_queue_entries, s->rx_queue_entries,
           s->queue_generation, s->cmd_tfd_bytes, s->cmd_buffer_bytes,
           s->tx_tfd_bytes, s->tx_buffer_bytes, s->rx_desc_bytes,
           s->rx_buffer_bytes, (unsigned long)s->cmd_tfd_phys,
           (unsigned long)s->cmd_buffer_phys, (unsigned long)s->tx_tfd_phys,
           (unsigned long)s->tx_buffer_phys, (unsigned long)s->rx_desc_phys,
           (unsigned long)s->rx_used_desc_phys,
           (unsigned long)s->rx_status_phys,
           (unsigned long)s->rx_buffer_phys,
           (unsigned long)s->cmd_bc_phys,
           (unsigned long)s->tx_bc_phys,
           (unsigned long)wifi_rx_desc[0].addr, s->cmd_write_ptr,
           s->tx_write_ptr, s->rx_write_ptr,
           s->queues_armed ? "yes" : "no", s->queue_errors);
  return 0;
}

int wifi_context_probe(int arm, char *report, size_t report_size) {
  const wifi_status_t *s;
  const char *mode;
  uint32_t planned_boot_ctrl =
      CSR_AUTO_FUNC_BOOT_ENA | CSR_AUTO_FUNC_INIT;
  int rc;

  if (!report || report_size == 0) {
    return -1;
  }

  wifi_init();
  s = &wifi_status_state;

  if (!s->present) {
    snprintf(report, report_size,
             "wifi context: no PCI wireless controller detected\n");
    return -1;
  }

  if (s->vendor_id != 0x8086) {
    snprintf(report, report_size,
             "wifi context: unsupported controller %04x:%04x\n",
             s->vendor_id, s->device_id);
    return -1;
  }

  rc = wifi_prepare_context_info();
  s = &wifi_status_state;
  if (rc != 0) {
    snprintf(report, report_size,
             "wifi context: staging failed\n"
             "errors=%lu legacy=0x%lx v2=0x%lx scratch=0x%lx "
             "prph=0x%lx\n"
             "queues: ready=%s armed=%s rx-desc=0x%lx rx-used=0x%lx "
             "rx-status=0x%lx\n"
             "firmware: valid=%s sections=%lu source=%s\n",
             s->context_errors, (unsigned long)s->legacy_context_phys,
             (unsigned long)s->v2_context_phys,
             (unsigned long)s->prph_scratch_phys,
             (unsigned long)s->prph_info_phys,
             s->queues_ready ? "yes" : "no",
             s->queues_armed ? "yes" : "no",
             (unsigned long)s->rx_desc_phys,
             (unsigned long)s->rx_used_desc_phys,
             (unsigned long)s->rx_status_phys,
             s->firmware_valid ? "yes" : "no",
             s->firmware_section_count, s->firmware_source);
    return -1;
  }

  if (arm) {
    if (!s->queues_armed) {
      wifi_status_state.context_errors++;
      snprintf(report, report_size,
               "wifi context: context-info staged, but queues are not armed\n"
               "run: wifi boot arm, wifi alive, wifi queues arm, then "
               "wifi context arm\n"
               "mode=%s legacy=0x%lx v2=0x%lx scratch=0x%lx\n",
               s->context_mode == WIFI_CONTEXT_MODE_V2_LITE ? "v2-lite"
                                                            : "legacy",
               (unsigned long)s->legacy_context_phys,
               (unsigned long)s->v2_context_phys,
               (unsigned long)s->prph_scratch_phys);
      return -1;
    }
    wifi_status_state.context_armed = 1;
    wifi_status_state.status =
        "wifi: firmware context-info armed; CSR kick deferred";
  }

  s = &wifi_status_state;
  mode = s->context_mode == WIFI_CONTEXT_MODE_V2_LITE ? "v2-lite" : "legacy";
  snprintf(report, report_size,
           "wifi context: %s\n"
           "mode=%s generation=%lu legacy-size=%u v2-size=%u scratch-size=%u\n"
           "dram-map: lmac=%lu umac=%lu paging=%lu chunks of up to %u bytes\n"
           "dma: legacy=0x%lx v2=0x%lx prph-info=0x%lx scratch=0x%lx\n"
           "rings: mtr=0x%lx mcr=0x%lx entries=%u/%u msg-bytes=%u\n"
           "idx: cr-head=0x%lx cr-tail=0x%lx tr-head=0x%lx tr-tail=0x%lx\n"
           "queues: cmd=0x%lx rx-free=0x%lx rx-used=0x%lx "
           "rx-status=0x%lx entries cmd=%lu rx=%lu\n"
           "flags: legacy=0x%08x prph=0x%08x planned-csr=0x%03x "
           "value=0x%08x:%08x boot-ctrl=0x%08x\n"
           "state: CSR kick intentionally deferred on this manual boot path; "
           "next step is scheduler/message-ring command setup\n",
           arm ? "context-info armed" : "context-info staged", mode,
           s->context_generation, s->context_size, s->context_v2_size,
           s->prph_scratch_size, s->context_lmac_sections,
           s->context_umac_sections, s->context_paging_sections,
           WIFI_CONTEXT_DRAM_CHUNK_BYTES,
           (unsigned long)s->legacy_context_phys,
           (unsigned long)s->v2_context_phys,
           (unsigned long)s->prph_info_phys,
           (unsigned long)s->prph_scratch_phys,
           (unsigned long)s->mtr_ring_phys,
           (unsigned long)s->mcr_ring_phys, s->mtr_entries, s->mcr_entries,
           s->msg_ring_bytes, (unsigned long)s->cr_head_phys,
           (unsigned long)s->cr_tail_phys, (unsigned long)s->tr_head_phys,
           (unsigned long)s->tr_tail_phys,
           (unsigned long)s->cmd_tfd_phys,
           (unsigned long)s->rx_desc_phys,
           (unsigned long)s->rx_used_desc_phys,
           (unsigned long)s->rx_status_phys,
           s->cmd_queue_entries, s->rx_queue_entries,
           s->context_control_flags, s->context_v2_control_flags,
           s->context_csr_target, s->context_csr_value_hi,
           s->context_csr_value_lo, planned_boot_ctrl);
  return 0;
}

static uint16_t wifi_gen2_byte_count(uint32_t bytes, uint32_t tbs) {
  uint32_t len_dw = (bytes + 3U) / 4U;
  uint32_t filled_tfd = sizeof(uint16_t) + (tbs * sizeof(wifi_tfh_tb_t));
  uint32_t fetch_chunks = (filled_tfd + 63U) / 64U;

  if (fetch_chunks > 0) {
    fetch_chunks--;
  }
  if (len_dw > 0x0fffU) {
    len_dw = 0x0fffU;
  }
  return (uint16_t)(len_dw | ((fetch_chunks & 0x3U) << 12));
}

static void wifi_scheduler_mark_failure(const char *status) {
  wifi_status_state.scheduler_ready = 0;
  wifi_status_state.scheduler_armed = 0;
  wifi_status_state.scheduler_failed = 1;
  wifi_status_state.scheduler_errors++;
  wifi_status_state.status = status;
}

static int wifi_prepare_scheduler_command(void) {
  uint32_t idx;
  uint32_t next_idx;
  uint32_t command_len;
  uint64_t frame_phys;
  wifi_scheduler_cmd_frame_t *frame;
  wifi_tfh_tfd_t *tfd;

  if (!wifi_status_state.context_ready && wifi_prepare_context_info() != 0) {
    wifi_scheduler_mark_failure(
        "wifi: scheduler setup needs firmware context first");
    return -1;
  }
  if (!wifi_status_state.context_ready || !wifi_status_state.queues_ready) {
    wifi_scheduler_mark_failure(
        "wifi: scheduler setup needs staged queues and context");
    return -1;
  }

  if (!wifi_status_state.cmd_bc_phys) {
    wifi_status_state.cmd_bc_phys = wifi_phys_addr(wifi_cmd_bc_tbl);
  }
  if (!wifi_status_state.cmd_bc_phys) {
    wifi_scheduler_mark_failure(
        "wifi: scheduler byte-count DMA address missing");
    return -1;
  }

  idx = wifi_status_state.cmd_write_ptr % WIFI_CMD_QUEUE_ENTRIES;
  next_idx = (idx + 1U) % WIFI_CMD_QUEUE_ENTRIES;
  frame = (wifi_scheduler_cmd_frame_t *)wifi_cmd_buffers[idx];
  tfd = (wifi_tfh_tfd_t *)wifi_cmd_tfd[idx];
  command_len = sizeof(*frame);
  frame_phys = wifi_phys_addr(frame);

  if (!frame_phys) {
    wifi_scheduler_mark_failure(
        "wifi: scheduler command DMA address translation failed");
    return -1;
  }

  memset(frame, 0, sizeof(*frame));
  memset(tfd, 0, sizeof(*tfd));

  frame->header.cmd = WIFI_CMD_SCD_QUEUE_CFG;
  frame->header.group_id = WIFI_CMD_GROUP_LONG;
  frame->header.sequence =
      (uint16_t)((WIFI_DQA_CMD_QUEUE << 8) | (idx & 0xffU));
  frame->header.length = sizeof(frame->queue_cfg);
  frame->header.version = WIFI_CMD_VERSION_TX_QUEUE_CFG;
  frame->queue_cfg.sta_id = 0;
  frame->queue_cfg.tid = 0;
  frame->queue_cfg.flags = WIFI_TX_QUEUE_CFG_ENABLE_QUEUE;
  frame->queue_cfg.cb_size = WIFI_CMD_QUEUE_CB_SIZE_VALUE;
  frame->queue_cfg.byte_cnt_addr = wifi_status_state.cmd_bc_phys;
  frame->queue_cfg.tfdq_addr = wifi_status_state.cmd_tfd_phys;

  tfd->num_tbs = 1;
  tfd->tbs[0].tb_len = (uint16_t)command_len;
  tfd->tbs[0].addr = frame_phys;
  wifi_cmd_bc_tbl[idx].tfd_offset =
      wifi_gen2_byte_count(command_len, tfd->num_tbs);

  wifi_status_state.scheduler_ready = 1;
  wifi_status_state.scheduler_failed = 0;
  wifi_status_state.scheduler_generation++;
  wifi_status_state.scheduler_cmd_id = WIFI_CMD_SCD_QUEUE_CFG;
  wifi_status_state.scheduler_cmd_group = WIFI_CMD_GROUP_LONG;
  wifi_status_state.scheduler_cmd_version = WIFI_CMD_VERSION_TX_QUEUE_CFG;
  wifi_status_state.scheduler_cmd_len = command_len;
  wifi_status_state.scheduler_cmd_sequence = frame->header.sequence;
  wifi_status_state.scheduler_cmd_queue = WIFI_DQA_CMD_QUEUE;
  wifi_status_state.scheduler_cmd_index = idx;
  wifi_status_state.scheduler_cmd_tbs = tfd->num_tbs;
  wifi_status_state.scheduler_cmd_tb_len = command_len;
  wifi_status_state.scheduler_wptr_value =
      (WIFI_DQA_CMD_QUEUE << HBUS_TARG_WRPTR_Q_SHIFT) | next_idx;
  wifi_status_state.command_ready = 1;
  wifi_status_state.command_failed = 0;
  wifi_status_state.command_doorbell_value =
      wifi_status_state.scheduler_wptr_value;
  wifi_status_state.status =
      "wifi: scheduler command frame staged for firmware command queue";
  return 0;
}

int wifi_scheduler_probe(int arm, char *report, size_t report_size) {
  const wifi_status_t *s;
  int rc;

  if (!report || report_size == 0) {
    return -1;
  }

  wifi_init();
  s = &wifi_status_state;

  if (!s->present) {
    snprintf(report, report_size,
             "wifi scheduler: no PCI wireless controller detected\n");
    return -1;
  }

  if (s->vendor_id != 0x8086) {
    snprintf(report, report_size,
             "wifi scheduler: unsupported controller %04x:%04x\n",
             s->vendor_id, s->device_id);
    return -1;
  }

  rc = wifi_prepare_scheduler_command();
  s = &wifi_status_state;
  if (rc != 0) {
    snprintf(report, report_size,
             "wifi scheduler: staging failed\n"
             "errors=%lu queues=%s context=%s cmd-bc=0x%lx tx-bc=0x%lx\n"
             "rings: mtr=0x%lx mcr=0x%lx cr-head=0x%lx tr-tail=0x%lx\n",
             s->scheduler_errors,
             s->queues_ready ? (s->queues_armed ? "armed" : "staged")
                             : "idle",
             s->context_ready ? (s->context_armed ? "armed" : "staged")
                              : "idle",
             (unsigned long)s->cmd_bc_phys, (unsigned long)s->tx_bc_phys,
             (unsigned long)s->mtr_ring_phys,
             (unsigned long)s->mcr_ring_phys,
             (unsigned long)s->cr_head_phys,
             (unsigned long)s->tr_tail_phys);
    return -1;
  }

  if (arm) {
    if (!s->context_armed) {
      wifi_status_state.scheduler_errors++;
      snprintf(report, report_size,
               "wifi scheduler: command frame staged, but context is not armed\n"
               "run: wifi context arm, then wifi scheduler arm\n"
               "planned-doorbell: CSR[0x%03x]=0x%08x queue=%u index=%u\n",
               HBUS_TARG_WRPTR, s->scheduler_wptr_value,
               s->scheduler_cmd_queue, s->scheduler_cmd_index);
      return -1;
    }
    wifi_status_state.scheduler_armed = 1;
    wifi_status_state.status =
        "wifi: scheduler command armed; doorbell deferred";
  }

  s = &wifi_status_state;
  snprintf(report, report_size,
           "wifi scheduler: %s\n"
           "cmd: id=0x%02x group=0x%02x version=%u len=%u "
           "seq=0x%04x queue=%u index=%u tbs=%u tb-len=%u\n"
           "dma: cmd-tfd=0x%lx cmd-buf=0x%lx cmd-bc=0x%lx "
           "tx-bc=0x%lx\n"
           "rings: mtr=0x%lx mcr=0x%lx entries=%u/%u msg-bytes=%u\n"
           "idx: cr-head=0x%lx cr-tail=0x%lx tr-head=0x%lx tr-tail=0x%lx\n"
           "doorbell: planned CSR[0x%03x]=0x%08x generation=%lu errors=%lu\n"
           "state: command frame and byte-count table are ready; actual "
           "doorbell waits for RX response handling\n",
           arm ? "scheduler command armed" : "scheduler command staged",
           s->scheduler_cmd_id, s->scheduler_cmd_group,
           s->scheduler_cmd_version, s->scheduler_cmd_len,
           s->scheduler_cmd_sequence, s->scheduler_cmd_queue,
           s->scheduler_cmd_index, s->scheduler_cmd_tbs,
           s->scheduler_cmd_tb_len, (unsigned long)s->cmd_tfd_phys,
           (unsigned long)s->cmd_buffer_phys, (unsigned long)s->cmd_bc_phys,
           (unsigned long)s->tx_bc_phys, (unsigned long)s->mtr_ring_phys,
           (unsigned long)s->mcr_ring_phys, s->mtr_entries, s->mcr_entries,
           s->msg_ring_bytes, (unsigned long)s->cr_head_phys,
           (unsigned long)s->cr_tail_phys, (unsigned long)s->tr_head_phys,
           (unsigned long)s->tr_tail_phys, HBUS_TARG_WRPTR,
           s->scheduler_wptr_value, s->scheduler_generation,
           s->scheduler_errors);
  return 0;
}

static uint32_t wifi_rx_closed_status(void) {
  volatile uint16_t *status = (volatile uint16_t *)wifi_rx_status;
  return (uint32_t)(status[0] & (WIFI_RX_QUEUE_ENTRIES - 1U));
}

static int wifi_rx_parse_one(void) {
  uint32_t closed = wifi_rx_closed_status();
  uint32_t idx = wifi_status_state.rx_read_ptr & (WIFI_RX_QUEUE_ENTRIES - 1U);
  wifi_rx_completion_desc_t *cd;
  wifi_rx_packet_t *pkt;
  uint32_t rbid;

  wifi_status_state.rx_closed_rb = closed;
  if (idx == closed) {
    return 0;
  }

  cd = &wifi_rx_used_desc[idx];
  rbid = cd->rbid;
  wifi_status_state.rx_last_index = idx;
  wifi_status_state.rx_last_rbid = rbid;
  wifi_status_state.rx_last_flags = cd->flags;

  if (rbid == 0 || rbid > WIFI_RX_QUEUE_ENTRIES) {
    wifi_status_state.rx_path_failed = 1;
    wifi_status_state.rx_path_errors++;
    wifi_status_state.status = "wifi: RX completion had invalid RBID";
    return -1;
  }

  pkt = (wifi_rx_packet_t *)wifi_rx_buffers[rbid - 1U];
  wifi_status_state.rx_last_len_n_flags = pkt->len_n_flags;
  wifi_status_state.rx_last_len =
      pkt->len_n_flags & FH_RSCSR_FRAME_SIZE_MSK;
  wifi_status_state.rx_last_queue =
      (pkt->len_n_flags & FH_RSCSR_RXQ_MASK) >> FH_RSCSR_RXQ_POS;
  wifi_status_state.rx_last_cmd = pkt->header.cmd;
  wifi_status_state.rx_last_group = pkt->header.group_id;
  wifi_status_state.rx_last_sequence = pkt->header.sequence;

  if (pkt->len_n_flags == FH_RSCSR_FRAME_INVALID) {
    wifi_status_state.rx_path_failed = 1;
    wifi_status_state.rx_path_errors++;
    wifi_status_state.status = "wifi: RX packet marked invalid by firmware";
    return -1;
  }

  wifi_status_state.rx_read_ptr =
      (wifi_status_state.rx_read_ptr + 1U) & (WIFI_RX_QUEUE_ENTRIES - 1U);
  wifi_status_state.rx_packets++;
  wifi_status_state.rx_path_ready = 1;
  wifi_status_state.rx_path_failed = 0;
  wifi_status_state.status = "wifi: RX firmware response parsed";
  return 1;
}

int wifi_rx_probe(int poll, char *report, size_t report_size) {
  const wifi_status_t *s;
  int parsed = 0;
  int rc = 0;

  if (!report || report_size == 0) {
    return -1;
  }

  wifi_init();
  s = &wifi_status_state;

  if (!s->present) {
    snprintf(report, report_size,
             "wifi rx: no PCI wireless controller detected\n");
    return -1;
  }

  if (!s->queues_ready && wifi_prepare_host_queues() != 0) {
    s = &wifi_status_state;
    snprintf(report, report_size,
             "wifi rx: RX path staging failed\n"
             "errors=%lu rx-free=0x%lx rx-used=0x%lx rx-status=0x%lx\n",
             s->rx_path_errors, (unsigned long)s->rx_desc_phys,
             (unsigned long)s->rx_used_desc_phys,
             (unsigned long)s->rx_status_phys);
    return -1;
  }

  if (poll) {
    for (uint32_t i = 0; i < WIFI_COMMAND_POLL_LOOPS; i++) {
      wifi_status_state.rx_polls++;
      rc = wifi_rx_parse_one();
      if (rc != 0) {
        parsed = rc > 0 ? 1 : -1;
        break;
      }
      __asm__ volatile("pause");
    }
  } else {
    wifi_status_state.rx_polls++;
    rc = wifi_rx_parse_one();
    parsed = rc > 0 ? 1 : (rc < 0 ? -1 : 0);
  }

  s = &wifi_status_state;
  snprintf(report, report_size,
           "wifi rx: %s\n"
           "state: ready=%s failed=%s polls=%lu packets=%lu errors=%lu\n"
           "ring: closed=%u read=%u rx-free=0x%lx rx-used=0x%lx "
           "rx-status=0x%lx\n"
           "last: idx=%u rbid=%u flags=0x%02x len-flags=0x%08x "
           "len=%u queue=%u\n"
           "last-hdr: cmd=0x%02x group=0x%02x seq=0x%04x "
           "response-match=%s\n",
           parsed > 0 ? "firmware response parsed"
                      : (parsed < 0 ? "RX completion error"
                                    : "no new firmware response"),
           s->rx_path_ready ? "yes" : "no",
           s->rx_path_failed ? "yes" : "no", s->rx_polls, s->rx_packets,
           s->rx_path_errors, s->rx_closed_rb, s->rx_read_ptr,
           (unsigned long)s->rx_desc_phys,
           (unsigned long)s->rx_used_desc_phys,
           (unsigned long)s->rx_status_phys, s->rx_last_index,
           s->rx_last_rbid, s->rx_last_flags, s->rx_last_len_n_flags,
           s->rx_last_len, s->rx_last_queue, s->rx_last_cmd,
           s->rx_last_group, s->rx_last_sequence,
           (s->rx_last_sequence == s->scheduler_cmd_sequence &&
            s->rx_last_sequence != 0)
               ? "yes"
               : "no");
  return parsed < 0 ? -1 : 0;
}

static void wifi_command_mark_failure(const char *status) {
  wifi_status_state.command_ready = 0;
  wifi_status_state.command_sent = 0;
  wifi_status_state.command_failed = 1;
  wifi_status_state.command_errors++;
  wifi_status_state.status = status;
}

int wifi_command_probe(int arm, char *report, size_t report_size) {
  const wifi_status_t *s;
  unsigned long packets_before;
  int response_seen = 0;
  int rc;

  if (!report || report_size == 0) {
    return -1;
  }

  wifi_init();
  s = &wifi_status_state;

  if (!s->present) {
    snprintf(report, report_size,
             "wifi command: no PCI wireless controller detected\n");
    return -1;
  }

  rc = wifi_prepare_scheduler_command();
  s = &wifi_status_state;
  if (rc != 0) {
    snprintf(report, report_size,
             "wifi command: scheduler command staging failed\n"
             "scheduler=%s command-errors=%lu\n",
             s->scheduler_ready ? "ready" : "not-ready", s->command_errors);
    return -1;
  }

  if (!arm) {
    snprintf(report, report_size,
             "wifi command: ready, not sent\n"
             "doorbell: CSR[0x%03x]=0x%08x queue=%u index=%u seq=0x%04x\n"
             "rx: ready=%s closed=%u read=%u packets=%lu\n"
             "run: wifi command arm to ring the command queue doorbell\n",
             HBUS_TARG_WRPTR, s->command_doorbell_value,
             s->scheduler_cmd_queue, s->scheduler_cmd_index,
             s->scheduler_cmd_sequence, s->rx_path_ready ? "yes" : "no",
             s->rx_closed_rb, s->rx_read_ptr, s->rx_packets);
    return 0;
  }

  if (!s->context_armed || !s->scheduler_ready || !s->rx_path_ready) {
    wifi_command_mark_failure(
        "wifi: command doorbell needs armed context, scheduler, and RX path");
    s = &wifi_status_state;
    snprintf(report, report_size,
             "wifi command: refused to ring doorbell\n"
             "context=%s scheduler=%s rx=%s errors=%lu\n"
             "run: wifi context arm, wifi scheduler arm, wifi rx, then "
             "wifi command arm\n",
             s->context_armed ? "armed" : "not-armed",
             s->scheduler_ready ? "ready" : "not-ready",
             s->rx_path_ready ? "ready" : "not-ready", s->command_errors);
    return -1;
  }

  wifi_status_state.command_attempts++;
  wifi_status_state.command_doorbell_value =
      wifi_status_state.scheduler_wptr_value;
  packets_before = wifi_status_state.rx_packets;
  wifi_csr_write32(CSR_INT, CSR_INT_BIT_SCD | CSR_INT_BIT_HW_ERR |
                                CSR_INT_BIT_SW_ERR);
  wifi_csr_write32(HBUS_TARG_WRPTR, wifi_status_state.command_doorbell_value);
  wifi_status_state.cmd_write_ptr =
      (wifi_status_state.scheduler_cmd_index + 1U) & (WIFI_CMD_QUEUE_ENTRIES - 1U);
  wifi_status_state.command_sent = 1;
  wifi_status_state.command_failed = 0;
  wifi_status_state.status = "wifi: command doorbell sent; polling response";

  for (uint32_t i = 0; i < WIFI_COMMAND_POLL_LOOPS; i++) {
    uint32_t csr_int = wifi_csr_read32(CSR_INT);
    uint32_t fh_int = wifi_csr_read32(CSR_FH_INT_STATUS);
    wifi_status_state.command_poll_loops = i + 1U;
    wifi_status_state.command_last_csr_int = csr_int;
    wifi_status_state.command_last_fh_int = fh_int;
    wifi_status_state.command_last_closed_rb = wifi_rx_closed_status();
    wifi_status_state.rx_closed_rb = wifi_status_state.command_last_closed_rb;

    if (csr_int & (CSR_INT_BIT_HW_ERR | CSR_INT_BIT_SW_ERR)) {
      wifi_command_mark_failure(
          "wifi: command doorbell stopped on hardware/software error");
      break;
    }

    if (wifi_rx_parse_one() > 0 && wifi_status_state.rx_packets > packets_before) {
      response_seen = 1;
      break;
    }

    if (csr_int & CSR_INT_BIT_SCD) {
      wifi_csr_write32(CSR_INT, CSR_INT_BIT_SCD);
    }
    __asm__ volatile("pause");
  }

  s = &wifi_status_state;
  snprintf(report, report_size,
           "wifi command: %s\n"
           "doorbell: CSR[0x%03x]=0x%08x attempts=%lu sent=%s failed=%s\n"
           "cmd: id=0x%02x group=0x%02x seq=0x%04x queue=%u index=%u "
           "write=%u\n"
           "poll: loops=%u csr-int=0x%08x fh-int=0x%08x closed=%u "
           "response=%s\n"
           "rx-last: rbid=%u cmd=0x%02x group=0x%02x seq=0x%04x "
           "len=%u match=%s errors=%lu\n",
           response_seen ? "response observed"
                         : (s->command_failed ? "failed" : "doorbell sent"),
           HBUS_TARG_WRPTR, s->command_doorbell_value, s->command_attempts,
           s->command_sent ? "yes" : "no",
           s->command_failed ? "yes" : "no", s->scheduler_cmd_id,
           s->scheduler_cmd_group, s->scheduler_cmd_sequence,
           s->scheduler_cmd_queue, s->scheduler_cmd_index, s->cmd_write_ptr,
           s->command_poll_loops, s->command_last_csr_int,
           s->command_last_fh_int, s->command_last_closed_rb,
           response_seen ? "yes" : "no", s->rx_last_rbid,
           s->rx_last_cmd, s->rx_last_group, s->rx_last_sequence,
           s->rx_last_len,
           (s->rx_last_sequence == s->scheduler_cmd_sequence &&
            s->rx_last_sequence != 0)
               ? "yes"
               : "no",
           s->command_errors);
  return s->command_failed ? -1 : 0;
}

int wifi_scan(char *report, size_t report_size) {
  const wifi_status_t *s;

  if (!report || report_size == 0) {
    return -1;
  }

  wifi_init();
  wifi_find_firmware();
  s = &wifi_status_state;

  if (!s->present) {
    snprintf(report, report_size,
             "wifi scan: no wireless controller detected\n");
    return -1;
  }

  if (!s->firmware_present) {
    snprintf(report, report_size,
             "wifi scan: %s detected, but firmware is missing\n"
             "run: wifi firmware\n",
             s->chipset);
    return -1;
  }

  snprintf(report, report_size,
           "wifi scan: %s detected at %02x:%02x.%u\n"
           "firmware: %s (%lu bytes, valid=%s plan=%s dma=%s)\n"
           "wifi scan: radio scan is not available yet\n"
           "next: run wifi boot arm, wifi alive, wifi queues arm; then implement scan command\n",
           s->chipset, s->bus, s->device, (unsigned int)s->function,
           s->firmware_name, (unsigned long)s->firmware_size,
           s->firmware_valid ? "yes" : "no",
           s->firmware_load_plan_ready ? "yes" : "no",
           s->dma_ready ? "staged" : "idle");
  return -1;
}

int wifi_connect(const char *ssid, const char *password, char *report,
                 size_t report_size) {
  const wifi_status_t *s;
  UNUSED(password);

  if (!report || report_size == 0) {
    return -1;
  }

  if (!ssid || ssid[0] == '\0') {
    snprintf(report, report_size, "wifi connect: usage wifi connect <ssid>\n");
    return -1;
  }

  wifi_init();
  wifi_find_firmware();
  s = &wifi_status_state;

  if (!s->present) {
    snprintf(report, report_size,
             "wifi connect: no wireless controller detected\n");
    return -1;
  }

  snprintf(report, report_size,
           "wifi connect: saved nothing yet, driver is not ready\n"
           "target ssid: %s\n"
           "detected: %s (%04x:%04x)\n"
           "blocked by: firmware command queue, 802.11 association, and WPA layer\n",
           ssid, s->chipset, s->vendor_id, s->device_id);
  return -1;
}
