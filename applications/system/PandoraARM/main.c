/* Pandora ARM - emulator of the REAL Pandora D-605 keyfob firmware (EFM32
 * Cortex-M3) for Flipper Zero.
 *
 * The keyfob firmware runs UNMODIFIED on the Thumb-2 interpreter
 * (lib/thumbcore). This app emulates the hardware the firmware expects:
 *   - OLED UC16xx/ST7528 (framebuffer 92col x 16pag = 1472B): the firmware
 *     writes it over SPI; the framebuffer lives in RAM (0x20001152 pandora /
 *     0x200005DA pmax), we read it and paint it on the Flipper 128x64 screen.
 *   - 6 buttons (GPIO DIN, pull-up, 0=pressed). BLACK screen until the
 *     unlock knock-code (blind button sequence).
 *   - Si4432 (SPI model), timers, CMU, ADC: minimal stubs so the fw does not
 *     hang. Minimal NVIC (timer tick) to wake up the WFI.
 *
 * Faithfully ported from the reference emulator emu/pandora_tui.py (verified).
 * FlipperGB-style frontend (direct-draw, poll GPIO, safe_malloc, yield).
 */

#include <furi.h>
#include <furi_hal.h>
#include <furi_hal_subghz.h>
#include <gui/gui.h>
#include <gui/view_dispatcher.h>
#include <gui/modules/submenu.h>
#include <gui/modules/widget.h>
#include <input/input.h>
#include <dialogs/dialogs.h>
#include <storage/storage.h>

#include <stdlib.h>
#include <string.h>

#include "lib/thumbcore/thumb_core.h"

#define TAG "PandoraARM"

#define SCREEN_W 128
#define SCREEN_H 64
#define FB_SIZE (SCREEN_W * SCREEN_H / 8) /* 1024 bytes */

/* Keyfob OLED: 92 columns x 16 pages = 1472 bytes (panel 92x64 2bpp) */
#define OLED_COLS 92
#define OLED_PAGES 16
#define OLED_FB_SIZE (OLED_COLS * OLED_PAGES)

/* Memory map of the emulated EFM32 (NOT the Flipper; PA_ prefix to avoid
 * clashing with the PA_FLASH_BASE/PA_FLASH_SIZE from the Flipper's own STM32WB CMSIS). */
#define PA_FLASH_BASE 0x00000000u
#define PA_FLASH_SIZE 0x00042000u /* 264 KB (covers pandora 0x3FFFF + margin) */
#define PA_RAM_BASE 0x20000000u
#define PA_RAM_SIZE 0x00008000u /* 32 KB */

#define ALLOC_MARGIN 1024u
#define FRAME_US 30000u /* ~33 Hz UI refresh */

/* Window (in instructions) where the knock scanner is active after boot.
 * The keyfob enters low power afterwards; that is why the unlock is done at
 * startup (same as the Python TUI fix). */
#define KNOCK_SETTLE_INSN 250000ull
#define KNOCK_STEP_BUDGET 4000000ull
#define KNOCK_RELEASE_AFTER 4

/* --- POST-unlock navigation (nav_button) tuning. Mirrors pandora_tui.py:
 *   hold_reads=40 (short click), long_reads=120 (long press), budget 4M insn,
 *   adaptive settle up to 500k insn that stops as soon as the framebuffer
 *   changes (UI redrew) -> ~0.2s response instead of ~1s. */
#define NAV_HOLD_READS 40u
#define NAV_LONG_READS 120u
#define NAV_BUDGET_INSN 4000000ull
#define NAV_SETTLE_INSN 500000ull
#define NAV_SETTLE_MIN_INSN 120000ull /* don't cut before this even on fb change */

/* Physical hold detection (Flipper keys). Hold > ~500ms = long press of the
 * corresponding keyfob button. Flipper UP held > 1s = EXIT the app (Button 1
 * has no hold function on the real keyfob, so it is safe to use it for exit).
 * Flipper BACK held > 1s = cycle the RF bridge (OFF/TX/RX) without stealing a
 * keyfob button. */
#define HOLD_MS 500u
#define EXIT_HOLD_MS 1000u
#define RF_HOLD_MS 1000u

static void* safe_malloc(size_t size) {
    if(memmgr_heap_get_max_free_block() < size + ALLOC_MARGIN) return NULL;
    return malloc(size);
}

/* ------------------------------------------------------------ HW profiles */

typedef struct {
    const char* name;
    uint32_t flash_used; /* real size of the code in flash (only this is allocated,
                          * NOT the full 264KB: the Flipper has little heap) */
    uint32_t fb_addr; /* framebuffer in RAM */
    /* buttons (port, pin) */
    uint8_t up_port, up_pin;
    uint8_t down_port, down_pin;
    uint8_t ok_port, ok_pin;
    uint8_t back_port, back_pin;
    uint8_t b5_port, b5_pin;
    uint8_t b6_port, b6_pin;
    /* knock-code: up to 8 steps, terminated with 0xFF */
    uint8_t knock[8];
    uint8_t knock_len;
    uint8_t gate; /* button of the 2nd gate (BTN_* index) */
    uint32_t scan_head; /* head of the knock scanner */
    uint32_t spin_heads[8]; /* heads wait-release */
    uint8_t spin_count;
    uint32_t unlock_pc; /* bl unlock routine = completed */
    uint32_t nav_reader; /* read_gpio_pin inlined used POST-unlock (nav_button):
                          * pandora=0x97ac, pmax=0x96ec. r0=port, r1=pin. */

    /* --- RF bridge (software-demod; see EMU_MAP_pandora.md / arm_pandora.py) --- */
    bool rf_sw_demod; /* true: SW demodulation via ISR (pandora); false (pmax): HW FIFO */
    uint32_t demod_isr; /* demod GPIO ISR (0x09554 pandora) */
    uint8_t data_port; /* Si4432 DATA pin (GPIO port): pandora=1 (B) */
    uint8_t data_pin; /* DATA pin: pandora=0 */
    uint32_t timer1_cnt; /* TIMER1_CNT MMIO (0x40010424) = width timer */
    uint32_t rx_struct; /* base of the RX structure (0x20000000) */
    uint32_t rx_gate; /* [+0xF] active gate (!=0xFF) */
    uint32_t rx_mode; /* decoder selector (0x200005BC) */
    uint32_t rx_pending; /* pending data flag (0x200005C0); 0xFF = completed */
    uint32_t rx_state; /* id of the decoder that completed (0x200005C1) */
    uint32_t rx_result; /* decoded frame buffer (0x20001714) */
    uint8_t rx_scratch_sp_valid; /* 1 if the scratch SP should be used to call the ISR */
} ArmProfile;

/* logical button indices */
enum { B_UP = 0, B_DOWN, B_OK, B_BACK, B5, B6, BTN_COUNT };

static const ArmProfile PROFILE_PANDORA = {
    .name = "pandora",
    .flash_used = 0x13800u, /* code up to 0x135FF (~77KB) + margin */
    .fb_addr = 0x20001152u,
    .up_port = 5, .up_pin = 7,
    .down_port = 5, .down_pin = 5,
    .ok_port = 2, .ok_pin = 9,
    .back_port = 2, .back_pin = 7,
    .b5_port = 0, .b5_pin = 1,
    .b6_port = 0, .b6_pin = 6,
    .knock = {B_UP, B_OK, B_OK, B_UP, B_UP, B_DOWN, 0xFF, 0xFF},
    .knock_len = 6,
    .gate = B5,
    .scan_head = 0xFDFAu,
    .spin_heads = {0xFE5E, 0xFE82, 0xFE9A, 0xFEB2, 0xFECC, 0xFEF4, 0xFF12, 0},
    .spin_count = 7,
    .unlock_pc = 0xD108u,
    .nav_reader = 0x97ACu,
    /* RF bridge: pandora uses SW demod (ISR 0x09554), DATA pin = GPIO B0 */
    .rf_sw_demod = true,
    .demod_isr = 0x09554u,
    .data_port = 1, /* port B */
    .data_pin = 0,
    .timer1_cnt = 0x40010424u,
    .rx_struct = 0x20000000u,
    .rx_gate = 0x2000000Fu,
    .rx_mode = 0x200005BCu,
    .rx_pending = 0x200005C0u,
    .rx_state = 0x200005C1u,
    .rx_result = 0x20001714u,
    .rx_scratch_sp_valid = 1,
};
static const ArmProfile PROFILE_PANDORAMAX = {
    .name = "pandoramax",
    .flash_used = 0x15400u, /* code up to 0x150FF (~84KB) + margin */
    .fb_addr = 0x200005DAu,
    .up_port = 5, .up_pin = 7,
    .down_port = 5, .down_pin = 5,
    .ok_port = 2, .ok_pin = 9,
    .back_port = 0, .back_pin = 15,
    .b5_port = 0, .b5_pin = 1,
    .b6_port = 0, .b6_pin = 6,
    .knock = {B_UP, B_OK, B_DOWN, B_OK, B_UP, B_DOWN, 0xFF, 0xFF},
    .knock_len = 6,
    .gate = B5,
    .scan_head = 0x112E4u,
    .spin_heads = {0x1135A, 0x11382, 0x1139A, 0x113B2, 0x113CA, 0x113E2, 0x1144C, 0x11464},
    .spin_count = 8,
    .unlock_pc = 0xD6E4u,
    .nav_reader = 0x96ECu,
    /* pmax: Si4432 in PACKET/FIFO mode (HW demod), there is NO reliable SW ISR
     * to inject into; the software-demod RX bridge is disabled on this profile.
     * TX (bit-bang + rf_tx_on detection) does work the same way. */
    .rf_sw_demod = false,
    .demod_isr = 0,
    .data_port = 1,
    .data_pin = 0,
    .timer1_cnt = 0x40010424u,
    .rx_struct = 0x20000000u,
    .rx_gate = 0x2000000Fu,
    .rx_mode = 0x200005BCu,
    .rx_pending = 0x200005C0u,
    .rx_state = 0x200005C1u,
    .rx_result = 0x20001714u,
    .rx_scratch_sp_valid = 0,
};

/* ------------------------------------------------------------ Si4432 model */

typedef struct {
    uint8_t regs[0x80];
    uint8_t mode; /* 0 idle, 1 ready, 2 rx, 3 tx (derived from REG[0x07]) */
} Si4432;

static void si4432_init(Si4432* s) {
    memset(s->regs, 0, sizeof(s->regs));
    s->regs[0x00] = 0x08;
    s->regs[0x01] = 0x06;
    s->regs[0x26] = 0x60;
    s->mode = 0;
}
static uint8_t si4432_read(Si4432* s, uint8_t reg) {
    reg &= 0x7F;
    uint8_t v = s->regs[reg];
    if(reg == 0x03 || reg == 0x04) s->regs[reg] = 0;
    return v;
}
/* Returns the mode if it changed (so the frontend detects rf_tx_on/rf_rx_on),
 * -1 if it did not change. REG[0x07] (Operating Mode): bit3=TX, bit2=RX, bit0=READY. */
static int si4432_write(Si4432* s, uint8_t reg, uint8_t val) {
    reg &= 0x7F;
    s->regs[reg] = val;
    if(reg == 0x07) {
        uint8_t nm = s->mode;
        if(val & 0x08)
            nm = 3; /* TX */
        else if(val & 0x04)
            nm = 2; /* RX */
        else if(val & 0x01)
            nm = 1; /* READY */
        else
            nm = 0; /* IDLE */
        if(nm != s->mode) {
            s->mode = nm;
            return (int)nm;
        }
    }
    return -1;
}

/* ------------------------------------------------------------ app state */

enum {
    KBIT_UP = 1 << 0,
    KBIT_DOWN = 1 << 1,
    KBIT_LEFT = 1 << 2,
    KBIT_RIGHT = 1 << 3,
    KBIT_OK = 1 << 4,
    KBIT_BACK = 1 << 5,
};

/* Physical Flipper keys we track for hold-duration. Index into the key-tracking
 * arrays. NOTE: Flipper LEFT is mapped to keyfob Button 4 (BACK pin). */
enum {
    FK_UP = 0, /* Flipper UP    -> Button 1 (UP pin);   long hold = EXIT app */
    FK_OK, /* Flipper OK    -> Button 2 (OK pin) */
    FK_DOWN, /* Flipper DOWN  -> Button 3 (DOWN pin) */
    FK_LEFT, /* Flipper LEFT  -> Button 4 (BACK pin) */
    FK_RIGHT, /* Flipper RIGHT -> Button 5 (B5 pin) */
    FK_BACK, /* Flipper BACK  -> Button 6 (B6 pin);  long hold = RF cycle */
    FK_COUNT,
};

/* Maps a tracked Flipper key (FK_*) to the keyfob logical button (B_*). */
static const uint8_t FK_TO_BTN[FK_COUNT] = {
    [FK_UP] = B_UP,
    [FK_OK] = B_OK,
    [FK_DOWN] = B_DOWN,
    [FK_LEFT] = B_BACK,
    [FK_RIGHT] = B5,
    [FK_BACK] = B6,
};

/* Maps a tracked Flipper key (FK_*) to its KBIT_* mask bit. */
static const uint8_t FK_TO_KBIT[FK_COUNT] = {
    [FK_UP] = KBIT_UP,
    [FK_OK] = KBIT_OK,
    [FK_DOWN] = KBIT_DOWN,
    [FK_LEFT] = KBIT_LEFT,
    [FK_RIGHT] = KBIT_RIGHT,
    [FK_BACK] = KBIT_BACK,
};

#define SPI_RX_QUEUE_LEN 64

/* --- CC1101 bridge (adapted from PandoraPIC) --- */
/* TX: the ARM firmware enables rf_tx_on (Si4432 REG[0x07]=0x08) and bit-bangs
 * the DATA pin (GPIO DOUT port B bit0). We capture each edge with its duration
 * (measured by ninsn between toggles) in a LevelDuration ring and replay it
 * through furi_hal_subghz_start_async_tx. */
#define TX_RING_LEN 2048u
/* RX: furi_hal_subghz_start_async_rx delivers (level,duration_us) per edge
 * from IRQ. It is queued thread-safe; in the main loop it is injected into the
 * firmware demodulator (ISR 0x09554) replicating feed_sub from arm_pandora.py. */
#define RX_QUEUE_LEN 512u

/* ninsn->us conversion for the bit-bang TX. The keyfob is not real-time under
 * the interpreter; the scale is approximate and only affects the replayed
 * cadence, not the logical content of the frame. 1 insn ~= 1 us as an
 * adjustable base. */
#define ARM_INSN_US_NUM 1u
#define ARM_INSN_US_DEN 1u

/* RF bridge modes of the overlay (Left cycles). */
typedef enum {
    RfModeOff = 0,
    RfModeTx,
    RfModeRx,
} RfMode;

typedef struct {
    bool level;
    uint32_t duration; /* us */
} RfEdge;

typedef struct {
    ThumbCore* cpu;
    const ArmProfile* prof;
    uint8_t* flash; /* FLASH buffer (safe_malloc) */
    uint8_t* ram; /* RAM buffer 32KB */

    Si4432 si;
    /* SPI RX queue (Si4432 responses) */
    uint8_t spi_rx[SPI_RX_QUEUE_LEN];
    uint8_t spi_rx_head, spi_rx_tail;
    int spi_phase; /* -1 idle, 0 pending-after-addr */
    uint8_t spi_kind; /* 'w'/'r' */
    uint8_t spi_reg;

    /* GPIO */
    uint16_t gpio_din[16]; /* din per port (bit=1 released) */
    uint16_t gpio_dout[16];

    uint16_t timer_cnt[4];

    /* minimal NVIC */
    uint8_t in_irq;
    uint8_t irq_rr;
    uint32_t tick_count;
    uint64_t last_tick_insn;
    uint32_t wfi_pc_hint;

    /* knock FSM (PC-synchronized button injection) */
    const uint8_t* knock_seq;
    uint8_t knock_len;
    uint8_t knock_idx;
    uint8_t knock_sub; /* 0 press, 1 waitrelease */
    uint8_t knock_cur;
    uint8_t knock_spins;
    uint8_t knock_active;
    uint8_t unlocked;
    int max_step;

    /* nav FSM (POST-unlock button injection, synchronized with nav_reader).
     * Replica of nav_button()/_nav_hook in pandora_tui.py. Shares the SINGLE
     * per-instruction hook (code_hook) with the knock FSM: code_hook calls
     * nav_step() when nav_active, exactly as it calls knock_step() when
     * knock_active (the core only exposes ONE thumb_set_hook). */
    uint8_t nav_active; /* 1 while a nav_button injection is in progress */
    uint8_t nav_phase; /* 0 press, 1 hold, 2 done */
    uint8_t nav_port; /* GPIO port of the button being injected */
    uint8_t nav_pin; /* GPIO pin of the button being injected */
    uint8_t nav_btn; /* logical BTN_* index being injected */
    uint32_t nav_reads; /* count of reader re-reads of THIS pin during hold */
    uint32_t nav_reads_target; /* hold_reads (short) or long_reads (long press) */

    /* buttons (logical mask) */
    uint8_t btn_pressed[BTN_COUNT];

    /* Flipper framebuffer */
    uint8_t screen[FB_SIZE];
    FuriMutex* fb_mutex;
    Gui* gui;
    Canvas* canvas;

    bool radio_on;
    uint32_t frequency;

    /* --- CC1101 RF bridge (adapted from PandoraPIC) --- */
    RfMode rf_mode; /* OFF / TX / RX */

    /* TX bridge: ring of edges captured from the firmware DATA pin */
    RfEdge tx_ring[TX_RING_LEN];
    volatile uint32_t tx_head; /* producer (main loop / mmio_write) */
    volatile uint32_t tx_tail; /* consumer (async_tx callback) */
    bool tx_capturing; /* the firmware is in TX mode (rf_tx_on) */
    bool tx_last_level; /* last DATA pin level */
    uint64_t tx_last_insn; /* ninsn at the last edge */
    bool tx_active; /* async_tx in progress */
    uint32_t tx_frames; /* counter of logged TX frames */
    uint32_t tx_edges_frame; /* accumulated edges of the current frame */

    /* RX bridge: thread-safe queue fed from the CC1101 IRQ */
    FuriMessageQueue* rx_queue;
    bool rx_running; /* async_rx active */
    bool rx_primed; /* receiver RAM state prepared (prime_rx) */
    bool rx_prev_level; /* previous level for the transition feed */
    bool rx_feeding; /* 1 while the demod ISR runs (TIMER1=pulse_ticks) */
    uint32_t rx_pulse_ticks; /* TIMER1_CNT to present during the feed (dur*6) */
    uint32_t rx_events; /* counter of injected RX edges */
    uint32_t rx_frames; /* frames decoded by the firmware */

    volatile bool exit_requested;
    volatile bool menu_requested;
    bool menu_active;

    uint64_t ninsn;
    uint32_t pc;
    uint32_t next_pc;

    /* --- user-defined knock-code (menu) --- */
    uint8_t knock_user[16]; /* button sequence (B_* indices) */
    uint8_t knock_user_len; /* 0 = use the profile default */

    /* --- phase 1 menu (view_dispatcher) --- */
    ViewDispatcher* view_dispatcher;
    Submenu* submenu;
    Widget* about_widget; /* About screen (scrollable text) */
    FuriString* fw_path; /* chosen firmware path */
    char fw_name[32]; /* short name for the label */
} AppState;

static volatile uint32_t s_fb_cb_inflight = 0;

static void wait_inflight_zero(volatile uint32_t* counter) {
    while(__atomic_load_n(counter, __ATOMIC_ACQUIRE) != 0) {
        furi_delay_ms(1);
    }
}

/* RF bridge fwd decls (used from the MMIO callbacks) */
static void tx_on_si_mode(AppState* app, int new_mode);
static void tx_capture_data_edge(AppState* app, bool level);

/* ------------------------------------------------------------ buttons */

static void btn_port_pin(const ArmProfile* p, int btn, uint8_t* port, uint8_t* pin) {
    switch(btn) {
    case B_UP: *port = p->up_port; *pin = p->up_pin; break;
    case B_DOWN: *port = p->down_port; *pin = p->down_pin; break;
    case B_OK: *port = p->ok_port; *pin = p->ok_pin; break;
    case B_BACK: *port = p->back_port; *pin = p->back_pin; break;
    case B5: *port = p->b5_port; *pin = p->b5_pin; break;
    case B6: *port = p->b6_port; *pin = p->b6_pin; break;
    default: *port = 0; *pin = 0; break;
    }
}

static void set_button(AppState* app, int btn, bool pressed) {
    if(btn < 0 || btn >= BTN_COUNT) return;
    uint8_t port, pin;
    btn_port_pin(app->prof, btn, &port, &pin);
    if(pressed)
        app->gpio_din[port] &= (uint16_t)~(1u << pin); /* 0 = pressed */
    else
        app->gpio_din[port] |= (uint16_t)(1u << pin); /* 1 = released */
    app->btn_pressed[btn] = pressed ? 1 : 0;
}

static void init_buttons_released(AppState* app) {
    for(int i = 0; i < 16; i++) app->gpio_din[i] = 0xFFFF; /* pull-up everything released */
    for(int b = 0; b < BTN_COUNT; b++) app->btn_pressed[b] = 0;
}

/* ------------------------------------------------------------ MMIO (callbacks) */

static uint32_t mmio_read(void* ctx, uint32_t addr, int size) {
    AppState* app = (AppState*)ctx;
    uint32_t base = addr & 0xFFFFF000u;
    uint32_t off = addr & 0xFFFu;
    uint32_t v = 0;
    /* Unallocated flash (above flash_used and below 0x42000): the real firmware
     * would see 0xFF there. We only allocate the used code to save heap; we
     * return 0xFF so that reads of that region are faithful (not 0). */
    if(addr < 0x42000u && addr >= app->prof->flash_used) {
        return (size == 1) ? 0xFFu : (size == 2) ? 0xFFFFu : 0xFFFFFFFFu;
    }
    if(base == 0x4000C000u) { /* USART0 (SPI Si4432) */
        if(off == 0x10) {
            v = (1u << 6) | (1u << 5); /* TXBL + RXDATAV ready */
        } else if(off == 0x1C) { /* RXDATA */
            if(app->spi_rx_head != app->spi_rx_tail) {
                v = app->spi_rx[app->spi_rx_head];
                app->spi_rx_head = (uint8_t)((app->spi_rx_head + 1) % SPI_RX_QUEUE_LEN);
            }
        }
    } else if(base == 0x400C4000u) { /* 2nd USART (EEPROM/config) */
        if(off == 0x10) v = (1u << 6) | (1u << 5);
    } else if(addr >= 0x40006000u && addr < 0x40006000u + 36u * 16u) { /* GPIO */
        uint32_t port = (addr - 0x40006000u) / 36u;
        uint32_t poff = (addr - 0x40006000u) - port * 36u;
        if(poff == 0x1C && port < 16) v = app->gpio_din[port]; /* DIN */
    } else if(base == 0x40002000u) { /* ADC0 (pmax reads the battery) */
        if(off == 0x08)
            v = (1u << 16); /* SINGLEDV ready */
        else if(off == 0x24)
            v = 0x0C00; /* data ~3.9V */
    } else if(base == 0x40080000u || base == 0x400C8000u) { /* CMU */
        v = 0;
    } else if(base == 0x40010000u) { /* TIMER0..3 */
        uint32_t tn = off >> 10;
        uint32_t sub = off & 0x3FFu;
        if(sub == 0x24 && tn < 4) {
            /* During the RX feed the demod ISR reads TIMER1.CNT as the WIDTH of
             * the half-period (see arm_pandora.py: TIMER1_CNT = dur_us*6). */
            if(tn == 1 && app->rx_feeding) {
                v = app->rx_pulse_ticks & 0xFFFFu;
            } else {
                app->timer_cnt[tn] = (uint16_t)(app->timer_cnt[tn] + 0x2000u);
                v = app->timer_cnt[tn];
            }
        }
    }
    (void)size;
    return v;
}

static void spi_byte(AppState* app, uint8_t b) {
    if(app->spi_phase < 0) {
        if(b & 0x80) {
            app->spi_kind = 'w';
            app->spi_reg = b & 0x7F;
        } else {
            app->spi_kind = 'r';
            app->spi_reg = b & 0x7F;
        }
        app->spi_phase = 0;
        /* junk echo when sending the address */
        app->spi_rx[app->spi_rx_tail] = 0;
        app->spi_rx_tail = (uint8_t)((app->spi_rx_tail + 1) % SPI_RX_QUEUE_LEN);
    } else {
        if(app->spi_kind == 'w') {
            int nm = si4432_write(&app->si, app->spi_reg, b);
            if(nm >= 0) tx_on_si_mode(app, nm); /* rf_tx_on / rf_rx_on detected */
        } else {
            uint8_t rv = si4432_read(&app->si, app->spi_reg);
            app->spi_rx[app->spi_rx_tail] = rv;
            app->spi_rx_tail = (uint8_t)((app->spi_rx_tail + 1) % SPI_RX_QUEUE_LEN);
        }
        app->spi_phase = -1;
    }
}

static void mmio_write(void* ctx, uint32_t addr, uint32_t val, int size) {
    AppState* app = (AppState*)ctx;
    uint32_t base = addr & 0xFFFFF000u;
    uint32_t off = addr & 0xFFFu;
    if(base == 0x4000C000u && off == 0x34) { /* USART0 TXDATA -> Si4432 */
        spi_byte(app, (uint8_t)(val & 0xFF));
    } else if(addr >= 0x40006000u && addr < 0x40006000u + 36u * 16u) { /* GPIO DOUT* */
        uint32_t port = (addr - 0x40006000u) / 36u;
        uint32_t poff = (addr - 0x40006000u) - port * 36u;
        if(port < 16) {
            if(poff == 0x10)
                app->gpio_dout[port] &= (uint16_t)~val; /* DOUTCLR */
            else if(poff == 0x14)
                app->gpio_dout[port] |= (uint16_t)val; /* DOUTSET */
            else if(poff == 0x0C)
                app->gpio_dout[port] = (uint16_t)val; /* DOUT */
            /* TX capture: the firmware bit-bangs the DATA pin (DOUT port B bit0)
             * while it is in TX mode (rf_tx_on). */
            if(app->tx_capturing && port == app->prof->data_port) {
                bool level = (app->gpio_dout[port] >> app->prof->data_pin) & 1u;
                tx_capture_data_edge(app, level);
            }
        }
    }
    (void)size;
}

/* ------------------------------------------------------------ minimal NVIC */

static uint32_t read_u32_le(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* Current VTOR (SCB->VTOR at 0xE000ED08). We read it via the core (callback). */
static uint32_t cur_vtor(AppState* app) {
    return thumb_read_mem(app->cpu, 0xE000ED08u, 4);
}

static uint32_t irq_handler_addr(AppState* app, int irqn) {
    uint32_t vt = cur_vtor(app);
    uint32_t off = vt + 0x40u + (uint32_t)irqn * 4u;
    return thumb_read_mem(app->cpu, off, 4);
}

/* Timer IRQs that we fire periodically (from the RE: IRQ12/13/14 timers,
 * IRQ1 system). IRQ numbers (not exception numbers). */
static const int TIMER_IRQS[] = {12, 13, 14, 1};

static void nvic_tick(AppState* app) {
    if(app->in_irq) return;
    uint32_t vtor = cur_vtor(app);
    if(vtor < 0x8000u) return; /* the app has not installed its table yet */
    int irqn = TIMER_IRQS[app->irq_rr % (int)(sizeof(TIMER_IRQS) / sizeof(int))];
    app->irq_rr++;
    uint32_t handler = irq_handler_addr(app, irqn);
    if(handler == 0 || handler == 0x82DBu) return; /* default handler: skip */
    app->tick_count++;
    app->in_irq = 1;
    thumb_enter_exception(app->cpu, handler, THUMB_EXC_RETURN_MSP_THREAD, (uint32_t)(irqn + 16));
    app->next_pc = thumb_get_reg(app->cpu, 15);
}

/* Locates a WFI instruction (0xBF30) in the app code to detect sleep. */
static uint32_t find_wfi(const uint8_t* flash, uint32_t size) {
    uint32_t lim = size < 0x20000u ? size : 0x20000u;
    for(uint32_t off = 0x8100u; off + 1 < lim; off += 2) {
        if(flash[off] == 0x30 && flash[off + 1] == 0xBF) return off;
    }
    return 0xFFFFFFFFu;
}

/* ------------------------------------------------------------ knock FSM */

/* Called by the per-instruction hook. Injects the knock-code synchronized with
 * the scanner: presses at scan_head, releases after N spins (confirms on
 * release). Replica of _knock_step from pandora_tui.py. */
static void knock_step(AppState* app, uint32_t a) {
    if(!app->knock_active) return;
    if(app->knock_sub == 0 && a == app->prof->scan_head) {
        if(app->knock_idx < app->knock_len) {
            app->knock_cur = app->knock_seq[app->knock_idx];
            set_button(app, app->knock_cur, true);
            app->knock_sub = 1;
            app->knock_spins = 0;
        } else {
            app->knock_active = 0;
        }
    } else if(app->knock_sub == 1) {
        for(int i = 0; i < app->prof->spin_count; i++) {
            if(a == app->prof->spin_heads[i]) {
                app->knock_spins++;
                if(app->knock_spins >= KNOCK_RELEASE_AFTER) {
                    set_button(app, app->knock_cur, false);
                    app->knock_idx++;
                    app->knock_sub = 0;
                }
                break;
            }
        }
    }
}

/* Called by the per-instruction hook. POST-unlock navigation injection,
 * synchronized with the profile's nav_reader. Faithful port of _nav_hook (the
 * closure inside nav_button) in pandora_tui.py:
 *   - the nav_reader (read_gpio_pin inlined) is SHARED: the firmware runs it
 *     once per button and per UI-loop turn with r0=port, r1=pin.
 *   - phase "press": on the FIRST reader read (ANY pin), press our pin (0) so
 *     the button's handler sees it pressed on its next turn.
 *   - phase "hold":  count ONLY the reader re-reads of OUR pin (r0==port &&
 *     r1==pin). After nav_reads_target re-reads, release the pin -> the handler
 *     dispatches (short click, or long press if target == NAV_LONG_READS).
 * The pin counting on r0/r1 is the reliability fix from NAV_DIAG.md: counting
 * reads of any pin lost presses. */
static void nav_step(AppState* app, uint32_t a) {
    if(!app->nav_active) return;
    if(a != app->prof->nav_reader) return;
    uint32_t r0 = thumb_get_reg(app->cpu, 0); /* port */
    uint32_t r1 = thumb_get_reg(app->cpu, 1); /* pin */
    bool is_our_pin = (r0 == app->nav_port && r1 == app->nav_pin);
    if(app->nav_phase == 0) { /* press */
        app->gpio_din[app->nav_port] &= (uint16_t)~(1u << app->nav_pin); /* 0 = pressed */
        app->btn_pressed[app->nav_btn] = 1;
        app->nav_phase = 1;
        app->nav_reads = 0;
    } else if(app->nav_phase == 1) { /* hold */
        if(is_our_pin) {
            app->nav_reads++;
            if(app->nav_reads >= app->nav_reads_target) {
                app->gpio_din[app->nav_port] |= (uint16_t)(1u << app->nav_pin); /* release */
                app->btn_pressed[app->nav_btn] = 0;
                app->nav_phase = 2; /* done */
            }
        }
    }
}

/* Per-instruction hook: counts, detects unlock_pc, advances the knock FSM
 * (pre-unlock) OR the nav FSM (post-unlock). The core only exposes ONE hook
 * (thumb_set_hook); both FSMs share this single code_hook, exactly like the
 * reference emulator shares one UC_HOOK_CODE. Only one of the two is ever
 * active at a time. */
static void code_hook(void* ctx, uint32_t pc) {
    AppState* app = (AppState*)ctx;
    app->ninsn++;
    app->pc = pc;
    if(!app->unlocked && pc == app->prof->unlock_pc) {
        app->unlocked = 1;
    }
    if(app->knock_active) knock_step(app, pc);
    if(app->nav_active) nav_step(app, pc);
}

/* ------------------------------------------------------------ execution */

/* A burst of instructions with WFI/tick + EXC_RETURN handling. Replica of
 * run_burst from pandora_tui.py. */
static void run_burst(AppState* app, uint32_t count) {
    bool app_running = cur_vtor(app) >= 0x8000u;
    bool stuck_wfi = app_running && (app->next_pc & ~1u) == (app->wfi_pc_hint & ~1u);
    bool want_tick = app_running &&
                     (stuck_wfi || (app->ninsn - app->last_tick_insn) > 20000ull);
    if(want_tick && !app->in_irq) {
        app->last_tick_insn = app->ninsn;
        nvic_tick(app);
    }
    thumb_set_reg(app->cpu, 15, app->next_pc);
    for(uint32_t i = 0; i < count; i++) {
        int rc = thumb_step(app->cpu);
        if(rc < 0) break;
        uint32_t pc = thumb_get_reg(app->cpu, 15);
        /* EXC_RETURN: the ISR finished (bx lr with 0xFFFFFFFx) */
        if((pc & 0xFFFFFFF0u) == 0xFFFFFFF0u) {
            thumb_exc_return(app->cpu, pc);
            app->in_irq = 0;
            app->next_pc = thumb_get_reg(app->cpu, 15);
            return;
        }
    }
    app->next_pc = thumb_get_reg(app->cpu, 15);
}

static void tap(AppState* app, int btn) {
    /* buffer in the struct itself for the 1-step tap */
    static uint8_t one[1];
    one[0] = (uint8_t)btn;
    app->knock_seq = one;
    app->knock_len = 1;
    app->knock_idx = 0;
    app->knock_sub = 0;
    app->knock_cur = 0;
    app->knock_active = 1;
    uint64_t done = 0;
    while(app->knock_active && done < 4000000ull) {
        run_burst(app, 40000);
        done += 40000;
        if(app->knock_idx >= app->knock_len) app->knock_active = 0;
    }
    app->knock_active = 0;
}

/* Cheap hash of the keyfob framebuffer (92x16=1472B) read directly from the
 * emulated RAM, used by nav_button's adaptive settle to detect a UI redraw.
 * Mirrors hash(bytes(self.read_fb())) in pandora_tui.py (read_fb returns the
 * firmware's framebuffer). FNV-1a. */
static uint32_t fb_hash(AppState* app) {
    uint32_t h = 2166136261u;
    uint32_t base = app->prof->fb_addr;
    for(uint32_t i = 0; i < (uint32_t)OLED_FB_SIZE; i++) {
        uint8_t b = (uint8_t)thumb_read_mem(app->cpu, base + i, 1);
        h ^= b;
        h *= 16777619u;
    }
    return h;
}

/* Injects ONE navigation button POST-unlock and lets the UI advance. Faithful
 * port of nav_button() in pandora_tui.py.
 *
 * Unlike the knock (pre-unlock scanner tap()), the post-unlock UI uses a main
 * loop that calls each button handler one-by-one; each handler reads its pin
 * once via the SHARED nav_reader (r0=port, r1=pin) and, if it sees it pressed,
 * enters a hold loop re-reading the pin, dispatching on release and
 * distinguishing short click vs long press by how many re-reads the hold
 * lasted.
 *
 *   1) arm the nav FSM; on the first nav_reader read we PRESS the pin (sync).
 *   2) hold for nav_reads_target re-reads OF OUR PIN (hold_reads short /
 *      long_reads for a long press).
 *   3) release -> the handler dispatches.
 *
 * Then an ADAPTIVE settle: run short bursts and STOP as soon as the framebuffer
 * changes (UI redrew) past a minimum, so feedback is ~0.2s instead of ~1s.
 *
 * Returns true if the injection completed (press+hold+release). */
static bool nav_button(AppState* app, int btn, bool hold) {
    if(btn < 0 || btn >= BTN_COUNT) return false;
    uint8_t port, pin;
    btn_port_pin(app->prof, btn, &port, &pin);
    app->nav_btn = (uint8_t)btn;
    app->nav_port = port;
    app->nav_pin = pin;
    app->nav_phase = 0; /* press */
    app->nav_reads = 0;
    app->nav_reads_target = hold ? NAV_LONG_READS : NAV_HOLD_READS;
    app->nav_active = 1;

    uint64_t done = 0;
    while(app->nav_phase != 2 && done < NAV_BUDGET_INSN) {
        run_burst(app, 50000);
        done += 50000;
    }
    app->nav_active = 0;
    /* guarantee release (in case the budget ran out mid-hold) */
    app->gpio_din[port] |= (uint16_t)(1u << pin);
    app->btn_pressed[btn] = 0;

    bool completed = (app->nav_phase == 2);

    /* adaptive settle: stop as soon as the firmware redraws the framebuffer. */
    uint32_t prev = fb_hash(app);
    done = 0;
    while(done < NAV_SETTLE_INSN) {
        run_burst(app, 60000);
        done += 60000;
        uint32_t cur = fb_hash(app);
        if(cur != prev && done >= NAV_SETTLE_MIN_INSN) break; /* UI redrew: feedback ready */
        prev = cur;
    }
    return completed;
}

static bool auto_unlock(AppState* app) {
    app->unlocked = 0;
    app->max_step = -1;
    /* 1) settle until the scanner is active (valid window) */
    uint64_t done = 0;
    while(done < KNOCK_SETTLE_INSN) {
        run_burst(app, 100000);
        done += 100000;
    }
    /* 2) inject the full sequence synchronized with the scanner.
     * Prefer the sequence ENTERED by the user in the menu; if none was defined,
     * use the default of the detected profile. */
    if(app->knock_user_len > 0) {
        app->knock_seq = app->knock_user;
        app->knock_len = app->knock_user_len;
        FURI_LOG_I(TAG, "knock: using user sequence (len=%u)", app->knock_user_len);
    } else {
        app->knock_seq = app->prof->knock;
        app->knock_len = app->prof->knock_len;
        FURI_LOG_I(TAG, "knock: using default of profile %s (len=%u)",
                   app->prof->name, app->prof->knock_len);
    }
    app->knock_idx = 0;
    app->knock_sub = 0;
    app->knock_cur = 0;
    app->knock_active = 1;
    uint64_t budget = KNOCK_STEP_BUDGET * (app->knock_len ? app->knock_len : 1);
    done = 0;
    while(app->knock_active && !app->unlocked && done < budget) {
        run_burst(app, 40000);
        done += 40000;
        if(app->knock_idx >= app->knock_len) app->knock_active = 0;
    }
    app->knock_active = 0;
    /* 3) final gate: hold B5 for a while */
    set_button(app, app->prof->gate, true);
    done = 0;
    while(done < 600000) {
        run_burst(app, 50000);
        done += 50000;
    }
    set_button(app, app->prof->gate, false);
    /* 4) let it run so the app redraws the UI (with ticks) */
    done = 0;
    while(done < 600000) {
        run_burst(app, 100000);
        done += 100000;
    }
    if(app->unlocked) {
        FURI_LOG_I(TAG, "unlock OK (knock-code accepted, PC unlock=0x%06lX)",
                   (unsigned long)app->prof->unlock_pc);
    } else {
        FURI_LOG_W(TAG, "unlock FAILED (knock-code incomplete / not accepted)");
    }
    return app->unlocked != 0;
}

/* ------------------------------------------------------------ render OLED */

/* The keyfob framebuffer is 92col x 16pag = 1472B, page-addressed, bit0 top,
 * GRAYSCALE 2bpp: each page byte is 4 logical pixels (bits 2k,2k+1 = pixel k),
 * 16 pag x 4 = 64 real rows. We collapse 2bpp->1bpp (pixel on if level>=1) and
 * paint it centered on the Flipper 128x64 screen. Logical panel 92x64.
 * (See emu/oled_render.py fb_to_gray.) */
static void render_oled(AppState* app) {
    furi_mutex_acquire(app->fb_mutex, FuriWaitForever);
    memset(app->screen, 0, FB_SIZE);
    uint32_t fb = app->prof->fb_addr;
    const int x_off = (SCREEN_W - OLED_COLS) / 2; /* center 92 in 128 => 18 */
    for(int page = 0; page < OLED_PAGES; page++) {
        for(int col = 0; col < OLED_COLS; col++) {
            uint32_t a = fb + (uint32_t)(page * OLED_COLS + col);
            uint8_t v = (uint8_t)thumb_read_mem(app->cpu, a, 1);
            for(int k = 0; k < 4; k++) {
                /* pixel level = popcount of the 2 bits (2k, 2k+1) */
                int lvl = ((v >> (2 * k)) & 1) + ((v >> (2 * k + 1)) & 1);
                if(lvl == 0) continue; /* off */
                int y = page * 4 + k; /* 0..63 */
                int x = x_off + col;
                if(y < 0 || y >= SCREEN_H || x < 0 || x >= SCREEN_W) continue;
                app->screen[(y >> 3) * SCREEN_W + x] |= (uint8_t)(1u << (y & 7));
            }
        }
    }
    furi_mutex_release(app->fb_mutex);
}

static void framebuffer_commit_callback(
    uint8_t* data,
    size_t size,
    CanvasOrientation orientation,
    void* context) {
    __atomic_fetch_add(&s_fb_cb_inflight, 1, __ATOMIC_RELAXED);
    AppState* app = (AppState*)context;
    (void)orientation;
    if(!app || !data || size < FB_SIZE || app->menu_active) {
        __atomic_fetch_sub(&s_fb_cb_inflight, 1, __ATOMIC_RELAXED);
        return;
    }
    if(furi_mutex_acquire(app->fb_mutex, 0) != FuriStatusOk) {
        __atomic_fetch_sub(&s_fb_cb_inflight, 1, __ATOMIC_RELAXED);
        return;
    }
    const uint8_t* src = app->screen;
    for(size_t i = 0; i < FB_SIZE; i++) {
        data[i] = (uint8_t)(src[i] ^ 0xFF); /* screen bit=1 light; display bit=1 dark */
    }
    furi_mutex_release(app->fb_mutex);
    __atomic_fetch_sub(&s_fb_cb_inflight, 1, __ATOMIC_RELAXED);
}

/* ------------------------------------------------------------ input */

static uint8_t poll_raw_keys(void) {
    uint8_t keys = 0;
    for(size_t i = 0; i < input_pins_count; i++) {
        const InputPin* p = &input_pins[i];
        if(furi_hal_gpio_read(p->gpio) == p->inverted) continue;
        switch(p->key) {
        case InputKeyUp: keys |= KBIT_UP; break;
        case InputKeyDown: keys |= KBIT_DOWN; break;
        case InputKeyLeft: keys |= KBIT_LEFT; break;
        case InputKeyRight: keys |= KBIT_RIGHT; break;
        case InputKeyOk: keys |= KBIT_OK; break;
        case InputKeyBack: keys |= KBIT_BACK; break;
        default: break;
        }
    }
    return keys;
}

/* ------------------------------------------------------------ load + boot */

typedef enum { LoadOk, LoadIoError, LoadNoMem, LoadBadFormat } LoadResult;

/* Decodes a hex byte (2 chars). Returns -1 if invalid. */
static int hexbyte(const char* s) {
    int hi = (unsigned char)s[0], lo = (unsigned char)s[1];
    if(hi >= '0' && hi <= '9') hi -= '0';
    else { hi |= 0x20; if(hi >= 'a' && hi <= 'f') hi = hi - 'a' + 10; else return -1; }
    if(lo >= '0' && lo <= '9') lo -= '0';
    else { lo |= 0x20; if(lo >= 'a' && lo <= 'f') lo = lo - 'a' + 10; else return -1; }
    return (hi << 4) | lo;
}

/* Processes ONE line of Intel HEX (without the leading ':') writing to flash.
 * Updates *ext_lin. Returns 1 if it was EOF, 0 if next, -1 on error. */
static int ihex_line(const char* ln, size_t len, uint8_t* flash, uint32_t flash_size,
                     uint32_t* ext_lin) {
    if(len < 10) return 0; /* line too short: ignore (stray CR/LF) */
    int count = hexbyte(ln);
    int ah = hexbyte(ln + 2);
    int al = hexbyte(ln + 4);
    int rtype = hexbyte(ln + 6);
    if(count < 0 || ah < 0 || al < 0 || rtype < 0) return -1;
    uint32_t addr = ((uint32_t)ah << 8) | (uint32_t)al;
    const char* data = ln + 8;
    if(rtype == 0x00) { /* data */
        uint32_t base = *ext_lin + addr;
        for(int k = 0; k < count; k++) {
            int b = hexbyte(data + k * 2);
            if(b < 0) return -1;
            if(base + (uint32_t)k < flash_size) flash[base + k] = (uint8_t)b;
        }
    } else if(rtype == 0x01) { /* EOF */
        return 1;
    } else if(rtype == 0x04) { /* ext linear addr */
        int b0 = hexbyte(data), b1 = hexbyte(data + 2);
        if(b0 < 0 || b1 < 0) return -1;
        *ext_lin = (((uint32_t)b0 << 8) | (uint32_t)b1) << 16;
    }
    /* other types (02/03/05): ignore */
    return 0;
}

/* Loads the firmware by STREAMING from the SD (does NOT read the whole file to
 * RAM: the .hex files can be 600KB+ and the Flipper has little free heap). The
 * flash buffer is allocated to the real size the firmware uses (flash_used of
 * the profile). */
static LoadResult load_firmware(AppState* app, Storage* storage, const char* path) {
    File* f = storage_file_alloc(storage);
    LoadResult res = LoadIoError;
    const uint32_t flash_size = app->prof->flash_used;
    do {
        if(!storage_file_open(f, path, FSAM_READ, FSOM_OPEN_EXISTING)) break;
        uint64_t fsize = storage_file_size(f);
        if(fsize == 0 || fsize > 4u * 1024u * 1024u) {
            res = LoadBadFormat;
            break;
        }

        app->flash = (uint8_t*)safe_malloc(flash_size);
        if(!app->flash) {
            FURI_LOG_E(TAG, "no heap for flash (%lu B)", (unsigned long)flash_size);
            res = LoadNoMem;
            break;
        }
        memset(app->flash, 0xFF, flash_size);

        /* detect format by reading the first byte */
        uint8_t first = 0;
        if(storage_file_read(f, &first, 1) != 1) break;
        storage_file_seek(f, 0, true);

        /* shared streaming buffers (static: not on the stack). ~1.1KB .bss */
        static uint8_t chunk[512];
        static char line[600];
        size_t rd;

        if(first == ':') {
            /* Intel HEX: read in chunks and parse by lines (streaming). */
            size_t linepos = 0;
            uint32_t ext_lin = 0;
            bool eof_rec = false;
            bool err = false;
            while(!eof_rec && (rd = storage_file_read(f, chunk, sizeof(chunk))) > 0) {
                for(size_t i = 0; i < rd; i++) {
                    char ch = (char)chunk[i];
                    if(ch == ':') {
                        linepos = 0; /* start of record */
                    } else if(ch == '\n' || ch == '\r') {
                        if(linepos > 0) {
                            int r = ihex_line(line, linepos, app->flash, flash_size, &ext_lin);
                            if(r < 0) { err = true; break; }
                            if(r == 1) { eof_rec = true; break; }
                            linepos = 0;
                        }
                    } else {
                        if(linepos < sizeof(line) - 1) line[linepos++] = ch;
                    }
                }
                if(err) break;
            }
            if(err) {
                res = LoadBadFormat;
                break;
            }
        } else {
            /* plain binary: copy in chunks directly to flash */
            uint32_t off = 0;
            while((rd = storage_file_read(f, chunk, sizeof(chunk))) > 0) {
                for(size_t i = 0; i < rd && off < flash_size; i++, off++)
                    app->flash[off] = chunk[i];
                if(off >= flash_size) break;
            }
        }
        res = LoadOk;
    } while(false);
    storage_file_close(f);
    storage_file_free(f);
    return res;
}

static const ArmProfile* pick_profile(const char* path) {
    if(path) {
        if(strstr(path, "MAX") || strstr(path, "max") || strstr(path, "PANDORA_MAX"))
            return &PROFILE_PANDORAMAX;
        if(strstr(path, "2_5253") || strstr(path, "pandora"))
            return &PROFILE_PANDORA;
    }
    return &PROFILE_PANDORA;
}

/* ------------------------------------------------------------ CC1101 RF */

static const uint8_t PRESET_OOK650[] = {
    0x02, 0x0D, 0x07, 0x04, 0x08, 0x32, 0x0B, 0x06, 0x10, 0xF8, 0x11, 0x32, 0x12, 0x30,
    0x14, 0x00, 0x15, 0x00, 0x18, 0x18, 0x19, 0x16, 0x1B, 0x07, 0x1C, 0x00, 0x1D, 0x91,
    0x20, 0xFB, 0x21, 0xB6, 0x22, 0x11, 0x00, 0x00, 0x00, 0xC0, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00,
};

static void radio_init(AppState* app) {
    app->frequency = 433920000;
    furi_hal_subghz_reset();
    furi_hal_subghz_load_custom_preset(PRESET_OOK650);
    furi_hal_subghz_set_frequency_and_path(app->frequency);
    furi_hal_subghz_idle();
    app->radio_on = true;
    app->rf_mode = RfModeOff;
    FURI_LOG_I(TAG, "CC1101 init OOK650 @%lu Hz", (unsigned long)app->frequency);
}

static void rx_bridge_stop(AppState* app);

static void radio_deinit(AppState* app) {
    if(!app->radio_on) return;
    if(app->tx_active) {
        furi_hal_subghz_stop_async_tx();
        app->tx_active = false;
    }
    if(app->rx_running) rx_bridge_stop(app);
    furi_hal_subghz_idle();
    furi_hal_subghz_sleep();
    app->radio_on = false;
    FURI_LOG_I(TAG, "CC1101 sleep");
}

/* ===================================================================== TX bridge */

/* Called when the firmware changes the Si4432 mode (REG[0x07]). On TX we start
 * capturing the DATA pin edges; when leaving TX we close the captured frame.
 * Replica of PandoraPIC's rf_tx_on detection. */
static void tx_on_si_mode(AppState* app, int new_mode) {
    if(app->rf_mode != RfModeTx) return; /* we only capture if the overlay is in TX */
    if(new_mode == 3) { /* TX */
        app->tx_capturing = true;
        app->tx_last_level = (app->gpio_dout[app->prof->data_port] >> app->prof->data_pin) & 1u;
        app->tx_last_insn = app->ninsn;
        app->tx_edges_frame = 0;
        FURI_LOG_I(TAG, "RF: firmware rf_tx_on -> capturing DATA pin (B%u.%u)",
                   app->prof->data_port, app->prof->data_pin);
    } else { /* IDLE/READY/RX: end of the frame */
        if(app->tx_capturing) {
            app->tx_capturing = false;
            FURI_LOG_I(TAG, "RF: firmware TX end (%lu edges captured)",
                       (unsigned long)app->tx_edges_frame);
        }
    }
}

/* Captures an edge of the firmware output DATA pin: accumulates the level that
 * JUST ended with its duration (ninsn between toggles) into the TX ring. */
static void tx_capture_data_edge(AppState* app, bool level) {
    if(!app->tx_capturing) return;
    if(level == app->tx_last_level) return; /* no edge */
    uint64_t now = app->ninsn;
    uint64_t dins = now - app->tx_last_insn;
    app->tx_last_insn = now;
    uint32_t dur_us = (uint32_t)((dins * ARM_INSN_US_NUM) / ARM_INSN_US_DEN);
    if(dur_us == 0) dur_us = 1;
    uint32_t head = app->tx_head;
    uint32_t next = (head + 1) % TX_RING_LEN;
    if(next != app->tx_tail) {
        app->tx_ring[head].level = app->tx_last_level;
        app->tx_ring[head].duration = dur_us;
        app->tx_head = next;
        app->tx_edges_frame++;
    }
    app->tx_last_level = level;
}

/* async_tx callback: drains the ring of captured edges. */
static LevelDuration radio_tx_callback(void* context) {
    AppState* app = (AppState*)context;
    uint32_t tail = app->tx_tail;
    if(tail == app->tx_head) return level_duration_reset();
    RfEdge e = app->tx_ring[tail];
    app->tx_tail = (tail + 1) % TX_RING_LEN;
    return level_duration_make(e.level, e.duration);
}

/* Starts/finishes the replay of the accumulated edges. Main loop. */
static void tx_bridge_flush(AppState* app) {
    if(app->rf_mode != RfModeTx || !app->radio_on) return;
    if(app->tx_active) {
        if(furi_hal_subghz_is_async_tx_complete()) {
            furi_hal_subghz_stop_async_tx();
            furi_hal_subghz_idle();
            app->tx_active = false;
            app->tx_frames++;
            FURI_LOG_I(TAG, "TX frame #%lu sent", (unsigned long)app->tx_frames);
        }
        return;
    }
    uint32_t pending = (app->tx_head + TX_RING_LEN - app->tx_tail) % TX_RING_LEN;
    if(pending < 16) return; /* wait for a reasonable burst */
    if(app->tx_capturing) return; /* frame still being captured: wait for the close */
    if(!furi_hal_subghz_is_tx_allowed(app->frequency)) {
        FURI_LOG_E(TAG, "TX NOT allowed @%lu Hz; dropping %lu edges",
                   (unsigned long)app->frequency, (unsigned long)pending);
        app->tx_tail = app->tx_head;
        return;
    }
    /* log the frame (first edges: level+duration). */
    FURI_LOG_I(TAG, "TX async start: %lu edges", (unsigned long)pending);
    {
        char line[96];
        int off = 0;
        uint32_t t = app->tx_tail;
        for(int i = 0; i < 8 && t != app->tx_head; i++) {
            RfEdge e = app->tx_ring[t];
            int w = snprintf(line + off, sizeof(line) - (size_t)off, "%c%lu ",
                             e.level ? 'H' : 'L', (unsigned long)e.duration);
            if(w < 0 || (size_t)(off + w) >= sizeof(line)) break;
            off += w;
            t = (t + 1) % TX_RING_LEN;
        }
        FURI_LOG_D(TAG, "TX frame[0..7]: %s", line);
    }
    if(furi_hal_subghz_start_async_tx(radio_tx_callback, app)) {
        app->tx_active = true;
    } else {
        FURI_LOG_E(TAG, "start_async_tx failed");
        app->tx_tail = app->tx_head;
    }
}

/* ===================================================================== RX bridge */

/* CC1101 callback (runs in IRQ): enqueues the edge. Do NOT block. */
static void radio_rx_callback(bool level, uint32_t duration, void* context) {
    AppState* app = (AppState*)context;
    RfEdge e = {.level = level, .duration = duration};
    furi_message_queue_put(app->rx_queue, &e, 0);
}

/* Executes a firmware subroutine (addr) as call() does in arm_pandora.py:
 * scratch SP, LR=sentinel, runs until returning to the sentinel. Leaves the
 * core state (PC/SP/LR) restored for the main loop. */
#define ARM_CALL_SENTINEL 0x001FFFF0u
#define ARM_CALL_SCRATCH_SP 0x20007000u

static void call_subroutine(AppState* app, uint32_t addr, uint32_t max_insn) {
    ThumbCore* cpu = app->cpu;
    /* save the main loop context */
    uint32_t s_pc = app->next_pc;
    uint32_t s_sp = thumb_get_reg(cpu, 13);
    uint32_t s_lr = thumb_get_reg(cpu, 14);
    uint32_t s_r0 = thumb_get_reg(cpu, 0);
    uint32_t s_r1 = thumb_get_reg(cpu, 1);
    uint8_t s_halted = cpu->halted;

    thumb_set_reg(cpu, 13, ARM_CALL_SCRATCH_SP);
    thumb_set_reg(cpu, 14, ARM_CALL_SENTINEL | 1u);
    thumb_set_reg(cpu, 15, addr | 1u);
    cpu->halted = 0;

    for(uint32_t i = 0; i < max_insn; i++) {
        uint32_t pc = thumb_get_reg(cpu, 15) & ~1u;
        if(pc == (ARM_CALL_SENTINEL & ~1u)) break;
        if(thumb_step(cpu) < 0) break;
    }

    /* restore */
    thumb_set_reg(cpu, 0, s_r0);
    thumb_set_reg(cpu, 1, s_r1);
    thumb_set_reg(cpu, 13, s_sp);
    thumb_set_reg(cpu, 14, s_lr);
    cpu->halted = s_halted;
    thumb_set_reg(cpu, 15, s_pc);
    app->next_pc = s_pc;
}

static void rx_wr8(AppState* app, uint32_t addr, uint8_t v) {
    thumb_write_mem(app->cpu, addr, v, 1);
}

/* Prepares the receiver RAM state as rf_rx_on would leave it (prime_rx from
 * arm_pandora.py): active gate, mode, shift-chains at the idle level. */
static void rx_prime(AppState* app, uint8_t mode, bool init_level) {
    const ArmProfile* p = app->prof;
    uint8_t lvl = init_level ? 1 : 0;
    /* clear the RX structure (0x40 bytes) */
    for(uint32_t o = 0; o < 0x40; o++) rx_wr8(app, p->rx_struct + o, 0);
    rx_wr8(app, p->rx_gate, 1); /* [+0xF] active gate (!=0xFF) */
    rx_wr8(app, p->rx_mode, mode); /* decoder selector */
    rx_wr8(app, p->rx_pending, 0); /* data present (!=0xFF) */
    rx_wr8(app, p->rx_state, 0);
    for(uint32_t o = 0x10; o < 0x1C; o++) rx_wr8(app, p->rx_struct + o, lvl);
    rx_wr8(app, p->rx_struct + 9, lvl);
    rx_wr8(app, p->rx_struct + 0xA, lvl);
    /* DATA pin (DIN port B bit0) at the idle level */
    if(init_level)
        app->gpio_din[p->data_port] |= (uint16_t)(1u << p->data_pin);
    else
        app->gpio_din[p->data_port] &= (uint16_t)~(1u << p->data_pin);
    app->rx_prev_level = init_level;
    app->rx_primed = true;
}

/* Injects ONE edge (level,dur_us) into the firmware demodulator, replicating
 * feed_sub from arm_pandora.py: sets TIMER1_CNT=dur*6, DATA pin=level, seeds the
 * shift-chain with the PREVIOUS level and fires the demod ISR once. Returns true
 * if the firmware completed a frame (rx_pending==0xFF). */
static bool rx_feed_edge(AppState* app, bool level, uint32_t dur_us) {
    const ArmProfile* p = app->prof;
    bool prev = app->rx_prev_level;
    app->rx_pulse_ticks = (dur_us * 6u) & 0xFFFFu;
    /* DATA pin at the NEW level (the edge already occurred) */
    if(level)
        app->gpio_din[p->data_port] |= (uint16_t)(1u << p->data_pin);
    else
        app->gpio_din[p->data_port] &= (uint16_t)~(1u << p->data_pin);
    /* shift-chain + filtered levels at the PREVIOUS level to force edge
     * detection (see calibration note in arm_pandora.py / CAL_arm_decoder). */
    uint8_t pl = prev ? 1 : 0;
    for(uint32_t o = 0x10; o < 0x1C; o++) rx_wr8(app, p->rx_struct + o, pl);
    rx_wr8(app, p->rx_struct + 9, pl);
    rx_wr8(app, p->rx_struct + 0xA, pl);

    app->rx_feeding = true;
    call_subroutine(app, p->demod_isr, 200000);
    app->rx_feeding = false;
    app->rx_prev_level = level;
    app->rx_events++;

    uint8_t pend = (uint8_t)thumb_read_mem(app->cpu, p->rx_pending, 1);
    return pend == 0xFF;
}

static void rx_bridge_start(AppState* app) {
    if(app->rx_running || !app->radio_on) return;
    if(!app->prof->rf_sw_demod) {
        FURI_LOG_W(TAG, "RX bridge not available on profile %s (Si4432 FIFO/HW)",
                   app->prof->name);
        return;
    }
    if(app->tx_active) {
        furi_hal_subghz_stop_async_tx();
        app->tx_active = false;
    }
    furi_hal_subghz_idle();
    furi_message_queue_reset(app->rx_queue);
    rx_prime(app, 1 /* default decoder mode */, false);
    furi_hal_subghz_start_async_rx(radio_rx_callback, app);
    app->rx_running = true;
    FURI_LOG_I(TAG, "RX bridge ON (injects into ISR 0x%06lX, DATA pin B%u.%u)",
               (unsigned long)app->prof->demod_isr, app->prof->data_port,
               app->prof->data_pin);
}

static void rx_bridge_stop(AppState* app) {
    if(!app->rx_running) return;
    furi_hal_subghz_stop_async_rx();
    furi_hal_subghz_idle();
    app->rx_running = false;
    app->rx_primed = false;
    FURI_LOG_I(TAG, "RX bridge OFF (%lu edges injected, %lu frames)",
               (unsigned long)app->rx_events, (unsigned long)app->rx_frames);
}

/* Drains the RX queue and injects each edge into the firmware demodulator. */
static void rx_bridge_pump(AppState* app) {
    if(!app->rx_running) return;
    RfEdge e;
    int drained = 0;
    while(furi_message_queue_get(app->rx_queue, &e, 0) == FuriStatusOk) {
        bool done = rx_feed_edge(app, e.level, e.duration);
        drained++;
        if(done) {
            app->rx_frames++;
            uint8_t id = (uint8_t)thumb_read_mem(app->cpu, app->prof->rx_state, 1);
            char line[64];
            int off = 0;
            for(int i = 0; i < 12; i++) {
                uint8_t b = (uint8_t)thumb_read_mem(app->cpu, app->prof->rx_result + (uint32_t)i, 1);
                int w = snprintf(line + off, sizeof(line) - (size_t)off, "%02X", b);
                if(w < 0 || (size_t)(off + w) >= sizeof(line)) break;
                off += w;
            }
            FURI_LOG_I(TAG, "RX frame #%lu decoded id=%u frame=%s",
                       (unsigned long)app->rx_frames, id, line);
            /* re-prime for the next frame */
            rx_prime(app, 1, app->rx_prev_level);
        }
        if(drained >= 48) break; /* do not hog the frame */
    }
    if(drained) {
        FURI_LOG_D(TAG, "RX injected %d edges (total %lu)", drained,
                   (unsigned long)app->rx_events);
    }
}

/* Toggles the RF bridge: OFF -> TX -> RX -> OFF. Logged (like PandoraPIC). */
static void rf_cycle_mode(AppState* app) {
    switch(app->rf_mode) {
    case RfModeOff:
        app->rf_mode = RfModeTx;
        app->tx_head = app->tx_tail = 0;
        app->tx_capturing = false;
        app->tx_edges_frame = 0;
        /* if the firmware is already in TX, start capturing immediately */
        if(app->si.mode == 3) tx_on_si_mode(app, 3);
        FURI_LOG_I(TAG, "RF mode TX (capture firmware DATA pin)");
        break;
    case RfModeTx:
        app->tx_capturing = false;
        if(app->tx_active) {
            furi_hal_subghz_stop_async_tx();
            app->tx_active = false;
        }
        app->rf_mode = RfModeRx;
        rx_bridge_start(app);
        if(!app->rx_running) {
            /* profile without SW-demod: jump directly to OFF */
            app->rf_mode = RfModeOff;
            FURI_LOG_I(TAG, "RF mode OFF (RX not supported on this profile)");
        } else {
            FURI_LOG_I(TAG, "RF mode RX (inject into firmware demodulator)");
        }
        break;
    case RfModeRx:
    default:
        rx_bridge_stop(app);
        app->rf_mode = RfModeOff;
        FURI_LOG_I(TAG, "RF mode OFF");
        break;
    }
}

/* ------------------------------------------------------------ overlay help */

/* In-emulator overlay (direct-draw): status + RF bridge control (Left). */
static void overlay_draw(AppState* app) {
    Canvas* c = app->canvas;
    canvas_reset(c);
    canvas_clear(c);
    canvas_set_font(c, FontPrimary);
    canvas_draw_str(c, 2, 10, "Pandora ARM");
    canvas_draw_line(c, 0, 12, 127, 12);
    canvas_set_font(c, FontSecondary);
    char buf[48];
    snprintf(buf, sizeof(buf), "FW:%s %s", app->prof ? app->prof->name : "?",
             app->unlocked ? "UNLOCKED" : "LOCKED");
    canvas_draw_str(c, 2, 24, buf);
    const char* rf = "OFF";
    if(app->rf_mode == RfModeTx) rf = "TX";
    if(app->rf_mode == RfModeRx) rf = "RX";
    snprintf(buf, sizeof(buf), "RF:%s %luMHz", rf, (unsigned long)(app->frequency / 1000000u));
    canvas_draw_str(c, 2, 34, buf);
    snprintf(buf, sizeof(buf), "TX:%lu RX:%lu", (unsigned long)app->tx_frames,
             (unsigned long)app->rx_frames);
    canvas_draw_str(c, 2, 44, buf);
    canvas_draw_str(c, 2, 56, "OK=cont Left=RF Back=exit");
    canvas_commit(c);
}

/* ============================================================ phase 1 menu */
/* view_dispatcher + submenu (NO number_input: the ARM uses a knock-code). Same
 * pattern as PandoraPIC: the dispatcher BLOCKS; the items set a flag and stop
 * the dispatcher; main opens the file browser / re-creates the menu / starts the
 * emulator. view_dispatcher and direct-draw NEVER coexist. */

/* Views (just one: the submenu is re-populated for main-menu / knock-builder) */
typedef enum {
    PandoraViewSubmenu,
    PandoraViewAbout,
} PandoraView;

/* Main menu items */
typedef enum {
    MenuItemFirmware,
    MenuItemKnock,
    MenuItemLaunch,
    MenuItemAbout,
} MenuItemMain;

/* Knock-code builder items (buttons + Done/Clear) */
typedef enum {
    KnockItemUp,
    KnockItemDown,
    KnockItemOk,
    KnockItemBack,
    KnockItemB5,
    KnockItemB6,
    KnockItemDone,
    KnockItemClear,
} KnockItem;

/* Flow requested when the dispatcher is stopped. */
typedef enum {
    FlowExit,
    FlowFileBrowser,
    FlowLaunch,
} MenuFlow;

static volatile MenuFlow g_menu_flow = FlowExit;

static const char* btn_name(uint8_t b) {
    switch(b) {
    case B_UP: return "UP";
    case B_DOWN: return "DOWN";
    case B_OK: return "OK";
    case B_BACK: return "BACK";
    case B5: return "B5";
    case B6: return "B6";
    default: return "?";
    }
}

/* Builds the string of the current knock sequence (profile default if the user
 * did not define any). */
static void knock_seq_str(AppState* app, char* out, size_t n) {
    const uint8_t* seq;
    uint8_t len;
    bool is_default = (app->knock_user_len == 0);
    if(is_default) {
        seq = app->prof ? app->prof->knock : NULL;
        len = app->prof ? app->prof->knock_len : 0;
    } else {
        seq = app->knock_user;
        len = app->knock_user_len;
    }
    int off = 0;
    if(is_default) {
        int w = snprintf(out, n, "(def) ");
        off = (w > 0) ? w : 0;
    }
    for(uint8_t i = 0; i < len && seq; i++) {
        int w = snprintf(out + off, n - (size_t)off, "%s%s", btn_name(seq[i]),
                         (i + 1 < len) ? "," : "");
        if(w < 0 || (size_t)(off + w) >= n) break;
        off += w;
    }
    if(len == 0) snprintf(out, n, "<empty>");
}

static void menu_submenu_callback(void* context, uint32_t index);
static void knock_submenu_callback(void* context, uint32_t index);

/* (Re)builds the knock-builder submenu with the current sequence. */
static void knock_rebuild(AppState* app) {
    submenu_reset(app->submenu);
    char hdr[64];
    char seq[48];
    knock_seq_str(app, seq, sizeof(seq));
    snprintf(hdr, sizeof(hdr), "Knock: %s", seq);
    submenu_set_header(app->submenu, hdr);
    submenu_add_item(app->submenu, "UP", KnockItemUp, knock_submenu_callback, app);
    submenu_add_item(app->submenu, "DOWN", KnockItemDown, knock_submenu_callback, app);
    submenu_add_item(app->submenu, "OK", KnockItemOk, knock_submenu_callback, app);
    submenu_add_item(app->submenu, "BACK", KnockItemBack, knock_submenu_callback, app);
    submenu_add_item(app->submenu, "B5", KnockItemB5, knock_submenu_callback, app);
    submenu_add_item(app->submenu, "B6", KnockItemB6, knock_submenu_callback, app);
    submenu_add_item(app->submenu, "Done", KnockItemDone, knock_submenu_callback, app);
    submenu_add_item(app->submenu, "Clear", KnockItemClear, knock_submenu_callback, app);
}

/* About screen text (scrollable). Describes what the firmware is and the full
 * button mapping (verified against the physical keyfob). */
static const char* const ABOUT_TEXT =
    "\e#Pandora D-605 (ARM)\e#\n"
    "Emulator of the real Pandora D-605 car-alarm keyfob firmware (Silicon Labs "
    "EFM32, ARM Cortex-M3). The firmware runs unmodified; this app emulates the "
    "keyfob hardware: the OLED screen and the 6 physical buttons, and bridges the "
    "Flipper CC1101 for real RX/TX.\n"
    "\n"
    "Buttons (Flipper key = keyfob button):\n"
    "Up = Btn1  (On / menu / cell+)\n"
    "Ok = Btn2  (confirm / mode; hold: erase cell)\n"
    "Down = Btn3  (menu / cell-; hold: auto)\n"
    "Left = Btn4  (RX freq / close car; hold: accept)\n"
    "Right = Btn5  (AM+FM / open car; hold: highway)\n"
    "Back = Btn6  (trunk; hold: back to menu)\n"
    "Hold Up (>1s) = EXIT app\n"
    "Hold Back (>1s) = cycle RF (OFF/TX/RX)\n"
    "Any key held = long press of that keyfob button. The firmware decides what "
    "each button does in each mode.\n"
    "\n"
    "Unlock: the screen is black until a knock-code (blind button sequence) is "
    "entered; the app auto-unlocks on launch (pandora: Up,Ok,Ok,Up,Up,Down / max: "
    "Up,Ok,Down,Ok,Up,Down).";

/* (Re)builds the main menu with the updated labels. */
static void menu_rebuild(AppState* app) {
    submenu_reset(app->submenu);
    submenu_set_header(app->submenu, "Pandora ARM");
    char buf[64];
    snprintf(buf, sizeof(buf), "Firmware: %s", app->fw_name[0] ? app->fw_name : "<none>");
    submenu_add_item(app->submenu, buf, MenuItemFirmware, menu_submenu_callback, app);
    char seq[48];
    knock_seq_str(app, seq, sizeof(seq));
    snprintf(buf, sizeof(buf), "Knock: %s", seq);
    submenu_add_item(app->submenu, buf, MenuItemKnock, menu_submenu_callback, app);
    submenu_add_item(app->submenu, "Launch", MenuItemLaunch, menu_submenu_callback, app);
    submenu_add_item(app->submenu, "About", MenuItemAbout, menu_submenu_callback, app);
}

static void knock_submenu_callback(void* context, uint32_t index) {
    AppState* app = (AppState*)context;
    int add = -1;
    switch(index) {
    case KnockItemUp: add = B_UP; break;
    case KnockItemDown: add = B_DOWN; break;
    case KnockItemOk: add = B_OK; break;
    case KnockItemBack: add = B_BACK; break;
    case KnockItemB5: add = B5; break;
    case KnockItemB6: add = B6; break;
    case KnockItemDone:
        FURI_LOG_I(TAG, "knock-builder: sequence confirmed (len=%u)", app->knock_user_len);
        app->menu_active = false;
        menu_rebuild(app);
        return;
    case KnockItemClear:
        app->knock_user_len = 0; /* back to the profile default */
        FURI_LOG_I(TAG, "knock-builder: cleared (uses profile default)");
        knock_rebuild(app);
        return;
    default: return;
    }
    if(add >= 0 && app->knock_user_len < sizeof(app->knock_user)) {
        app->knock_user[app->knock_user_len++] = (uint8_t)add;
        FURI_LOG_I(TAG, "knock-builder: +%s (len=%u)", btn_name((uint8_t)add),
                   app->knock_user_len);
        knock_rebuild(app);
    }
}

static void menu_submenu_callback(void* context, uint32_t index) {
    AppState* app = (AppState*)context;
    switch(index) {
    case MenuItemFirmware:
        g_menu_flow = FlowFileBrowser;
        view_dispatcher_stop(app->view_dispatcher);
        break;
    case MenuItemKnock:
        app->menu_active = true; /* "en knock-builder" */
        knock_rebuild(app);
        break;
    case MenuItemLaunch:
        g_menu_flow = FlowLaunch;
        view_dispatcher_stop(app->view_dispatcher);
        break;
    case MenuItemAbout:
        app->menu_active = true; /* reuse flag: "in a sub-view" -> Back returns to menu */
        widget_reset(app->about_widget);
        widget_add_text_scroll_element(app->about_widget, 0, 0, 128, 64, ABOUT_TEXT);
        view_dispatcher_switch_to_view(app->view_dispatcher, PandoraViewAbout);
        break;
    default: break;
    }
}

/* Back: from a sub-view (knock-builder or About) it returns to the main menu;
 * from the main menu it exits. */
static bool menu_back_callback(void* context) {
    AppState* app = (AppState*)context;
    /* Determining the active view is cumbersome with the API; we use a simple
     * flag (menu_active) that is set to true whenever we enter a sub-view
     * (knock-builder re-populates the submenu; About switches to its own Widget
     * view). On Back we rebuild the main menu and return to the submenu view. */
    if(app->menu_active) { /* reuse menu_active as "in a sub-view" */
        app->menu_active = false;
        menu_rebuild(app);
        view_dispatcher_switch_to_view(app->view_dispatcher, PandoraViewSubmenu);
        return true;
    }
    g_menu_flow = FlowExit;
    view_dispatcher_stop(app->view_dispatcher);
    return true;
}

/* Builds and runs the menu (blocks until stop). Creates/destroys the
 * view_dispatcher HERE to leave the GUI free on return. */
static MenuFlow run_menu(AppState* app, Gui* gui) {
    g_menu_flow = FlowExit;
    app->menu_active = false;

    app->view_dispatcher = view_dispatcher_alloc();
    app->submenu = submenu_alloc();
    app->about_widget = widget_alloc();

    view_dispatcher_set_event_callback_context(app->view_dispatcher, app);
    view_dispatcher_set_navigation_event_callback(app->view_dispatcher, menu_back_callback);

    view_dispatcher_add_view(
        app->view_dispatcher, PandoraViewSubmenu, submenu_get_view(app->submenu));
    /* a single submenu instance serves both logical views; to be able to map
     * Back differently we reuse the menu_active flag. To make the knock view
     * independent, we use the same re-populated submenu. */
    view_dispatcher_add_view(
        app->view_dispatcher, PandoraViewAbout, widget_get_view(app->about_widget));

    menu_rebuild(app);
    view_dispatcher_attach_to_gui(app->view_dispatcher, gui, ViewDispatcherTypeFullscreen);
    view_dispatcher_switch_to_view(app->view_dispatcher, PandoraViewSubmenu);

    view_dispatcher_run(app->view_dispatcher); /* BLOCKS */

    view_dispatcher_remove_view(app->view_dispatcher, PandoraViewSubmenu);
    view_dispatcher_remove_view(app->view_dispatcher, PandoraViewAbout);
    submenu_free(app->submenu);
    widget_free(app->about_widget);
    view_dispatcher_free(app->view_dispatcher);
    app->submenu = NULL;
    app->about_widget = NULL;
    app->view_dispatcher = NULL;
    return g_menu_flow;
}

static void set_fw_name_from_path(AppState* app) {
    const char* p = furi_string_get_cstr(app->fw_path);
    const char* slash = strrchr(p, '/');
    const char* base = slash ? slash + 1 : p;
    strncpy(app->fw_name, base, sizeof(app->fw_name) - 1);
    app->fw_name[sizeof(app->fw_name) - 1] = 0;
}

/* ------------------------------------------------------------ boot */

static void emu_reset_and_unlock(AppState* app) {
    /* core reset: SP=*(0), PC=*(4) from flash */
    uint32_t sp0 = read_u32_le(app->flash + 0);
    uint32_t rst = read_u32_le(app->flash + 4);
    memset(app->ram, 0, PA_RAM_SIZE);
    thumb_reset(app->cpu, sp0, rst);
    app->next_pc = rst | 1u;
    app->ninsn = 0;
    app->in_irq = 0;
    app->irq_rr = 0;
    app->tick_count = 0;
    app->last_tick_insn = 0;
    app->unlocked = 0;
    app->max_step = -1;
    app->knock_active = 0;
    app->nav_active = 0;
    app->nav_phase = 0;
    app->spi_phase = -1;
    app->spi_rx_head = app->spi_rx_tail = 0;
    init_buttons_released(app);
    si4432_init(&app->si);
    auto_unlock(app);
}

/* ============================================================ phase 2 emulator */

/* Runs the emulator with a direct-draw takeover. The firmware is ALREADY loaded
 * and booted/unlocked and the view_dispatcher has ALREADY been freed (they do
 * not coexist). */
static void run_emulator(AppState* app, Gui* gui) {
    Canvas* canvas = NULL;
    bool fb_cb_added = false;

    app->rx_queue = furi_message_queue_alloc(RX_QUEUE_LEN, sizeof(RfEdge));
    if(!app->rx_queue) {
        FURI_LOG_E(TAG, "rx_queue alloc failed");
        return;
    }

    app->gui = gui;
    gui_add_framebuffer_callback(gui, framebuffer_commit_callback, app);
    fb_cb_added = true;
    canvas = gui_direct_draw_acquire(gui);
    if(!canvas) {
        FURI_LOG_E(TAG, "direct_draw_acquire failed");
        goto teardown;
    }
    app->canvas = canvas;

    FURI_LOG_I(TAG, "emulator: main loop (fw=%s, %s)", app->prof->name,
               app->unlocked ? "UNLOCKED" : "LOCKED");

    uint32_t epoch = furi_get_tick();
    uint64_t next_us = 0;

    /* --- PHYSICAL KEY MAPPING (6 Flipper keys = 6 keyfob buttons) ---
     *   Flipper UP    -> Button 1 = UP   pin (On/menu/cell+).  Hold >1s = EXIT.
     *   Flipper OK    -> Button 2 = OK   pin (confirm/mode; hold=erase).
     *   Flipper DOWN  -> Button 3 = DOWN pin (menu/cell-; hold=auto).
     *   Flipper LEFT  -> Button 4 = BACK pin (freq/close; long=accept).
     *   Flipper RIGHT -> Button 5 = B5   pin (AM+FM/open; long=highway).
     *   Flipper BACK  -> Button 6 = B6   pin (short=trunk; long=back-to-menu).
     *                    Hold >1s = cycle RF bridge (OFF/TX/RX) - does NOT steal
     *                    a keyfob button (the >1s window is above the keyfob's
     *                    long-press, and RF is a host-side utility).
     * Button 1 (Flipper UP) has NO hold function on the real keyfob, so a long
     * hold of it is used to EXIT the app (safe).
     *
     * Hold detection: poll_raw_keys() gives the instantaneous state. We track
     * per-key press time with furi_get_tick() (ms). On release < HOLD_MS -> short
     * nav_button(); a held key past its threshold fires once (long press / exit /
     * RF) and is marked consumed so the subsequent release does nothing. */
    bool key_down[FK_COUNT] = {false};
    uint32_t key_t0[FK_COUNT] = {0};
    bool key_consumed[FK_COUNT] = {false}; /* long action already fired this hold */

    while(!app->exit_requested) {
        uint8_t keys = poll_raw_keys();
        uint32_t now_tick = furi_get_tick();

        for(int fk = 0; fk < FK_COUNT; fk++) {
            bool down = (keys & FK_TO_KBIT[fk]) != 0;

            if(down && !key_down[fk]) {
                /* rising edge: start timing, defer the action to release/hold */
                key_down[fk] = true;
                key_t0[fk] = now_tick;
                key_consumed[fk] = false;
            } else if(down && key_down[fk] && !key_consumed[fk]) {
                /* still held: check hold thresholds (fire once) */
                uint32_t held = now_tick - key_t0[fk];
                if(fk == FK_UP) {
                    if(held >= EXIT_HOLD_MS) { /* Button 1 long hold = EXIT */
                        app->exit_requested = true;
                        key_consumed[fk] = true;
                    }
                } else if(fk == FK_BACK) {
                    if(held >= RF_HOLD_MS) { /* Flipper BACK long hold = RF cycle */
                        rf_cycle_mode(app);
                        /* brief status overlay (RF mode / counters) as feedback;
                         * the normal render resumes on the next frame. */
                        overlay_draw(app);
                        furi_delay_ms(600);
                        key_consumed[fk] = true;
                    }
                }
                /* other keys: the long-vs-short decision is made on release */
            } else if(!down && key_down[fk]) {
                /* release: decide short vs long for the keyfob button */
                uint32_t held = now_tick - key_t0[fk];
                key_down[fk] = false;
                if(key_consumed[fk]) {
                    /* exit / RF already handled on hold; no keyfob button */
                } else if(app->unlocked) {
                    int btn = FK_TO_BTN[fk];
                    if(fk == FK_UP) {
                        /* Button 1 has no keyfob long-press; always short click */
                        nav_button(app, btn, false);
                    } else if(fk == FK_BACK) {
                        /* Button 6: short=trunk, 500ms..1s=long press. (>1s was
                         * RF cycle, handled above as consumed.) */
                        nav_button(app, btn, held >= HOLD_MS);
                    } else {
                        nav_button(app, btn, held >= HOLD_MS);
                    }
                } else {
                    /* pre-unlock fallback: use the knock tap() (scanner). Normally
                     * auto_unlock already ran at boot, so this path is rare. */
                    tap(app, FK_TO_BTN[fk]);
                }
            }
        }

        if(app->exit_requested) break;

        /* run a slice of the real firmware (captures TX via mmio_write) */
        run_burst(app, 150000);

        /* RF bridge: replay accumulated TX / inject received RX */
        tx_bridge_flush(app);
        rx_bridge_pump(app);

        /* redraw the screen from the firmware framebuffer */
        render_oled(app);
        if(!app->menu_active) canvas_commit(canvas);

        /* pacing + yield */
        next_us += FRAME_US;
        uint32_t target = epoch + (uint32_t)((next_us + 500) / 1000);
        uint32_t now = furi_get_tick();
        int32_t ahead = (int32_t)(target - now);
        if(ahead > 4 * 33) {
            epoch = now;
            next_us = 0;
            furi_delay_ms(1);
        } else if(ahead > 0) {
            furi_delay_until_tick(target);
        } else {
            furi_delay_ms(1);
        }
    }

teardown:
    radio_deinit(app);
    if(fb_cb_added) gui_remove_framebuffer_callback(gui, framebuffer_commit_callback, app);
    wait_inflight_zero(&s_fb_cb_inflight);
    if(canvas) gui_direct_draw_release(gui);
    if(app->rx_queue) {
        furi_message_queue_free(app->rx_queue);
        app->rx_queue = NULL;
    }
    FURI_LOG_I(TAG, "emulator: exit");
}

/* ------------------------------------------------------------ main */

int32_t pandora_arm_app(void* p) {
    UNUSED(p);

    AppState* app = (AppState*)malloc(sizeof(AppState));
    if(!app) return -1;
    memset(app, 0, sizeof(AppState));

    Storage* storage = (Storage*)furi_record_open(RECORD_STORAGE);
    DialogsApp* dialogs = (DialogsApp*)furi_record_open(RECORD_DIALOGS);
    Gui* gui = (Gui*)furi_record_open(RECORD_GUI);
    app->fw_path = furi_string_alloc_set_str("/ext");

    bool launch = false;

    /* --- PHASE 1: menu (view_dispatcher). Loop until Launch or Exit. --- */
    while(true) {
        /* keep the profile up to date to show the knock default */
        if(app->fw_name[0]) app->prof = pick_profile(furi_string_get_cstr(app->fw_path));

        MenuFlow flow = run_menu(app, gui);
        if(flow == FlowExit) {
            launch = false;
            break;
        } else if(flow == FlowFileBrowser) {
            DialogsFileBrowserOptions opts;
            dialog_file_browser_set_basic_options(&opts, "*", NULL);
            opts.base_path = "/ext";
            opts.hide_ext = false;
            if(dialog_file_browser_show(dialogs, app->fw_path, app->fw_path, &opts)) {
                set_fw_name_from_path(app);
                app->prof = pick_profile(furi_string_get_cstr(app->fw_path));
                FURI_LOG_I(TAG, "firmware chosen: %s (profile %s)", app->fw_name,
                           app->prof->name);
            }
            continue;
        } else if(flow == FlowLaunch) {
            if(app->fw_name[0] == 0) {
                FURI_LOG_W(TAG, "Launch without firmware; opening file browser");
                DialogsFileBrowserOptions opts;
                dialog_file_browser_set_basic_options(&opts, "*", NULL);
                opts.base_path = "/ext";
                opts.hide_ext = false;
                if(!dialog_file_browser_show(dialogs, app->fw_path, app->fw_path, &opts)) {
                    continue;
                }
                set_fw_name_from_path(app);
                app->prof = pick_profile(furi_string_get_cstr(app->fw_path));
            }
            launch = true;
            break;
        }
    }

    /* --- PHASE 2: emulator (direct-draw). Only if the user requested Launch. --- */
    if(launch) {
        do {
            app->prof = pick_profile(furi_string_get_cstr(app->fw_path));

            app->cpu = (ThumbCore*)malloc(sizeof(ThumbCore));
            if(!app->cpu) {
                FURI_LOG_E(TAG, "cpu alloc failed");
                break;
            }
            thumb_init(app->cpu);

            FURI_LOG_I(
                TAG, "free heap=%uK, max block=%uK, need flash=%luK + ram=%uK",
                (unsigned)(memmgr_get_free_heap() / 1024u),
                (unsigned)(memmgr_heap_get_max_free_block() / 1024u),
                (unsigned long)(app->prof->flash_used / 1024u),
                (unsigned)(PA_RAM_SIZE / 1024u));

            LoadResult lr = load_firmware(app, storage, furi_string_get_cstr(app->fw_path));
            if(lr != LoadOk) {
                char tbuf[64];
                if(lr == LoadNoMem)
                    snprintf(
                        tbuf, sizeof(tbuf), "Out of memory\nfree:%uK max:%uK",
                        (unsigned)(memmgr_get_free_heap() / 1024u),
                        (unsigned)(memmgr_heap_get_max_free_block() / 1024u));
                else if(lr == LoadBadFormat)
                    snprintf(tbuf, sizeof(tbuf), "Invalid format\n(.hex or .bin)");
                else
                    snprintf(tbuf, sizeof(tbuf), "Load error");
                FURI_LOG_E(TAG, "load failed: %s", tbuf);
                DialogMessage* msg = dialog_message_alloc();
                dialog_message_set_text(msg, tbuf, 64, 30, AlignCenter, AlignCenter);
                dialog_message_set_buttons(msg, NULL, "OK", NULL);
                dialog_message_show(dialogs, msg);
                dialog_message_free(msg);
                break;
            }

            app->ram = (uint8_t*)safe_malloc(PA_RAM_SIZE);
            if(!app->ram) {
                FURI_LOG_E(TAG, "ram alloc failed");
                break;
            }
            memset(app->ram, 0, PA_RAM_SIZE);

            /* connect memory: direct FLASH/RAM regions + MMIO callbacks.
             * flash_size = real allocated size (flash_used of the profile). */
            thumb_set_regions(
                app->cpu, app->flash, PA_FLASH_BASE, app->prof->flash_used, app->ram,
                PA_RAM_BASE, PA_RAM_SIZE);
            thumb_set_mem_cb(app->cpu, mmio_read, mmio_write, app);
            thumb_set_hook(app->cpu, code_hook, app);

            app->wfi_pc_hint = find_wfi(app->flash, app->prof->flash_used);

            app->fb_mutex = furi_mutex_alloc(FuriMutexTypeNormal);
            if(!app->fb_mutex) {
                FURI_LOG_E(TAG, "fb_mutex alloc failed");
                break;
            }

            radio_init(app);

            FURI_LOG_I(TAG, "profile: %s (knock %s)", app->prof->name,
                       app->knock_user_len ? "user" : "default");

            /* boot + auto-unlock (valid knock scanner window) */
            emu_reset_and_unlock(app);
            FURI_LOG_I(TAG, "boot OK -> PC=0x%06lX insn=%lu", (unsigned long)app->pc,
                       (unsigned long)app->ninsn);

            run_emulator(app, gui);
        } while(false);
    }

    /* --- teardown --- */
    if(app->cpu) free(app->cpu);
    if(app->flash) free(app->flash);
    if(app->ram) free(app->ram);
    if(app->fb_mutex) furi_mutex_free(app->fb_mutex);
    furi_string_free(app->fw_path);
    furi_record_close(RECORD_GUI);
    furi_record_close(RECORD_DIALOGS);
    furi_record_close(RECORD_STORAGE);
    free(app);
    return 0;
}
