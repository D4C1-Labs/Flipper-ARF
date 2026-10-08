/*
 * thumb_core.h - Interprete / emulador funcional de CPU ARM Cortex-M3 (Thumb-2,
 *                ARMv7-M) en C puro (C99/C11).
 *
 * Analogo ARM del nucleo PIC18 (applications/system/PandoraPIC/lib/pic18core/).
 * Pensado para ejecutar el firmware real de los keyfobs Pandora (EFM32
 * Cortex-M3): 2_5253623551453304897.flash.bin y PANDORA_MAX.flash.bin.
 *
 * Objetivo: compilable tanto para host (gcc -std=c11) como para Flipper Zero /
 * ARM Cortex-M4 (arm-none-eabi-gcc -mcpu=cortex-m4 -mthumb -Os -Wall -Wextra
 * -Werror). Sin dependencias fuera de <stdint.h>, <stddef.h>, <string.h>.
 *
 * ---------------------------------------------------------------------------
 * MODELO DE MEMORIA (importante):
 *   El core NO posee la memoria. El frontend decide el mapa (FLASH / RAM /
 *   MMIO del EFM32) mediante dos callbacks:
 *
 *     typedef uint32_t (*ThumbRead )(void* ctx, uint32_t addr, int size);
 *     typedef void     (*ThumbWrite)(void* ctx, uint32_t addr, uint32_t val, int size);
 *
 *   size es 1, 2 o 4 bytes. Todas las lecturas/escrituras son little-endian
 *   (igual que Cortex-M).
 *
 *   RENDIMIENTO: ademas de los callbacks, el core acepta punteros directos a
 *   una region FLASH y a una region RAM (thumb_set_regions). Fetch y
 *   lecturas/escrituras alineadas que caigan dentro de esas regiones usan el
 *   puntero directo (sin pasar por el callback). Direcciones fuera de esas
 *   regiones (MMIO, PPB, etc.) siempre van por el callback. Las escrituras a
 *   RAM se reflejan en el buffer directo. Esto da ~N instr/seg altas en host
 *   manteniendo exactitud con Unicorn.
 * ---------------------------------------------------------------------------
 *
 * COBERTURA: todo el Thumb-2 que usan ambos firmwares. Flags N,Z,C,V exactos,
 * bloques IT, TBB/TBH, UDIV/SDIV, bitfields, saturacion basica, barreras.
 *
 * INTERRUPCIONES: el core NO implementa NVIC. Expone thumb_enter_exception()
 * (apila el stack frame Cortex-M {r0,r1,r2,r3,r12,lr,pc,xpsr}, pone PC=handler
 * y LR=EXC_RETURN) y detecta EXC_RETURN en BX/POP para hacer el unstacking
 * (replica de emu/pandora_tui.py _enter_irq/_exc_return).
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
#define THUMB_PSR_ICI_IT_MASK 0x0600FC00u /* IT[7:0] disperso en xPSR */

/* EXC_RETURN tipicos (Cortex-M). El core detecta el prefijo 0xFFFFFFxx. */
#define THUMB_EXC_RETURN_MSP_THREAD  0xFFFFFFF9u
#define THUMB_EXC_RETURN_PSP_THREAD  0xFFFFFFFDu
#define THUMB_EXC_RETURN_MSP_HANDLER 0xFFFFFFF1u

/* Codigos de fault devueltos por thumb_step (< 0). */
#define THUMB_OK           0
#define THUMB_FAULT_UNDEF (-1)
#define THUMB_FAULT_MEM   (-2)
#define THUMB_FAULT_HALT  (-3)

struct ThumbCore; /* fwd */

/* ----------------------------------------------------------------- callbacks */
typedef uint32_t (*ThumbRead)(void* ctx, uint32_t addr, int size);  /* size 1/2/4 */
typedef void     (*ThumbWrite)(void* ctx, uint32_t addr, uint32_t val, int size);
/* Hook por instruccion: invocado con el PC (bit0 limpio) ANTES de ejecutar. */
typedef void     (*ThumbHook)(void* ctx, uint32_t pc);

/* ----------------------------------------------------------------- estado --- */
typedef struct ThumbCore {
    uint32_t r[16];     /* r0..r15 ; r[13]=SP activo, r[14]=LR, r[15]=PC */
    uint32_t xpsr;      /* APSR(N,Z,C,V,Q) | IPSR | EPSR(T, IT) */
    uint32_t primask;   /* bit0 */
    uint32_t faultmask; /* bit0 */
    uint32_t basepri;   /* 8 bits */
    uint32_t control;   /* bit0=nPRIV, bit1=SPSEL */
    uint32_t msp;       /* main stack pointer   */
    uint32_t psp;       /* process stack pointer */

    /* Estado del bloque IT en curso (decodificado de EPSR para rapidez). */
    uint8_t it_cond;    /* condicion base (firstcond) */
    uint8_t it_mask;    /* mascara IT (4 bits), 0 = sin IT activo */

    uint64_t cycles;    /* contador de instrucciones ejecutadas */
    uint8_t  halted;    /* BKPT / fault fatal */
    uint8_t  sleeping;  /* WFI/WFE (el core igual avanza; el frontend decide) */
    uint8_t  branched;  /* interno: la instruccion actual escribio PC (salto) */
    int      last_fault;

    /* Regiones directas (opcional, para fetch/acceso rapido). */
    uint8_t* flash_ptr; uint32_t flash_base; uint32_t flash_size;
    uint8_t* ram_ptr;   uint32_t ram_base;   uint32_t ram_size;

    /* Callbacks de memoria (MMIO y todo lo que no caiga en las regiones). */
    ThumbRead  read_cb;
    ThumbWrite write_cb;
    void*      mem_ctx;

    /* Hook opcional por instruccion. */
    ThumbHook  hook;
    void*      hook_ctx;
} ThumbCore;

/* ---------------------------------------------------------------- API ------- */

/* Inicializa todo a cero. Llamar antes de cualquier otra cosa. */
void thumb_init(ThumbCore* c);

/* Reset: fija SP(=MSP) y PC. Pone modo Thumb (T=1), limpia IT. */
void thumb_reset(ThumbCore* c, uint32_t sp, uint32_t pc);

/* Fija los callbacks de memoria (MMIO). */
void thumb_set_mem_cb(ThumbCore* c, ThumbRead rd, ThumbWrite wr, void* ctx);

/* Fija regiones directas de FLASH y RAM para fetch/acceso rapido (opcional).
 * Pasa NULL/0 para deshabilitar una region. */
void thumb_set_regions(ThumbCore* c,
                       uint8_t* flash_ptr, uint32_t flash_base, uint32_t flash_size,
                       uint8_t* ram_ptr,   uint32_t ram_base,   uint32_t ram_size);

/* Fija el hook por instruccion (opcional). */
void thumb_set_hook(ThumbCore* c, ThumbHook hook, void* ctx);

/* Ejecuta UNA instruccion. Devuelve 0 si ok, <0 en fault (codigo THUMB_FAULT_*). */
int thumb_step(ThumbCore* c);

/* Ejecuta hasta max instrucciones o hasta fault/halt. Devuelve nº ejecutadas. */
uint64_t thumb_run(ThumbCore* c, uint64_t max);

/* ---------------- memoria (respeta regiones directas + callbacks) ----------- */
uint32_t thumb_read_mem(ThumbCore* c, uint32_t addr, int size);
void     thumb_write_mem(ThumbCore* c, uint32_t addr, uint32_t val, int size);

/* ---------------- registros ------------------------------------------------- */
uint32_t thumb_get_reg(const ThumbCore* c, int n);  /* n 0..15 */
void     thumb_set_reg(ThumbCore* c, int n, uint32_t v);
uint32_t thumb_get_xpsr(const ThumbCore* c);
void     thumb_set_xpsr(ThumbCore* c, uint32_t v);

/* ---------------- excepciones (NVIC lo maneja el frontend) ------------------
 * thumb_enter_exception: apila {r0,r1,r2,r3,r12,lr,pc,xpsr} en el SP activo,
 * pone PC=handler|1, LR=exc_return (p.ej. THUMB_EXC_RETURN_MSP_THREAD),
 * y fija IPSR=exc_num. Devuelve 0 ok. Replica _enter_irq de pandora_tui.py.
 */
int thumb_enter_exception(ThumbCore* c, uint32_t handler, uint32_t exc_return, uint32_t exc_num);

/* ¿'val' es un EXC_RETURN (0xFFFFFFFx)? Util para que el frontend detecte el
 * retorno de ISR en un BX LR. */
int thumb_is_exc_return(uint32_t val);

/* thumb_exc_return: deshace el apilado (unstacking) usando 'exc_return' para
 * saber de que stack sacar el frame. Deja PC en la instruccion interrumpida.
 * Replica _exc_return de pandora_tui.py. */
int thumb_exc_return(ThumbCore* c, uint32_t exc_return);

#ifdef __cplusplus
}
#endif

#endif /* THUMB_CORE_H */
