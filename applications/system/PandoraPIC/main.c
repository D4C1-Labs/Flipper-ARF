/* Pandora PIC - emulator of the REAL Pandora DXL-5000 keyfob firmware (PIC18)
 * for the Flipper Zero.
 *
 * The keyfob firmware runs UNMODIFIED on top of the PIC18 interpreter
 * (lib/pic18core). This app emulates the hardware the firmware expects:
 *   - SSD1306 128x32 OLED display: its GDDRAM is reconstructed by intercepting
 *     the firmware's SW-SPI serializer (hook on send-byte + DC pin), just like
 *     the reference Python emulator (emu/pic18_tui.py). It is scaled/drawn on
 *     the Flipper's 128x64 screen.
 *   - 5 buttons (register 0xF8E, active-low) mapped to the Flipper's buttons
 *     (direct GPIO polling, like FlipperGB).
 *   - OOK radio: the firmware bit-bangs the RF. It is bridged to the Flipper's
 *     CC1101 (TX of the OOK frame captured from the bit-bang pin; RX feeding the
 *     firmware's data pin with the real RF edges).
 *
 * FLOW (v2):
 *   Phase 1 - MENU (normal GUI with view_dispatcher + submenu + number_input):
 *     * "Firmware: <name>"  -> opens the file browser (.hex/.bin)
 *     * "PIN: <value>"      -> number_input (numeric PIN, default 2552)
 *     * "Launch"            -> starts the emulator
 *     The view_dispatcher BLOCKS until the user chooses Launch or exits.
 *   Phase 2 - EMULATOR (direct-draw takeover, INCOMPATIBLE with view_dispatcher):
 *     after Launch, the view_dispatcher and its views are FULLY released, and
 *     ONLY THEN is the takeover done (gui_direct_draw_acquire +
 *     gui_add_framebuffer_callback) and the emulator loop is run. On exiting the
 *     emulator the app terminates (v1: does not return to the menu).
 *
 * The file browser CANNOT be opened while the view_dispatcher runs (its event
 * loop monopolizes the GUI). The robust approach chosen and DOCUMENTED: the
 * "Firmware" item sets a flag (FlowFileBrowser) and calls view_dispatcher_stop();
 * after returning from run() main opens the file browser and RE-CREATES the
 * menu. Same for "PIN" and "Launch". This way view_dispatcher and file browser /
 * direct-draw never coexist. See PandoraPIC_v2.md.
 *
 * Emulator frontend inspired by FlipperGB (direct-draw takeover, pin polling,
 * safe_malloc, mandatory per-frame yield).
 */

#include <furi.h>
#include <furi_hal.h>
#include <furi_hal_subghz.h>
#include <gui/gui.h>
#include <gui/view_dispatcher.h>
#include <gui/modules/submenu.h>
#include <gui/modules/number_input.h>
#include <gui/modules/widget.h>
#include <input/input.h>
#include <dialogs/dialogs.h>
#include <storage/storage.h>

#include <stdlib.h>
#include <string.h>

#include "lib/pic18core/pic18_core.h"

#define TAG "PandoraPIC"

#define SCREEN_W 128
#define SCREEN_H 64
#define FB_SIZE (SCREEN_W * SCREEN_H / 8) /* 1024 bytes, page-format */

/* Keyfob SSD1306: 128 columns x 4 pages = 512 bytes (128x32) */
#define GDDRAM_COLS 128
#define GDDRAM_PAGES 4
#define GDDRAM_SIZE (GDDRAM_COLS * GDDRAM_PAGES)

#define PROG_MAX 0x1B000u /* covers up to 0x1A17B (mariofull) with margin */

#define ALLOC_MARGIN 1024u
#define FRAME_US 30000 /* ~33 Hz UI refresh; the keyfob is not a game */

#define PIN_DEFAULT 2552

/* On the Flipper malloc() does NOT return NULL: it crashes the firmware with
 * OOM. Every allocation that depends on the .hex size goes through here. */
static void* safe_malloc(size_t size) {
    if(memmgr_heap_get_max_free_block() < size + ALLOC_MARGIN) return NULL;
    return malloc(size);
}

/* ------------------------------------------------------------ HW profiles */

typedef struct {
    const char* name;
    uint32_t send_byte_pc; /* hook of the SW-SPI serializer */
    uint16_t byte_reg; /* bank4 register with the already-assembled byte */
    uint16_t dc_flag_a; /* bank1 flags: nonzero => COMMAND, zero => DATA */
    uint16_t dc_flag_b;
    bool has_pin; /* mariofull has PIN 2552 */
    /* --- boot: the XC8 runtime has a cinit that must be handled --- */
    uint8_t boot_mode; /* 0 = run-to (mariofull, PORTB.1 handshake); 1 = cinit records (mario2) */
    uint32_t cinit_wait_pc; /* mariofull: run up to here (main loop) */
    uint32_t cinit_table; /* mario2: cinit records table */
    uint32_t cinit_table_end;
    uint32_t cinit_entry; /* mario2: run up to here before applying cinit */
    uint32_t cinit_exit; /* mario2: PC after applying cinit */
    bool portb1_handshake; /* mariofull: PORTB.1 always reads 0 (ACK) */

    /* --- PIN (mariofull): validation 0x018D76 compares 2,5,5,2 --- */
    uint32_t pin_validate; /* RCALL of the validation */
    uint32_t pin_accept; /* PC of ACCEPT */
    uint32_t pin_reject; /* PC of REJECT */
    uint16_t pin_slot_lo[4]; /* 4 16-bit integers (lo) in bank1 */
    uint16_t pin_slot_hi[4];
    uint16_t pin_unlock_lo; /* "already unlocked" flag (lo,hi) */
    uint16_t pin_unlock_hi;

    /* --- OOK RF bit-bang: output pin (LAT) and input pin (PORT) --- */
    uint16_t ook_tx_lat; /* absolute LAT SFR of the OOK TX pin */
    uint8_t ook_tx_bit; /* bit of the OOK TX pin */
} PicProfile;

/* mariofull: send 0x174C, byte 0x4FE, flags 0x1F6|0x1F7; boot run-to main loop
 *            0x001712 with PORTB.1 handshake; OOK TX on LATB.0.
 * mario2:    send 0x1758, byte 0x4EF, flags 0x1F7|0x1F8; boot cinit records
 *            (table 0x17530..0x175F8, entry 0x175FA, exit 0x17636); OOK TX LATB.1. */
static const PicProfile PROFILE_FULL = {
    .name = "mariofull",
    .send_byte_pc = 0x00174Cu,
    .byte_reg = 0x4FEu,
    .dc_flag_a = 0x1F6u,
    .dc_flag_b = 0x1F7u,
    .has_pin = true,
    .boot_mode = 0,
    .cinit_wait_pc = 0x001712u,
    .cinit_table = 0,
    .cinit_table_end = 0,
    .cinit_entry = 0,
    .cinit_exit = 0,
    .portb1_handshake = true,
    .pin_validate = 0x018D76u,
    .pin_accept = 0x018DA8u,
    .pin_reject = 0x018DE2u,
    .pin_slot_lo = {0x1D9u, 0x1DBu, 0x1DDu, 0x1DFu},
    .pin_slot_hi = {0x1DAu, 0x1DCu, 0x1DEu, 0x1E0u},
    .pin_unlock_lo = 0x1D7u,
    .pin_unlock_hi = 0x1D8u,
    .ook_tx_lat = 0xF8Bu, /* LATB */
    .ook_tx_bit = 0u, /* LATB.0 */
};
static const PicProfile PROFILE_MARIO2 = {
    .name = "mario2",
    .send_byte_pc = 0x001758u,
    .byte_reg = 0x4EFu,
    .dc_flag_a = 0x1F7u,
    .dc_flag_b = 0x1F8u,
    .has_pin = false,
    .boot_mode = 1,
    .cinit_wait_pc = 0,
    .cinit_table = 0x017530u,
    .cinit_table_end = 0x0175F8u,
    .cinit_entry = 0x0175FAu,
    .cinit_exit = 0x017636u,
    .portb1_handshake = false,
    .pin_validate = 0,
    .pin_accept = 0,
    .pin_reject = 0,
    .pin_slot_lo = {0, 0, 0, 0},
    .pin_slot_hi = {0, 0, 0, 0},
    .pin_unlock_lo = 0,
    .pin_unlock_hi = 0,
    .ook_tx_lat = 0xF8Bu, /* LATB */
    .ook_tx_bit = 1u, /* LATB.1 */
};

/* ------------------------------------------------------------ app state */

enum {
    KBIT_UP = 1 << 0,
    KBIT_DOWN = 1 << 1,
    KBIT_LEFT = 1 << 2,
    KBIT_RIGHT = 1 << 3,
    KBIT_OK = 1 << 4,
    KBIT_BACK = 1 << 5,
};

/* Keyfob buttons in register 0xF8E (active-low). */
#define BTN_REG 0xF8Eu
#define BTN_B1_BIT 1 /* up / + / increment PIN digit */
#define BTN_B2_BIT 2 /* OK / confirm PIN */
#define BTN_B3_BIT 4 /* menu */
#define BTN_B5_BIT 6
#define BTN_B6_BIT 7
#define BTN_ALL_MASK ((1 << 1) | (1 << 2) | (1 << 4) | (1 << 6) | (1 << 7))

/* SFR addresses used in the port read */
#define SFR_PORTB 0xF81u
#define SFR_LATB 0xF8Bu

/* --- CC1101 bridge --- */
/* TX: the firmware toggles the OOK pin (LATB.x). We capture each edge with its
 * duration (measured by cpu->cycles between toggles) in a LevelDuration ring and
 * replay it via furi_hal_subghz_start_async_tx. */
#define TX_RING_LEN 2048u

/* RX: furi_hal_subghz_start_async_rx delivers (level,duration_us) per edge from
 * the IRQ. We enqueue them in a thread-safe queue and in the main loop set the
 * level of the firmware's input pin (on_port_read) with its timing. */
#define RX_QUEUE_LEN 512u

typedef struct {
    bool level;
    uint32_t duration; /* CC1101 ticks (us) */
} RfEdge;

/* PIC cycles->us conversion: the keyfob runs on the internal oscillator; the RE
 * does not fix the absolute Fosc (see RE_DYN_mario2/mariofull "real Te in us
 * DESC"). We use 4MHz => 1 instruction cycle = 1us as a base; it is adjustable
 * and only affects the time scale of the replayed OOK, not its content. */
#define PIC_CYCLE_US_NUM 1u
#define PIC_CYCLE_US_DEN 1u

typedef struct {
    /* core */
    Pic18Cpu* cpu;
    uint8_t* prog; /* program buffer (safe_malloc) */
    const PicProfile* prof;

    /* reconstructed SSD1306 GDDRAM */
    uint8_t gddram[GDDRAM_SIZE];
    uint16_t gd_page;
    uint16_t gd_col;
    int gd_cmd_arg_left; /* multi-byte commands: how many args remain */

    /* Flipper framebuffer (1bpp, bit=1 => light) */
    uint8_t screen[FB_SIZE];

    Gui* gui;
    Canvas* canvas;
    FuriMutex* fb_mutex;

    /* buttons */
    uint8_t btn_mask; /* BTN_*_BIT bits currently pressed */

    /* CC1101 radio */
    bool radio_on; /* CC1101 acquired/configured */
    bool rf_tx_enabled; /* TX bridge active (captures firmware edges) */
    bool rf_rx_enabled; /* RX bridge active (injects real RF edges) */
    uint32_t frequency;

    /* TX bridge: ring of edges captured from the firmware's OOK pin */
    RfEdge tx_ring[TX_RING_LEN];
    volatile uint32_t tx_head; /* producer (main loop, on_lat_write) */
    volatile uint32_t tx_tail; /* consumer (async_tx callback) */
    bool tx_last_level; /* last level of the OOK pin */
    uint64_t tx_last_cycles; /* CPU cycles at the last edge */
    bool tx_active; /* async_tx in progress */
    uint32_t tx_frames; /* counter of logged TX frames */

    /* RX bridge: thread-safe queue fed from the CC1101 IRQ */
    FuriMessageQueue* rx_queue;
    uint16_t rx_in_port; /* PORT SFR of the firmware's RX input pin */
    uint8_t rx_in_bit; /* bit of the RX input pin */
    bool rx_cur_level; /* current level injected into the input pin */
    uint32_t rx_level_until; /* tick until which to hold rx_cur_level */
    uint32_t rx_events; /* counter of injected RX edges */

    volatile bool exit_requested;
    volatile bool menu_requested; /* in-emulator help overlay */
    bool menu_active;

    uint32_t steps_per_frame;

    /* PIN chosen in the menu (numeric; 2552 by default). */
    int32_t pin_value;
    bool pin_entered; /* the PIN was already injected after boot */

    /* --- phase 1 menu (view_dispatcher) --- */
    ViewDispatcher* view_dispatcher;
    Submenu* submenu;
    NumberInput* number_input;
    Widget* about; /* About screen (scrollable help text) */
    uint8_t menu_current_view; /* current phase-1 view (PandoraView; for Back routing) */
    FuriString* fw_path; /* chosen firmware path */
    char fw_name[32]; /* short name for the label */
} AppState;

static AppState* g_app = NULL;
static volatile uint32_t s_fb_cb_inflight = 0;

/* ---------------------------------------------------- SSD1306 GDDRAM model */

/* Decodes a byte sent to the SSD1306 according to the DC level.
 * DC=0 => command (addressing/config), DC=1 => pixel data. */
static void gddram_feed(AppState* app, uint8_t b, bool is_cmd) {
    if(is_cmd) {
        if(app->gd_cmd_arg_left > 0) {
            /* argument of a multi-byte command: 0x21 col start/end, 0x22 page */
            app->gd_cmd_arg_left--;
            return;
        }
        if(b >= 0xB0 && b <= 0xB7) {
            app->gd_page = b - 0xB0;
        } else if(b <= 0x0F) {
            app->gd_col = (app->gd_col & 0xF0) | b;
        } else if(b >= 0x10 && b <= 0x1F) {
            app->gd_col = (app->gd_col & 0x0F) | ((b & 0x0F) << 4);
        } else if(b == 0x20) {
            app->gd_cmd_arg_left = 1; /* addressing mode takes 1 arg */
        } else if(b == 0x21) {
            app->gd_cmd_arg_left = 2; /* col start, col end */
            app->gd_col = 0;
        } else if(b == 0x22) {
            app->gd_cmd_arg_left = 2; /* page start, page end */
            app->gd_page = 0;
        } else if(
            b == 0x81 || b == 0xA8 || b == 0xD3 || b == 0xDA || b == 0xD5 || b == 0xD9 ||
            b == 0xDB || b == 0x8D || b == 0xAD) {
            app->gd_cmd_arg_left = 1; /* 1-argument commands */
        }
        /* 1-byte commands (0x40,0xA1,0xC8,0xA4,0xA6,0xAF,0xAE...) do not move pointers */
    } else {
        /* data: 8 vertical pixels of (page,col), bit0 at top */
        if(app->gd_page < GDDRAM_PAGES && app->gd_col < GDDRAM_COLS) {
            app->gddram[app->gd_page * GDDRAM_COLS + app->gd_col] = b;
        }
        app->gd_col++;
        if(app->gd_col >= GDDRAM_COLS) {
            app->gd_col = 0;
            app->gd_page = (app->gd_page + 1) % GDDRAM_PAGES;
        }
    }
}

/* Code hook: on entering the send-byte serializer, reads the already-assembled
 * byte and the DC level from the firmware's flags, and feeds it into the GDDRAM.
 * This way we capture EVERYTHING the firmware "draws" without touching it. */
static void code_hook(Pic18Cpu* cpu, uint32_t pc, void* ctx) {
    AppState* app = (AppState*)ctx;
    if(pc != app->prof->send_byte_pc) return;
    uint8_t b = pic18_read_ram(cpu, app->prof->byte_reg);
    uint8_t fa = pic18_read_ram(cpu, app->prof->dc_flag_a);
    uint8_t fb = pic18_read_ram(cpu, app->prof->dc_flag_b);
    bool is_cmd = (fa != 0) || (fb != 0); /* nonzero => COMMAND */
    gddram_feed(app, b, is_cmd);
}

/* ----------------------------------------------------- buttons (PORT read) */

/* The firmware reads the 5 buttons in register 0xF8E (active-low). We return
 * 0xFF with the pressed bits at 0. For PORTB: mariofull's handshake. For the RX
 * input pin (if the RX bridge is active) we inject the current level. */
static int port_read_hook(Pic18Cpu* cpu, uint16_t addr, void* ctx) {
    AppState* app = (AppState*)ctx;
    if(addr == BTN_REG) {
        return (int)(0xFFu & ~app->btn_mask);
    }
    if(addr == SFR_PORTB && app->prof && app->prof->portb1_handshake) {
        uint8_t latb = pic18_read_ram(cpu, SFR_LATB);
        uint8_t v = (uint8_t)(latb & (uint8_t)~0x02u); /* bit1 = 0 (ACK) */
        /* If the RX bridge uses PORTB, we overlay the injected level. */
        if(app->rf_rx_enabled && app->rx_in_port == SFR_PORTB) {
            if(app->rx_cur_level)
                v |= (uint8_t)(1u << app->rx_in_bit);
            else
                v &= (uint8_t)~(1u << app->rx_in_bit);
        }
        return (int)v;
    }
    if(app->rf_rx_enabled && addr == app->rx_in_port) {
        uint8_t v = pic18_read_ram(cpu, addr);
        if(app->rx_cur_level)
            v |= (uint8_t)(1u << app->rx_in_bit);
        else
            v &= (uint8_t)~(1u << app->rx_in_bit);
        return (int)v;
    }
    return -1; /* default */
}

/* LAT write hook: when the TX bridge is active, detects the toggles of the
 * firmware's OOK pin and accumulates the edges with their duration (measured by
 * cpu->cycles between toggles) in the TX ring. The async_tx callback drains them. */
static void lat_write_hook(Pic18Cpu* cpu, uint16_t addr, uint8_t val, void* ctx) {
    AppState* app = (AppState*)ctx;
    if(!app->rf_tx_enabled) return;
    if(addr != app->prof->ook_tx_lat) return;
    bool level = (val >> app->prof->ook_tx_bit) & 1u;
    if(level == app->tx_last_level) return; /* no edge */

    uint64_t now = cpu->cycles;
    uint64_t dcyc = now - app->tx_last_cycles;
    app->tx_last_cycles = now;

    /* duration of the level that JUST ended (tx_last_level) */
    uint32_t dur_us = (uint32_t)((dcyc * PIC_CYCLE_US_NUM) / PIC_CYCLE_US_DEN);
    if(dur_us == 0) dur_us = 1;

    uint32_t head = app->tx_head;
    uint32_t next = (head + 1) % TX_RING_LEN;
    if(next != app->tx_tail) { /* ring not full */
        app->tx_ring[head].level = app->tx_last_level;
        app->tx_ring[head].duration = dur_us;
        app->tx_head = next;
    }
    app->tx_last_level = level;
}

static void set_button_bit(AppState* app, uint8_t bit, bool pressed) {
    if(pressed)
        app->btn_mask |= (uint8_t)(1u << bit);
    else
        app->btn_mask &= (uint8_t)~(1u << bit);
    app->btn_mask &= BTN_ALL_MASK;
}

/* ------------------------------------------------------------ rendering */

/* The GDDRAM is 128x32 (4 pages). The Flipper's screen is 128x64. We draw it
 * vertically centered (rows 16..47) at 1:1 scale, 1 pixel per pixel. screen[]
 * uses the Flipper page format: byte (x, page) with bit = row&7, bit=1 => light. */
static void render_gddram(AppState* app) {
    furi_mutex_acquire(app->fb_mutex, FuriWaitForever);
    memset(app->screen, 0, FB_SIZE);
    const int y_off = 16; /* center 32 rows in 64 */
    for(int page = 0; page < GDDRAM_PAGES; page++) {
        for(int col = 0; col < GDDRAM_COLS; col++) {
            uint8_t v = app->gddram[page * GDDRAM_COLS + col];
            for(int bit = 0; bit < 8; bit++) {
                int src_y = page * 8 + bit; /* 0..31 */
                if(!((v >> bit) & 1)) continue;
                int y = y_off + src_y;
                int x = col;
                if(x < 0 || x >= SCREEN_W || y < 0 || y >= SCREEN_H) continue;
                app->screen[(y >> 3) * SCREEN_W + x] |= (uint8_t)(1u << (y & 7));
            }
        }
    }
    furi_mutex_release(app->fb_mutex);
}

/* Framebuffer commit callback (direct draw). screen bit=1 => light; the display
 * buffer is bit=1 => dark, hence the XOR. */
static void framebuffer_commit_callback(
    uint8_t* data,
    size_t size,
    CanvasOrientation orientation,
    void* context) {
    __atomic_fetch_add(&s_fb_cb_inflight, 1, __ATOMIC_RELAXED);
    AppState* app = (AppState*)context;
    UNUSED(orientation);
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
        data[i] = (uint8_t)(src[i] ^ 0xFF);
    }
    furi_mutex_release(app->fb_mutex);
    __atomic_fetch_sub(&s_fb_cb_inflight, 1, __ATOMIC_RELAXED);
}

/* ------------------------------------------------------------ input */

static void wait_inflight_zero(volatile uint32_t* counter) {
    while(__atomic_load_n(counter, __ATOMIC_ACQUIRE) != 0) {
        furi_delay_ms(1);
    }
}

/* Direct pin polling (avoids the input service, like FlipperGB). Maps:
 *   Flipper Up    -> B1 (increment PIN digit / up)
 *   Flipper Down  -> B3 (menu/down)
 *   Flipper Ok    -> B2 (confirm)
 *   Flipper Left  -> B5
 *   Flipper Right -> B6
 *   Flipper Back  -> (handled for exit / menu)
 * Up+Down simultaneously = help overlay / RF toggle (a physically impossible
 * gesture).
 */
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

static void apply_keys_to_buttons(AppState* app, uint8_t keys) {
    /* Up+Down together open the help overlay (and toggle the RF bridge). */
    bool combo_menu = (keys & (KBIT_UP | KBIT_DOWN)) == (KBIT_UP | KBIT_DOWN);
    if(combo_menu) {
        app->menu_requested = true;
        set_button_bit(app, BTN_B1_BIT, false);
        set_button_bit(app, BTN_B3_BIT, false);
    } else {
        set_button_bit(app, BTN_B1_BIT, keys & KBIT_UP);
        set_button_bit(app, BTN_B3_BIT, keys & KBIT_DOWN);
    }
    set_button_bit(app, BTN_B2_BIT, keys & KBIT_OK);
    set_button_bit(app, BTN_B5_BIT, keys & KBIT_LEFT);
    set_button_bit(app, BTN_B6_BIT, keys & KBIT_RIGHT);
}

/* ------------------------------------------------------------ .hex loading */

typedef enum {
    LoadOk,
    LoadIoError,
    LoadNoMem,
    LoadBadFormat,
} LoadResult;

/* Decodes 2 hex chars. -1 if invalid. */
static int pic_hexbyte(const char* s) {
    int hi = (unsigned char)s[0], lo = (unsigned char)s[1];
    if(hi >= '0' && hi <= '9') hi -= '0';
    else { hi |= 0x20; if(hi >= 'a' && hi <= 'f') hi = hi - 'a' + 10; else return -1; }
    if(lo >= '0' && lo <= '9') lo -= '0';
    else { lo |= 0x20; if(lo >= 'a' && lo <= 'f') lo = lo - 'a' + 10; else return -1; }
    return (hi << 4) | lo;
}

/* Processes an Intel HEX line (without ':') writing to cpu->prog and config
 * words. Returns 1 EOF, 0 continue, -1 error. */
static int pic_ihex_line(Pic18Cpu* cpu, const char* ln, size_t len, uint32_t* ext_lin) {
    if(len < 10) return 0;
    int count = pic_hexbyte(ln);
    int ah = pic_hexbyte(ln + 2);
    int al = pic_hexbyte(ln + 4);
    int rtype = pic_hexbyte(ln + 6);
    if(count < 0 || ah < 0 || al < 0 || rtype < 0) return -1;
    uint32_t addr = ((uint32_t)ah << 8) | (uint32_t)al;
    const char* data = ln + 8;
    if(rtype == 0x00) {
        uint32_t base = *ext_lin + addr;
        for(int k = 0; k < count; k++) {
            int b = pic_hexbyte(data + k * 2);
            if(b < 0) return -1;
            uint32_t a = base + (uint32_t)k;
            if(a < cpu->prog_size) {
                cpu->prog[a] = (uint8_t)b;
            } else if(a >= 0x300000u && cpu->config_count < 64) {
                /* config words: store in the core's table */
                cpu->config_addr[cpu->config_count] = a;
                cpu->config_val[cpu->config_count] = (uint8_t)b;
                cpu->config_count++;
            }
        }
    } else if(rtype == 0x01) {
        return 1;
    } else if(rtype == 0x04) {
        int b0 = pic_hexbyte(data), b1 = pic_hexbyte(data + 2);
        if(b0 < 0 || b1 < 0) return -1;
        *ext_lin = (((uint32_t)b0 << 8) | (uint32_t)b1) << 16;
    } else if(rtype == 0x02) {
        int b0 = pic_hexbyte(data), b1 = pic_hexbyte(data + 2);
        if(b0 < 0 || b1 < 0) return -1;
        *ext_lin = (((uint32_t)b0 << 8) | (uint32_t)b1) << 4;
    }
    return 0;
}

/* STREAMING load from the SD (does not read the whole .hex into RAM; the .hex
 * files are 300KB+ and the Flipper has little heap). */
static LoadResult load_firmware(AppState* app, Storage* storage, const char* path) {
    File* f = storage_file_alloc(storage);
    LoadResult res = LoadIoError;
    do {
        if(!storage_file_open(f, path, FSAM_READ, FSOM_OPEN_EXISTING)) break;
        uint64_t fsize = storage_file_size(f);
        if(fsize == 0 || fsize > 4u * 1024u * 1024u) {
            res = LoadBadFormat;
            break;
        }

        app->prog = (uint8_t*)safe_malloc(PROG_MAX);
        if(!app->prog) {
            FURI_LOG_E(TAG, "no heap for prog (%u B)", (unsigned)PROG_MAX);
            res = LoadNoMem;
            break;
        }
        memset(app->prog, 0xFF, PROG_MAX);
        pic18_set_prog(app->cpu, app->prog, PROG_MAX);
        app->cpu->config_count = 0;

        uint8_t first = 0;
        if(storage_file_read(f, &first, 1) != 1) break;
        storage_file_seek(f, 0, true);

        /* shared streaming buffers (static: not on the stack). ~1.1KB .bss */
        static uint8_t chunk[512];
        static char line[600];
        size_t rd;

        if(first == ':') {
            size_t linepos = 0;
            uint32_t ext_lin = 0;
            bool eof_rec = false, err = false;
            while(!eof_rec && (rd = storage_file_read(f, chunk, sizeof(chunk))) > 0) {
                for(size_t i = 0; i < rd; i++) {
                    char ch = (char)chunk[i];
                    if(ch == ':') {
                        linepos = 0;
                    } else if(ch == '\n' || ch == '\r') {
                        if(linepos > 0) {
                            int r = pic_ihex_line(app->cpu, line, linepos, &ext_lin);
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
            if(err) { res = LoadBadFormat; break; }
        } else {
            /* flat binary: copy in chunks directly to prog */
            uint32_t off = 0;
            while((rd = storage_file_read(f, chunk, sizeof(chunk))) > 0) {
                for(size_t i = 0; i < rd && off < PROG_MAX; i++, off++)
                    app->prog[off] = chunk[i];
                if(off >= PROG_MAX) break;
            }
        }
        res = LoadOk;
    } while(false);
    storage_file_close(f);
    storage_file_free(f);
    return res;
}

/* Selects the profile by file name (mariofull vs mario2) or by the config words
 * (CONFIG3L 0xA2 full / 0xBF mario2). Defaults to mariofull. */
static const PicProfile* pick_profile(const char* path, Pic18Cpu* cpu) {
    if(path) {
        if(strstr(path, "full") || strstr(path, "2552")) return &PROFILE_FULL;
        if(strstr(path, "MariO_2") || strstr(path, "mario2")) return &PROFILE_MARIO2;
    }
    /* CONFIG3L en 0x300005: 0xA2=full, 0xBF=mario2 */
    for(uint16_t i = 0; i < cpu->config_count; i++) {
        if(cpu->config_addr[i] == 0x300005u) {
            return (cpu->config_val[i] == 0xA2u) ? &PROFILE_FULL : &PROFILE_MARIO2;
        }
    }
    return &PROFILE_FULL;
}

/* ------------------------------------------------------------ boot/cinit */

/* Applies the XC8 runtime cinit records (mario2): flash->RAM copy from the
 * initialization table. Equivalent to emu/boot_mario2.py apply_cinit. Only
 * writes GPR RAM (<0xF80), not SFR. */
static void apply_cinit_records(AppState* app) {
    Pic18Cpu* cpu = app->cpu;
    uint32_t tp = app->prof->cinit_table;
    uint32_t end = app->prof->cinit_table_end;
    uint16_t fsr0 = 0x000;
    int guard = 0;
    while(tp < end && guard++ < 512) {
        uint8_t n = (tp < cpu->prog_size) ? cpu->prog[tp] : 0;
        tp++;
        if(n == 0) break;
        uint8_t fd = (tp < cpu->prog_size) ? cpu->prog[tp] : 0;
        tp++;
        if(fd & 0x80) {
            uint8_t lo = (tp < cpu->prog_size) ? cpu->prog[tp] : 0;
            tp++;
            fsr0 = (uint16_t)(((fd & 0x0F) << 8) | lo);
        }
        if(!(fd & 0x40)) {
            tp++; /* consume 1 extra byte (like the python) */
        }
        if(tp + n > end + 2) break; /* record runs off the table: desync */
        for(uint8_t k = 0; k < n; k++) {
            uint8_t b = (tp < cpu->prog_size) ? cpu->prog[tp] : 0;
            tp++;
            if(fsr0 < 0xF80u) cpu->ram[fsr0 & 0xFFF] = b;
            fsr0 = (uint16_t)((fsr0 + 1) & 0xFFF);
        }
    }
}

/* Takes the firmware from reset to the main loop, with the runtime init resolved
 * according to the profile. Returns true if the boot reached its destination. */
static bool boot_firmware(AppState* app) {
    Pic18Cpu* cpu = app->cpu;
    pic18_reset(cpu);
    if(app->prof->boot_mode == 1) {
        /* mario2: run prologue+data-init up to the cinit entry, apply the
         * records, jump to the cinit exit. */
        uint64_t budget = 2000000;
        while(cpu->pc != app->prof->cinit_entry && budget > 0) {
            pic18_step(cpu);
            budget--;
            if(cpu->halted) break;
        }
        if(cpu->pc != app->prof->cinit_entry) return false;
        apply_cinit_records(app);
        cpu->pc = app->prof->cinit_exit;
        return true;
    } else {
        /* mariofull: the PORTB.1 handshake (port_read_hook) lets the cinit finish
         * on its own; we run up to the main loop. */
        uint64_t budget = 3000000;
        while(cpu->pc != app->prof->cinit_wait_pc && budget > 0) {
            pic18_step(cpu);
            budget--;
            if(cpu->halted) break;
        }
        return cpu->pc == app->prof->cinit_wait_pc;
    }
}

/* ------------------------------------------------------------ PIN inject */

/* Return sentinel placed on the stack before triggering the validation. */
#define PIN_RET_SENTINEL 0x01FFFEu

/* Result of the PIN validation (filled by pin_code_hook via ctx). */
typedef struct {
    const PicProfile* prof;
    int accept;
    int reject;
    int done;
} PinRun;

/* Code hook used only during enter_pin(): detects ACCEPT/REJECT and stops at the
 * sentinel. The context (ctx) is a PinRun* local to this run; this way we do NOT
 * need global state and it is reentrant. */
static void pin_code_hook(Pic18Cpu* cpu, uint32_t pc, void* ctx) {
    PinRun* r = (PinRun*)ctx;
    if(pc == r->prof->pin_accept) {
        r->accept = 1;
        r->done = 1;
    } else if(pc == r->prof->pin_reject) {
        r->reject = 1;
        r->done = 1;
    } else if(pc == PIN_RET_SENTINEL) {
        cpu->halted = 1;
        r->done = 1;
    }
}

/* Injects the numeric PIN chosen in the menu into the PIN FSM buffer and triggers
 * the firmware's REAL validation (0x018D76), just like emu/pic18_tui.py
 * enter_pin. Logs the ACCEPT/REJECT verdict. Only applies to mariofull (has_pin);
 * for mario2 the PIN field is ignored.
 *
 * The core's prog/RAM content after boot stays intact except for the PIN buffer:
 * the validation runs with its own stack/BSR and a return sentinel, and we
 * restore PC/stack/BSR/stkptr so as not to disturb the emulator's main loop
 * (which starts right after from cinit_wait_pc / cinit_exit). */
static void enter_pin(AppState* app) {
    const PicProfile* prof = app->prof;
    if(!prof->has_pin || app->pin_entered) return;
    app->pin_entered = true;

    Pic18Cpu* cpu = app->cpu;

    /* decompose the numeric PIN into 4 decimal digits (2552 -> 2,5,5,2) */
    int32_t v = app->pin_value;
    if(v < 0) v = 0;
    int digits[4];
    digits[0] = (v / 1000) % 10;
    digits[1] = (v / 100) % 10;
    digits[2] = (v / 10) % 10;
    digits[3] = v % 10;

    /* 1) compose the PIN buffer (result of the B1/B2 presses) */
    for(int i = 0; i < 4; i++) {
        pic18_write_ram(cpu, prof->pin_slot_lo[i], (uint8_t)(digits[i] & 0xFF));
        pic18_write_ram(cpu, prof->pin_slot_hi[i], (uint8_t)((digits[i] >> 8) & 0xFF));
    }
    pic18_write_ram(cpu, prof->pin_unlock_lo, 0); /* "already unlocked" flag = 0 */
    pic18_write_ram(cpu, prof->pin_unlock_hi, 0);

    /* 2) run the REAL validation with a return sentinel and an ACCEPT/REJECT
     * hook. We save and restore the main loop context. */
    uint32_t saved_pc = cpu->pc;
    uint8_t saved_bsr = cpu->ram[PIC18_SFR_BSR];
    uint8_t saved_stkptr = cpu->stkptr;
    uint8_t saved_halted = cpu->halted;
    Pic18CodeHook saved_hook = cpu->on_code;
    void* saved_ctx = cpu->code_ctx;

    PinRun run = {.prof = prof, .accept = 0, .reject = 0, .done = 0};
    cpu->on_code = pin_code_hook;
    cpu->code_ctx = &run;

    cpu->halted = 0;
    cpu->stkptr = 0;
    cpu->ram[PIC18_SFR_BSR] = 0;
    pic18_push(cpu, PIN_RET_SENTINEL); /* return sentinel */
    cpu->pc = prof->pin_validate;

    uint64_t budget = 4000000;
    while(budget-- > 0 && !cpu->halted && !run.done) {
        pic18_step(cpu);
    }

    /* restore the main loop context */
    cpu->on_code = saved_hook;
    cpu->code_ctx = saved_ctx;
    cpu->pc = saved_pc;
    cpu->ram[PIC18_SFR_BSR] = saved_bsr;
    cpu->stkptr = saved_stkptr;
    cpu->halted = saved_halted;

    if(run.accept) {
        FURI_LOG_I(TAG, "PIN %04ld -> ACCEPT (OOK service TX)", (long)app->pin_value);
    } else if(run.reject) {
        FURI_LOG_I(TAG, "PIN %04ld -> REJECT (no transmit)", (long)app->pin_value);
    } else {
        FURI_LOG_W(TAG, "PIN %04ld -> no verdict (budget exhausted)", (long)app->pin_value);
    }
}

/* ------------------------------------------------------------ CC1101 RF */

/* OOK 650kHz async preset in the furi_hal_subghz_load_custom_preset format:
 * {reg, val} pairs terminated by 0x00, then 2 bytes (ignored) + 8 bytes of
 * PA table. Values aligned with the firmware's async OOK table
 * (lib/subghz/devices/cc1101_configs.c: preset_ook_650khz_async_regs). */
static const uint8_t PRESET_OOK650[] = {
    0x02, 0x0D, /* IOCFG0: async serial data out/in */
    0x07, 0x04, /* FIFOTHR */
    0x08, 0x32, /* PKTCTRL0: async, continuous, no whitening */
    0x0B, 0x06, /* FSCTRL1 */
    0x10, 0xF8, /* MDMCFG4 (bw) */
    0x11, 0x32, /* MDMCFG3 (data rate) */
    0x12, 0x30, /* MDMCFG2: ASK/OOK, no preamble/sync */
    0x14, 0x00, /* MDMCFG0 */
    0x15, 0x00, /* DEVIATN */
    0x18, 0x18, /* MCSM0 */
    0x19, 0x16, /* FOCCFG */
    0x1B, 0x07, /* AGCCTRL2 */
    0x1C, 0x00, /* AGCCTRL1 */
    0x1D, 0x91, /* AGCCTRL0 */
    0x20, 0xFB, /* WORCTRL */
    0x21, 0xB6, /* FREND1 */
    0x22, 0x11, /* FREND0 */
    0x00, /* terminator */
    0x00, 0x00, /* 2 bytes (ignored by the parser before PA) */
    0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* PA table (OOK: C0) */
};

/* The Pandora firmware does its own OOK modulation by bit-bang; here we
 * initialize the Flipper's CC1101 (async RX/TX) for the radio bridge. */
static void radio_init(AppState* app) {
    app->frequency = 433920000;
    furi_hal_subghz_reset();
    furi_hal_subghz_load_custom_preset(PRESET_OOK650);
    furi_hal_subghz_set_frequency_and_path(app->frequency);
    furi_hal_subghz_idle();
    app->radio_on = true;
    /* default RX input pin: the PORTB handshake (mariofull) or PORTB in general;
     * it is the pin the firmware samples. It can be adjusted per profile. */
    app->rx_in_port = SFR_PORTB;
    app->rx_in_bit = 0;
    FURI_LOG_I(
        TAG, "CC1101 init OOK650 @%lu Hz", (unsigned long)app->frequency);
}

static void radio_deinit(AppState* app) {
    if(!app->radio_on) return;
    /* stop any async in progress */
    if(app->tx_active) {
        furi_hal_subghz_stop_async_tx();
        app->tx_active = false;
    }
    if(app->rf_rx_enabled) {
        furi_hal_subghz_stop_async_rx();
        app->rf_rx_enabled = false;
    }
    furi_hal_subghz_idle();
    furi_hal_subghz_sleep();
    app->radio_on = false;
    FURI_LOG_I(TAG, "CC1101 sleep");
}

/* --- TX bridge --- */

/* async_tx callback: drains the ring of edges captured from the OOK pin. */
static LevelDuration radio_tx_callback(void* context) {
    AppState* app = (AppState*)context;
    uint32_t tail = app->tx_tail;
    if(tail == app->tx_head) {
        /* nothing more to send for now: end the burst */
        return level_duration_reset();
    }
    RfEdge e = app->tx_ring[tail];
    app->tx_tail = (tail + 1) % TX_RING_LEN;
    return level_duration_make(e.level, e.duration);
}

/* Launches the replay of the accumulated edges if there are enough and there is
 * no TX in progress. Called in the main loop. */
static void tx_bridge_flush(AppState* app) {
    if(!app->rf_tx_enabled || !app->radio_on) return;
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
    /* count pending edges */
    uint32_t pending = (app->tx_head + TX_RING_LEN - app->tx_tail) % TX_RING_LEN;
    if(pending < 16) return; /* wait for a reasonable burst */
    if(!furi_hal_subghz_is_tx_allowed(app->frequency)) {
        FURI_LOG_E(
            TAG, "TX NOT allowed @%lu Hz; discarding %lu edges",
            (unsigned long)app->frequency, (unsigned long)pending);
        app->tx_tail = app->tx_head; /* discard */
        return;
    }
    FURI_LOG_I(TAG, "TX async start: %lu edges", (unsigned long)pending);
    if(furi_hal_subghz_start_async_tx(radio_tx_callback, app)) {
        app->tx_active = true;
    } else {
        FURI_LOG_E(TAG, "start_async_tx failed");
        app->tx_tail = app->tx_head;
    }
}

/* --- RX bridge --- */

/* CC1101 callback (runs in IRQ): enqueues the edge. Do NOT block. */
static void radio_rx_callback(bool level, uint32_t duration, void* context) {
    AppState* app = (AppState*)context;
    RfEdge e = {.level = level, .duration = duration};
    /* timeout 0: if the queue is full, the edge is lost (better than blocking in
     * the IRQ). */
    furi_message_queue_put(app->rx_queue, &e, 0);
}

static void rx_bridge_start(AppState* app) {
    if(app->rf_rx_enabled || !app->radio_on) return;
    if(app->tx_active) {
        furi_hal_subghz_stop_async_tx();
        app->tx_active = false;
    }
    furi_hal_subghz_idle();
    furi_message_queue_reset(app->rx_queue);
    app->rx_cur_level = false;
    app->rx_level_until = furi_get_tick();
    furi_hal_subghz_start_async_rx(radio_rx_callback, app);
    app->rf_rx_enabled = true;
    FURI_LOG_I(TAG, "RX bridge ON (injects into %03X.%u)", app->rx_in_port, app->rx_in_bit);
}

static void rx_bridge_stop(AppState* app) {
    if(!app->rf_rx_enabled) return;
    furi_hal_subghz_stop_async_rx();
    furi_hal_subghz_idle();
    app->rf_rx_enabled = false;
    FURI_LOG_I(TAG, "RX bridge OFF (%lu edges injected)", (unsigned long)app->rx_events);
}

/* Drains the RX queue and updates the level injected into the firmware's input
 * pin. The timing is honored by each edge's duration; the firmware reads the pin
 * via on_port_read (port_read_hook) and demodulates on its own. */
static void rx_bridge_pump(AppState* app) {
    if(!app->rf_rx_enabled) return;
    uint32_t now = furi_get_tick();
    /* while the current level has already "fulfilled" its duration, we pull the
     * next edge from the queue. */
    RfEdge e;
    int drained = 0;
    while((int32_t)(now - app->rx_level_until) >= 0 &&
          furi_message_queue_get(app->rx_queue, &e, 0) == FuriStatusOk) {
        app->rx_cur_level = e.level;
        /* duration comes in us; the Flipper tick is ms. We hold the level for at
         * least 1 tick (sub-ms edges are aggregated de facto). */
        uint32_t dur_ms = e.duration / 1000u;
        if(dur_ms == 0) dur_ms = 1;
        app->rx_level_until = now + dur_ms;
        app->rx_events++;
        drained++;
        if(drained >= 64) break; /* don't hog the frame */
    }
    if(drained) {
        FURI_LOG_D(
            TAG, "RX injected %d edges (total %lu)", drained,
            (unsigned long)app->rx_events);
    }
}

/* Cycles the RF bridge: OFF -> TX -> RX -> OFF. Logged. */
static void rf_cycle_mode(AppState* app) {
    if(!app->rf_tx_enabled && !app->rf_rx_enabled) {
        /* OFF -> TX */
        app->rf_tx_enabled = true;
        app->tx_last_level = (pic18_read_ram(app->cpu, app->prof->ook_tx_lat) >>
                              app->prof->ook_tx_bit) &
                             1u;
        app->tx_last_cycles = app->cpu->cycles;
        app->tx_head = app->tx_tail = 0;
        FURI_LOG_I(TAG, "RF mode TX (capture OOK pin %03X.%u)",
                   app->prof->ook_tx_lat, app->prof->ook_tx_bit);
    } else if(app->rf_tx_enabled && !app->rf_rx_enabled) {
        /* TX -> RX */
        app->rf_tx_enabled = false;
        if(app->tx_active) {
            furi_hal_subghz_stop_async_tx();
            app->tx_active = false;
        }
        rx_bridge_start(app);
    } else {
        /* RX -> OFF */
        rx_bridge_stop(app);
    }
}

/* ------------------------------------------------------------ help overlay */

static void overlay_draw(AppState* app) {
    Canvas* c = app->canvas;
    canvas_reset(c);
    canvas_clear(c);
    canvas_set_font(c, FontPrimary);
    canvas_draw_str(c, 2, 10, "Pandora PIC");
    canvas_draw_line(c, 0, 12, 127, 12);
    canvas_set_font(c, FontSecondary);
    char buf[48];
    snprintf(buf, sizeof(buf), "FW: %s", app->prof ? app->prof->name : "?");
    canvas_draw_str(c, 2, 24, buf);
    const char* rf = "OFF";
    if(app->rf_tx_enabled) rf = "TX";
    if(app->rf_rx_enabled) rf = "RX";
    snprintf(buf, sizeof(buf), "RF:%s %luMHz", rf, (unsigned long)(app->frequency / 1000000u));
    canvas_draw_str(c, 2, 34, buf);
    canvas_draw_str(c, 2, 44, "OK=cont  Left=RF  Back=exit");
    canvas_commit(c);
}

/* ------------------------------------------------------------ phase 1 menu */

/* view_dispatcher views */
typedef enum {
    PandoraViewSubmenu,
    PandoraViewNumberInput,
    PandoraViewAbout,
} PandoraView;

/* Main submenu items */
typedef enum {
    MenuItemFirmware,
    MenuItemPin,
    MenuItemLaunch,
    MenuItemAbout,
} MenuItem;

/* Phase 1 result: which action the user requested when stopping the dispatcher. */
typedef enum {
    FlowExit, /* exit the app */
    FlowFileBrowser, /* open the file browser and return to the menu */
    FlowPinInput, /* enter the number_input (handled inside the dispatcher) */
    FlowLaunch, /* start the emulator */
} MenuFlow;

static volatile MenuFlow g_menu_flow = FlowExit;

/* submenu item selection callback (declared before menu_rebuild) */
static void menu_submenu_callback(void* context, uint32_t index);

/* About text (scrollable). The button mapping below MUST match what the code
 * actually does: see apply_keys_to_buttons() and the emulator main loop.
 *   Flipper Up    -> keyfob B1 (bit1 of reg 0xF8E) : up / increment PIN digit
 *   Flipper Ok    -> keyfob B2 (bit2)              : confirm / OK
 *   Flipper Down  -> keyfob B3 (bit4)              : menu / down
 *   Flipper Left  -> keyfob B5 (bit6)
 *   Flipper Right -> keyfob B6 (bit7)
 *   Up+Down       -> in-emulator overlay; inside it Left cycles RF OFF/TX/RX. */
static const char* const ABOUT_TEXT =
    "\e#Pandora DXL-5000 (PIC)\n"
    "Emulator of the real Pandora DXL-5000 car-alarm keyfob firmware "
    "(Microchip PIC18). The firmware runs unmodified; this app emulates the "
    "keyfob hardware: the SSD1306 OLED (128x32) and the physical buttons, and "
    "bridges the Flipper CC1101 for real RX/TX.\n"
    "\n"
    "\e#Buttons (Flipper key = keyfob button):\n"
    "Up = B1: up / increment PIN digit\n"
    "Ok = B2: confirm / OK\n"
    "Down = B3: menu / down\n"
    "Left = B5\n"
    "Right = B6\n"
    "Back: exit emulator / overlay\n"
    "\n"
    "\e#RF:\n"
    "Up+Down opens the overlay; inside it Ok continues, Left cycles RF "
    "OFF/TX/RX, Back exits.\n"
    "\n"
    "\e#PIN:\n"
    "The 'full' firmware (dxl5000_full_pin2552) needs PIN 2552, set it in the "
    "menu before Launch. mario2 has no PIN.";

/* Rebuilds the submenu (updated Firmware/PIN labels). The submenu module COPIES
 * the label internally, so stack buffers are fine. */
static void menu_rebuild(AppState* app) {
    submenu_reset(app->submenu);
    submenu_set_header(app->submenu, "Pandora PIC");
    char buf[48];
    snprintf(buf, sizeof(buf), "Firmware: %s", app->fw_name[0] ? app->fw_name : "<none>");
    submenu_add_item(app->submenu, buf, MenuItemFirmware, menu_submenu_callback, app);
    snprintf(buf, sizeof(buf), "PIN: %ld", (long)app->pin_value);
    submenu_add_item(app->submenu, buf, MenuItemPin, menu_submenu_callback, app);
    submenu_add_item(app->submenu, "Launch", MenuItemLaunch, menu_submenu_callback, app);
    submenu_add_item(app->submenu, "About", MenuItemAbout, menu_submenu_callback, app);
}

/* submenu item selection callback */
static void menu_submenu_callback(void* context, uint32_t index) {
    AppState* app = (AppState*)context;
    switch(index) {
    case MenuItemFirmware:
        g_menu_flow = FlowFileBrowser;
        view_dispatcher_stop(app->view_dispatcher);
        break;
    case MenuItemPin:
        /* the number_input is handled INSIDE the dispatcher: we switch views.
         * The result callback was set once in run_menu; here we only set the
         * header and the current value. */
        number_input_set_header_text(app->number_input, "PIN (e.g. 2552)");
        app->menu_current_view = PandoraViewNumberInput;
        view_dispatcher_switch_to_view(app->view_dispatcher, PandoraViewNumberInput);
        break;
    case MenuItemLaunch:
        g_menu_flow = FlowLaunch;
        view_dispatcher_stop(app->view_dispatcher);
        break;
    case MenuItemAbout:
        /* show the scrollable About screen; Back returns to the submenu
         * (handled by menu_back_callback). */
        widget_reset(app->about);
        widget_add_text_scroll_element(app->about, 0, 0, 128, 64, ABOUT_TEXT);
        app->menu_current_view = PandoraViewAbout;
        view_dispatcher_switch_to_view(app->view_dispatcher, PandoraViewAbout);
        break;
    default:
        break;
    }
}

/* number_input callback: stores the PIN and returns to the submenu */
static void menu_number_input_callback(void* context, int32_t number) {
    AppState* app = (AppState*)context;
    app->pin_value = number;
    FURI_LOG_I(TAG, "PIN set in menu: %ld", (long)number);
    menu_rebuild(app);
    app->menu_current_view = PandoraViewSubmenu;
    view_dispatcher_switch_to_view(app->view_dispatcher, PandoraViewSubmenu);
}

/* Global back: from the About screen it returns to the submenu; from the submenu
 * it exits the app. (number_input handles its own back internally.) */
static bool menu_back_callback(void* context) {
    AppState* app = (AppState*)context;
    if(app->menu_current_view == PandoraViewAbout) {
        app->menu_current_view = PandoraViewSubmenu;
        view_dispatcher_switch_to_view(app->view_dispatcher, PandoraViewSubmenu);
        return true;
    }
    g_menu_flow = FlowExit;
    view_dispatcher_stop(app->view_dispatcher);
    return true;
}

/* Builds and runs the menu (blocks until stop). Returns the requested flow.
 * The view_dispatcher and its views are created and destroyed HERE, so that on
 * return the GUI is free for the file browser or direct-draw. */
static MenuFlow run_menu(AppState* app, Gui* gui) {
    g_menu_flow = FlowExit;

    app->view_dispatcher = view_dispatcher_alloc();
    app->submenu = submenu_alloc();
    app->number_input = number_input_alloc();
    app->about = widget_alloc();
    app->menu_current_view = PandoraViewSubmenu;

    view_dispatcher_set_event_callback_context(app->view_dispatcher, app);
    view_dispatcher_set_navigation_event_callback(app->view_dispatcher, menu_back_callback);

    view_dispatcher_add_view(
        app->view_dispatcher, PandoraViewSubmenu, submenu_get_view(app->submenu));
    view_dispatcher_add_view(
        app->view_dispatcher, PandoraViewNumberInput, number_input_get_view(app->number_input));
    view_dispatcher_add_view(
        app->view_dispatcher, PandoraViewAbout, widget_get_view(app->about));

    /* the number_input result callback is set once; the current value is passed
     * as current_number to pre-load the keyboard. */
    number_input_set_result_callback(
        app->number_input, menu_number_input_callback, app, app->pin_value, 0, 9999);

    /* submenu items */
    menu_rebuild(app);

    view_dispatcher_attach_to_gui(app->view_dispatcher, gui, ViewDispatcherTypeFullscreen);
    view_dispatcher_switch_to_view(app->view_dispatcher, PandoraViewSubmenu);

    view_dispatcher_run(app->view_dispatcher); /* BLOCKS until stop */

    /* FULL teardown: without this the GUI is not free for the emulator */
    view_dispatcher_remove_view(app->view_dispatcher, PandoraViewSubmenu);
    view_dispatcher_remove_view(app->view_dispatcher, PandoraViewNumberInput);
    view_dispatcher_remove_view(app->view_dispatcher, PandoraViewAbout);
    submenu_free(app->submenu);
    number_input_free(app->number_input);
    widget_free(app->about);
    view_dispatcher_free(app->view_dispatcher);
    app->submenu = NULL;
    app->number_input = NULL;
    app->about = NULL;
    app->view_dispatcher = NULL;

    return g_menu_flow;
}

/* Fills fw_name with the basename of the chosen path, for the menu label. */
static void set_fw_name_from_path(AppState* app) {
    const char* p = furi_string_get_cstr(app->fw_path);
    const char* slash = strrchr(p, '/');
    const char* base = slash ? slash + 1 : p;
    strncpy(app->fw_name, base, sizeof(app->fw_name) - 1);
    app->fw_name[sizeof(app->fw_name) - 1] = 0;
}

/* ------------------------------------------------------------ emulator */

/* Runs the emulator with direct-draw takeover. Returns on exit. Assumes the
 * firmware is ALREADY loaded and booted, and that the view_dispatcher was
 * already released. */
static void run_emulator(AppState* app, Gui* gui) {
    Canvas* canvas = NULL;
    bool fb_cb_added = false;

    app->fb_mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    if(!app->fb_mutex) {
        FURI_LOG_E(TAG, "fb_mutex alloc failed");
        return;
    }
    app->rx_queue = furi_message_queue_alloc(RX_QUEUE_LEN, sizeof(RfEdge));
    if(!app->rx_queue) {
        FURI_LOG_E(TAG, "rx_queue alloc failed");
        furi_mutex_free(app->fb_mutex);
        app->fb_mutex = NULL;
        return;
    }

    radio_init(app);

    /* --- PIN injection after boot (mariofull) --- */
    enter_pin(app);

    app->gui = gui;
    gui_add_framebuffer_callback(gui, framebuffer_commit_callback, app);
    fb_cb_added = true;
    canvas = gui_direct_draw_acquire(gui);
    if(!canvas) {
        FURI_LOG_E(TAG, "direct_draw_acquire failed");
        goto teardown;
    }
    app->canvas = canvas;

    FURI_LOG_I(TAG, "emulator: boot OK, main loop (steps/frame=%lu)",
               (unsigned long)app->steps_per_frame);

    uint32_t epoch = furi_get_tick();
    uint64_t next_us = 0;

    while(!app->exit_requested) {
        if(app->menu_requested) {
            app->menu_requested = false;
            app->menu_active = true;
            overlay_draw(app);
            /* overlay: OK=continue, Left=cycle RF, Back=exit */
            bool left_prev = false;
            while(app->menu_active && !app->exit_requested) {
                uint8_t k = poll_raw_keys();
                if(k & KBIT_BACK) app->exit_requested = true;
                if(k & KBIT_OK) app->menu_active = false;
                bool left = (k & KBIT_LEFT) != 0;
                if(left && !left_prev) {
                    rf_cycle_mode(app);
                    overlay_draw(app);
                }
                left_prev = left;
                furi_delay_ms(16);
            }
            epoch = furi_get_tick();
            next_us = 0;
            continue;
        }

        /* input -> keyfob buttons */
        uint8_t keys = poll_raw_keys();
        apply_keys_to_buttons(app, keys);

        /* run a slice of the real firmware (captures TX via lat_write_hook) */
        pic18_run(app->cpu, app->steps_per_frame);

        /* RF bridge: replay accumulated TX / inject received RX */
        tx_bridge_flush(app);
        rx_bridge_pump(app);

        /* reconstruct the screen from the captured GDDRAM */
        render_gddram(app);

        /* present */
        if(!app->menu_active) canvas_commit(canvas);

        /* pacing + mandatory yield (don't starve the input service) */
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
    if(app->fb_mutex) {
        furi_mutex_free(app->fb_mutex);
        app->fb_mutex = NULL;
    }
    FURI_LOG_I(TAG, "emulator: exit");
}

/* ------------------------------------------------------------ main */

int32_t pandora_pic_app(void* p) {
    UNUSED(p);

    AppState* app = (AppState*)malloc(sizeof(AppState));
    if(!app) return -1;
    memset(app, 0, sizeof(AppState));
    g_app = app;
    app->steps_per_frame = 150000; /* adjustable; the keyfob is not real-time */
    app->pin_value = PIN_DEFAULT;

    Storage* storage = (Storage*)furi_record_open(RECORD_STORAGE);
    DialogsApp* dialogs = (DialogsApp*)furi_record_open(RECORD_DIALOGS);
    Gui* gui = (Gui*)furi_record_open(RECORD_GUI);

    app->fw_path = furi_string_alloc_set_str("/ext");

    bool launch = false;

    /* --- PHASE 1: menu (view_dispatcher). Loop until Launch or Exit. --- */
    while(true) {
        MenuFlow flow = run_menu(app, gui);
        if(flow == FlowExit) {
            launch = false;
            break;
        } else if(flow == FlowFileBrowser) {
            /* the GUI is already free (view_dispatcher released in run_menu) */
            DialogsFileBrowserOptions opts;
            dialog_file_browser_set_basic_options(&opts, "*", NULL);
            opts.base_path = "/ext";
            opts.hide_ext = false;
            if(dialog_file_browser_show(dialogs, app->fw_path, app->fw_path, &opts)) {
                set_fw_name_from_path(app);
                FURI_LOG_I(TAG, "firmware chosen: %s", app->fw_name);
            }
            continue; /* re-create the menu */
        } else if(flow == FlowLaunch) {
            if(app->fw_name[0] == 0) {
                FURI_LOG_W(TAG, "Launch without firmware; opening file browser");
                DialogsFileBrowserOptions opts;
                dialog_file_browser_set_basic_options(&opts, "*", NULL);
                opts.base_path = "/ext";
                opts.hide_ext = false;
                if(!dialog_file_browser_show(dialogs, app->fw_path, app->fw_path, &opts)) {
                    continue; /* cancelled: return to the menu */
                }
                set_fw_name_from_path(app);
            }
            launch = true;
            break;
        }
    }

    /* --- PHASE 2: emulator (direct-draw). Only if the user requested Launch. --- */
    if(launch) {
        do {
            app->cpu = (Pic18Cpu*)malloc(sizeof(Pic18Cpu));
            if(!app->cpu) {
                FURI_LOG_E(TAG, "cpu alloc failed");
                break;
            }
            pic18_init(app->cpu);

            FURI_LOG_I(
                TAG, "free heap=%uK, max block=%uK, need prog=%uK",
                (unsigned)(memmgr_get_free_heap() / 1024u),
                (unsigned)(memmgr_heap_get_max_free_block() / 1024u),
                (unsigned)(PROG_MAX / 1024u));

            LoadResult lr = load_firmware(app, storage, furi_string_get_cstr(app->fw_path));
            if(lr != LoadOk) {
                char tbuf[64];
                if(lr == LoadNoMem)
                    snprintf(
                        tbuf, sizeof(tbuf), "Out of memory\nfree:%uK max:%uK",
                        (unsigned)(memmgr_get_free_heap() / 1024u),
                        (unsigned)(memmgr_heap_get_max_free_block() / 1024u));
                else if(lr == LoadBadFormat)
                    snprintf(tbuf, sizeof(tbuf), "Invalid format");
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

            app->prof = pick_profile(furi_string_get_cstr(app->fw_path), app->cpu);
            FURI_LOG_I(TAG, "profile: %s (PIN=%s)", app->prof->name,
                       app->prof->has_pin ? "yes" : "no");

            /* core callbacks: display + buttons + RF (BEFORE boot: mariofull's
             * PORTB.1 handshake is used during the cinit). */
            app->cpu->on_code = code_hook;
            app->cpu->code_ctx = app;
            app->cpu->on_port_read = port_read_hook;
            app->cpu->port_read_ctx = app;
            app->cpu->on_lat_write = lat_write_hook;
            app->cpu->lat_write_ctx = app;

            if(!boot_firmware(app)) {
                FURI_LOG_E(TAG, "boot failed (init not complete)");
                DialogMessage* msg = dialog_message_alloc();
                dialog_message_set_text(
                    msg, "Boot failed", 64, 30, AlignCenter, AlignCenter);
                dialog_message_set_buttons(msg, NULL, "OK", NULL);
                dialog_message_show(dialogs, msg);
                dialog_message_free(msg);
                break;
            }
            FURI_LOG_I(TAG, "boot OK -> PC=0x%06lX", (unsigned long)app->cpu->pc);

            run_emulator(app, gui);
        } while(false);
    }

    /* --- teardown --- */
    if(app->cpu) free(app->cpu);
    if(app->prog) free(app->prog);
    furi_string_free(app->fw_path);
    furi_record_close(RECORD_GUI);
    furi_record_close(RECORD_DIALOGS);
    furi_record_close(RECORD_STORAGE);
    free(app);
    g_app = NULL;
    return 0;
}
