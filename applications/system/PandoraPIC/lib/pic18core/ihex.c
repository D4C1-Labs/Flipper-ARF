/*
 * ihex.c - Intel HEX parser (faithful port of emu/ihex.py).
 */
#include "ihex.h"

/* Converts an ascii hex digit to 0..15, or -1 if invalid. */
static int ihex_nibble(int c) {
    if(c >= '0' && c <= '9') return c - '0';
    if(c >= 'a' && c <= 'f') return c - 'a' + 10;
    if(c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/*
 * Processes a single Intel HEX line (without the leading ':' or the newline).
 * len = number of hex characters. Returns IHEX_OK or an error, and updates
 * *ext_lin / *ext_seg and *eof.
 */
static int ihex_line(
    const char* line,
    size_t len,
    uint32_t* ext_lin,
    uint32_t* ext_seg,
    int* eof,
    IHexByteCb cb,
    void* ctx) {
    /* We need at least count+addr(2)+type+chk = 5 bytes = 10 nibbles. */
    if(len < 10 || (len & 1)) return IHEX_ERR_FORMAT;

    uint8_t raw[260]; /* count max 255 + 5 header/chk */
    size_t nbytes = len / 2;
    if(nbytes > sizeof(raw)) return IHEX_ERR_FORMAT;

    for(size_t i = 0; i < nbytes; i++) {
        int hi = ihex_nibble((unsigned char)line[2 * i]);
        int lo = ihex_nibble((unsigned char)line[2 * i + 1]);
        if(hi < 0 || lo < 0) return IHEX_ERR_FORMAT;
        raw[i] = (uint8_t)((hi << 4) | lo);
    }

    uint8_t count = raw[0];
    uint16_t addr = (uint16_t)((raw[1] << 8) | raw[2]);
    uint8_t rtype = raw[3];

    /* full line: 4 header + count data + 1 chk */
    if(nbytes != (size_t)(5 + count)) return IHEX_ERR_FORMAT;

    /* checksum: sum of all bytes (including chk) & 0xFF == 0 */
    uint32_t sum = 0;
    for(size_t i = 0; i < nbytes; i++) sum += raw[i];
    if((sum & 0xFF) != 0) return IHEX_ERR_CHECKSUM;

    const uint8_t* dat = &raw[4];

    switch(rtype) {
    case 0x00: { /* data */
        uint32_t base = *ext_lin + *ext_seg;
        for(uint8_t i = 0; i < count; i++) {
            cb(base + addr + i, dat[i], ctx);
        }
        break;
    }
    case 0x01: /* EOF */
        *eof = 1;
        break;
    case 0x02: /* Extended Segment Address */
        if(count < 2) return IHEX_ERR_FORMAT;
        *ext_seg = (uint32_t)(((dat[0] << 8) | dat[1])) << 4;
        break;
    case 0x04: /* Extended Linear Address */
        if(count < 2) return IHEX_ERR_FORMAT;
        *ext_lin = (uint32_t)(((dat[0] << 8) | dat[1])) << 16;
        break;
    case 0x03: /* Start Segment Address  - ignore */
    case 0x05: /* Start Linear Address   - ignore */
        break;
    default:
        return IHEX_ERR_RTYPE;
    }
    return IHEX_OK;
}

int ihex_parse_mem(const uint8_t* data, size_t size, IHexByteCb cb, void* ctx) {
    uint32_t ext_lin = 0;
    uint32_t ext_seg = 0;
    int eof = 0;
    size_t i = 0;

    while(i < size) {
        /* locate the start of a line: look for ':' */
        /* skip preceding spaces/carriage returns */
        while(i < size && (data[i] == '\r' || data[i] == '\n' ||
                           data[i] == ' ' || data[i] == '\t')) {
            i++;
        }
        if(i >= size) break;
        if(data[i] != ':') {
            /* lines that don't start with ':' are ignored (like the python) */
            while(i < size && data[i] != '\n') i++;
            continue;
        }
        i++; /* skip ':' */
        size_t start = i;
        while(i < size && data[i] != '\n' && data[i] != '\r') i++;
        size_t len = i - start;
        /* trim trailing spaces */
        while(len > 0 && (data[start + len - 1] == ' ' ||
                          data[start + len - 1] == '\t')) {
            len--;
        }
        if(len == 0) continue;

        int rc = ihex_line(
            (const char*)&data[start], len, &ext_lin, &ext_seg, &eof, cb, ctx);
        if(rc != IHEX_OK) return rc;
        if(eof) break;
    }
    return IHEX_OK;
}

#ifdef PIC18_HOST
#include <stdio.h>
#include <stdlib.h>

int ihex_parse_file(const char* path, IHexByteCb cb, void* ctx) {
    FILE* fh = fopen(path, "rb");
    if(!fh) return IHEX_ERR_IO;
    if(fseek(fh, 0, SEEK_END) != 0) {
        fclose(fh);
        return IHEX_ERR_IO;
    }
    long sz = ftell(fh);
    if(sz < 0) {
        fclose(fh);
        return IHEX_ERR_IO;
    }
    rewind(fh);
    uint8_t* buf = (uint8_t*)malloc((size_t)sz);
    if(!buf) {
        fclose(fh);
        return IHEX_ERR_IO;
    }
    size_t rd = fread(buf, 1, (size_t)sz, fh);
    fclose(fh);
    if(rd != (size_t)sz) {
        free(buf);
        return IHEX_ERR_IO;
    }
    int rc = ihex_parse_mem(buf, (size_t)sz, cb, ctx);
    free(buf);
    return rc;
}
#endif
