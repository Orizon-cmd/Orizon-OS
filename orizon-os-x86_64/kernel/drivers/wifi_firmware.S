/*
 * Orizon OS x86_64 - Embedded Intel Wi-Fi firmware
 *
 * This embeds the primary Lenovo/Alder Lake-N Intel CNVi firmware so installed
 * systems updated from GitHub can stage Wi-Fi firmware without reinstalling
 * from a new ISO. Additional firmware blobs are still copied into the ISO as
 * Limine modules for broader live-boot coverage.
 */

.section .rodata
.balign 16
.global orizon_iwlwifi_so_a0_hr_b0_89_ucode_start
.global orizon_iwlwifi_so_a0_hr_b0_89_ucode_end
orizon_iwlwifi_so_a0_hr_b0_89_ucode_start:
.incbin "firmware/iwlwifi-so-a0-hr-b0-89.ucode"
orizon_iwlwifi_so_a0_hr_b0_89_ucode_end:
.balign 16
