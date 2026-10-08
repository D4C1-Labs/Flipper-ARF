/* Pandora ARM - emulador del firmware REAL de keyfob Pandora D-605 (EFM32
 * Cortex-M3) para Flipper Zero.
 *
 * El firmware del llavero corre SIN modificar sobre el interprete Thumb-2
 * (lib/thumbcore). Esta app emula el hardware que el firmware espera:
 *   - OLED UC16xx/ST7528 (framebuffer 92col x 16pag = 1472B): el firmware lo
 *     escribe por SPI; el framebuffer vive en RAM (0x20001152 pandora /
 *     0x200005DA pmax), lo leemos y lo pintamos en la pantalla 128x64 del Flipper.
 *   - 6 botones (GPIO DIN, pull-up, 0=pulsado). Pantalla NEGRA hasta el
 *     knock-code de desbloqueo (secuencia de botones a ciegas).
 *   - Si4432 (modelo SPI), timers, CMU, ADC: stubs minimos para que el fw no se
 *     cuelgue. NVIC minimo (tick de timer) para despertar el WFI.
 *
 * Portado fielmente del emulador de referencia emu/pandora_tui.py (verificado).
 * Frontend al estilo FlipperGB (direct-draw, poll GPIO, safe_malloc, yield).
 */

#include <furi.h>
#include <furi_hal.h>
#include <furi_hal_subghz.h>
#include <gui/gui.h>
#include <gui/view_dispatcher.h>
#include <gui/modules/submenu.h>
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

/* OLED del keyfob: 92 columnas x 16 paginas = 1472 bytes (panel 92x64 2bpp) */
#define OLED_COLS 92
#define OLED_PAGES 16
#define OLED_FB_SIZE (OLED_COLS * OLED_PAGES)

/* Mapa de memoria del EFM32 emulado (NO del Flipper; prefijo PA_ para no chocar
 * con los PA_FLASH_BASE/PA_FLASH_SIZE del CMSIS del STM32WB del propio Flipper). */
#define PA_FLASH_BASE 0x00000000u
#define PA_FLASH_SIZE 0x00042000u /* 264 KB (cubre pandora 0x3FFFF + margen) */
#define PA_RAM_BASE 0x20000000u
#define PA_RAM_SIZE 0x00008000u /* 32 KB */

#define ALLOC_MARGIN 1024u
#define FRAME_US 30000u /* ~33 Hz de refresco de UI */

/* Ventana (en instrucciones) donde el scanner del knock esta activo tras boot.
 * El keyfob entra en bajo consumo despues; por eso el desbloqueo se hace al
 * arrancar (igual que la correccion de la TUI Python). */
#define KNOCK_SETTLE_INSN 250000ull
#define KNOCK_STEP_BUDGET 4000000ull
#define KNOCK_RELEASE_AFTER 4

static void* safe_malloc(size_t size) {
    if(memmgr_heap_get_max_free_block() < size + ALLOC_MARGIN) return NULL;
    return malloc(size);
}

/* ------------------------------------------------------------ perfiles HW */

typedef struct {
    const char* name;
    uint32_t fb_addr; /* framebuffer en RAM */
    /* botones (port, pin) */
    uint8_t up_port, up_pin;
    uint8_t down_port, down_pin;
    uint8_t ok_port, ok_pin;
    uint8_t back_port, back_pin;
    uint8_t b5_port, b5_pin;
    uint8_t b6_port, b6_pin;
    /* knock-code: hasta 8 pasos, terminado en 0xFF */
    uint8_t knock[8];
    uint8_t knock_len;
    uint8_t gate; /* boton del 2o gate (indice BTN_*) */
    uint32_t scan_head; /* cabeza del escaner del knock */
    uint32_t spin_heads[8]; /* heads wait-release */
    uint8_t spin_count;
    uint32_t unlock_pc; /* bl rutina de desbloqueo = completado */

    /* --- bridge RF (software-demod; ver EMU_MAP_pandora.md / arm_pandora.py) --- */
    bool rf_sw_demod; /* true: demodulacion por SW via ISR (pandora); false (pmax): HW FIFO */
    uint32_t demod_isr; /* ISR demod GPIO (0x09554 pandora) */
    uint8_t data_port; /* pin DATA del Si4432 (GPIO puerto): pandora=1 (B) */
    uint8_t data_pin; /* pin DATA: pandora=0 */
    uint32_t timer1_cnt; /* TIMER1_CNT MMIO (0x40010424) = cronometro de ancho */
    uint32_t rx_struct; /* base de la estructura RX (0x20000000) */
    uint32_t rx_gate; /* [+0xF] gate activo (!=0xFF) */
    uint32_t rx_mode; /* selector de decodificador (0x200005BC) */
    uint32_t rx_pending; /* flag dato pendiente (0x200005C0); 0xFF = completado */
    uint32_t rx_state; /* id del decoder que completo (0x200005C1) */
    uint32_t rx_result; /* buffer del frame decodificado (0x20001714) */
    uint8_t rx_scratch_sp_valid; /* 1 si usar scratch SP para llamar la ISR */
} ArmProfile;

/* indices logicos de boton */
enum { B_UP = 0, B_DOWN, B_OK, B_BACK, B5, B6, BTN_COUNT };

static const ArmProfile PROFILE_PANDORA = {
    .name = "pandora",
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
    /* bridge RF: pandora usa demod por SW (ISR 0x09554), pin DATA = GPIO B0 */
    .rf_sw_demod = true,
    .demod_isr = 0x09554u,
    .data_port = 1, /* puerto B */
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
    /* pmax: Si4432 en modo PAQUETE/FIFO (demod por HW), NO hay ISR de SW fiable
     * para inyectar; el bridge RX software-demod se deshabilita en este perfil.
     * TX (bit-bang + deteccion de rf_tx_on) si funciona igual. */
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
    uint8_t mode; /* 0 idle, 1 ready, 2 rx, 3 tx (derivado de REG[0x07]) */
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
/* Devuelve el modo si cambio (para que el frontend detecte rf_tx_on/rf_rx_on),
 * -1 si no cambio. REG[0x07] (Operating Mode): bit3=TX, bit2=RX, bit0=READY. */
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

/* ------------------------------------------------------------ estado app */

enum {
    KBIT_UP = 1 << 0,
    KBIT_DOWN = 1 << 1,
    KBIT_LEFT = 1 << 2,
    KBIT_RIGHT = 1 << 3,
    KBIT_OK = 1 << 4,
    KBIT_BACK = 1 << 5,
};

#define SPI_RX_QUEUE_LEN 64

/* --- CC1101 bridge (adaptado de PandoraPIC) --- */
/* TX: el firmware ARM activa rf_tx_on (Si4432 REG[0x07]=0x08) y bit-banguea el
 * pin DATA (GPIO DOUT puerto B bit0). Capturamos cada flanco con su duracion
 * (medida por ninsn entre toggles) en un ring de LevelDuration y lo
 * reproducimos por furi_hal_subghz_start_async_tx. */
#define TX_RING_LEN 2048u
/* RX: furi_hal_subghz_start_async_rx entrega (level,duration_us) por flanco
 * desde IRQ. Se encola thread-safe; en el main loop se inyecta al demodulador
 * del firmware (ISR 0x09554) replicando feed_sub de arm_pandora.py. */
#define RX_QUEUE_LEN 512u

/* Conversion ninsn->us para la TX bit-bang. El keyfob no es tiempo-real bajo el
 * interprete; la escala es aproximada y solo afecta a la cadencia reproducida,
 * no al contenido logico de la trama. 1 insn ~= 1 us como base ajustable. */
#define ARM_INSN_US_NUM 1u
#define ARM_INSN_US_DEN 1u

/* Modos del puente RF del overlay (Left cicla). */
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
    uint8_t* flash; /* buffer FLASH (safe_malloc) */
    uint8_t* ram; /* buffer RAM 32KB */

    Si4432 si;
    /* cola RX del SPI (respuestas del Si4432) */
    uint8_t spi_rx[SPI_RX_QUEUE_LEN];
    uint8_t spi_rx_head, spi_rx_tail;
    int spi_phase; /* -1 idle, 0 pending-after-addr */
    uint8_t spi_kind; /* 'w'/'r' */
    uint8_t spi_reg;

    /* GPIO */
    uint16_t gpio_din[16]; /* din por puerto (bit=1 suelto) */
    uint16_t gpio_dout[16];

    uint16_t timer_cnt[4];

    /* NVIC minimo */
    uint8_t in_irq;
    uint8_t irq_rr;
    uint32_t tick_count;
    uint64_t last_tick_insn;
    uint32_t wfi_pc_hint;

    /* knock FSM (inyeccion de botones sincronizada por PC) */
    const uint8_t* knock_seq;
    uint8_t knock_len;
    uint8_t knock_idx;
    uint8_t knock_sub; /* 0 press, 1 waitrelease */
    uint8_t knock_cur;
    uint8_t knock_spins;
    uint8_t knock_active;
    uint8_t unlocked;
    int max_step;

    /* botones (mascara logica) */
    uint8_t btn_pressed[BTN_COUNT];

    /* framebuffer del Flipper */
    uint8_t screen[FB_SIZE];
    FuriMutex* fb_mutex;
    Gui* gui;
    Canvas* canvas;

    bool radio_on;
    uint32_t frequency;

    /* --- puente RF CC1101 (adaptado de PandoraPIC) --- */
    RfMode rf_mode; /* OFF / TX / RX */

    /* TX bridge: ring de flancos capturados del pin DATA del firmware */
    RfEdge tx_ring[TX_RING_LEN];
    volatile uint32_t tx_head; /* productor (main loop / mmio_write) */
    volatile uint32_t tx_tail; /* consumidor (callback async_tx) */
    bool tx_capturing; /* el firmware esta en modo TX (rf_tx_on) */
    bool tx_last_level; /* ultimo nivel del pin DATA */
    uint64_t tx_last_insn; /* ninsn en el ultimo flanco */
    bool tx_active; /* async_tx en curso */
    uint32_t tx_frames; /* contador de tramas TX logueadas */
    uint32_t tx_edges_frame; /* flancos acumulados de la trama actual */

    /* RX bridge: cola thread-safe alimentada desde la IRQ del CC1101 */
    FuriMessageQueue* rx_queue;
    bool rx_running; /* async_rx activo */
    bool rx_primed; /* estado RAM del receptor preparado (prime_rx) */
    bool rx_prev_level; /* nivel previo para el feed por transicion */
    bool rx_feeding; /* 1 mientras se ejecuta la ISR demod (TIMER1=pulse_ticks) */
    uint32_t rx_pulse_ticks; /* TIMER1_CNT a presentar durante el feed (dur*6) */
    uint32_t rx_events; /* contador de flancos RX inyectados */
    uint32_t rx_frames; /* tramas decodificadas por el firmware */

    volatile bool exit_requested;
    volatile bool menu_requested;
    bool menu_active;

    uint64_t ninsn;
    uint32_t pc;
    uint32_t next_pc;

    /* --- knock-code definido por el usuario (menu) --- */
    uint8_t knock_user[16]; /* secuencia de botones (indices B_*) */
    uint8_t knock_user_len; /* 0 = usar la default del perfil */

    /* --- menu fase 1 (view_dispatcher) --- */
    ViewDispatcher* view_dispatcher;
    Submenu* submenu;
    FuriString* fw_path; /* ruta del firmware elegida */
    char fw_name[32]; /* nombre corto para el label */
} AppState;

static volatile uint32_t s_fb_cb_inflight = 0;

static void wait_inflight_zero(volatile uint32_t* counter) {
    while(__atomic_load_n(counter, __ATOMIC_ACQUIRE) != 0) {
        furi_delay_ms(1);
    }
}

/* fwd decls del puente RF (usados desde los callbacks MMIO) */
static void tx_on_si_mode(AppState* app, int new_mode);
static void tx_capture_data_edge(AppState* app, bool level);

/* ------------------------------------------------------------ botones */

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
        app->gpio_din[port] &= (uint16_t)~(1u << pin); /* 0 = pulsado */
    else
        app->gpio_din[port] |= (uint16_t)(1u << pin); /* 1 = suelto */
    app->btn_pressed[btn] = pressed ? 1 : 0;
}

static void init_buttons_released(AppState* app) {
    for(int i = 0; i < 16; i++) app->gpio_din[i] = 0xFFFF; /* pull-up todo suelto */
    for(int b = 0; b < BTN_COUNT; b++) app->btn_pressed[b] = 0;
}

/* ------------------------------------------------------------ MMIO (callbacks) */

static uint32_t mmio_read(void* ctx, uint32_t addr, int size) {
    AppState* app = (AppState*)ctx;
    uint32_t base = addr & 0xFFFFF000u;
    uint32_t off = addr & 0xFFFu;
    uint32_t v = 0;
    if(base == 0x4000C000u) { /* USART0 (SPI Si4432) */
        if(off == 0x10) {
            v = (1u << 6) | (1u << 5); /* TXBL + RXDATAV listos */
        } else if(off == 0x1C) { /* RXDATA */
            if(app->spi_rx_head != app->spi_rx_tail) {
                v = app->spi_rx[app->spi_rx_head];
                app->spi_rx_head = (uint8_t)((app->spi_rx_head + 1) % SPI_RX_QUEUE_LEN);
            }
        }
    } else if(base == 0x400C4000u) { /* 2o USART (EEPROM/config) */
        if(off == 0x10) v = (1u << 6) | (1u << 5);
    } else if(addr >= 0x40006000u && addr < 0x40006000u + 36u * 16u) { /* GPIO */
        uint32_t port = (addr - 0x40006000u) / 36u;
        uint32_t poff = (addr - 0x40006000u) - port * 36u;
        if(poff == 0x1C && port < 16) v = app->gpio_din[port]; /* DIN */
    } else if(base == 0x40002000u) { /* ADC0 (pmax lee bateria) */
        if(off == 0x08)
            v = (1u << 16); /* SINGLEDV listo */
        else if(off == 0x24)
            v = 0x0C00; /* dato ~3.9V */
    } else if(base == 0x40080000u || base == 0x400C8000u) { /* CMU */
        v = 0;
    } else if(base == 0x40010000u) { /* TIMER0..3 */
        uint32_t tn = off >> 10;
        uint32_t sub = off & 0x3FFu;
        if(sub == 0x24 && tn < 4) {
            /* Durante el feed RX la ISR demod lee TIMER1.CNT como ANCHO del
             * semiperiodo (ver arm_pandora.py: TIMER1_CNT = dur_us*6). */
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
        /* eco basura al enviar la direccion */
        app->spi_rx[app->spi_rx_tail] = 0;
        app->spi_rx_tail = (uint8_t)((app->spi_rx_tail + 1) % SPI_RX_QUEUE_LEN);
    } else {
        if(app->spi_kind == 'w') {
            int nm = si4432_write(&app->si, app->spi_reg, b);
            if(nm >= 0) tx_on_si_mode(app, nm); /* rf_tx_on / rf_rx_on detectado */
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
            /* captura TX: el firmware bit-banguea el pin DATA (DOUT port B bit0)
             * mientras esta en modo TX (rf_tx_on). */
            if(app->tx_capturing && port == app->prof->data_port) {
                bool level = (app->gpio_dout[port] >> app->prof->data_pin) & 1u;
                tx_capture_data_edge(app, level);
            }
        }
    }
    (void)size;
}

/* ------------------------------------------------------------ NVIC minimo */

static uint32_t read_u32_le(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* VTOR actual (SCB->VTOR en 0xE000ED08). Lo leemos via el core (callback). */
static uint32_t cur_vtor(AppState* app) {
    return thumb_read_mem(app->cpu, 0xE000ED08u, 4);
}

static uint32_t irq_handler_addr(AppState* app, int irqn) {
    uint32_t vt = cur_vtor(app);
    uint32_t off = vt + 0x40u + (uint32_t)irqn * 4u;
    return thumb_read_mem(app->cpu, off, 4);
}

/* IRQs de timer que disparamos periodicamente (del RE: IRQ12/13/14 timers,
 * IRQ1 sistema). Numeros de IRQ (no de excepcion). */
static const int TIMER_IRQS[] = {12, 13, 14, 1};

static void nvic_tick(AppState* app) {
    if(app->in_irq) return;
    uint32_t vtor = cur_vtor(app);
    if(vtor < 0x8000u) return; /* app aun no instalo su tabla */
    int irqn = TIMER_IRQS[app->irq_rr % (int)(sizeof(TIMER_IRQS) / sizeof(int))];
    app->irq_rr++;
    uint32_t handler = irq_handler_addr(app, irqn);
    if(handler == 0 || handler == 0x82DBu) return; /* default handler: skip */
    app->tick_count++;
    app->in_irq = 1;
    thumb_enter_exception(app->cpu, handler, THUMB_EXC_RETURN_MSP_THREAD, (uint32_t)(irqn + 16));
    app->next_pc = thumb_get_reg(app->cpu, 15);
}

/* Localiza una instruccion WFI (0xBF30) en el codigo app para detectar sleep. */
static uint32_t find_wfi(const uint8_t* flash, uint32_t size) {
    uint32_t lim = size < 0x20000u ? size : 0x20000u;
    for(uint32_t off = 0x8100u; off + 1 < lim; off += 2) {
        if(flash[off] == 0x30 && flash[off + 1] == 0xBF) return off;
    }
    return 0xFFFFFFFFu;
}

/* ------------------------------------------------------------ knock FSM */

/* Llamado por el hook por-instruccion. Inyecta el knock-code sincronizado con
 * el escaner: pulsa en scan_head, suelta tras N spins (confirma al soltar).
 * Replica _knock_step de pandora_tui.py. */
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

/* Hook por instruccion: cuenta, detecta unlock_pc, y avanza el knock FSM. */
static void code_hook(void* ctx, uint32_t pc) {
    AppState* app = (AppState*)ctx;
    app->ninsn++;
    app->pc = pc;
    if(!app->unlocked && pc == app->prof->unlock_pc) {
        app->unlocked = 1;
    }
    if(app->knock_active) knock_step(app, pc);
}

/* ------------------------------------------------------------ ejecucion */

/* Una rafaga de instrucciones con manejo de WFI/tick + EXC_RETURN. Replica
 * run_burst de pandora_tui.py. */
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
        /* EXC_RETURN: el ISR termino (bx lr con 0xFFFFFFFx) */
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
    /* buffer en la propia struct para el tap de 1 paso */
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

static bool auto_unlock(AppState* app) {
    app->unlocked = 0;
    app->max_step = -1;
    /* 1) settle hasta que el escaner este activo (ventana valida) */
    uint64_t done = 0;
    while(done < KNOCK_SETTLE_INSN) {
        run_burst(app, 100000);
        done += 100000;
    }
    /* 2) inyectar la secuencia completa sincronizada con el escaner.
     * Preferir la secuencia INGRESADA por el usuario en el menu; si no definio
     * ninguna, usar la default del perfil detectado. */
    if(app->knock_user_len > 0) {
        app->knock_seq = app->knock_user;
        app->knock_len = app->knock_user_len;
        FURI_LOG_I(TAG, "knock: usando secuencia del usuario (len=%u)", app->knock_user_len);
    } else {
        app->knock_seq = app->prof->knock;
        app->knock_len = app->prof->knock_len;
        FURI_LOG_I(TAG, "knock: usando default del perfil %s (len=%u)",
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
    /* 3) gate final: mantener B5 un rato */
    set_button(app, app->prof->gate, true);
    done = 0;
    while(done < 600000) {
        run_burst(app, 50000);
        done += 50000;
    }
    set_button(app, app->prof->gate, false);
    /* 4) dejar correr para que la app redibuje la UI (con ticks) */
    done = 0;
    while(done < 600000) {
        run_burst(app, 100000);
        done += 100000;
    }
    if(app->unlocked) {
        FURI_LOG_I(TAG, "unlock OK (knock-code aceptado, PC unlock=0x%06lX)",
                   (unsigned long)app->prof->unlock_pc);
    } else {
        FURI_LOG_W(TAG, "unlock FALLO (knock-code no completo / no aceptado)");
    }
    return app->unlocked != 0;
}

/* ------------------------------------------------------------ render OLED */

/* El framebuffer del keyfob es 92col x 16pag = 1472B, page-addressed, bit0
 * arriba, GRAYSCALE 2bpp: cada byte de pagina son 4 pixeles logicos (bits
 * 2k,2k+1 = pixel k), 16 pag x 4 = 64 filas reales. Colapsamos 2bpp->1bpp
 * (pixel encendido si nivel>=1) y lo pintamos centrado en la pantalla 128x64
 * del Flipper. Panel logico 92x64. (Ver emu/oled_render.py fb_to_gray.) */
static void render_oled(AppState* app) {
    furi_mutex_acquire(app->fb_mutex, FuriWaitForever);
    memset(app->screen, 0, FB_SIZE);
    uint32_t fb = app->prof->fb_addr;
    const int x_off = (SCREEN_W - OLED_COLS) / 2; /* centrar 92 en 128 => 18 */
    for(int page = 0; page < OLED_PAGES; page++) {
        for(int col = 0; col < OLED_COLS; col++) {
            uint32_t a = fb + (uint32_t)(page * OLED_COLS + col);
            uint8_t v = (uint8_t)thumb_read_mem(app->cpu, a, 1);
            for(int k = 0; k < 4; k++) {
                /* nivel del pixel = popcount de los 2 bits (2k, 2k+1) */
                int lvl = ((v >> (2 * k)) & 1) + ((v >> (2 * k + 1)) & 1);
                if(lvl == 0) continue; /* apagado */
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
        data[i] = (uint8_t)(src[i] ^ 0xFF); /* screen bit=1 claro; display bit=1 oscuro */
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

/* ------------------------------------------------------------ carga + boot */

typedef enum { LoadOk, LoadIoError, LoadNoMem, LoadBadFormat } LoadResult;

/* Parser Intel HEX minimo desde buffer: llena flash[] por direccion (solo el
 * espacio de codigo < PA_FLASH_SIZE). Devuelve 0 OK. */
static int parse_ihex_into_flash(const uint8_t* hex, size_t n, uint8_t* flash) {
    size_t i = 0;
    uint32_t ext_lin = 0;
    while(i < n) {
        /* saltar hasta ':' */
        while(i < n && hex[i] != ':') i++;
        if(i >= n) break;
        i++; /* saltar ':' */
        /* helper para leer 2 hex */
        #define HEX2(dst)                                                   \
            do {                                                            \
                if(i + 1 >= n) return -1;                                   \
                int hi = hex[i], lo = hex[i + 1];                           \
                hi = (hi <= '9') ? hi - '0' : (hi | 0x20) - 'a' + 10;       \
                lo = (lo <= '9') ? lo - '0' : (lo | 0x20) - 'a' + 10;       \
                (dst) = (uint8_t)((hi << 4) | lo);                          \
                i += 2;                                                     \
            } while(0)
        uint8_t count, ah, al, rtype;
        HEX2(count);
        HEX2(ah);
        HEX2(al);
        HEX2(rtype);
        uint32_t addr = ((uint32_t)ah << 8) | al;
        if(rtype == 0x00) { /* data */
            uint32_t base = ext_lin + addr;
            for(uint8_t k = 0; k < count; k++) {
                uint8_t b;
                HEX2(b);
                if(base + k < PA_FLASH_SIZE) flash[base + k] = b;
            }
            uint8_t chk;
            HEX2(chk);
            (void)chk;
        } else if(rtype == 0x01) { /* EOF */
            break;
        } else if(rtype == 0x04) { /* ext linear addr */
            uint8_t b0, b1, chk;
            HEX2(b0);
            HEX2(b1);
            HEX2(chk);
            (void)chk;
            ext_lin = ((uint32_t)b0 << 8 | b1) << 16;
        } else {
            /* otros tipos (02/03/05): consumir count+chk */
            for(uint8_t k = 0; k < count; k++) {
                uint8_t b;
                HEX2(b);
                (void)b;
            }
            uint8_t chk;
            HEX2(chk);
            (void)chk;
        }
        #undef HEX2
    }
    return 0;
}

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
        uint8_t* buf = (uint8_t*)safe_malloc((size_t)fsize + 1);
        if(!buf) {
            res = LoadNoMem;
            break;
        }
        size_t got = storage_file_read(f, buf, (size_t)fsize);
        if(got != fsize) {
            free(buf);
            break;
        }
        buf[got] = 0;

        app->flash = (uint8_t*)safe_malloc(PA_FLASH_SIZE);
        if(!app->flash) {
            free(buf);
            res = LoadNoMem;
            break;
        }
        memset(app->flash, 0xFF, PA_FLASH_SIZE);

        if(buf[0] == ':') {
            if(parse_ihex_into_flash(buf, got, app->flash) < 0) {
                free(buf);
                res = LoadBadFormat;
                break;
            }
        } else {
            size_t copy = got < PA_FLASH_SIZE ? got : PA_FLASH_SIZE;
            memcpy(app->flash, buf, copy);
        }
        free(buf);
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

/* Llamado cuando el firmware cambia el modo del Si4432 (REG[0x07]). En TX
 * empezamos a capturar los flancos del pin DATA; al salir de TX cerramos la
 * trama capturada. Replica la deteccion de rf_tx_on de PandoraPIC. */
static void tx_on_si_mode(AppState* app, int new_mode) {
    if(app->rf_mode != RfModeTx) return; /* solo capturamos si el overlay esta en TX */
    if(new_mode == 3) { /* TX */
        app->tx_capturing = true;
        app->tx_last_level = (app->gpio_dout[app->prof->data_port] >> app->prof->data_pin) & 1u;
        app->tx_last_insn = app->ninsn;
        app->tx_edges_frame = 0;
        FURI_LOG_I(TAG, "RF: firmware rf_tx_on -> capturando pin DATA (B%u.%u)",
                   app->prof->data_port, app->prof->data_pin);
    } else { /* IDLE/READY/RX: fin de la trama */
        if(app->tx_capturing) {
            app->tx_capturing = false;
            FURI_LOG_I(TAG, "RF: fin TX firmware (%lu flancos capturados)",
                       (unsigned long)app->tx_edges_frame);
        }
    }
}

/* Captura un flanco del pin DATA de salida del firmware: acumula el nivel que
 * ACABA de terminar con su duracion (ninsn entre toggles) en el ring de TX. */
static void tx_capture_data_edge(AppState* app, bool level) {
    if(!app->tx_capturing) return;
    if(level == app->tx_last_level) return; /* sin flanco */
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

/* Callback de async_tx: drena el ring de flancos capturados. */
static LevelDuration radio_tx_callback(void* context) {
    AppState* app = (AppState*)context;
    uint32_t tail = app->tx_tail;
    if(tail == app->tx_head) return level_duration_reset();
    RfEdge e = app->tx_ring[tail];
    app->tx_tail = (tail + 1) % TX_RING_LEN;
    return level_duration_make(e.level, e.duration);
}

/* Lanza/termina la reproduccion de los flancos acumulados. Main loop. */
static void tx_bridge_flush(AppState* app) {
    if(app->rf_mode != RfModeTx || !app->radio_on) return;
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
    uint32_t pending = (app->tx_head + TX_RING_LEN - app->tx_tail) % TX_RING_LEN;
    if(pending < 16) return; /* esperar una rafaga razonable */
    if(app->tx_capturing) return; /* trama aun en captura: esperar al cierre */
    if(!furi_hal_subghz_is_tx_allowed(app->frequency)) {
        FURI_LOG_E(TAG, "TX NO permitida @%lu Hz; descarto %lu flancos",
                   (unsigned long)app->frequency, (unsigned long)pending);
        app->tx_tail = app->tx_head;
        return;
    }
    /* loguear la trama (primeros flancos: nivel+duracion). */
    FURI_LOG_I(TAG, "TX arranque async: %lu flancos", (unsigned long)pending);
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
        FURI_LOG_D(TAG, "TX trama[0..7]: %s", line);
    }
    if(furi_hal_subghz_start_async_tx(radio_tx_callback, app)) {
        app->tx_active = true;
    } else {
        FURI_LOG_E(TAG, "start_async_tx fallo");
        app->tx_tail = app->tx_head;
    }
}

/* ===================================================================== RX bridge */

/* Callback del CC1101 (corre en IRQ): encola el flanco. NO bloquear. */
static void radio_rx_callback(bool level, uint32_t duration, void* context) {
    AppState* app = (AppState*)context;
    RfEdge e = {.level = level, .duration = duration};
    furi_message_queue_put(app->rx_queue, &e, 0);
}

/* Ejecuta una subrutina del firmware (addr) como lo hace call() en
 * arm_pandora.py: SP de scratch, LR=sentinela, corre hasta volver a la
 * sentinela. Deja el estado del core (PC/SP/LR) restaurado para el main loop. */
#define ARM_CALL_SENTINEL 0x001FFFF0u
#define ARM_CALL_SCRATCH_SP 0x20007000u

static void call_subroutine(AppState* app, uint32_t addr, uint32_t max_insn) {
    ThumbCore* cpu = app->cpu;
    /* guardar contexto del main loop */
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

    /* restaurar */
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

/* Prepara el estado RAM del receptor como lo dejaria rf_rx_on (prime_rx de
 * arm_pandora.py): gate activo, modo, shift-chains al nivel de reposo. */
static void rx_prime(AppState* app, uint8_t mode, bool init_level) {
    const ArmProfile* p = app->prof;
    uint8_t lvl = init_level ? 1 : 0;
    /* limpiar la estructura RX (0x40 bytes) */
    for(uint32_t o = 0; o < 0x40; o++) rx_wr8(app, p->rx_struct + o, 0);
    rx_wr8(app, p->rx_gate, 1); /* [+0xF] gate activo (!=0xFF) */
    rx_wr8(app, p->rx_mode, mode); /* selector de decodificador */
    rx_wr8(app, p->rx_pending, 0); /* hay dato (!=0xFF) */
    rx_wr8(app, p->rx_state, 0);
    for(uint32_t o = 0x10; o < 0x1C; o++) rx_wr8(app, p->rx_struct + o, lvl);
    rx_wr8(app, p->rx_struct + 9, lvl);
    rx_wr8(app, p->rx_struct + 0xA, lvl);
    /* pin DATA (DIN puerto B bit0) al nivel de reposo */
    if(init_level)
        app->gpio_din[p->data_port] |= (uint16_t)(1u << p->data_pin);
    else
        app->gpio_din[p->data_port] &= (uint16_t)~(1u << p->data_pin);
    app->rx_prev_level = init_level;
    app->rx_primed = true;
}

/* Inyecta UN flanco (nivel,dur_us) al demodulador del firmware, replicando
 * feed_sub de arm_pandora.py: fija TIMER1_CNT=dur*6, pin DATA=nivel, siembra la
 * shift-chain con el nivel PREVIO y dispara la ISR demod una vez. Devuelve true
 * si el firmware completo una trama (rx_pending==0xFF). */
static bool rx_feed_edge(AppState* app, bool level, uint32_t dur_us) {
    const ArmProfile* p = app->prof;
    bool prev = app->rx_prev_level;
    app->rx_pulse_ticks = (dur_us * 6u) & 0xFFFFu;
    /* pin DATA en el nivel NUEVO (el flanco ya ocurrio) */
    if(level)
        app->gpio_din[p->data_port] |= (uint16_t)(1u << p->data_pin);
    else
        app->gpio_din[p->data_port] &= (uint16_t)~(1u << p->data_pin);
    /* shift-chain + niveles filtrados al nivel PREVIO para forzar la deteccion
     * del flanco (ver nota de calibracion en arm_pandora.py / CAL_arm_decoder). */
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
        FURI_LOG_W(TAG, "RX bridge no disponible en perfil %s (Si4432 FIFO/HW)",
                   app->prof->name);
        return;
    }
    if(app->tx_active) {
        furi_hal_subghz_stop_async_tx();
        app->tx_active = false;
    }
    furi_hal_subghz_idle();
    furi_message_queue_reset(app->rx_queue);
    rx_prime(app, 1 /* modo decoder por defecto */, false);
    furi_hal_subghz_start_async_rx(radio_rx_callback, app);
    app->rx_running = true;
    FURI_LOG_I(TAG, "RX bridge ON (inyecta en ISR 0x%06lX, pin DATA B%u.%u)",
               (unsigned long)app->prof->demod_isr, app->prof->data_port,
               app->prof->data_pin);
}

static void rx_bridge_stop(AppState* app) {
    if(!app->rx_running) return;
    furi_hal_subghz_stop_async_rx();
    furi_hal_subghz_idle();
    app->rx_running = false;
    app->rx_primed = false;
    FURI_LOG_I(TAG, "RX bridge OFF (%lu flancos inyectados, %lu tramas)",
               (unsigned long)app->rx_events, (unsigned long)app->rx_frames);
}

/* Drena la cola RX e inyecta cada flanco al demodulador del firmware. */
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
            FURI_LOG_I(TAG, "RX trama #%lu decodificada id=%u frame=%s",
                       (unsigned long)app->rx_frames, id, line);
            /* re-primar para la siguiente trama */
            rx_prime(app, 1, app->rx_prev_level);
        }
        if(drained >= 48) break; /* no acaparar el frame */
    }
    if(drained) {
        FURI_LOG_D(TAG, "RX inyectados %d flancos (total %lu)", drained,
                   (unsigned long)app->rx_events);
    }
}

/* Alterna el puente RF: OFF -> TX -> RX -> OFF. Logueado (como PandoraPIC). */
static void rf_cycle_mode(AppState* app) {
    switch(app->rf_mode) {
    case RfModeOff:
        app->rf_mode = RfModeTx;
        app->tx_head = app->tx_tail = 0;
        app->tx_capturing = false;
        app->tx_edges_frame = 0;
        /* si el firmware ya esta en TX, empezar a capturar de inmediato */
        if(app->si.mode == 3) tx_on_si_mode(app, 3);
        FURI_LOG_I(TAG, "RF modo TX (captura pin DATA del firmware)");
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
            /* perfil sin SW-demod: saltar directo a OFF */
            app->rf_mode = RfModeOff;
            FURI_LOG_I(TAG, "RF modo OFF (RX no soportado en este perfil)");
        } else {
            FURI_LOG_I(TAG, "RF modo RX (inyecta al demodulador del firmware)");
        }
        break;
    case RfModeRx:
    default:
        rx_bridge_stop(app);
        app->rf_mode = RfModeOff;
        FURI_LOG_I(TAG, "RF modo OFF");
        break;
    }
}

/* ------------------------------------------------------------ overlay ayuda */

/* Overlay in-emulador (direct-draw): estado + control del puente RF (Left). */
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

/* ============================================================ menu fase 1 */
/* view_dispatcher + submenu (NO number_input: el ARM usa knock-code). Mismo
 * patron que PandoraPIC: el dispatcher BLOQUEA; los items ponen una bandera y
 * paran el dispatcher; el main abre file browser / re-crea el menu / arranca el
 * emulador. NUNCA coexisten view_dispatcher y direct-draw. */

/* Vistas (una sola: el submenu se re-puebla para menu-principal / knock-builder) */
typedef enum {
    PandoraViewSubmenu,
} PandoraView;

/* Items del menu principal */
typedef enum {
    MenuItemFirmware,
    MenuItemKnock,
    MenuItemLaunch,
} MenuItemMain;

/* Items del builder del knock-code (botones + Done/Clear) */
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

/* Flujo pedido al parar el dispatcher. */
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

/* Construye la cadena de la secuencia actual del knock (default del perfil si el
 * usuario no definio ninguna). */
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

/* (Re)construye el submenu del knock-builder con la secuencia actual. */
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

/* (Re)construye el menu principal con las etiquetas actualizadas. */
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
        FURI_LOG_I(TAG, "knock-builder: secuencia confirmada (len=%u)", app->knock_user_len);
        app->menu_active = false;
        menu_rebuild(app);
        return;
    case KnockItemClear:
        app->knock_user_len = 0; /* vuelve a la default del perfil */
        FURI_LOG_I(TAG, "knock-builder: limpiada (usa default del perfil)");
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
    default: break;
    }
}

/* Back: desde el knock-builder vuelve al menu principal; desde el menu sale. */
static bool menu_back_callback(void* context) {
    AppState* app = (AppState*)context;
    /* Determinar la vista activa es engorroso con la API; usamos una bandera
     * simple: si el usuario esta en el knock-builder, volvemos al menu. El
     * submenu del knock no tiene sub-vistas, asi que basta con cambiar de vista
     * y seguir. Para distinguir, miramos si la cabecera actual es del knock via
     * un flag. */
    if(app->menu_active) { /* reuse menu_active como "en knock-builder" */
        app->menu_active = false;
        menu_rebuild(app);
        view_dispatcher_switch_to_view(app->view_dispatcher, PandoraViewSubmenu);
        return true;
    }
    g_menu_flow = FlowExit;
    view_dispatcher_stop(app->view_dispatcher);
    return true;
}

/* Construye y corre el menu (bloquea hasta stop). Crea/destruye el
 * view_dispatcher AQUI para dejar el GUI libre al volver. */
static MenuFlow run_menu(AppState* app, Gui* gui) {
    g_menu_flow = FlowExit;
    app->menu_active = false;

    app->view_dispatcher = view_dispatcher_alloc();
    app->submenu = submenu_alloc();

    view_dispatcher_set_event_callback_context(app->view_dispatcher, app);
    view_dispatcher_set_navigation_event_callback(app->view_dispatcher, menu_back_callback);

    view_dispatcher_add_view(
        app->view_dispatcher, PandoraViewSubmenu, submenu_get_view(app->submenu));
    /* una sola instancia de submenu sirve para ambas vistas logicas; para poder
     * mapear Back distinto reutilizamos la bandera menu_active. Para que la vista
     * del knock sea independiente, usamos el mismo submenu re-poblado. */

    menu_rebuild(app);
    view_dispatcher_attach_to_gui(app->view_dispatcher, gui, ViewDispatcherTypeFullscreen);
    view_dispatcher_switch_to_view(app->view_dispatcher, PandoraViewSubmenu);

    view_dispatcher_run(app->view_dispatcher); /* BLOQUEA */

    view_dispatcher_remove_view(app->view_dispatcher, PandoraViewSubmenu);
    submenu_free(app->submenu);
    view_dispatcher_free(app->view_dispatcher);
    app->submenu = NULL;
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
    /* reset del core: SP=*(0), PC=*(4) desde flash */
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
    app->spi_phase = -1;
    app->spi_rx_head = app->spi_rx_tail = 0;
    init_buttons_released(app);
    si4432_init(&app->si);
    auto_unlock(app);
}

/* ============================================================ emulador fase 2 */

/* Corre el emulador con direct-draw takeover. El firmware YA esta cargado y
 * booteado/desbloqueado y el view_dispatcher YA se libero (no coexisten). */
static void run_emulator(AppState* app, Gui* gui) {
    Canvas* canvas = NULL;
    bool fb_cb_added = false;

    app->rx_queue = furi_message_queue_alloc(RX_QUEUE_LEN, sizeof(RfEdge));
    if(!app->rx_queue) {
        FURI_LOG_E(TAG, "rx_queue alloc fallo");
        return;
    }

    app->gui = gui;
    gui_add_framebuffer_callback(gui, framebuffer_commit_callback, app);
    fb_cb_added = true;
    canvas = gui_direct_draw_acquire(gui);
    if(!canvas) {
        FURI_LOG_E(TAG, "direct_draw_acquire fallo");
        goto teardown;
    }
    app->canvas = canvas;

    FURI_LOG_I(TAG, "emulador: main loop (fw=%s, %s)", app->prof->name,
               app->unlocked ? "UNLOCKED" : "LOCKED");

    uint32_t epoch = furi_get_tick();
    uint64_t next_us = 0;
    uint8_t prev_keys = 0;

    while(!app->exit_requested) {
        if(app->menu_requested) {
            app->menu_requested = false;
            app->menu_active = true;
            overlay_draw(app);
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

        uint8_t keys = poll_raw_keys();
        /* Up+Down juntos = overlay de la app */
        if((keys & (KBIT_UP | KBIT_DOWN)) == (KBIT_UP | KBIT_DOWN) &&
           (prev_keys & (KBIT_UP | KBIT_DOWN)) != (KBIT_UP | KBIT_DOWN)) {
            app->menu_requested = true;
            prev_keys = keys;
            continue;
        }
        /* botones del keyfob (post-unlock: nav basica por edge) */
        uint8_t ch = (uint8_t)(keys & ~prev_keys);
        if(ch & KBIT_UP) tap(app, B_UP);
        if(ch & KBIT_DOWN) tap(app, B_DOWN);
        if(ch & KBIT_OK) tap(app, B_OK);
        if(ch & KBIT_RIGHT) tap(app, B6);
        /* Left se reserva para el overlay (ciclo RF); no se mapea a boton aqui */
        prev_keys = keys;

        /* correr una tajada del firmware real (captura TX via mmio_write) */
        run_burst(app, 150000);

        /* bridge RF: reproducir TX acumulada / inyectar RX recibida */
        tx_bridge_flush(app);
        rx_bridge_pump(app);

        /* redibujar la pantalla desde el framebuffer del firmware */
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
    FURI_LOG_I(TAG, "emulador: salida");
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

    /* --- FASE 1: menu (view_dispatcher). Loop hasta Launch o Exit. --- */
    while(true) {
        /* mantener el perfil actualizado para mostrar la default del knock */
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
                FURI_LOG_I(TAG, "firmware elegido: %s (perfil %s)", app->fw_name,
                           app->prof->name);
            }
            continue;
        } else if(flow == FlowLaunch) {
            if(app->fw_name[0] == 0) {
                FURI_LOG_W(TAG, "Launch sin firmware; abriendo file browser");
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

    /* --- FASE 2: emulador (direct-draw). Solo si el usuario pidio Launch. --- */
    if(launch) {
        do {
            app->prof = pick_profile(furi_string_get_cstr(app->fw_path));

            app->cpu = (ThumbCore*)malloc(sizeof(ThumbCore));
            if(!app->cpu) {
                FURI_LOG_E(TAG, "cpu alloc fallo");
                break;
            }
            thumb_init(app->cpu);

            LoadResult lr = load_firmware(app, storage, furi_string_get_cstr(app->fw_path));
            if(lr != LoadOk) {
                DialogMessage* msg = dialog_message_alloc();
                const char* t = "Error de carga";
                if(lr == LoadNoMem) t = "Sin memoria";
                if(lr == LoadBadFormat) t = "Formato invalido\n(.hex o .bin)";
                FURI_LOG_E(TAG, "carga fallida");
                dialog_message_set_text(msg, t, 64, 30, AlignCenter, AlignCenter);
                dialog_message_set_buttons(msg, NULL, "OK", NULL);
                dialog_message_show(dialogs, msg);
                dialog_message_free(msg);
                break;
            }

            app->ram = (uint8_t*)safe_malloc(PA_RAM_SIZE);
            if(!app->ram) {
                FURI_LOG_E(TAG, "ram alloc fallo");
                break;
            }
            memset(app->ram, 0, PA_RAM_SIZE);

            /* conectar memoria: regiones directas FLASH/RAM + callbacks MMIO */
            thumb_set_regions(
                app->cpu, app->flash, PA_FLASH_BASE, PA_FLASH_SIZE, app->ram, PA_RAM_BASE,
                PA_RAM_SIZE);
            thumb_set_mem_cb(app->cpu, mmio_read, mmio_write, app);
            thumb_set_hook(app->cpu, code_hook, app);

            app->wfi_pc_hint = find_wfi(app->flash, PA_FLASH_SIZE);

            app->fb_mutex = furi_mutex_alloc(FuriMutexTypeNormal);
            if(!app->fb_mutex) {
                FURI_LOG_E(TAG, "fb_mutex alloc fallo");
                break;
            }

            radio_init(app);

            FURI_LOG_I(TAG, "perfil: %s (knock %s)", app->prof->name,
                       app->knock_user_len ? "usuario" : "default");

            /* boot + auto-unlock (ventana valida del scanner del knock) */
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
