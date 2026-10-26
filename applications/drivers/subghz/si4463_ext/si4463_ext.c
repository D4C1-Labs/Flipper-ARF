#include "si4463_ext.h"
#include "si4463_modem.h"

#include <furi.h>
#include <furi_hal_spi.h>
#include <furi_hal_interrupt.h>
#include <furi_hal_resources.h>
#include <furi_hal_bus.h>
#include <furi_hal_cortex.h>
#include <stm32wbxx_ll_dma.h>
#include <stm32wbxx_ll_tim.h>

#include <string.h>

#define TAG "SubGhzDeviceSi4463Ext"

#define SI4463_DATA_GPIO (&gpio_ext_pb2)
#define SI4463_SDN_GPIO  (&gpio_ext_pc3)

#define SI4463_FREQ_MIN_HZ 425000000u
#define SI4463_FREQ_MAX_HZ 463000000u
#define SI4463_XO_HZ       30000000ull
#define SI4463_OUTDIV      8ull
#define SI4463_NPRESC      2ull

#define SI4463_SPI_BUS        (&furi_hal_spi_bus_handle_external_radio)
#define SI4463_SPI_BUS_LEGACY (&furi_hal_spi_bus_handle_external)

#define SI4463_CMD_PART_INFO      0x01
#define SI4463_CMD_POWER_UP       0x02
#define SI4463_CMD_GET_PROPERTY   0x12
#define SI4463_CMD_GET_MODEM_STAT 0x22
#define SI4463_CMD_GET_CHIP_STAT  0x23
#define SI4463_CMD_GPIO_PIN_CFG   0x13
#define SI4463_CMD_START_TX       0x31
#define SI4463_CMD_START_RX       0x32
#define SI4463_CMD_CHANGE_STATE   0x34
#define SI4463_CMD_IRCAL          0x17
#define SI4463_CMD_READ_CMD_BUFF  0x44

#define SI4463_CMD_TIMEOUT_MS   30u
#define SI4463_IRCAL_TIMEOUT_MS 500u

#define SI4463_STATE_SLEEP 0x01
#define SI4463_STATE_READY 0x03
#define SI4463_STATE_RX    0x08

#define SI4463_GPIO_TRISTATE 0x01
#define SI4463_GPIO_INPUT    0x04
#define SI4463_GPIO_SDO      0x0B
#define SI4463_GPIO_RX_RAW   0x15
#define SI4463_GPIO_TX_STATE 0x20
#define SI4463_GPIO_RX_STATE 0x21

#define SI4463_GPIO_DRIVE_MED_LOW 0x40

#define SI4463_RX_NEXT_REMAIN 0x00

#define SI4463_PART_SI4463 0x4463

#define SI4463_RSSI_OFFSET_DB 134

#define SI4463_POWER_UP_TIMEOUT_MS 50u
#define SI4463_POR_TIMEOUT_MS      20u
#define SI4463_BOOT_ATTEMPTS       3u
#define SI4463_DMA                 (DMA2)
#define SI4463_DMA_CH3_CHANNEL     (LL_DMA_CHANNEL_3)
#define SI4463_DMA_CH4_CHANNEL     (LL_DMA_CHANNEL_4)
#define SI4463_DMA_CH5_CHANNEL     (LL_DMA_CHANNEL_5)
#define SI4463_DMA_CH3_IRQ         (FuriHalInterruptIdDma2Ch3)
#define SI4463_DMA_CH3_DEF         SI4463_DMA, SI4463_DMA_CH3_CHANNEL
#define SI4463_DMA_CH4_DEF         SI4463_DMA, SI4463_DMA_CH4_CHANNEL
#define SI4463_DMA_CH5_DEF         SI4463_DMA, SI4463_DMA_CH5_CHANNEL

#define SI4463_ASYNC_TX_BUFFER_FULL 256u
#define SI4463_ASYNC_TX_BUFFER_HALF (SI4463_ASYNC_TX_BUFFER_FULL / 2)
#define SI4463_ASYNC_TX_GUARD_TIME  (999u >> 1)

typedef enum {
    Si4463StateInit,
    Si4463StateIdle,
    Si4463StateAsyncRx,
    Si4463StateAsyncTx,
} Si4463State;

typedef enum {
    Si4463TxMwIdle,
    Si4463TxMwReset,
    Si4463TxMwRun,
} Si4463TxMwState;

typedef struct {
    Si4463TxMwState state;
    bool is_odd_level;
    uint32_t adder_duration;
} Si4463TxMw;

typedef struct {
    uint32_t* buffer;
    SubGhzDeviceSi4463ExtCallback callback;
    void* callback_context;
    uint32_t gpio_tx_buff[2];
    uint32_t debug_gpio_buff[2];
    Si4463TxMw middleware;
} Si4463AsyncTx;

typedef struct {
    SubGhzDeviceSi4463ExtCaptureCallback capture_callback;
    void* capture_callback_context;
    uint32_t timer_ticks;
    bool pending_valid;
    bool pending_level;
    uint32_t pending_duration;
    uint32_t glitch_us;
    volatile bool suppressed;
    bool discard_next;
} Si4463AsyncRx;

#define SI4463_CAPTURE_GLITCH_US     50u
#define SI4463_CAPTURE_GLITCH_FSK_US 40u
#define SI4463_CAPTURE_DURATION_MAX  0x3FFFFFFEu

typedef struct {
    volatile Si4463State state;
    const GpioPin* async_mirror_pin;
    const FuriHalSpiBusHandle* spi_bus_handle;
    const GpioPin* g0_pin;
    const GpioPin* sdn_pin;
    Si4463AsyncTx async_tx;
    Si4463AsyncRx async_rx;
    Si4463Modem modem;
    uint32_t frequency;
    bool frequency_valid;
    bool configuration_valid;
    bool identified;
    uint8_t chiprev;
    uint8_t rom;
    bool sdn_owned;
    bool asleep;
    bool standby;
    bool spi_fallback;
    bool modem_status_failed;
} Si4463Device;

static Si4463Device* si4463 = NULL;

static bool si4463_trx_selected(const uint8_t* tx, uint8_t* rx, size_t len, uint32_t timeout_ms) {
    const FuriHalSpiBusHandle* handle = si4463->spi_bus_handle;
    furi_hal_gpio_write(handle->cs, true);
    furi_delay_us(2);
    furi_hal_gpio_write(handle->cs, false);
    bool ok = furi_hal_spi_bus_trx(handle, tx, rx, len, timeout_ms);
    furi_hal_gpio_write(handle->cs, true);
    return ok;
}

static bool si4463_wait_cts(uint32_t timeout_ms);

static bool si4463_command(
    const uint8_t* cmd,
    size_t cmd_len,
    uint8_t* data,
    size_t data_len,
    uint32_t timeout_ms) {
    if(cmd_len == 0 || cmd_len > 16 || data_len > 16) return false;

    if(!si4463_wait_cts(timeout_ms)) return false;

    uint8_t rx_cmd[16] = {0};
    if(!si4463_trx_selected(cmd, rx_cmd, cmd_len, timeout_ms)) return false;

    uint8_t tx[18] = {0};
    uint8_t rx[18] = {0};
    tx[0] = SI4463_CMD_READ_CMD_BUFF;
    const size_t trx_len = 2 + data_len;
    const uint32_t start = furi_get_tick();
    do {
        if(!si4463_trx_selected(tx, rx, trx_len, timeout_ms)) return false;
        if(rx[1] == 0xFF) {
            if(data && data_len) memcpy(data, &rx[2], data_len);
            return true;
        }
        furi_delay_us(100);
    } while((furi_get_tick() - start) < furi_ms_to_ticks(timeout_ms));

    return false;
}

static bool si4463_send(const uint8_t* cmd, size_t len, void* context) {
    UNUSED(context);
    const uint32_t timeout_ms = cmd[0] == SI4463_CMD_IRCAL ? SI4463_IRCAL_TIMEOUT_MS :
                                                             SI4463_CMD_TIMEOUT_MS;
    if(!si4463_command(cmd, len, NULL, 0, timeout_ms)) return false;
    const uint8_t status_cmd[] = {SI4463_CMD_GET_CHIP_STAT, 0xF7};
    uint8_t status[4];
    if(!si4463_command(
           status_cmd, sizeof(status_cmd), status, sizeof(status), SI4463_CMD_TIMEOUT_MS))
        return false;
    if(status[0] & 0x08) {
        FURI_LOG_E(TAG, "Command %02X rejected: %02X", status[3], status[2]);
        return false;
    }
    return true;
}

static bool si4463_power_up(bool patched) {
    const uint8_t cmd[7] = {
        SI4463_CMD_POWER_UP, patched ? 0x81 : 0x01, 0x00, 0x01, 0xC9, 0xC3, 0x80};
    return si4463_command(cmd, sizeof(cmd), NULL, 0, SI4463_POWER_UP_TIMEOUT_MS);
}

static bool si4463_wait_cts(uint32_t timeout_ms) {
    const uint8_t tx[2] = {SI4463_CMD_READ_CMD_BUFF, 0x00};
    uint8_t rx[2] = {0};
    const uint32_t start = furi_get_tick();
    do {
        if(si4463_trx_selected(tx, rx, sizeof(tx), timeout_ms) && rx[1] == 0xFF) return true;
        furi_delay_us(100);
    } while((furi_get_tick() - start) < furi_ms_to_ticks(timeout_ms));
    return false;
}

static bool si4463_read_part(uint16_t* part) {
    const uint8_t cmd = SI4463_CMD_PART_INFO;
    uint8_t info[8] = {0};
    if(!si4463_command(&cmd, 1, info, sizeof(info), SI4463_CMD_TIMEOUT_MS)) return false;
    *part = ((uint16_t)info[1] << 8) | info[2];
    si4463->chiprev = info[0];
    si4463->rom = info[7];
    FURI_LOG_I(TAG, "Part %04X chiprev %u ROM %u", *part, info[0], info[7]);
    return true;
}

static bool si4463_change_state(uint8_t next) {
    const uint8_t cmd[2] = {SI4463_CMD_CHANGE_STATE, next};
    return si4463_command(cmd, sizeof(cmd), NULL, 0, SI4463_CMD_TIMEOUT_MS);
}

static bool si4463_gpio(uint8_t gpio0) {
    const uint8_t cmd[8] = {
        SI4463_CMD_GPIO_PIN_CFG,
        gpio0,
        SI4463_GPIO_TRISTATE,
        SI4463_GPIO_RX_STATE,
        SI4463_GPIO_TX_STATE,
        0x00,
        SI4463_GPIO_SDO,
        SI4463_GPIO_DRIVE_MED_LOW,
    };
    return si4463_command(cmd, sizeof(cmd), NULL, 0, SI4463_CMD_TIMEOUT_MS);
}

static bool si4463_program_frequency(uint32_t hz, uint32_t* actual) {
    const uint64_t n = ((uint64_t)hz << 19) / (SI4463_NPRESC * SI4463_XO_HZ / SI4463_OUTDIV);
    const uint8_t inte = (uint8_t)((n >> 19) - 1);
    const uint32_t frac = (uint32_t)(n & 0x7FFFF) + 0x80000;
    const uint8_t cmd[8] = {
        0x11,
        0x40,
        0x04,
        0x00,
        inte,
        (uint8_t)((frac >> 16) & 0xFF),
        (uint8_t)((frac >> 8) & 0xFF),
        (uint8_t)(frac & 0xFF),
    };
    if(!si4463_command(cmd, sizeof(cmd), NULL, 0, SI4463_CMD_TIMEOUT_MS)) return false;
    if(actual) {
        *actual = (uint32_t)((n * (SI4463_NPRESC * SI4463_XO_HZ / SI4463_OUTDIV)) >> 19);
    }
    return true;
}

static bool si4463_configure(void) {
    si4463->configuration_valid = false;
    if(!si4463_modem_load_base(si4463_send, NULL)) return false;
    if(!si4463_modem_apply(si4463_send, NULL, &si4463->modem)) return false;
    if(!si4463_gpio(SI4463_GPIO_TRISTATE)) return false;
    if(si4463->frequency_valid) {
        if(!si4463_program_frequency(si4463->frequency, NULL)) return false;
    }
    si4463->configuration_valid = si4463_change_state(SI4463_STATE_READY);
    return si4463->configuration_valid;
}

static void si4463_sdn_drive_high(void) {
    furi_hal_gpio_write(si4463->sdn_pin, true);
    furi_hal_gpio_init(si4463->sdn_pin, GpioModeOutputPushPull, GpioPullNo, GpioSpeedLow);
    si4463->sdn_owned = true;
}

static void si4463_sdn_release(void) {
    furi_hal_gpio_init(si4463->sdn_pin, GpioModeAnalog, GpioPullNo, GpioSpeedLow);
    si4463->sdn_owned = false;
}

static bool si4463_patch_send(const uint8_t* cmd, size_t len, void* context) {
    UNUSED(context);
    return si4463_command(cmd, len, NULL, 0, 50);
}

static bool si4463_sdn_reset_and_configure(void) {
    si4463->configuration_valid = false;
    if(si4463->chiprev != 34 || si4463->rom != 6) {
        FURI_LOG_E(TAG, "WDS profile requires C2 / ROM 6");
        return false;
    }
    bool powered = false;
    for(uint32_t attempt = 0; attempt < SI4463_BOOT_ATTEMPTS && !powered; attempt++) {
        si4463_sdn_drive_high();
        furi_delay_ms(1);
        furi_hal_gpio_write(si4463->sdn_pin, false);
        powered = si4463_wait_cts(SI4463_POR_TIMEOUT_MS) &&
                  si4463_modem_load_patch(si4463_patch_send, NULL) && si4463_power_up(true);
        if(!powered) FURI_LOG_W(TAG, "POWER_UP stalled, attempt %lu", attempt + 1);
    }
    if(!powered) return false;
    si4463->asleep = false;
    si4463->standby = false;
    if(!si4463_gpio(SI4463_GPIO_TRISTATE) || !si4463_modem_calibrate(si4463_send, NULL)) {
        FURI_LOG_E(TAG, "WDS IRCAL failed");
        return false;
    }
    FURI_LOG_I(TAG, "ROM6 patch CA90, IRCAL complete");
    return si4463_configure();
}

static bool si4463_get_property(uint8_t group, uint8_t index, uint8_t* value) {
    const uint8_t cmd[] = {SI4463_CMD_GET_PROPERTY, group, 0x01, index};
    return si4463_command(cmd, sizeof(cmd), value, 1, SI4463_CMD_TIMEOUT_MS);
}

static bool si4463_config_retained(void) {
    uint8_t mod_type = 0;
    uint8_t pkt_config1 = 0;
    return si4463_get_property(0x20, 0x00, &mod_type) &&
           si4463_get_property(0x12, 0x06, &pkt_config1) &&
           (mod_type == SI4463_MOD_OOK || mod_type == SI4463_MOD_2FSK) &&
           pkt_config1 == si4463_modem_profile_pkt_config1(si4463->modem.wds_profile);
}

static bool si4463_ensure_awake(void) {
    if(si4463->standby) {
        si4463->standby = false;
        if(si4463->configuration_valid && si4463_change_state(SI4463_STATE_READY) &&
           si4463_config_retained()) {
            return true;
        }
        FURI_LOG_W(TAG, "Configuration lost in standby, full reset");
        si4463->asleep = true;
    }
    if(!si4463->asleep) return si4463->configuration_valid;
    return si4463_sdn_reset_and_configure();
}

static bool si4463_bus_is_cc1101(bool* clocked) {
    const FuriHalSpiBusHandle* handle = si4463->spi_bus_handle;
    *clocked = false;

    furi_hal_gpio_init(handle->miso, GpioModeInput, GpioPullUp, GpioSpeedLow);
    furi_hal_gpio_write(handle->cs, false);

    bool ready = false;
    FuriHalCortexTimer ready_timer = furi_hal_cortex_timer_get(10 * 1000);
    while(!furi_hal_cortex_timer_is_expired(ready_timer)) {
        if(!furi_hal_gpio_read(handle->miso)) {
            ready = true;
            break;
        }
    }

    if(!ready) {
        furi_hal_gpio_write(handle->cs, true);
        furi_hal_gpio_init_ex(
            handle->miso,
            GpioModeAltFunctionPushPull,
            GpioPullDown,
            GpioSpeedVeryHigh,
            GpioAltFn5SPI1);
        return false;
    }

    furi_hal_gpio_init_ex(
        handle->miso, GpioModeAltFunctionPushPull, GpioPullDown, GpioSpeedVeryHigh, GpioAltFn5SPI1);
    uint8_t tx[2] = {0xF1, 0x00};
    uint8_t rx[2] = {0};
    *clocked = true;
    bool trx_ok = furi_hal_spi_bus_trx(handle, tx, rx, sizeof(tx), 20);
    furi_hal_gpio_write(handle->cs, true);
    return trx_ok && ((rx[0] & 0x80) == 0) && (rx[1] == 0x04 || rx[1] == 0x14);
}

#define SI4463_MODEM_STATUS_CTS_US 40u
#define SI4463_CTS_RETRY_US        20u

static bool si4463_read_modem_status(uint8_t* resp, size_t len) {
    const uint8_t cmd[] = {SI4463_CMD_GET_MODEM_STAT, 0xFF};
    furi_check(len >= 3 && len <= 8);
    if(si4463->modem_status_failed && !si4463_wait_cts(SI4463_CMD_TIMEOUT_MS)) return false;
    si4463->modem_status_failed = true;
    uint8_t ignored[sizeof(cmd)];
    if(!si4463_trx_selected(cmd, ignored, sizeof(cmd), SI4463_CMD_TIMEOUT_MS)) return false;

    furi_delay_us(SI4463_MODEM_STATUS_CTS_US);
    uint8_t tx[10] = {SI4463_CMD_READ_CMD_BUFF};
    uint8_t rx[10] = {0};
    const uint32_t start = furi_get_tick();
    do {
        if(!si4463_trx_selected(tx, rx, len + 2, SI4463_CMD_TIMEOUT_MS)) return false;
        if(rx[1] == 0xFF) {
            memcpy(resp, &rx[2], len);
            si4463->modem_status_failed = false;
            return true;
        }
        furi_delay_us(SI4463_CTS_RETRY_US);
    } while((furi_get_tick() - start) < furi_ms_to_ticks(SI4463_CMD_TIMEOUT_MS));
    return false;
}

#define SI4463_RSSI_BURST_MIN_US 1000u

static uint8_t si4463_rssi_to_cc1101(uint8_t curr_rssi) {
    int32_t v = (int32_t)curr_rssi - 2 * (SI4463_RSSI_OFFSET_DB - 74);
    if(v < INT8_MIN) v = INT8_MIN;
    if(v > INT8_MAX) v = INT8_MAX;
    return (uint8_t)(int8_t)v;
}

bool subghz_device_si4463_ext_is_frequency_valid(uint32_t frequency) {
    return frequency >= SI4463_FREQ_MIN_HZ && frequency <= SI4463_FREQ_MAX_HZ;
}

typedef enum {
    Si4463BringUpOk,
    Si4463BringUpCc1101,
    Si4463BringUpAbsent,
    Si4463BringUpFailed,
} Si4463BringUp;

static Si4463BringUp si4463_identify(bool clear_error) {
    bool clocked = false;
    if(si4463_bus_is_cc1101(&clocked)) return Si4463BringUpCc1101;
    if(clocked || clear_error) {
        const uint8_t clear = SI4463_CMD_GET_CHIP_STAT;
        si4463_command(&clear, 1, NULL, 0, 10);
    }
    if(!si4463_power_up(false)) {
        FURI_LOG_W(TAG, "No POWER_UP response");
        return Si4463BringUpAbsent;
    }
    uint16_t part = 0;
    if(!si4463_read_part(&part) || part != SI4463_PART_SI4463) {
        FURI_LOG_W(TAG, "PART_INFO 0x%04X", part);
        return Si4463BringUpAbsent;
    }
    return Si4463BringUpOk;
}

static Si4463BringUp si4463_bring_up(bool configure) {
    Si4463BringUp result = Si4463BringUpAbsent;
    for(uint8_t attempt = 0; attempt < 2; attempt++) {
        if(attempt) {
            furi_hal_spi_bus_handle_deinit(si4463->spi_bus_handle);
            si4463->spi_bus_handle = SI4463_SPI_BUS_LEGACY;
            si4463->spi_fallback = true;
            furi_hal_spi_bus_handle_init(si4463->spi_bus_handle);
        }
        furi_hal_spi_acquire(si4463->spi_bus_handle);
        result = si4463_identify(attempt > 0);
        if(result == Si4463BringUpOk && configure && !si4463_sdn_reset_and_configure()) {
            result = Si4463BringUpFailed;
        }
        furi_hal_spi_release(si4463->spi_bus_handle);
        if(result == Si4463BringUpOk || result == Si4463BringUpCc1101) break;
    }
    return result;
}

static void si4463_device_alloc(void) {
    si4463 = malloc(sizeof(Si4463Device));
    memset(si4463, 0, sizeof(Si4463Device));
    si4463->state = Si4463StateInit;
    si4463->spi_bus_handle = SI4463_SPI_BUS;
    si4463->g0_pin = SI4463_DATA_GPIO;
    si4463->sdn_pin = SI4463_SDN_GPIO;
    si4463_modem_from_cc1101(NULL, &si4463->modem);

    furi_hal_spi_bus_handle_init(si4463->spi_bus_handle);
    furi_hal_gpio_init(si4463->sdn_pin, GpioModeAnalog, GpioPullNo, GpioSpeedLow);
    furi_hal_gpio_init(si4463->g0_pin, GpioModeAnalog, GpioPullNo, GpioSpeedLow);
    furi_delay_ms(20);
}

static void si4463_device_free(void) {
    furi_hal_gpio_init(si4463->g0_pin, GpioModeAnalog, GpioPullNo, GpioSpeedLow);
    furi_hal_spi_bus_handle_deinit(si4463->spi_bus_handle);
    free(si4463);
    si4463 = NULL;
}

static bool si4463_enter_standby(void) {
    const uint8_t cmd[2] = {SI4463_CMD_CHANGE_STATE, SI4463_STATE_SLEEP};
    uint8_t ignored[sizeof(cmd)];
    return si4463_wait_cts(SI4463_CMD_TIMEOUT_MS) &&
           si4463_trx_selected(cmd, ignored, sizeof(cmd), SI4463_CMD_TIMEOUT_MS);
}

bool subghz_device_si4463_ext_alloc(SubGhzDeviceConf* conf) {
    UNUSED(conf);
    furi_assert(si4463 == NULL);
    si4463_device_alloc();

    const Si4463BringUp result = si4463_bring_up(true);
    const bool ok = result == Si4463BringUpOk;
    if(ok) {
        si4463->identified = true;
        si4463->state = Si4463StateIdle;
        FURI_LOG_I(TAG, "Init OK, SPI %s", si4463->spi_fallback ? "2 MHz (fallback)" : "8 MHz");
    } else if(result == Si4463BringUpCc1101) {
        FURI_LOG_I(TAG, "Bus answered as CC1101");
    } else {
        FURI_LOG_E(TAG, "Init failed");
    }
    if(!ok && si4463->sdn_owned) si4463_sdn_release();
    return ok;
}

void subghz_device_si4463_ext_free(void) {
    furi_assert(si4463 != NULL);

    if(si4463->sdn_owned && si4463->identified && !si4463->asleep && !si4463->standby) {
        furi_hal_spi_acquire(si4463->spi_bus_handle);
        if(si4463_change_state(SI4463_STATE_READY)) si4463_enter_standby();
        furi_hal_spi_release(si4463->spi_bus_handle);
    }
    if(si4463->sdn_owned) si4463_sdn_release();
    si4463_device_free();
}

bool subghz_device_si4463_ext_is_connect(void) {
    if(si4463 != NULL) return si4463->identified;
    si4463_device_alloc();
    const bool found = si4463_bring_up(false) == Si4463BringUpOk;
    si4463_device_free();
    return found;
}

void subghz_device_si4463_ext_reset(void) {
    furi_assert(si4463 != NULL);
    if(!si4463->identified) return;

    furi_hal_spi_acquire(si4463->spi_bus_handle);
    if(!si4463_sdn_reset_and_configure()) {
        FURI_LOG_E(TAG, "Reset failed");
    }
    si4463->state = Si4463StateIdle;
    furi_hal_spi_release(si4463->spi_bus_handle);
}

void subghz_device_si4463_ext_sleep(void) {
    furi_assert(si4463 != NULL);
    furi_assert(si4463->state == Si4463StateIdle);
    if(!si4463->sdn_owned || si4463->asleep || si4463->standby) return;

    furi_hal_spi_acquire(si4463->spi_bus_handle);
    const bool standby = si4463->configuration_valid && si4463_change_state(SI4463_STATE_READY) &&
                         si4463_gpio(SI4463_GPIO_TRISTATE) && si4463_enter_standby();
    furi_hal_spi_release(si4463->spi_bus_handle);
    if(standby) {
        si4463->standby = true;
    } else {
        si4463_sdn_drive_high();
        si4463->asleep = true;
    }
    furi_hal_gpio_init(si4463->g0_pin, GpioModeAnalog, GpioPullNo, GpioSpeedLow);
}

void subghz_device_si4463_ext_idle(void) {
    furi_assert(si4463 != NULL);
    furi_hal_spi_acquire(si4463->spi_bus_handle);
    if(si4463_ensure_awake()) {
        si4463_change_state(SI4463_STATE_READY);
        si4463_gpio(SI4463_GPIO_TRISTATE);
    }
    furi_hal_spi_release(si4463->spi_bus_handle);
}

void subghz_device_si4463_ext_load_custom_preset(const uint8_t* preset_data) {
    furi_assert(si4463 != NULL);
    Si4463Modem modem;
    si4463_modem_from_cc1101(preset_data, &modem);
    furi_hal_spi_acquire(si4463->spi_bus_handle);
    si4463->modem = modem;
    si4463->async_rx.suppressed = true;
    if(si4463_ensure_awake()) {
        si4463->configuration_valid = si4463_change_state(SI4463_STATE_READY) &&
                                      si4463_modem_apply(si4463_send, NULL, &si4463->modem);
        if(!si4463->configuration_valid) {
            FURI_LOG_E(
                TAG,
                "Preset apply failed (WDS profile %s exported?); RX/TX disabled until reset",
                si4463_modem_profile_name(si4463->modem.wds_profile));
        } else {
            FURI_LOG_I(
                TAG,
                "Preset mod=%02X rate=%lu BW=%lu dev=%lu profile=%s",
                si4463->modem.mod_type,
                si4463->modem.bps,
                si4463->modem.rx_bw_hz,
                si4463->modem.deviation_hz,
                si4463_modem_profile_name(si4463->modem.wds_profile));
        }
    }
    furi_hal_spi_release(si4463->spi_bus_handle);
}

uint32_t subghz_device_si4463_ext_set_frequency(uint32_t frequency) {
    furi_assert(si4463 != NULL);
    if(!subghz_device_si4463_ext_is_frequency_valid(frequency)) {
        si4463->frequency_valid = false;
        FURI_LOG_W(TAG, "Frequency %lu Hz outside 403-463 MHz", frequency);
        return 0;
    }

    uint32_t actual = 0;
    furi_hal_spi_acquire(si4463->spi_bus_handle);
    si4463->frequency = frequency;
    si4463->frequency_valid = true;
    si4463->async_rx.suppressed = true;
    if(!si4463_ensure_awake() || !si4463_program_frequency(frequency, &actual)) {
        si4463->frequency_valid = false;
        actual = 0;
        FURI_LOG_E(TAG, "Frequency program failed");
    }
    furi_hal_spi_release(si4463->spi_bus_handle);
    return actual;
}

void subghz_device_si4463_ext_set_async_mirror_pin(const GpioPin* pin) {
    furi_assert(si4463 != NULL);
    si4463->async_mirror_pin = pin;
}

const GpioPin* subghz_device_si4463_ext_get_data_gpio(void) {
    furi_assert(si4463 != NULL);
    return si4463->g0_pin;
}

bool subghz_device_si4463_ext_tx(void) {
    furi_assert(si4463 != NULL);
    if(!si4463->frequency_valid) return false;

    furi_hal_spi_acquire(si4463->spi_bus_handle);
    bool ok = false;
    do {
        if(!si4463_ensure_awake()) break;
        if(!si4463_gpio(SI4463_GPIO_INPUT)) break;
        if(!si4463_change_state(SI4463_STATE_READY)) break;
        if(!si4463_program_frequency(si4463->frequency, NULL) ||
           !si4463_modem_apply_tx(si4463_send, NULL, &si4463->modem))
            break;
        const uint8_t cmd[5] = {SI4463_CMD_START_TX, 0x00, 0x00, 0x00, 0x00};
        ok = si4463_command(cmd, sizeof(cmd), NULL, 0, SI4463_CMD_TIMEOUT_MS);
    } while(false);
    furi_hal_spi_release(si4463->spi_bus_handle);
    if(!ok) FURI_LOG_E(TAG, "START_TX failed");
    return ok;
}

void subghz_device_si4463_ext_flush_tx(void) {
}

void subghz_device_si4463_ext_rx(void) {
    furi_assert(si4463 != NULL);
    if(!si4463->frequency_valid) {
        FURI_LOG_W(TAG, "START_RX refused: no valid frequency");
        return;
    }
    furi_hal_spi_acquire(si4463->spi_bus_handle);
    bool ok = false;
    do {
        if(!si4463_ensure_awake()) break;
        if(!si4463_gpio(SI4463_GPIO_RX_RAW)) break;
        if(!si4463_change_state(SI4463_STATE_READY)) break;
        const uint8_t cmd[8] = {
            SI4463_CMD_START_RX,
            0x00,
            0x00,
            0x00,
            0x00,
            SI4463_RX_NEXT_REMAIN,
            SI4463_RX_NEXT_REMAIN,
            SI4463_RX_NEXT_REMAIN,
        };
        ok = si4463_command(cmd, sizeof(cmd), NULL, 0, SI4463_CMD_TIMEOUT_MS);
    } while(false);
    furi_hal_spi_release(si4463->spi_bus_handle);
    if(!ok) FURI_LOG_E(TAG, "START_RX failed");
}

void subghz_device_si4463_ext_flush_rx(void) {
}

float subghz_device_si4463_ext_get_rssi(void) {
    furi_assert(si4463 != NULL);
    uint8_t resp[8] = {0};
    furi_hal_spi_acquire(si4463->spi_bus_handle);
    bool ok = si4463_ensure_awake() && si4463_read_modem_status(resp, 3);
    furi_hal_spi_release(si4463->spi_bus_handle);
    if(!ok) return -128.0f;
    return (float)resp[2] / 2.0f - SI4463_RSSI_OFFSET_DB;
}

uint32_t subghz_device_si4463_ext_rssi_burst(uint8_t* out, size_t count, uint32_t period_us) {
    furi_check(out);
    furi_assert(si4463 != NULL);
    if(count == 0) return 0;

    if(period_us < SI4463_RSSI_BURST_MIN_US) period_us = SI4463_RSSI_BURST_MIN_US;
    const uint32_t cycles_per_us = furi_hal_cortex_instructions_per_microsecond();
    const uint32_t period_cycles = period_us * cycles_per_us;

    furi_hal_spi_acquire(si4463->spi_bus_handle);
    if(!si4463_ensure_awake()) {
        furi_hal_spi_release(si4463->spi_bus_handle);
        memset(out, 0, count);
        return 0;
    }

    uint8_t discard[8];
    si4463_read_modem_status(discard, 3);
    const uint32_t warm = DWT->CYCCNT;
    while((int32_t)(DWT->CYCCNT - (warm + period_cycles)) < 0) {
    }
    const uint32_t t0 = DWT->CYCCNT;
    for(size_t i = 0; i < count; i++) {
        const uint32_t deadline = t0 + (uint32_t)i * period_cycles;
        while((int32_t)(DWT->CYCCNT - deadline) < 0) {
        }
        uint8_t resp[8] = {0};
        if(si4463_read_modem_status(resp, 3)) {
            out[i] = si4463_rssi_to_cc1101(resp[2]);
        } else {
            out[i] = (uint8_t)INT8_MIN;
        }
    }
    const uint32_t span = DWT->CYCCNT - t0;
    furi_hal_spi_release(si4463->spi_bus_handle);
    return span / cycles_per_us;
}

uint8_t subghz_device_si4463_ext_get_lqi(void) {
    return 0;
}

int8_t subghz_device_si4463_ext_get_freq_est(void) {
    furi_assert(si4463 != NULL);
    if(si4463_modem_is_ook(&si4463->modem)) return 0;

    uint8_t resp[8] = {0};
    furi_hal_spi_acquire(si4463->spi_bus_handle);
    bool ok = si4463_ensure_awake() && si4463_read_modem_status(resp, 8);
    furi_hal_spi_release(si4463->spi_bus_handle);
    if(!ok) return 0;

    const int16_t afc = (int16_t)(((uint16_t)resp[6] << 8) | resp[7]);
    const int64_t offset_hz =
        ((int64_t)afc * (int64_t)(SI4463_NPRESC * SI4463_XO_HZ)) / (int64_t)(SI4463_OUTDIV << 19);
    int64_t lsb = (offset_hz * 16384) / 26000000;
    if(lsb < INT8_MIN) lsb = INT8_MIN;
    if(lsb > INT8_MAX) lsb = INT8_MAX;
    return (int8_t)lsb;
}

static bool si4463_start_debug(void) {
    if(si4463->async_mirror_pin == NULL) return false;
    furi_hal_gpio_init(
        si4463->async_mirror_pin, GpioModeOutputPushPull, GpioPullNo, GpioSpeedVeryHigh);
    return true;
}

static bool si4463_stop_debug(void) {
    if(si4463->async_mirror_pin == NULL) return false;
    furi_hal_gpio_init(si4463->async_mirror_pin, GpioModeAnalog, GpioPullNo, GpioSpeedLow);
    return true;
}

static void si4463_capture_timer_isr(void* context) {
    UNUSED(context);
    FURI_CRITICAL_ENTER();
    if(LL_TIM_IsActiveFlag_UPDATE(TIM17)) {
        LL_TIM_ClearFlag_UPDATE(TIM17);
        if(si4463->async_rx.timer_ticks < 0x1FFF0000u) si4463->async_rx.timer_ticks += 0x10000u;
    }
    FURI_CRITICAL_EXIT();
}

static void si4463_capture_isr(void* context) {
    UNUSED(context);
    Si4463AsyncRx* rx = &si4463->async_rx;
    FURI_CRITICAL_ENTER();
    uint32_t ticks = LL_TIM_GetCounter(TIM17);
    if(LL_TIM_IsActiveFlag_UPDATE(TIM17)) {
        ticks = LL_TIM_GetCounter(TIM17);
        if(rx->timer_ticks < 0x1FFF0000u) rx->timer_ticks += 0x10000u;
    }
    ticks = rx->timer_ticks == 0x1FFF0000u ? 0x1FFFFFFFu : ticks + rx->timer_ticks;
    LL_TIM_SetCounter(TIM17, 0);
    LL_TIM_ClearFlag_UPDATE(TIM17);
    rx->timer_ticks = 0;
    const bool pin_high = furi_hal_gpio_read(si4463->g0_pin);
    FURI_CRITICAL_EXIT();

    if(si4463->async_mirror_pin != NULL) {
        furi_hal_gpio_write(si4463->async_mirror_pin, pin_high);
    }
    if(rx->suppressed || !rx->capture_callback || !ticks) return;
    if(rx->discard_next) {
        rx->discard_next = false;
        return;
    }
    const bool level = !pin_high;
    const uint32_t duration = ticks << 1;
    if(!rx->glitch_us) {
        rx->capture_callback(level, duration, rx->capture_callback_context);
        return;
    }
    if(!rx->pending_valid) {
        rx->pending_valid = true;
        rx->pending_level = level;
        rx->pending_duration = duration;
    } else if(duration < rx->glitch_us || level == rx->pending_level) {
        const uint32_t room = SI4463_CAPTURE_DURATION_MAX - rx->pending_duration;
        rx->pending_duration += duration < room ? duration : room;
    } else {
        rx->capture_callback(
            rx->pending_level, rx->pending_duration, rx->capture_callback_context);
        rx->pending_level = level;
        rx->pending_duration = duration;
    }
}

static void si4463_capture_enable(bool enabled) {
    FURI_CRITICAL_ENTER();
    si4463->async_rx.suppressed = !enabled;
    si4463->async_rx.discard_next = true;
    si4463->async_rx.pending_valid = false;
    si4463->async_rx.timer_ticks = 0;
    LL_TIM_SetCounter(TIM17, 0);
    LL_TIM_ClearFlag_UPDATE(TIM17);
    FURI_CRITICAL_EXIT();
}

void subghz_device_si4463_ext_start_async_rx(
    SubGhzDeviceSi4463ExtCaptureCallback callback,
    void* context) {
    furi_assert(si4463->state == Si4463StateIdle);
    si4463->state = Si4463StateAsyncRx;
    si4463->async_rx.capture_callback = callback;
    si4463->async_rx.capture_callback_context = context;
    si4463->async_rx.timer_ticks = 0;
    si4463->async_rx.pending_valid = false;
    si4463->async_rx.pending_duration = 0;
    si4463->async_rx.suppressed = true;
    si4463->async_rx.glitch_us = si4463_modem_is_ook(&si4463->modem) ?
                                     SI4463_CAPTURE_GLITCH_US :
                                     SI4463_CAPTURE_GLITCH_FSK_US;

    furi_hal_bus_enable(FuriHalBusTIM17);

    LL_TIM_InitTypeDef tim = {0};
    tim.Prescaler = (64 << 1) - 1;
    tim.CounterMode = LL_TIM_COUNTERMODE_UP;
    tim.Autoreload = 0xFFFF;
    tim.ClockDivision = LL_TIM_CLOCKDIVISION_DIV1;
    LL_TIM_Init(TIM17, &tim);
    LL_TIM_SetClockSource(TIM17, LL_TIM_CLOCKSOURCE_INTERNAL);
    LL_TIM_DisableARRPreload(TIM17);
    LL_TIM_DisableDMAReq_TRIG(TIM17);
    LL_TIM_DisableIT_TRIG(TIM17);

    furi_hal_gpio_init(si4463->g0_pin, GpioModeInterruptRiseFall, GpioPullUp, GpioSpeedVeryHigh);
    furi_hal_gpio_remove_int_callback(si4463->g0_pin);

    LL_TIM_SetCounter(TIM17, 0);
    LL_TIM_ClearFlag_UPDATE(TIM17);
    furi_hal_interrupt_set_isr(FuriHalInterruptIdTim1TrgComTim17, si4463_capture_timer_isr, NULL);
    LL_TIM_EnableIT_UPDATE(TIM17);
    LL_TIM_EnableCounter(TIM17);
    si4463_start_debug();
    furi_hal_gpio_add_int_callback(si4463->g0_pin, si4463_capture_isr, context);
    subghz_device_si4463_ext_rx();
    si4463_capture_enable(true);
}

void subghz_device_si4463_ext_stop_async_rx(void) {
    furi_assert(si4463->state == Si4463StateAsyncRx);
    si4463->state = Si4463StateIdle;

    si4463_capture_enable(false);
    subghz_device_si4463_ext_idle();
    furi_hal_spi_acquire(si4463->spi_bus_handle);
    if(si4463->configuration_valid && si4463->frequency_valid &&
       !si4463_program_frequency(si4463->frequency, NULL))
        si4463->configuration_valid = false;
    furi_hal_spi_release(si4463->spi_bus_handle);

    FURI_CRITICAL_ENTER();
    furi_hal_gpio_remove_int_callback(si4463->g0_pin);
    LL_TIM_DisableIT_UPDATE(TIM17);
    furi_hal_interrupt_set_isr(FuriHalInterruptIdTim1TrgComTim17, NULL, NULL);
    furi_hal_bus_disable(FuriHalBusTIM17);
    si4463_stop_debug();
    si4463->async_rx.capture_callback = NULL;
    FURI_CRITICAL_EXIT();
    furi_hal_gpio_init(si4463->g0_pin, GpioModeAnalog, GpioPullNo, GpioSpeedLow);
}

static void si4463_tx_middleware_idle(Si4463TxMw* middleware) {
    middleware->state = Si4463TxMwIdle;
    middleware->is_odd_level = false;
    middleware->adder_duration = SI4463_ASYNC_TX_GUARD_TIME;
}

static uint32_t si4463_tx_middleware_duration(Si4463TxMw* middleware) {
    if(middleware->state == Si4463TxMwReset) return 0;

    while(true) {
        LevelDuration ld = si4463->async_tx.callback(si4463->async_tx.callback_context);
        if(level_duration_is_reset(ld)) {
            middleware->state = Si4463TxMwReset;
            return middleware->is_odd_level ? middleware->adder_duration : 0;
        } else if(level_duration_is_wait(ld)) {
            middleware->is_odd_level = !middleware->is_odd_level;
            uint32_t ret = middleware->adder_duration + SI4463_ASYNC_TX_GUARD_TIME;
            middleware->adder_duration = 0;
            return ret;
        }

        bool is_level = level_duration_get_level(ld);
        if(middleware->state == Si4463TxMwIdle) {
            if(is_level != middleware->is_odd_level) {
                middleware->state = Si4463TxMwRun;
                middleware->is_odd_level = is_level;
                middleware->adder_duration = level_duration_get_duration(ld);
                return SI4463_ASYNC_TX_GUARD_TIME;
            }
            continue;
        }

        if(is_level == middleware->is_odd_level) {
            middleware->adder_duration += level_duration_get_duration(ld);
            continue;
        }
        middleware->is_odd_level = is_level;
        uint32_t ret = middleware->adder_duration;
        middleware->adder_duration = level_duration_get_duration(ld);
        return ret;
    }
}

static void si4463_async_tx_refill(uint32_t* buffer, size_t samples) {
    while(samples > 0) {
        uint32_t duration = si4463_tx_middleware_duration(&si4463->async_tx.middleware);
        if(duration == 0) {
            *buffer = 0;
            buffer++;
            samples--;
            LL_DMA_DisableIT_HT(SI4463_DMA_CH3_DEF);
            LL_DMA_DisableIT_TC(SI4463_DMA_CH3_DEF);
            if(LL_DMA_IsActiveFlag_HT3(SI4463_DMA)) LL_DMA_ClearFlag_HT3(SI4463_DMA);
            if(LL_DMA_IsActiveFlag_TC3(SI4463_DMA)) LL_DMA_ClearFlag_TC3(SI4463_DMA);
            break;
        }
        if(duration < 4) duration = 4;
        *buffer = (duration >> 1) - 1;
        buffer++;
        samples--;
    }
}

static void si4463_async_tx_dma_isr(void* context) {
    UNUSED(context);
#if SI4463_DMA_CH3_CHANNEL == LL_DMA_CHANNEL_3
    if(LL_DMA_IsActiveFlag_HT3(SI4463_DMA)) {
        LL_DMA_ClearFlag_HT3(SI4463_DMA);
        si4463_async_tx_refill(si4463->async_tx.buffer, SI4463_ASYNC_TX_BUFFER_HALF);
    }
    if(LL_DMA_IsActiveFlag_TC3(SI4463_DMA)) {
        LL_DMA_ClearFlag_TC3(SI4463_DMA);
        si4463_async_tx_refill(
            si4463->async_tx.buffer + SI4463_ASYNC_TX_BUFFER_HALF, SI4463_ASYNC_TX_BUFFER_HALF);
    }
#else
#error Update this code. Would you kindly?
#endif
}

static void si4463_async_tx_hw_start(void) {
    furi_hal_gpio_write(si4463->g0_pin, false);
    furi_hal_gpio_init(si4463->g0_pin, GpioModeOutputPushPull, GpioPullNo, GpioSpeedVeryHigh);

    LL_DMA_SetMemoryAddress(SI4463_DMA_CH3_DEF, (uint32_t)si4463->async_tx.buffer);
    LL_DMA_SetPeriphAddress(SI4463_DMA_CH3_DEF, (uint32_t) & (TIM17->ARR));
    LL_DMA_ConfigTransfer(
        SI4463_DMA_CH3_DEF,
        LL_DMA_DIRECTION_MEMORY_TO_PERIPH | LL_DMA_MODE_CIRCULAR | LL_DMA_PERIPH_NOINCREMENT |
            LL_DMA_MEMORY_INCREMENT | LL_DMA_PDATAALIGN_WORD | LL_DMA_MDATAALIGN_WORD |
            LL_DMA_PRIORITY_VERYHIGH);
    LL_DMA_SetDataLength(SI4463_DMA_CH3_DEF, SI4463_ASYNC_TX_BUFFER_FULL);
    LL_DMA_SetPeriphRequest(SI4463_DMA_CH3_DEF, LL_DMAMUX_REQ_TIM17_UP);
    LL_DMA_EnableIT_TC(SI4463_DMA_CH3_DEF);
    LL_DMA_EnableIT_HT(SI4463_DMA_CH3_DEF);
    LL_DMA_EnableChannel(SI4463_DMA_CH3_DEF);
    furi_hal_interrupt_set_isr(SI4463_DMA_CH3_IRQ, si4463_async_tx_dma_isr, NULL);

    furi_hal_bus_enable(FuriHalBusTIM17);
    LL_TIM_SetCounterMode(TIM17, LL_TIM_COUNTERMODE_UP);
    LL_TIM_SetClockDivision(TIM17, LL_TIM_CLOCKDIVISION_DIV1);
    LL_TIM_SetAutoReload(TIM17, 500);
    LL_TIM_SetPrescaler(TIM17, (64 << 1) - 1);
    LL_TIM_SetClockSource(TIM17, LL_TIM_CLOCKSOURCE_INTERNAL);
    LL_TIM_DisableARRPreload(TIM17);

    si4463_tx_middleware_idle(&si4463->async_tx.middleware);
    si4463_async_tx_refill(si4463->async_tx.buffer, SI4463_ASYNC_TX_BUFFER_FULL);

    const GpioPin* gpio = si4463->g0_pin;
    si4463->async_tx.gpio_tx_buff[0] = (uint32_t)gpio->pin << GPIO_NUMBER;
    si4463->async_tx.gpio_tx_buff[1] = gpio->pin;
    LL_DMA_SetMemoryAddress(SI4463_DMA_CH4_DEF, (uint32_t)si4463->async_tx.gpio_tx_buff);
    LL_DMA_SetPeriphAddress(SI4463_DMA_CH4_DEF, (uint32_t) & (gpio->port->BSRR));
    LL_DMA_ConfigTransfer(
        SI4463_DMA_CH4_DEF,
        LL_DMA_DIRECTION_MEMORY_TO_PERIPH | LL_DMA_MODE_CIRCULAR | LL_DMA_PERIPH_NOINCREMENT |
            LL_DMA_MEMORY_INCREMENT | LL_DMA_PDATAALIGN_WORD | LL_DMA_MDATAALIGN_WORD |
            LL_DMA_PRIORITY_HIGH);
    LL_DMA_SetDataLength(SI4463_DMA_CH4_DEF, 2);
    LL_DMA_SetPeriphRequest(SI4463_DMA_CH4_DEF, LL_DMAMUX_REQ_TIM17_UP);
    LL_DMA_EnableChannel(SI4463_DMA_CH4_DEF);

    if(si4463_start_debug()) {
        gpio = si4463->async_mirror_pin;
        si4463->async_tx.debug_gpio_buff[0] = (uint32_t)gpio->pin << GPIO_NUMBER;
        si4463->async_tx.debug_gpio_buff[1] = gpio->pin;
        LL_DMA_SetMemoryAddress(SI4463_DMA_CH5_DEF, (uint32_t)si4463->async_tx.debug_gpio_buff);
        LL_DMA_SetPeriphAddress(SI4463_DMA_CH5_DEF, (uint32_t) & (gpio->port->BSRR));
        LL_DMA_ConfigTransfer(
            SI4463_DMA_CH5_DEF,
            LL_DMA_DIRECTION_MEMORY_TO_PERIPH | LL_DMA_MODE_CIRCULAR | LL_DMA_PERIPH_NOINCREMENT |
                LL_DMA_MEMORY_INCREMENT | LL_DMA_PDATAALIGN_WORD | LL_DMA_MDATAALIGN_WORD |
                LL_DMA_PRIORITY_LOW);
        LL_DMA_SetDataLength(SI4463_DMA_CH5_DEF, 2);
        LL_DMA_SetPeriphRequest(SI4463_DMA_CH5_DEF, LL_DMAMUX_REQ_TIM17_UP);
        LL_DMA_EnableChannel(SI4463_DMA_CH5_DEF);
    }
}

static void si4463_async_tx_hw_stop(void) {
    furi_hal_gpio_write(si4463->g0_pin, false);
    furi_hal_gpio_init(si4463->g0_pin, GpioModeAnalog, GpioPullNo, GpioSpeedLow);
    furi_hal_bus_disable(FuriHalBusTIM17);
    furi_hal_interrupt_set_isr(FuriHalInterruptIdTim1TrgComTim17, NULL, NULL);
    LL_DMA_DeInit(SI4463_DMA_CH3_DEF);
    LL_DMA_DisableChannel(SI4463_DMA_CH4_DEF);
    furi_hal_interrupt_set_isr(SI4463_DMA_CH3_IRQ, NULL, NULL);
    if(si4463_stop_debug()) LL_DMA_DisableChannel(SI4463_DMA_CH5_DEF);
    free(si4463->async_tx.buffer);
    si4463->async_tx.buffer = NULL;
}

bool subghz_device_si4463_ext_start_async_tx(SubGhzDeviceSi4463ExtCallback callback, void* context) {
    furi_assert(si4463->state == Si4463StateIdle);
    furi_assert(callback);
    if(!si4463->frequency_valid) return false;

    furi_hal_spi_acquire(si4463->spi_bus_handle);
    bool prepared = si4463_ensure_awake() && si4463_gpio(SI4463_GPIO_INPUT);
    furi_hal_spi_release(si4463->spi_bus_handle);
    if(!prepared) return false;

    si4463->async_tx.callback = callback;
    si4463->async_tx.callback_context = context;
    si4463->async_tx.buffer = malloc(SI4463_ASYNC_TX_BUFFER_FULL * sizeof(uint32_t));
    si4463_async_tx_hw_start();

    if(!subghz_device_si4463_ext_tx()) {
        si4463_async_tx_hw_stop();
        return false;
    }

    si4463->state = Si4463StateAsyncTx;
    LL_TIM_EnableDMAReq_UPDATE(TIM17);
    LL_TIM_SetCounter(TIM17, 0);
    LL_TIM_EnableCounter(TIM17);
    return true;
}

bool subghz_device_si4463_ext_is_async_tx_complete(void) {
    return (si4463->state == Si4463StateAsyncTx) && (LL_TIM_GetAutoReload(TIM17) == 0);
}

void subghz_device_si4463_ext_stop_async_tx(void) {
    furi_assert(si4463->state == Si4463StateAsyncTx);
    subghz_device_si4463_ext_idle();
    si4463_async_tx_hw_stop();
    si4463->state = Si4463StateIdle;
}
