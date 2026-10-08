/*
 * test_host.c - Harness de verificacion del core PIC18 en C (solo host).
 *
 * Modos:
 *   test_host trace <file.hex> <steps> [out]
 *       Vuelca una traza (step pc op w status bsr stkptr fsr0 fsr1 fsr2 ramhash)
 *       con el MISMO formato que emu/dump_trace.py, para diff exacto.
 *
 *   test_host keeloq <MariO_2.hex>
 *       Ejecuta el nucleo KeeLoq @0x151C con PT=0x12345678 KEY=0x0123456789ABCDEF
 *       y imprime el CT. Referencia python: 0xCEE8C947.
 *
 *   test_host pin <mario_full_pin2552.hex> <d0> <d1> <d2> <d3>
 *       Carga el buffer de PIN y ejecuta la validacion @0x18D76; imprime
 *       ACCEPT/REJECT segun el hook de direccion.
 */
#include "pic18_core.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* FNV-1a 32-bit sobre los 0x1000 bytes de RAM (igual que dump_trace.py) */
static uint32_t fnv1a(const uint8_t* data, size_t n) {
    uint32_t h = 0x811C9DC5u;
    for(size_t i = 0; i < n; i++) {
        h ^= data[i];
        h *= 0x01000193u;
    }
    return h;
}

static uint8_t* alloc_prog(void) {
    uint8_t* p = (uint8_t*)malloc(PIC18_PROG_SIZE);
    if(p) memset(p, 0xFF, PIC18_PROG_SIZE);
    return p;
}

static int cmd_trace(const char* hexpath, uint64_t steps, const char* out) {
    uint8_t* prog = alloc_prog();
    if(!prog) { fprintf(stderr, "malloc\n"); return 2; }
    Pic18Cpu cpu;
    pic18_init(&cpu);
    pic18_set_prog(&cpu, prog, PIC18_PROG_SIZE);
    if(pic18_load_hex(&cpu, hexpath) != 0) {
        fprintf(stderr, "no se pudo cargar %s\n", hexpath);
        free(prog);
        return 2;
    }
    pic18_reset(&cpu);

    FILE* fh = out ? fopen(out, "w") : stdout;
    if(!fh) { fprintf(stderr, "no se pudo abrir %s\n", out); free(prog); return 2; }

    uint64_t n = 0;
    while(n < steps && !cpu.halted) {
        uint32_t pc = cpu.pc;
        uint16_t op = (uint16_t)(cpu.prog[pc] | (cpu.prog[pc + 1] << 8));
        uint32_t rh = fnv1a(cpu.ram, PIC18_RAM_SIZE);
        fprintf(fh, "%llu %06X %04X %02X %02X %X %02X %03X %03X %03X %08X\n",
                (unsigned long long)cpu.steps, (unsigned)pc, (unsigned)op, (unsigned)cpu.w,
                (unsigned)pic18_status(&cpu), (unsigned)pic18_bsr(&cpu), (unsigned)cpu.stkptr,
                (unsigned)pic18_fsr(&cpu, 0), (unsigned)pic18_fsr(&cpu, 1),
                (unsigned)pic18_fsr(&cpu, 2), (unsigned)rh);
        pic18_step(&cpu);
        n++;
    }
    if(out) fclose(fh);
    free(prog);
    return 0;
}

/* --- KeeLoq: replica de emu/keeloq_dynamic.run_core --- */
static int cmd_keeloq(const char* hexpath) {
    uint8_t* prog = alloc_prog();
    if(!prog) return 2;
    Pic18Cpu cpu;
    pic18_init(&cpu);
    pic18_set_prog(&cpu, prog, PIC18_PROG_SIZE);
    if(pic18_load_hex(&cpu, hexpath) != 0) { free(prog); return 2; }
    pic18_reset(&cpu);

    uint32_t pt = 0x12345678u;
    unsigned long long key = 0x0123456789ABCDEFULL;
    for(int i = 0; i < 4; i++) pic18_write_ram(&cpu, 0x91 + i, (pt >> (8 * i)) & 0xFF);
    for(int i = 0; i < 8; i++) pic18_write_ram(&cpu, 0x1B2 + i, (key >> (8 * i)) & 0xFF);
    pic18_set_sfr(&cpu, "BSR", 0);
    cpu.pc = 0x00151C;
    cpu.stkptr = 0;
    pic18_push(&cpu, 0x000002); /* sentinela de retorno */

    int rounds = 0;
    for(long i = 0; i < 2000000; i++) {
        if(cpu.pc == 0x000002) break;
        uint32_t pc = cpu.pc;
        pic18_step(&cpu);
        if(pc == 0x0015D8) rounds++;
    }
    uint32_t out = 0;
    for(int i = 0; i < 4; i++) out |= (uint32_t)pic18_read_ram(&cpu, 0x91 + i) << (8 * i);
    printf("KEELOQ CT=0x%08X rounds=%d\n", (unsigned)out, rounds);
    free(prog);
    return 0;
}

/* --- PIN: replica de emu/pin_trace.run_pin(from_boot=False) --- */
static int g_accept = 0, g_reject = 0;
static void pin_hook(Pic18Cpu* c, uint32_t pc, void* ctx) {
    (void)c; (void)ctx;
    if(pc == 0x018DA8) g_accept = 1;
    if(pc == 0x018DE2) g_reject = 1;
    if(pc == 0x01FFFE) c->halted = 1; /* sentinela */
}

static int cmd_pin(const char* hexpath, int d0, int d1, int d2, int d3) {
    uint8_t* prog = alloc_prog();
    if(!prog) return 2;
    Pic18Cpu cpu;
    pic18_init(&cpu);
    pic18_set_prog(&cpu, prog, PIC18_PROG_SIZE);
    if(pic18_load_hex(&cpu, hexpath) != 0) { free(prog); return 2; }
    pic18_reset(&cpu);

    int digits[4] = {d0, d1, d2, d3};
    uint16_t slots_lo[4] = {0x1D9, 0x1DB, 0x1DD, 0x1DF};
    uint16_t slots_hi[4] = {0x1DA, 0x1DC, 0x1DE, 0x1E0};
    for(int i = 0; i < 4; i++) {
        pic18_write_ram(&cpu, slots_lo[i], digits[i] & 0xFF);
        pic18_write_ram(&cpu, slots_hi[i], (digits[i] >> 8) & 0xFF);
    }
    pic18_write_ram(&cpu, 0x1D7, 0);
    pic18_write_ram(&cpu, 0x1D8, 0);

    g_accept = 0;
    g_reject = 0;
    cpu.on_code = pin_hook;
    cpu.code_ctx = NULL;

    cpu.pc = 0x018D76;
    pic18_set_sfr(&cpu, "BSR", 0);
    pic18_push(&cpu, 0x01FFFE);
    pic18_run(&cpu, 2000000);

    printf("PIN %d%d%d%d ACCEPT=%d REJECT=%d\n", d0, d1, d2, d3, g_accept, g_reject);
    free(prog);
    return 0;
}

int main(int argc, char** argv) {
    if(argc < 2) {
        fprintf(stderr, "uso: %s trace|keeloq|pin ...\n", argv[0]);
        return 1;
    }
    if(strcmp(argv[1], "trace") == 0 && argc >= 4) {
        const char* out = (argc >= 5) ? argv[4] : NULL;
        return cmd_trace(argv[2], strtoull(argv[3], NULL, 10), out);
    }
    if(strcmp(argv[1], "keeloq") == 0 && argc >= 3) {
        return cmd_keeloq(argv[2]);
    }
    if(strcmp(argv[1], "pin") == 0 && argc >= 7) {
        return cmd_pin(argv[2], atoi(argv[3]), atoi(argv[4]), atoi(argv[5]), atoi(argv[6]));
    }
    fprintf(stderr, "argumentos invalidos\n");
    return 1;
}
