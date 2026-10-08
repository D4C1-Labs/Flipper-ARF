/* Pandora PIC - emulador del firmware REAL de keyfob Pandora DXL-5000 (PIC18)
 * para Flipper Zero.
 *
 * El firmware del llavero corre SIN modificar sobre el interprete PIC18
 * (lib/pic18core). Esta app emula el hardware que el firmware espera:
 *   - Display OLED SSD1306 128x32: se reconstruye su GDDRAM interceptando el
 *     serializador SW-SPI del firmware (hook en send-byte + pin DC), igual que
 *     el emulador Python de referencia (emu/pic18_tui.py). Se escala/pinta en
 *     la pantalla 128x64 del Flipper.
 *   - 5 botones (registro 0xF8E, activos a bajo) mapeados a los botones del
 *     Flipper (polling GPIO directo, como FlipperGB).
 *   - Radio OOK: el firmware hace bit-bang de la RF. Se puentea al CC1101 del
 *     Flipper (TX de la trama OOK capturada del pin bit-bang; RX alimentando el
 *     pin de datos del firmware con los flancos reales de la RF).
 *
 * FLUJO (v2):
 *   Fase 1 - MENU (GUI normal con view_dispatcher + submenu + number_input):
 *     * "Firmware: <nombre>" -> abre el file browser (.hex/.bin)
 *     * "PIN: <valor>"       -> number_input (PIN numerico, default 2552)
 *     * "Launch"             -> arranca el emulador
 *     El view_dispatcher BLOQUEA hasta que el usuario elige Launch o sale.
 *   Fase 2 - EMULADOR (direct-draw takeover, INCOMPATIBLE con view_dispatcher):
 *     tras Launch se LIBERA por completo el view_dispatcher y sus views, y
 *     RECIEN AHI se hace el takeover (gui_direct_draw_acquire +
 *     gui_add_framebuffer_callback) y se corre el loop del emulador. Al salir
 *     del emulador la app termina (v1: no vuelve al menu).
 *
 * El file browser NO se puede abrir mientras el view_dispatcher corre (su
 * event loop monopoliza el GUI). Enfoque robusto elegido y DOCUMENTADO: el item
 * "Firmware" pone una bandera (FlowFileBrowser) y hace view_dispatcher_stop();
 * tras salir del run() el main abre el file browser y RE-CREA el menu. Lo mismo
 * para "PIN" y "Launch". Asi nunca coexisten view_dispatcher y file browser /
 * direct-draw. Ver PandoraPIC_v2.md.
 *
 * Frontend del emulador inspirado en FlipperGB (direct-draw takeover, poll de
 * pines, safe_malloc, yield obligatorio por frame).
 */

#include <furi.h>
#include <furi_hal.h>
#include <furi_hal_subghz.h>
#include <gui/gui.h>
#include <gui/view_dispatcher.h>
#include <gui/modules/submenu.h>
#include <gui/modules/number_input.h>
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

/* SSD1306 del keyfob: 128 columnas x 4 paginas = 512 bytes (128x32) */
#define GDDRAM_COLS 128
#define GDDRAM_PAGES 4
#define GDDRAM_SIZE (GDDRAM_COLS * GDDRAM_PAGES)

#define PROG_MAX 0x1B000u /* cubre hasta 0x1A17B (mariofull) con margen */

#define ALLOC_MARGIN 1024u
#define FRAME_US 30000 /* ~33 Hz de refresco de UI; el keyfob no es un juego */

#define PIN_DEFAULT 2552

/* En el Flipper malloc() NO devuelve NULL: crashea el firmware con OOM. Toda
 * asignacion dependiente del tamano del .hex pasa por aqui. */
static void* safe_malloc(size_t size) {
    if(memmgr_heap_get_max_free_block() < size + ALLOC_MARGIN) return NULL;
    return malloc(size);
}

/* ------------------------------------------------------------ perfiles HW */

typedef struct {
    const char* name;
    uint32_t send_byte_pc; /* hook del serializador SW-SPI */
    uint16_t byte_reg; /* registro banco4 con el byte ya ensamblado */
    uint16_t dc_flag_a; /* flags banco1: nonzero => COMANDO, zero => DATO */
    uint16_t dc_flag_b;
    bool has_pin; /* mariofull tiene PIN 2552 */
    /* --- boot: el runtime XC8 tiene un cinit que hay que manejar --- */
    uint8_t boot_mode; /* 0 = run-to (mariofull, handshake PORTB.1); 1 = cinit records (mario2) */
    uint32_t cinit_wait_pc; /* mariofull: correr hasta aqui (main loop) */
    uint32_t cinit_table; /* mario2: tabla de records cinit */
    uint32_t cinit_table_end;
    uint32_t cinit_entry; /* mario2: correr hasta aqui antes de aplicar cinit */
    uint32_t cinit_exit; /* mario2: PC tras aplicar cinit */
    bool portb1_handshake; /* mariofull: PORTB.1 se lee siempre 0 (ACK) */

    /* --- PIN (mariofull): validacion 0x018D76 compara 2,5,5,2 --- */
    uint32_t pin_validate; /* RCALL de la validacion */
    uint32_t pin_accept; /* PC de ACCEPT */
    uint32_t pin_reject; /* PC de REJECT */
    uint16_t pin_slot_lo[4]; /* 4 enteros 16-bit (lo) en bank1 */
    uint16_t pin_slot_hi[4];
    uint16_t pin_unlock_lo; /* flag "ya desbloqueado" (lo,hi) */
    uint16_t pin_unlock_hi;

    /* --- RF OOK bit-bang: pin de salida (LAT) y pin de entrada (PORT) --- */
    uint16_t ook_tx_lat; /* SFR LAT absoluto del pin OOK TX */
    uint8_t ook_tx_bit; /* bit del pin OOK TX */
} PicProfile;

/* mariofull: send 0x174C, byte 0x4FE, flags 0x1F6|0x1F7; boot run-to main loop
 *            0x001712 con handshake PORTB.1; OOK TX en LATB.0.
 * mario2:    send 0x1758, byte 0x4EF, flags 0x1F7|0x1F8; boot cinit records
 *            (tabla 0x17530..0x175F8, entry 0x175FA, exit 0x17636); OOK TX LATB.1. */
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

/* ------------------------------------------------------------ estado app */

enum {
    KBIT_UP = 1 << 0,
    KBIT_DOWN = 1 << 1,
    KBIT_LEFT = 1 << 2,
    KBIT_RIGHT = 1 << 3,
    KBIT_OK = 1 << 4,
    KBIT_BACK = 1 << 5,
};

/* Botones del keyfob en el registro 0xF8E (activos a bajo). */
#define BTN_REG 0xF8Eu
#define BTN_B1_BIT 1 /* arriba / + / sube digito PIN */
#define BTN_B2_BIT 2 /* OK / confirma PIN */
#define BTN_B3_BIT 4 /* menu */
#define BTN_B5_BIT 6
#define BTN_B6_BIT 7
#define BTN_ALL_MASK ((1 << 1) | (1 << 2) | (1 << 4) | (1 << 6) | (1 << 7))

/* Direcciones SFR usadas en el port read */
#define SFR_PORTB 0xF81u
#define SFR_LATB 0xF8Bu

/* --- CC1101 bridge --- */
/* TX: el firmware togglea el pin OOK (LATB.x). Capturamos cada flanco con su
 * duracion (medida por cpu->cycles entre toggles) en un ring de LevelDuration y
 * lo reproducimos por furi_hal_subghz_start_async_tx. */
#define TX_RING_LEN 2048u

/* RX: furi_hal_subghz_start_async_rx entrega (level,duration_us) por flanco
 * desde IRQ. Los encolamos en una cola thread-safe y en el main loop fijamos el
 * nivel del pin de entrada del firmware (on_port_read) con su timing. */
#define RX_QUEUE_LEN 512u

typedef struct {
    bool level;
    uint32_t duration; /* ticks del CC1101 (us) */
} RfEdge;

/* Conversion cycles->us del PIC: el keyfob corre con oscilador interno; el RE
 * no fija el Fosc absoluto (ver RE_DYN_mario2/mariofull "Te real en us DESC").
 * Usamos 4MHz => 1 ciclo de instruccion = 1us como base; es ajustable y solo
 * afecta a la escala temporal de la OOK reproducida, no a su contenido. */
#define PIC_CYCLE_US_NUM 1u
#define PIC_CYCLE_US_DEN 1u

typedef struct {
    /* nucleo */
    Pic18Cpu* cpu;
    uint8_t* prog; /* buffer de programa (safe_malloc) */
    const PicProfile* prof;

    /* GDDRAM del SSD1306 reconstruida */
    uint8_t gddram[GDDRAM_SIZE];
    uint16_t gd_page;
    uint16_t gd_col;
    int gd_cmd_arg_left; /* comandos multi-byte: cuantos args quedan */

    /* framebuffer del Flipper (1bpp, bit=1 => claro) */
    uint8_t screen[FB_SIZE];

    Gui* gui;
    Canvas* canvas;
    FuriMutex* fb_mutex;

    /* botones */
    uint8_t btn_mask; /* bits BTN_*_BIT actualmente pulsados */

    /* radio CC1101 */
    bool radio_on; /* CC1101 adquirido/configurado */
    bool rf_tx_enabled; /* puente TX activo (captura flancos del firmware) */
    bool rf_rx_enabled; /* puente RX activo (inyecta flancos de la RF real) */
    uint32_t frequency;

    /* TX bridge: ring de flancos capturados del pin OOK del firmware */
    RfEdge tx_ring[TX_RING_LEN];
    volatile uint32_t tx_head; /* productor (main loop, on_lat_write) */
    volatile uint32_t tx_tail; /* consumidor (callback async_tx) */
    bool tx_last_level; /* ultimo nivel del pin OOK */
    uint64_t tx_last_cycles; /* cycles del CPU en el ultimo flanco */
    bool tx_active; /* async_tx en curso */
    uint32_t tx_frames; /* contador de tramas TX logueadas */

    /* RX bridge: cola thread-safe alimentada desde la IRQ del CC1101 */
    FuriMessageQueue* rx_queue;
    uint16_t rx_in_port; /* SFR PORT del pin de entrada RX del firmware */
    uint8_t rx_in_bit; /* bit del pin de entrada RX */
    bool rx_cur_level; /* nivel actual inyectado en el pin de entrada */
    uint32_t rx_level_until; /* tick hasta el que mantener rx_cur_level */
    uint32_t rx_events; /* contador de flancos RX inyectados */

    volatile bool exit_requested;
    volatile bool menu_requested; /* overlay de ayuda in-emulador */
    bool menu_active;

    uint32_t steps_per_frame;

    /* PIN elegido en el menu (numerico; 2552 por defecto). */
    int32_t pin_value;
    bool pin_entered; /* ya se inyecto el PIN tras el boot */

    /* --- menu fase 1 (view_dispatcher) --- */
    ViewDispatcher* view_dispatcher;
    Submenu* submenu;
    NumberInput* number_input;
    FuriString* fw_path; /* ruta del firmware elegida */
    char fw_name[32]; /* nombre corto para el label */
} AppState;

static AppState* g_app = NULL;
static volatile uint32_t s_fb_cb_inflight = 0;

/* ---------------------------------------------------- SSD1306 GDDRAM model */

/* Decodifica un byte enviado al SSD1306 segun el nivel DC.
 * DC=0 => comando (direccionamiento/config), DC=1 => dato de pixel. */
static void gddram_feed(AppState* app, uint8_t b, bool is_cmd) {
    if(is_cmd) {
        if(app->gd_cmd_arg_left > 0) {
            /* argumento de un comando multi-byte: 0x21 col start/end, 0x22 page */
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
            app->gd_cmd_arg_left = 1; /* addressing mode toma 1 arg */
        } else if(b == 0x21) {
            app->gd_cmd_arg_left = 2; /* col start, col end */
            app->gd_col = 0;
        } else if(b == 0x22) {
            app->gd_cmd_arg_left = 2; /* page start, page end */
            app->gd_page = 0;
        } else if(
            b == 0x81 || b == 0xA8 || b == 0xD3 || b == 0xDA || b == 0xD5 || b == 0xD9 ||
            b == 0xDB || b == 0x8D || b == 0xAD) {
            app->gd_cmd_arg_left = 1; /* comandos de 1 argumento */
        }
        /* comandos de 1 byte (0x40,0xA1,0xC8,0xA4,0xA6,0xAF,0xAE...) no mueven punteros */
    } else {
        /* dato: 8 pixeles verticales de (page,col), bit0 arriba */
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

/* Hook de codigo: al entrar en el serializador send-byte, lee el byte ya
 * ensamblado y el nivel DC desde los flags del firmware, y lo mete en la
 * GDDRAM. Asi capturamos TODO lo que el firmware "dibuja" sin tocarlo. */
static void code_hook(Pic18Cpu* cpu, uint32_t pc, void* ctx) {
    AppState* app = (AppState*)ctx;
    if(pc != app->prof->send_byte_pc) return;
    uint8_t b = pic18_read_ram(cpu, app->prof->byte_reg);
    uint8_t fa = pic18_read_ram(cpu, app->prof->dc_flag_a);
    uint8_t fb = pic18_read_ram(cpu, app->prof->dc_flag_b);
    bool is_cmd = (fa != 0) || (fb != 0); /* nonzero => COMANDO */
    gddram_feed(app, b, is_cmd);
}

/* ----------------------------------------------------- botones (PORT read) */

/* El firmware lee los 5 botones en el registro 0xF8E (activo-bajo). Devolvemos
 * 0xFF con los bits pulsados en 0. Para PORTB: handshake de mariofull. Para el
 * pin de entrada RX (si el puente RX esta activo) inyectamos el nivel actual. */
static int port_read_hook(Pic18Cpu* cpu, uint16_t addr, void* ctx) {
    AppState* app = (AppState*)ctx;
    if(addr == BTN_REG) {
        return (int)(0xFFu & ~app->btn_mask);
    }
    if(addr == SFR_PORTB && app->prof && app->prof->portb1_handshake) {
        uint8_t latb = pic18_read_ram(cpu, SFR_LATB);
        uint8_t v = (uint8_t)(latb & (uint8_t)~0x02u); /* bit1 = 0 (ACK) */
        /* Si el puente RX usa PORTB, superponemos el nivel inyectado. */
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

/* Hook de escritura a LAT: cuando el puente TX esta activo, detecta los toggles
 * del pin OOK del firmware y acumula los flancos con su duracion (medida por
 * cpu->cycles entre toggles) en el ring de TX. El callback async_tx los drena. */
static void lat_write_hook(Pic18Cpu* cpu, uint16_t addr, uint8_t val, void* ctx) {
    AppState* app = (AppState*)ctx;
    if(!app->rf_tx_enabled) return;
    if(addr != app->prof->ook_tx_lat) return;
    bool level = (val >> app->prof->ook_tx_bit) & 1u;
    if(level == app->tx_last_level) return; /* sin flanco */

    uint64_t now = cpu->cycles;
    uint64_t dcyc = now - app->tx_last_cycles;
    app->tx_last_cycles = now;

    /* duracion del nivel que ACABA de terminar (tx_last_level) */
    uint32_t dur_us = (uint32_t)((dcyc * PIC_CYCLE_US_NUM) / PIC_CYCLE_US_DEN);
    if(dur_us == 0) dur_us = 1;

    uint32_t head = app->tx_head;
    uint32_t next = (head + 1) % TX_RING_LEN;
    if(next != app->tx_tail) { /* ring no lleno */
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

/* La GDDRAM es 128x32 (4 paginas). La pantalla del Flipper es 128x64. Pintamos
 * centrado verticalmente (filas 16..47) a escala 1:1, 1 pixel por pixel.
 * screen[] usa formato de pagina del Flipper: byte (x, page) con bit = fila&7,
 * bit=1 => claro. */
static void render_gddram(AppState* app) {
    furi_mutex_acquire(app->fb_mutex, FuriWaitForever);
    memset(app->screen, 0, FB_SIZE);
    const int y_off = 16; /* centrar 32 filas en 64 */
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

/* Callback de commit del framebuffer (direct draw). screen bit=1 => claro;
 * el buffer del display es bit=1 => oscuro, por eso el XOR. */
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

/* Poll directo de los pines (evita el input service, como FlipperGB). Mapea:
 *   Flipper Up    -> B1 (sube digito PIN / arriba)
 *   Flipper Down  -> B3 (menu/abajo)
 *   Flipper Ok    -> B2 (confirma)
 *   Flipper Left  -> B5
 *   Flipper Right -> B6
 *   Flipper Back  -> (gestionado para salir / menu)
 * Up+Down simultaneo = overlay de ayuda / toggle RF (gesto imposible fisico).
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
    /* Up+Down juntos abren el overlay de ayuda (y togglean el puente RF). */
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

/* ------------------------------------------------------------ carga .hex */

typedef enum {
    LoadOk,
    LoadIoError,
    LoadNoMem,
    LoadBadFormat,
} LoadResult;

static LoadResult load_firmware(AppState* app, Storage* storage, const char* path) {
    File* f = storage_file_alloc(storage);
    LoadResult res = LoadIoError;
    do {
        if(!storage_file_open(f, path, FSAM_READ, FSOM_OPEN_EXISTING)) break;
        uint64_t fsize = storage_file_size(f);
        if(fsize == 0 || fsize > 2u * 1024u * 1024u) {
            res = LoadBadFormat;
            break;
        }
        /* leer el .hex entero a un buffer temporal */
        uint8_t* hexbuf = (uint8_t*)safe_malloc((size_t)fsize + 1);
        if(!hexbuf) {
            res = LoadNoMem;
            break;
        }
        size_t got = storage_file_read(f, hexbuf, (size_t)fsize);
        if(got != fsize) {
            free(hexbuf);
            break;
        }
        hexbuf[got] = 0;

        /* buffer de programa del core */
        app->prog = (uint8_t*)safe_malloc(PROG_MAX);
        if(!app->prog) {
            free(hexbuf);
            res = LoadNoMem;
            break;
        }
        memset(app->prog, 0xFF, PROG_MAX);
        pic18_set_prog(app->cpu, app->prog, PROG_MAX);

        /* detectar si es .hex (texto Intel HEX empieza con ':') o .bin */
        int rc;
        if(hexbuf[0] == ':') {
            rc = pic18_load_hex_mem(app->cpu, hexbuf, got);
        } else {
            rc = pic18_load_prog(app->cpu, hexbuf, got, 0);
        }
        free(hexbuf);
        if(rc < 0) {
            res = LoadBadFormat;
            break;
        }
        res = LoadOk;
    } while(false);
    storage_file_close(f);
    storage_file_free(f);
    return res;
}

/* Selecciona el perfil por el nombre de archivo (mariofull vs mario2) o por
 * los config words (CONFIG3L 0xA2 full / 0xBF mario2). Por defecto mariofull. */
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

/* Aplica los records cinit del runtime XC8 (mario2): copia flash->RAM de la
 * tabla de inicializacion. Equivalente a emu/boot_mario2.py apply_cinit. Solo
 * escribe GPR RAM (<0xF80), no SFR. */
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
            tp++; /* consume 1 byte extra (como el python) */
        }
        if(tp + n > end + 2) break; /* record se sale de la tabla: desync */
        for(uint8_t k = 0; k < n; k++) {
            uint8_t b = (tp < cpu->prog_size) ? cpu->prog[tp] : 0;
            tp++;
            if(fsr0 < 0xF80u) cpu->ram[fsr0 & 0xFFF] = b;
            fsr0 = (uint16_t)((fsr0 + 1) & 0xFFF);
        }
    }
}

/* Lleva el firmware desde reset hasta el main loop, con el init del runtime
 * resuelto segun el perfil. Devuelve true si el boot llego a destino. */
static bool boot_firmware(AppState* app) {
    Pic18Cpu* cpu = app->cpu;
    pic18_reset(cpu);
    if(app->prof->boot_mode == 1) {
        /* mario2: correr prologo+data-init hasta la entrada del cinit, aplicar
         * los records, saltar a la salida del cinit. */
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
        /* mariofull: el handshake PORTB.1 (port_read_hook) permite que el cinit
         * termine por si solo; corremos hasta el main loop. */
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

/* Sentinela de retorno montada en la pila antes de disparar la validacion. */
#define PIN_RET_SENTINEL 0x01FFFEu

/* Resultado de la validacion del PIN (lo rellena pin_code_hook via ctx). */
typedef struct {
    const PicProfile* prof;
    int accept;
    int reject;
    int done;
} PinRun;

/* Hook de codigo usado solo durante enter_pin(): detecta ACCEPT/REJECT y para
 * en la sentinela. El contexto (ctx) es un PinRun* propio de esta ejecucion;
 * asi NO necesitamos estado global y es reentrante. */
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

/* Inyecta el PIN numerico elegido en el menu en el buffer de la FSM del PIN y
 * dispara la validacion REAL del firmware (0x018D76), igual que
 * emu/pic18_tui.py enter_pin. Loguea el veredicto ACCEPT/REJECT. Solo aplica a
 * mariofull (has_pin); para mario2 el campo PIN se ignora.
 *
 * El contenido de prog/RAM del core tras el boot queda intacto salvo por el
 * buffer del PIN: la validacion corre con una pila/BSR propias y una sentinela
 * de retorno, y restauramos PC/stack/BSR/stkptr para no perturbar el main loop
 * del emulador (que arranca justo despues desde cinit_wait_pc / cinit_exit). */
static void enter_pin(AppState* app) {
    const PicProfile* prof = app->prof;
    if(!prof->has_pin || app->pin_entered) return;
    app->pin_entered = true;

    Pic18Cpu* cpu = app->cpu;

    /* descomponer el PIN numerico en 4 digitos decimales (2552 -> 2,5,5,2) */
    int32_t v = app->pin_value;
    if(v < 0) v = 0;
    int digits[4];
    digits[0] = (v / 1000) % 10;
    digits[1] = (v / 100) % 10;
    digits[2] = (v / 10) % 10;
    digits[3] = v % 10;

    /* 1) componer el buffer del PIN (resultado de las pulsaciones B1/B2) */
    for(int i = 0; i < 4; i++) {
        pic18_write_ram(cpu, prof->pin_slot_lo[i], (uint8_t)(digits[i] & 0xFF));
        pic18_write_ram(cpu, prof->pin_slot_hi[i], (uint8_t)((digits[i] >> 8) & 0xFF));
    }
    pic18_write_ram(cpu, prof->pin_unlock_lo, 0); /* flag "ya desbloqueado" = 0 */
    pic18_write_ram(cpu, prof->pin_unlock_hi, 0);

    /* 2) ejecutar la validacion REAL con una sentinela de retorno y hook de
     * ACCEPT/REJECT. Guardamos y restauramos el contexto del main loop. */
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
    pic18_push(cpu, PIN_RET_SENTINEL); /* sentinela de retorno */
    cpu->pc = prof->pin_validate;

    uint64_t budget = 4000000;
    while(budget-- > 0 && !cpu->halted && !run.done) {
        pic18_step(cpu);
    }

    /* restaurar contexto del main loop */
    cpu->on_code = saved_hook;
    cpu->code_ctx = saved_ctx;
    cpu->pc = saved_pc;
    cpu->ram[PIC18_SFR_BSR] = saved_bsr;
    cpu->stkptr = saved_stkptr;
    cpu->halted = saved_halted;

    if(run.accept) {
        FURI_LOG_I(TAG, "PIN %04ld -> ACCEPT (TX de servicio OOK)", (long)app->pin_value);
    } else if(run.reject) {
        FURI_LOG_I(TAG, "PIN %04ld -> REJECT (no transmite)", (long)app->pin_value);
    } else {
        FURI_LOG_W(TAG, "PIN %04ld -> sin veredicto (budget agotado)", (long)app->pin_value);
    }
}

/* ------------------------------------------------------------ CC1101 RF */

/* Preset OOK 650kHz async en el formato de furi_hal_subghz_load_custom_preset:
 * pares {reg, val} terminados en 0x00, luego 2 bytes (ignorados) + 8 bytes de
 * PA table. Valores alineados con la tabla OOK async del firmware
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
    0x00, /* terminador */
    0x00, 0x00, /* 2 bytes (ignorados por el parser antes de PA) */
    0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* PA table (OOK: C0) */
};

/* El firmware Pandora hace su propia modulacion OOK por bit-bang; aqui
 * inicializamos el CC1101 del Flipper (RX/TX async) para el puente de radio. */
static void radio_init(AppState* app) {
    app->frequency = 433920000;
    furi_hal_subghz_reset();
    furi_hal_subghz_load_custom_preset(PRESET_OOK650);
    furi_hal_subghz_set_frequency_and_path(app->frequency);
    furi_hal_subghz_idle();
    app->radio_on = true;
    /* pin de entrada RX por defecto: el handshake PORTB (mariofull) o PORTB en
     * general; es el pin que el firmware samplea. Se puede ajustar por perfil. */
    app->rx_in_port = SFR_PORTB;
    app->rx_in_bit = 0;
    FURI_LOG_I(
        TAG, "CC1101 init OOK650 @%lu Hz", (unsigned long)app->frequency);
}

static void radio_deinit(AppState* app) {
    if(!app->radio_on) return;
    /* parar cualquier async en curso */
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

/* Callback de async_tx: drena el ring de flancos capturados del pin OOK. */
static LevelDuration radio_tx_callback(void* context) {
    AppState* app = (AppState*)context;
    uint32_t tail = app->tx_tail;
    if(tail == app->tx_head) {
        /* nada mas que enviar por ahora: terminamos la rafaga */
        return level_duration_reset();
    }
    RfEdge e = app->tx_ring[tail];
    app->tx_tail = (tail + 1) % TX_RING_LEN;
    return level_duration_make(e.level, e.duration);
}

/* Lanza la reproduccion de los flancos acumulados si hay suficientes y no hay
 * una TX en curso. Se llama en el main loop. */
static void tx_bridge_flush(AppState* app) {
    if(!app->rf_tx_enabled || !app->radio_on) return;
    if(app->tx_active) {
        if(furi_hal_subghz_is_async_tx_complete()) {
            furi_hal_subghz_stop_async_tx();
            furi_hal_subghz_idle();
            app->tx_active = false;
            app->tx_frames++;
            FURI_LOG_I(TAG, "TX trama #%lu enviada", (unsigned long)app->tx_frames);
        }
        return;
    }
    /* contar flancos pendientes */
    uint32_t pending = (app->tx_head + TX_RING_LEN - app->tx_tail) % TX_RING_LEN;
    if(pending < 16) return; /* esperar a una rafaga razonable */
    if(!furi_hal_subghz_is_tx_allowed(app->frequency)) {
        FURI_LOG_E(
            TAG, "TX NO permitida @%lu Hz; descarto %lu flancos",
            (unsigned long)app->frequency, (unsigned long)pending);
        app->tx_tail = app->tx_head; /* descartar */
        return;
    }
    FURI_LOG_I(TAG, "TX arranque async: %lu flancos", (unsigned long)pending);
    if(furi_hal_subghz_start_async_tx(radio_tx_callback, app)) {
        app->tx_active = true;
    } else {
        FURI_LOG_E(TAG, "start_async_tx fallo");
        app->tx_tail = app->tx_head;
    }
}

/* --- RX bridge --- */

/* Callback del CC1101 (corre en IRQ): encola el flanco. NO bloquear. */
static void radio_rx_callback(bool level, uint32_t duration, void* context) {
    AppState* app = (AppState*)context;
    RfEdge e = {.level = level, .duration = duration};
    /* timeout 0: si la cola esta llena, se pierde el flanco (mejor que bloquear
     * en IRQ). */
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
    FURI_LOG_I(TAG, "RX bridge ON (inyecta en %03X.%u)", app->rx_in_port, app->rx_in_bit);
}

static void rx_bridge_stop(AppState* app) {
    if(!app->rf_rx_enabled) return;
    furi_hal_subghz_stop_async_rx();
    furi_hal_subghz_idle();
    app->rf_rx_enabled = false;
    FURI_LOG_I(TAG, "RX bridge OFF (%lu flancos inyectados)", (unsigned long)app->rx_events);
}

/* Drena la cola RX y actualiza el nivel inyectado en el pin de entrada del
 * firmware. El timing se respeta por el duration de cada flanco; el firmware
 * lee el pin via on_port_read (port_read_hook) y demodula por su cuenta. */
static void rx_bridge_pump(AppState* app) {
    if(!app->rf_rx_enabled) return;
    uint32_t now = furi_get_tick();
    /* mientras el nivel actual ya haya "cumplido" su duracion, sacamos el
     * siguiente flanco de la cola. */
    RfEdge e;
    int drained = 0;
    while((int32_t)(now - app->rx_level_until) >= 0 &&
          furi_message_queue_get(app->rx_queue, &e, 0) == FuriStatusOk) {
        app->rx_cur_level = e.level;
        /* duration viene en us; el tick del Flipper es ms. Mantenemos el nivel
         * al menos 1 tick (los flancos sub-ms se agregan de facto). */
        uint32_t dur_ms = e.duration / 1000u;
        if(dur_ms == 0) dur_ms = 1;
        app->rx_level_until = now + dur_ms;
        app->rx_events++;
        drained++;
        if(drained >= 64) break; /* no acaparar el frame */
    }
    if(drained) {
        FURI_LOG_D(
            TAG, "RX inyectados %d flancos (total %lu)", drained,
            (unsigned long)app->rx_events);
    }
}

/* Alterna el puente RF: OFF -> TX -> RX -> OFF. Logueado. */
static void rf_cycle_mode(AppState* app) {
    if(!app->rf_tx_enabled && !app->rf_rx_enabled) {
        /* OFF -> TX */
        app->rf_tx_enabled = true;
        app->tx_last_level = (pic18_read_ram(app->cpu, app->prof->ook_tx_lat) >>
                              app->prof->ook_tx_bit) &
                             1u;
        app->tx_last_cycles = app->cpu->cycles;
        app->tx_head = app->tx_tail = 0;
        FURI_LOG_I(TAG, "RF modo TX (captura pin OOK %03X.%u)",
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

/* ------------------------------------------------------------ overlay ayuda */

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

/* ------------------------------------------------------------ menu fase 1 */

/* Vistas del view_dispatcher */
typedef enum {
    PandoraViewSubmenu,
    PandoraViewNumberInput,
} PandoraView;

/* Items del submenu principal */
typedef enum {
    MenuItemFirmware,
    MenuItemPin,
    MenuItemLaunch,
} MenuItem;

/* Resultado de la fase 1: que accion pidio el usuario al parar el dispatcher. */
typedef enum {
    FlowExit, /* salir de la app */
    FlowFileBrowser, /* abrir el file browser y volver al menu */
    FlowPinInput, /* entrar al number_input (manejado dentro del dispatcher) */
    FlowLaunch, /* arrancar el emulador */
} MenuFlow;

static volatile MenuFlow g_menu_flow = FlowExit;

/* callback de seleccion de item del submenu (declarado antes de menu_rebuild) */
static void menu_submenu_callback(void* context, uint32_t index);

/* Reconstruye el submenu (etiquetas Firmware/PIN actualizadas). El modulo
 * submenu COPIA la etiqueta internamente, asi que buffers en pila estan bien. */
static void menu_rebuild(AppState* app) {
    submenu_reset(app->submenu);
    submenu_set_header(app->submenu, "Pandora PIC");
    char buf[48];
    snprintf(buf, sizeof(buf), "Firmware: %s", app->fw_name[0] ? app->fw_name : "<none>");
    submenu_add_item(app->submenu, buf, MenuItemFirmware, menu_submenu_callback, app);
    snprintf(buf, sizeof(buf), "PIN: %ld", (long)app->pin_value);
    submenu_add_item(app->submenu, buf, MenuItemPin, menu_submenu_callback, app);
    submenu_add_item(app->submenu, "Launch", MenuItemLaunch, menu_submenu_callback, app);
}

/* callback de seleccion de item del submenu */
static void menu_submenu_callback(void* context, uint32_t index) {
    AppState* app = (AppState*)context;
    switch(index) {
    case MenuItemFirmware:
        g_menu_flow = FlowFileBrowser;
        view_dispatcher_stop(app->view_dispatcher);
        break;
    case MenuItemPin:
        /* el number_input se maneja DENTRO del dispatcher: cambiamos de vista.
         * El callback de resultado se fijo una vez en run_menu; aqui solo
         * ponemos el header y el valor actual. */
        number_input_set_header_text(app->number_input, "PIN (ej. 2552)");
        view_dispatcher_switch_to_view(app->view_dispatcher, PandoraViewNumberInput);
        break;
    case MenuItemLaunch:
        g_menu_flow = FlowLaunch;
        view_dispatcher_stop(app->view_dispatcher);
        break;
    default:
        break;
    }
}

/* callback del number_input: guarda el PIN y vuelve al submenu */
static void menu_number_input_callback(void* context, int32_t number) {
    AppState* app = (AppState*)context;
    app->pin_value = number;
    FURI_LOG_I(TAG, "PIN fijado en menu: %ld", (long)number);
    menu_rebuild(app);
    view_dispatcher_switch_to_view(app->view_dispatcher, PandoraViewSubmenu);
}

/* Back global: desde el submenu sale de la app; desde number_input vuelve. */
static bool menu_back_callback(void* context) {
    AppState* app = (AppState*)context;
    /* si estamos en number_input, volver al submenu lo maneja el modulo; aqui
     * tratamos el back del submenu => salir. */
    g_menu_flow = FlowExit;
    view_dispatcher_stop(app->view_dispatcher);
    return true;
}

/* Construye y corre el menu (bloquea hasta stop). Devuelve el flujo pedido.
 * El view_dispatcher y sus vistas se crean y destruyen AQUI, de modo que al
 * volver el GUI queda libre para el file browser o el direct-draw. */
static MenuFlow run_menu(AppState* app, Gui* gui) {
    g_menu_flow = FlowExit;

    app->view_dispatcher = view_dispatcher_alloc();
    app->submenu = submenu_alloc();
    app->number_input = number_input_alloc();

    view_dispatcher_set_event_callback_context(app->view_dispatcher, app);
    view_dispatcher_set_navigation_event_callback(app->view_dispatcher, menu_back_callback);

    view_dispatcher_add_view(
        app->view_dispatcher, PandoraViewSubmenu, submenu_get_view(app->submenu));
    view_dispatcher_add_view(
        app->view_dispatcher, PandoraViewNumberInput, number_input_get_view(app->number_input));

    /* el callback de resultado del number_input se fija una vez; el valor
     * actual se pasa como current_number para pre-cargar el teclado. */
    number_input_set_result_callback(
        app->number_input, menu_number_input_callback, app, app->pin_value, 0, 9999);

    /* items del submenu */
    menu_rebuild(app);

    view_dispatcher_attach_to_gui(app->view_dispatcher, gui, ViewDispatcherTypeFullscreen);
    view_dispatcher_switch_to_view(app->view_dispatcher, PandoraViewSubmenu);

    view_dispatcher_run(app->view_dispatcher); /* BLOQUEA hasta stop */

    /* teardown COMPLETO: sin esto el GUI no queda libre para el emulador */
    view_dispatcher_remove_view(app->view_dispatcher, PandoraViewSubmenu);
    view_dispatcher_remove_view(app->view_dispatcher, PandoraViewNumberInput);
    submenu_free(app->submenu);
    number_input_free(app->number_input);
    view_dispatcher_free(app->view_dispatcher);
    app->submenu = NULL;
    app->number_input = NULL;
    app->view_dispatcher = NULL;

    return g_menu_flow;
}

/* Rellena fw_name con el basename de la ruta elegida, para el label del menu. */
static void set_fw_name_from_path(AppState* app) {
    const char* p = furi_string_get_cstr(app->fw_path);
    const char* slash = strrchr(p, '/');
    const char* base = slash ? slash + 1 : p;
    strncpy(app->fw_name, base, sizeof(app->fw_name) - 1);
    app->fw_name[sizeof(app->fw_name) - 1] = 0;
}

/* ------------------------------------------------------------ emulador */

/* Corre el emulador con direct-draw takeover. Devuelve al salir. Asume que el
 * firmware YA esta cargado y booteado, y que el view_dispatcher ya se libero. */
static void run_emulator(AppState* app, Gui* gui) {
    Canvas* canvas = NULL;
    bool fb_cb_added = false;

    app->fb_mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    if(!app->fb_mutex) {
        FURI_LOG_E(TAG, "fb_mutex alloc fallo");
        return;
    }
    app->rx_queue = furi_message_queue_alloc(RX_QUEUE_LEN, sizeof(RfEdge));
    if(!app->rx_queue) {
        FURI_LOG_E(TAG, "rx_queue alloc fallo");
        furi_mutex_free(app->fb_mutex);
        app->fb_mutex = NULL;
        return;
    }

    radio_init(app);

    /* --- inyeccion del PIN tras el boot (mariofull) --- */
    enter_pin(app);

    app->gui = gui;
    gui_add_framebuffer_callback(gui, framebuffer_commit_callback, app);
    fb_cb_added = true;
    canvas = gui_direct_draw_acquire(gui);
    if(!canvas) {
        FURI_LOG_E(TAG, "direct_draw_acquire fallo");
        goto teardown;
    }
    app->canvas = canvas;

    FURI_LOG_I(TAG, "emulador: boot OK, main loop (steps/frame=%lu)",
               (unsigned long)app->steps_per_frame);

    uint32_t epoch = furi_get_tick();
    uint64_t next_us = 0;

    while(!app->exit_requested) {
        if(app->menu_requested) {
            app->menu_requested = false;
            app->menu_active = true;
            overlay_draw(app);
            /* overlay: OK=continuar, Left=ciclar RF, Back=salir */
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

        /* input -> botones del keyfob */
        uint8_t keys = poll_raw_keys();
        apply_keys_to_buttons(app, keys);

        /* ejecutar una tajada del firmware real (captura TX via lat_write_hook) */
        pic18_run(app->cpu, app->steps_per_frame);

        /* bridge RF: reproducir TX acumulada / inyectar RX recibida */
        tx_bridge_flush(app);
        rx_bridge_pump(app);

        /* reconstruir la pantalla desde la GDDRAM capturada */
        render_gddram(app);

        /* presentar */
        if(!app->menu_active) canvas_commit(canvas);

        /* pacing + yield obligatorio (no starvear el input service) */
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
    FURI_LOG_I(TAG, "emulador: salida");
}

/* ------------------------------------------------------------ main */

int32_t pandora_pic_app(void* p) {
    UNUSED(p);

    AppState* app = (AppState*)malloc(sizeof(AppState));
    if(!app) return -1;
    memset(app, 0, sizeof(AppState));
    g_app = app;
    app->steps_per_frame = 150000; /* ajustable; el keyfob no es tiempo-real */
    app->pin_value = PIN_DEFAULT;

    Storage* storage = (Storage*)furi_record_open(RECORD_STORAGE);
    DialogsApp* dialogs = (DialogsApp*)furi_record_open(RECORD_DIALOGS);
    Gui* gui = (Gui*)furi_record_open(RECORD_GUI);

    app->fw_path = furi_string_alloc_set_str("/ext");

    bool launch = false;

    /* --- FASE 1: menu (view_dispatcher). Loop hasta Launch o Exit. --- */
    while(true) {
        MenuFlow flow = run_menu(app, gui);
        if(flow == FlowExit) {
            launch = false;
            break;
        } else if(flow == FlowFileBrowser) {
            /* el GUI ya esta libre (view_dispatcher liberado en run_menu) */
            DialogsFileBrowserOptions opts;
            dialog_file_browser_set_basic_options(&opts, "*", NULL);
            opts.base_path = "/ext";
            opts.hide_ext = false;
            if(dialog_file_browser_show(dialogs, app->fw_path, app->fw_path, &opts)) {
                set_fw_name_from_path(app);
                FURI_LOG_I(TAG, "firmware elegido: %s", app->fw_name);
            }
            continue; /* re-crear el menu */
        } else if(flow == FlowLaunch) {
            if(app->fw_name[0] == 0) {
                FURI_LOG_W(TAG, "Launch sin firmware; abriendo file browser");
                DialogsFileBrowserOptions opts;
                dialog_file_browser_set_basic_options(&opts, "*", NULL);
                opts.base_path = "/ext";
                opts.hide_ext = false;
                if(!dialog_file_browser_show(dialogs, app->fw_path, app->fw_path, &opts)) {
                    continue; /* cancelo: volver al menu */
                }
                set_fw_name_from_path(app);
            }
            launch = true;
            break;
        }
    }

    /* --- FASE 2: emulador (direct-draw). Solo si el usuario pidio Launch. --- */
    if(launch) {
        do {
            app->cpu = (Pic18Cpu*)malloc(sizeof(Pic18Cpu));
            if(!app->cpu) {
                FURI_LOG_E(TAG, "cpu alloc fallo");
                break;
            }
            pic18_init(app->cpu);

            LoadResult lr = load_firmware(app, storage, furi_string_get_cstr(app->fw_path));
            if(lr != LoadOk) {
                const char* t = "Error de carga";
                if(lr == LoadNoMem) t = "Sin memoria";
                if(lr == LoadBadFormat) t = "Formato invalido";
                FURI_LOG_E(TAG, "carga fallida: %s", t);
                DialogMessage* msg = dialog_message_alloc();
                dialog_message_set_text(msg, t, 64, 30, AlignCenter, AlignCenter);
                dialog_message_set_buttons(msg, NULL, "OK", NULL);
                dialog_message_show(dialogs, msg);
                dialog_message_free(msg);
                break;
            }

            app->prof = pick_profile(furi_string_get_cstr(app->fw_path), app->cpu);
            FURI_LOG_I(TAG, "perfil: %s (PIN=%s)", app->prof->name,
                       app->prof->has_pin ? "si" : "no");

            /* callbacks del core: display + botones + RF (ANTES del boot: el
             * handshake PORTB.1 de mariofull se usa durante el cinit). */
            app->cpu->on_code = code_hook;
            app->cpu->code_ctx = app;
            app->cpu->on_port_read = port_read_hook;
            app->cpu->port_read_ctx = app;
            app->cpu->on_lat_write = lat_write_hook;
            app->cpu->lat_write_ctx = app;

            if(!boot_firmware(app)) {
                FURI_LOG_E(TAG, "boot fallido (init no completo)");
                DialogMessage* msg = dialog_message_alloc();
                dialog_message_set_text(
                    msg, "Boot fallido", 64, 30, AlignCenter, AlignCenter);
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
