#pragma once
#include <errno.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void __clear_cache(void*, void*);
void* __aeabi_uldivmod(uint64_t, uint64_t);
double __aeabi_f2d(float);
/* Bit parity/popcount helpers emitted by the compiler for SubGhz/NFC protocol
 * code. Needed by ForceXIP apps (subghz, nfc) whose XIP fast-relocation resolves
 * these against the firmware at load time. See EXTERNAL_LIBS.md. */
int __paritysi2(unsigned int);
int __popcountsi2(unsigned int);

#ifdef __cplusplus
}
#endif
