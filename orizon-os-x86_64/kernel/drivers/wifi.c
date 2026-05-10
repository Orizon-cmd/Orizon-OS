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
#include "../include/sha1.h"
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
    .tx_stage_ready = 0,
    .tx_stage_failed = 0,
    .tx_stage_errors = 0,
    .tx_stage_frames = 0,
    .tx_stage_queue = 0,
    .tx_stage_index = 0,
    .tx_stage_next_index = 0,
    .tx_stage_kind = 0,
    .tx_stage_frame_len = 0,
    .tx_stage_tfd_tbs = 0,
    .tx_stage_tb0_len = 0,
    .tx_stage_tb0_addr = 0,
    .tx_stage_bc_entry = 0,
    .tx_stage_wptr_value = 0,
    .tx_stage_checksum = 0,
    .txcmd_ready = 0,
    .txcmd_failed = 0,
    .txcmd_errors = 0,
    .txcmd_plans = 0,
    .txcmd_kind = 0,
    .txcmd_api_version = 0,
    .txcmd_id = 0,
    .txcmd_group = 0,
    .txcmd_sequence = 0,
    .txcmd_payload_len = 0,
    .txcmd_command_len = 0,
    .txcmd_frame_len = 0,
    .txcmd_header_len = 0,
    .txcmd_flags = 0,
    .txcmd_offload_assist = 0,
    .txcmd_rate_n_flags = 0,
    .txcmd_sta_id = 0,
    .txcmd_checksum = 0,
    .bind_ready = 0,
    .bind_failed = 0,
    .bind_errors = 0,
    .bind_plans = 0,
    .bind_mac_id = 0,
    .bind_color = 0,
    .bind_link_id = 0,
    .bind_sta_id = 0,
    .bind_action = 0,
    .bind_mac_cmd_id = 0,
    .bind_mac_group = 0,
    .bind_mac_version = 0,
    .bind_mac_len = 0,
    .bind_mac_checksum = 0,
    .bind_link_cmd_id = 0,
    .bind_link_group = 0,
    .bind_link_version = 0,
    .bind_link_len = 0,
    .bind_link_checksum = 0,
    .bind_sta_cmd_id = 0,
    .bind_sta_group = 0,
    .bind_sta_version = 0,
    .bind_sta_len = 0,
    .bind_sta_checksum = 0,
    .bind_assoc_aid = 0,
    .bind_security = 0,
    .bind_channel = 0,
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
    .rx_last_word0 = 0,
    .rx_last_word1 = 0,
    .rx_last_word2 = 0,
    .rx_last_word3 = 0,
    .rx_last_cd_word0 = 0,
    .rx_last_cd_word1 = 0,
    .rx_last_cd_word2 = 0,
    .rx_last_cd_word3 = 0,
    .command_ready = 0,
    .command_sent = 0,
    .command_failed = 0,
    .command_response_seen = 0,
    .command_timeout = 0,
    .command_attempts = 0,
    .command_errors = 0,
    .command_doorbell_value = 0,
    .command_wptr_readback = 0,
    .command_bc_entry = 0,
    .command_tfd_num_tbs = 0,
    .command_tfd_tb0_len = 0,
    .command_tfd_tb0_addr = 0,
    .command_before_csr_int = 0,
    .command_before_fh_int = 0,
    .command_before_closed_rb = 0,
    .command_before_read_ptr = 0,
    .command_after_csr_int = 0,
    .command_after_fh_int = 0,
    .command_after_closed_rb = 0,
    .command_after_read_ptr = 0,
    .command_last_csr_int = 0,
    .command_last_fh_int = 0,
    .command_last_closed_rb = 0,
    .command_poll_loops = 0,
    .nvm_ready = 0,
    .nvm_failed = 0,
    .nvm_response_seen = 0,
    .nvm_errors = 0,
    .nvm_generation = 0,
    .nvm_op = 0,
    .nvm_target = 0,
    .nvm_section = 0,
    .nvm_offset = 0,
    .nvm_length = 0,
    .nvm_resp_offset = 0,
    .nvm_resp_length = 0,
    .nvm_resp_type = 0,
    .nvm_resp_status = 0,
    .nvm_resp_word0 = 0,
    .nvm_resp_word1 = 0,
    .nvm_resp_word2 = 0,
    .nvm_resp_word3 = 0,
    .nvm_info_ready = 0,
    .nvm_info_failed = 0,
    .nvm_info_response_seen = 0,
    .nvm_info_errors = 0,
    .nvm_info_generation = 0,
    .nvm_info_flags = 0,
    .nvm_info_version = 0,
    .nvm_info_board_type = 0,
    .nvm_info_hw_addrs = 0,
    .nvm_info_mac_sku_flags = 0,
    .nvm_info_tx_chains = 0,
    .nvm_info_rx_chains = 0,
    .nvm_info_lar_enabled = 0,
    .nvm_info_n_channels = 0,
    .scan_ready = 0,
    .scan_failed = 0,
    .scan_response_seen = 0,
    .scan_start_seen = 0,
    .scan_iter_seen = 0,
    .scan_complete_seen = 0,
    .scan_inflight = 0,
    .scan_errors = 0,
    .scan_generation = 0,
    .scan_notifications = 0,
    .scan_uid = 0,
    .scan_cmd_len = 0,
    .scan_version = 0,
    .scan_flags = 0,
    .scan_general_flags = 0,
    .scan_channel_count = 0,
    .scan_channel_first = 0,
    .scan_channel_last = 0,
    .scan_dwell_active = 0,
    .scan_dwell_passive = 0,
    .scan_start_uid = 0,
    .scan_iter_uid = 0,
    .scan_iter_channels = 0,
    .scan_iter_status = 0,
    .scan_iter_bt_status = 0,
    .scan_iter_last_channel = 0,
    .scan_iter_tsf_low = 0,
    .scan_iter_tsf_high = 0,
    .scan_complete_uid = 0,
    .scan_complete_last_schedule = 0,
    .scan_complete_last_iter = 0,
    .scan_complete_status = 0,
    .scan_complete_ebs_status = 0,
    .scan_complete_elapsed = 0,
    .scan_result_truncated = 0,
    .scan_result_count = 0,
    .scan_result_reported = 0,
    .scan_result_entries = 0,
    .scan_result_bytes = 0,
    .scan_mpdu_packets = 0,
    .scan_mgmt_frames = 0,
    .scan_beacon_frames = 0,
    .scan_probe_resp_frames = 0,
    .scan_ssid_updates = 0,
    .scan_mpdu_parse_misses = 0,
    .scan_last_mpdu_payload_len = 0,
    .scan_last_mpdu_len = 0,
    .scan_last_frame_offset = 0,
    .scan_last_frame_len = 0,
    .scan_last_frame_control = 0,
    .scan_last_frame_subtype = 0,
    .scan_last_frame_channel = 0,
    .scan_last_debug_len = 0,
    .scan_candidate_count = 0,
    .scan_ap_count = 0,
    .scan_ap_overflow = 0,
    .connect_ready = 0,
    .connect_failed = 0,
    .connect_wpa = 0,
    .connect_open = 0,
    .connect_attempts = 0,
    .connect_ap_index = 0xffffffffU,
    .connect_channel = 0,
    .connect_ssid_len = 0,
    .connect_auth_frame_len = 0,
    .connect_assoc_frame_len = 0,
    .connect_auth_fc = 0,
    .connect_assoc_fc = 0,
    .connect_frame_checksum = 0,
    .connect_pmk_ready = 0,
    .connect_password_len = 0,
    .connect_pmk_iterations = 0,
    .connect_pmk_checksum = 0,
    .connect_auth_response_seen = 0,
    .connect_assoc_response_seen = 0,
    .connect_response_failed = 0,
    .connect_response_packets = 0,
    .connect_last_rx_subtype = 0,
    .connect_auth_alg = 0,
    .connect_auth_seq = 0,
    .connect_auth_status = 0,
    .connect_assoc_status = 0,
    .connect_assoc_aid = 0,
    .wpa_eapol_packets = 0,
    .wpa_key_frames = 0,
    .wpa_anonce_seen = 0,
    .wpa_key_info = 0,
    .wpa_key_len = 0,
    .wpa_key_data_len = 0,
    .wpa_replay_counter_hi = 0,
    .wpa_replay_counter_lo = 0,
    .wpa_anonce_checksum = 0,
    .wpa_snonce_checksum = 0,
    .wpa_ptk_checksum = 0,
    .wpa_kck_checksum = 0,
    .wpa_kek_checksum = 0,
    .wpa_tk_checksum = 0,
    .wpa_m2_mic_checksum = 0,
    .wpa_m2_checksum = 0,
    .wpa_m2_frame_len = 0,
    .wpa_m2_data_frame_len = 0,
    .wpa_m2_key_info = 0,
    .wpa_m2_key_data_len = 0,
    .wpa_key_desc_type = 0,
    .wpa_eapol_version = 0,
    .wpa_m1_seen = 0,
    .wpa_ptk_ready = 0,
    .wpa_m2_ready = 0,
    .wpa_m2_data_ready = 0,
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
#define WIFI_CMD_BUFFER_BYTES 2048U
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
#define WIFI_DQA_TX_QUEUE 1U
#define WIFI_CMD_TX 0x1cU
#define WIFI_CMD_SCD_QUEUE_CFG 0x1dU
#define WIFI_CMD_NVM_ACCESS 0x88U
#define WIFI_CMD_NVM_GET_INFO 0x02U
#define WIFI_CMD_SCAN_REQ_UMAC 0x0dU
#define WIFI_CMD_SCAN_ABORT_UMAC 0x0eU
#define WIFI_CMD_SCAN_COMPLETE_UMAC 0x0fU
#define WIFI_CMD_SCAN_START_NOTIFICATION_UMAC 0xb2U
#define WIFI_CMD_SCAN_ITERATION_COMPLETE_UMAC 0xb5U
#define WIFI_CMD_REPLY_RX_PHY 0xc0U
#define WIFI_CMD_REPLY_RX_MPDU 0xc1U
#define WIFI_CMD_GROUP_LONG 0x01U
#define WIFI_CMD_GROUP_LEGACY 0x00U
#define WIFI_CMD_GROUP_MAC_CONF 0x03U
#define WIFI_CMD_GROUP_SCAN 0x06U
#define WIFI_CMD_GROUP_REGULATORY_NVM 0x0cU
#define WIFI_CMD_MAC_CONFIG 0x08U
#define WIFI_CMD_LINK_CONFIG 0x09U
#define WIFI_CMD_STA_CONFIG 0x0aU
#define WIFI_CMD_VERSION_TX_API_V10 10U
#define WIFI_CMD_VERSION_MAC_CONFIG 1U
#define WIFI_CMD_VERSION_LINK_CONFIG 1U
#define WIFI_CMD_VERSION_STA_CONFIG 1U
#define WIFI_CMD_VERSION_TX_QUEUE_CFG 2U
#define WIFI_CMD_VERSION_NVM_ACCESS 0U
#define WIFI_CMD_VERSION_NVM_GET_INFO 1U
#define WIFI_CMD_VERSION_SCAN_REQ_UMAC 1U
#define WIFI_TX_QUEUE_CFG_ENABLE_QUEUE 0x0001U
#define WIFI_CMD_QUEUE_CB_SIZE_VALUE 2U
#define WIFI_NVM_READ 0U
#define WIFI_NVM_TARGET_CACHE 0U
#define WIFI_NVM_SECTION_SW 1U
#define WIFI_NVM_DEFAULT_READ_BYTES 64U
#define WIFI_NVM_MAC_SKU_2GHZ (1U << 0)
#define WIFI_NVM_MAC_SKU_5GHZ (1U << 1)
#define WIFI_NVM_MAC_SKU_11N (1U << 2)
#define WIFI_NVM_MAC_SKU_11AC (1U << 3)
#define WIFI_NVM_MAC_SKU_11AX (1U << 4)
#define WIFI_NVM_MAC_SKU_MIMO_DISABLED (1U << 5)
#define WIFI_SCAN_MAX_CHANNELS 8U
#define WIFI_SCAN_PROBE_BUF_BYTES 512U
#define WIFI_SCAN_DIRECT_SSID_SLOTS 20U
#define WIFI_SCAN_UMAC_FLAG_START_NOTIF (1U << 1)
#define WIFI_SCAN_GEN_PASS_ALL (1U << 2)
#define WIFI_SCAN_GEN_PASSIVE (1U << 3)
#define WIFI_SCAN_GEN_ITER_COMPLETE (1U << 5)
#define WIFI_SCAN_CHANNEL_FLAG_ENABLE_ORDER (1U << 5)
#define WIFI_SCAN_PRIORITY_EXT_6 6U
#define WIFI_SCAN_DWELL_ACTIVE 10U
#define WIFI_SCAN_DWELL_PASSIVE 110U
#define WIFI_SCAN_DWELL_FRAGMENTED 44U
#define WIFI_SCAN_DWELL_EXTENDED 90U
#define WIFI_SCAN_POLL_LOOPS 800000U
#define WIFI_SCAN_DRAIN_LOOPS 100000U
#define WIFI_RX_MPDU_DESC_SIZE_V1 48U
#define WIFI_RX_MPDU_DESC_SIZE_V3 64U
#define WIFI_80211_MGMT_HEADER_BYTES 24U
#define WIFI_80211_BEACON_FIXED_BYTES 12U
#define WIFI_80211_FC_TYPE_MASK 0x000cU
#define WIFI_80211_FC_SUBTYPE_MASK 0x00f0U
#define WIFI_80211_TYPE_MGMT 0U
#define WIFI_80211_TYPE_DATA 2U
#define WIFI_80211_FC_TODS 0x0100U
#define WIFI_80211_FC_FROMDS 0x0200U
#define WIFI_80211_SUBTYPE_QOS_DATA 8U
#define WIFI_80211_SUBTYPE_ASSOC_REQ 0U
#define WIFI_80211_SUBTYPE_ASSOC_RESP 1U
#define WIFI_80211_SUBTYPE_AUTH 11U
#define WIFI_80211_SUBTYPE_PROBE_RESP 5U
#define WIFI_80211_SUBTYPE_BEACON 8U
#define WIFI_80211_CAP_ESS 0x0001U
#define WIFI_80211_CAP_PRIVACY 0x0010U
#define WIFI_80211_LISTEN_INTERVAL 10U
#define WIFI_WPA2_PMK_BYTES 32U
#define WIFI_WPA2_PBKDF2_ITERATIONS 4096U
#define WIFI_WPA2_MIN_PASSPHRASE 8U
#define WIFI_WPA2_MAX_PASSPHRASE 63U
#define WIFI_WPA2_HEX_PSK_CHARS 64U
#define WIFI_WPA_NONCE_BYTES 32U
#define WIFI_WPA_PTK_BYTES 64U
#define WIFI_WPA_KCK_BYTES 16U
#define WIFI_WPA_KEK_BYTES 16U
#define WIFI_WPA_TK_BYTES 16U
#define WIFI_WPA_EAPOL_M2_BYTES 256U
#define WIFI_TX_STAGE_KIND_AUTH 1U
#define WIFI_TX_STAGE_KIND_ASSOC 2U
#define WIFI_TX_STAGE_KIND_EAPOL_M2 3U
#define WIFI_TX_CMD_FLAG_ENCRYPT_DIS (1U << 1)
#define WIFI_TX_CMD_FLAG_HIGH_PRI (1U << 2)
#define WIFI_TX_CMD_OFFLD_MH_SIZE_SHIFT 8U
#define WIFI_TX_CMD_OFFLD_PAD (1U << 13)
#define WIFI_TX_CMD_STA_ID_UNBOUND 0xffU
#define WIFI_BIND_CTXT_ID_POS 0U
#define WIFI_BIND_CTXT_COLOR_POS 8U
#define WIFI_BIND_MAC_ID_CLIENT 0U
#define WIFI_BIND_COLOR_CLIENT 1U
#define WIFI_BIND_LINK_ID_PRIMARY 0U
#define WIFI_BIND_PHY_ID_INVALID 0xffffffffU
#define WIFI_BIND_STA_ID_AP 0U
#define WIFI_BIND_ACTION_ADD 1U
#define WIFI_BIND_FW_MAC_TYPE_BSS_STA 5U
#define WIFI_BIND_STATION_TYPE_PEER 0U
#define WIFI_BIND_CCK_RATES 0x0000000fU
#define WIFI_BIND_OFDM_RATES 0x00000ff0U
#define WIFI_BIND_MAC_FILTER_MGMT (1U << 1)
#define WIFI_BIND_MAC_FILTER_GROUP (1U << 2)
#define WIFI_BIND_MAC_FILTER_DECRYPT_OFF (1U << 3)
#define WIFI_EAPOL_ETHERTYPE 0x888eU
#define WIFI_EAPOL_TYPE_KEY 3U
#define WIFI_WPA_KEY_INFO_PAIRWISE 0x0008U
#define WIFI_WPA_KEY_INFO_ACK 0x0080U
#define WIFI_WPA_KEY_INFO_MIC 0x0100U
#define WIFI_WPA_KEY_INFO_TYPE_MASK 0x0007U
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
  uint32_t pn_low;
  uint16_t pn_high;
  uint16_t aux_info;
} wifi_tx_dram_sec_info_t;

typedef struct __attribute__((packed)) {
  uint16_t len;
  uint16_t flags;
  uint32_t offload_assist;
  wifi_tx_dram_sec_info_t dram_info;
  uint32_t rate_n_flags;
  uint8_t reserved[8];
} wifi_tx_cmd_v10_t;

typedef struct __attribute__((packed)) {
  uint16_t cw_min;
  uint16_t cw_max;
  uint8_t aifsn;
  uint8_t fifos_mask;
  uint16_t edca_txop;
} wifi_bind_ac_qos_diag_t;

typedef struct __attribute__((packed)) {
  uint32_t is_assoc;
  uint32_t dtim_time;
  uint64_t dtim_tsf;
  uint32_t bi;
  uint32_t reserved1;
  uint32_t dtim_interval;
  uint32_t data_policy;
  uint32_t listen_interval;
  uint32_t assoc_id;
  uint32_t assoc_beacon_arrive_time;
} wifi_bind_mac_sta_diag_t;

typedef struct __attribute__((packed)) {
  uint32_t id_and_color;
  uint32_t action;
  uint32_t mac_type;
  uint32_t tsf_id;
  uint8_t node_addr[6];
  uint16_t reserved_for_node_addr;
  uint8_t bssid_addr[6];
  uint16_t reserved_for_bssid_addr;
  uint32_t cck_rates;
  uint32_t ofdm_rates;
  uint32_t protection_flags;
  uint32_t cck_short_preamble;
  uint32_t short_slot;
  uint32_t filter_flags;
  uint32_t qos_flags;
  wifi_bind_ac_qos_diag_t ac[5];
  wifi_bind_mac_sta_diag_t sta;
} wifi_bind_mac_config_diag_t;

typedef struct __attribute__((packed)) {
  uint32_t action;
  uint32_t link_id;
  uint32_t mac_id;
  uint32_t phy_id;
  uint8_t local_link_addr[6];
  uint16_t reserved_for_local_link_addr;
  uint32_t modify_mask;
  uint32_t active;
  uint32_t listen_lmac;
  uint32_t cck_rates;
  uint32_t ofdm_rates;
  uint32_t cck_short_preamble;
  uint32_t short_slot;
  uint32_t protection_flags;
  uint32_t qos_flags;
  wifi_bind_ac_qos_diag_t ac[5];
  uint8_t htc_trig_based_pkt_ext;
  uint8_t rand_alloc_ecwmin;
  uint8_t rand_alloc_ecwmax;
  uint8_t ndp_fdbk_buff_th_exp;
  uint8_t trig_based_txf[24];
  uint32_t bi;
  uint32_t dtim_interval;
  uint16_t puncture_mask;
  uint16_t frame_time_rts_th;
  uint32_t flags;
  uint32_t flags_mask;
  uint8_t ref_bssid_addr[6];
  uint16_t reserved_for_ref_bssid_addr;
  uint8_t bssid_index;
  uint8_t bss_color;
  uint8_t spec_link_id;
  uint8_t ul_mu_data_disable;
  uint8_t ibss_bssid_addr[6];
  uint16_t reserved_for_ibss_bssid_addr;
  uint8_t reserved_tail[32];
} wifi_bind_link_config_diag_t;

typedef struct __attribute__((packed)) {
  uint32_t sta_id;
  uint32_t link_id;
  uint8_t peer_mld_address[6];
  uint16_t reserved_for_peer_mld_address;
  uint8_t peer_link_address[6];
  uint16_t reserved_for_peer_link_address;
  uint32_t station_type;
  uint32_t assoc_id;
  uint32_t beamform_flags;
  uint32_t mfp;
  uint32_t mimo;
  uint32_t mimo_protection;
  uint32_t ack_enabled;
  uint32_t trig_rnd_alloc;
  uint32_t tx_ampdu_spacing;
  uint32_t tx_ampdu_max_size;
  uint32_t sp_length;
  uint32_t uapsd_acs;
  uint8_t pkt_ext[12];
  uint32_t htc_flags;
  uint8_t use_ldpc_x2_cw;
  uint8_t use_icf;
  uint8_t dps_pad_time;
  uint8_t dps_trans_delay;
  uint8_t mic_prep_pad_delay;
  uint8_t mic_compute_pad_delay;
  uint8_t reserved[2];
} wifi_bind_sta_config_diag_t;

typedef struct __attribute__((packed)) {
  uint8_t op_code;
  uint8_t target;
  uint16_t type;
  uint16_t offset;
  uint16_t length;
} wifi_nvm_access_cmd_t;

typedef struct __attribute__((packed)) {
  uint16_t offset;
  uint16_t length;
  uint16_t type;
  uint16_t status;
} wifi_nvm_access_resp_t;

typedef struct __attribute__((packed)) {
  uint32_t reserved;
} wifi_nvm_get_info_cmd_t;

typedef struct __attribute__((packed)) {
  uint8_t flags;
  uint8_t count;
  uint16_t reserved;
} wifi_scan_umac_chan_param_t;

typedef struct __attribute__((packed)) {
  uint32_t flags;
  uint8_t channel_num;
  uint8_t iter_count;
  uint16_t iter_interval;
} wifi_scan_channel_cfg_umac_t;

typedef struct __attribute__((packed)) {
  uint16_t offset;
  uint16_t len;
} wifi_scan_probe_segment_t;

typedef struct __attribute__((packed)) {
  wifi_scan_probe_segment_t mac_header;
  wifi_scan_probe_segment_t band_data[2];
  wifi_scan_probe_segment_t common_data;
  uint8_t buf[WIFI_SCAN_PROBE_BUF_BYTES];
} wifi_scan_probe_req_v1_t;

typedef struct __attribute__((packed)) {
  uint8_t id;
  uint8_t len;
  uint8_t ssid[32];
} wifi_scan_ssid_ie_t;

typedef struct __attribute__((packed)) {
  uint16_t interval;
  uint8_t iter_count;
  uint8_t reserved;
} wifi_scan_umac_schedule_t;

typedef struct __attribute__((packed)) {
  uint32_t flags;
  uint32_t uid;
  uint32_t ooc_priority;
  uint16_t general_flags;
  uint8_t reserved;
  uint8_t scan_start_mac_id;
  uint8_t extended_dwell;
  uint8_t active_dwell;
  uint8_t passive_dwell;
  uint8_t fragmented_dwell;
  uint32_t max_out_time;
  uint32_t suspend_time;
  uint32_t scan_priority;
  wifi_scan_umac_chan_param_t channel;
} wifi_scan_req_umac_v1_t;

typedef struct __attribute__((packed)) {
  wifi_scan_umac_schedule_t schedule[2];
  uint16_t delay;
  uint16_t reserved;
  wifi_scan_probe_req_v1_t preq;
  wifi_scan_ssid_ie_t direct_scan[WIFI_SCAN_DIRECT_SSID_SLOTS];
} wifi_scan_req_umac_tail_v1_t;

typedef struct __attribute__((packed)) {
  uint32_t uid;
  uint32_t reserved;
} wifi_umac_scan_start_t;

typedef struct __attribute__((packed)) {
  uint32_t uid;
  uint8_t last_schedule;
  uint8_t last_iter;
  uint8_t status;
  uint8_t ebs_status;
  uint32_t time_from_last_iter;
  uint32_t reserved;
} wifi_umac_scan_complete_t;

typedef struct __attribute__((packed)) {
  uint32_t uid;
  uint8_t scanned_channels;
  uint8_t status;
  uint8_t bt_status;
  uint8_t last_channel;
  uint64_t start_tsf;
} wifi_umac_scan_iter_complete_t;

typedef struct __attribute__((packed)) {
  uint8_t channel;
  uint8_t band;
  uint8_t probe_status;
  uint8_t num_probe_not_sent;
  uint32_t duration;
} wifi_scan_result_notif_t;

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
static uint8_t wifi_txcmd_plan_buffer[WIFI_CMD_BUFFER_BYTES]
    __attribute__((aligned(16)));
static uint8_t wifi_bind_mac_buffer[WIFI_CMD_BUFFER_BYTES]
    __attribute__((aligned(16)));
static uint8_t wifi_bind_link_buffer[WIFI_CMD_BUFFER_BYTES]
    __attribute__((aligned(16)));
static uint8_t wifi_bind_sta_buffer[WIFI_CMD_BUFFER_BYTES]
    __attribute__((aligned(16)));
static uint8_t wifi_scan_payload[WIFI_CMD_BUFFER_BYTES]
    __attribute__((aligned(16)));
static uint8_t wifi_connect_auth_frame[64] __attribute__((aligned(16)));
static uint8_t wifi_connect_assoc_frame[WIFI_CONNECT_FRAME_BYTES]
    __attribute__((aligned(16)));
static uint8_t wifi_connect_pmk[WIFI_WPA2_PMK_BYTES]
    __attribute__((aligned(16)));
static uint8_t wifi_wpa_anonce[WIFI_WPA_NONCE_BYTES]
    __attribute__((aligned(16)));
static uint8_t wifi_wpa_snonce[WIFI_WPA_NONCE_BYTES]
    __attribute__((aligned(16)));
static uint8_t wifi_wpa_ptk[WIFI_WPA_PTK_BYTES]
    __attribute__((aligned(16)));
static uint8_t wifi_wpa_eapol_m2[WIFI_WPA_EAPOL_M2_BYTES]
    __attribute__((aligned(16)));
static uint8_t wifi_wpa_m2_data_frame[WIFI_CONNECT_FRAME_BYTES]
    __attribute__((aligned(16)));
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

static uint16_t wifi_read_le16(const uint8_t *p) {
  return (uint16_t)p[0] | (uint16_t)((uint16_t)p[1] << 8);
}

static void wifi_write_le16(uint8_t *p, uint16_t value) {
  p[0] = (uint8_t)(value & 0xffU);
  p[1] = (uint8_t)((value >> 8) & 0xffU);
}

static size_t wifi_align4(size_t value) {
  return (value + 3U) & ~(size_t)3U;
}

static int wifi_is_printable_ascii(uint8_t c) {
  return c >= 32 && c <= 126;
}

static void wifi_scan_clear_access_points(void) {
  wifi_status_state.scan_mpdu_packets = 0;
  wifi_status_state.scan_mgmt_frames = 0;
  wifi_status_state.scan_beacon_frames = 0;
  wifi_status_state.scan_probe_resp_frames = 0;
  wifi_status_state.scan_ssid_updates = 0;
  wifi_status_state.scan_mpdu_parse_misses = 0;
  wifi_status_state.scan_last_mpdu_payload_len = 0;
  wifi_status_state.scan_last_mpdu_len = 0;
  wifi_status_state.scan_last_frame_offset = 0;
  wifi_status_state.scan_last_frame_len = 0;
  wifi_status_state.scan_last_frame_control = 0;
  wifi_status_state.scan_last_frame_subtype = 0;
  wifi_status_state.scan_last_frame_channel = 0;
  wifi_status_state.scan_last_debug_len = 0;
  wifi_status_state.scan_candidate_count = 0;
  memset(wifi_status_state.scan_last_debug_bytes, 0,
         sizeof(wifi_status_state.scan_last_debug_bytes));
  memset(wifi_status_state.scan_candidate_offset, 0,
         sizeof(wifi_status_state.scan_candidate_offset));
  memset(wifi_status_state.scan_candidate_len, 0,
         sizeof(wifi_status_state.scan_candidate_len));
  memset(wifi_status_state.scan_candidate_fc, 0,
         sizeof(wifi_status_state.scan_candidate_fc));
  memset(wifi_status_state.scan_candidate_type, 0,
         sizeof(wifi_status_state.scan_candidate_type));
  memset(wifi_status_state.scan_candidate_subtype, 0,
         sizeof(wifi_status_state.scan_candidate_subtype));
  wifi_status_state.scan_ap_count = 0;
  wifi_status_state.scan_ap_overflow = 0;
  memset(wifi_status_state.scan_ap_channel, 0,
         sizeof(wifi_status_state.scan_ap_channel));
  memset(wifi_status_state.scan_ap_frame_subtype, 0,
         sizeof(wifi_status_state.scan_ap_frame_subtype));
  memset(wifi_status_state.scan_ap_seen_count, 0,
         sizeof(wifi_status_state.scan_ap_seen_count));
  memset(wifi_status_state.scan_ap_capability, 0,
         sizeof(wifi_status_state.scan_ap_capability));
  memset(wifi_status_state.scan_ap_security, 0,
         sizeof(wifi_status_state.scan_ap_security));
  memset(wifi_status_state.scan_ap_ssid_len, 0,
         sizeof(wifi_status_state.scan_ap_ssid_len));
  memset(wifi_status_state.scan_ap_bssid, 0,
         sizeof(wifi_status_state.scan_ap_bssid));
  memset(wifi_status_state.scan_ap_ssid, 0,
         sizeof(wifi_status_state.scan_ap_ssid));
}

static void wifi_bind_clear_plan(void) {
  wifi_status_state.bind_ready = 0;
  wifi_status_state.bind_failed = 0;
  wifi_status_state.bind_errors = 0;
  wifi_status_state.bind_plans = 0;
  wifi_status_state.bind_mac_id = 0;
  wifi_status_state.bind_color = 0;
  wifi_status_state.bind_link_id = 0;
  wifi_status_state.bind_sta_id = 0;
  wifi_status_state.bind_action = 0;
  wifi_status_state.bind_mac_cmd_id = 0;
  wifi_status_state.bind_mac_group = 0;
  wifi_status_state.bind_mac_version = 0;
  wifi_status_state.bind_mac_len = 0;
  wifi_status_state.bind_mac_checksum = 0;
  wifi_status_state.bind_link_cmd_id = 0;
  wifi_status_state.bind_link_group = 0;
  wifi_status_state.bind_link_version = 0;
  wifi_status_state.bind_link_len = 0;
  wifi_status_state.bind_link_checksum = 0;
  wifi_status_state.bind_sta_cmd_id = 0;
  wifi_status_state.bind_sta_group = 0;
  wifi_status_state.bind_sta_version = 0;
  wifi_status_state.bind_sta_len = 0;
  wifi_status_state.bind_sta_checksum = 0;
  wifi_status_state.bind_assoc_aid = 0;
  wifi_status_state.bind_security = 0;
  wifi_status_state.bind_channel = 0;
  memset(wifi_bind_mac_buffer, 0, sizeof(wifi_bind_mac_buffer));
  memset(wifi_bind_link_buffer, 0, sizeof(wifi_bind_link_buffer));
  memset(wifi_bind_sta_buffer, 0, sizeof(wifi_bind_sta_buffer));
}

static void wifi_connect_clear_plan(void) {
  wifi_status_state.connect_ready = 0;
  wifi_status_state.connect_failed = 0;
  wifi_status_state.connect_wpa = 0;
  wifi_status_state.connect_open = 0;
  wifi_status_state.connect_ap_index = 0xffffffffU;
  wifi_status_state.connect_channel = 0;
  wifi_status_state.connect_ssid_len = 0;
  wifi_status_state.connect_auth_frame_len = 0;
  wifi_status_state.connect_assoc_frame_len = 0;
  wifi_status_state.connect_auth_fc = 0;
  wifi_status_state.connect_assoc_fc = 0;
  wifi_status_state.connect_frame_checksum = 0;
  wifi_status_state.connect_pmk_ready = 0;
  wifi_status_state.connect_password_len = 0;
  wifi_status_state.connect_pmk_iterations = 0;
  wifi_status_state.connect_pmk_checksum = 0;
  wifi_status_state.connect_auth_response_seen = 0;
  wifi_status_state.connect_assoc_response_seen = 0;
  wifi_status_state.connect_response_failed = 0;
  wifi_status_state.connect_response_packets = 0;
  wifi_status_state.connect_last_rx_subtype = 0;
  wifi_status_state.connect_auth_alg = 0;
  wifi_status_state.connect_auth_seq = 0;
  wifi_status_state.connect_auth_status = 0;
  wifi_status_state.connect_assoc_status = 0;
  wifi_status_state.connect_assoc_aid = 0;
  wifi_status_state.wpa_eapol_packets = 0;
  wifi_status_state.wpa_key_frames = 0;
  wifi_status_state.wpa_anonce_seen = 0;
  wifi_status_state.wpa_key_info = 0;
  wifi_status_state.wpa_key_len = 0;
  wifi_status_state.wpa_key_data_len = 0;
  wifi_status_state.wpa_replay_counter_hi = 0;
  wifi_status_state.wpa_replay_counter_lo = 0;
  wifi_status_state.wpa_anonce_checksum = 0;
  wifi_status_state.wpa_snonce_checksum = 0;
  wifi_status_state.wpa_ptk_checksum = 0;
  wifi_status_state.wpa_kck_checksum = 0;
  wifi_status_state.wpa_kek_checksum = 0;
  wifi_status_state.wpa_tk_checksum = 0;
  wifi_status_state.wpa_m2_mic_checksum = 0;
  wifi_status_state.wpa_m2_checksum = 0;
  wifi_status_state.wpa_m2_frame_len = 0;
  wifi_status_state.wpa_m2_data_frame_len = 0;
  wifi_status_state.wpa_m2_key_info = 0;
  wifi_status_state.wpa_m2_key_data_len = 0;
  wifi_status_state.wpa_key_desc_type = 0;
  wifi_status_state.wpa_eapol_version = 0;
  wifi_status_state.wpa_m1_seen = 0;
  wifi_status_state.wpa_ptk_ready = 0;
  wifi_status_state.wpa_m2_ready = 0;
  wifi_status_state.wpa_m2_data_ready = 0;
  wifi_status_state.tx_stage_ready = 0;
  wifi_status_state.tx_stage_failed = 0;
  wifi_status_state.tx_stage_errors = 0;
  wifi_status_state.tx_stage_frames = 0;
  wifi_status_state.tx_stage_queue = 0;
  wifi_status_state.tx_stage_index = 0;
  wifi_status_state.tx_stage_next_index = 0;
  wifi_status_state.tx_stage_kind = 0;
  wifi_status_state.tx_stage_frame_len = 0;
  wifi_status_state.tx_stage_tfd_tbs = 0;
  wifi_status_state.tx_stage_tb0_len = 0;
  wifi_status_state.tx_stage_tb0_addr = 0;
  wifi_status_state.tx_stage_bc_entry = 0;
  wifi_status_state.tx_stage_wptr_value = 0;
  wifi_status_state.tx_stage_checksum = 0;
  wifi_status_state.txcmd_ready = 0;
  wifi_status_state.txcmd_failed = 0;
  wifi_status_state.txcmd_errors = 0;
  wifi_status_state.txcmd_plans = 0;
  wifi_status_state.txcmd_kind = 0;
  wifi_status_state.txcmd_api_version = 0;
  wifi_status_state.txcmd_id = 0;
  wifi_status_state.txcmd_group = 0;
  wifi_status_state.txcmd_sequence = 0;
  wifi_status_state.txcmd_payload_len = 0;
  wifi_status_state.txcmd_command_len = 0;
  wifi_status_state.txcmd_frame_len = 0;
  wifi_status_state.txcmd_header_len = 0;
  wifi_status_state.txcmd_flags = 0;
  wifi_status_state.txcmd_offload_assist = 0;
  wifi_status_state.txcmd_rate_n_flags = 0;
  wifi_status_state.txcmd_sta_id = 0;
  wifi_status_state.txcmd_checksum = 0;
  memset(wifi_status_state.connect_bssid, 0,
         sizeof(wifi_status_state.connect_bssid));
  memset(wifi_status_state.connect_local_mac, 0,
         sizeof(wifi_status_state.connect_local_mac));
  memset(wifi_status_state.connect_ssid, 0,
         sizeof(wifi_status_state.connect_ssid));
  memset(wifi_connect_auth_frame, 0, sizeof(wifi_connect_auth_frame));
  memset(wifi_connect_assoc_frame, 0, sizeof(wifi_connect_assoc_frame));
  memset(wifi_connect_pmk, 0, sizeof(wifi_connect_pmk));
  memset(wifi_wpa_anonce, 0, sizeof(wifi_wpa_anonce));
  memset(wifi_wpa_snonce, 0, sizeof(wifi_wpa_snonce));
  memset(wifi_wpa_ptk, 0, sizeof(wifi_wpa_ptk));
  memset(wifi_wpa_eapol_m2, 0, sizeof(wifi_wpa_eapol_m2));
  memset(wifi_wpa_m2_data_frame, 0, sizeof(wifi_wpa_m2_data_frame));
  memset(wifi_txcmd_plan_buffer, 0, sizeof(wifi_txcmd_plan_buffer));
  wifi_bind_clear_plan();
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
  wifi_status_state.tx_stage_ready = 0;
  wifi_status_state.tx_stage_failed = 0;
  wifi_status_state.tx_stage_errors = 0;
  wifi_status_state.tx_stage_frames = 0;
  wifi_status_state.tx_stage_queue = 0;
  wifi_status_state.tx_stage_index = 0;
  wifi_status_state.tx_stage_next_index = 0;
  wifi_status_state.tx_stage_kind = 0;
  wifi_status_state.tx_stage_frame_len = 0;
  wifi_status_state.tx_stage_tfd_tbs = 0;
  wifi_status_state.tx_stage_tb0_len = 0;
  wifi_status_state.tx_stage_tb0_addr = 0;
  wifi_status_state.tx_stage_bc_entry = 0;
  wifi_status_state.tx_stage_wptr_value = 0;
  wifi_status_state.tx_stage_checksum = 0;
  wifi_status_state.txcmd_ready = 0;
  wifi_status_state.txcmd_failed = 0;
  wifi_status_state.txcmd_errors = 0;
  wifi_status_state.txcmd_plans = 0;
  wifi_status_state.txcmd_kind = 0;
  wifi_status_state.txcmd_api_version = 0;
  wifi_status_state.txcmd_id = 0;
  wifi_status_state.txcmd_group = 0;
  wifi_status_state.txcmd_sequence = 0;
  wifi_status_state.txcmd_payload_len = 0;
  wifi_status_state.txcmd_command_len = 0;
  wifi_status_state.txcmd_frame_len = 0;
  wifi_status_state.txcmd_header_len = 0;
  wifi_status_state.txcmd_flags = 0;
  wifi_status_state.txcmd_offload_assist = 0;
  wifi_status_state.txcmd_rate_n_flags = 0;
  wifi_status_state.txcmd_sta_id = 0;
  wifi_status_state.txcmd_checksum = 0;
  wifi_bind_clear_plan();
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
  wifi_status_state.rx_last_word0 = 0;
  wifi_status_state.rx_last_word1 = 0;
  wifi_status_state.rx_last_word2 = 0;
  wifi_status_state.rx_last_word3 = 0;
  wifi_status_state.rx_last_cd_word0 = 0;
  wifi_status_state.rx_last_cd_word1 = 0;
  wifi_status_state.rx_last_cd_word2 = 0;
  wifi_status_state.rx_last_cd_word3 = 0;
  wifi_status_state.command_ready = 0;
  wifi_status_state.command_sent = 0;
  wifi_status_state.command_failed = 0;
  wifi_status_state.command_response_seen = 0;
  wifi_status_state.command_timeout = 0;
  wifi_status_state.command_attempts = 0;
  wifi_status_state.command_errors = 0;
  wifi_status_state.command_doorbell_value = 0;
  wifi_status_state.command_wptr_readback = 0;
  wifi_status_state.command_bc_entry = 0;
  wifi_status_state.command_tfd_num_tbs = 0;
  wifi_status_state.command_tfd_tb0_len = 0;
  wifi_status_state.command_tfd_tb0_addr = 0;
  wifi_status_state.command_before_csr_int = 0;
  wifi_status_state.command_before_fh_int = 0;
  wifi_status_state.command_before_closed_rb = 0;
  wifi_status_state.command_before_read_ptr = 0;
  wifi_status_state.command_after_csr_int = 0;
  wifi_status_state.command_after_fh_int = 0;
  wifi_status_state.command_after_closed_rb = 0;
  wifi_status_state.command_after_read_ptr = 0;
  wifi_status_state.command_last_csr_int = 0;
  wifi_status_state.command_last_fh_int = 0;
  wifi_status_state.command_last_closed_rb = 0;
  wifi_status_state.command_poll_loops = 0;
  wifi_status_state.nvm_ready = 0;
  wifi_status_state.nvm_failed = 0;
  wifi_status_state.nvm_response_seen = 0;
  wifi_status_state.nvm_errors = 0;
  wifi_status_state.nvm_generation = 0;
  wifi_status_state.nvm_op = 0;
  wifi_status_state.nvm_target = 0;
  wifi_status_state.nvm_section = 0;
  wifi_status_state.nvm_offset = 0;
  wifi_status_state.nvm_length = 0;
  wifi_status_state.nvm_resp_offset = 0;
  wifi_status_state.nvm_resp_length = 0;
  wifi_status_state.nvm_resp_type = 0;
  wifi_status_state.nvm_resp_status = 0;
  wifi_status_state.nvm_resp_word0 = 0;
  wifi_status_state.nvm_resp_word1 = 0;
  wifi_status_state.nvm_resp_word2 = 0;
  wifi_status_state.nvm_resp_word3 = 0;
  wifi_status_state.nvm_info_ready = 0;
  wifi_status_state.nvm_info_failed = 0;
  wifi_status_state.nvm_info_response_seen = 0;
  wifi_status_state.nvm_info_errors = 0;
  wifi_status_state.nvm_info_generation = 0;
  wifi_status_state.nvm_info_flags = 0;
  wifi_status_state.nvm_info_version = 0;
  wifi_status_state.nvm_info_board_type = 0;
  wifi_status_state.nvm_info_hw_addrs = 0;
  wifi_status_state.nvm_info_mac_sku_flags = 0;
  wifi_status_state.nvm_info_tx_chains = 0;
  wifi_status_state.nvm_info_rx_chains = 0;
  wifi_status_state.nvm_info_lar_enabled = 0;
  wifi_status_state.nvm_info_n_channels = 0;
  wifi_status_state.scan_ready = 0;
  wifi_status_state.scan_failed = 0;
  wifi_status_state.scan_response_seen = 0;
  wifi_status_state.scan_start_seen = 0;
  wifi_status_state.scan_iter_seen = 0;
  wifi_status_state.scan_complete_seen = 0;
  wifi_status_state.scan_inflight = 0;
  wifi_status_state.scan_errors = 0;
  wifi_status_state.scan_generation = 0;
  wifi_status_state.scan_notifications = 0;
  wifi_status_state.scan_uid = 0;
  wifi_status_state.scan_cmd_len = 0;
  wifi_status_state.scan_version = 0;
  wifi_status_state.scan_flags = 0;
  wifi_status_state.scan_general_flags = 0;
  wifi_status_state.scan_channel_count = 0;
  wifi_status_state.scan_channel_first = 0;
  wifi_status_state.scan_channel_last = 0;
  wifi_status_state.scan_dwell_active = 0;
  wifi_status_state.scan_dwell_passive = 0;
  wifi_status_state.scan_start_uid = 0;
  wifi_status_state.scan_iter_uid = 0;
  wifi_status_state.scan_iter_channels = 0;
  wifi_status_state.scan_iter_status = 0;
  wifi_status_state.scan_iter_bt_status = 0;
  wifi_status_state.scan_iter_last_channel = 0;
  wifi_status_state.scan_iter_tsf_low = 0;
  wifi_status_state.scan_iter_tsf_high = 0;
  wifi_status_state.scan_complete_uid = 0;
  wifi_status_state.scan_complete_last_schedule = 0;
  wifi_status_state.scan_complete_last_iter = 0;
  wifi_status_state.scan_complete_status = 0;
  wifi_status_state.scan_complete_ebs_status = 0;
  wifi_status_state.scan_complete_elapsed = 0;
  wifi_status_state.scan_result_truncated = 0;
  wifi_status_state.scan_result_count = 0;
  wifi_status_state.scan_result_reported = 0;
  wifi_status_state.scan_result_entries = 0;
  wifi_status_state.scan_result_bytes = 0;
  memset(wifi_status_state.scan_result_channel, 0,
         sizeof(wifi_status_state.scan_result_channel));
  memset(wifi_status_state.scan_result_band, 0,
         sizeof(wifi_status_state.scan_result_band));
  memset(wifi_status_state.scan_result_probe_status, 0,
         sizeof(wifi_status_state.scan_result_probe_status));
  memset(wifi_status_state.scan_result_probe_not_sent, 0,
         sizeof(wifi_status_state.scan_result_probe_not_sent));
  memset(wifi_status_state.scan_result_duration, 0,
         sizeof(wifi_status_state.scan_result_duration));
  wifi_scan_clear_access_points();
  wifi_connect_clear_plan();
  wifi_status_state.connect_attempts = 0;
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
  wifi_status_state.rx_last_word0 = 0;
  wifi_status_state.rx_last_word1 = 0;
  wifi_status_state.rx_last_word2 = 0;
  wifi_status_state.rx_last_word3 = 0;
  wifi_status_state.rx_last_cd_word0 = 0;
  wifi_status_state.rx_last_cd_word1 = 0;
  wifi_status_state.rx_last_cd_word2 = 0;
  wifi_status_state.rx_last_cd_word3 = 0;
  wifi_status_state.command_ready = 0;
  wifi_status_state.command_sent = 0;
  wifi_status_state.command_failed = 0;
  wifi_status_state.command_response_seen = 0;
  wifi_status_state.command_timeout = 0;
  wifi_status_state.command_doorbell_value = 0;
  wifi_status_state.command_wptr_readback = 0;
  wifi_status_state.command_bc_entry = 0;
  wifi_status_state.command_tfd_num_tbs = 0;
  wifi_status_state.command_tfd_tb0_len = 0;
  wifi_status_state.command_tfd_tb0_addr = 0;
  wifi_status_state.command_before_csr_int = 0;
  wifi_status_state.command_before_fh_int = 0;
  wifi_status_state.command_before_closed_rb = 0;
  wifi_status_state.command_before_read_ptr = 0;
  wifi_status_state.command_after_csr_int = 0;
  wifi_status_state.command_after_fh_int = 0;
  wifi_status_state.command_after_closed_rb = 0;
  wifi_status_state.command_after_read_ptr = 0;
  wifi_status_state.command_last_csr_int = 0;
  wifi_status_state.command_last_fh_int = 0;
  wifi_status_state.command_last_closed_rb = 0;
  wifi_status_state.command_poll_loops = 0;
  wifi_status_state.nvm_ready = 0;
  wifi_status_state.nvm_failed = 0;
  wifi_status_state.nvm_response_seen = 0;
  wifi_status_state.nvm_op = 0;
  wifi_status_state.nvm_target = 0;
  wifi_status_state.nvm_section = 0;
  wifi_status_state.nvm_offset = 0;
  wifi_status_state.nvm_length = 0;
  wifi_status_state.nvm_resp_offset = 0;
  wifi_status_state.nvm_resp_length = 0;
  wifi_status_state.nvm_resp_type = 0;
  wifi_status_state.nvm_resp_status = 0;
  wifi_status_state.nvm_resp_word0 = 0;
  wifi_status_state.nvm_resp_word1 = 0;
  wifi_status_state.nvm_resp_word2 = 0;
  wifi_status_state.nvm_resp_word3 = 0;
  wifi_status_state.nvm_info_ready = 0;
  wifi_status_state.nvm_info_failed = 0;
  wifi_status_state.nvm_info_response_seen = 0;
  wifi_status_state.nvm_info_flags = 0;
  wifi_status_state.nvm_info_version = 0;
  wifi_status_state.nvm_info_board_type = 0;
  wifi_status_state.nvm_info_hw_addrs = 0;
  wifi_status_state.nvm_info_mac_sku_flags = 0;
  wifi_status_state.nvm_info_tx_chains = 0;
  wifi_status_state.nvm_info_rx_chains = 0;
  wifi_status_state.nvm_info_lar_enabled = 0;
  wifi_status_state.nvm_info_n_channels = 0;
  wifi_status_state.scan_ready = 0;
  wifi_status_state.scan_failed = 0;
  wifi_status_state.scan_response_seen = 0;
  wifi_status_state.scan_start_seen = 0;
  wifi_status_state.scan_iter_seen = 0;
  wifi_status_state.scan_complete_seen = 0;
  wifi_status_state.scan_inflight = 0;
  wifi_status_state.scan_notifications = 0;
  wifi_status_state.scan_uid = 0;
  wifi_status_state.scan_cmd_len = 0;
  wifi_status_state.scan_version = 0;
  wifi_status_state.scan_flags = 0;
  wifi_status_state.scan_general_flags = 0;
  wifi_status_state.scan_channel_count = 0;
  wifi_status_state.scan_channel_first = 0;
  wifi_status_state.scan_channel_last = 0;
  wifi_status_state.scan_dwell_active = 0;
  wifi_status_state.scan_dwell_passive = 0;
  wifi_status_state.scan_start_uid = 0;
  wifi_status_state.scan_iter_uid = 0;
  wifi_status_state.scan_iter_channels = 0;
  wifi_status_state.scan_iter_status = 0;
  wifi_status_state.scan_iter_bt_status = 0;
  wifi_status_state.scan_iter_last_channel = 0;
  wifi_status_state.scan_iter_tsf_low = 0;
  wifi_status_state.scan_iter_tsf_high = 0;
  wifi_status_state.scan_complete_uid = 0;
  wifi_status_state.scan_complete_last_schedule = 0;
  wifi_status_state.scan_complete_last_iter = 0;
  wifi_status_state.scan_complete_status = 0;
  wifi_status_state.scan_complete_ebs_status = 0;
  wifi_status_state.scan_complete_elapsed = 0;
  wifi_status_state.scan_result_truncated = 0;
  wifi_status_state.scan_result_count = 0;
  wifi_status_state.scan_result_reported = 0;
  wifi_status_state.scan_result_entries = 0;
  wifi_status_state.scan_result_bytes = 0;
  memset(wifi_status_state.scan_result_channel, 0,
         sizeof(wifi_status_state.scan_result_channel));
  memset(wifi_status_state.scan_result_band, 0,
         sizeof(wifi_status_state.scan_result_band));
  memset(wifi_status_state.scan_result_probe_status, 0,
         sizeof(wifi_status_state.scan_result_probe_status));
  memset(wifi_status_state.scan_result_probe_not_sent, 0,
         sizeof(wifi_status_state.scan_result_probe_not_sent));
  memset(wifi_status_state.scan_result_duration, 0,
         sizeof(wifi_status_state.scan_result_duration));
  wifi_scan_clear_access_points();
  wifi_connect_clear_plan();
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

static const char *wifi_tx_stage_kind_text(uint32_t kind);

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
           "scheduler=%s rx=%s command=%s nvm=%s nvm-info=%s scan=%s "
           "connect=%s bind=%s txstage=%s txcmd=%s status=%s",
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
           s->nvm_response_seen ? "response"
                                : (s->nvm_ready
                                       ? "ready"
                                       : (s->nvm_failed ? "failed" : "idle")),
           s->nvm_info_response_seen
               ? "response"
               : (s->nvm_info_ready
                      ? "ready"
                      : (s->nvm_info_failed ? "failed" : "idle")),
           s->scan_complete_seen
               ? "complete"
               : (s->scan_iter_seen
                      ? "iter"
                      : (s->scan_start_seen
                             ? "started"
                             : (s->scan_response_seen
                                    ? "response"
                                    : (s->scan_inflight
                                           ? "inflight"
                                           : (s->scan_ready
                                                  ? "ready"
                                                  : (s->scan_failed ? "failed"
                                                                    : "idle")))))),
           s->connect_assoc_response_seen
               ? (s->connect_assoc_status ? "assoc-failed" : "assoc-rx")
               : (s->connect_auth_response_seen
                      ? (s->connect_auth_status ? "auth-failed" : "auth-rx")
                      : (s->connect_ready
                             ? (s->connect_wpa ? "wpa-plan" : "open-plan")
                             : (s->connect_failed ? "failed" : "idle"))),
           s->bind_ready ? "ready" : (s->bind_failed ? "failed" : "idle"),
           s->tx_stage_ready
               ? wifi_tx_stage_kind_text(s->tx_stage_kind)
               : (s->tx_stage_failed ? "failed" : "idle"),
           s->txcmd_ready
               ? wifi_tx_stage_kind_text(s->txcmd_kind)
               : (s->txcmd_failed ? "failed" : "idle"),
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

static void wifi_nvm_mark_failure(const char *status) {
  wifi_status_state.nvm_ready = 0;
  wifi_status_state.nvm_failed = 1;
  wifi_status_state.nvm_response_seen = 0;
  wifi_status_state.nvm_errors++;
  wifi_status_state.status = status;
}

static void wifi_nvm_info_mark_failure(const char *status) {
  wifi_status_state.nvm_info_ready = 0;
  wifi_status_state.nvm_info_failed = 1;
  wifi_status_state.nvm_info_response_seen = 0;
  wifi_status_state.nvm_info_errors++;
  wifi_status_state.status = status;
}

static void wifi_scan_mark_failure(const char *status) {
  wifi_status_state.scan_ready = 0;
  wifi_status_state.scan_inflight = 0;
  wifi_status_state.scan_failed = 1;
  wifi_status_state.scan_errors++;
  wifi_status_state.status = status;
}

static int wifi_is_scan_notification_group(uint8_t group_id) {
  return group_id == WIFI_CMD_GROUP_LONG || group_id == WIFI_CMD_GROUP_LEGACY ||
         group_id == WIFI_CMD_GROUP_SCAN;
}

static const char *wifi_scan_complete_status_text(uint32_t status) {
  switch (status) {
  case 1U:
    return "completed";
  case 2U:
    return "aborted";
  default:
    return "unknown";
  }
}

static const char *wifi_scan_ebs_status_text(uint32_t status) {
  switch (status) {
  case 0U:
    return "success";
  case 1U:
    return "failed";
  case 2U:
    return "chan-not-found";
  case 3U:
    return "inactive";
  default:
    return "unknown";
  }
}

static const char *wifi_scan_band_text(uint32_t band) {
  switch (band) {
  case 0U:
    return "5GHz";
  case 1U:
    return "2.4GHz";
  default:
    return "unknown";
  }
}

static void wifi_scan_clear_channel_results(void) {
  wifi_status_state.scan_result_truncated = 0;
  wifi_status_state.scan_result_count = 0;
  wifi_status_state.scan_result_reported = 0;
  wifi_status_state.scan_result_entries = 0;
  wifi_status_state.scan_result_bytes = 0;
  memset(wifi_status_state.scan_result_channel, 0,
         sizeof(wifi_status_state.scan_result_channel));
  memset(wifi_status_state.scan_result_band, 0,
         sizeof(wifi_status_state.scan_result_band));
  memset(wifi_status_state.scan_result_probe_status, 0,
         sizeof(wifi_status_state.scan_result_probe_status));
  memset(wifi_status_state.scan_result_probe_not_sent, 0,
         sizeof(wifi_status_state.scan_result_probe_not_sent));
  memset(wifi_status_state.scan_result_duration, 0,
         sizeof(wifi_status_state.scan_result_duration));
}

static int wifi_scan_parse_channel_results(const uint8_t *payload,
                                           uint32_t payload_len) {
  uint32_t result_offset = (uint32_t)sizeof(wifi_umac_scan_iter_complete_t);
  uint32_t result_bytes;
  uint32_t reported;
  uint32_t entries;
  uint32_t stored;
  int malformed = 0;
  int truncated = 0;

  wifi_scan_clear_channel_results();
  if (!payload || payload_len < result_offset) {
    return 1;
  }

  reported = payload[4];
  result_bytes = payload_len - result_offset;
  entries = result_bytes / (uint32_t)sizeof(wifi_scan_result_notif_t);
  stored = entries;
  if (stored > reported) {
    stored = reported;
  }
  if (stored > WIFI_SCAN_RESULT_SLOTS) {
    stored = WIFI_SCAN_RESULT_SLOTS;
    truncated = 1;
  }
  if (reported > entries ||
      (result_bytes % (uint32_t)sizeof(wifi_scan_result_notif_t)) != 0U) {
    truncated = 1;
    malformed = 1;
  }

  wifi_status_state.scan_result_reported = reported;
  wifi_status_state.scan_result_entries = entries;
  wifi_status_state.scan_result_bytes = result_bytes;
  wifi_status_state.scan_result_count = stored;
  wifi_status_state.scan_result_truncated = truncated;

  for (uint32_t i = 0; i < stored; i++) {
    const uint8_t *entry =
        payload + result_offset + i * sizeof(wifi_scan_result_notif_t);
    wifi_status_state.scan_result_channel[i] = entry[0];
    wifi_status_state.scan_result_band[i] = entry[1];
    wifi_status_state.scan_result_probe_status[i] = entry[2];
    wifi_status_state.scan_result_probe_not_sent[i] = entry[3];
    wifi_status_state.scan_result_duration[i] = wifi_read_le32(entry + 4U);
  }

  return malformed;
}

static int wifi_scan_bssid_equal(uint32_t slot, const uint8_t *bssid) {
  for (uint32_t i = 0; i < 6U; i++) {
    if (wifi_status_state.scan_ap_bssid[slot][i] != bssid[i]) {
      return 0;
    }
  }
  return 1;
}

static void wifi_scan_copy_ssid(uint32_t slot, const uint8_t *ssid,
                                uint32_t ssid_len) {
  uint32_t copy_len = ssid_len;

  if (copy_len > WIFI_SCAN_SSID_MAX) {
    copy_len = WIFI_SCAN_SSID_MAX;
  }

  for (uint32_t i = 0; i < copy_len; i++) {
    uint8_t c = ssid[i];
    wifi_status_state.scan_ap_ssid[slot][i] =
        wifi_is_printable_ascii(c) ? (char)c : '.';
  }
  wifi_status_state.scan_ap_ssid[slot][copy_len] = '\0';
  wifi_status_state.scan_ap_ssid_len[slot] = copy_len;
}

static int wifi_scan_store_ap(const uint8_t *bssid, const uint8_t *ssid,
                              uint32_t ssid_len, uint32_t channel,
                              uint32_t subtype, uint32_t capability,
                              uint32_t security) {
  uint32_t slot = WIFI_SCAN_AP_SLOTS;

  if (!bssid || !ssid) {
    return -1;
  }

  for (uint32_t i = 0; i < wifi_status_state.scan_ap_count; i++) {
    if (wifi_scan_bssid_equal(i, bssid)) {
      slot = i;
      break;
    }
  }

  if (slot == WIFI_SCAN_AP_SLOTS) {
    if (wifi_status_state.scan_ap_count >= WIFI_SCAN_AP_SLOTS) {
      wifi_status_state.scan_ap_overflow++;
      return -1;
    }
    slot = wifi_status_state.scan_ap_count++;
    for (uint32_t i = 0; i < 6U; i++) {
      wifi_status_state.scan_ap_bssid[slot][i] = bssid[i];
    }
  }

  if (ssid_len > 0 || wifi_status_state.scan_ap_ssid_len[slot] == 0) {
    wifi_scan_copy_ssid(slot, ssid, ssid_len);
  }
  wifi_status_state.scan_ap_channel[slot] = channel;
  wifi_status_state.scan_ap_frame_subtype[slot] = subtype;
  wifi_status_state.scan_ap_capability[slot] = capability;
  wifi_status_state.scan_ap_security[slot] = security;
  wifi_status_state.scan_ap_seen_count[slot]++;
  wifi_status_state.scan_ssid_updates++;
  return 0;
}

static void wifi_scan_record_candidate(uint32_t offset, uint32_t frame_len,
                                       uint16_t fc) {
  uint32_t slot = wifi_status_state.scan_candidate_count;

  if (slot >= WIFI_SCAN_CANDIDATE_SLOTS) {
    return;
  }

  wifi_status_state.scan_candidate_offset[slot] = offset;
  wifi_status_state.scan_candidate_len[slot] = frame_len;
  wifi_status_state.scan_candidate_fc[slot] = fc;
  wifi_status_state.scan_candidate_type[slot] =
      (fc & WIFI_80211_FC_TYPE_MASK) >> 2;
  wifi_status_state.scan_candidate_subtype[slot] =
      (fc & WIFI_80211_FC_SUBTYPE_MASK) >> 4;
  wifi_status_state.scan_candidate_count = slot + 1U;
}

static void wifi_scan_capture_mpdu_debug(const uint8_t *payload,
                                         uint32_t payload_len) {
  uint32_t copy_len = payload_len;

  if (copy_len > WIFI_SCAN_DEBUG_BYTES) {
    copy_len = WIFI_SCAN_DEBUG_BYTES;
  }

  wifi_status_state.scan_last_mpdu_payload_len = payload_len;
  wifi_status_state.scan_last_debug_len = copy_len;
  wifi_status_state.scan_candidate_count = 0;
  memset(wifi_status_state.scan_last_debug_bytes, 0,
         sizeof(wifi_status_state.scan_last_debug_bytes));
  memset(wifi_status_state.scan_candidate_offset, 0,
         sizeof(wifi_status_state.scan_candidate_offset));
  memset(wifi_status_state.scan_candidate_len, 0,
         sizeof(wifi_status_state.scan_candidate_len));
  memset(wifi_status_state.scan_candidate_fc, 0,
         sizeof(wifi_status_state.scan_candidate_fc));
  memset(wifi_status_state.scan_candidate_type, 0,
         sizeof(wifi_status_state.scan_candidate_type));
  memset(wifi_status_state.scan_candidate_subtype, 0,
         sizeof(wifi_status_state.scan_candidate_subtype));

  for (uint32_t i = 0; i < copy_len; i++) {
    wifi_status_state.scan_last_debug_bytes[i] = payload[i];
  }
}

static int wifi_scan_vendor_ie_is_wpa(const uint8_t *data, uint32_t len) {
  return data && len >= 4U && data[0] == 0x00U && data[1] == 0x50U &&
         data[2] == 0xf2U && data[3] == 0x01U;
}

static int wifi_scan_parse_80211_ies(const uint8_t *frame, uint32_t frame_len,
                                     uint32_t *channel,
                                     const uint8_t **ssid,
                                     uint32_t *ssid_len,
                                     uint32_t *security) {
  uint32_t pos = WIFI_80211_MGMT_HEADER_BYTES + WIFI_80211_BEACON_FIXED_BYTES;
  int ssid_seen = 0;

  *channel = 0;
  *ssid = 0;
  *ssid_len = 0;

  while (pos + 2U <= frame_len) {
    uint8_t id = frame[pos];
    uint8_t len = frame[pos + 1U];
    const uint8_t *data = frame + pos + 2U;

    if (pos + 2U + (uint32_t)len > frame_len) {
      break;
    }
    if (id == 0U && len <= WIFI_SCAN_SSID_MAX) {
      *ssid = data;
      *ssid_len = len;
      ssid_seen = 1;
    } else if (id == 3U && len >= 1U) {
      *channel = data[0];
    } else if (id == 48U) {
      *security = WIFI_SCAN_SECURITY_WPA2;
    } else if (id == 221U && *security != WIFI_SCAN_SECURITY_WPA2 &&
               wifi_scan_vendor_ie_is_wpa(data, len)) {
      *security = WIFI_SCAN_SECURITY_WPA;
    }
    pos += 2U + (uint32_t)len;
  }

  return ssid_seen;
}

static int wifi_mac_equal6(const uint8_t *a, const uint8_t *b) {
  if (!a || !b) {
    return 0;
  }
  for (uint32_t i = 0; i < 6U; i++) {
    if (a[i] != b[i]) {
      return 0;
    }
  }
  return 1;
}

static uint16_t wifi_read_be16(const uint8_t *p) {
  return (uint16_t)(((uint16_t)p[0] << 8) | (uint16_t)p[1]);
}

static uint32_t wifi_read_be32(const uint8_t *p) {
  return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
         ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static void wifi_write_be16(uint8_t *p, uint16_t value) {
  p[0] = (uint8_t)((value >> 8) & 0xffU);
  p[1] = (uint8_t)(value & 0xffU);
}

static void wifi_write_be32(uint8_t *p, uint32_t value) {
  p[0] = (uint8_t)(value >> 24);
  p[1] = (uint8_t)((value >> 16) & 0xffU);
  p[2] = (uint8_t)((value >> 8) & 0xffU);
  p[3] = (uint8_t)(value & 0xffU);
}

static uint32_t wifi_wpa_checksum(const uint8_t *data, uint32_t len) {
  uint32_t hash = 2166136261U;

  for (uint32_t i = 0; i < len; i++) {
    hash ^= data[i];
    hash *= 16777619U;
  }
  return hash ? hash : 1U;
}

static void wifi_connect_copy_mac(uint8_t *dst, const uint8_t *src);

static int wifi_wpa_lexicographic_less(const uint8_t *a, const uint8_t *b,
                                       uint32_t len) {
  for (uint32_t i = 0; i < len; i++) {
    if (a[i] < b[i]) {
      return 1;
    }
    if (a[i] > b[i]) {
      return 0;
    }
  }
  return 0;
}

static int wifi_wpa_prf_sha1(const uint8_t *key, uint32_t key_len,
                             const char *label, const uint8_t *seed,
                             uint32_t seed_len, uint8_t *out,
                             uint32_t out_len) {
  uint8_t block[160];
  uint8_t digest[SHA1_DIGEST_SIZE];
  uint32_t label_len;
  uint32_t produced = 0;
  uint8_t counter = 0;

  if (!key || !label || !seed || !out || out_len == 0) {
    return -1;
  }

  label_len = (uint32_t)strlen(label);
  if (label_len + 1U + seed_len + 1U > sizeof(block)) {
    return -1;
  }

  while (produced < out_len) {
    uint32_t offset = 0;
    uint32_t take;

    memcpy(block + offset, label, label_len);
    offset += label_len;
    block[offset++] = 0;
    memcpy(block + offset, seed, seed_len);
    offset += seed_len;
    block[offset++] = counter++;

    hmac_sha1(key, key_len, block, offset, digest);
    take = out_len - produced;
    if (take > SHA1_DIGEST_SIZE) {
      take = SHA1_DIGEST_SIZE;
    }
    memcpy(out + produced, digest, take);
    produced += take;
  }

  memset(block, 0, sizeof(block));
  memset(digest, 0, sizeof(digest));
  return 0;
}

static void wifi_wpa_seed_append_u32(uint8_t *seed, uint32_t *offset,
                                     uint32_t value) {
  wifi_write_be32(seed + *offset, value);
  *offset += 4U;
}

static int wifi_wpa_generate_snonce(void) {
  uint8_t seed[128];
  uint32_t offset = 0;

  memset(seed, 0, sizeof(seed));
  memcpy(seed + offset, wifi_status_state.connect_local_mac, 6U);
  offset += 6U;
  memcpy(seed + offset, wifi_status_state.connect_bssid, 6U);
  offset += 6U;
  memcpy(seed + offset, wifi_wpa_anonce, sizeof(wifi_wpa_anonce));
  offset += sizeof(wifi_wpa_anonce);
  wifi_wpa_seed_append_u32(seed, &offset,
                           (uint32_t)wifi_status_state.connect_attempts);
  wifi_wpa_seed_append_u32(seed, &offset,
                           (uint32_t)wifi_status_state.scan_generation);
  wifi_wpa_seed_append_u32(seed, &offset,
                           wifi_status_state.wpa_replay_counter_hi);
  wifi_wpa_seed_append_u32(seed, &offset,
                           wifi_status_state.wpa_replay_counter_lo);
  wifi_wpa_seed_append_u32(seed, &offset,
                           ((uint32_t)wifi_status_state.vendor_id << 16) |
                               wifi_status_state.device_id);

  if (wifi_wpa_prf_sha1(wifi_connect_pmk, sizeof(wifi_connect_pmk),
                        "Orizon OS WPA2 SNonce", seed, offset,
                        wifi_wpa_snonce, sizeof(wifi_wpa_snonce)) != 0) {
    memset(seed, 0, sizeof(seed));
    return -1;
  }

  wifi_status_state.wpa_snonce_checksum =
      wifi_wpa_checksum(wifi_wpa_snonce, sizeof(wifi_wpa_snonce));
  memset(seed, 0, sizeof(seed));
  return 0;
}

static int wifi_wpa_derive_ptk(void) {
  uint8_t seed[76];
  uint32_t offset = 0;
  const uint8_t *mac_low;
  const uint8_t *mac_high;
  const uint8_t *nonce_low;
  const uint8_t *nonce_high;

  if (!wifi_status_state.connect_pmk_ready ||
      !wifi_status_state.wpa_anonce_seen) {
    return -1;
  }

  if (wifi_wpa_generate_snonce() != 0) {
    return -1;
  }

  if (wifi_wpa_lexicographic_less(wifi_status_state.connect_local_mac,
                                  wifi_status_state.connect_bssid, 6U)) {
    mac_low = wifi_status_state.connect_local_mac;
    mac_high = wifi_status_state.connect_bssid;
  } else {
    mac_low = wifi_status_state.connect_bssid;
    mac_high = wifi_status_state.connect_local_mac;
  }

  if (wifi_wpa_lexicographic_less(wifi_wpa_snonce, wifi_wpa_anonce,
                                  sizeof(wifi_wpa_snonce))) {
    nonce_low = wifi_wpa_snonce;
    nonce_high = wifi_wpa_anonce;
  } else {
    nonce_low = wifi_wpa_anonce;
    nonce_high = wifi_wpa_snonce;
  }

  memcpy(seed + offset, mac_low, 6U);
  offset += 6U;
  memcpy(seed + offset, mac_high, 6U);
  offset += 6U;
  memcpy(seed + offset, nonce_low, WIFI_WPA_NONCE_BYTES);
  offset += WIFI_WPA_NONCE_BYTES;
  memcpy(seed + offset, nonce_high, WIFI_WPA_NONCE_BYTES);
  offset += WIFI_WPA_NONCE_BYTES;

  if (wifi_wpa_prf_sha1(wifi_connect_pmk, sizeof(wifi_connect_pmk),
                        "Pairwise key expansion", seed, offset, wifi_wpa_ptk,
                        sizeof(wifi_wpa_ptk)) != 0) {
    memset(seed, 0, sizeof(seed));
    return -1;
  }

  wifi_status_state.wpa_ptk_ready = 1;
  wifi_status_state.wpa_ptk_checksum =
      wifi_wpa_checksum(wifi_wpa_ptk, sizeof(wifi_wpa_ptk));
  wifi_status_state.wpa_kck_checksum =
      wifi_wpa_checksum(wifi_wpa_ptk, WIFI_WPA_KCK_BYTES);
  wifi_status_state.wpa_kek_checksum =
      wifi_wpa_checksum(wifi_wpa_ptk + WIFI_WPA_KCK_BYTES, WIFI_WPA_KEK_BYTES);
  wifi_status_state.wpa_tk_checksum =
      wifi_wpa_checksum(wifi_wpa_ptk + WIFI_WPA_KCK_BYTES + WIFI_WPA_KEK_BYTES,
                        WIFI_WPA_TK_BYTES);
  memset(seed, 0, sizeof(seed));
  return 0;
}

static int wifi_wpa_build_m2(uint8_t eapol_version, uint8_t descriptor_type) {
  static const uint8_t rsn_wpa2_psk_ccmp_ie[] = {
      0x30U, 0x14U, 0x01U, 0x00U, 0x00U, 0x0fU, 0xacU, 0x04U,
      0x01U, 0x00U, 0x00U, 0x0fU, 0xacU, 0x04U, 0x01U, 0x00U,
      0x00U, 0x0fU, 0xacU, 0x02U, 0x00U, 0x00U};
  uint32_t key_body_len = 95U + (uint32_t)sizeof(rsn_wpa2_psk_ccmp_ie);
  uint32_t frame_len = 4U + key_body_len;
  uint16_t key_info;
  uint8_t mic[SHA1_DIGEST_SIZE];

  if (!wifi_status_state.wpa_ptk_ready ||
      frame_len > sizeof(wifi_wpa_eapol_m2)) {
    return -1;
  }

  memset(wifi_wpa_eapol_m2, 0, sizeof(wifi_wpa_eapol_m2));
  wifi_wpa_eapol_m2[0] = eapol_version ? eapol_version : 2U;
  wifi_wpa_eapol_m2[1] = WIFI_EAPOL_TYPE_KEY;
  wifi_write_be16(wifi_wpa_eapol_m2 + 2U, (uint16_t)key_body_len);
  wifi_wpa_eapol_m2[4] = descriptor_type ? descriptor_type : 2U;

  key_info = (uint16_t)(wifi_status_state.wpa_key_info &
                        WIFI_WPA_KEY_INFO_TYPE_MASK);
  if (!key_info) {
    key_info = 2U;
  }
  key_info |= (uint16_t)(WIFI_WPA_KEY_INFO_PAIRWISE | WIFI_WPA_KEY_INFO_MIC);
  wifi_write_be16(wifi_wpa_eapol_m2 + 5U, key_info);
  wifi_write_be16(wifi_wpa_eapol_m2 + 7U,
                  (uint16_t)wifi_status_state.wpa_key_len);
  wifi_write_be32(wifi_wpa_eapol_m2 + 9U,
                  wifi_status_state.wpa_replay_counter_hi);
  wifi_write_be32(wifi_wpa_eapol_m2 + 13U,
                  wifi_status_state.wpa_replay_counter_lo);
  memcpy(wifi_wpa_eapol_m2 + 17U, wifi_wpa_snonce, sizeof(wifi_wpa_snonce));
  wifi_write_be16(wifi_wpa_eapol_m2 + 97U,
                  (uint16_t)sizeof(rsn_wpa2_psk_ccmp_ie));
  memcpy(wifi_wpa_eapol_m2 + 99U, rsn_wpa2_psk_ccmp_ie,
         sizeof(rsn_wpa2_psk_ccmp_ie));

  hmac_sha1(wifi_wpa_ptk, WIFI_WPA_KCK_BYTES, wifi_wpa_eapol_m2, frame_len,
            mic);
  memcpy(wifi_wpa_eapol_m2 + 81U, mic, 16U);

  wifi_status_state.wpa_m2_ready = 1;
  wifi_status_state.wpa_m2_frame_len = frame_len;
  wifi_status_state.wpa_m2_key_info = key_info;
  wifi_status_state.wpa_m2_key_data_len =
      (uint32_t)sizeof(rsn_wpa2_psk_ccmp_ie);
  wifi_status_state.wpa_m2_mic_checksum = wifi_wpa_checksum(mic, 16U);
  wifi_status_state.wpa_m2_checksum =
      wifi_wpa_checksum(wifi_wpa_eapol_m2, frame_len);
  memset(mic, 0, sizeof(mic));
  return 0;
}

static int wifi_wpa_build_m2_data_frame(void) {
  uint16_t fc =
      (uint16_t)((WIFI_80211_TYPE_DATA << 2) | WIFI_80211_FC_TODS);
  uint32_t offset = WIFI_80211_MGMT_HEADER_BYTES;
  static const uint8_t llc_snap_eapol[] = {
      0xaaU, 0xaaU, 0x03U, 0x00U, 0x00U, 0x00U, 0x88U, 0x8eU};

  if (!wifi_status_state.wpa_m2_ready ||
      offset + sizeof(llc_snap_eapol) + wifi_status_state.wpa_m2_frame_len >
          sizeof(wifi_wpa_m2_data_frame)) {
    return -1;
  }

  memset(wifi_wpa_m2_data_frame, 0, sizeof(wifi_wpa_m2_data_frame));
  wifi_write_le16(wifi_wpa_m2_data_frame + 0U, fc);
  wifi_write_le16(wifi_wpa_m2_data_frame + 2U, 0);
  wifi_connect_copy_mac(wifi_wpa_m2_data_frame + 4U,
                        wifi_status_state.connect_bssid);
  wifi_connect_copy_mac(wifi_wpa_m2_data_frame + 10U,
                        wifi_status_state.connect_local_mac);
  wifi_connect_copy_mac(wifi_wpa_m2_data_frame + 16U,
                        wifi_status_state.connect_bssid);
  wifi_write_le16(wifi_wpa_m2_data_frame + 22U, 0);
  memcpy(wifi_wpa_m2_data_frame + offset, llc_snap_eapol,
         sizeof(llc_snap_eapol));
  offset += (uint32_t)sizeof(llc_snap_eapol);
  memcpy(wifi_wpa_m2_data_frame + offset, wifi_wpa_eapol_m2,
         wifi_status_state.wpa_m2_frame_len);
  offset += wifi_status_state.wpa_m2_frame_len;

  wifi_status_state.wpa_m2_data_ready = 1;
  wifi_status_state.wpa_m2_data_frame_len = offset;
  return 0;
}

static int wifi_wpa_prepare_m2(uint8_t eapol_version, uint8_t descriptor_type) {
  if (wifi_wpa_derive_ptk() != 0 ||
      wifi_wpa_build_m2(eapol_version, descriptor_type) != 0 ||
      wifi_wpa_build_m2_data_frame() != 0) {
    wifi_status_state.wpa_ptk_ready = 0;
    wifi_status_state.wpa_m2_ready = 0;
    wifi_status_state.wpa_m2_data_ready = 0;
    return -1;
  }
  return 0;
}

static int wifi_connect_frame_matches_plan(const uint8_t *frame,
                                           uint32_t frame_len) {
  if (!wifi_status_state.connect_ready ||
      frame_len < WIFI_80211_MGMT_HEADER_BYTES) {
    return 0;
  }

  return wifi_mac_equal6(frame + 4U, wifi_status_state.connect_local_mac) &&
         wifi_mac_equal6(frame + 10U, wifi_status_state.connect_bssid) &&
         wifi_mac_equal6(frame + 16U, wifi_status_state.connect_bssid);
}

static int wifi_connect_parse_response_frame(const uint8_t *frame,
                                             uint32_t frame_len,
                                             uint32_t subtype) {
  if (!wifi_connect_frame_matches_plan(frame, frame_len)) {
    return 0;
  }

  wifi_status_state.connect_last_rx_subtype = subtype;
  wifi_status_state.connect_response_packets++;

  if (subtype == WIFI_80211_SUBTYPE_AUTH) {
    uint32_t offset = WIFI_80211_MGMT_HEADER_BYTES;
    if (frame_len < offset + 6U) {
      wifi_status_state.connect_response_failed = 1;
      wifi_status_state.status = "wifi: short authentication response frame";
      return 1;
    }
    wifi_status_state.connect_auth_response_seen = 1;
    wifi_status_state.connect_auth_alg = wifi_read_le16(frame + offset);
    wifi_status_state.connect_auth_seq = wifi_read_le16(frame + offset + 2U);
    wifi_status_state.connect_auth_status = wifi_read_le16(frame + offset + 4U);
    wifi_status_state.connect_response_failed =
        wifi_status_state.connect_auth_status ? 1 : 0;
    wifi_status_state.status = wifi_status_state.connect_auth_status
                                   ? "wifi: authentication response failed"
                                   : "wifi: authentication response accepted";
    return 1;
  }

  if (subtype == WIFI_80211_SUBTYPE_ASSOC_RESP) {
    uint32_t offset = WIFI_80211_MGMT_HEADER_BYTES;
    if (frame_len < offset + 6U) {
      wifi_status_state.connect_response_failed = 1;
      wifi_status_state.status = "wifi: short association response frame";
      return 1;
    }
    wifi_status_state.connect_assoc_response_seen = 1;
    wifi_status_state.connect_assoc_status = wifi_read_le16(frame + offset + 2U);
    wifi_status_state.connect_assoc_aid =
        wifi_read_le16(frame + offset + 4U) & 0x3fffU;
    wifi_status_state.connect_response_failed =
        wifi_status_state.connect_assoc_status ? 1 : 0;
    if (wifi_status_state.connect_assoc_status == 0) {
      wifi_status_state.associated = wifi_status_state.connect_wpa ? 0 : 1;
      wifi_status_state.driver_ready = wifi_status_state.connect_wpa ? 0 : 1;
      wifi_status_state.status = wifi_status_state.connect_wpa
                                     ? "wifi: association accepted; WPA pending"
                                     : "wifi: open association accepted";
    } else {
      wifi_status_state.status = "wifi: association response failed";
    }
    return 1;
  }

  return 0;
}

static uint32_t wifi_80211_data_header_len(uint16_t fc, uint32_t subtype) {
  uint32_t len = WIFI_80211_MGMT_HEADER_BYTES;

  if ((fc & WIFI_80211_FC_TODS) && (fc & WIFI_80211_FC_FROMDS)) {
    len += 6U;
  }
  if (subtype & WIFI_80211_SUBTYPE_QOS_DATA) {
    len += 2U;
  }
  return len;
}

static int wifi_wpa_parse_eapol_key(const uint8_t *frame, uint32_t frame_len,
                                    uint16_t fc, uint32_t subtype) {
  uint32_t hdr_len;
  const uint8_t *llc;
  const uint8_t *eapol;
  uint32_t eapol_len;
  uint32_t key_body_len;
  uint32_t key_info;
  int is_pairwise_m1;

  if (!wifi_status_state.connect_ready ||
      !wifi_mac_equal6(frame + 4U, wifi_status_state.connect_local_mac) ||
      !wifi_mac_equal6(frame + 10U, wifi_status_state.connect_bssid)) {
    return 0;
  }

  hdr_len = wifi_80211_data_header_len(fc, subtype);
  if (frame_len < hdr_len + 8U + 4U) {
    return 0;
  }
  llc = frame + hdr_len;
  if (llc[0] != 0xaaU || llc[1] != 0xaaU || llc[2] != 0x03U ||
      llc[3] != 0x00U || llc[4] != 0x00U || llc[5] != 0x00U ||
      wifi_read_be16(llc + 6U) != WIFI_EAPOL_ETHERTYPE) {
    return 0;
  }

  eapol = llc + 8U;
  eapol_len = frame_len - hdr_len - 8U;
  if (eapol_len < 4U || eapol[1] != WIFI_EAPOL_TYPE_KEY) {
    return 0;
  }

  key_body_len = wifi_read_be16(eapol + 2U);
  if (key_body_len + 4U > eapol_len || key_body_len < 95U) {
    return 0;
  }

  wifi_status_state.wpa_eapol_packets++;
  wifi_status_state.wpa_key_frames++;
  key_info = wifi_read_be16(eapol + 5U);
  wifi_status_state.wpa_key_info = key_info;
  wifi_status_state.wpa_key_len = wifi_read_be16(eapol + 7U);
  wifi_status_state.wpa_replay_counter_hi = wifi_read_be32(eapol + 9U);
  wifi_status_state.wpa_replay_counter_lo = wifi_read_be32(eapol + 13U);
  wifi_status_state.wpa_key_data_len = wifi_read_be16(eapol + 97U);
  wifi_status_state.wpa_key_desc_type = eapol[4];
  wifi_status_state.wpa_eapol_version = eapol[0];
  wifi_status_state.wpa_anonce_seen = 1;
  memcpy(wifi_wpa_anonce, eapol + 17U, sizeof(wifi_wpa_anonce));
  wifi_status_state.wpa_anonce_checksum =
      wifi_wpa_checksum(wifi_wpa_anonce, sizeof(wifi_wpa_anonce));

  is_pairwise_m1 = (key_info & WIFI_WPA_KEY_INFO_PAIRWISE) &&
                   (key_info & WIFI_WPA_KEY_INFO_ACK) &&
                   !(key_info & WIFI_WPA_KEY_INFO_MIC);
  if (is_pairwise_m1) {
    wifi_status_state.wpa_m1_seen = 1;
    if (wifi_status_state.connect_pmk_ready &&
        wifi_wpa_prepare_m2(eapol[0], eapol[4]) == 0) {
      wifi_status_state.status =
          "wifi: WPA M1 parsed; PTK and M2 response prepared";
    } else {
      wifi_status_state.status =
          "wifi: WPA M1 parsed; PTK/M2 preparation pending";
    }
  } else {
    wifi_status_state.status =
        "wifi: WPA EAPOL-Key frame parsed; ANonce captured";
  }
  return 1;
}

static int wifi_scan_parse_80211_frame(const uint8_t *frame, uint32_t frame_len,
                                       uint32_t frame_offset,
                                       uint32_t mpdu_len) {
  uint16_t fc;
  uint32_t type;
  uint32_t subtype;
  uint32_t channel;
  uint32_t capability;
  uint32_t security;
  const uint8_t *ssid;
  uint32_t ssid_len;

  if (!frame || frame_len < WIFI_80211_MGMT_HEADER_BYTES +
                              WIFI_80211_BEACON_FIXED_BYTES) {
    return 0;
  }

  fc = wifi_read_le16(frame);
  type = (fc & WIFI_80211_FC_TYPE_MASK) >> 2;
  subtype = (fc & WIFI_80211_FC_SUBTYPE_MASK) >> 4;
  if (type == WIFI_80211_TYPE_DATA) {
    if (wifi_wpa_parse_eapol_key(frame, frame_len, fc, subtype)) {
      wifi_status_state.scan_last_mpdu_len = mpdu_len;
      wifi_status_state.scan_last_frame_offset = frame_offset;
      wifi_status_state.scan_last_frame_len = frame_len;
      wifi_status_state.scan_last_frame_control = fc;
      wifi_status_state.scan_last_frame_subtype = subtype;
      return 1;
    }
    return 0;
  }

  if (type != WIFI_80211_TYPE_MGMT ||
      (subtype != WIFI_80211_SUBTYPE_BEACON &&
       subtype != WIFI_80211_SUBTYPE_PROBE_RESP &&
       subtype != WIFI_80211_SUBTYPE_AUTH &&
       subtype != WIFI_80211_SUBTYPE_ASSOC_RESP)) {
    return 0;
  }

  if (subtype == WIFI_80211_SUBTYPE_AUTH ||
      subtype == WIFI_80211_SUBTYPE_ASSOC_RESP) {
    if (wifi_connect_parse_response_frame(frame, frame_len, subtype)) {
      wifi_status_state.scan_mgmt_frames++;
      wifi_status_state.scan_last_mpdu_len = mpdu_len;
      wifi_status_state.scan_last_frame_offset = frame_offset;
      wifi_status_state.scan_last_frame_len = frame_len;
      wifi_status_state.scan_last_frame_control = fc;
      wifi_status_state.scan_last_frame_subtype = subtype;
      return 1;
    }
    return 0;
  }

  capability = wifi_read_le16(frame + WIFI_80211_MGMT_HEADER_BYTES + 10U);
  security = (capability & WIFI_80211_CAP_PRIVACY)
                 ? WIFI_SCAN_SECURITY_WEP
                 : WIFI_SCAN_SECURITY_OPEN;
  if (!wifi_scan_parse_80211_ies(frame, frame_len, &channel, &ssid,
                                 &ssid_len, &security)) {
    return 0;
  }
  if (channel == 0) {
    channel = wifi_status_state.scan_iter_last_channel;
  }

  wifi_status_state.scan_mgmt_frames++;
  if (subtype == WIFI_80211_SUBTYPE_BEACON) {
    wifi_status_state.scan_beacon_frames++;
  } else {
    wifi_status_state.scan_probe_resp_frames++;
  }
  wifi_status_state.scan_last_mpdu_len = mpdu_len;
  wifi_status_state.scan_last_frame_offset = frame_offset;
  wifi_status_state.scan_last_frame_len = frame_len;
  wifi_status_state.scan_last_frame_control = fc;
  wifi_status_state.scan_last_frame_subtype = subtype;
  wifi_status_state.scan_last_frame_channel = channel;
  wifi_scan_store_ap(frame + 16U, ssid, ssid_len, channel, subtype, capability,
                     security);
  return 1;
}

static int wifi_scan_try_mpdu_candidate(const uint8_t *payload,
                                        uint32_t payload_len,
                                        uint32_t offset,
                                        uint32_t mpdu_len) {
  uint32_t frame_len;

  if (!payload || offset >= payload_len) {
    return 0;
  }

  frame_len = payload_len - offset;
  if (mpdu_len > 0 && mpdu_len < frame_len) {
    frame_len = mpdu_len;
  }
  if (frame_len < WIFI_80211_MGMT_HEADER_BYTES +
                      WIFI_80211_BEACON_FIXED_BYTES) {
    return 0;
  }
  wifi_scan_record_candidate(offset, frame_len, wifi_read_le16(payload + offset));
  return wifi_scan_parse_80211_frame(payload + offset, frame_len, offset,
                                     mpdu_len);
}

static int wifi_scan_parse_rx_mpdu(const uint8_t *payload,
                                   uint32_t payload_len) {
  uint32_t mpdu_len = payload_len >= 2U ? wifi_read_le16(payload) : 0;
  uint32_t candidates[] = {WIFI_RX_MPDU_DESC_SIZE_V3,
                           WIFI_RX_MPDU_DESC_SIZE_V1, 20U, 0U};

  wifi_status_state.scan_mpdu_packets++;
  wifi_status_state.scan_last_mpdu_len = mpdu_len;
  wifi_scan_capture_mpdu_debug(payload, payload_len);

  for (uint32_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); i++) {
    if (wifi_scan_try_mpdu_candidate(payload, payload_len, candidates[i],
                                     mpdu_len)) {
      return 1;
    }
  }

  for (uint32_t off = 0; off + WIFI_80211_MGMT_HEADER_BYTES +
                               WIFI_80211_BEACON_FIXED_BYTES <= payload_len;
       off += 2U) {
    if (wifi_scan_try_mpdu_candidate(payload, payload_len, off, 0)) {
      return 1;
    }
  }

  wifi_status_state.scan_mpdu_parse_misses++;
  return 0;
}

static int wifi_stage_command_payload(uint8_t cmd_id, uint8_t group_id,
                                      uint8_t version, const void *payload,
                                      uint32_t payload_len,
                                      const char *status) {
  uint32_t idx;
  uint32_t next_idx;
  uint32_t command_len;
  uint64_t frame_phys;
  wifi_cmd_header_wide_t *frame;
  wifi_tfh_tfd_t *tfd;

  if (!wifi_status_state.context_ready && wifi_prepare_context_info() != 0) {
    return -1;
  }
  if (!wifi_status_state.context_ready || !wifi_status_state.queues_ready) {
    return -1;
  }

  if (!wifi_status_state.cmd_tfd_phys) {
    return -1;
  }
  if (!wifi_status_state.cmd_bc_phys) {
    wifi_status_state.cmd_bc_phys = wifi_phys_addr(wifi_cmd_bc_tbl);
  }
  if (!wifi_status_state.cmd_bc_phys) {
    return -1;
  }
  if (!payload || payload_len == 0 ||
      sizeof(wifi_cmd_header_wide_t) + payload_len > WIFI_CMD_BUFFER_BYTES) {
    return -1;
  }

  idx = wifi_status_state.cmd_write_ptr % WIFI_CMD_QUEUE_ENTRIES;
  next_idx = (idx + 1U) % WIFI_CMD_QUEUE_ENTRIES;
  frame = (wifi_cmd_header_wide_t *)wifi_cmd_buffers[idx];
  tfd = (wifi_tfh_tfd_t *)wifi_cmd_tfd[idx];
  command_len = sizeof(*frame) + payload_len;
  frame_phys = wifi_phys_addr(frame);

  if (!frame_phys) {
    return -1;
  }

  memset(wifi_cmd_buffers[idx], 0, WIFI_CMD_BUFFER_BYTES);
  memset(tfd, 0, sizeof(*tfd));

  frame->cmd = cmd_id;
  frame->group_id = group_id;
  frame->sequence =
      (uint16_t)((WIFI_DQA_CMD_QUEUE << 8) | (idx & 0xffU));
  frame->length = (uint16_t)payload_len;
  frame->reserved = 0;
  frame->version = version;
  memcpy((uint8_t *)frame + sizeof(*frame), payload, payload_len);

  tfd->num_tbs = 1;
  tfd->tbs[0].tb_len = (uint16_t)command_len;
  tfd->tbs[0].addr = frame_phys;
  wifi_cmd_bc_tbl[idx].tfd_offset =
      wifi_gen2_byte_count(command_len, tfd->num_tbs);

  wifi_status_state.scheduler_cmd_id = cmd_id;
  wifi_status_state.scheduler_cmd_group = group_id;
  wifi_status_state.scheduler_cmd_version = version;
  wifi_status_state.scheduler_cmd_len = command_len;
  wifi_status_state.scheduler_cmd_sequence = frame->sequence;
  wifi_status_state.scheduler_cmd_queue = WIFI_DQA_CMD_QUEUE;
  wifi_status_state.scheduler_cmd_index = idx;
  wifi_status_state.scheduler_cmd_tbs = tfd->num_tbs;
  wifi_status_state.scheduler_cmd_tb_len = command_len;
  wifi_status_state.scheduler_wptr_value =
      (WIFI_DQA_CMD_QUEUE << HBUS_TARG_WRPTR_Q_SHIFT) | next_idx;
  wifi_status_state.command_ready = 1;
  wifi_status_state.command_failed = 0;
  wifi_status_state.command_timeout = 0;
  wifi_status_state.command_doorbell_value =
      wifi_status_state.scheduler_wptr_value;
  wifi_status_state.command_bc_entry = wifi_cmd_bc_tbl[idx].tfd_offset;
  wifi_status_state.command_tfd_num_tbs = tfd->num_tbs;
  wifi_status_state.command_tfd_tb0_len = tfd->tbs[0].tb_len;
  wifi_status_state.command_tfd_tb0_addr = tfd->tbs[0].addr;
  wifi_status_state.status = status;
  return 0;
}

static int wifi_prepare_scheduler_command(void) {
  wifi_tx_queue_cfg_cmd_t cfg;

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
  if (!wifi_status_state.cmd_tfd_phys) {
    wifi_scheduler_mark_failure(
        "wifi: scheduler TFD DMA address missing");
    return -1;
  }

  memset(&cfg, 0, sizeof(cfg));
  cfg.sta_id = 0;
  cfg.tid = 0;
  cfg.flags = WIFI_TX_QUEUE_CFG_ENABLE_QUEUE;
  cfg.cb_size = WIFI_CMD_QUEUE_CB_SIZE_VALUE;
  cfg.byte_cnt_addr = wifi_status_state.cmd_bc_phys;
  cfg.tfdq_addr = wifi_status_state.cmd_tfd_phys;

  if (wifi_stage_command_payload(
          WIFI_CMD_SCD_QUEUE_CFG, WIFI_CMD_GROUP_LONG,
          WIFI_CMD_VERSION_TX_QUEUE_CFG, &cfg, sizeof(cfg),
          "wifi: scheduler command frame staged for firmware command queue") !=
      0) {
    wifi_scheduler_mark_failure(
        "wifi: scheduler command staging failed");
    return -1;
  }

  wifi_status_state.scheduler_ready = 1;
  wifi_status_state.scheduler_failed = 0;
  wifi_status_state.scheduler_generation++;
  return 0;
}

static int wifi_prepare_nvm_command(void) {
  wifi_nvm_access_cmd_t cmd;

  memset(&cmd, 0, sizeof(cmd));
  cmd.op_code = WIFI_NVM_READ;
  cmd.target = WIFI_NVM_TARGET_CACHE;
  cmd.type = WIFI_NVM_SECTION_SW;
  cmd.offset = 0;
  cmd.length = WIFI_NVM_DEFAULT_READ_BYTES;

  if (wifi_stage_command_payload(
          WIFI_CMD_NVM_ACCESS, WIFI_CMD_GROUP_LEGACY,
          WIFI_CMD_VERSION_NVM_ACCESS, &cmd, sizeof(cmd),
          "wifi: NVM read command staged for firmware command queue") != 0) {
    wifi_nvm_mark_failure("wifi: NVM command staging failed");
    return -1;
  }

  wifi_status_state.nvm_ready = 1;
  wifi_status_state.nvm_failed = 0;
  wifi_status_state.nvm_response_seen = 0;
  wifi_status_state.nvm_generation++;
  wifi_status_state.nvm_op = cmd.op_code;
  wifi_status_state.nvm_target = cmd.target;
  wifi_status_state.nvm_section = cmd.type;
  wifi_status_state.nvm_offset = cmd.offset;
  wifi_status_state.nvm_length = cmd.length;
  wifi_status_state.nvm_resp_offset = 0;
  wifi_status_state.nvm_resp_length = 0;
  wifi_status_state.nvm_resp_type = 0;
  wifi_status_state.nvm_resp_status = 0;
  wifi_status_state.nvm_resp_word0 = 0;
  wifi_status_state.nvm_resp_word1 = 0;
  wifi_status_state.nvm_resp_word2 = 0;
  wifi_status_state.nvm_resp_word3 = 0;
  return 0;
}

static int wifi_prepare_nvm_info_command(void) {
  wifi_nvm_get_info_cmd_t cmd;

  memset(&cmd, 0, sizeof(cmd));

  if (wifi_stage_command_payload(
          WIFI_CMD_NVM_GET_INFO, WIFI_CMD_GROUP_REGULATORY_NVM,
          WIFI_CMD_VERSION_NVM_GET_INFO, &cmd, sizeof(cmd),
          "wifi: NVM_GET_INFO command staged for firmware command queue") != 0) {
    wifi_nvm_info_mark_failure("wifi: NVM_GET_INFO command staging failed");
    return -1;
  }

  wifi_status_state.nvm_info_ready = 1;
  wifi_status_state.nvm_info_failed = 0;
  wifi_status_state.nvm_info_response_seen = 0;
  wifi_status_state.nvm_info_generation++;
  wifi_status_state.nvm_info_flags = 0;
  wifi_status_state.nvm_info_version = 0;
  wifi_status_state.nvm_info_board_type = 0;
  wifi_status_state.nvm_info_hw_addrs = 0;
  wifi_status_state.nvm_info_mac_sku_flags = 0;
  wifi_status_state.nvm_info_tx_chains = 0;
  wifi_status_state.nvm_info_rx_chains = 0;
  wifi_status_state.nvm_info_lar_enabled = 0;
  wifi_status_state.nvm_info_n_channels = 0;
  return 0;
}

static uint32_t wifi_scan_build_channel_plan(uint8_t *channels,
                                             uint32_t max_channels) {
  uint32_t count = 0;
  int use_2ghz = 1;
  int use_5ghz = 0;

  if (wifi_status_state.nvm_info_response_seen &&
      !wifi_status_state.nvm_info_failed) {
    use_2ghz =
        (wifi_status_state.nvm_info_mac_sku_flags & WIFI_NVM_MAC_SKU_2GHZ) != 0;
    use_5ghz =
        (wifi_status_state.nvm_info_mac_sku_flags & WIFI_NVM_MAC_SKU_5GHZ) != 0;
  }

  if (use_2ghz && count + 3U <= max_channels) {
    channels[count++] = 1;
    channels[count++] = 6;
    channels[count++] = 11;
  }
  if (use_5ghz && count + 4U <= max_channels) {
    channels[count++] = 36;
    channels[count++] = 40;
    channels[count++] = 44;
    channels[count++] = 48;
  }
  if (count == 0 && max_channels > 0) {
    channels[count++] = 1;
  }
  return count;
}

static int wifi_prepare_scan_command(void) {
  wifi_scan_req_umac_v1_t *cmd;
  wifi_scan_channel_cfg_umac_t *cfg;
  wifi_scan_req_umac_tail_v1_t *tail;
  uint8_t channels[WIFI_SCAN_MAX_CHANNELS];
  uint32_t channel_count;
  uint32_t payload_len;

  channel_count = wifi_scan_build_channel_plan(channels, WIFI_SCAN_MAX_CHANNELS);
  payload_len = (uint32_t)sizeof(wifi_scan_req_umac_v1_t) +
                channel_count * (uint32_t)sizeof(wifi_scan_channel_cfg_umac_t) +
                (uint32_t)sizeof(wifi_scan_req_umac_tail_v1_t);
  if (channel_count == 0 || payload_len > sizeof(wifi_scan_payload)) {
    wifi_scan_mark_failure("wifi: scan channel plan does not fit command buffer");
    return -1;
  }

  memset(wifi_scan_payload, 0, sizeof(wifi_scan_payload));
  cmd = (wifi_scan_req_umac_v1_t *)wifi_scan_payload;
  cfg = (wifi_scan_channel_cfg_umac_t *)(wifi_scan_payload + sizeof(*cmd));
  tail = (wifi_scan_req_umac_tail_v1_t *)((uint8_t *)cfg +
         channel_count * sizeof(*cfg));

  cmd->flags = WIFI_SCAN_UMAC_FLAG_START_NOTIF;
  cmd->uid = (uint32_t)((wifi_status_state.scan_generation + 1U) & 0xffU);
  if (cmd->uid == 0) {
    cmd->uid = 1;
  }
  cmd->ooc_priority = WIFI_SCAN_PRIORITY_EXT_6;
  cmd->general_flags = WIFI_SCAN_GEN_PASS_ALL | WIFI_SCAN_GEN_PASSIVE |
                       WIFI_SCAN_GEN_ITER_COMPLETE;
  cmd->extended_dwell = WIFI_SCAN_DWELL_EXTENDED;
  cmd->active_dwell = WIFI_SCAN_DWELL_ACTIVE;
  cmd->passive_dwell = WIFI_SCAN_DWELL_PASSIVE;
  cmd->fragmented_dwell = WIFI_SCAN_DWELL_FRAGMENTED;
  cmd->max_out_time = 0;
  cmd->suspend_time = 0;
  cmd->scan_priority = WIFI_SCAN_PRIORITY_EXT_6;
  cmd->channel.flags = WIFI_SCAN_CHANNEL_FLAG_ENABLE_ORDER;
  cmd->channel.count = (uint8_t)channel_count;

  for (uint32_t i = 0; i < channel_count; i++) {
    cfg[i].flags = 0;
    cfg[i].channel_num = channels[i];
    cfg[i].iter_count = 1;
    cfg[i].iter_interval = 0;
  }
  tail->schedule[0].interval = 0;
  tail->schedule[0].iter_count = 1;
  tail->delay = 0;

  if (wifi_stage_command_payload(
          WIFI_CMD_SCAN_REQ_UMAC, WIFI_CMD_GROUP_LONG,
          WIFI_CMD_VERSION_SCAN_REQ_UMAC, wifi_scan_payload, payload_len,
          "wifi: passive UMAC scan request staged") != 0) {
    wifi_scan_mark_failure("wifi: passive UMAC scan command staging failed");
    return -1;
  }

  wifi_status_state.scan_ready = 1;
  wifi_status_state.scan_failed = 0;
  wifi_status_state.scan_response_seen = 0;
  wifi_status_state.scan_start_seen = 0;
  wifi_status_state.scan_iter_seen = 0;
  wifi_status_state.scan_complete_seen = 0;
  wifi_status_state.scan_inflight = 0;
  wifi_status_state.scan_notifications = 0;
  wifi_status_state.scan_generation++;
  wifi_status_state.scan_uid = cmd->uid;
  wifi_status_state.scan_cmd_len = payload_len;
  wifi_status_state.scan_version = WIFI_CMD_VERSION_SCAN_REQ_UMAC;
  wifi_status_state.scan_flags = cmd->flags;
  wifi_status_state.scan_general_flags = cmd->general_flags;
  wifi_status_state.scan_channel_count = channel_count;
  wifi_status_state.scan_channel_first = channels[0];
  wifi_status_state.scan_channel_last = channels[channel_count - 1U];
  wifi_status_state.scan_dwell_active = cmd->active_dwell;
  wifi_status_state.scan_dwell_passive = cmd->passive_dwell;
  wifi_status_state.scan_start_uid = 0;
  wifi_status_state.scan_iter_uid = 0;
  wifi_status_state.scan_iter_channels = 0;
  wifi_status_state.scan_iter_status = 0;
  wifi_status_state.scan_iter_bt_status = 0;
  wifi_status_state.scan_iter_last_channel = 0;
  wifi_status_state.scan_iter_tsf_low = 0;
  wifi_status_state.scan_iter_tsf_high = 0;
  wifi_status_state.scan_complete_uid = 0;
  wifi_status_state.scan_complete_last_schedule = 0;
  wifi_status_state.scan_complete_last_iter = 0;
  wifi_status_state.scan_complete_status = 0;
  wifi_status_state.scan_complete_ebs_status = 0;
  wifi_status_state.scan_complete_elapsed = 0;
  wifi_status_state.scan_result_truncated = 0;
  wifi_status_state.scan_result_count = 0;
  wifi_status_state.scan_result_reported = 0;
  wifi_status_state.scan_result_entries = 0;
  wifi_status_state.scan_result_bytes = 0;
  memset(wifi_status_state.scan_result_channel, 0,
         sizeof(wifi_status_state.scan_result_channel));
  memset(wifi_status_state.scan_result_band, 0,
         sizeof(wifi_status_state.scan_result_band));
  memset(wifi_status_state.scan_result_probe_status, 0,
         sizeof(wifi_status_state.scan_result_probe_status));
  memset(wifi_status_state.scan_result_probe_not_sent, 0,
         sizeof(wifi_status_state.scan_result_probe_not_sent));
  memset(wifi_status_state.scan_result_duration, 0,
         sizeof(wifi_status_state.scan_result_duration));
  wifi_scan_clear_access_points();
  wifi_connect_clear_plan();
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
  uint32_t payload_len;
  uint32_t payload_available;
  const uint8_t *payload;
  int specialized_status = 0;

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
  wifi_status_state.rx_last_word0 = ((const uint32_t *)pkt)[0];
  wifi_status_state.rx_last_word1 = ((const uint32_t *)pkt)[1];
  wifi_status_state.rx_last_word2 = ((const uint32_t *)pkt)[2];
  wifi_status_state.rx_last_word3 = ((const uint32_t *)pkt)[3];
  wifi_status_state.rx_last_cd_word0 = ((const uint32_t *)cd)[0];
  wifi_status_state.rx_last_cd_word1 = ((const uint32_t *)cd)[1];
  wifi_status_state.rx_last_cd_word2 = ((const uint32_t *)cd)[2];
  wifi_status_state.rx_last_cd_word3 = ((const uint32_t *)cd)[3];
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

  payload_len = 0;
  if (wifi_status_state.rx_last_len > sizeof(wifi_cmd_header_t)) {
    payload_len = wifi_status_state.rx_last_len -
                  (uint32_t)sizeof(wifi_cmd_header_t);
  }
  payload_available = WIFI_RX_BUFFER_BYTES -
                      (uint32_t)sizeof(wifi_rx_packet_t);
  if (payload_len > payload_available) {
    payload_len = payload_available;
  }
  payload = ((const uint8_t *)pkt) + sizeof(wifi_rx_packet_t);

  if (pkt->header.cmd == WIFI_CMD_NVM_ACCESS &&
      payload_len >= sizeof(wifi_nvm_access_resp_t)) {
    const wifi_nvm_access_resp_t *resp =
        (const wifi_nvm_access_resp_t *)payload;
    const uint8_t *data = payload + sizeof(*resp);
    uint32_t data_len = payload_len - (uint32_t)sizeof(*resp);

    wifi_status_state.nvm_response_seen = 1;
    wifi_status_state.nvm_ready = 0;
    wifi_status_state.nvm_failed = resp->status ? 1 : 0;
    wifi_status_state.nvm_resp_offset = resp->offset;
    wifi_status_state.nvm_resp_length = resp->length;
    wifi_status_state.nvm_resp_type = resp->type;
    wifi_status_state.nvm_resp_status = resp->status;
    wifi_status_state.nvm_resp_word0 = data_len >= 4U ? wifi_read_le32(data) : 0;
    wifi_status_state.nvm_resp_word1 =
        data_len >= 8U ? wifi_read_le32(data + 4U) : 0;
    wifi_status_state.nvm_resp_word2 =
        data_len >= 12U ? wifi_read_le32(data + 8U) : 0;
    wifi_status_state.nvm_resp_word3 =
        data_len >= 16U ? wifi_read_le32(data + 12U) : 0;
    wifi_status_state.status = resp->status
                                   ? "wifi: NVM firmware response reported error"
                                   : "wifi: NVM firmware response parsed";
    specialized_status = 1;
    if (resp->status) {
      wifi_status_state.nvm_errors++;
    }
  }

  if (pkt->header.cmd == WIFI_CMD_NVM_GET_INFO &&
      pkt->header.group_id == WIFI_CMD_GROUP_REGULATORY_NVM) {
    wifi_status_state.nvm_info_response_seen = 1;
    wifi_status_state.nvm_info_ready = 0;
    wifi_status_state.nvm_info_failed = payload_len < 24U ? 1 : 0;
    if (payload_len >= 24U) {
      wifi_status_state.nvm_info_flags = wifi_read_le32(payload);
      wifi_status_state.nvm_info_version = wifi_read_le16(payload + 4U);
      wifi_status_state.nvm_info_board_type = payload[6];
      wifi_status_state.nvm_info_hw_addrs = payload[7];
      wifi_status_state.nvm_info_mac_sku_flags = wifi_read_le32(payload + 8U);
      wifi_status_state.nvm_info_tx_chains = wifi_read_le32(payload + 12U);
      wifi_status_state.nvm_info_rx_chains = wifi_read_le32(payload + 16U);
      wifi_status_state.nvm_info_lar_enabled = wifi_read_le32(payload + 20U);
      wifi_status_state.nvm_info_n_channels =
          payload_len >= 28U ? wifi_read_le32(payload + 24U) : 0;
      wifi_status_state.status = "wifi: NVM_GET_INFO response parsed";
    } else {
      wifi_status_state.nvm_info_errors++;
      wifi_status_state.status = "wifi: NVM_GET_INFO response too short";
    }
    specialized_status = 1;
  }

  if (pkt->header.cmd == WIFI_CMD_REPLY_RX_MPDU &&
      wifi_is_scan_notification_group(pkt->header.group_id)) {
    if (wifi_scan_parse_rx_mpdu(payload, payload_len)) {
      wifi_status_state.status =
          "wifi: 802.11 beacon/probe response SSID parsed";
    } else {
      wifi_status_state.status =
          "wifi: RX MPDU observed; no beacon/probe SSID parsed yet";
    }
    specialized_status = 1;
  }

  if (pkt->header.cmd == WIFI_CMD_SCAN_START_NOTIFICATION_UMAC &&
      wifi_is_scan_notification_group(pkt->header.group_id)) {
    if (payload_len >= sizeof(wifi_umac_scan_start_t)) {
      wifi_status_state.scan_start_seen = 1;
      wifi_status_state.scan_inflight = 1;
      wifi_status_state.scan_ready = 0;
      wifi_status_state.scan_failed = 0;
      wifi_status_state.scan_notifications++;
      wifi_status_state.scan_start_uid = wifi_read_le32(payload);
      wifi_status_state.status = "wifi: UMAC scan start notification parsed";
    } else {
      wifi_scan_mark_failure("wifi: UMAC scan start notification too short");
    }
    specialized_status = 1;
  }

  if (pkt->header.cmd == WIFI_CMD_SCAN_ITERATION_COMPLETE_UMAC &&
      wifi_is_scan_notification_group(pkt->header.group_id)) {
    if (payload_len >= sizeof(wifi_umac_scan_iter_complete_t)) {
      wifi_status_state.scan_iter_seen = 1;
      wifi_status_state.scan_inflight = 1;
      wifi_status_state.scan_ready = 0;
      wifi_status_state.scan_failed = 0;
      wifi_status_state.scan_notifications++;
      wifi_status_state.scan_iter_uid = wifi_read_le32(payload);
      wifi_status_state.scan_iter_channels = payload[4];
      wifi_status_state.scan_iter_status = payload[5];
      wifi_status_state.scan_iter_bt_status = payload[6];
      wifi_status_state.scan_iter_last_channel = payload[7];
      wifi_status_state.scan_iter_tsf_low = wifi_read_le32(payload + 8U);
      wifi_status_state.scan_iter_tsf_high = wifi_read_le32(payload + 12U);
      if (wifi_scan_parse_channel_results(payload, payload_len)) {
        wifi_status_state.scan_errors++;
        wifi_status_state.status =
            "wifi: UMAC scan iteration parsed with truncated channel results";
      } else {
        wifi_status_state.status =
            "wifi: UMAC scan iteration notification parsed";
      }
    } else {
      wifi_scan_mark_failure(
          "wifi: UMAC scan iteration notification too short");
    }
    specialized_status = 1;
  }

  if (pkt->header.cmd == WIFI_CMD_SCAN_COMPLETE_UMAC &&
      wifi_is_scan_notification_group(pkt->header.group_id)) {
    if (payload_len >= sizeof(wifi_umac_scan_complete_t)) {
      wifi_status_state.scan_complete_seen = 1;
      wifi_status_state.scan_inflight = 0;
      wifi_status_state.scan_ready = 0;
      wifi_status_state.scan_failed = 0;
      wifi_status_state.scan_notifications++;
      wifi_status_state.scan_complete_uid = wifi_read_le32(payload);
      wifi_status_state.scan_complete_last_schedule = payload[4];
      wifi_status_state.scan_complete_last_iter = payload[5];
      wifi_status_state.scan_complete_status = payload[6];
      wifi_status_state.scan_complete_ebs_status = payload[7];
      wifi_status_state.scan_complete_elapsed = wifi_read_le32(payload + 8U);
      wifi_status_state.status = "wifi: UMAC scan complete notification parsed";
    } else {
      wifi_scan_mark_failure("wifi: UMAC scan complete notification too short");
    }
    specialized_status = 1;
  }

  if (pkt->header.cmd == WIFI_CMD_SCAN_REQ_UMAC &&
      pkt->header.group_id == WIFI_CMD_GROUP_LONG) {
    wifi_status_state.scan_response_seen = 1;
    wifi_status_state.scan_ready = 0;
    wifi_status_state.scan_inflight = 1;
    wifi_status_state.scan_failed = 0;
    wifi_status_state.status = "wifi: UMAC scan command response observed";
    specialized_status = 1;
  }

  wifi_status_state.rx_read_ptr =
      (wifi_status_state.rx_read_ptr + 1U) & (WIFI_RX_QUEUE_ENTRIES - 1U);
  wifi_status_state.rx_packets++;
  wifi_status_state.rx_path_ready = 1;
  wifi_status_state.rx_path_failed = 0;
  if (!specialized_status) {
    wifi_status_state.status = "wifi: RX firmware response parsed";
  }
  return 1;
}

static void wifi_connect_append_response_status(char *report,
                                                size_t report_size);

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
           "response-match=%s\n"
           "raw: pkt=%08x %08x %08x %08x cd=%08x %08x %08x %08x\n",
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
               : "no",
           s->rx_last_word0, s->rx_last_word1, s->rx_last_word2,
           s->rx_last_word3, s->rx_last_cd_word0, s->rx_last_cd_word1,
           s->rx_last_cd_word2, s->rx_last_cd_word3);
  wifi_connect_append_response_status(report, report_size);
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

  rc = s->command_ready ? 0 : wifi_prepare_scheduler_command();
  s = &wifi_status_state;
  if (rc != 0) {
    snprintf(report, report_size,
             "wifi command: command staging failed\n"
             "command=%s scheduler=%s command-errors=%lu\n",
             s->command_ready ? "ready" : "not-ready",
             s->scheduler_ready ? "ready" : "not-ready", s->command_errors);
    return -1;
  }

  if (wifi_mmio) {
    wifi_status_state.command_before_csr_int = wifi_csr_read32(CSR_INT);
    wifi_status_state.command_before_fh_int =
        wifi_csr_read32(CSR_FH_INT_STATUS);
  }
  wifi_status_state.command_before_closed_rb = wifi_rx_closed_status();
  wifi_status_state.command_before_read_ptr = wifi_status_state.rx_read_ptr;
  if (!arm) {
    s = &wifi_status_state;
    snprintf(report, report_size,
             "wifi command: ready, not sent\n"
             "doorbell: CSR[0x%03x]=0x%08x queue=%u index=%u seq=0x%04x\n"
             "cmd-buf: tfd-tbs=%u tb0-len=%u tb0=0x%lx bc=0x%04x\n"
             "before: csr-int=0x%08x fh-int=0x%08x closed=%u read=%u\n"
             "rx: ready=%s packets=%lu raw=%08x %08x %08x %08x\n"
             "run: wifi command arm to ring the command queue doorbell\n",
             HBUS_TARG_WRPTR, s->command_doorbell_value,
             s->scheduler_cmd_queue, s->scheduler_cmd_index,
             s->scheduler_cmd_sequence, s->command_tfd_num_tbs,
             s->command_tfd_tb0_len,
             (unsigned long)s->command_tfd_tb0_addr, s->command_bc_entry,
             s->command_before_csr_int, s->command_before_fh_int,
             s->command_before_closed_rb, s->command_before_read_ptr,
             s->rx_path_ready ? "yes" : "no", s->rx_packets,
             s->rx_last_word0, s->rx_last_word1, s->rx_last_word2,
             s->rx_last_word3);
    return 0;
  }

  if (!s->context_armed || !s->command_ready || !s->rx_path_ready) {
    wifi_command_mark_failure(
        "wifi: command doorbell needs armed context, staged command, and RX path");
    s = &wifi_status_state;
    snprintf(report, report_size,
             "wifi command: refused to ring doorbell\n"
             "context=%s command=%s rx=%s errors=%lu\n"
             "run: wifi context arm, stage a command, wifi rx, then "
             "wifi command arm\n",
             s->context_armed ? "armed" : "not-armed",
             s->command_ready ? "ready" : "not-ready",
             s->rx_path_ready ? "ready" : "not-ready", s->command_errors);
    return -1;
  }

  wifi_status_state.command_attempts++;
  wifi_status_state.command_response_seen = 0;
  wifi_status_state.command_timeout = 0;
  wifi_status_state.command_doorbell_value =
      wifi_status_state.scheduler_wptr_value;
  packets_before = wifi_status_state.rx_packets;
  wifi_csr_write32(CSR_INT, CSR_INT_BIT_SCD | CSR_INT_BIT_HW_ERR |
                                CSR_INT_BIT_SW_ERR);
  wifi_csr_write32(HBUS_TARG_WRPTR, wifi_status_state.command_doorbell_value);
  wifi_status_state.command_wptr_readback = wifi_csr_read32(HBUS_TARG_WRPTR);
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
      wifi_status_state.command_response_seen = 1;
      break;
    }

    if (csr_int & CSR_INT_BIT_SCD) {
      wifi_csr_write32(CSR_INT, CSR_INT_BIT_SCD);
    }
    __asm__ volatile("pause");
  }

  wifi_status_state.command_after_csr_int = wifi_csr_read32(CSR_INT);
  wifi_status_state.command_after_fh_int = wifi_csr_read32(CSR_FH_INT_STATUS);
  wifi_status_state.command_after_closed_rb = wifi_rx_closed_status();
  wifi_status_state.command_after_read_ptr = wifi_status_state.rx_read_ptr;
  if (!response_seen && !wifi_status_state.command_failed) {
    wifi_status_state.command_timeout = 1;
    wifi_status_state.command_failed = 1;
    wifi_status_state.command_errors++;
    wifi_status_state.status = "wifi: command response timed out";
  }

  s = &wifi_status_state;
  snprintf(report, report_size,
           "wifi command: %s\n"
           "doorbell: CSR[0x%03x]=0x%08x readback=0x%08x attempts=%lu "
           "sent=%s failed=%s timeout=%s\n"
           "cmd: id=0x%02x group=0x%02x seq=0x%04x queue=%u index=%u "
           "write=%u tfd-tbs=%u tb0-len=%u bc=0x%04x\n"
           "before: csr=0x%08x fh=0x%08x closed=%u read=%u\n"
           "after: csr=0x%08x fh=0x%08x closed=%u read=%u\n"
           "poll: loops=%u last-csr=0x%08x last-fh=0x%08x closed=%u "
           "response=%s\n"
           "rx-last: rbid=%u cmd=0x%02x group=0x%02x seq=0x%04x "
           "len=%u match=%s errors=%lu\n"
           "rx-raw: pkt=%08x %08x %08x %08x cd=%08x %08x %08x %08x\n",
           response_seen ? "response observed"
                         : (s->command_failed ? "failed/timeout"
                                             : "doorbell sent"),
           HBUS_TARG_WRPTR, s->command_doorbell_value,
           s->command_wptr_readback, s->command_attempts,
           s->command_sent ? "yes" : "no",
           s->command_failed ? "yes" : "no",
           s->command_timeout ? "yes" : "no", s->scheduler_cmd_id,
           s->scheduler_cmd_group, s->scheduler_cmd_sequence,
           s->scheduler_cmd_queue, s->scheduler_cmd_index, s->cmd_write_ptr,
           s->command_tfd_num_tbs, s->command_tfd_tb0_len,
           s->command_bc_entry, s->command_before_csr_int,
           s->command_before_fh_int, s->command_before_closed_rb,
           s->command_before_read_ptr, s->command_after_csr_int,
           s->command_after_fh_int, s->command_after_closed_rb,
           s->command_after_read_ptr,
           s->command_poll_loops, s->command_last_csr_int,
           s->command_last_fh_int, s->command_last_closed_rb,
           response_seen ? "yes" : "no", s->rx_last_rbid,
           s->rx_last_cmd, s->rx_last_group, s->rx_last_sequence,
           s->rx_last_len,
           (s->rx_last_sequence == s->scheduler_cmd_sequence &&
            s->rx_last_sequence != 0)
               ? "yes"
               : "no",
           s->command_errors, s->rx_last_word0, s->rx_last_word1,
           s->rx_last_word2, s->rx_last_word3, s->rx_last_cd_word0,
           s->rx_last_cd_word1, s->rx_last_cd_word2, s->rx_last_cd_word3);
  return s->command_failed ? -1 : 0;
}

int wifi_nvm_probe(int arm, char *report, size_t report_size) {
  const wifi_status_t *s;
  int rc;

  if (!report || report_size == 0) {
    return -1;
  }

  wifi_init();
  s = &wifi_status_state;

  if (!s->present) {
    snprintf(report, report_size,
             "wifi nvm: no PCI wireless controller detected\n");
    return -1;
  }

  if (s->vendor_id != 0x8086) {
    snprintf(report, report_size,
             "wifi nvm: unsupported controller %04x:%04x\n",
             s->vendor_id, s->device_id);
    return -1;
  }

  rc = wifi_prepare_nvm_command();
  s = &wifi_status_state;
  if (rc != 0) {
    snprintf(report, report_size,
             "wifi nvm: command staging failed\n"
             "queues=%s context=%s command=%s errors=%lu\n"
             "run: wifi queues arm, wifi context arm, wifi rx, then wifi nvm arm\n",
             s->queues_ready ? (s->queues_armed ? "armed" : "staged")
                             : "idle",
             s->context_ready ? (s->context_armed ? "armed" : "staged")
                              : "idle",
             s->command_ready ? "ready" : "not-ready", s->nvm_errors);
    return -1;
  }

  if (arm) {
    rc = wifi_command_probe(1, report, report_size);
    if (rc != 0 && !wifi_status_state.nvm_response_seen) {
      wifi_status_state.nvm_ready = 0;
      wifi_status_state.nvm_failed = 1;
    }
  } else {
    rc = 0;
  }

  s = &wifi_status_state;
  snprintf(report, report_size,
           "wifi nvm: %s\n"
           "request: op=%u target=%u section=%u offset=%u len=%u "
           "generation=%lu errors=%lu\n"
           "cmd: id=0x%02x group=0x%02x version=%u seq=0x%04x "
           "queue=%u index=%u doorbell=0x%08x\n"
           "state: context=%s rx=%s command-sent=%s command-failed=%s "
           "response=%s\n"
           "response: offset=%u len=%u type=%u status=%u "
           "data=%08x %08x %08x %08x\n"
           "%s",
           s->nvm_response_seen
               ? (s->nvm_failed ? "firmware response error"
                                : "firmware response parsed")
               : (arm ? (s->command_failed ? "command failed/timeout"
                                           : "command sent")
                      : "read command staged, not sent"),
           s->nvm_op, s->nvm_target, s->nvm_section, s->nvm_offset,
           s->nvm_length, s->nvm_generation, s->nvm_errors,
           s->scheduler_cmd_id, s->scheduler_cmd_group,
           s->scheduler_cmd_version, s->scheduler_cmd_sequence,
           s->scheduler_cmd_queue, s->scheduler_cmd_index,
           s->command_doorbell_value,
           s->context_armed ? "armed" : (s->context_ready ? "staged" : "idle"),
           s->rx_path_ready ? "ready" : "not-ready",
           s->command_sent ? "yes" : "no",
           s->command_failed ? "yes" : "no",
           s->nvm_response_seen ? "yes" : "no", s->nvm_resp_offset,
           s->nvm_resp_length, s->nvm_resp_type, s->nvm_resp_status,
           s->nvm_resp_word0, s->nvm_resp_word1, s->nvm_resp_word2,
           s->nvm_resp_word3,
           arm ? "" : "run: wifi nvm arm to ring the command queue doorbell\n");
  return rc;
}

int wifi_nvm_info_probe(int arm, char *report, size_t report_size) {
  const wifi_status_t *s;
  int rc;

  if (!report || report_size == 0) {
    return -1;
  }

  wifi_init();
  s = &wifi_status_state;

  if (!s->present) {
    snprintf(report, report_size,
             "wifi nvm-info: no PCI wireless controller detected\n");
    return -1;
  }

  if (s->vendor_id != 0x8086) {
    snprintf(report, report_size,
             "wifi nvm-info: unsupported controller %04x:%04x\n",
             s->vendor_id, s->device_id);
    return -1;
  }

  rc = wifi_prepare_nvm_info_command();
  s = &wifi_status_state;
  if (rc != 0) {
    snprintf(report, report_size,
             "wifi nvm-info: command staging failed\n"
             "queues=%s context=%s command=%s errors=%lu\n"
             "run: wifi queues arm, wifi context arm, wifi rx, then wifi nvm-info arm\n",
             s->queues_ready ? (s->queues_armed ? "armed" : "staged")
                             : "idle",
             s->context_ready ? (s->context_armed ? "armed" : "staged")
                              : "idle",
             s->command_ready ? "ready" : "not-ready",
             s->nvm_info_errors);
    return -1;
  }

  if (arm) {
    rc = wifi_command_probe(1, report, report_size);
    if (rc != 0 && !wifi_status_state.nvm_info_response_seen) {
      wifi_status_state.nvm_info_ready = 0;
      wifi_status_state.nvm_info_failed = 1;
    }
  } else {
    rc = 0;
  }

  s = &wifi_status_state;
  snprintf(report, report_size,
           "wifi nvm-info: %s\n"
           "cmd: id=0x%02x group=0x%02x version=%u seq=0x%04x "
           "queue=%u index=%u doorbell=0x%08x generation=%lu errors=%lu\n"
           "general: flags=0x%08x nvm-version=0x%04x board=%u hw-addrs=%u\n"
           "capabilities: sku=0x%08x 2ghz=%s 5ghz=%s 11n=%s 11ac=%s "
           "11ax=%s mimo=%s\n"
           "phy: tx-chains=0x%08x rx-chains=0x%08x lar=%u channels=%u\n"
           "state: context=%s rx=%s command-sent=%s command-failed=%s "
           "response=%s\n"
           "%s",
           s->nvm_info_response_seen
               ? (s->nvm_info_failed ? "response parse failed"
                                      : "radio capabilities parsed")
               : (arm ? (s->command_failed ? "command failed/timeout"
                                           : "command sent")
                      : "command staged, not sent"),
           s->scheduler_cmd_id, s->scheduler_cmd_group,
           s->scheduler_cmd_version, s->scheduler_cmd_sequence,
           s->scheduler_cmd_queue, s->scheduler_cmd_index,
           s->command_doorbell_value, s->nvm_info_generation,
           s->nvm_info_errors, s->nvm_info_flags, s->nvm_info_version,
           s->nvm_info_board_type, s->nvm_info_hw_addrs,
           s->nvm_info_mac_sku_flags,
           (s->nvm_info_mac_sku_flags & WIFI_NVM_MAC_SKU_2GHZ) ? "yes" : "no",
           (s->nvm_info_mac_sku_flags & WIFI_NVM_MAC_SKU_5GHZ) ? "yes" : "no",
           (s->nvm_info_mac_sku_flags & WIFI_NVM_MAC_SKU_11N) ? "yes" : "no",
           (s->nvm_info_mac_sku_flags & WIFI_NVM_MAC_SKU_11AC) ? "yes" : "no",
           (s->nvm_info_mac_sku_flags & WIFI_NVM_MAC_SKU_11AX) ? "yes" : "no",
           (s->nvm_info_mac_sku_flags & WIFI_NVM_MAC_SKU_MIMO_DISABLED)
               ? "disabled"
               : "enabled",
           s->nvm_info_tx_chains, s->nvm_info_rx_chains,
           s->nvm_info_lar_enabled, s->nvm_info_n_channels,
           s->context_armed ? "armed" : (s->context_ready ? "staged" : "idle"),
           s->rx_path_ready ? "ready" : "not-ready",
           s->command_sent ? "yes" : "no",
           s->command_failed ? "yes" : "no",
           s->nvm_info_response_seen ? "yes" : "no",
           arm ? "" : "run: wifi nvm-info arm to ring the command queue doorbell\n");
  return (rc != 0 || s->nvm_info_failed) ? -1 : 0;
}

static void wifi_report_append(char *report, size_t report_size,
                               const char *line) {
  size_t len;

  if (!report || report_size == 0 || !line) {
    return;
  }

  len = strlen(report);
  if (len >= report_size - 1U) {
    return;
  }

  snprintf(report + len, report_size - len, "%s", line);
}

static void wifi_report_step(char *report, size_t report_size,
                             const char *name, int rc) {
  const wifi_status_t *s = &wifi_status_state;
  char line[192];

  snprintf(line, sizeof(line), "[%s] %s - %s\n", rc == 0 ? " ok " : "fail",
           name, s->status ? s->status : "no status");
  wifi_report_append(report, report_size, line);
}

static void wifi_scan_append_channel_results(char *report, size_t report_size) {
  const wifi_status_t *s = &wifi_status_state;
  char line[192];

  snprintf(line, sizeof(line),
           "results: reported=%u entries=%u stored=%u bytes=%u truncated=%s\n",
           s->scan_result_reported, s->scan_result_entries,
           s->scan_result_count, s->scan_result_bytes,
           s->scan_result_truncated ? "yes" : "no");
  wifi_report_append(report, report_size, line);

  if (s->scan_result_count == 0) {
    wifi_report_append(report, report_size,
                       "result[0]: none yet; wait for scan iteration notify\n");
    return;
  }

  for (uint32_t i = 0; i < s->scan_result_count; i++) {
    snprintf(line, sizeof(line),
             "result[%u]: channel=%u band=%s(%u) probe=%u not-sent=%u "
             "duration-us=%u\n",
             i, s->scan_result_channel[i],
             wifi_scan_band_text(s->scan_result_band[i]),
             s->scan_result_band[i], s->scan_result_probe_status[i],
             s->scan_result_probe_not_sent[i], s->scan_result_duration[i]);
    wifi_report_append(report, report_size, line);
  }
}

static const char *wifi_scan_frame_subtype_text(uint32_t subtype) {
  switch (subtype) {
  case WIFI_80211_SUBTYPE_ASSOC_RESP:
    return "assoc-response";
  case WIFI_80211_SUBTYPE_AUTH:
    return "auth";
  case WIFI_80211_SUBTYPE_BEACON:
    return "beacon";
  case WIFI_80211_SUBTYPE_PROBE_RESP:
    return "probe-response";
  default:
    return "unknown";
  }
}

static const char *wifi_scan_security_text(uint32_t security) {
  switch (security) {
  case WIFI_SCAN_SECURITY_OPEN:
    return "open";
  case WIFI_SCAN_SECURITY_WEP:
    return "wep";
  case WIFI_SCAN_SECURITY_WPA:
    return "wpa";
  case WIFI_SCAN_SECURITY_WPA2:
    return "wpa2-rsn";
  default:
    return "unknown";
  }
}

static void wifi_connect_append_response_status(char *report,
                                                size_t report_size) {
  const wifi_status_t *s = &wifi_status_state;
  char line[224];

  if (!s->connect_ready && s->connect_response_packets == 0) {
    return;
  }

  snprintf(line, sizeof(line),
           "connect-rx: packets=%lu auth=%s assoc=%s failed=%s "
           "last-subtype=%u/%s auth-status=%u assoc-status=%u aid=%u\n",
           s->connect_response_packets,
           s->connect_auth_response_seen ? "seen" : "none",
           s->connect_assoc_response_seen ? "seen" : "none",
           s->connect_response_failed ? "yes" : "no",
           s->connect_last_rx_subtype,
           wifi_scan_frame_subtype_text(s->connect_last_rx_subtype),
           s->connect_auth_status, s->connect_assoc_status,
           s->connect_assoc_aid);
  wifi_report_append(report, report_size, line);

  if (s->wpa_eapol_packets || s->wpa_anonce_seen) {
    snprintf(line, sizeof(line),
             "wpa-rx: eapol=%lu key=%lu m1=%s anonce=%s "
             "desc=%u ver=%u key-info=0x%04x key-len=%u data-len=%u\n",
             s->wpa_eapol_packets, s->wpa_key_frames,
             s->wpa_m1_seen ? "seen" : "none",
             s->wpa_anonce_seen ? "seen" : "none",
             s->wpa_key_desc_type, s->wpa_eapol_version, s->wpa_key_info,
             s->wpa_key_len, s->wpa_key_data_len);
    wifi_report_append(report, report_size, line);
    snprintf(line, sizeof(line),
             "wpa-rx: replay=0x%08x%08x anonce-check=0x%08x "
             "snonce-check=0x%08x ptk=%s m2=%s data=%s\n",
             s->wpa_replay_counter_hi, s->wpa_replay_counter_lo,
             s->wpa_anonce_checksum, s->wpa_snonce_checksum,
             s->wpa_ptk_ready ? "ready" : "pending",
             s->wpa_m2_ready ? "ready" : "pending",
             s->wpa_m2_data_ready ? "ready" : "pending");
    wifi_report_append(report, report_size, line);
  }
}

static void wifi_scan_append_access_points(char *report, size_t report_size) {
  const wifi_status_t *s = &wifi_status_state;
  char line[224];

  snprintf(line, sizeof(line),
           "aps: count=%u overflow=%u mpdu=%lu mgmt=%lu beacon=%lu "
           "probe-resp=%lu ssid-updates=%lu\n",
           s->scan_ap_count, s->scan_ap_overflow, s->scan_mpdu_packets,
           s->scan_mgmt_frames, s->scan_beacon_frames,
           s->scan_probe_resp_frames, s->scan_ssid_updates);
  wifi_report_append(report, report_size, line);
  snprintf(line, sizeof(line),
           "last-frame: fc=0x%04x subtype=%u/%s offset=%u len=%u "
           "mpdu-len=%u channel=%u\n",
           s->scan_last_frame_control, s->scan_last_frame_subtype,
           wifi_scan_frame_subtype_text(s->scan_last_frame_subtype),
           s->scan_last_frame_offset, s->scan_last_frame_len,
           s->scan_last_mpdu_len, s->scan_last_frame_channel);
  wifi_report_append(report, report_size, line);
  snprintf(line, sizeof(line),
           "mpdu-debug: payload-len=%u parse-misses=%lu raw-len=%u "
           "candidates=%u\n",
           s->scan_last_mpdu_payload_len, s->scan_mpdu_parse_misses,
           s->scan_last_debug_len, s->scan_candidate_count);
  wifi_report_append(report, report_size, line);
  if (s->scan_last_debug_len > 0) {
    wifi_report_append(report, report_size, "mpdu-raw:");
    for (uint32_t i = 0; i < s->scan_last_debug_len; i++) {
      char byte_text[8];
      snprintf(byte_text, sizeof(byte_text), " %02x",
               s->scan_last_debug_bytes[i]);
      wifi_report_append(report, report_size, byte_text);
    }
    wifi_report_append(report, report_size, "\n");
  }
  for (uint32_t i = 0; i < s->scan_candidate_count; i++) {
    snprintf(line, sizeof(line),
             "candidate[%u]: offset=%u len=%u fc=0x%04x type=%u subtype=%u/%s\n",
             i, s->scan_candidate_offset[i], s->scan_candidate_len[i],
             s->scan_candidate_fc[i], s->scan_candidate_type[i],
             s->scan_candidate_subtype[i],
             wifi_scan_frame_subtype_text(s->scan_candidate_subtype[i]));
    wifi_report_append(report, report_size, line);
  }

  if (s->scan_ap_count == 0) {
    wifi_report_append(report, report_size,
                       "ap[0]: none yet; wait for beacon/probe RX packets\n");
    return;
  }

  for (uint32_t i = 0; i < s->scan_ap_count; i++) {
    const char *ssid = s->scan_ap_ssid_len[i] ? s->scan_ap_ssid[i] : "<hidden>";
    snprintf(line, sizeof(line),
             "ap[%u]: ssid=\"%s\" bssid=%02x:%02x:%02x:%02x:%02x:%02x "
             "channel=%u security=%s cap=0x%04x source=%s seen=%u\n",
             i, ssid, s->scan_ap_bssid[i][0], s->scan_ap_bssid[i][1],
             s->scan_ap_bssid[i][2], s->scan_ap_bssid[i][3],
             s->scan_ap_bssid[i][4], s->scan_ap_bssid[i][5],
             s->scan_ap_channel[i],
             wifi_scan_security_text(s->scan_ap_security[i]),
             s->scan_ap_capability[i],
             wifi_scan_frame_subtype_text(s->scan_ap_frame_subtype[i]),
             s->scan_ap_seen_count[i]);
    wifi_report_append(report, report_size, line);
  }
}

int wifi_bringup_probe(char *report, size_t report_size) {
  char scratch[256];
  const wifi_status_t *s;
  int rc;

  if (!report || report_size == 0) {
    return -1;
  }

  report[0] = '\0';
  wifi_report_append(report, report_size, "wifi bringup: Lenovo/Intel CNVi path\n");

  rc = wifi_firmware_probe(scratch, sizeof(scratch));
  wifi_report_step(report, report_size, "firmware", rc);
  if (rc != 0) {
    goto done;
  }

  rc = wifi_apm_probe(scratch, sizeof(scratch));
  wifi_report_step(report, report_size, "apm", rc);
  if (rc != 0) {
    goto done;
  }

  rc = wifi_boot_firmware(1, scratch, sizeof(scratch));
  wifi_report_step(report, report_size, "boot", rc);
  if (rc != 0) {
    goto done;
  }

  rc = wifi_alive_probe(scratch, sizeof(scratch));
  wifi_report_step(report, report_size, "alive", rc);
  if (rc != 0) {
    goto done;
  }

  rc = wifi_queue_probe(1, scratch, sizeof(scratch));
  wifi_report_step(report, report_size, "queues", rc);
  if (rc != 0) {
    goto done;
  }

  rc = wifi_context_probe(1, scratch, sizeof(scratch));
  wifi_report_step(report, report_size, "context", rc);
  if (rc != 0) {
    goto done;
  }

  rc = wifi_scheduler_probe(1, scratch, sizeof(scratch));
  wifi_report_step(report, report_size, "scheduler", rc);
  if (rc != 0) {
    goto done;
  }

  rc = wifi_rx_probe(1, scratch, sizeof(scratch));
  wifi_report_step(report, report_size, "rx", rc);
  if (rc != 0) {
    goto done;
  }

  rc = wifi_nvm_info_probe(1, scratch, sizeof(scratch));
  wifi_report_step(report, report_size, "nvm-info", rc);

done:
  s = &wifi_status_state;
  {
    char line[384];
    snprintf(line, sizeof(line),
             "summary: chipset=%s pci=%04x:%04x firmware=%s source=%s "
             "boot=%s alive=%s queues=%s context=%s rx=%s nvm-info=%s\n"
             "radio: sku=0x%08x tx=0x%08x rx=0x%08x channels=%u status=%s\n",
             s->chipset, s->vendor_id, s->device_id,
             s->firmware_present ? s->firmware_name : "missing",
             s->firmware_source, s->boot_ready ? "ready" : "not-ready",
             s->alive_seen ? "seen" : "not-seen",
             s->queues_ready ? "ready" : "not-ready",
             s->context_armed ? "armed" : "not-armed",
             s->rx_path_ready ? "ready" : "not-ready",
             s->nvm_info_response_seen
                 ? (s->nvm_info_failed ? "failed" : "response")
                 : "none",
             s->nvm_info_mac_sku_flags, s->nvm_info_tx_chains,
             s->nvm_info_rx_chains, s->nvm_info_n_channels,
             s->status ? s->status : "no status");
    wifi_report_append(report, report_size, line);
  }
  return rc;
}

int wifi_scan(int arm, char *report, size_t report_size) {
  const wifi_status_t *s;
  uint8_t channels[WIFI_SCAN_MAX_CHANNELS];
  uint32_t channel_count;
  int rc = 0;

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

  channel_count = wifi_scan_build_channel_plan(channels, WIFI_SCAN_MAX_CHANNELS);

  if (!arm) {
    snprintf(report, report_size,
             "wifi scan: passive UMAC plan ready\n"
             "hardware: %s at %02x:%02x.%u pci=%04x:%04x\n"
             "firmware: %s (%lu bytes, valid=%s source=%s)\n"
             "radio: nvm-info=%s sku=0x%08x tx=0x%08x rx=0x%08x "
             "reported-channels=%u\n"
             "plan: version=%u channels=%u first=%u last=%u "
             "dwell-active=%u dwell-passive=%u mode=passive\n"
             "state: boot=%s alive=%s context=%s rx=%s\n"
             "run: wifi bringup, then wifi scan arm for the experimental "
             "firmware scan request\n",
             s->chipset, s->bus, s->device, (unsigned int)s->function,
             s->vendor_id, s->device_id, s->firmware_name,
             (unsigned long)s->firmware_size,
             s->firmware_valid ? "yes" : "no", s->firmware_source,
             s->nvm_info_response_seen ? "response" : "missing",
             s->nvm_info_mac_sku_flags, s->nvm_info_tx_chains,
             s->nvm_info_rx_chains, s->nvm_info_n_channels,
             WIFI_CMD_VERSION_SCAN_REQ_UMAC, channel_count,
             channel_count ? channels[0] : 0,
             channel_count ? channels[channel_count - 1U] : 0,
             WIFI_SCAN_DWELL_ACTIVE, WIFI_SCAN_DWELL_PASSIVE,
             s->boot_ready ? "ready" : "not-ready",
             s->alive_seen ? "seen" : "not-seen",
             s->context_armed ? "armed" : "not-armed",
             s->rx_path_ready ? "ready" : "not-ready");
    return 0;
  }

  if (!s->context_armed || !s->rx_path_ready) {
    snprintf(report, report_size,
             "wifi scan: not ready to arm scan command\n"
             "state: boot=%s alive=%s queues=%s context=%s rx=%s nvm-info=%s\n"
             "run: wifi bringup first, then wifi scan arm\n",
             s->boot_ready ? "ready" : "not-ready",
             s->alive_seen ? "seen" : "not-seen",
             s->queues_ready ? "ready" : "not-ready",
             s->context_armed ? "armed" : "not-armed",
             s->rx_path_ready ? "ready" : "not-ready",
             s->nvm_info_response_seen ? "response" : "missing");
    return -1;
  }

  rc = wifi_prepare_scan_command();
  s = &wifi_status_state;
  if (rc != 0) {
    snprintf(report, report_size,
             "wifi scan: command staging failed\n"
             "channels=%u errors=%lu status=%s\n",
             channel_count, s->scan_errors, s->status);
    return -1;
  }

  rc = wifi_command_probe(1, report, report_size);
  s = &wifi_status_state;
  snprintf(report, report_size,
           "wifi scan: %s\n"
           "cmd: id=0x%02x group=0x%02x version=%u uid=%u len=%u "
           "seq=0x%04x queue=%u index=%u\n"
           "plan: channels=%u first=%u last=%u flags=0x%08x "
           "general=0x%08x dwell=%u/%u\n"
           "state: sent=%s response=%s start=%s iter=%s complete=%s "
           "inflight=%s failed=%s timeout=%s notifications=%lu errors=%lu\n"
           "iter: uid=%u channels=%u status=%u bt=%u last-channel=%u "
           "tsf=0x%08x%08x\n"
           "complete: uid=%u status=%u/%s ebs=%u/%s elapsed=%u\n"
           "note: passive UMAC scan request sent; SSID parsing now watches "
           "RX_MPDU beacon/probe-response frames\n"
           "next: run wifi scan poll until complete=yes\n",
           s->scan_response_seen
               ? "firmware accepted/answered scan command"
               : (s->command_failed ? "scan command failed/timeout"
                                    : "scan command sent"),
           s->scheduler_cmd_id, s->scheduler_cmd_group,
           s->scheduler_cmd_version, s->scan_uid, s->scan_cmd_len,
           s->scheduler_cmd_sequence, s->scheduler_cmd_queue,
           s->scheduler_cmd_index, s->scan_channel_count,
           s->scan_channel_first, s->scan_channel_last, s->scan_flags,
           s->scan_general_flags, s->scan_dwell_active,
           s->scan_dwell_passive, s->command_sent ? "yes" : "no",
           s->scan_response_seen ? "yes" : "no",
           s->scan_start_seen ? "yes" : "no",
           s->scan_iter_seen ? "yes" : "no",
           s->scan_complete_seen ? "yes" : "no",
           s->scan_inflight ? "yes" : "no",
           s->command_failed ? "yes" : "no",
           s->command_timeout ? "yes" : "no", s->scan_notifications,
           s->scan_errors, s->scan_iter_uid, s->scan_iter_channels,
           s->scan_iter_status, s->scan_iter_bt_status,
           s->scan_iter_last_channel, s->scan_iter_tsf_high,
           s->scan_iter_tsf_low, s->scan_complete_uid,
           s->scan_complete_status,
           wifi_scan_complete_status_text(s->scan_complete_status),
           s->scan_complete_ebs_status,
           wifi_scan_ebs_status_text(s->scan_complete_ebs_status),
           s->scan_complete_elapsed);
  wifi_scan_append_channel_results(report, report_size);
  wifi_scan_append_access_points(report, report_size);
  wifi_connect_append_response_status(report, report_size);
  return rc;
}

int wifi_scan_poll(char *report, size_t report_size) {
  const wifi_status_t *s;
  unsigned long notifications_before;
  unsigned long packets_before;
  unsigned long ssids_before;
  int complete_before;
  uint32_t poll_limit;
  uint32_t loops = 0;
  int parsed = 0;
  int observed = 0;

  if (!report || report_size == 0) {
    return -1;
  }

  wifi_init();
  s = &wifi_status_state;

  if (!s->present) {
    snprintf(report, report_size,
             "wifi scan poll: no wireless controller detected\n");
    return -1;
  }

  if (!s->rx_path_ready) {
    snprintf(report, report_size,
             "wifi scan poll: RX path is not ready\n"
             "state: boot=%s alive=%s queues=%s context=%s rx=%s\n"
             "run: wifi bringup, wifi scan arm, then wifi scan poll\n",
             s->boot_ready ? "ready" : "not-ready",
             s->alive_seen ? "seen" : "not-seen",
             s->queues_ready ? "ready" : "not-ready",
             s->context_armed ? "armed" : "not-armed",
             s->rx_path_ready ? "ready" : "not-ready");
    return -1;
  }

  notifications_before = s->scan_notifications;
  packets_before = s->rx_packets;
  ssids_before = s->scan_ssid_updates;
  complete_before = s->scan_complete_seen;
  poll_limit = complete_before ? WIFI_SCAN_DRAIN_LOOPS : WIFI_SCAN_POLL_LOOPS;

  for (uint32_t i = 0; i < poll_limit; i++) {
    int rc;
    loops = i + 1U;
    wifi_status_state.rx_polls++;
    rc = wifi_rx_parse_one();
    if (rc > 0) {
      parsed++;
      if (wifi_status_state.scan_notifications != notifications_before ||
          wifi_status_state.scan_ssid_updates != ssids_before ||
          (!complete_before && wifi_status_state.scan_complete_seen)) {
        observed = 1;
        break;
      }
    }
    if (rc < 0) {
      break;
    }
    __asm__ volatile("pause");
  }

  s = &wifi_status_state;
  snprintf(report, report_size,
           "wifi scan poll: %s\n"
           "state: uid=%u sent=%s response=%s start=%s iter=%s complete=%s "
           "inflight=%s notifications=%lu aps=%u ssid-updates=%lu "
           "failed=%s errors=%lu\n"
           "poll: loops=%u parsed=%d packets-before=%lu packets-after=%lu "
           "notifications-before=%lu ssids-before=%lu\n"
           "start: uid=%u\n"
           "iter: uid=%u channels=%u status=%u bt=%u last-channel=%u "
           "tsf=0x%08x%08x\n"
           "complete: uid=%u schedule=%u iter=%u status=%u/%s ebs=%u/%s "
           "elapsed=%u\n"
           "rx-last: cmd=0x%02x group=0x%02x seq=0x%04x len=%u\n"
           "next: repeat wifi scan poll to drain more beacon/probe RX frames\n",
           observed ? "scan/AP update observed"
                    : "no new scan notification in this poll window",
           s->scan_uid, s->command_sent ? "yes" : "no",
           s->scan_response_seen ? "yes" : "no",
           s->scan_start_seen ? "yes" : "no",
           s->scan_iter_seen ? "yes" : "no",
           s->scan_complete_seen ? "yes" : "no",
           s->scan_inflight ? "yes" : "no", s->scan_notifications,
           s->scan_ap_count, s->scan_ssid_updates,
           s->scan_failed ? "yes" : "no", s->scan_errors, loops, parsed,
           packets_before, s->rx_packets, notifications_before, ssids_before,
           s->scan_start_uid, s->scan_iter_uid, s->scan_iter_channels,
           s->scan_iter_status, s->scan_iter_bt_status,
           s->scan_iter_last_channel, s->scan_iter_tsf_high,
           s->scan_iter_tsf_low, s->scan_complete_uid,
           s->scan_complete_last_schedule, s->scan_complete_last_iter,
           s->scan_complete_status,
           wifi_scan_complete_status_text(s->scan_complete_status),
           s->scan_complete_ebs_status,
           wifi_scan_ebs_status_text(s->scan_complete_ebs_status),
           s->scan_complete_elapsed, s->rx_last_cmd, s->rx_last_group,
           s->rx_last_sequence, s->rx_last_len);
  wifi_scan_append_channel_results(report, report_size);
  wifi_scan_append_access_points(report, report_size);
  wifi_connect_append_response_status(report, report_size);
  return s->scan_failed ? -1 : 0;
}

int wifi_crypto_probe(char *report, size_t report_size) {
  int rc;

  if (!report || report_size == 0) {
    return -1;
  }

  rc = sha1_selftest();
  snprintf(report, report_size,
           "wifi crypto: %s\n"
           "sha1: %s\n"
           "pbkdf2-hmac-sha1: %s\n"
           "wpa2: pmk-bytes=%u iterations=%u passphrase-len=%u..%u "
           "hex-psk-len=%u\n"
           "note: connect reports only PMK checksum, never the key\n",
           rc == 0 ? "self-test passed" : "self-test failed",
           rc == 0 || rc == -2 || rc == -3 ? "ok" : "failed",
           rc == 0 ? "ok" : "failed",
           WIFI_WPA2_PMK_BYTES, WIFI_WPA2_PBKDF2_ITERATIONS,
           WIFI_WPA2_MIN_PASSPHRASE, WIFI_WPA2_MAX_PASSPHRASE,
           WIFI_WPA2_HEX_PSK_CHARS);
  return rc;
}

static void wifi_connect_copy_mac(uint8_t *dst, const uint8_t *src) {
  for (uint32_t i = 0; i < 6U; i++) {
    dst[i] = src[i];
  }
}

static void wifi_connect_make_local_mac(uint8_t *mac) {
  mac[0] = 0x02U;
  mac[1] = 0x4fU;
  mac[2] = 0x5aU;
  mac[3] = (uint8_t)(wifi_status_state.vendor_id & 0xffU);
  mac[4] = (uint8_t)(wifi_status_state.device_id & 0xffU);
  mac[5] = (uint8_t)(wifi_status_state.scan_generation ?
                         wifi_status_state.scan_generation : 1U);
}

static int wifi_connect_ssid_matches(const char *target,
                                     const char *candidate,
                                     uint32_t candidate_len) {
  size_t target_len;

  if (!target || !candidate || candidate_len == 0 ||
      candidate_len > WIFI_SCAN_SSID_MAX) {
    return 0;
  }

  target_len = strlen(target);
  if (target_len != (size_t)candidate_len) {
    return 0;
  }
  return memcmp(target, candidate, candidate_len) == 0;
}

static int wifi_connect_find_ap(const char *ssid, uint32_t *index) {
  const wifi_status_t *s = &wifi_status_state;

  for (uint32_t i = 0; i < s->scan_ap_count; i++) {
    if (wifi_connect_ssid_matches(ssid, s->scan_ap_ssid[i],
                                  s->scan_ap_ssid_len[i])) {
      if (index) {
        *index = i;
      }
      return 0;
    }
  }
  return -1;
}

static uint32_t wifi_connect_checksum(const uint8_t *data, uint32_t len) {
  uint32_t hash = 2166136261U;

  for (uint32_t i = 0; i < len; i++) {
    hash ^= data[i];
    hash *= 16777619U;
  }
  return hash ? hash : 1U;
}

static int wifi_connect_hex_value(char c) {
  if (c >= '0' && c <= '9') {
    return c - '0';
  }
  if (c >= 'a' && c <= 'f') {
    return c - 'a' + 10;
  }
  if (c >= 'A' && c <= 'F') {
    return c - 'A' + 10;
  }
  return -1;
}

static int wifi_connect_parse_hex_pmk(const char *password) {
  for (uint32_t i = 0; i < WIFI_WPA2_PMK_BYTES; i++) {
    int hi = wifi_connect_hex_value(password[i * 2U]);
    int lo = wifi_connect_hex_value(password[i * 2U + 1U]);
    if (hi < 0 || lo < 0) {
      memset(wifi_connect_pmk, 0, sizeof(wifi_connect_pmk));
      return -1;
    }
    wifi_connect_pmk[i] = (uint8_t)((hi << 4) | lo);
  }
  return 0;
}

static int wifi_connect_prepare_wpa2_pmk(const char *password,
                                         uint32_t ssid_len,
                                         char *reason,
                                         size_t reason_size) {
  size_t password_len;

  if (!password) {
    if (reason && reason_size > 0) {
      snprintf(reason, reason_size, "missing password");
    }
    return -1;
  }

  password_len = strlen(password);
  if (password_len == WIFI_WPA2_HEX_PSK_CHARS) {
    if (wifi_connect_parse_hex_pmk(password) != 0) {
      if (reason && reason_size > 0) {
        snprintf(reason, reason_size,
                 "64-character WPA2 PSK must be hexadecimal");
      }
      return -1;
    }
    wifi_status_state.connect_pmk_ready = 1;
    wifi_status_state.connect_password_len = (uint32_t)password_len;
    wifi_status_state.connect_pmk_iterations = 0;
    wifi_status_state.connect_pmk_checksum =
        wifi_connect_checksum(wifi_connect_pmk, sizeof(wifi_connect_pmk));
    return 0;
  }

  if (password_len < WIFI_WPA2_MIN_PASSPHRASE ||
      password_len > WIFI_WPA2_MAX_PASSPHRASE) {
    if (reason && reason_size > 0) {
      snprintf(reason, reason_size, "WPA2 passphrase length must be %u..%u",
               WIFI_WPA2_MIN_PASSPHRASE, WIFI_WPA2_MAX_PASSPHRASE);
    }
    return -1;
  }

  if (pbkdf2_hmac_sha1(password, password_len,
                       wifi_status_state.connect_ssid, ssid_len,
                       WIFI_WPA2_PBKDF2_ITERATIONS, wifi_connect_pmk,
                       sizeof(wifi_connect_pmk)) != 0) {
    if (reason && reason_size > 0) {
      snprintf(reason, reason_size, "PBKDF2-HMAC-SHA1 failed");
    }
    return -1;
  }

  wifi_status_state.connect_pmk_ready = 1;
  wifi_status_state.connect_password_len = (uint32_t)password_len;
  wifi_status_state.connect_pmk_iterations = WIFI_WPA2_PBKDF2_ITERATIONS;
  wifi_status_state.connect_pmk_checksum =
      wifi_connect_checksum(wifi_connect_pmk, sizeof(wifi_connect_pmk));
  return 0;
}

static void wifi_connect_write_mgmt_header(uint8_t *frame, uint16_t fc,
                                           const uint8_t *bssid,
                                           const uint8_t *local_mac) {
  wifi_write_le16(frame + 0U, fc);
  wifi_write_le16(frame + 2U, 0);
  wifi_connect_copy_mac(frame + 4U, bssid);
  wifi_connect_copy_mac(frame + 10U, local_mac);
  wifi_connect_copy_mac(frame + 16U, bssid);
  wifi_write_le16(frame + 22U, 0);
}

static int wifi_connect_add_ie(uint8_t *frame, uint32_t capacity,
                               uint32_t *offset, uint8_t id,
                               const uint8_t *data, uint32_t len) {
  uint32_t pos;

  if (!frame || !offset || *offset + 2U + len > capacity || len > 255U) {
    return -1;
  }

  pos = *offset;
  frame[pos++] = id;
  frame[pos++] = (uint8_t)len;
  if (len > 0 && data) {
    memcpy(frame + pos, data, len);
  }
  *offset = pos + len;
  return 0;
}

static int wifi_connect_build_auth_frame(const uint8_t *bssid,
                                         const uint8_t *local_mac) {
  uint16_t fc =
      (uint16_t)(WIFI_80211_SUBTYPE_AUTH << 4);
  uint32_t offset = WIFI_80211_MGMT_HEADER_BYTES;

  memset(wifi_connect_auth_frame, 0, sizeof(wifi_connect_auth_frame));
  wifi_connect_write_mgmt_header(wifi_connect_auth_frame, fc, bssid, local_mac);
  wifi_write_le16(wifi_connect_auth_frame + offset, 0);
  offset += 2U;
  wifi_write_le16(wifi_connect_auth_frame + offset, 1);
  offset += 2U;
  wifi_write_le16(wifi_connect_auth_frame + offset, 0);
  offset += 2U;

  wifi_status_state.connect_auth_fc = fc;
  wifi_status_state.connect_auth_frame_len = offset;
  return 0;
}

static int wifi_connect_build_assoc_frame(const char *ssid, uint32_t ssid_len,
                                          int use_wpa, const uint8_t *bssid,
                                          const uint8_t *local_mac) {
  static const uint8_t rates_24ghz[] = {
      0x82U, 0x84U, 0x8bU, 0x96U, 0x0cU, 0x12U, 0x18U, 0x24U};
  static const uint8_t rates_5ghz[] = {
      0x8cU, 0x12U, 0x98U, 0x24U, 0xb0U, 0x48U, 0x60U, 0x6cU};
  static const uint8_t rsn_wpa2_psk_ccmp[] = {
      0x01U, 0x00U, 0x00U, 0x0fU, 0xacU, 0x04U, 0x01U,
      0x00U, 0x00U, 0x0fU, 0xacU, 0x04U, 0x01U, 0x00U,
      0x00U, 0x0fU, 0xacU, 0x02U, 0x00U, 0x00U};
  uint16_t fc =
      (uint16_t)(WIFI_80211_SUBTYPE_ASSOC_REQ << 4);
  uint16_t capability = WIFI_80211_CAP_ESS;
  const uint8_t *rates = rates_24ghz;
  uint32_t rates_len = (uint32_t)sizeof(rates_24ghz);
  uint32_t offset = WIFI_80211_MGMT_HEADER_BYTES;

  if (!ssid || ssid_len == 0 || ssid_len > WIFI_SCAN_SSID_MAX) {
    return -1;
  }

  if (wifi_status_state.connect_channel > 14U) {
    rates = rates_5ghz;
    rates_len = (uint32_t)sizeof(rates_5ghz);
  }
  if (use_wpa) {
    capability |= WIFI_80211_CAP_PRIVACY;
  }

  memset(wifi_connect_assoc_frame, 0, sizeof(wifi_connect_assoc_frame));
  wifi_connect_write_mgmt_header(wifi_connect_assoc_frame, fc, bssid,
                                 local_mac);
  wifi_write_le16(wifi_connect_assoc_frame + offset, capability);
  offset += 2U;
  wifi_write_le16(wifi_connect_assoc_frame + offset,
                  WIFI_80211_LISTEN_INTERVAL);
  offset += 2U;

  if (wifi_connect_add_ie(wifi_connect_assoc_frame,
                          sizeof(wifi_connect_assoc_frame), &offset, 0,
                          (const uint8_t *)ssid, ssid_len) != 0) {
    return -1;
  }
  if (wifi_connect_add_ie(wifi_connect_assoc_frame,
                          sizeof(wifi_connect_assoc_frame), &offset, 1,
                          rates, rates_len) != 0) {
    return -1;
  }
  if (use_wpa &&
      wifi_connect_add_ie(wifi_connect_assoc_frame,
                          sizeof(wifi_connect_assoc_frame), &offset, 48,
                          rsn_wpa2_psk_ccmp,
                          (uint32_t)sizeof(rsn_wpa2_psk_ccmp)) != 0) {
    return -1;
  }

  wifi_status_state.connect_assoc_fc = fc;
  wifi_status_state.connect_assoc_frame_len = offset;
  return 0;
}

static void wifi_connect_append_known_aps(char *report, size_t report_size) {
  const wifi_status_t *s = &wifi_status_state;
  char line[192];
  uint32_t limit = s->scan_ap_count < 8U ? s->scan_ap_count : 8U;

  if (s->scan_ap_count == 0) {
    wifi_report_append(report, report_size,
                       "known-aps: none; run wifi scan arm, then wifi scan poll\n");
    return;
  }

  for (uint32_t i = 0; i < limit; i++) {
    const char *known =
        s->scan_ap_ssid_len[i] ? s->scan_ap_ssid[i] : "<hidden>";
    snprintf(line, sizeof(line),
             "known-ap[%u]: ssid=\"%s\" bssid=%02x:%02x:%02x:%02x:%02x:%02x "
             "channel=%u security=%s seen=%u\n",
             i, known, s->scan_ap_bssid[i][0], s->scan_ap_bssid[i][1],
             s->scan_ap_bssid[i][2], s->scan_ap_bssid[i][3],
             s->scan_ap_bssid[i][4], s->scan_ap_bssid[i][5],
             s->scan_ap_channel[i],
             wifi_scan_security_text(s->scan_ap_security[i]),
             s->scan_ap_seen_count[i]);
    wifi_report_append(report, report_size, line);
  }
  if (s->scan_ap_count > limit) {
    snprintf(line, sizeof(line), "known-aps: +%u more\n",
             s->scan_ap_count - limit);
    wifi_report_append(report, report_size, line);
  }
}

int wifi_wpa_probe(char *report, size_t report_size) {
  const wifi_status_t *s;
  int prepared_now = 0;

  if (!report || report_size == 0) {
    return -1;
  }

  report[0] = '\0';
  wifi_init();
  s = &wifi_status_state;

  if (!s->present) {
    snprintf(report, report_size,
             "wifi wpa: no wireless controller detected\n");
    return -1;
  }

  if (!s->connect_ready) {
    snprintf(report, report_size,
             "wifi wpa: no WPA connection plan yet\n"
             "run: wifi bringup, wifi scan arm, wifi scan poll, "
             "wifi connect <ssid> <password>\n");
    return -1;
  }

  if (!s->connect_wpa) {
    snprintf(report, report_size,
             "wifi wpa: current connection plan is open, no WPA handshake "
             "needed\n"
             "target: ssid=\"%s\"\n",
             s->connect_ssid);
    return 0;
  }

  if (s->wpa_m1_seen && s->connect_pmk_ready &&
      (!s->wpa_ptk_ready || !s->wpa_m2_ready || !s->wpa_m2_data_ready)) {
    if (wifi_wpa_prepare_m2((uint8_t)s->wpa_eapol_version,
                            (uint8_t)s->wpa_key_desc_type) == 0) {
      prepared_now = 1;
    }
  }

  s = &wifi_status_state;
  snprintf(report, report_size,
           "wifi wpa: %s\n"
           "target: ssid=\"%s\" bssid=%02x:%02x:%02x:%02x:%02x:%02x\n"
           "pmk=%s m1=%s anonce=%s ptk=%s m2=%s data-frame=%s\n"
           "rx: eapol=%lu key=%lu desc=%u ver=%u key-info=0x%04x "
           "replay=0x%08x%08x\n"
           "checks: pmk=0x%08x anonce=0x%08x snonce=0x%08x "
           "ptk=0x%08x kck=0x%08x kek=0x%08x tk=0x%08x\n"
           "m2: key-info=0x%04x eapol-len=%u data-frame-len=%u "
           "key-data-len=%u mic-check=0x%08x frame-check=0x%08x\n"
           "note: M2 is prepared only for diagnostics; wifi tx m2 stages it "
           "without ringing TX\n",
           s->wpa_m2_ready
               ? (prepared_now ? "PTK/M2 prepared now" : "PTK/M2 ready")
               : (s->wpa_m1_seen ? "M1 seen, M2 pending"
                                  : "waiting for AP EAPOL M1"),
           s->connect_ssid, s->connect_bssid[0], s->connect_bssid[1],
           s->connect_bssid[2], s->connect_bssid[3], s->connect_bssid[4],
           s->connect_bssid[5],
           s->connect_pmk_ready ? "ready" : "missing",
           s->wpa_m1_seen ? "seen" : "none",
           s->wpa_anonce_seen ? "seen" : "none",
           s->wpa_ptk_ready ? "ready" : "pending",
           s->wpa_m2_ready ? "ready" : "pending",
           s->wpa_m2_data_ready ? "ready" : "pending",
           s->wpa_eapol_packets, s->wpa_key_frames, s->wpa_key_desc_type,
           s->wpa_eapol_version, s->wpa_key_info, s->wpa_replay_counter_hi,
           s->wpa_replay_counter_lo, s->connect_pmk_checksum,
           s->wpa_anonce_checksum, s->wpa_snonce_checksum,
           s->wpa_ptk_checksum, s->wpa_kck_checksum, s->wpa_kek_checksum,
           s->wpa_tk_checksum, s->wpa_m2_key_info, s->wpa_m2_frame_len,
           s->wpa_m2_data_frame_len, s->wpa_m2_key_data_len,
           s->wpa_m2_mic_checksum, s->wpa_m2_checksum);
  return s->wpa_m2_ready ? 0 : -1;
}

static uint32_t wifi_bind_id_and_color(uint32_t id, uint32_t color) {
  return ((id & 0xffU) << WIFI_BIND_CTXT_ID_POS) |
         ((color & 0xffU) << WIFI_BIND_CTXT_COLOR_POS);
}

static void wifi_bind_fill_qos(wifi_bind_ac_qos_diag_t *ac, uint32_t count) {
  uint32_t i;

  if (!ac) {
    return;
  }

  for (i = 0; i < count; i++) {
    ac[i].cw_min = 0x000fU;
    ac[i].cw_max = 0x03ffU;
    ac[i].aifsn = 2U;
    ac[i].fifos_mask = 0U;
    ac[i].edca_txop = 0U;
  }
  if (count > 2U) {
    ac[2].cw_min = 0x0007U;
    ac[2].cw_max = 0x000fU;
  }
  if (count > 3U) {
    ac[3].cw_min = 0x0003U;
    ac[3].cw_max = 0x0007U;
    ac[3].aifsn = 1U;
  }
}

static uint32_t wifi_bind_build_command(uint32_t cmd_id, uint32_t group_id,
                                        uint32_t version,
                                        const void *payload,
                                        uint32_t payload_len,
                                        uint8_t *buffer,
                                        uint32_t buffer_size) {
  wifi_cmd_header_wide_t *cmd;
  uint32_t command_len;

  if (!payload || !buffer || payload_len == 0 ||
      payload_len > 0xffffU) {
    return 0;
  }

  command_len = (uint32_t)sizeof(wifi_cmd_header_wide_t) + payload_len;
  if (command_len > buffer_size) {
    return 0;
  }

  memset(buffer, 0, buffer_size);
  cmd = (wifi_cmd_header_wide_t *)buffer;
  cmd->cmd = (uint8_t)cmd_id;
  cmd->group_id = (uint8_t)group_id;
  cmd->sequence = 0;
  cmd->length = (uint16_t)payload_len;
  cmd->reserved = 0;
  cmd->version = (uint8_t)version;
  memcpy(buffer + sizeof(*cmd), payload, payload_len);
  return command_len;
}

static void wifi_bind_mark_failure(const char *status) {
  wifi_status_state.bind_ready = 0;
  wifi_status_state.bind_failed = 1;
  wifi_status_state.bind_errors++;
  wifi_status_state.status = status;
}

int wifi_bind_probe(char *report, size_t report_size) {
  const wifi_status_t *s;
  wifi_bind_mac_config_diag_t mac_cmd;
  wifi_bind_link_config_diag_t link_cmd;
  wifi_bind_sta_config_diag_t sta_cmd;
  uint32_t mac_id;
  uint32_t color;
  uint32_t link_id;
  uint32_t sta_id;
  uint32_t assoc_aid;
  uint32_t security;
  uint32_t mac_len;
  uint32_t link_len;
  uint32_t sta_len;
  char line[256];

  if (!report || report_size == 0) {
    return -1;
  }

  report[0] = '\0';
  wifi_init();
  s = &wifi_status_state;

  if (!s->present) {
    snprintf(report, report_size,
             "wifi bind: no wireless controller detected\n");
    return -1;
  }

  if (!s->connect_ready) {
    wifi_bind_mark_failure(
        "wifi: MAC/LINK/STA binding needs a connection plan first");
    snprintf(report, report_size,
             "wifi bind: no connection frame plan ready\n"
             "run: wifi bringup, wifi scan arm, wifi scan poll, "
             "wifi connect <ssid> [password]\n");
    return -1;
  }

  if (s->connect_assoc_response_seen && s->connect_assoc_status) {
    wifi_bind_mark_failure(
        "wifi: association response failed, binding stopped");
    snprintf(report, report_size,
             "wifi bind: association failed, not building binding plan\n"
             "assoc-status=%u ssid=\"%s\"\n",
             s->connect_assoc_status, s->connect_ssid);
    return -1;
  }

  mac_id = WIFI_BIND_MAC_ID_CLIENT;
  color = WIFI_BIND_COLOR_CLIENT +
          (wifi_status_state.bind_plans % 0x0fU);
  link_id = WIFI_BIND_LINK_ID_PRIMARY;
  sta_id = WIFI_BIND_STA_ID_AP;
  assoc_aid =
      (s->connect_assoc_response_seen && s->connect_assoc_status == 0)
          ? s->connect_assoc_aid
          : 0U;
  security = s->connect_wpa ? WIFI_SCAN_SECURITY_WPA2
                            : WIFI_SCAN_SECURITY_OPEN;
  if (s->connect_ap_index < WIFI_SCAN_AP_SLOTS) {
    security = s->scan_ap_security[s->connect_ap_index];
  }

  memset(&mac_cmd, 0, sizeof(mac_cmd));
  mac_cmd.id_and_color = wifi_bind_id_and_color(mac_id, color);
  mac_cmd.action = WIFI_BIND_ACTION_ADD;
  mac_cmd.mac_type = WIFI_BIND_FW_MAC_TYPE_BSS_STA;
  mac_cmd.tsf_id = 0;
  wifi_connect_copy_mac(mac_cmd.node_addr, s->connect_local_mac);
  wifi_connect_copy_mac(mac_cmd.bssid_addr, s->connect_bssid);
  mac_cmd.cck_rates = WIFI_BIND_CCK_RATES;
  mac_cmd.ofdm_rates = WIFI_BIND_OFDM_RATES;
  mac_cmd.filter_flags =
      WIFI_BIND_MAC_FILTER_MGMT | WIFI_BIND_MAC_FILTER_GROUP |
      (s->connect_wpa ? WIFI_BIND_MAC_FILTER_DECRYPT_OFF : 0U);
  wifi_bind_fill_qos(mac_cmd.ac, 5U);
  mac_cmd.sta.is_assoc = assoc_aid ? 1U : 0U;
  mac_cmd.sta.bi = 100U;
  mac_cmd.sta.dtim_interval = 1U;
  mac_cmd.sta.listen_interval = WIFI_80211_LISTEN_INTERVAL;
  mac_cmd.sta.assoc_id = assoc_aid;

  memset(&link_cmd, 0, sizeof(link_cmd));
  link_cmd.action = WIFI_BIND_ACTION_ADD;
  link_cmd.link_id = link_id;
  link_cmd.mac_id = mac_id;
  link_cmd.phy_id = WIFI_BIND_PHY_ID_INVALID;
  wifi_connect_copy_mac(link_cmd.local_link_addr, s->connect_local_mac);
  link_cmd.active = assoc_aid ? 1U : 0U;
  link_cmd.cck_rates = WIFI_BIND_CCK_RATES;
  link_cmd.ofdm_rates = WIFI_BIND_OFDM_RATES;
  wifi_bind_fill_qos(link_cmd.ac, 5U);
  link_cmd.bi = 100U;
  link_cmd.dtim_interval = 1U;
  wifi_connect_copy_mac(link_cmd.ref_bssid_addr, s->connect_bssid);
  link_cmd.spec_link_id = (uint8_t)link_id;

  memset(&sta_cmd, 0, sizeof(sta_cmd));
  sta_cmd.sta_id = sta_id;
  sta_cmd.link_id = link_id;
  wifi_connect_copy_mac(sta_cmd.peer_mld_address, s->connect_bssid);
  wifi_connect_copy_mac(sta_cmd.peer_link_address, s->connect_bssid);
  sta_cmd.station_type = WIFI_BIND_STATION_TYPE_PEER;
  sta_cmd.assoc_id = assoc_aid;

  mac_len = wifi_bind_build_command(
      WIFI_CMD_MAC_CONFIG, WIFI_CMD_GROUP_MAC_CONF,
      WIFI_CMD_VERSION_MAC_CONFIG, &mac_cmd, sizeof(mac_cmd),
      wifi_bind_mac_buffer, sizeof(wifi_bind_mac_buffer));
  link_len = wifi_bind_build_command(
      WIFI_CMD_LINK_CONFIG, WIFI_CMD_GROUP_MAC_CONF,
      WIFI_CMD_VERSION_LINK_CONFIG, &link_cmd, sizeof(link_cmd),
      wifi_bind_link_buffer, sizeof(wifi_bind_link_buffer));
  sta_len = wifi_bind_build_command(
      WIFI_CMD_STA_CONFIG, WIFI_CMD_GROUP_MAC_CONF,
      WIFI_CMD_VERSION_STA_CONFIG, &sta_cmd, sizeof(sta_cmd),
      wifi_bind_sta_buffer, sizeof(wifi_bind_sta_buffer));

  if (!mac_len || !link_len || !sta_len) {
    wifi_bind_mark_failure(
        "wifi: MAC/LINK/STA diagnostic binding buffer failed");
    snprintf(report, report_size,
             "wifi bind: diagnostic buffer build failed\n"
             "mac-len=%u link-len=%u sta-len=%u\n",
             mac_len, link_len, sta_len);
    return -1;
  }

  wifi_status_state.bind_ready = 1;
  wifi_status_state.bind_failed = 0;
  wifi_status_state.bind_plans++;
  wifi_status_state.bind_mac_id = mac_id;
  wifi_status_state.bind_color = color;
  wifi_status_state.bind_link_id = link_id;
  wifi_status_state.bind_sta_id = sta_id;
  wifi_status_state.bind_action = WIFI_BIND_ACTION_ADD;
  wifi_status_state.bind_mac_cmd_id = WIFI_CMD_MAC_CONFIG;
  wifi_status_state.bind_mac_group = WIFI_CMD_GROUP_MAC_CONF;
  wifi_status_state.bind_mac_version = WIFI_CMD_VERSION_MAC_CONFIG;
  wifi_status_state.bind_mac_len = mac_len;
  wifi_status_state.bind_mac_checksum =
      wifi_connect_checksum(wifi_bind_mac_buffer, mac_len);
  wifi_status_state.bind_link_cmd_id = WIFI_CMD_LINK_CONFIG;
  wifi_status_state.bind_link_group = WIFI_CMD_GROUP_MAC_CONF;
  wifi_status_state.bind_link_version = WIFI_CMD_VERSION_LINK_CONFIG;
  wifi_status_state.bind_link_len = link_len;
  wifi_status_state.bind_link_checksum =
      wifi_connect_checksum(wifi_bind_link_buffer, link_len);
  wifi_status_state.bind_sta_cmd_id = WIFI_CMD_STA_CONFIG;
  wifi_status_state.bind_sta_group = WIFI_CMD_GROUP_MAC_CONF;
  wifi_status_state.bind_sta_version = WIFI_CMD_VERSION_STA_CONFIG;
  wifi_status_state.bind_sta_len = sta_len;
  wifi_status_state.bind_sta_checksum =
      wifi_connect_checksum(wifi_bind_sta_buffer, sta_len);
  wifi_status_state.bind_assoc_aid = assoc_aid;
  wifi_status_state.bind_security = security;
  wifi_status_state.bind_channel = s->connect_channel;
  wifi_status_state.status =
      "wifi: MAC/LINK/STA diagnostic binding plan built; not queued";

  snprintf(line, sizeof(line),
           "wifi bind: MAC/LINK/STA diagnostic plan ready\n");
  wifi_report_append(report, report_size, line);
  snprintf(line, sizeof(line),
           "target: ssid=\"%s\" bssid=%02x:%02x:%02x:%02x:%02x:%02x "
           "security=%s channel=%u\n",
           s->connect_ssid, s->connect_bssid[0], s->connect_bssid[1],
           s->connect_bssid[2], s->connect_bssid[3],
           s->connect_bssid[4], s->connect_bssid[5],
           wifi_scan_security_text(security), s->connect_channel);
  wifi_report_append(report, report_size, line);
  snprintf(line, sizeof(line),
           "ids: mac=%u color=%u link=%u sta=%u action=add assoc-aid=%u\n",
           wifi_status_state.bind_mac_id, wifi_status_state.bind_color,
           wifi_status_state.bind_link_id, wifi_status_state.bind_sta_id,
           wifi_status_state.bind_assoc_aid);
  wifi_report_append(report, report_size, line);
  snprintf(line, sizeof(line),
           "mac-cmd: id=0x%02x group=0x%02x ver=%u len=%u "
           "checksum=0x%08x\n",
           wifi_status_state.bind_mac_cmd_id,
           wifi_status_state.bind_mac_group,
           wifi_status_state.bind_mac_version,
           wifi_status_state.bind_mac_len,
           wifi_status_state.bind_mac_checksum);
  wifi_report_append(report, report_size, line);
  snprintf(line, sizeof(line),
           "link-cmd: id=0x%02x group=0x%02x ver=%u len=%u "
           "checksum=0x%08x\n",
           wifi_status_state.bind_link_cmd_id,
           wifi_status_state.bind_link_group,
           wifi_status_state.bind_link_version,
           wifi_status_state.bind_link_len,
           wifi_status_state.bind_link_checksum);
  wifi_report_append(report, report_size, line);
  snprintf(line, sizeof(line),
           "sta-cmd: id=0x%02x group=0x%02x ver=%u len=%u "
           "checksum=0x%08x plans=%lu\n",
           wifi_status_state.bind_sta_cmd_id,
           wifi_status_state.bind_sta_group,
           wifi_status_state.bind_sta_version,
           wifi_status_state.bind_sta_len,
           wifi_status_state.bind_sta_checksum,
           wifi_status_state.bind_plans);
  wifi_report_append(report, report_size, line);
  wifi_report_append(report, report_size,
                     "safety: diagnostic buffers only; not copied to "
                     "command queue and doorbell not armed\n");
  wifi_report_append(report, report_size,
                     "next: guard firmware ACKs for these commands, then "
                     "queue TX_CMD with the bound station context\n");
  return 0;
}

static uint32_t wifi_txcmd_header_len(const uint8_t *frame,
                                      uint32_t frame_len) {
  uint16_t fc;
  uint32_t type;
  uint32_t subtype;
  uint32_t hdr_len;

  if (!frame || frame_len < WIFI_80211_MGMT_HEADER_BYTES) {
    return 0;
  }

  fc = wifi_read_le16(frame);
  type = (fc & WIFI_80211_FC_TYPE_MASK) >> 2;
  subtype = (fc & WIFI_80211_FC_SUBTYPE_MASK) >> 4;
  hdr_len = type == WIFI_80211_TYPE_DATA
                ? wifi_80211_data_header_len(fc, subtype)
                : WIFI_80211_MGMT_HEADER_BYTES;
  return hdr_len <= frame_len ? hdr_len : 0;
}

static uint32_t wifi_txcmd_offload_assist(uint32_t hdr_len) {
  uint32_t assist = (hdr_len / 2U) << WIFI_TX_CMD_OFFLD_MH_SIZE_SHIFT;

  if (hdr_len % 4U) {
    assist |= WIFI_TX_CMD_OFFLD_PAD;
  }
  return assist;
}

static int wifi_txcmd_select_frame(const char *target, uint32_t *kind,
                                   const uint8_t **frame,
                                   uint32_t *frame_len) {
  if (!target || target[0] == '\0') {
    target = "assoc";
  }

  if (strcmp(target, "auth") == 0) {
    *kind = WIFI_TX_STAGE_KIND_AUTH;
    *frame = wifi_connect_auth_frame;
    *frame_len = wifi_status_state.connect_auth_frame_len;
    return 0;
  }
  if (strcmp(target, "assoc") == 0) {
    *kind = WIFI_TX_STAGE_KIND_ASSOC;
    *frame = wifi_connect_assoc_frame;
    *frame_len = wifi_status_state.connect_assoc_frame_len;
    return 0;
  }
  if (strcmp(target, "m2") == 0) {
    if (!wifi_status_state.wpa_m2_data_ready &&
        wifi_status_state.wpa_m1_seen &&
        wifi_status_state.connect_pmk_ready) {
      wifi_wpa_prepare_m2((uint8_t)wifi_status_state.wpa_eapol_version,
                          (uint8_t)wifi_status_state.wpa_key_desc_type);
    }
    *kind = WIFI_TX_STAGE_KIND_EAPOL_M2;
    *frame = wifi_wpa_m2_data_frame;
    *frame_len = wifi_status_state.wpa_m2_data_frame_len;
    return wifi_status_state.wpa_m2_data_ready ? 0 : -1;
  }
  return -1;
}

static int wifi_txcmd_build_plan(uint32_t kind, const uint8_t *frame,
                                 uint32_t frame_len) {
  wifi_cmd_header_wide_t *cmd;
  wifi_tx_cmd_v10_t *tx;
  uint32_t hdr_len;
  uint32_t payload_len;
  uint32_t command_len;
  uint32_t flags;
  uint32_t sta_id;

  if (!wifi_status_state.connect_ready || !frame ||
      frame_len < WIFI_80211_MGMT_HEADER_BYTES || frame_len > 2342U) {
    wifi_status_state.txcmd_ready = 0;
    wifi_status_state.txcmd_failed = 1;
    wifi_status_state.txcmd_errors++;
    wifi_status_state.status = "wifi: TX_CMD plan needs a valid frame plan";
    return -1;
  }

  hdr_len = wifi_txcmd_header_len(frame, frame_len);
  payload_len = (uint32_t)sizeof(wifi_tx_cmd_v10_t) + frame_len;
  command_len = (uint32_t)sizeof(wifi_cmd_header_wide_t) + payload_len;
  if (!hdr_len || command_len > sizeof(wifi_txcmd_plan_buffer)) {
    wifi_status_state.txcmd_ready = 0;
    wifi_status_state.txcmd_failed = 1;
    wifi_status_state.txcmd_errors++;
    wifi_status_state.status = "wifi: TX_CMD diagnostic buffer is too small";
    return -1;
  }

  memset(wifi_txcmd_plan_buffer, 0, sizeof(wifi_txcmd_plan_buffer));
  cmd = (wifi_cmd_header_wide_t *)wifi_txcmd_plan_buffer;
  tx = (wifi_tx_cmd_v10_t *)(wifi_txcmd_plan_buffer + sizeof(*cmd));

  cmd->cmd = WIFI_CMD_TX;
  cmd->group_id = WIFI_CMD_GROUP_LEGACY;
  cmd->sequence = 0;
  cmd->length = (uint16_t)payload_len;
  cmd->reserved = 0;
  cmd->version = WIFI_CMD_VERSION_TX_API_V10;

  flags = WIFI_TX_CMD_FLAG_HIGH_PRI | WIFI_TX_CMD_FLAG_ENCRYPT_DIS;
  sta_id = wifi_status_state.bind_ready ? wifi_status_state.bind_sta_id
                                        : WIFI_TX_CMD_STA_ID_UNBOUND;
  tx->len = (uint16_t)frame_len;
  tx->flags = (uint16_t)flags;
  tx->offload_assist = wifi_txcmd_offload_assist(hdr_len);
  tx->rate_n_flags = 0;
  memcpy((uint8_t *)tx + sizeof(*tx), frame, frame_len);

  wifi_status_state.txcmd_ready = 1;
  wifi_status_state.txcmd_failed = 0;
  wifi_status_state.txcmd_plans++;
  wifi_status_state.txcmd_kind = kind;
  wifi_status_state.txcmd_api_version = WIFI_CMD_VERSION_TX_API_V10;
  wifi_status_state.txcmd_id = WIFI_CMD_TX;
  wifi_status_state.txcmd_group = WIFI_CMD_GROUP_LEGACY;
  wifi_status_state.txcmd_sequence = 0;
  wifi_status_state.txcmd_payload_len = payload_len;
  wifi_status_state.txcmd_command_len = command_len;
  wifi_status_state.txcmd_frame_len = frame_len;
  wifi_status_state.txcmd_header_len = hdr_len;
  wifi_status_state.txcmd_flags = flags;
  wifi_status_state.txcmd_offload_assist = tx->offload_assist;
  wifi_status_state.txcmd_rate_n_flags = tx->rate_n_flags;
  wifi_status_state.txcmd_sta_id = sta_id;
  wifi_status_state.txcmd_checksum =
      wifi_connect_checksum(wifi_txcmd_plan_buffer, command_len);
  wifi_status_state.status =
      "wifi: Intel TX_CMD v10 diagnostic plan built; not queued";
  return 0;
}

int wifi_txcmd_probe(const char *target, char *report, size_t report_size) {
  const wifi_status_t *s;
  const uint8_t *frame = NULL;
  uint32_t frame_len = 0;
  uint32_t kind = 0;
  int rc;

  if (!report || report_size == 0) {
    return -1;
  }

  report[0] = '\0';
  wifi_init();
  s = &wifi_status_state;

  if (!s->present) {
    snprintf(report, report_size,
             "wifi txcmd: no wireless controller detected\n");
    return -1;
  }

  if (!s->connect_ready) {
    snprintf(report, report_size,
             "wifi txcmd: no connection frame plan ready\n"
             "run: wifi bringup, wifi scan arm, wifi scan poll, "
             "wifi connect <ssid> [password]\n");
    return -1;
  }

  rc = wifi_txcmd_select_frame(target, &kind, &frame, &frame_len);
  if (rc != 0) {
    wifi_status_state.txcmd_ready = 0;
    wifi_status_state.txcmd_failed = 1;
    wifi_status_state.txcmd_errors++;
    snprintf(report, report_size,
             "wifi txcmd: usage wifi txcmd [auth|assoc|m2]\n"
             "m2 requires: association response + WPA EAPOL M1 parsed by "
             "wifi rx poll\n");
    return -1;
  }

  rc = wifi_txcmd_build_plan(kind, frame, frame_len);
  s = &wifi_status_state;
  snprintf(report, report_size,
           "wifi txcmd: %s\n"
           "target: %s ssid=\"%s\" bssid=%02x:%02x:%02x:%02x:%02x:%02x\n"
           "cmd: id=0x%02x group=0x%02x api=%u payload-len=%u "
           "command-len=%u plans=%lu checksum=0x%08x\n"
           "frame: kind=%s len=%u hdr-len=%u sta-id=%u bound=%s\n"
           "flags: tx=0x%04x offload=0x%08x rate=0x%08x "
           "encrypt-disabled=yes high-priority=yes\n"
           "safety: diagnostic buffer only; not copied to command queue and "
           "doorbell not armed\n"
           "next: bind MAC/STA context, then allow guarded TX_CMD queueing\n",
           rc == 0 ? "Intel TX_CMD v10 plan ready" : "Intel TX_CMD plan failed",
           (!target || target[0] == '\0') ? "assoc" : target,
           s->connect_ssid, s->connect_bssid[0], s->connect_bssid[1],
           s->connect_bssid[2], s->connect_bssid[3], s->connect_bssid[4],
           s->connect_bssid[5], s->txcmd_id, s->txcmd_group,
           s->txcmd_api_version, s->txcmd_payload_len,
           s->txcmd_command_len, s->txcmd_plans, s->txcmd_checksum,
           wifi_tx_stage_kind_text(s->txcmd_kind), s->txcmd_frame_len,
           s->txcmd_header_len, s->txcmd_sta_id,
           s->txcmd_sta_id != WIFI_TX_CMD_STA_ID_UNBOUND ? "yes" : "no",
           s->txcmd_flags,
           s->txcmd_offload_assist, s->txcmd_rate_n_flags);
  return rc;
}

static const char *wifi_tx_stage_kind_text(uint32_t kind) {
  switch (kind) {
  case WIFI_TX_STAGE_KIND_AUTH:
    return "auth";
  case WIFI_TX_STAGE_KIND_ASSOC:
    return "assoc";
  case WIFI_TX_STAGE_KIND_EAPOL_M2:
    return "m2";
  default:
    return "none";
  }
}

static int wifi_tx_stage_frame(uint32_t kind, const uint8_t *frame,
                               uint32_t frame_len) {
  uint32_t idx;
  uint32_t next_idx;
  uint64_t frame_phys;
  wifi_tfh_tfd_t *tfd;

  if (!wifi_status_state.connect_ready || !frame || frame_len == 0 ||
      frame_len > WIFI_TX_BUFFER_BYTES) {
    wifi_status_state.tx_stage_failed = 1;
    wifi_status_state.tx_stage_errors++;
    wifi_status_state.status = "wifi: TX staging needs a connection frame plan";
    return -1;
  }

  if (!wifi_status_state.queues_ready || !wifi_status_state.tx_tfd_phys ||
      !wifi_status_state.tx_buffer_phys || !wifi_status_state.tx_bc_phys) {
    wifi_status_state.tx_stage_failed = 1;
    wifi_status_state.tx_stage_errors++;
    wifi_status_state.status = "wifi: TX staging needs prepared host queues";
    return -1;
  }

  idx = wifi_status_state.tx_write_ptr % WIFI_TX_QUEUE_ENTRIES;
  next_idx = (idx + 1U) % WIFI_TX_QUEUE_ENTRIES;
  tfd = (wifi_tfh_tfd_t *)wifi_tx_tfd[idx];
  frame_phys = wifi_phys_addr(wifi_tx_buffers[idx]);
  if (!frame_phys) {
    wifi_status_state.tx_stage_failed = 1;
    wifi_status_state.tx_stage_errors++;
    wifi_status_state.status = "wifi: TX staging DMA address missing";
    return -1;
  }

  memset(wifi_tx_buffers[idx], 0, WIFI_TX_BUFFER_BYTES);
  memset(tfd, 0, sizeof(*tfd));
  memcpy(wifi_tx_buffers[idx], frame, frame_len);

  tfd->num_tbs = 1;
  tfd->tbs[0].tb_len = (uint16_t)frame_len;
  tfd->tbs[0].addr = frame_phys;
  wifi_tx_bc_tbl[idx].tfd_offset =
      wifi_gen2_byte_count(frame_len, tfd->num_tbs);

  wifi_status_state.tx_stage_ready = 1;
  wifi_status_state.tx_stage_failed = 0;
  wifi_status_state.tx_stage_frames++;
  wifi_status_state.tx_stage_queue = WIFI_DQA_TX_QUEUE;
  wifi_status_state.tx_stage_index = idx;
  wifi_status_state.tx_stage_next_index = next_idx;
  wifi_status_state.tx_stage_kind = kind;
  wifi_status_state.tx_stage_frame_len = frame_len;
  wifi_status_state.tx_stage_tfd_tbs = tfd->num_tbs;
  wifi_status_state.tx_stage_tb0_len = tfd->tbs[0].tb_len;
  wifi_status_state.tx_stage_tb0_addr = tfd->tbs[0].addr;
  wifi_status_state.tx_stage_bc_entry = wifi_tx_bc_tbl[idx].tfd_offset;
  wifi_status_state.tx_stage_wptr_value =
      (WIFI_DQA_TX_QUEUE << HBUS_TARG_WRPTR_Q_SHIFT) | next_idx;
  wifi_status_state.tx_stage_checksum =
      wifi_connect_checksum(frame, frame_len);
  wifi_status_state.tx_write_ptr = next_idx;
  wifi_status_state.status =
      "wifi: management frame staged in TX DMA buffer; doorbell not armed";
  return 0;
}

int wifi_tx_stage_probe(const char *target, char *report, size_t report_size) {
  const wifi_status_t *s;
  int stage_auth = 0;
  int stage_assoc = 0;
  int stage_m2 = 0;
  int rc = 0;

  if (!report || report_size == 0) {
    return -1;
  }

  report[0] = '\0';
  wifi_init();
  s = &wifi_status_state;

  if (!s->present) {
    snprintf(report, report_size,
             "wifi tx: no wireless controller detected\n");
    return -1;
  }

  if (!target || target[0] == '\0' || strcmp(target, "all") == 0) {
    stage_auth = 1;
    stage_assoc = 1;
  } else if (strcmp(target, "auth") == 0) {
    stage_auth = 1;
  } else if (strcmp(target, "assoc") == 0) {
    stage_assoc = 1;
  } else if (strcmp(target, "m2") == 0) {
    stage_m2 = 1;
  } else {
    snprintf(report, report_size,
             "wifi tx: usage wifi tx [auth|assoc|m2|all]\n");
    return -1;
  }

  if (!s->connect_ready) {
    snprintf(report, report_size,
             "wifi tx: no connection frame plan ready\n"
             "run: wifi bringup, wifi scan arm, wifi scan poll, "
             "wifi connect <ssid> [password]\n");
    return -1;
  }

  if (stage_auth) {
    rc = wifi_tx_stage_frame(WIFI_TX_STAGE_KIND_AUTH, wifi_connect_auth_frame,
                             s->connect_auth_frame_len);
  }
  if (rc == 0 && stage_assoc) {
    rc = wifi_tx_stage_frame(WIFI_TX_STAGE_KIND_ASSOC, wifi_connect_assoc_frame,
                             s->connect_assoc_frame_len);
  }
  if (rc == 0 && stage_m2) {
    if (!wifi_status_state.wpa_m2_data_ready &&
        wifi_status_state.wpa_m1_seen &&
        wifi_status_state.connect_pmk_ready) {
      wifi_wpa_prepare_m2((uint8_t)wifi_status_state.wpa_eapol_version,
                          (uint8_t)wifi_status_state.wpa_key_desc_type);
    }
    if (!wifi_status_state.wpa_m2_data_ready) {
      wifi_status_state.tx_stage_failed = 1;
      wifi_status_state.tx_stage_errors++;
      wifi_status_state.status =
          "wifi: WPA M2 data frame is not ready for TX staging";
      rc = -1;
    } else {
      rc = wifi_tx_stage_frame(WIFI_TX_STAGE_KIND_EAPOL_M2,
                               wifi_wpa_m2_data_frame,
                               wifi_status_state.wpa_m2_data_frame_len);
    }
  }

  s = &wifi_status_state;
  snprintf(report, report_size,
           "wifi tx: %s\n"
           "target: %s ssid=\"%s\" bssid=%02x:%02x:%02x:%02x:%02x:%02x\n"
           "stage: ready=%s failed=%s frames=%lu last=%s queue=%u "
           "index=%u next=%u\n"
           "dma: tx-tfd=0x%lx tx-buf=0x%lx tx-bc=0x%lx tb0=0x%lx\n"
           "tfd: tbs=%u tb0-len=%u frame-len=%u bc=0x%04x "
           "checksum=0x%08x\n"
           "doorbell-plan: reg=0x%03x value=0x%08x not-written=yes\n"
           "next: validate Intel TX_CMD/STA binding before ringing this queue\n",
           rc == 0 ? "management TX frame staged" : "management TX staging failed",
           (!target || target[0] == '\0') ? "all" : target,
           s->connect_ssid, s->connect_bssid[0], s->connect_bssid[1],
           s->connect_bssid[2], s->connect_bssid[3], s->connect_bssid[4],
           s->connect_bssid[5], s->tx_stage_ready ? "yes" : "no",
           s->tx_stage_failed ? "yes" : "no", s->tx_stage_frames,
           wifi_tx_stage_kind_text(s->tx_stage_kind), s->tx_stage_queue,
           s->tx_stage_index, s->tx_stage_next_index,
           (unsigned long)s->tx_tfd_phys, (unsigned long)s->tx_buffer_phys,
           (unsigned long)s->tx_bc_phys,
           (unsigned long)s->tx_stage_tb0_addr, s->tx_stage_tfd_tbs,
           s->tx_stage_tb0_len, s->tx_stage_frame_len, s->tx_stage_bc_entry,
           s->tx_stage_checksum, HBUS_TARG_WRPTR, s->tx_stage_wptr_value);
  return rc;
}

int wifi_connect(const char *ssid, const char *password, char *report,
                 size_t report_size) {
  const wifi_status_t *s;
  uint32_t ap_index = 0;
  uint32_t ap_security;
  uint32_t ssid_len;
  int use_wpa;
  const char *password_note;
  char pmk_reason[96];
  uint32_t auth_hash;
  uint32_t assoc_hash;

  if (!report || report_size == 0) {
    return -1;
  }

  report[0] = '\0';
  wifi_init();
  wifi_find_firmware();
  s = &wifi_status_state;

  if (!s->present) {
    snprintf(report, report_size,
             "wifi connect: no wireless controller detected\n");
    return -1;
  }

  if (!ssid || ssid[0] == '\0') {
    snprintf(report, report_size,
             "wifi connect: usage wifi connect <ssid> [password]\n");
    return -1;
  }

  ssid_len = (uint32_t)strlen(ssid);
  if (ssid_len > WIFI_SCAN_SSID_MAX) {
    snprintf(report, report_size,
             "wifi connect: SSID too long (%u, max %u)\n",
             ssid_len, WIFI_SCAN_SSID_MAX);
    return -1;
  }

  wifi_status_state.connect_attempts++;
  wifi_connect_clear_plan();

  if (!s->firmware_present || !s->boot_ready || !s->context_armed ||
      !s->rx_path_ready) {
    wifi_status_state.connect_failed = 1;
    snprintf(report, report_size,
             "wifi connect: radio stack is not ready yet\n"
             "state: firmware=%s boot=%s alive=%s context=%s rx=%s scan-aps=%u\n"
             "run: wifi bringup, wifi scan arm, wifi scan poll, then retry\n",
             s->firmware_present ? s->firmware_name : "missing",
             s->boot_ready ? "ready" : "not-ready",
             s->alive_seen ? "seen" : "not-seen",
             s->context_armed ? "armed" : "not-armed",
             s->rx_path_ready ? "ready" : "not-ready", s->scan_ap_count);
    return -1;
  }

  if (wifi_connect_find_ap(ssid, &ap_index) != 0) {
    wifi_status_state.connect_failed = 1;
    snprintf(report, report_size,
             "wifi connect: SSID not found in the current scan table\n"
             "target: ssid=\"%s\"\n"
             "scan: response=%s complete=%s aps=%u\n",
             ssid, s->scan_response_seen ? "yes" : "no",
             s->scan_complete_seen ? "yes" : "no", s->scan_ap_count);
    wifi_connect_append_known_aps(report, report_size);
    wifi_report_append(report, report_size,
                       "run: wifi scan arm, then repeat wifi scan poll until the AP appears\n");
    return -1;
  }

  s = &wifi_status_state;
  ap_security = s->scan_ap_security[ap_index];
  if (ap_security == WIFI_SCAN_SECURITY_WEP ||
      ap_security == WIFI_SCAN_SECURITY_WPA) {
    wifi_status_state.connect_failed = 1;
    snprintf(report, report_size,
             "wifi connect: unsupported AP security for now\n"
             "target: ssid=\"%s\" channel=%u security=%s\n"
             "supported-now: open plan or WPA2/RSN association template\n",
             ssid, s->scan_ap_channel[ap_index],
             wifi_scan_security_text(ap_security));
    return -1;
  }

  if (ap_security == WIFI_SCAN_SECURITY_WPA2 &&
      (!password || password[0] == '\0')) {
    wifi_status_state.connect_failed = 1;
    snprintf(report, report_size,
             "wifi connect: password required by scanned AP\n"
             "target: ssid=\"%s\" channel=%u security=%s\n"
             "usage: wifi connect <ssid> <password>\n",
             ssid, s->scan_ap_channel[ap_index],
             wifi_scan_security_text(ap_security));
    return -1;
  }

  use_wpa = ap_security == WIFI_SCAN_SECURITY_WPA2 ||
            (ap_security == WIFI_SCAN_SECURITY_UNKNOWN && password &&
             password[0]);
  password_note = use_wpa ? "provided"
                          : (password && password[0] ? "ignored-open-ap"
                                                      : "none");
  wifi_status_state.connect_ap_index = ap_index;
  wifi_status_state.connect_channel = s->scan_ap_channel[ap_index];
  wifi_status_state.connect_ssid_len = ssid_len;
  wifi_status_state.connect_wpa = use_wpa ? 1 : 0;
  wifi_status_state.connect_open = use_wpa ? 0 : 1;
  memcpy(wifi_status_state.connect_ssid, ssid, ssid_len);
  wifi_status_state.connect_ssid[ssid_len] = '\0';
  wifi_connect_copy_mac(wifi_status_state.connect_bssid,
                        s->scan_ap_bssid[ap_index]);
  wifi_connect_make_local_mac(wifi_status_state.connect_local_mac);

  if (use_wpa) {
    pmk_reason[0] = '\0';
    if (wifi_connect_prepare_wpa2_pmk(password, ssid_len, pmk_reason,
                                      sizeof(pmk_reason)) != 0) {
      wifi_status_state.connect_failed = 1;
      snprintf(report, report_size,
               "wifi connect: WPA2 key preparation failed\n"
               "target: ssid=\"%s\" channel=%u security=%s\n"
               "reason: %s\n",
               ssid, wifi_status_state.connect_channel,
               wifi_scan_security_text(ap_security),
               pmk_reason[0] ? pmk_reason : "unknown");
      return -1;
    }
  }

  if (wifi_connect_build_auth_frame(wifi_status_state.connect_bssid,
                                    wifi_status_state.connect_local_mac) != 0 ||
      wifi_connect_build_assoc_frame(ssid, ssid_len, use_wpa,
                                     wifi_status_state.connect_bssid,
                                     wifi_status_state.connect_local_mac) != 0) {
    wifi_status_state.connect_failed = 1;
    snprintf(report, report_size,
             "wifi connect: failed to build association frame plan\n"
             "target: ssid=\"%s\" channel=%u security=%s\n",
             ssid, wifi_status_state.connect_channel,
             use_wpa ? "wpa2-psk" : "open");
    return -1;
  }

  auth_hash = wifi_connect_checksum(wifi_connect_auth_frame,
                                    wifi_status_state.connect_auth_frame_len);
  assoc_hash = wifi_connect_checksum(wifi_connect_assoc_frame,
                                     wifi_status_state.connect_assoc_frame_len);
  wifi_status_state.connect_frame_checksum =
      auth_hash ^ (assoc_hash * 16777619U);
  wifi_status_state.connect_ready = 1;
  wifi_status_state.connect_failed = 0;
  wifi_status_state.driver_ready = 0;
  wifi_status_state.associated = 0;
  wifi_status_state.status =
      "wifi: association frames prepared; TX/WPA layer pending";

  snprintf(report, report_size,
           "wifi connect: association frame plan ready\n"
           "target: ssid=\"%s\" bssid=%02x:%02x:%02x:%02x:%02x:%02x "
           "channel=%u ap-security=%s frame-security=%s password=%s\n"
           "local: sta-mac=%02x:%02x:%02x:%02x:%02x:%02x\n"
           "frames: auth-fc=0x%04x auth-len=%u assoc-fc=0x%04x "
           "assoc-len=%u checksum=0x%08x\n"
           "wpa2: pmk=%s iterations=%u secret-len=%u pmk-check=0x%08x\n"
           "state: boot=%s context=%s rx=%s scan-aps=%u plan=%s associated=no\n"
           "next: run wifi tx all to stage DMA, then implement Intel TX_CMD, "
           "ADD_STA/MAC binding, and WPA 4-way handshake/key install\n"
           "note: password is not stored; for now it only selects the WPA2 "
           "RSN association template\n",
           wifi_status_state.connect_ssid, wifi_status_state.connect_bssid[0],
           wifi_status_state.connect_bssid[1],
           wifi_status_state.connect_bssid[2],
           wifi_status_state.connect_bssid[3],
           wifi_status_state.connect_bssid[4],
           wifi_status_state.connect_bssid[5],
           wifi_status_state.connect_channel,
           wifi_scan_security_text(ap_security),
           use_wpa ? "wpa2-psk" : "open",
           password_note,
           wifi_status_state.connect_local_mac[0],
           wifi_status_state.connect_local_mac[1],
           wifi_status_state.connect_local_mac[2],
           wifi_status_state.connect_local_mac[3],
           wifi_status_state.connect_local_mac[4],
           wifi_status_state.connect_local_mac[5],
           wifi_status_state.connect_auth_fc,
           wifi_status_state.connect_auth_frame_len,
           wifi_status_state.connect_assoc_fc,
           wifi_status_state.connect_assoc_frame_len,
           wifi_status_state.connect_frame_checksum,
           wifi_status_state.connect_pmk_ready
               ? (wifi_status_state.connect_pmk_iterations ? "derived"
                                                            : "hex-psk")
               : "not-needed",
           wifi_status_state.connect_pmk_iterations,
           wifi_status_state.connect_password_len,
           wifi_status_state.connect_pmk_checksum,
           s->boot_ready ? "ready" : "not-ready",
           s->context_armed ? "armed" : "not-armed",
           s->rx_path_ready ? "ready" : "not-ready", s->scan_ap_count,
           wifi_status_state.connect_ready ? "ready" : "failed");
  return 0;
}
