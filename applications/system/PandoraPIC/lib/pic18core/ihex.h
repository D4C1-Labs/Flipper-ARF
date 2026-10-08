/*
 * ihex.h - Intel HEX parser (port of emu/ihex.py).
 *
 * Supports extended addresses (rtype 0x02 Extended Segment, 0x04 Extended
 * Linear), data (0x00), EOF (0x01) and the start-address records 0x03/0x05
 * (ignored). Verifies the checksum of each line. Preserves the REAL 32-bit
 * LINEAR ADDRESS of each byte (important for PIC18: separating code <PROG_SIZE
 * from the config words 0x300000+).
 *
 * No dependencies beyond <stdint.h>/<stddef.h>. The FILE parser
 * (ihex_parse_file) only exists under #ifdef PIC18_HOST (uses <stdio.h>). On
 * the Flipper, ihex_parse_mem is used over the .hex already read into RAM.
 */
#ifndef IHEX_H
#define IHEX_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Error codes */
#define IHEX_OK            0
#define IHEX_ERR_FORMAT   -1   /* short line / invalid hex          */
#define IHEX_ERR_CHECKSUM -2   /* bad checksum                      */
#define IHEX_ERR_RTYPE    -3   /* unknown record type               */
#define IHEX_ERR_IO       -4   /* I/O failure (host only)           */

/*
 * Callback that receives each decoded byte with its real linear address.
 * ctx is the caller's opaque context. Equivalent to mem[addr] = byte in python.
 */
typedef void (*IHexByteCb)(uint32_t addr, uint8_t value, void* ctx);

/*
 * Parses Intel HEX from a buffer in memory. Invokes cb for each byte of the
 * data records. Returns IHEX_OK or an IHEX_ERR_* code.
 */
int ihex_parse_mem(const uint8_t* data, size_t size, IHexByteCb cb, void* ctx);

#ifdef PIC18_HOST
/*
 * Parses Intel HEX from a file (host only). Returns IHEX_OK or an error.
 */
int ihex_parse_file(const char* path, IHexByteCb cb, void* ctx);
#endif

#ifdef __cplusplus
}
#endif

#endif /* IHEX_H */
