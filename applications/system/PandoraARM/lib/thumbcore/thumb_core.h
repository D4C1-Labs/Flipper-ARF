/*
 * thumb_core.h - Functional ARM Cortex-M3 CPU interpreter / emulator (Thumb-2,
 *                ARMv7-M) in pure C (C99/C11).
 *
 * ARM analogue of the PIC18 core (applications/system/PandoraPIC/lib/pic18core/).
 * Intended to run the real Pandora keyfob firmware (EFM32 Cortex-M3):
 * 2_5253623551453304897.flash.bin and PANDORA_MAX.flash.bin.
 *
 * Goal: compilable both for the host (gcc -std=c11) and for Flipper Zero /
 * ARM Cortex-M4 (arm-none-eabi-gcc -mcpu=cortex-m4 -mthumb -Os -Wall -Wextra
 * -Werror). No dependencies beyond <stdint.h>, <stddef.h>, <string.h>.
 *
 * ---------------------------------------------------------------------------
 * MEMORY MODEL (important):
 *   The core does NOT own the memory. The frontend decides the map (FLASH / RAM /
 *   EFM32 MMIO) through two callbacks:
 *
 *     typedef uint32_t (*ThumbRead )(void* ctx, uint32_t addr, int size);
 *     typedef void     (*ThumbWrite)(void* ctx, uint32_t addr, uint32_t val, int size);
 *
 *   size is 1, 2 or 4 bytes. All reads/writes are little-endian
 *   (same as Cortex-M).
 *
 *   PERFORMANCE: in addition to the callbacks, the core accepts direct pointers
 *   to a FLASH region and a RAM region (thumb_set_regions). Fetch and aligned
 *   reads/writes that fall within those regions use the direct pointer (without
 *   going through the callback). Addresses outside those regions (MMIO, PPB,
 *   etc.) always go through the callback. Writes to RAM are reflected in the
 *   direct buffer. This yields high ~N instr/sec on the host while keeping
 *   accuracy with Unicorn.
 * ---------------------------------------------------------------------------
 *
 * COVERAGE: all the Thumb-2 used by both firmwares. Exact N,Z,C,V flags,
 * IT blocks, TBB/TBH, UDIV/SDIV, bitfields, basic saturation, barriers.
 *
 * INTERRUPTS: the core does NOT implement NVIC. It exposes thumb_enter_exception()
 * (pushes the Cortex-M stack frame {r0,r1,r2,r3,r12,lr,pc,xpsr}, sets PC=handler
 * and LR=EXC_RETURN) and detects EXC_RETURN in BX/POP to do the unstacking
 * (replica of emu/pandora_tui.py _enter_irq/_exc_return).
 */
#ifndef THUMB_CORE_H
#define THUMB_CORE_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------------- xPSR bits */
#define THUMB_PSR_N (1u << 31)
#define THUMB_PSR_Z (1u << 30)
#define THUMB_PSR_C (1u << 29)
#define THUMB_PSR_V (1u << 28)
#define THUMB_PSR_Q (1u << 27)
#define THUMB_PSR_T (1u << 24)
#define THUMB_PSR_ICI_IT_MASK 0x0600FC00u /* IT[7:0] scattered across xPSR */

/* Typical EXC_RETURN (Cortex-M). The core detects the 0xFFFFFFxx prefix. */
#define THUMB_EXC_RETURN_MSP_THREAD  0xFFFFFFF9u
#define THUMB_EXC_RETURN_PSP_THREAD  0xFFFFFFFDu
#define THUMB_EXC_RETURN_MSP_HANDLER 0xFFFFFFF1u

/* Fault codes returned by thumb_step (< 0). */
#define THUMB_OK           0
#define THUMB_FAULT_UNDEF (-1)
#define THUMB_FAULT_MEM   (-2)
#define THUMB_FAULT_HALT  (-3)

struct ThumbCore; /* fwd */

/* ----------------------------------------------------------------- callbacks */
typedef uint32_t (*ThumbRead)(void* ctx, uint32_t addr, int size);  /* size 1/2/4 */
typedef void     (*ThumbWrite)(void* ctx, uint32_t addr, uint32_t val, int size);
/* Per-instruction hook: invoked with the PC (bit0 cleared) BEFORE executing. */
typedef void     (*ThumbHook)(void* ctx, uint32_t pc);

/* ----------------------------------------------------------------- state --- */
typedef struct ThumbCore {
    uint32_t r[16];     /* r0..r15 ; r[13]=active SP, r[14]=LR, r[15]=PC */
    uint32_t xpsr;      /* APSR(N,Z,C,V,Q) | IPSR | EPSR(T, IT) */
    uint32_t primask;   /* bit0 */
    uint32_t faultmask; /* bit0 */
    uint32_t basepri;   /* 8 bits */
    uint32_t control;   /* bit0=nPRIV, bit1=SPSEL */
    uint32_t msp;       /* main stack pointer   */
    uint32_t psp;       /* process stack pointer */

    /* State of the IT block in progress (decoded from EPSR for speed). */
    uint8_t it_cond;    /* base condition (firstcond) */
    uint8_t it_mask;    /* IT mask (4 bits), 0 = no active IT */

    uint64_t cycles;    /* counter of executed instructions */
    uint8_t  halted;    /* BKPT / fatal fault */
    uint8_t  sleeping;  /* WFI/WFE (the core advances anyway; the frontend decides) */
    uint8_t  branched;  /* internal: the current instruction wrote PC (branch) */
    int      last_fault;

    /* Direct regions (optional, for fast fetch/access). */
    uint8_t* flash_ptr; uint32_t flash_base; uint32_t flash_size;
    uint8_t* ram_ptr;   uint32_t ram_base;   uint32_t ram_size;

    /* Memory callbacks (MMIO and anything that does not fall in the regions). */
    ThumbRead  read_cb;
    ThumbWrite write_cb;
    void*      mem_ctx;

    /* Optional per-instruction hook. */
    ThumbHook  hook;
    void*      hook_ctx;
} ThumbCore;

/* ---------------------------------------------------------------- API ------- */

/* Initializes everything to zero. Call before anything else. */
void thumb_init(ThumbCore* c);

/* Reset: sets SP(=MSP) and PC. Sets Thumb mode (T=1), clears IT. */
void thumb_reset(ThumbCore* c, uint32_t sp, uint32_t pc);

/* Sets the memory callbacks (MMIO). */
void thumb_set_mem_cb(ThumbCore* c, ThumbRead rd, ThumbWrite wr, void* ctx);

/* Sets direct FLASH and RAM regions for fast fetch/access (optional).
 * Pass NULL/0 to disable a region. */
void thumb_set_regions(ThumbCore* c,
                       uint8_t* flash_ptr, uint32_t flash_base, uint32_t flash_size,
                       uint8_t* ram_ptr,   uint32_t ram_base,   uint32_t ram_size);

/* Sets the per-instruction hook (optional). */
void thumb_set_hook(ThumbCore* c, ThumbHook hook, void* ctx);

/* Executes ONE instruction. Returns 0 if ok, <0 on fault (THUMB_FAULT_* code). */
int thumb_step(ThumbCore* c);

/* Executes up to max instructions or until fault/halt. Returns number executed. */
uint64_t thumb_run(ThumbCore* c, uint64_t max);

/* ---------------- memory (respects direct regions + callbacks) ------------- */
uint32_t thumb_read_mem(ThumbCore* c, uint32_t addr, int size);
void     thumb_write_mem(ThumbCore* c, uint32_t addr, uint32_t val, int size);

/* ---------------- registers ------------------------------------------------- */
uint32_t thumb_get_reg(const ThumbCore* c, int n);  /* n 0..15 */
void     thumb_set_reg(ThumbCore* c, int n, uint32_t v);
uint32_t thumb_get_xpsr(const ThumbCore* c);
void     thumb_set_xpsr(ThumbCore* c, uint32_t v);

/* ---------------- exceptions (NVIC handled by the frontend) -----------------
 * thumb_enter_exception: pushes {r0,r1,r2,r3,r12,lr,pc,xpsr} onto the active SP,
 * sets PC=handler|1, LR=exc_return (e.g. THUMB_EXC_RETURN_MSP_THREAD),
 * and sets IPSR=exc_num. Returns 0 ok. Replica of _enter_irq in pandora_tui.py.
 */
int thumb_enter_exception(ThumbCore* c, uint32_t handler, uint32_t exc_return, uint32_t exc_num);

/* Is 'val' an EXC_RETURN (0xFFFFFFFx)? Useful for the frontend to detect the
 * ISR return in a BX LR. */
int thumb_is_exc_return(uint32_t val);

/* thumb_exc_return: undoes the push (unstacking) using 'exc_return' to know
 * which stack to pull the frame from. Leaves PC at the interrupted instruction.
 * Replica of _exc_return in pandora_tui.py. */
int thumb_exc_return(ThumbCore* c, uint32_t exc_return);

#ifdef __cplusplus
}
#endif

#endif /* THUMB_CORE_H */
