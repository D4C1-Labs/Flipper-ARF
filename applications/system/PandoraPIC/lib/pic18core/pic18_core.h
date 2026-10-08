/*
 * pic18_core.h - Interprete / emulador funcional de CPU PIC18 (nucleo 16-bit).
 *
 * Port fiel, instruccion por instruccion, de emu/pic18cpu.py (python de
 * referencia, verificado). Mismo semantica de opcodes, flags STATUS
 * (C/DC/Z/OV/N), bancos (BSR, bit access 'a'), FSR0/1/2 con
 * INDF/POSTINC/POSTDEC/PREINC/PLUSW, pila de retorno de 31 niveles, fast
 * stack (bit 's'), TBLPTR/TABLAT, PRODH:L, SFRs 0xF80-0xFFF.
 *
 * Objetivo: compilable tanto para host (gcc -std=c11) como para Flipper Zero /
 * ARM Cortex-M4 (arm-none-eabi-gcc -mcpu=cortex-m4 -mthumb -Os). Sin
 * dependencias fuera de <stdint.h>, <string.h>, <stdlib.h> (y <stdio.h> solo en
 * el parser de archivo, protegido con #ifdef PIC18_HOST).
 *
 * ---------------------------------------------------------------------------
 * MEMORIA DE PROGRAMA (importante para Flipper):
 *   El buffer de programa `prog` NO lo posee el core: es un puntero a un buffer
 *   asignado por el caller (p.ej. safe_malloc del tamano real del .hex, ~110KB)
 *   mas los config words. El core recibe prog como puntero + tamano via
 *   pic18_set_prog(). En host se puede asignar 0x20000 (128KB). En Flipper se
 *   asigna solo lo necesario. Direcciones fuera de [0, prog_size) devuelven
 *   0xFF al leer (igual que la zona no presente del Python).
 * ---------------------------------------------------------------------------
 */
#ifndef PIC18_CORE_H
#define PIC18_CORE_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ----------------------------------------------------------------- constantes */
#define PIC18_PROG_SIZE 0x20000u /* cubre hasta 0x1A180 con margen (host) */
#define PIC18_RAM_SIZE  0x1000u  /* data RAM + SFR (bancos 0..F)          */
#define PIC18_STACK_LEVELS 31    /* pila de retorno                        */

/* STATUS bit positions (PIC18) */
#define PIC18_ST_C  0
#define PIC18_ST_DC 1
#define PIC18_ST_Z  2
#define PIC18_ST_OV 3
#define PIC18_ST_N  4

/* Direcciones absolutas (en el espacio de datos) de SFR usados por la API */
#define PIC18_SFR_WREG    0xFE8u
#define PIC18_SFR_STATUS  0xFD8u
#define PIC18_SFR_BSR     0xFE0u
#define PIC18_SFR_PCLATH  0xFFAu
#define PIC18_SFR_PCLATU  0xFFBu

struct Pic18Cpu; /* fwd */

/* -------------------------------------------------------------- callbacks ---
 * Equivalentes a los hooks del python (on_port_read / on_lat_write /
 * on_port_write / trace de direccion). Cada uno lleva su propio void* ctx.
 */

/* Lectura de un PORTx. Debe devolver 0..255 para hacer override, o -1 para
 * dejar el comportamiento por defecto (default_port). addr es la direccion
 * absoluta del SFR (0xF80..0xF84). */
typedef int (*Pic18PortRead)(struct Pic18Cpu* cpu, uint16_t addr, void* ctx);

/* Escritura a un LATx o PORTx (bit-bang TX). addr es la direccion absoluta del
 * SFR; val es el byte escrito. */
typedef void (*Pic18PortWrite)(struct Pic18Cpu* cpu, uint16_t addr, uint8_t val, void* ctx);

/* Hook de codigo: se invoca al entrar (PC) en cualquier instruccion, con el pc
 * actual. Equivale al conjunto de hooks de direccion + trace del python; el
 * caller decide si filtra por pc. */
typedef void (*Pic18CodeHook)(struct Pic18Cpu* cpu, uint32_t pc, void* ctx);

/* ----------------------------------------------------------------- estado --- */
typedef struct Pic18Cpu {
    /* Memoria de programa: puntero propiedad del caller (ver cabecera). */
    uint8_t* prog;
    uint32_t prog_size;

    /* Config words 0x300000+ (como el self.config del python). Pequeno y
     * escaso: lo guardamos como tabla direccion->byte lineal. */
    uint32_t config_addr[64];
    uint8_t  config_val[64];
    uint16_t config_count;

    uint8_t ram[PIC18_RAM_SIZE]; /* data RAM + SFR */

    /* Registros nucleo */
    uint32_t pc;                 /* 21 bits */
    uint8_t  w;
    uint32_t stack[PIC18_STACK_LEVELS];
    uint8_t  stkptr;             /* numero de entradas usadas */
    uint8_t  ws;                 /* fast return shadow: WREG  */
    uint8_t  statuss;            /* fast return shadow: STATUS*/
    uint8_t  bsrs;               /* fast return shadow: BSR   */

    uint64_t cycles;
    uint64_t steps;
    uint8_t  halted;
    uint8_t  sleeping;

    /* Valor por defecto para lecturas de PORT no interceptadas */
    uint8_t  default_port;

    /* Callbacks + contextos */
    Pic18PortRead  on_port_read;
    void*          port_read_ctx;
    Pic18PortWrite on_lat_write;
    void*          lat_write_ctx;
    Pic18PortWrite on_port_write;
    void*          port_write_ctx;
    Pic18CodeHook  on_code;      /* trace/hook de direccion (opcional) */
    void*          code_ctx;
} Pic18Cpu;

/* ---------------------------------------------------------------- API ------- */

/* Inicializa la estructura a cero y pone prog = NULL. Llamar antes de todo. */
void pic18_init(Pic18Cpu* cpu);

/* Asigna el buffer de programa (propiedad del caller). size en bytes. */
void pic18_set_prog(Pic18Cpu* cpu, uint8_t* prog, uint32_t size);

/* Reset del nucleo (PC=0, SFR a POR tipicos, RAM a 0, TRIS=0xFF). */
void pic18_reset(Pic18Cpu* cpu);

/* Carga Intel HEX desde un buffer en memoria (el .hex leido a RAM). Llena
 * prog[] por direccion real y guarda config words 0x300000+. Devuelve 0 OK,
 * <0 en error de formato. Requiere prog ya asignado (pic18_set_prog). */
int pic18_load_hex_mem(Pic18Cpu* cpu, const uint8_t* hexdata, size_t size);

/* Carga un binario plano en prog[base..]. Devuelve 0 OK, <0 si no cabe. */
int pic18_load_prog(Pic18Cpu* cpu, const uint8_t* bin, size_t size, uint32_t base);

#ifdef PIC18_HOST
/* Carga Intel HEX desde un fichero (solo host). Devuelve 0 OK, <0 error. */
int pic18_load_hex(Pic18Cpu* cpu, const char* path);
#endif

/* Ejecuta una instruccion. Devuelve 1 si sigue vivo, 0 si halted. */
int pic18_step(Pic18Cpu* cpu);

/* Ejecuta hasta max_steps o halted. Devuelve numero de pasos ejecutados. */
uint64_t pic18_run(Pic18Cpu* cpu, uint64_t max_steps);

/* Acceso a memoria de datos con semantica de SFR (igual que read/write_data). */
uint8_t pic18_read_data(Pic18Cpu* cpu, uint16_t addr);
void    pic18_write_data(Pic18Cpu* cpu, uint16_t addr, uint8_t val);

/* Acceso crudo a RAM (sin semantica SFR), equivalente a read_ram/write_ram. */
uint8_t pic18_read_ram(Pic18Cpu* cpu, uint16_t addr);
void    pic18_write_ram(Pic18Cpu* cpu, uint16_t addr, uint8_t val);

/* SFR por direccion absoluta. get usa semantica de lectura (read_data);
 * set escribe crudo en ram[] (igual que set_sfr del python). */
uint8_t pic18_get_sfr_addr(Pic18Cpu* cpu, uint16_t addr);
void    pic18_set_sfr_addr(Pic18Cpu* cpu, uint16_t addr, uint8_t val);

/* SFR por nombre (p.ej. "STATUS"). Devuelve direccion 0 si no existe. */
uint16_t pic18_sfr_addr_by_name(const char* name);
int      pic18_get_sfr(Pic18Cpu* cpu, const char* name, uint8_t* out);
int      pic18_set_sfr(Pic18Cpu* cpu, const char* name, uint8_t val);

/* Helpers de estado frecuentes */
uint8_t  pic18_status(const Pic18Cpu* cpu);
uint8_t  pic18_bsr(const Pic18Cpu* cpu);
uint16_t pic18_fsr(const Pic18Cpu* cpu, int n);

/* Pila de retorno (para montar sentinelas de retorno en tests/harness). */
void     pic18_push(Pic18Cpu* cpu, uint32_t addr);
uint32_t pic18_pop(Pic18Cpu* cpu);

#ifdef __cplusplus
}
#endif

#endif /* PIC18_CORE_H */
