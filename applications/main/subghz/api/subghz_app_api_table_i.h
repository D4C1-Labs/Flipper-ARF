#include <subghz/devices/cc1101_configs.h>

/*
 * SubGhz app private API table — exposes the CC1101 preset register arrays to
 * the radio_device_cc1101_ext plugin when SubGhz runs as an external FAP.
 *
 * These symbols used to live in the firmware's global API table. Now that the
 * SubGhz protocol library is compiled into the FAP (and executed via XIP),
 * they are provided here and merged with the firmware API via a composite
 * resolver (see lib/subghz/devices/registry.c).
 */
static constexpr auto subghz_app_api_table = sort(create_array_t<sym_entry>(
    API_VARIABLE(subghz_device_cc1101_preset_ook_270khz_async_regs, const uint8_t[]),
    API_VARIABLE(subghz_device_cc1101_preset_ook_650khz_async_regs, const uint8_t[]),
    API_VARIABLE(subghz_device_cc1101_preset_2fsk_dev2_38khz_async_regs, const uint8_t[]),
    API_VARIABLE(subghz_device_cc1101_preset_2fsk_dev12khz_async_regs, const uint8_t[]),
    API_VARIABLE(subghz_device_cc1101_preset_2fsk_dev47_6khz_async_regs, const uint8_t[]),
    API_VARIABLE(subghz_device_cc1101_preset_msk_99_97kb_async_regs, const uint8_t[]),
    API_VARIABLE(subghz_device_cc1101_preset_gfsk_9_99kb_async_regs, const uint8_t[])));
