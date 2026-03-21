#include "rolljam_cc1101_ext.h"
#include <furi_hal_gpio.h>
#include <furi_hal_resources.h>
#include <furi_hal_cortex.h>
#include <furi_hal_power.h>

// ============================================================
// 5V OTG power
// ============================================================

static bool otg_was_enabled   = false;
static bool use_flux_capacitor = false;

void rolljam_ext_set_flux_capacitor(bool enabled) {
    use_flux_capacitor = enabled;
}

static void rolljam_ext_power_on(void) {
    otg_was_enabled = furi_hal_power_is_otg_enabled();
    if(!otg_was_enabled) {
        uint8_t attempts = 0;
        while(!furi_hal_power_is_otg_enabled() && attempts++ < 5) {
            furi_hal_power_enable_otg();
            furi_delay_ms(10);
        }
    }
}

static void rolljam_ext_power_off(void) {
    if(!otg_was_enabled) {
        furi_hal_power_disable_otg();
    }
}

static const GpioPin* pin_mosi = &gpio_ext_pa7;
static const GpioPin* pin_miso = &gpio_ext_pa6;
static const GpioPin* pin_cs   = &gpio_ext_pa4;
static const GpioPin* pin_sck  = &gpio_ext_pb3;
static const GpioPin* pin_gdo0 = &gpio_ext_pb2;
static const GpioPin* pin_amp  = &gpio_ext_pc3;

// ============================================================
// CC1101 Registers
// ============================================================
#define CC_IOCFG2    0x00
#define CC_IOCFG0    0x02
#define CC_FIFOTHR   0x03
#define CC_SYNC1     0x04
#define CC_SYNC0     0x05
#define CC_PKTLEN    0x06
#define CC_PKTCTRL1  0x07
#define CC_PKTCTRL0  0x08
#define CC_FSCTRL1   0x0B
#define CC_FSCTRL0   0x0C
#define CC_FREQ2     0x0D
#define CC_FREQ1     0x0E
#define CC_FREQ0     0x0F
#define CC_MDMCFG4   0x10
#define CC_MDMCFG3   0x11
#define CC_MDMCFG2   0x12
#define CC_MDMCFG1   0x13
#define CC_MDMCFG0   0x14
#define CC_DEVIATN   0x15
#define CC_MCSM1     0x17
#define CC_MCSM0     0x18
#define CC_FOCCFG    0x19
#define CC_AGCCTRL2  0x1B
#define CC_AGCCTRL1  0x1C
#define CC_AGCCTRL0  0x1D
#define CC_FREND0    0x22
#define CC_FSCAL3    0x23
#define CC_FSCAL2    0x24
#define CC_FSCAL1    0x25
#define CC_FSCAL0    0x26
#define CC_TEST2     0x2C
#define CC_TEST1     0x2D
#define CC_TEST0     0x2E
#define CC_PATABLE   0x3E
#define CC_TXFIFO    0x3F

#define CC_PARTNUM   0x30
#define CC_VERSION   0x31
#define CC_MARCSTATE 0x35
#define CC_TXBYTES   0x3A

#define CC_SRES      0x30
#define CC_SCAL      0x33
#define CC_STX       0x35
#define CC_SIDLE     0x36
#define CC_SFTX      0x3B

#define MARC_IDLE    0x01
#define MARC_TX      0x13

// ============================================================
// Band calibration
// ============================================================

typedef struct {
    uint32_t min_freq;
    uint32_t max_freq;
    uint8_t fscal3;
    uint8_t fscal2;
    uint8_t fscal1;
    uint8_t fscal0;
} ExtBandCal;

static const ExtBandCal ext_band_cals[] = {
    { 299000000, 348000000, 0xEA, 0x2A, 0x00, 0x1F },
    { 386000000, 464000000, 0xE9, 0x2A, 0x00, 0x1F },
    { 778000000, 928000000, 0xEA, 0x2A, 0x00, 0x11 },
};
#define EXT_BAND_CAL_COUNT (sizeof(ext_band_cals) / sizeof(ext_band_cals[0]))

static const ExtBandCal* ext_get_band_cal(uint32_t freq) {
    for(size_t i = 0; i < EXT_BAND_CAL_COUNT; i++) {
        if(freq >= ext_band_cals[i].min_freq && freq <= ext_band_cals[i].max_freq)
            return &ext_band_cals[i];
    }
    return &ext_band_cals[1];
}

static inline void spi_delay(void) {
    for(int i = 0; i < 16; i++) __NOP();
}

static inline void cs_lo(void) { furi_hal_gpio_write(pin_cs, false); spi_delay(); }
static inline void cs_hi(void) { spi_delay(); furi_hal_gpio_write(pin_cs, true); spi_delay(); }

static bool wait_miso(uint32_t us) {
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CTRL        |= DWT_CTRL_CYCCNTENA_Msk;
    uint32_t s = DWT->CYCCNT;
    uint32_t t = (SystemCoreClock / 1000000) * us;
    while(furi_hal_gpio_read(pin_miso)) {
        if((DWT->CYCCNT - s) > t) return false;
    }
    return true;
}

static uint8_t spi_byte(uint8_t tx) {
    uint8_t rx = 0;
    for(int8_t i = 7; i >= 0; i--) {
        furi_hal_gpio_write(pin_mosi, (tx >> i) & 0x01);
        spi_delay();
        furi_hal_gpio_write(pin_sck, true);
        spi_delay();
        if(furi_hal_gpio_read(pin_miso)) rx |= (1 << i);
        furi_hal_gpio_write(pin_sck, false);
        spi_delay();
    }
    return rx;
}

static uint8_t cc_strobe(uint8_t cmd) {
    cs_lo();
    if(!wait_miso(5000)) { cs_hi(); return 0xFF; }
    uint8_t s = spi_byte(cmd);
    cs_hi();
    return s;
}

static void cc_write(uint8_t a, uint8_t v) {
    cs_lo();
    if(!wait_miso(5000)) { cs_hi(); return; }
    spi_byte(a); spi_byte(v);
    cs_hi();
}

static uint8_t cc_read_status(uint8_t a) {
    cs_lo();
    if(!wait_miso(5000)) { cs_hi(); return 0xFF; }
    spi_byte(a | 0xC0);
    uint8_t v = spi_byte(0x00);
    cs_hi();
    return v;
}

static void cc_write_burst(uint8_t a, const uint8_t* d, uint8_t n) {
    cs_lo();
    if(!wait_miso(5000)) { cs_hi(); return; }
    spi_byte(a | 0x40);
    for(uint8_t i = 0; i < n; i++) spi_byte(d[i]);
    cs_hi();
}

static bool cc_reset(void) {
    cs_hi(); furi_delay_us(30);
    cs_lo(); furi_delay_us(30);
    cs_hi(); furi_delay_us(50);
    cs_lo();
    if(!wait_miso(10000)) { cs_hi(); return false; }
    spi_byte(CC_SRES);
    if(!wait_miso(100000)) { cs_hi(); return false; }
    cs_hi();
    furi_delay_ms(5);
    FURI_LOG_I(TAG, "EXT: Reset OK");
    return true;
}

static bool cc_check(void) {
    uint8_t p = cc_read_status(CC_PARTNUM);
    uint8_t v = cc_read_status(CC_VERSION);
    FURI_LOG_I(TAG, "EXT: PART=0x%02X VER=0x%02X", p, v);
    return (v == 0x14 || v == 0x04 || v == 0x03);
}

static uint8_t cc_state(void) { return cc_read_status(CC_MARCSTATE) & 0x1F; }
static uint8_t cc_txbytes(void) { return cc_read_status(CC_TXBYTES) & 0x7F; }

static void cc_idle(void) {
    cc_strobe(CC_SIDLE);
    for(int i = 0; i < 500; i++) {
        if(cc_state() == MARC_IDLE) return;
        furi_delay_us(50);
    }
}

static void cc_set_freq(uint32_t f) {
    uint32_t r = (uint32_t)(((uint64_t)f << 16) / 26000000ULL);
    cc_write(CC_FREQ2, (r >> 16) & 0xFF);
    cc_write(CC_FREQ1, (r >>  8) & 0xFF);
    cc_write(CC_FREQ0,  r        & 0xFF);
}

static bool cc_configure_jam(uint32_t freq) {
    const ExtBandCal* cal = ext_get_band_cal(freq);
    FURI_LOG_I(TAG, "EXT: Config OOK jam at %lu Hz", freq);
    cc_idle();
    cc_write(CC_IOCFG0,   0x02);
    cc_write(CC_IOCFG2,   0x2F);
    cc_write(CC_PKTCTRL0, 0x00);
    cc_write(CC_PKTCTRL1, 0x00);
    cc_write(CC_PKTLEN,   0xFF);
    cc_write(CC_FIFOTHR,  0x07);
    cc_write(CC_SYNC1,    0x00);
    cc_write(CC_SYNC0,    0x00);
    cc_set_freq(freq);
    cc_write(CC_FSCTRL1,  0x06);
    cc_write(CC_FSCTRL0,  0x00);
    cc_write(CC_MDMCFG4,  0x85);
    cc_write(CC_MDMCFG3,  0x43);
    cc_write(CC_MDMCFG2,  0x30);
    cc_write(CC_MDMCFG1,  0x00);
    cc_write(CC_MDMCFG0,  0xF8);
    cc_write(CC_DEVIATN,  0x47);
    cc_write(CC_MCSM1,    0x00);
    cc_write(CC_MCSM0,    0x18);
    cc_write(CC_FREND0,   0x11);
    uint8_t pa[8] = {0x00,0xC0,0xC0,0xC0,0xC0,0xC0,0xC0,0xC0};
    cc_write_burst(CC_PATABLE, pa, 8);
    cc_write(CC_FSCAL3,  cal->fscal3);
    cc_write(CC_FSCAL2,  cal->fscal2);
    cc_write(CC_FSCAL1,  cal->fscal1);
    cc_write(CC_FSCAL0,  cal->fscal0);
    cc_write(CC_TEST2,   0x81);
    cc_write(CC_TEST1,   0x35);
    cc_write(CC_TEST0,   0x09);
    cc_idle();
    cc_strobe(CC_SCAL);
    furi_delay_ms(2);
    cc_idle();
    uint8_t st = cc_state();
    FURI_LOG_I(TAG, "EXT: state=0x%02X FSCAL={0x%02X,0x%02X,0x%02X,0x%02X}",
               st, cal->fscal3, cal->fscal2, cal->fscal1, cal->fscal0);
    return (st == MARC_IDLE);
}

static bool cc_configure_jam_fsk(uint32_t freq, bool wide) {
    const ExtBandCal* cal = ext_get_band_cal(freq);
    FURI_LOG_I(TAG, "EXT: Config FSK jam at %lu Hz (wide=%d)", freq, wide);
    cc_idle();
    cc_write(CC_IOCFG0,   0x02);
    cc_write(CC_IOCFG2,   0x2F);
    cc_write(CC_PKTCTRL0, 0x00);
    cc_write(CC_PKTCTRL1, 0x00);
    cc_write(CC_PKTLEN,   0xFF);
    cc_write(CC_FIFOTHR,  0x07);
    cc_write(CC_SYNC1,    0x00);
    cc_write(CC_SYNC0,    0x00);
    cc_set_freq(freq);
    cc_write(CC_FSCTRL1,  0x06);
    cc_write(CC_FSCTRL0,  0x00);
    cc_write(CC_MDMCFG4,  0x85);
    cc_write(CC_MDMCFG3,  0x43);
    cc_write(CC_MDMCFG2,  0x00);
    cc_write(CC_MDMCFG1,  0x00);
    cc_write(CC_MDMCFG0,  0xF8);
    cc_write(CC_DEVIATN,  wide ? 0x47 : 0x15);
    cc_write(CC_MCSM1,    0x00);
    cc_write(CC_MCSM0,    0x18);
    cc_write(CC_FREND0,   0x10);
    uint8_t pa[8] = {0xC0,0xC0,0xC0,0xC0,0xC0,0xC0,0xC0,0xC0};
    cc_write_burst(CC_PATABLE, pa, 8);
    cc_write(CC_FSCAL3,  cal->fscal3);
    cc_write(CC_FSCAL2,  cal->fscal2);
    cc_write(CC_FSCAL1,  cal->fscal1);
    cc_write(CC_FSCAL0,  cal->fscal0);
    cc_write(CC_TEST2,   0x81);
    cc_write(CC_TEST1,   0x35);
    cc_write(CC_TEST0,   0x09);
    cc_idle();
    cc_strobe(CC_SCAL);
    furi_delay_ms(2);
    cc_idle();
    return (cc_state() == MARC_IDLE);
}

static void ext_gpio_init_spi_pins(void) {
    furi_hal_gpio_init(pin_cs,   GpioModeOutputPushPull, GpioPullNo, GpioSpeedVeryHigh);
    furi_hal_gpio_write(pin_cs, true);
    furi_hal_gpio_init(pin_sck,  GpioModeOutputPushPull, GpioPullNo, GpioSpeedVeryHigh);
    furi_hal_gpio_write(pin_sck, false);
    furi_hal_gpio_init(pin_mosi, GpioModeOutputPushPull, GpioPullNo, GpioSpeedVeryHigh);
    furi_hal_gpio_write(pin_mosi, false);
    furi_hal_gpio_init(pin_miso, GpioModeInput, GpioPullUp, GpioSpeedVeryHigh);
    furi_hal_gpio_init(pin_gdo0, GpioModeInput, GpioPullDown, GpioSpeedVeryHigh);
}

static void ext_gpio_deinit_spi_pins(void) {
    furi_hal_gpio_init(pin_cs,   GpioModeAnalog, GpioPullNo, GpioSpeedLow);
    furi_hal_gpio_init(pin_sck,  GpioModeAnalog, GpioPullNo, GpioSpeedLow);
    furi_hal_gpio_init(pin_mosi, GpioModeAnalog, GpioPullNo, GpioSpeedLow);
    furi_hal_gpio_init(pin_miso, GpioModeAnalog, GpioPullNo, GpioSpeedLow);
    furi_hal_gpio_init(pin_gdo0, GpioModeAnalog, GpioPullNo, GpioSpeedLow);
}

void rolljam_ext_gpio_init(void) {
    FURI_LOG_I(TAG, "EXT GPIO init (deferred to jam thread)");
    if(use_flux_capacitor) {
        furi_hal_gpio_init_simple(pin_amp, GpioModeOutputPushPull);
        furi_hal_gpio_write(pin_amp, false);
    }
}

void rolljam_ext_gpio_deinit(void) {
    if(use_flux_capacitor) {
        furi_hal_gpio_write(pin_amp, false);
        furi_hal_gpio_init_simple(pin_amp, GpioModeAnalog);
    }

    FURI_LOG_I(TAG, "EXT GPIO deinit");
}

// ============================================================
// Noise pattern & jam helpers
// ============================================================

static void jam_start_tx(const uint8_t* pattern, uint8_t len) {
    cc_strobe(CC_SFTX);
    furi_delay_ms(1);
    cc_write_burst(CC_TXFIFO, pattern, len);
    cc_strobe(CC_STX);
    furi_delay_ms(5);
}

static int32_t jam_thread_worker(void* context) {
    RollJamApp* app = context;

    bool is_fsk       = (app->mod_index == ModIndex_FM238 || app->mod_index == ModIndex_FM476);
    uint32_t freq_pos = app->frequency + app->jam_offset_hz;
    uint32_t freq_neg = app->frequency - app->jam_offset_hz;

    FURI_LOG_I(TAG, "JAM thread start: target=%lu offset=%lu FSK=%d",
               app->frequency, app->jam_offset_hz, is_fsk);

    ext_gpio_init_spi_pins();
    furi_delay_ms(5);

    if(!cc_reset()) {
        FURI_LOG_E(TAG, "JAM: Reset failed — CC1101 externo no conectado o mal cableado");
        ext_gpio_deinit_spi_pins();
        app->jamming_active = false;
        return -1;
    }
    if(!cc_check()) {
        FURI_LOG_E(TAG, "JAM: Chip no detectado");
        ext_gpio_deinit_spi_pins();
        app->jamming_active = false;
        return -1;
    }

    bool jam_ok;
    if(app->mod_index == ModIndex_FM238)
        jam_ok = cc_configure_jam_fsk(freq_pos, false);
    else if(app->mod_index == ModIndex_FM476)
        jam_ok = cc_configure_jam_fsk(freq_pos, true);
    else
        jam_ok = cc_configure_jam(freq_pos);

    if(!jam_ok) {
        FURI_LOG_E(TAG, "JAM: Config failed");
        ext_gpio_deinit_spi_pins();
        app->jamming_active = false;
        return -1;
    }

    static const uint8_t noise_pattern[62] = {
        0xAA,0x55,0xAA,0x55,0xAA,0x55,0xAA,0x55,
        0xAA,0x55,0xAA,0x55,0xAA,0x55,0xAA,0x55,
        0xAA,0x55,0xAA,0x55,0xAA,0x55,0xAA,0x55,
        0xAA,0x55,0xAA,0x55,0xAA,0x55,0xAA,0x55,
        0xAA,0x55,0xAA,0x55,0xAA,0x55,0xAA,0x55,
        0xAA,0x55,0xAA,0x55,0xAA,0x55,0xAA,0x55,
        0xAA,0x55,0xAA,0x55,0xAA,0x55,0xAA,0x55,
        0xAA,0x55
    };

    if(use_flux_capacitor) furi_hal_gpio_write(pin_amp, true);
    jam_start_tx(noise_pattern, 62);

    uint8_t st = cc_state();
    if(st != MARC_TX) {
        cc_idle();
        jam_start_tx(noise_pattern, 62);
        st = cc_state();
        if(st != MARC_TX) {
            FURI_LOG_E(TAG, "JAM: Cannot enter TX (state=0x%02X)", st);
            if(use_flux_capacitor) furi_hal_gpio_write(pin_amp, false);
            ext_gpio_deinit_spi_pins();
            app->jamming_active = false;
            return -1;
        }
    }

    FURI_LOG_I(TAG, "JAM: *** ACTIVE *** freq_pos=%lu", freq_pos);

    uint32_t loops      = 0;
    uint32_t underflows = 0;
    uint32_t refills    = 0;
    bool on_pos         = true;

    while(app->jam_thread_running) {
        loops++;

        if(is_fsk && (loops % 4 == 0)) {
            cc_idle();
            cc_strobe(CC_SFTX);
            furi_delay_us(100);
            on_pos = !on_pos;
            cc_set_freq(on_pos ? freq_pos : freq_neg);
            cc_write_burst(CC_TXFIFO, noise_pattern, 62);
            cc_strobe(CC_STX);
            furi_delay_ms(1);
            continue;
        }

        st = cc_state();
        if(st != MARC_TX) {
            underflows++;
            cc_idle();
            cc_strobe(CC_SFTX);
            furi_delay_us(100);
            cc_write_burst(CC_TXFIFO, noise_pattern, 62);
            cc_strobe(CC_STX);
            furi_delay_ms(1);
            continue;
        }

        uint8_t txb = cc_txbytes();
        if(txb < 20) {
            uint8_t space = 62 - txb;
            if(space > 50) space = 50;
            cc_write_burst(CC_TXFIFO, noise_pattern, space);
            refills++;
        }

        if(loops % 500 == 0) {
            FURI_LOG_I(TAG, "JAM: loops=%lu uf=%lu refills=%lu txb=%d",
                       loops, underflows, refills, cc_txbytes());
        }

        furi_delay_ms(50);
    }

    cc_idle();
    if(use_flux_capacitor) furi_hal_gpio_write(pin_amp, false);
    cc_write(CC_IOCFG2, 0x2E);

    ext_gpio_deinit_spi_pins();

    FURI_LOG_I(TAG, "JAM: STOPPED (loops=%lu uf=%lu refills=%lu)", loops, underflows, refills);
    return 0;
}

// ============================================================
// Public API
// ============================================================

void rolljam_jammer_start(RollJamApp* app) {
    if(app->jamming_active) return;

    app->jam_frequency     = app->frequency + app->jam_offset_hz;
    app->jam_thread_running = true;
    app->jamming_active     = true;

    rolljam_ext_power_on();
    furi_delay_ms(50);

    rolljam_ext_gpio_init();

    app->jam_thread = furi_thread_alloc_ex("RJ_Jam", 4096, jam_thread_worker, app);
    furi_thread_start(app->jam_thread);

    FURI_LOG_I(TAG, ">>> JAMMER THREAD STARTED <<<");
}

void rolljam_jammer_stop(RollJamApp* app) {
    if(!app->jamming_active) return;

    app->jam_thread_running = false;
    furi_thread_join(app->jam_thread);
    furi_thread_free(app->jam_thread);
    app->jam_thread = NULL;

    rolljam_ext_gpio_deinit();
    rolljam_ext_power_off();
    app->jamming_active = false;

    FURI_LOG_I(TAG, ">>> JAMMER STOPPED <<<");
}
