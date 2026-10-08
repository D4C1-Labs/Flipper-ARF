/*
 * ctypes_shim.c - Expone el core Thumb a Python via ctypes, con callbacks de
 * memoria implementados en Python. Permite que el MISMO modelo de MMIO (escrito
 * en Python) sirva tanto a Unicorn como a nuestro interprete C, de modo que la
 * comparacion paso-a-paso sea 100% valida.
 *
 * Compilar:
 *   gcc -std=c11 -O2 -fPIC -shared -I../lib/thumbcore \
 *       ctypes_shim.c ../lib/thumbcore/thumb_core.c -o libthumbshim.so
 */
#include "thumb_core.h"
#include <stdlib.h>

/* Callbacks hacia Python */
typedef uint32_t (*PyRead)(uint32_t addr, int size);
typedef void     (*PyWrite)(uint32_t addr, uint32_t val, int size);

static PyRead  g_pyread  = NULL;
static PyWrite g_pywrite = NULL;

static uint32_t c_read(void* ctx, uint32_t addr, int size) {
    (void)ctx;
    return g_pyread ? g_pyread(addr, size) : 0;
}
static void c_write(void* ctx, uint32_t addr, uint32_t val, int size) {
    (void)ctx;
    if(g_pywrite) g_pywrite(addr, val, size);
}

static ThumbCore g_core;

void shim_init(void) {
    thumb_init(&g_core);
    thumb_set_mem_cb(&g_core, c_read, c_write, NULL);
}

void shim_set_callbacks(PyRead rd, PyWrite wr) {
    g_pyread = rd;
    g_pywrite = wr;
}

void shim_reset(uint32_t sp, uint32_t pc) {
    thumb_reset(&g_core, sp, pc);
}

int shim_step(void) {
    return thumb_step(&g_core);
}

uint32_t shim_get_reg(int n) { return thumb_get_reg(&g_core, n); }
void     shim_set_reg(int n, uint32_t v) { thumb_set_reg(&g_core, n, v); }
uint32_t shim_get_xpsr(void) { return thumb_get_xpsr(&g_core); }
void     shim_set_xpsr(uint32_t v) { thumb_set_xpsr(&g_core, v); }

uint32_t shim_get_pc(void) { return thumb_get_reg(&g_core, 15); }

/* No usamos regiones directas aqui: TODO va por callback para igualar a Unicorn
 * byte-a-byte (mismo modelo de memoria exacto). */
