/* Pandora PIC - emulator of the REAL Pandora DXL-5000 keyfob firmware (PIC18)
 * for the Flipper Zero.
 *
 * The keyfob firmware runs UNMODIFIED on top of the PIC18 interpreter
 * (lib/pic18core). This app emulates the hardware the firmware expects:
 *   - SSD1306 128x32 OLED display: its GDDRAM is reconstructed by intercepting
 *     the firmware's SW-SPI serializer (hook on send-byte + DC pin), just like
 *     the reference Python emulator (emu/pic18_tui.py). It is scaled/drawn on
 *     the Flipper's 128x64 screen.
 *   - Buttons (active-low) mapped to the Flipper's keys (direct GPIO polling,
 *     like FlipperGB). The button register is per firmware VERSION, confirmed
 *     dynamically (emu/re_btn_siblings.py, emu/re_sm_btnprobe.py): the DXL-5000
 *     siblings (mario2/mariofull) read 5 buttons from LATE (0xF8E) bits 1,2,4,6,7;
 *     the Pandora D010/D174 (super_mario) reads 3 buttons from PORTB (0xF81) bits
 *     1,2,4. The 3 firmwares target the same D010 keyfob but are different builds,
 *     so the button register differs by version (see the PicProfile comments).
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

#define PROG_MAX 0x1B000u /* program address space extent (covers mariofull 0x1A17B) */

#define ALLOC_MARGIN 1024u
#define FRAME_US 30000 /* ~33 Hz UI refresh; the keyfob is not a game */

/* Fluidity: the firmware runs flat out, but the input must stay responsive. The
 * per-frame insn budget (steps_per_frame) is executed in SMALLER sub-bursts so
 * the input is polled between them (same TOTAL useful insn/s -- only the polling
 * cadence changes, like the ARM front-end). A single 150k burst at ~60-80k
 * insn/s meant input was only sampled roughly once per ~2 s; sub-bursting cuts
 * that to tens of ms without any pacing/throttle penalty (render + canvas_commit
 * still happen once per frame). */
#define EMU_INSN_PER_BURST 4000u /* insns between input polls (input latency cap) */

/* Canonical flat flash image on the SD (PROG_MAX bytes, gaps = 0xFF). The flash
 * cache streams 4KB pages from here. Lives in the app data dir (like PandoraARM's
 * PA_FLASH_BIN). The PIC18 program segment (0x0000..~0x1A17C) is normalised into
 * this flat file ONCE at load; config words (0x300000+) are parsed separately
 * into cpu->config_* and are NOT part of the flat image. */
#define PP_DATA_DIR "/ext/apps_data/pandora_pic"
#define PP_FLASH_BIN PP_DATA_DIR "/flash.bin"

#define PIN_DEFAULT 2552

/* On the Flipper malloc() does NOT return NULL: it crashes the firmware with
 * OOM. Every allocation that depends on the .hex size goes through here. */
static void* safe_malloc(size_t size) {
    if(memmgr_heap_get_max_free_block() < size + ALLOC_MARGIN) return NULL;
    return malloc(size);
}

/* --- streaming LRU flash cache (replaces the old "all pages resident" malloc) --
 * ROOT CAUSE of the mariofull (104KB) "boot failed" on hardware: the previous
 * design allocated ONE 4KB slot per program page and kept EVERYTHING resident.
 * mariofull is 0x1A17C bytes = 27 pages = 108KB, but the Flipper only has ~71KB
 * of heap, so only ~18 pages fit; the other 9 pages read as 0xFF and the firmware
 * executes garbage -> boot fails (exactly the reported symptom).
 *
 * FIX (same pattern as PandoraARM's FlashCache / FlipperGB's RomCache): split the
 * flash into 4KB pages streamed from a canonical FLAT .bin on the SD, held in an
 * LRU cache of N slots. Each slot is its own small safe_malloc (small blocks fit
 * a fragmented heap; no single big block is ever requested). We allocate AS MANY
 * slots as fit, leaving only a small heap reserve for the GUI/system -> the whole
 * mariofull image (27 pages) fits in slots on a clean heap and is fully resident.
 * If some pages do not fit, the remaining ones are served on-demand (seek+read
 * 4KB, LRU eviction) -- the firmware still boots, just slower on misses.
 *
 * CRITICAL PERF NOTE (differs from PandoraARM!): the PIC18 executes DIRECTLY from
 * the paged program memory -- EVERY instruction fetch (pword) resolves 1-2 bytes
 * through this cache. (The ARM copies code to RAM and runs from there, so its
 * cache is cold-path.) So:
 *   - we MINIMISE the heap reserve to maximise resident slots (fully-resident =
 *     O(1) direct pointer, no SD at runtime -> full speed);
 *   - we keep a 1-entry "last page" fast path (last_page/last_ptr) so sequential
 *     fetches inside the same 4KB page skip the linear slot scan entirely;
 *   - on a (rare) miss we seek+read 4KB from the SD. If the working set does not
 *     fit the slots, this is slow -- documented honestly. For mariofull on a
 *     clean heap it IS fully resident, so no runtime SD reads at all. */
#define FLASH_PAGE_SIZE 0x1000u /* 4KB cache granularity (matches PandoraARM) */
#define FLASH_MAX_PAGES 32u /* 32 * 4KB = 128KB, covers mariofull (0x1A17C) */
/* Heap kept free for the GUI direct-draw takeover + input + CC1101 + rx_queue
 * while the emulator runs. Kept SMALL because the PIC18 runs from the cache: we
 * want as many resident slots as possible. The degrade ladder frees even more if
 * the first pass cannot make the image fully resident. */
#define FLASH_HEAP_RESERVE (10u * 1024u)
#define FLASH_MIN_SLOTS 4u /* absolute floor (streaming still works, just slow) */

typedef struct {
    File* file; /* flat .bin on SD (PROG_MAX bytes, gaps=0xFF); NULL if resident */
    uint32_t flash_used; /* logical program extent in bytes (PROG_MAX) */
    uint32_t num_pages; /* ceil(flash_used / FLASH_PAGE_SIZE) */
    uint8_t* slots[FLASH_MAX_PAGES]; /* num_slots pointers to 4KB blocks */
    uint32_t slot_page[FLASH_MAX_PAGES]; /* flash page in each slot (0xFFFFFFFF=empty) */
    uint32_t slot_use[FLASH_MAX_PAGES]; /* LRU stamps */
    /* O(1) page->slot reverse index for the STREAMING path: page_slot[page] is the
     * slot that currently holds `page`, or SLOT_NONE if the page is not resident.
     * Kept in sync on every fill/evict so fc_page_ptr never scans the slot array.
     * (Unused when fully_resident: there slot i == page i directly.) */
    uint16_t page_slot[FLASH_MAX_PAGES];
    uint32_t use_counter;
    uint32_t miss_count; /* diagnostics: SD page misses (fills) */
    uint16_t num_slots;
    bool fully_resident; /* every page has a slot -> O(1) index, no eviction */
    /* 2-entry fast path for the per-fetch hot loop. The PIC18 constantly alternates
     * between instruction fetches (code page) and TBLRD/table reads (data page): a
     * single entry would thrash (every data read evicts the code page and vice
     * versa). Two slots cover the common "code page + data page" working pair so
     * both fetch kinds hit the fast path without the page lookup. */
    uint32_t last_page; /* last resolved page (0xFFFFFFFF = none) */
    uint8_t* last_ptr; /* pointer to the slot holding last_page */
    uint32_t last_page2; /* 2nd fast-path page (0xFFFFFFFF = none) */
    uint8_t* last_ptr2; /* pointer to the slot holding last_page2 */
} FlashCache;

#define FC_SLOT_NONE 0xFFFFu

/* Resets the cache to an empty state (no file, no slots). */
static void fc_init(FlashCache* fc) {
    memset(fc, 0, sizeof(*fc));
    fc->last_page = 0xFFFFFFFFu;
    fc->last_page2 = 0xFFFFFFFFu;
    for(uint32_t i = 0; i < FLASH_MAX_PAGES; i++) fc->page_slot[i] = FC_SLOT_NONE;
}

/* Reads one 4KB page from the flat .bin into dst. A short read (last partial page
 * / SD hiccup) is padded with 0xFF (blank flash). Returns false on seek/IO fault. */
static bool fc_read_page(FlashCache* fc, uint32_t page, uint8_t* dst) {
    fc->miss_count++;
    uint32_t base = page * FLASH_PAGE_SIZE;
    if(!storage_file_seek(fc->file, base, true)) return false;
    size_t got = storage_file_read(fc->file, dst, FLASH_PAGE_SIZE);
    if(got < FLASH_PAGE_SIZE) memset(dst + got, 0xFF, FLASH_PAGE_SIZE - got);
    return true;
}

/* Opens the flat .bin and allocates as many 4KB slots as fit (each its own small
 * safe_malloc), leaving a heap reserve. If every page gets a slot the image is
 * fully resident (pre-loaded, slot i == page i, no runtime SD). Returns true on
 * success (at least FLASH_MIN_SLOTS, or everything resident). */
static bool fc_open(FlashCache* fc, Storage* storage) {
    fc->num_pages = (fc->flash_used + FLASH_PAGE_SIZE - 1u) / FLASH_PAGE_SIZE;
    if(fc->num_pages > FLASH_MAX_PAGES) fc->num_pages = FLASH_MAX_PAGES;
    fc->use_counter = 0;
    fc->miss_count = 0;
    fc->num_slots = 0;
    fc->fully_resident = false;
    fc->last_page = 0xFFFFFFFFu;
    fc->last_ptr = NULL;
    fc->last_page2 = 0xFFFFFFFFu;
    fc->last_ptr2 = NULL;
    for(uint32_t i = 0; i < FLASH_MAX_PAGES; i++) fc->page_slot[i] = FC_SLOT_NONE;

    fc->file = storage_file_alloc(storage);
    if(!storage_file_open(fc->file, PP_FLASH_BIN, FSAM_READ, FSOM_OPEN_EXISTING)) {
        FURI_LOG_E(TAG, "flash cache: cannot open %s", PP_FLASH_BIN);
        return false;
    }

    /* Allocate as many slots as fit, degrading the heap reserve if needed. The
     * PIC18 runs from the cache, so fully resident is strongly preferred. */
    static const size_t reserve_ladder[3] = {FLASH_HEAP_RESERVE, 8u * 1024u, 6u * 1024u};
    for(int step = 0; step < 3; step++) {
        size_t reserve = reserve_ladder[step];
        while(fc->num_slots < fc->num_pages) {
            if(memmgr_get_free_heap() < reserve + FLASH_PAGE_SIZE + ALLOC_MARGIN) break;
            uint8_t* slot = (uint8_t*)safe_malloc(FLASH_PAGE_SIZE);
            if(!slot) break;
            fc->slots[fc->num_slots] = slot;
            fc->slot_page[fc->num_slots] = 0xFFFFFFFFu;
            fc->slot_use[fc->num_slots] = 0;
            fc->num_slots++;
        }
        if(fc->num_slots >= fc->num_pages) break; /* already everything */
        if(step + 1 < 3)
            FURI_LOG_W(TAG, "flash cache: degrading heap reserve to %uK for more slots",
                       (unsigned)(reserve_ladder[step + 1] / 1024u));
    }

    if(fc->num_slots < FLASH_MIN_SLOTS && fc->num_slots < fc->num_pages) {
        FURI_LOG_E(TAG, "flash cache: only %u slots (need >= %u)", fc->num_slots,
                   (unsigned)FLASH_MIN_SLOTS);
        return false;
    }

    if(fc->num_slots >= fc->num_pages) {
        /* fully resident: pre-load every page, slot i == page i, no eviction */
        for(uint32_t i = 0; i < fc->num_pages; i++) {
            if(!fc_read_page(fc, i, fc->slots[i])) return false;
            fc->slot_page[i] = i;
            fc->slot_use[i] = ++fc->use_counter;
        }
        fc->fully_resident = true;
        storage_file_close(fc->file);
        storage_file_free(fc->file);
        fc->file = NULL;
        FURI_LOG_I(TAG, "flash cache: fully resident (%lu pages x 4KB = %luKB)",
                   (unsigned long)fc->num_pages,
                   (unsigned long)(fc->num_pages * FLASH_PAGE_SIZE / 1024u));
    } else {
        /* streaming LRU: warm the first num_slots pages (low flash = boot code) */
        for(uint16_t i = 0; i < fc->num_slots; i++) {
            if(!fc_read_page(fc, i, fc->slots[i])) return false;
            fc->slot_page[i] = i;
            fc->slot_use[i] = ++fc->use_counter;
            fc->page_slot[i] = i; /* page i lives in slot i after warm-up */
        }
        FURI_LOG_W(TAG,
                   "flash cache: STREAMING LRU (%u slots / %lu pages) -- SD reads on "
                   "miss, EXPECT SLOWDOWN (PIC18 fetches from flash)",
                   fc->num_slots, (unsigned long)fc->num_pages);
    }
    return true;
}

static void fc_free(FlashCache* fc) {
    for(uint16_t i = 0; i < fc->num_slots; i++) {
        if(fc->slots[i]) {
            free(fc->slots[i]);
            fc->slots[i] = NULL;
        }
    }
    if(fc->file) {
        storage_file_close(fc->file);
        storage_file_free(fc->file);
        fc->file = NULL;
    }
    fc->num_slots = 0;
    fc->last_page = 0xFFFFFFFFu;
    fc->last_ptr = NULL;
    fc->last_page2 = 0xFFFFFFFFu;
    fc->last_ptr2 = NULL;
}

/* Streams `page` into the LRU victim slot and returns its pointer. Plain LRU
 * (oldest slot_use). The core copies the byte out immediately (it never holds a
 * page pointer across instructions), so any slot is a safe victim. Maintains the
 * O(1) page->slot index: the evicted page is cleared, the new page points here. */
static uint8_t* fc_fill(FlashCache* fc, uint32_t page) {
    uint16_t lru = 0;
    uint32_t lru_use = 0xFFFFFFFFu;
    for(uint16_t i = 0; i < fc->num_slots; i++) {
        if(fc->slot_use[i] < lru_use) {
            lru_use = fc->slot_use[i];
            lru = i;
        }
    }
    uint32_t evicted = fc->slot_page[lru];
    if(evicted != 0xFFFFFFFFu && evicted < fc->num_pages) fc->page_slot[evicted] = FC_SLOT_NONE;
    /* Invalidate any fast-path entry that still points at this slot: its page is
     * about to change, so the cached (page,ptr) pair would otherwise return stale
     * bytes for the evicted page. (Correctness guard for the streaming path; in
     * fully_resident mode fc_fill is never called.) */
    if(fc->last_ptr == fc->slots[lru]) {
        fc->last_page = 0xFFFFFFFFu;
        fc->last_ptr = NULL;
    }
    if(fc->last_ptr2 == fc->slots[lru]) {
        fc->last_page2 = 0xFFFFFFFFu;
        fc->last_ptr2 = NULL;
    }
    if(!fc_read_page(fc, page, fc->slots[lru])) {
        memset(fc->slots[lru], 0xFF, FLASH_PAGE_SIZE); /* IO fault: blank flash */
    }
    fc->slot_page[lru] = page;
    fc->slot_use[lru] = ++fc->use_counter;
    if(page < fc->num_pages) fc->page_slot[page] = lru;
    return fc->slots[lru];
}

/* Returns a pointer to the 4KB slot holding `page`, loading it on a miss. O(1)
 * when fully resident (slot i == page i) AND O(1) in streaming mode via the
 * page->slot reverse index (no linear scan); only a true miss touches the SD. */
static uint8_t* fc_page_ptr(FlashCache* fc, uint32_t page) {
    if(fc->fully_resident) return fc->slots[page]; /* slot i == page i */
    uint16_t slot = (page < fc->num_pages) ? fc->page_slot[page] : FC_SLOT_NONE;
    if(slot != FC_SLOT_NONE) {
        fc->slot_use[slot] = ++fc->use_counter;
        return fc->slots[slot];
    }
    return fc_fill(fc, page);
}

/* Reads a byte by absolute program address through the cache. Bytes at/above
 * flash_used read as 0xFF (blank flash) without touching the cache. A 2-entry
 * last-page fast path keeps the per-instruction fetch cheap: one slot tracks the
 * sequential CODE page, the other the TBLRD/table DATA page, so the two fetch
 * kinds alternating does not thrash a single-entry cache. */
static uint8_t fc_read_addr(FlashCache* fc, uint32_t addr) {
    if(addr >= fc->flash_used) return 0xFF;
    uint32_t page = addr >> 12; /* / FLASH_PAGE_SIZE */
    uint32_t poff = addr & (FLASH_PAGE_SIZE - 1u);
    if(page == fc->last_page && fc->last_ptr) return fc->last_ptr[poff];
    if(page == fc->last_page2 && fc->last_ptr2) {
        /* promote the 2nd entry to primary (keep the hottest page in slot 1) */
        uint32_t tp = fc->last_page;
        uint8_t* tptr = fc->last_ptr;
        fc->last_page = fc->last_page2;
        fc->last_ptr = fc->last_ptr2;
        fc->last_page2 = tp;
        fc->last_ptr2 = tptr;
        return fc->last_ptr[poff];
    }
    uint8_t* p = fc_page_ptr(fc, page);
    /* demote the current primary into the 2nd entry, install the new primary */
    fc->last_page2 = fc->last_page;
    fc->last_ptr2 = fc->last_ptr;
    fc->last_page = page;
    fc->last_ptr = p;
    return p[poff];
}

/* Core program-read callback: resolve the fetch/TBLRD against the paged cache. */
static uint8_t fc_core_read(Pic18Cpu* cpu, uint32_t a, void* ctx) {
    UNUSED(cpu);
    return fc_read_addr((FlashCache*)ctx, a);
}

/* ------------------------------------------------------------ HW profiles */

/* RF model: the DXL-5000 siblings (mario2/mariofull) bit-bang an OOK waveform on
 * a single LAT pin; the Pandora D010/D174 ("super_mario") uses a Si4432 over
 * SW-SPI in FIFO/packet mode (TX-only). See RE_super_mario.md §9. */
typedef enum {
    RfModeOok, /* mario2/mariofull: OOK bit-bang on one LAT pin */
    RfModeSi4432Fifo, /* super_mario/D010: Si4432 SPI capture + replay (TX pure) */
} RfMode;

typedef struct {
    const char* name;
    uint32_t send_byte_pc; /* hook of the SW-SPI serializer */
    uint16_t byte_reg; /* bank4 register with the already-assembled byte */
    uint16_t dc_flag_a; /* bank1 flags: nonzero => COMMAND, zero => DATA */
    uint16_t dc_flag_b;
    bool has_pin; /* mariofull has PIN 2552 */
    /* --- boot: the XC8 runtime has a cinit that must be handled ---
     * 0 = run-to (mariofull, PORTB.1 handshake)
     * 1 = cinit records (mario2)
     * 2 = free-run (super_mario/D010: no handshake, delay-skip + selfclear) */
    uint8_t boot_mode;
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

    /* --- RF --- */
    RfMode rf_mode;
    /* OOK (RfModeOok): output pin (LAT) */
    uint16_t ook_tx_lat; /* absolute LAT SFR of the OOK TX pin */
    uint8_t ook_tx_bit; /* bit of the OOK TX pin */
    /* Si4432 SW-SPI pins (RfModeSi4432Fifo), see RE_super_mario.md §9.1/9.2 */
    uint16_t si_cs_lat; /* NSEL/CS (active-low), e.g. LATB.6 */
    uint8_t si_cs_bit;
    uint16_t si_sck_lat; /* SCLK, e.g. LATC.3 */
    uint8_t si_sck_bit;
    uint16_t si_sdi_lat; /* SDI/MOSI, e.g. LATB.5 */
    uint8_t si_sdi_bit;
    uint16_t si_sdn_lat; /* SDN shutdown (active-high), e.g. LATB.7 */
    uint8_t si_sdn_bit;

    /* --- super_mario (D010) display + boot helpers --- */
    uint16_t data_reg; /* bank2 byte the caller leaves for the wrapper */
    uint32_t disp_init; /* SSD1306 init routine (emits D5 80 A8 1F ...) */
    uint32_t disp_bringup[3]; /* reset / contrast setup / init (force-call order) */
    uint32_t main_render; /* main screen render routine */
    uint32_t delay_entry; /* SW delay loop entry to neutralise (0 = none) */
    uint32_t delay_ret; /* RETURN address the skip jumps to */
    uint16_t delay_outer; /* fixed-cell outer counter to zero (delay_fsr==false) */
    bool delay_fsr; /* true: outer counter is addressed via FSR0/INDF0 */
    bool selfclear_sfr; /* true: clear EECON1 WR/RD + ADCON0 GO/DONE each step */
    bool force_render; /* true: FALLBACK force-call of bringup+main_render if the
                        * auto-run (IRQ+free-run) draws nothing (super_mario only) */

    /* --- buttons ---
     * The button register and bits are per firmware VERSION and were confirmed
     * DYNAMICALLY (emu/re_btn_siblings.py, emu/re_sm_btnprobe.py):
     *   - mario2 / mariofull (DXL-5000 siblings): read 5 buttons from LATE (0xF8E)
     *     bits 1,2,4,6,7 (BTFSC/BTFSS LATE,n in the UI dispatch). btn_is_lat=true.
     *   - super_mario (Pandora D010/D174): reads 3 buttons from PORTB (0xF81) bits
     *     1,2,4 (BTFSC PORTB,n handlers 0x00CC60/0x00C54C/0x00C1D4). btn_is_lat=false.
     * btn_is_lat matters on the Flipper because the PIC18 core reads LAT registers
     * straight from ram[] (no on_port_read callback): LATE buttons must be injected
     * by WRITING ram[btn_reg], while PORTB buttons go through port_read_hook. */
    uint16_t btn_reg; /* register the firmware polls (0xF8E LATE siblings / 0xF81 PORTB D010) */
    bool btn_is_lat; /* true: btn_reg is a LAT -> inject via ram[] write (siblings) */
    uint8_t btn_bit_up; /* Flipper Up    -> this bit (active-low) */
    uint8_t btn_bit_ok; /* Flipper Ok    -> this bit */
    uint8_t btn_bit_menu; /* Flipper Down  -> this bit */
    uint8_t btn_bit_left; /* Flipper Left  -> this bit (0xFF = unused) */
    uint8_t btn_bit_right; /* Flipper Right -> this bit (0xFF = unused) */

    /* --- interrupt model (push the firmware out of the idle busy-wait) --- */
    bool irq_enable; /* enable the core interrupt model after boot */
    uint32_t irq_period; /* instructions per timer tick */
    uint16_t irq_sem_addr; /* frame-tick semaphore (RAM addr), 0 = none */
    uint8_t irq_sem_bit;
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
    .rf_mode = RfModeOok,
    .ook_tx_lat = 0xF8Bu, /* LATB */
    .ook_tx_bit = 0u, /* LATB.0 */
    .btn_reg = 0xF8Eu, /* 5 buttons on LATE 0xF8E (bits 1,2,4,6,7) confirmed dyn. */
    .btn_is_lat = true, /* LATE: inject via ram[] (core reads LAT from ram) */
    .btn_bit_up = 1u, /* B1 = LATE.1 */
    .btn_bit_ok = 2u, /* B2 = LATE.2 */
    .btn_bit_menu = 4u, /* B3 = LATE.4 */
    .btn_bit_left = 6u, /* B5 = LATE.6 */
    .btn_bit_right = 7u, /* B6 = LATE.7 */
    /* AUTO-ANIMATE (like PandoraARM): after boot, run the firmware freely with the
     * interrupt model + SW-delay-skip so it draws its REAL screen on its own,
     * instead of force-calling the render. The delay neutralisation is MANDATORY
     * for usable speed (verified in emu/pic18_tui.py: without it the firmware
     * stays in the DECFSZ delay and nz stays 0; with it the firmware draws the
     * real main screen, nz=69/512). delay_cfg from PROFILES['mariofull']:
     * entry 0x001704, outer cell 0x0505, RETURN 0x00172A. Zeroing the OUTER cell
     * is the operation that matters (jump-only does nothing). */
    .delay_entry = 0x001704u,
    .delay_ret = 0x00172Au,
    .delay_outer = 0x0505u,
    .delay_fsr = false,
    .force_render = false, /* auto-run path (force_render kept as diagnostics only) */
    .irq_enable = true,
    .irq_period = 3000u,
    .irq_sem_addr = 0x060u, /* frame-tick semaphore (idle busy-wait flag) */
    .irq_sem_bit = 0u,
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
    .rf_mode = RfModeOok,
    .ook_tx_lat = 0xF8Bu, /* LATB */
    .ook_tx_bit = 1u, /* LATB.1 */
    .btn_reg = 0xF8Eu, /* 5 buttons on LATE 0xF8E (bits 1,2,4,6,7) confirmed dyn. */
    .btn_is_lat = true, /* LATE: inject via ram[] (core reads LAT from ram) */
    .btn_bit_up = 1u, /* B1 = LATE.1 */
    .btn_bit_ok = 2u, /* B2 = LATE.2 */
    .btn_bit_menu = 4u, /* B3 = LATE.4 */
    .btn_bit_left = 6u, /* B5 = LATE.6 */
    .btn_bit_right = 7u, /* B6 = LATE.7 */
    /* AUTO-ANIMATE (like PandoraARM): auto-run with IRQ + SW-delay-skip so the
     * firmware draws its own real main screen. delay_cfg from PROFILES['mario2']:
     * entry 0x00170E, outer cell 0x04F6, RETURN 0x001736 (verified: nz=69/512). */
    .delay_entry = 0x00170Eu,
    .delay_ret = 0x001736u,
    .delay_outer = 0x04F6u,
    .delay_fsr = false,
    .force_render = false, /* auto-run path (force_render kept as diagnostics only) */
    .irq_enable = true,
    .irq_period = 3000u,
    .irq_sem_addr = 0x060u, /* frame-tick semaphore (idle busy-wait flag) */
    .irq_sem_bit = 0u,
};

/* Pandora D010/D174 "MaRiO" DIY firmware (Super_mario.hex, ~52KB flash, reset
 * GOTO 0x00D052). Addresses reused from emu/pic18_tui.py PROFILES['super_mario']
 * and RE_super_mario.md:
 *   - DISPLAY: SSD1306 128x32 (same controller as siblings). send-byte 0x0013F2,
 *     byte_reg bank2 0x2F9, data_reg 0x2F8, DC flags bank1 (0x13D,0x13E), init
 *     0x00148C, bring-up (0x00138E,0x00147C,0x00148C), main render 0x003B52.
 *   - BOOT: no PORTB handshake; free-run with delay-skip (0x001364 -> RETURN
 *     0x00138C, outer counter via FSR0/INDF0) and self-clearing busy bits
 *     (EECON1 WR/RD, ADCON0 GO/DONE).
 *   - BUTTONS: 3 on PORTB (0xF81), active-low: B1=PORTB.1, B2=PORTB.2, B4=PORTB.4.
 *   - RF: Si4432 SW-SPI, TX-only FIFO. CS=LATB.6, SCLK=LATC.3, SDI=LATB.5,
 *     SDN=LATB.7, (SDO=PORTC.4, NIRQ=PORTD.2). TX triggers 0x34 xx / 0x15 xx. */
static const PicProfile PROFILE_SUPER_MARIO = {
    .name = "super_mario",
    .send_byte_pc = 0x0013F2u,
    .byte_reg = 0x2F9u,
    .dc_flag_a = 0x13Du,
    .dc_flag_b = 0x13Eu,
    .has_pin = false,
    .boot_mode = 2,
    .portb1_handshake = false,
    .data_reg = 0x2F8u,
    .disp_init = 0x00148Cu,
    .disp_bringup = {0x00138Eu, 0x00147Cu, 0x00148Cu},
    .main_render = 0x003B52u,
    .delay_entry = 0x001364u,
    .delay_ret = 0x00138Cu,
    .delay_fsr = true,
    .selfclear_sfr = true,
    /* super_mario: force_render is a FALLBACK only. The primary path is auto-run
     * with IRQ (irq_enable below), like the siblings. HONEST LIMITATION (confirmed
     * in the Python reference emu/pic18_tui.py and RE_super_mario.md §4): this
     * DIY D010 firmware never reaches its display routines autonomously on a cold
     * boot (send-byte is never called; the refresh is gated by battery/mode/RTC
     * state not modelled). So the auto-run draws nothing (nz=0) and the main loop
     * FALLS BACK to force_render, which force-calls the bring-up + main render and
     * draws the real status screen (nz=160/512). The siblings DO auto-animate; for
     * this profile force_render remains the reliable path. */
    .force_render = true,
    .rf_mode = RfModeSi4432Fifo,
    .si_cs_lat = 0xF8Bu, /* LATB.6 */
    .si_cs_bit = 6u,
    .si_sck_lat = 0xF8Cu, /* LATC.3 */
    .si_sck_bit = 3u,
    .si_sdi_lat = 0xF8Bu, /* LATB.5 */
    .si_sdi_bit = 5u,
    .si_sdn_lat = 0xF8Bu, /* LATB.7 */
    .si_sdn_bit = 7u,
    .btn_reg = 0xF81u, /* PORTB (3 buttons, active-low) confirmed dynamically */
    .btn_is_lat = false, /* PORTB: injected via port_read_hook (on_port_read) */
    .btn_bit_up = 1u, /* B1 = PORTB.1 */
    .btn_bit_ok = 2u, /* B2 = PORTB.2 */
    .btn_bit_menu = 4u, /* B4 = PORTB.4 */
    .btn_bit_left = 0xFFu, /* unused (only 3 buttons) */
    .btn_bit_right = 0xFFu, /* unused */
    .irq_enable = true,
    .irq_period = 3000u,
    .irq_sem_addr = 0x060u, /* frame-tick semaphore (idle busy-wait flag) */
    .irq_sem_bit = 0u,
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

/* Buttons are PROFILE-driven: the DXL-5000 siblings poll 5 buttons at LATE
 * (0xF8E); the Pandora D010 (super_mario) polls 3 buttons at PORTB (0xF81). The
 * mapping (which bit each Flipper key drives) lives in the active PicProfile. We
 * keep a logical "btn_mask" of the pressed bits and expose it on btn_reg via
 * port_read_hook (active-low: pressed => 0). */
#define BTN_BIT_UNUSED 0xFFu

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
    uint8_t* prog; /* legacy single buffer (NULL when the paged cache is used) */
    FlashCache fc; /* paged flash cache (program memory) */
    const PicProfile* prof;

    /* reconstructed SSD1306 GDDRAM.
     * DOUBLE-BUFFERED (like PandoraARM's oled_work/oled_fb) so the auto-run render
     * never shows a half-drawn/blank frame (no flicker):
     *   - gddram      = WORK buffer, written byte-by-byte by gddram_feed as the
     *                   firmware flushes its pages.
     *   - gddram_vis  = VISIBLE buffer, the ONLY thing render_gddram reads.
     * The work buffer is committed to the visible one atomically by oled_commit()
     * when a flush pass is COMPLETE (new SET PAGE 0 begins, or the firmware went
     * idle with a stable non-blank work buffer). Blank frames are never committed
     * (they are the internal clear before the real content is painted). */
    uint8_t gddram[GDDRAM_SIZE]; /* WORK buffer */
    uint8_t gddram_vis[GDDRAM_SIZE]; /* VISIBLE buffer (render reads this) */
    uint8_t gddram_prev[GDDRAM_SIZE]; /* last work snapshot (idle-stability detect) */
    bool gd_work_dirty; /* data written since the last commit */
    uint32_t gd_frames; /* commits performed (diagnostic) */
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

    /* --- Si4432 SPI bridge (super_mario / D010; RfModeSi4432Fifo) ---
     * We watch the firmware bit-bang the Si4432 over SW-SPI (CS/SCLK/SDI) and
     * reconstruct the byte stream it writes (the block protocol
     * [0x11][bank][len][off][payload] + triggers). On the TX trigger (reg 0x34 /
     * 0x15) we convert the captured FIFO payload (bank 0x21) to OOK edges and
     * replay them on the CC1101. The D010 is a PURE transmitter: no RX path. */
    bool si_cs_active; /* CS asserted (active-low) */
    bool si_prev_sck; /* previous SCLK level (edge detect) */
    uint8_t si_shift; /* bits shifted in for the current byte (MSB-first) */
    uint8_t si_bitcnt; /* bit count 0..8 */
    uint8_t si_frame[64]; /* current CS-low frame bytes */
    uint8_t si_frame_len; /* bytes captured in the current frame */
    uint8_t si_payload[64]; /* last captured FIFO payload (bank 0x21) */
    uint8_t si_payload_len;
    bool si_tx_pending; /* a TX trigger (0x34/0x15) was seen; replay the payload */
    uint32_t si_spi_bytes; /* diagnostics: total SPI bytes captured */
    uint32_t si_frames; /* diagnostics: total SPI frames captured */

    /* --- diagnostics / heartbeat --- */
    uint64_t hb_steps; /* total firmware steps executed (for insn/s rate) */
    uint32_t dbg_last_nz; /* last logged framebuffer nonzero count */
    bool booted; /* boot completed */
    bool render_forced; /* super_mario: force-render already done */

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

/* Returns the number of non-zero bytes in a GDDRAM buffer (blank-frame test). */
static uint32_t gddram_nonzero(const uint8_t* buf) {
    uint32_t nz = 0;
    for(uint32_t i = 0; i < GDDRAM_SIZE; i++)
        if(buf[i]) nz++;
    return nz;
}

/* Atomic double-buffer commit: copy the WORK buffer to the VISIBLE buffer so
 * render_gddram shows a COMPLETE frame (port of PandoraARM oled_commit). Skips
 * the commit when the work buffer is blank (that is the internal clear that
 * precedes the real content; committing it would flash a black frame). Called on
 * a flush-pass boundary. Mutex-protected so the fb callback never sees a torn
 * visible buffer. */
static void oled_commit(AppState* app) {
    if(!app->gd_work_dirty) return;
    if(gddram_nonzero(app->gddram) == 0) return; /* blank clear: do not show */
    if(app->fb_mutex) furi_mutex_acquire(app->fb_mutex, FuriWaitForever);
    memcpy(app->gddram_vis, app->gddram, GDDRAM_SIZE);
    if(app->fb_mutex) furi_mutex_release(app->fb_mutex);
    app->gd_work_dirty = false;
    app->gd_frames++;
}

/* Decodes a byte sent to the SSD1306 (robust command/data discriminator, port of
 * emu/pic18_tui.py SSD1306GDDRAM.feed). The physical DC pin is useless (the
 * wrapper lowers it before every byte); the reliable signal is the firmware's
 * dc_flag (f6): nonzero => COMMAND. SET PAGE (0xB0-0xB7) is the one command the
 * firmware emits with f6=0, so it is special-cased. Commands with arguments
 * consume the next N bytes as args (not pixels). This is identical for all three
 * firmwares (same SSD1306 128x32 controller; see RE_super_mario.md). */
static void gddram_feed(AppState* app, uint8_t b, bool f6_nonzero) {
    /* 1) pending argument of an open multi-byte command -> consume, never pixel */
    if(app->gd_cmd_arg_left > 0) {
        app->gd_cmd_arg_left--;
        if(b == 0x21) {
            /* handled below only on opener; here it is an arg */
        }
        return;
    }
    /* 2) classify: command if the opener flag is set, or it's a SET PAGE. */
    bool is_cmd = f6_nonzero || (b >= 0xB0 && b <= 0xB7);
    if(is_cmd) {
        if(b >= 0xB0 && b <= 0xB7) {
            /* set page; on SET PAGE the firmware assumes col already 0 (RE §8.1).
             * A SET PAGE 0 (0xB0) starts a NEW flush pass -> the previous pass is
             * complete, so commit the work buffer to the visible one (double
             * buffer; blank frames are skipped inside oled_commit). */
            if(b == 0xB0) oled_commit(app);
            app->gd_page = b - 0xB0;
            app->gd_col = 0;
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
            b == 0xDB || b == 0x8D || b == 0x23) {
            app->gd_cmd_arg_left = 1; /* 1-argument commands */
        }
        /* 1-byte commands (0x40,0xA1,0xC8,0xA4,0xA6,0xAF,0xAE...) do not move ptr */
    } else {
        /* data: 8 vertical pixels of (page,col), bit0 at top */
        if(app->gd_page < GDDRAM_PAGES && app->gd_col < GDDRAM_COLS) {
            app->gddram[app->gd_page * GDDRAM_COLS + app->gd_col] = b;
            app->gd_work_dirty = true;
        }
        app->gd_col++;
        if(app->gd_col >= GDDRAM_COLS) {
            app->gd_col = 0;
            app->gd_page = (app->gd_page + 1) % GDDRAM_PAGES;
        }
    }
}

/* SFR addresses handled by the super_mario boot helpers. EECON1/ADCON0 are plain
 * RAM SFRs (no special write branch), so the firmware's busy-waits on their WR/
 * RD / GO-DONE bits never complete unless we model the silicon "done". */
#define SFR_EECON1 0xFA6u
#define SFR_ADCON0 0xFC2u

/* Code hook (fires on EVERY instruction; keep it cheap). Responsibilities:
 *  - send-byte serializer: capture the byte the firmware "draws" into the GDDRAM.
 *  - super_mario boot helpers: self-clear the Si4432/EEPROM/ADC busy bits and
 *    neutralise the software display delay (both ports of emu/pic18_tui.py). */
static void code_hook(Pic18Cpu* cpu, uint32_t pc, void* ctx) {
    AppState* app = (AppState*)ctx;
    const PicProfile* prof = app->prof;

    /* display capture (all profiles) */
    if(pc == prof->send_byte_pc) {
        uint8_t b = pic18_read_ram(cpu, prof->byte_reg);
        uint8_t fa = pic18_read_ram(cpu, prof->dc_flag_a);
        uint8_t fb = pic18_read_ram(cpu, prof->dc_flag_b);
        bool f6 = (fa != 0) || (fb != 0); /* nonzero => COMMAND opener */
        gddram_feed(app, b, f6);
        return;
    }

    /* Neutralise the SW display delay (nested DECFSZ loop). This is MANDATORY for
     * the firmware to advance at usable speed so it reaches its render chain (port
     * of emu/pic18_tui.py _install_delay_skip). Two variants:
     *   - delay_fsr (super_mario): the outer counter is addressed indirectly via
     *     FSR0/INDF0 -> zero *FSR0.
     *   - fixed-cell (mario2/mariofull): the outer counter is a FIXED RAM cell
     *     (delay_outer) -> zero it. (Verified: zeroing the outer cell is the
     *     operation that matters; jumping to RETURN alone leaves nz=0.)
     * In both cases we then jump to the loop's RETURN. */
    if(prof->delay_entry && pc == prof->delay_entry) {
        if(prof->delay_fsr) {
            uint16_t fsr0 = pic18_fsr(cpu, 0);
            pic18_write_ram(cpu, fsr0, 0);
        } else {
            pic18_write_ram(cpu, prof->delay_outer, 0); /* zero the fixed outer cell */
        }
        cpu->pc = prof->delay_ret;
        return;
    }

    /* super_mario: self-clear the HW busy bits the real silicon clears on
     * completion (EECON1 WR/RD, ADCON0 GO/DONE). Cheap: just mask them each step
     * so the boot busy-waits fall through. */
    if(prof->selfclear_sfr) {
        uint8_t e = pic18_read_ram(cpu, SFR_EECON1);
        if(e & 0x03) pic18_write_ram(cpu, SFR_EECON1, (uint8_t)(e & ~0x03u));
        uint8_t a = pic18_read_ram(cpu, SFR_ADCON0);
        if(a & 0x02) pic18_write_ram(cpu, SFR_ADCON0, (uint8_t)(a & ~0x02u));
    }
}

/* ----------------------------------------------------- buttons (PORT read) */

/* This hook is called by the core ONLY for PORTA..PORTE reads (SFRK_PORT). The
 * super_mario (D010) buttons live on PORTB (0xF81) and are served here. The
 * DXL-5000 siblings read their buttons from LATE (0xF8E), which the core reads
 * straight from ram[] and never routes through here -- those are injected by
 * apply_keys_to_buttons (ram[] write). active-low: pressed bits read as 0.
 * mariofull also needs the PORTB.1 boot handshake. For the RX input pin (if the
 * RX bridge is active) we inject the current level. */
static int port_read_hook(Pic18Cpu* cpu, uint16_t addr, void* ctx) {
    AppState* app = (AppState*)ctx;
    const PicProfile* prof = app->prof;
    if(prof && !prof->btn_is_lat && addr == prof->btn_reg) {
        /* active-low: pressed bits (btn_mask) read as 0, the rest as 1. The D010
         * shares PORTB.2 physically with the display DATA line, but it reads it
         * as an input here; btn_mask only sets real button bits. */
        uint8_t v = (uint8_t)(0xFFu & ~app->btn_mask);
        /* if the RX bridge injects on this same register, overlay its level */
        if(app->rf_rx_enabled && app->rx_in_port == addr) {
            if(app->rx_cur_level)
                v |= (uint8_t)(1u << app->rx_in_bit);
            else
                v &= (uint8_t)~(1u << app->rx_in_bit);
        }
        return (int)v;
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
    /* super_mario Si4432: NIRQ on PORTD.2 (0xF83.2, active-low) -> return READY
     * (bit low) so the chip-ready / packet-sent busy-waits (BTFSC PORTD,2;BRA at
     * 0x003E42) complete. SDO on PORTC.4 (0xF82.4) -> return 0 (no MISO data; the
     * D010 is a pure transmitter so the firmware never needs a real read). */
    if(prof && prof->rf_mode == RfModeSi4432Fifo) {
        if(addr == 0xF83u) { /* PORTD: NIRQ bit2 low = ready */
            uint8_t v = pic18_read_ram(cpu, addr);
            v &= (uint8_t)~(1u << 2);
            return (int)v;
        }
        if(addr == 0xF82u) { /* PORTC: SDO bit4 = 0 */
            uint8_t v = pic18_read_ram(cpu, addr);
            v &= (uint8_t)~(1u << 4);
            return (int)v;
        }
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

/* --- OOK bit-bang capture (mario2/mariofull) --- */
/* Detects the toggles of the firmware's OOK pin and accumulates the edges with
 * their duration (measured by cpu->cycles between toggles) in the TX ring. */
static void ook_capture(Pic18Cpu* cpu, AppState* app, uint8_t val) {
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

/* --- Si4432 SW-SPI capture (super_mario / D010) ---
 * The firmware bit-bangs the Si4432 over SW-SPI: CS=LATB.6 (active-low, held low
 * for the whole frame), SCLK=LATC.3 (idle low, sample on rising edge), SDI=LATB.5
 * (MSB first). We reconstruct the byte stream per CS-low frame. When a frame is a
 * FIFO payload write (block protocol [0x11][0x21][len][off][payload...], see
 * RE_super_mario.md §9.4), we keep the payload. When a TX trigger frame is seen
 * (reg 0x34 or 0x15 written), we flag si_tx_pending so the main loop converts the
 * payload to OOK edges and replays it on the CC1101. D010 is TX-only: no RX. */
static void si4432_frame_done(AppState* app) {
    if(app->si_frame_len == 0) return;
    app->si_frames++;
    uint8_t reg = app->si_frame[0];
    /* Block protocol: [0x11][bank][len][off][payload...]. Bank 0x21 = FIFO.
     * The firmware sends the FIFO payload in several chunks, each with its byte
     * OFFSET (si_frame[3]); we place them by offset so the full payload is
     * reconstructed (RE_super_mario.md §9.4). */
    if(reg == 0x11 && app->si_frame_len >= 4 && app->si_frame[1] == 0x21) {
        uint8_t len = app->si_frame[2];
        uint8_t off = app->si_frame[3];
        uint8_t n = (uint8_t)(app->si_frame_len - 4);
        if(len < n) n = len; /* trust the declared length */
        for(uint8_t i = 0; i < n; i++) {
            uint16_t dst = (uint16_t)(off + i);
            if(dst < sizeof(app->si_payload)) {
                app->si_payload[dst] = app->si_frame[4 + i];
                if(dst + 1 > app->si_payload_len) app->si_payload_len = (uint8_t)(dst + 1);
            }
        }
        FURI_LOG_D(TAG, "si4432: FIFO chunk off=%u len=%u (payload now %u bytes)",
                   (unsigned)off, (unsigned)n, (unsigned)app->si_payload_len);
    } else if(reg == 0x34 || reg == 0x15) {
        /* TX trigger (0x34 xx / 0x15 xx) -> start transmitting the payload. */
        app->si_tx_pending = true;
        FURI_LOG_I(TAG, "si4432: TX trigger reg=0x%02X -> replay %u-byte payload",
                   (unsigned)reg, (unsigned)app->si_payload_len);
    }
    app->si_frame_len = 0;
}

static void si4432_capture(AppState* app, uint16_t addr, uint8_t val) {
    const PicProfile* prof = app->prof;
    /* SDN pulse (optional log): 1->0 wakes the chip */
    if(addr == prof->si_sdn_lat) {
        /* no action needed; the chip model is always "awake" for replay */
    }
    /* CS (NSEL) edge: active-low. Falling = frame start, rising = frame end. */
    if(addr == prof->si_cs_lat) {
        bool cs_high = (val >> prof->si_cs_bit) & 1u;
        bool cs_active = !cs_high; /* active-low */
        if(cs_active && !app->si_cs_active) {
            /* frame start */
            app->si_frame_len = 0;
            app->si_bitcnt = 0;
            app->si_shift = 0;
        } else if(!cs_active && app->si_cs_active) {
            /* frame end: flush any partial byte is discarded; decode frame */
            si4432_frame_done(app);
        }
        app->si_cs_active = cs_active;
    }
    /* SCLK edge: sample SDI on the rising edge (idle-low clock). SDI and SCLK may
     * live on different LAT registers; read SDI from the core RAM (last LAT). */
    if(addr == prof->si_sck_lat && app->si_cs_active) {
        bool sck = (val >> prof->si_sck_bit) & 1u;
        if(sck && !app->si_prev_sck) {
            /* rising edge: shift in the current SDI bit (MSB first) */
            uint8_t sdi_lat = (uint8_t)pic18_read_ram(app->cpu, prof->si_sdi_lat);
            uint8_t bit = (sdi_lat >> prof->si_sdi_bit) & 1u;
            app->si_shift = (uint8_t)((app->si_shift << 1) | bit);
            app->si_bitcnt++;
            if(app->si_bitcnt == 8) {
                if(app->si_frame_len < sizeof(app->si_frame))
                    app->si_frame[app->si_frame_len++] = app->si_shift;
                app->si_spi_bytes++;
                app->si_bitcnt = 0;
                app->si_shift = 0;
            }
        }
        app->si_prev_sck = sck;
    }
}

/* LAT write hook: dispatches to the OOK capture (siblings) or the Si4432 SPI
 * capture (super_mario). For super_mario the SPI is always captured (not gated by
 * rf_tx_enabled) so the payload/trigger are ready; the replay to the CC1101 only
 * happens when the RF bridge is ON. */
static void lat_write_hook(Pic18Cpu* cpu, uint16_t addr, uint8_t val, void* ctx) {
    AppState* app = (AppState*)ctx;
    const PicProfile* prof = app->prof;
    if(!prof) return;

    if(prof->rf_mode == RfModeSi4432Fifo) {
        si4432_capture(app, addr, val);
        return;
    }
    /* OOK bit-bang (mario2/mariofull): only while the TX bridge captures */
    if(!app->rf_tx_enabled) return;
    if(addr != prof->ook_tx_lat) return;
    ook_capture(cpu, app, val);
}

/* Sets/clears a button bit in the logical mask. bit==BTN_BIT_UNUSED is a no-op
 * (profiles with fewer than 5 buttons leave Left/Right unmapped). */
static void set_button_bit(AppState* app, uint8_t bit, bool pressed) {
    if(bit == BTN_BIT_UNUSED) return;
    if(pressed)
        app->btn_mask |= (uint8_t)(1u << bit);
    else
        app->btn_mask &= (uint8_t)~(1u << bit);
}

/* ------------------------------------------------------------ rendering */

/* The GDDRAM is 128x32 (4 pages). The Flipper's screen is 128x64. We draw it
 * vertically centered (rows 16..47) at 1:1 scale, 1 pixel per pixel. screen[]
 * uses the Flipper page format: byte (x, page) with bit = row&7, bit=1 => light.
 * Reads the VISIBLE (double-buffered) GDDRAM, which oled_commit only updates on a
 * complete, non-blank flush pass -> no partial/blank frame is ever shown. */
static void render_gddram(AppState* app) {
    furi_mutex_acquire(app->fb_mutex, FuriWaitForever);
    memset(app->screen, 0, FB_SIZE);
    const int y_off = 16; /* center 32 rows in 64 */
    for(int page = 0; page < GDDRAM_PAGES; page++) {
        for(int col = 0; col < GDDRAM_COLS; col++) {
            uint8_t v = app->gddram_vis[page * GDDRAM_COLS + col];
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

/* Direct pin polling (avoids the input service, like FlipperGB). The Flipper keys
 * drive the keyfob buttons PER PROFILE (bits in prof->btn_*):
 *   siblings (5 btn @0xF8E): Up=B1 Ok=B2 Down=B3 Left=B5 Right=B6
 *   D010     (3 btn @PORTB): Up=B1(RB1) Ok=B2(RB2) Down=B4(RB4); Left/Right unused
 * Up+Down simultaneously = help overlay / RF toggle (a physically impossible
 * gesture). */
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
    const PicProfile* prof = app->prof;
    /* Up+Down together open the help overlay (and toggle the RF bridge). */
    bool combo_menu = (keys & (KBIT_UP | KBIT_DOWN)) == (KBIT_UP | KBIT_DOWN);
    if(combo_menu) {
        app->menu_requested = true;
        set_button_bit(app, prof->btn_bit_up, false);
        set_button_bit(app, prof->btn_bit_menu, false);
    } else {
        set_button_bit(app, prof->btn_bit_up, keys & KBIT_UP);
        set_button_bit(app, prof->btn_bit_menu, keys & KBIT_DOWN);
    }
    set_button_bit(app, prof->btn_bit_ok, keys & KBIT_OK);
    set_button_bit(app, prof->btn_bit_left, keys & KBIT_LEFT);
    set_button_bit(app, prof->btn_bit_right, keys & KBIT_RIGHT);

    /* LATE-based siblings: the PIC18 core reads LAT registers straight from ram[]
     * (no on_port_read callback), so we must INJECT the active-low button byte by
     * writing ram[btn_reg] here. (PORTB/super_mario goes through port_read_hook.)
     * active-low: pressed bits (btn_mask) read as 0, the rest as 1. */
    if(prof->btn_is_lat) {
        pic18_write_ram(app->cpu, prof->btn_reg, (uint8_t)(0xFFu & ~app->btn_mask));
    }
}

/* Logs button press/release transitions with the keyfob button name. Called from
 * the main loop so the user can see, on the console, exactly which keyfob button
 * each Flipper key drives (poll + named press, like PandoraARM). */
static void log_button_changes(AppState* app, uint8_t keys) {
    static uint8_t s_last = 0;
    if(keys == s_last) return;
    uint8_t changed = keys ^ s_last;
    struct {
        uint8_t kbit;
        const char* name;
    } map[] = {
        {KBIT_UP, "Up->B1"}, {KBIT_OK, "Ok->B2"},   {KBIT_DOWN, "Down->menu"},
        {KBIT_LEFT, "Left"}, {KBIT_RIGHT, "Right"}, {KBIT_BACK, "Back"},
    };
    for(size_t i = 0; i < sizeof(map) / sizeof(map[0]); i++) {
        if(changed & map[i].kbit) {
            FURI_LOG_D(
                TAG, "button %s %s", map[i].name, (keys & map[i].kbit) ? "DOWN" : "UP");
        }
    }
    FURI_LOG_D(TAG, "poll_raw_keys: 0x%02X (was 0x%02X) btn_mask=0x%02X", keys, s_last,
               app->btn_mask);
    s_last = keys;
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

/* Processes an Intel HEX line (without ':') writing the decoded PROGRAM bytes
 * into the open flat .bin `out` by absolute address (seek+write), and the config
 * words (0x300000+) into cpu->config_*. The flat file is pre-filled with 0xFF, so
 * records can arrive out of order. Returns 1 EOF, 0 continue, -1 error. */
static int pic_ihex_line(
    AppState* app, const char* ln, size_t len, File* out, uint32_t* ext_lin) {
    if(len < 10) return 0;
    int count = pic_hexbyte(ln);
    int ah = pic_hexbyte(ln + 2);
    int al = pic_hexbyte(ln + 4);
    int rtype = pic_hexbyte(ln + 6);
    if(count < 0 || ah < 0 || al < 0 || rtype < 0) return -1;
    uint32_t addr = ((uint32_t)ah << 8) | (uint32_t)al;
    const char* data = ln + 8;
    Pic18Cpu* cpu = app->cpu;
    if(rtype == 0x00) {
        uint32_t base = *ext_lin + addr;
        /* decode the program bytes into a local buffer, then one seek + one write */
        uint8_t buf[256];
        int n = 0;
        uint32_t wbase = base;
        bool have_w = false;
        for(int k = 0; k < count; k++) {
            int b = pic_hexbyte(data + k * 2);
            if(b < 0) return -1;
            uint32_t a = base + (uint32_t)k;
            if(a < PROG_MAX) {
                if(!have_w) {
                    wbase = a;
                    have_w = true;
                }
                if(n < (int)sizeof(buf)) buf[n++] = (uint8_t)b;
            } else if(a >= 0x300000u && cpu->config_count < 64) {
                /* config words: store in the core's table (not in the flat image) */
                cpu->config_addr[cpu->config_count] = a;
                cpu->config_val[cpu->config_count] = (uint8_t)b;
                cpu->config_count++;
            }
        }
        if(n > 0) {
            if(!storage_file_seek(out, wbase, true)) return -1;
            if(storage_file_write(out, buf, (size_t)n) != (size_t)n) return -1;
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

/* Pre-fills the open flat file with `size` bytes of 0xFF (blank-flash value of
 * gaps / unused cells). Done in chunks; returns false on any write error. */
static bool pic_fill_ff(File* out, uint32_t size) {
    static uint8_t ff[512];
    memset(ff, 0xFF, sizeof(ff));
    if(!storage_file_seek(out, 0, true)) return false;
    uint32_t left = size;
    while(left) {
        size_t n = left < sizeof(ff) ? left : sizeof(ff);
        if(storage_file_write(out, ff, n) != n) return false;
        left -= (uint32_t)n;
    }
    return true;
}

/* Normalises the chosen firmware into the canonical FLAT .bin on the SD (PROG_MAX
 * bytes, gaps = 0xFF) and parses the config words into cpu->config_*. STREAMS the
 * source (never reads the whole .hex into RAM, never requests a big block), then
 * opens the streaming LRU flash cache over the flat file. The core reads program
 * memory through app->fc via on_prog_read (fc_core_read). */
static LoadResult load_firmware(AppState* app, Storage* storage, const char* path) {
    File* f = storage_file_alloc(storage);
    File* out = storage_file_alloc(storage);
    LoadResult res = LoadIoError;
    bool out_open = false;
    do {
        if(!storage_file_open(f, path, FSAM_READ, FSOM_OPEN_EXISTING)) break;
        uint64_t fsize = storage_file_size(f);
        if(fsize == 0 || fsize > 4u * 1024u * 1024u) {
            res = LoadBadFormat;
            break;
        }

        /* core reads program memory through the paged cache; prog stays NULL */
        app->prog = NULL;
        pic18_set_prog(app->cpu, NULL, PROG_MAX);
        app->cpu->on_prog_read = fc_core_read;
        app->cpu->prog_read_ctx = &app->fc;
        app->cpu->config_count = 0;

        /* STEP 1: normalise into the flat .bin on the SD */
        storage_common_mkdir(storage, PP_DATA_DIR); /* ok if it already exists */
        if(!storage_file_open(out, PP_FLASH_BIN, FSAM_READ_WRITE, FSOM_CREATE_ALWAYS))
            break;
        out_open = true;
        if(!pic_fill_ff(out, PROG_MAX)) break; /* gaps / blank flash = 0xFF */

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
                            int r = pic_ihex_line(app, line, linepos, out, &ext_lin);
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
            /* flat binary: copy the first PROG_MAX bytes verbatim into the flat file */
            if(!storage_file_seek(out, 0, true)) break;
            uint32_t off = 0;
            bool werr = false;
            while(off < PROG_MAX && (rd = storage_file_read(f, chunk, sizeof(chunk))) > 0) {
                size_t n = rd;
                if(off + n > PROG_MAX) n = PROG_MAX - off;
                if(storage_file_write(out, chunk, n) != n) { werr = true; break; }
                off += (uint32_t)n;
            }
            if(werr) break;
        }
        /* flush + close the flat file before the cache opens it read-only */
        storage_file_close(out);
        storage_file_free(out);
        out = NULL;
        out_open = false;

        /* STEP 2: open the streaming LRU flash cache over the flat .bin */
        fc_init(&app->fc);
        app->fc.flash_used = PROG_MAX;
        if(!fc_open(&app->fc, storage)) {
            res = LoadNoMem;
            break;
        }
        FURI_LOG_I(
            TAG, "flash cache ready: %u slots / %lu pages (%s), free heap %uK max block %uK",
            app->fc.num_slots, (unsigned long)app->fc.num_pages,
            app->fc.fully_resident ? "fully resident" : "streaming LRU",
            (unsigned)(memmgr_get_free_heap() / 1024u),
            (unsigned)(memmgr_heap_get_max_free_block() / 1024u));
        res = LoadOk;
    } while(false);
    if(out_open) {
        storage_file_close(out);
    }
    if(out) storage_file_free(out);
    storage_file_close(f);
    storage_file_free(f);
    return res;
}

/* Selects the profile by file name, then by the reset vector GOTO target, then by
 * config words. super_mario (D010/D174): reset GOTO 0x00D052. mario2: reset GOTO
 * 0x0173F6. mariofull: reset GOTO 0x019CCC (see RE_super_mario.md §1). */
static const PicProfile* pick_profile(const char* path, AppState* app) {
    Pic18Cpu* cpu = app->cpu;
    if(path) {
        /* super_mario FIRST (its name also contains "mario") */
        if(strstr(path, "Super_mario") || strstr(path, "super_mario") ||
           strstr(path, "D010") || strstr(path, "D174"))
            return &PROFILE_SUPER_MARIO;
        if(strstr(path, "full") || strstr(path, "2552")) return &PROFILE_FULL;
        if(strstr(path, "MariO_2") || strstr(path, "mario2")) return &PROFILE_MARIO2;
    }
    /* Decode the reset-vector GOTO: op words at 0x00/0x02 give the 21-bit target
     * (word address = ((w2&0xFFF)<<8)|(w1&0xFF); byte addr = target*2). */
    uint16_t w1 = (uint16_t)(fc_read_addr(&app->fc, 0) | (fc_read_addr(&app->fc, 1) << 8));
    uint16_t w2 = (uint16_t)(fc_read_addr(&app->fc, 2) | (fc_read_addr(&app->fc, 3) << 8));
    if((w1 & 0xFF00) == 0xEF00) {
        uint32_t tgt = ((uint32_t)(w2 & 0x0FFF) << 8) | (w1 & 0xFF);
        tgt *= 2;
        if(tgt == 0x00D052u) return &PROFILE_SUPER_MARIO;
        if(tgt == 0x0173F6u) return &PROFILE_MARIO2;
        if(tgt == 0x019CCCu) return &PROFILE_FULL;
    }
    /* CONFIG3L at 0x300005: 0xA2=full, 0xBF=mario2 (fallback). */
    for(uint16_t i = 0; i < cpu->config_count; i++) {
        if(cpu->config_addr[i] == 0x300005u) {
            return (cpu->config_val[i] == 0xA2u) ? &PROFILE_FULL : &PROFILE_MARIO2;
        }
    }
    return &PROFILE_FULL;
}

/* ------------------------------------------------------------ boot/cinit */

/* Reads a program byte through whichever backing the core uses (paged cache or,
 * on host, the direct buffer). */
static uint8_t prog_byte(AppState* app, uint32_t addr) {
    if(addr >= app->cpu->prog_size) return 0xFF;
    if(app->cpu->on_prog_read) return app->cpu->on_prog_read(app->cpu, addr, app->cpu->prog_read_ctx);
    if(app->prog) return app->prog[addr];
    return 0xFF;
}

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
        uint8_t n = prog_byte(app, tp);
        tp++;
        if(n == 0) break;
        uint8_t fd = prog_byte(app, tp);
        tp++;
        if(fd & 0x80) {
            uint8_t lo = prog_byte(app, tp);
            tp++;
            fsr0 = (uint16_t)(((fd & 0x0F) << 8) | lo);
        }
        if(!(fd & 0x40)) {
            tp++; /* consume 1 extra byte (like the python) */
        }
        if(tp + n > end + 2) break; /* record runs off the table: desync */
        for(uint8_t k = 0; k < n; k++) {
            uint8_t b = prog_byte(app, tp);
            tp++;
            if(fsr0 < 0xF80u) cpu->ram[fsr0 & 0xFFF] = b;
            fsr0 = (uint16_t)((fsr0 + 1) & 0xFFF);
        }
    }
}

/* Force-calls a firmware subroutine with a return sentinel, saving/restoring the
 * main-loop context (PC/BSR/STKPTR/halted). Used to force the display render
 * (super_mario does not auto-render on cold boot; RE §10). */
#define FORCE_RET_SENTINEL 0x01FFFCu
static void force_call(AppState* app, uint32_t entry, uint64_t budget) {
    Pic18Cpu* cpu = app->cpu;
    uint32_t saved_pc = cpu->pc;
    uint8_t saved_bsr = cpu->ram[PIC18_SFR_BSR];
    uint8_t saved_stkptr = cpu->stkptr;
    uint8_t saved_halted = cpu->halted;
    uint8_t saved_irq = cpu->irq_enabled;

    cpu->irq_enabled = 0; /* don't let the IRQ model perturb a force-call */
    cpu->halted = 0;
    cpu->stkptr = 0;
    pic18_push(cpu, FORCE_RET_SENTINEL);
    cpu->pc = entry;
    while(budget-- > 0 && !cpu->halted && cpu->pc != FORCE_RET_SENTINEL) {
        pic18_step(cpu);
    }
    cpu->pc = saved_pc;
    cpu->ram[PIC18_SFR_BSR] = saved_bsr;
    cpu->stkptr = saved_stkptr;
    cpu->halted = saved_halted;
    cpu->irq_enabled = saved_irq;
}

/* FALLBACK render for super_mario (DIAGNOSTIC / cold-boot path): force-call the
 * display bring-up + main render so the screen draws. The primary path is the
 * auto-run with IRQ (run_emulator); this is only used when the firmware does NOT
 * draw on its own (confirmed for the D010 DIY firmware; it is gated by battery/
 * RTC/mode state not modelled -- RE_super_mario.md §4/§10). Writes the WORK buffer
 * and commits it to the VISIBLE buffer at the end so render_gddram shows it. */
static void force_render(AppState* app) {
    const PicProfile* prof = app->prof;
    if(!prof->force_render) return;
    for(int i = 0; i < 3; i++) {
        if(prof->disp_bringup[i]) force_call(app, prof->disp_bringup[i], 3000000);
    }
    /* clear the GDDRAM before the deterministic render so a half-frame from boot
     * does not linger */
    memset(app->gddram, 0, GDDRAM_SIZE);
    app->gd_page = app->gd_col = 0;
    app->gd_cmd_arg_left = 0;
    app->gd_work_dirty = false;
    if(prof->main_render) force_call(app, prof->main_render, 3000000);
    /* publish the forced frame to the visible buffer (bypass the SET-PAGE-0
     * boundary: a force-call is a single complete pass). */
    app->gd_work_dirty = true;
    oled_commit(app);
    uint32_t nz = gddram_nonzero(app->gddram);
    FURI_LOG_I(TAG, "force_render: gddram nonzero=%lu/%u (committed)", (unsigned long)nz,
               GDDRAM_SIZE);
}

/* Takes the firmware from reset to the main loop, with the runtime init resolved
 * according to the profile. Returns true if the boot reached its destination. */
static bool boot_firmware(AppState* app) {
    Pic18Cpu* cpu = app->cpu;
    pic18_reset(cpu);
    const PicProfile* prof = app->prof;

    if(prof->boot_mode == 1) {
        /* mario2: run prologue+data-init up to the cinit entry, apply the
         * records, jump to the cinit exit. */
        uint64_t budget = 2000000;
        while(cpu->pc != prof->cinit_entry && budget > 0) {
            pic18_step(cpu);
            budget--;
            if(cpu->halted) break;
        }
        if(cpu->pc != prof->cinit_entry) return false;
        apply_cinit_records(app);
        cpu->pc = prof->cinit_exit;
        return true;
    } else if(prof->boot_mode == 2) {
        /* super_mario (D010/D174): no handshake. The XC8 C-runtime + peripheral
         * init runs straight through (reset GOTO 0x00D052 -> data/cinit -> EEPROM/
         * ADC/OSC init -> main loop ~0x00D2B2). The code_hook neutralises the SW
         * display delay (0x001364) and self-clears the EECON1/ADCON0 busy bits.
         * We free-run a fixed budget, then the caller enables interrupts and tries
         * the auto-run render (falling back to force_render if nothing is drawn). */
        uint64_t budget = 3000000;
        while(budget-- > 0 && !cpu->halted) {
            pic18_step(cpu);
        }
        FURI_LOG_I(
            TAG, "super_mario boot: ran %llu steps -> PC=0x%06lX (halted=%u)",
            (unsigned long long)cpu->steps, (unsigned long)cpu->pc, (unsigned)cpu->halted);
        return !cpu->halted;
    } else {
        /* mariofull: the PORTB.1 handshake (port_read_hook) lets the cinit finish
         * on its own; we run up to the main loop. */
        uint64_t budget = 3000000;
        while(cpu->pc != prof->cinit_wait_pc && budget > 0) {
            pic18_step(cpu);
            budget--;
            if(cpu->halted) break;
        }
        return cpu->pc == prof->cinit_wait_pc;
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

/* --- Si4432 (D010) TX replay ---
 * The D010 loads the frame into the Si4432 FIFO (captured in si_payload) and
 * triggers TX. The Si4432 serialises the packet at a data-rate programmed in its
 * config (not fully resolved; RE §9.7). The most robust bridge is to convert the
 * captured payload bytes to an OOK bit pattern (MSB-first, one chip per bit) and
 * replay it on the CC1101 as edges. The per-bit cell time is a tunable constant
 * (TE_US); on real hardware the exact rate can be calibrated if the target
 * receiver is strict. Honest limitation: without the physical bus trace the
 * absolute data-rate is approximate; the payload CONTENT (bytes) is exact. */
#define SI4432_TE_US 500u /* OOK chip time per payload bit (tunable) */

static void si4432_tx_replay(AppState* app) {
    if(!app->prof || app->prof->rf_mode != RfModeSi4432Fifo) return;
    if(!app->si_tx_pending) return;
    app->si_tx_pending = false;
    if(!app->rf_tx_enabled || !app->radio_on) {
        FURI_LOG_D(TAG, "si4432: TX pending but RF bridge OFF; payload buffered");
        app->si_tx_pending = true; /* keep it until the bridge is enabled */
        return;
    }
    if(app->si_payload_len == 0) {
        FURI_LOG_W(TAG, "si4432: TX trigger but empty payload");
        return;
    }
    /* Push the payload as OOK edges into the tx_ring (1 = carrier on). We merge
     * consecutive equal bits into one edge to keep the ring small. */
    app->tx_head = app->tx_tail = 0;
    bool cur = false;
    uint32_t run = 0;
    for(uint8_t i = 0; i < app->si_payload_len; i++) {
        uint8_t byte = app->si_payload[i];
        for(int b = 7; b >= 0; b--) {
            bool bit = (byte >> b) & 1u;
            if(bit == cur) {
                run++;
            } else {
                if(run) {
                    uint32_t head = app->tx_head;
                    uint32_t next = (head + 1) % TX_RING_LEN;
                    if(next != app->tx_tail) {
                        app->tx_ring[head].level = cur;
                        app->tx_ring[head].duration = run * SI4432_TE_US;
                        app->tx_head = next;
                    }
                }
                cur = bit;
                run = 1;
            }
        }
    }
    if(run) {
        uint32_t head = app->tx_head;
        uint32_t next = (head + 1) % TX_RING_LEN;
        if(next != app->tx_tail) {
            app->tx_ring[head].level = cur;
            app->tx_ring[head].duration = run * SI4432_TE_US;
            app->tx_head = next;
        }
    }
    uint32_t edges = (app->tx_head + TX_RING_LEN - app->tx_tail) % TX_RING_LEN;
    FURI_LOG_I(TAG, "si4432: replay %u-byte payload as %lu OOK edges",
               (unsigned)app->si_payload_len, (unsigned long)edges);
    if(!furi_hal_subghz_is_tx_allowed(app->frequency)) {
        FURI_LOG_E(TAG, "si4432: TX NOT allowed @%lu Hz", (unsigned long)app->frequency);
        app->tx_tail = app->tx_head;
        return;
    }
    if(furi_hal_subghz_start_async_tx(radio_tx_callback, app)) {
        app->tx_active = true;
    } else {
        FURI_LOG_E(TAG, "si4432: start_async_tx failed");
        app->tx_tail = app->tx_head;
    }
    /* consume the payload so the next TX sequence starts clean */
    app->si_payload_len = 0;
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

/* Cycles the RF bridge. For the OOK siblings: OFF -> TX -> RX -> OFF. For the
 * D010 (Si4432, pure transmitter): OFF -> TX -> OFF (no RX path). Logged. */
static void rf_cycle_mode(AppState* app) {
    const PicProfile* prof = app->prof;
    bool tx_only = (prof->rf_mode == RfModeSi4432Fifo); /* D010 has no RX */

    if(!app->rf_tx_enabled && !app->rf_rx_enabled) {
        /* OFF -> TX */
        app->rf_tx_enabled = true;
        if(prof->rf_mode == RfModeOok) {
            app->tx_last_level =
                (pic18_read_ram(app->cpu, prof->ook_tx_lat) >> prof->ook_tx_bit) & 1u;
            app->tx_last_cycles = app->cpu->cycles;
            app->tx_head = app->tx_tail = 0;
            FURI_LOG_I(TAG, "RF mode TX (capture OOK pin %03X.%u)",
                       prof->ook_tx_lat, prof->ook_tx_bit);
        } else {
            FURI_LOG_I(TAG, "RF mode TX (Si4432 FIFO replay; CS=%03X.%u SCLK=%03X.%u)",
                       prof->si_cs_lat, prof->si_cs_bit, prof->si_sck_lat, prof->si_sck_bit);
        }
    } else if(app->rf_tx_enabled && !app->rf_rx_enabled) {
        /* TX -> (RX for OOK) / OFF for the D010 */
        app->rf_tx_enabled = false;
        if(app->tx_active) {
            furi_hal_subghz_stop_async_tx();
            app->tx_active = false;
        }
        if(tx_only) {
            furi_hal_subghz_idle();
            FURI_LOG_I(TAG, "RF mode OFF (D010 is TX-only, no RX)");
        } else {
            rx_bridge_start(app);
        }
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

    /* LATE-based siblings: seed the button register to "all released" (0xFF,
     * active-low) so no phantom press is seen before the first poll. */
    if(app->prof->btn_is_lat) pic18_write_ram(app->cpu, app->prof->btn_reg, 0xFF);

    /* --- PIN injection after boot (mariofull only) --- */
    enter_pin(app);

    /* --- interrupt model: run the firmware FREELY with interrupts active so it
     * draws its OWN screen (boot splash + main screen), exactly like PandoraARM
     * which runs run_burst() continuously with the NVIC model instead of forcing
     * the render. The SW-delay-skip (code_hook) is what makes this fast enough.
     * Verified in emu/pic18_tui.py: mario2/mariofull auto-draw (nz=69/512) with
     * enable_interrupts + free_run; super_mario does NOT (see fallback below). --- */
    const PicProfile* prof = app->prof;
    if(prof->irq_enable) {
        pic18_irq_release_clear(app->cpu);
        if(prof->irq_sem_addr) pic18_irq_release_add(app->cpu, prof->irq_sem_addr, prof->irq_sem_bit);
        pic18_enable_interrupts(app->cpu, prof->irq_period);
        /* GIE must be set for the model to deliver; the firmware's own init may
         * already have set it, but enable it here so the first tick fires. */
        uint8_t intcon = pic18_read_ram(app->cpu, 0xFF2u);
        pic18_write_ram(app->cpu, 0xFF2u, (uint8_t)(intcon | 0x80u));
        FURI_LOG_I(
            TAG, "interrupts ON (period=%lu, sem=%03X.%u) -> AUTO-RUN render",
            (unsigned long)prof->irq_period, prof->irq_sem_addr, prof->irq_sem_bit);

        /* Prime the auto-run: let the firmware run freely for a few bursts so it
         * reaches and completes its first flush pass BEFORE we take over the GUI.
         * This is the "firmware draws itself" path (no force_render). A SET PAGE 0
         * of the NEXT pass (or the idle-stability commit below) publishes it. */
        for(int i = 0; i < 24 && gddram_nonzero(app->gddram_vis) == 0; i++) {
            pic18_run(app->cpu, app->steps_per_frame);
            /* idle-stability commit: if the firmware finished its single flush pass
             * (work dirty + stable vs last burst + non-blank), publish it now. The
             * siblings emit SET PAGE 0 only once (at the start, when work is still
             * blank), so without this the one-shot frame would never be shown. */
            if(app->gd_work_dirty && memcmp(app->gddram, app->gddram_prev, GDDRAM_SIZE) == 0)
                oled_commit(app);
            memcpy(app->gddram_prev, app->gddram, GDDRAM_SIZE);
        }
        FURI_LOG_I(TAG, "auto-run: gddram_vis nonzero=%lu/%u after prime",
                   (unsigned long)gddram_nonzero(app->gddram_vis), GDDRAM_SIZE);
    }

    /* --- FALLBACK: if the firmware did NOT draw on its own (super_mario: its
     * refresh is gated by battery/RTC/mode state not modelled, confirmed in the
     * Python reference), force the bring-up + main render. The siblings reach here
     * already drawn, so force_render is skipped for them (force_render=false). --- */
    if(prof->force_render && gddram_nonzero(app->gddram_vis) == 0) {
        FURI_LOG_I(TAG, "auto-run drew nothing -> FALLBACK force_render");
        force_render(app);
        app->render_forced = true;
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

        /* input -> keyfob buttons (+ logged transitions). Polled once up front and
         * again between sub-bursts below so a press is seen within one sub-burst
         * (~tens of ms) instead of a whole frame's worth of insns. */
        uint8_t keys = poll_raw_keys();
        apply_keys_to_buttons(app, keys);
        log_button_changes(app, keys);

        /* Run this frame's insn budget in sub-bursts, re-polling the input between
         * them. Same TOTAL useful insn/s as one big burst (the CPU runs flat out);
         * only the input-sampling cadence tightens -> far more responsive buttons.
         * The combo-menu request (set in apply_keys_to_buttons) breaks out early so
         * the overlay opens promptly. */
        uint64_t before = app->cpu->steps;
        uint32_t remaining = app->steps_per_frame;
        while(remaining > 0 && !app->exit_requested && !app->menu_requested) {
            uint32_t chunk = remaining < EMU_INSN_PER_BURST ? remaining : EMU_INSN_PER_BURST;
            uint64_t ran = pic18_run(app->cpu, chunk);
            remaining -= chunk;
            if(ran < chunk) break; /* firmware halted: stop sub-bursting this frame */
            if(remaining > 0) {
                uint8_t k = poll_raw_keys();
                apply_keys_to_buttons(app, k);
                log_button_changes(app, k);
            }
        }
        app->hb_steps += (app->cpu->steps - before);

        /* double-buffer idle-stability commit: a complete flush pass normally
         * commits on the next SET PAGE 0, but a firmware that redraws once (or
         * goes idle between redraws) would leave the last pass uncommitted. If the
         * work buffer is dirty and UNCHANGED vs the previous slice (the firmware
         * finished drawing and is idle), publish it now. Blank frames are skipped
         * inside oled_commit, so this never shows a half/black frame. */
        if(app->gd_work_dirty &&
           memcmp(app->gddram, app->gddram_prev, GDDRAM_SIZE) == 0) {
            oled_commit(app);
        }
        memcpy(app->gddram_prev, app->gddram, GDDRAM_SIZE);

        /* RF bridge: Si4432 FIFO replay (D010) + OOK TX replay / RX inject */
        si4432_tx_replay(app);
        tx_bridge_flush(app);
        rx_bridge_pump(app);

        /* reconstruct the screen from the captured GDDRAM */
        render_gddram(app);

        /* DIAG: display heartbeat (framebuffer nonzero) + emulation-rate heartbeat
         * every ~2s so the user can confirm on the console that the firmware keeps
         * running and whether it is drawing. */
        {
            static uint32_t s_hb_tick = 0;
            static uint64_t s_hb_steps = 0;
            uint32_t t = furi_get_tick();
            if(s_hb_tick == 0) s_hb_tick = t;
            if((uint32_t)(t - s_hb_tick) >= 2000) {
                uint32_t nz = 0;
                for(uint32_t i = 0; i < GDDRAM_SIZE; i++)
                    if(app->gddram[i]) nz++;
                uint64_t di = app->hb_steps - s_hb_steps;
                uint32_t dt = t - s_hb_tick;
                FURI_LOG_D(
                    TAG,
                    "hb: steps=%llu rate=%lu k-insn/s gddram_nz=%lu/%u irq=%lu spi_frames=%lu",
                    (unsigned long long)app->hb_steps,
                    (unsigned long)(dt ? (di / dt) : 0), (unsigned long)nz, GDDRAM_SIZE,
                    (unsigned long)app->cpu->irq_count, (unsigned long)app->si_frames);
                s_hb_tick = t;
                s_hb_steps = app->hb_steps;
            }
        }

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
    FURI_LOG_I(
        TAG, "emulator: exit (total steps=%llu, TX frames=%lu, SPI frames=%lu, flash misses=%lu)",
        (unsigned long long)app->cpu->steps, (unsigned long)app->tx_frames,
        (unsigned long)app->si_frames, (unsigned long)app->fc.miss_count);
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
                TAG, "free heap=%uK, max block=%uK, prog (paged %u x 4KB)=%uK",
                (unsigned)(memmgr_get_free_heap() / 1024u),
                (unsigned)(memmgr_heap_get_max_free_block() / 1024u),
                (unsigned)((PROG_MAX + FLASH_PAGE_SIZE - 1u) / FLASH_PAGE_SIZE),
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

            app->prof = pick_profile(furi_string_get_cstr(app->fw_path), app);
            FURI_LOG_I(TAG, "profile: %s (PIN=%s, RF=%s, buttons@%03X %s)", app->prof->name,
                       app->prof->has_pin ? "yes" : "no",
                       app->prof->rf_mode == RfModeSi4432Fifo ? "Si4432-FIFO(TX)" : "OOK",
                       app->prof->btn_reg,
                       app->prof->btn_is_lat ? "LATE(ram-inject)" : "PORTB(port-read)");

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
            app->booted = true;
            FURI_LOG_I(
                TAG, "boot OK -> PC=0x%06lX (steps=%llu, flash misses=%lu)",
                (unsigned long)app->cpu->pc, (unsigned long long)app->cpu->steps,
                (unsigned long)app->fc.miss_count);

            run_emulator(app, gui);
        } while(false);
    }

    /* --- teardown --- */
    if(app->cpu) free(app->cpu);
    if(app->prog) free(app->prog); /* legacy direct buffer (unused in paged mode) */
    fc_free(&app->fc); /* paged flash cache */
    furi_string_free(app->fw_path);
    furi_record_close(RECORD_GUI);
    furi_record_close(RECORD_DIALOGS);
    furi_record_close(RECORD_STORAGE);
    free(app);
    g_app = NULL;
    return 0;
}
