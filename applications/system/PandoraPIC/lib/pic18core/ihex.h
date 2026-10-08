/*
 * ihex.h - Parser de Intel HEX (port de emu/ihex.py).
 *
 * Soporta direcciones extendidas (rtype 0x02 Extended Segment, 0x04 Extended
 * Linear), data (0x00), EOF (0x01) y los start-address 0x03/0x05 (ignorados).
 * Verifica checksum de cada linea. Preserva la DIRECCION LINEAL REAL de 32 bits
 * de cada byte (importante para PIC18: separar codigo <PROG_SIZE de los config
 * words 0x300000+).
 *
 * Sin dependencias fuera de <stdint.h>/<stddef.h>. El parser de FICHERO
 * (ihex_parse_file) solo existe bajo #ifdef PIC18_HOST (usa <stdio.h>). En
 * Flipper se usa ihex_parse_mem sobre el .hex ya leido a RAM.
 */
#ifndef IHEX_H
#define IHEX_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Codigos de error */
#define IHEX_OK            0
#define IHEX_ERR_FORMAT   -1   /* linea corta / hex invalido        */
#define IHEX_ERR_CHECKSUM -2   /* checksum malo                     */
#define IHEX_ERR_RTYPE    -3   /* record type desconocido           */
#define IHEX_ERR_IO       -4   /* fallo de E/S (solo host)          */

/*
 * Callback que recibe cada byte decodificado con su direccion lineal real.
 * ctx es el contexto opaco del caller. Equivale a mem[addr] = byte del python.
 */
typedef void (*IHexByteCb)(uint32_t addr, uint8_t value, void* ctx);

/*
 * Parsea Intel HEX desde un buffer en memoria. Invoca cb por cada byte de los
 * records de datos. Devuelve IHEX_OK o un codigo IHEX_ERR_*.
 */
int ihex_parse_mem(const uint8_t* data, size_t size, IHexByteCb cb, void* ctx);

#ifdef PIC18_HOST
/*
 * Parsea Intel HEX desde un fichero (solo host). Devuelve IHEX_OK o error.
 */
int ihex_parse_file(const char* path, IHexByteCb cb, void* ctx);
#endif

#ifdef __cplusplus
}
#endif

#endif /* IHEX_H */
