/*
 * thumb_core.c - Thumb-2 (ARMv7-M / Cortex-M3) interpreter in pure C.
 *
 * Verified step by step against Unicorn Engine (CS_ARCH_ARM/CS_MODE_THUMB,
 * Cortex-M3 CPU) running the real Pandora keyfob firmware.
 *
 * Encoding references: ARMv7-M Architecture Reference Manual (DDI 0403),
 * chapters A5 (Thumb instruction set encoding) and A7 (instruction details).
 */
#include "thumb_core.h"
#include <string.h>

/* ========================================================================= */
/* Register helpers                                                          */
/* ========================================================================= */

#define PC (c->r[15])
#define SP (c->r[13])
#define LR (c->r[14])

/* Register read with PC semantics (returns aligned PC + 0 because the core
 * already keeps PC = address of the current instruction + 4 during the
 * computation). For most instructions that read Rn, use c->r[n]. */
static inline uint32_t reg_read(ThumbCore* c, int n) {
    return c->r[n];
}

/* ========================================================================= */
/* Memory access                                                             */
/* ========================================================================= */

static inline uint32_t mem_read(ThumbCore* c, uint32_t addr, int size) {
    /* Direct FLASH */
    if(c->flash_ptr && addr >= c->flash_base && (addr + (uint32_t)size) <= c->flash_base + c->flash_size) {
        uint8_t* p = c->flash_ptr + (addr - c->flash_base);
        switch(size) {
        case 1: return p[0];
        case 2: return (uint32_t)p[0] | ((uint32_t)p[1] << 8);
        default: return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
        }
    }
    /* Direct RAM */
    if(c->ram_ptr && addr >= c->ram_base && (addr + (uint32_t)size) <= c->ram_base + c->ram_size) {
        uint8_t* p = c->ram_ptr + (addr - c->ram_base);
        switch(size) {
        case 1: return p[0];
        case 2: return (uint32_t)p[0] | ((uint32_t)p[1] << 8);
        default: return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
        }
    }
    if(c->read_cb) return c->read_cb(c->mem_ctx, addr, size);
    return 0;
}

static inline void mem_write(ThumbCore* c, uint32_t addr, uint32_t val, int size) {
    /* Direct RAM (reflect writes). FLASH is typically not written. */
    if(c->ram_ptr && addr >= c->ram_base && (addr + (uint32_t)size) <= c->ram_base + c->ram_size) {
        uint8_t* p = c->ram_ptr + (addr - c->ram_base);
        switch(size) {
        case 1: p[0] = (uint8_t)val; break;
        case 2: p[0] = (uint8_t)val; p[1] = (uint8_t)(val >> 8); break;
        default:
            p[0] = (uint8_t)val; p[1] = (uint8_t)(val >> 8);
            p[2] = (uint8_t)(val >> 16); p[3] = (uint8_t)(val >> 24); break;
        }
        return;
    }
    if(c->write_cb) c->write_cb(c->mem_ctx, addr, val, size);
}

uint32_t thumb_read_mem(ThumbCore* c, uint32_t addr, int size) { return mem_read(c, addr, size); }
void thumb_write_mem(ThumbCore* c, uint32_t addr, uint32_t val, int size) { mem_write(c, addr, val, size); }

/* ========================================================================= */
/* Flags                                                                     */
/* ========================================================================= */

static inline void set_nz(ThumbCore* c, uint32_t res) {
    c->xpsr &= ~(THUMB_PSR_N | THUMB_PSR_Z);
    if(res & 0x80000000u) c->xpsr |= THUMB_PSR_N;
    if(res == 0) c->xpsr |= THUMB_PSR_Z;
}
static inline void set_c(ThumbCore* c, int carry) {
    if(carry) c->xpsr |= THUMB_PSR_C; else c->xpsr &= ~THUMB_PSR_C;
}
static inline void set_v(ThumbCore* c, int ov) {
    if(ov) c->xpsr |= THUMB_PSR_V; else c->xpsr &= ~THUMB_PSR_V;
}
static inline int get_c(ThumbCore* c) { return (c->xpsr & THUMB_PSR_C) ? 1 : 0; }

/* ARMv7-M AddWithCarry: returns result, sets carry_out/overflow_out. */
static inline uint32_t add_with_carry(uint32_t x, uint32_t y, uint32_t cin,
                                      int* carry_out, int* overflow_out) {
    uint64_t usum = (uint64_t)x + (uint64_t)y + (uint64_t)cin;
    int64_t ssum = (int64_t)(int32_t)x + (int64_t)(int32_t)y + (int64_t)cin;
    uint32_t res = (uint32_t)usum;
    *carry_out = (usum >> 32) & 1;
    *overflow_out = ((int64_t)(int32_t)res != ssum) ? 1 : 0;
    return res;
}

/* ========================================================================= */
/* Shifts / rotations (with carry out)                                       */
/* ========================================================================= */

enum { SRTYPE_LSL = 0, SRTYPE_LSR = 1, SRTYPE_ASR = 2, SRTYPE_ROR = 3, SRTYPE_RRX = 4 };

/* ARMv7-M Shift_C. amount in 0..255 (already computed). carry_in only for RRX
 * and for the LSL #0 case (which does not change carry; handled by the caller). */
static uint32_t shift_c(uint32_t val, int type, int amount, int carry_in, int* carry_out) {
    if(amount == 0) {
        *carry_out = carry_in;
        return val;
    }
    switch(type) {
    case SRTYPE_LSL:
        if(amount >= 32) { *carry_out = (amount == 32) ? (val & 1) : 0; return 0; }
        *carry_out = (val >> (32 - amount)) & 1;
        return val << amount;
    case SRTYPE_LSR:
        if(amount >= 32) { *carry_out = (amount == 32) ? ((val >> 31) & 1) : 0; return 0; }
        *carry_out = (val >> (amount - 1)) & 1;
        return val >> amount;
    case SRTYPE_ASR:
        if(amount >= 32) { *carry_out = (val >> 31) & 1; return (val & 0x80000000u) ? 0xFFFFFFFFu : 0; }
        *carry_out = (val >> (amount - 1)) & 1;
        return (uint32_t)((int32_t)val >> amount);
    case SRTYPE_ROR: {
        amount &= 31;
        if(amount == 0) { *carry_out = (val >> 31) & 1; return val; }
        uint32_t r = (val >> amount) | (val << (32 - amount));
        *carry_out = (r >> 31) & 1;
        return r;
    }
    case SRTYPE_RRX: {
        *carry_out = val & 1;
        return (val >> 1) | ((uint32_t)carry_in << 31);
    }
    }
    *carry_out = carry_in;
    return val;
}

/* ========================================================================= */
/* Thumb immediate expansion (ThumbExpandImm_C)                              */
/* ========================================================================= */

/* imm12 -> expanded value + carry. */
static uint32_t thumb_expand_imm_c(uint32_t imm12, int carry_in, int* carry_out) {
    if((imm12 & 0xC00) == 0) {
        uint32_t imm8 = imm12 & 0xFF;
        *carry_out = carry_in;
        switch((imm12 >> 8) & 3) {
        case 0: return imm8;
        case 1: return (imm8 << 16) | imm8;
        case 2: return (imm8 << 24) | (imm8 << 8);
        default: return (imm8 << 24) | (imm8 << 16) | (imm8 << 8) | imm8;
        }
    } else {
        uint32_t unrotated = 0x80u | (imm12 & 0x7F);
        int rot = (imm12 >> 7) & 0x1F;
        uint32_t r = (unrotated >> rot) | (unrotated << (32 - rot));
        *carry_out = (r >> 31) & 1;
        return r;
    }
}

/* ========================================================================= */
/* Conditions                                                                */
/* ========================================================================= */

static int cond_passed(ThumbCore* c, int cond) {
    int n = (c->xpsr & THUMB_PSR_N) ? 1 : 0;
    int z = (c->xpsr & THUMB_PSR_Z) ? 1 : 0;
    int cf = (c->xpsr & THUMB_PSR_C) ? 1 : 0;
    int v = (c->xpsr & THUMB_PSR_V) ? 1 : 0;
    int res;
    switch(cond >> 1) {
    case 0: res = z; break;                 /* EQ/NE */
    case 1: res = cf; break;                /* CS/CC */
    case 2: res = n; break;                 /* MI/PL */
    case 3: res = v; break;                 /* VS/VC */
    case 4: res = cf && !z; break;          /* HI/LS */
    case 5: res = (n == v); break;          /* GE/LT */
    case 6: res = (n == v) && !z; break;    /* GT/LE */
    default: res = 1; break;                /* AL */
    }
    if((cond & 1) && cond != 0xF) res = !res;
    return res;
}

/* ========================================================================= */
/* IT block                                                                  */
/* ========================================================================= */

/* IT state representation following ARMv7-M ITSTATE (8 bits), stored in
 * c->it_cond. ITSTATE<7:4> = firstcond, ITSTATE<3:0> = mask (as the IT sets it).
 * c->it_mask is used only as an "active block" flag (!=0). The effective
 * condition of the current instruction is ITSTATE<7:4>. ITAdvance: if
 * ITSTATE<2:0>==000 the block ends; otherwise ITSTATE<4:0> <<= 1. */
static inline int in_it_block(ThumbCore* c) { return c->it_mask != 0; }

/* Effective condition (4 bits) = ITSTATE<7:4>. */
static inline int it_current_cond(ThumbCore* c) {
    return (c->it_cond >> 4) & 0xF;
}

/* Advances the IT state after executing an instruction of the block. */
static void it_advance(ThumbCore* c) {
    uint8_t it = c->it_cond; /* ITSTATE 8 bits */
    if((it & 0x7) == 0) {
        c->it_cond = 0;
        c->it_mask = 0;
    } else {
        uint8_t low5 = (uint8_t)((it << 1) & 0x1F);
        c->it_cond = (uint8_t)((it & 0xE0) | low5);
        c->it_mask = 1; /* still active */
    }
}

/* ========================================================================= */
/* PC push/pop                                                               */
/* ========================================================================= */

/* Writes PC (bit0 = Thumb selection; in ARMv7-M always Thumb). Marks branch. */
static inline void branch_to(ThumbCore* c, uint32_t addr) {
    PC = addr & ~1u;
    c->branched = 1;
}

/* ========================================================================= */
/* 16-bit decoding                                                           */
/* ========================================================================= */

static int exec_thumb16(ThumbCore* c, uint16_t op);
static int exec_thumb32(ThumbCore* c, uint16_t hw1, uint16_t hw2);

/* Determines whether a halfword is the first hw of a 32-bit instruction. */
static inline int is_32bit(uint16_t hw) {
    uint16_t hi = hw >> 11;
    return (hi == 0x1D || hi == 0x1E || hi == 0x1F); /* 0b11101/11110/11111 */
}

/* ========================================================================= */
/* thumb_step                                                                */
/* ========================================================================= */

int thumb_step(ThumbCore* c) {
    if(c->halted) { c->last_fault = THUMB_FAULT_HALT; return THUMB_FAULT_HALT; }

    uint32_t pc_instr = PC;
    if(c->hook) c->hook(c->hook_ctx, pc_instr);

    uint16_t hw1 = (uint16_t)mem_read(c, pc_instr, 2);

    /* During execution, PC must be read as (instr + 4). */
    int ret;
    int cond_ok = 1;
    int it_active = in_it_block(c);
    if(it_active) {
        cond_ok = cond_passed(c, it_current_cond(c));
    }

    /* In ARMv7-M, PC read during execution equals (instr + 4) for both 16-bit
     * and 32-bit instructions (PC-relative reads). We set PC to instr+4 and
     * detect whether the executed instruction modified it (branch). If it did
     * not modify it, the next PC is instr + instr_size. */
    uint32_t pc_during = pc_instr + 4;
    uint32_t instr_size;
    c->branched = 0;
    if(is_32bit(hw1)) {
        uint16_t hw2 = (uint16_t)mem_read(c, pc_instr + 2, 2);
        instr_size = 4;
        PC = pc_during;
        if(cond_ok) {
            ret = exec_thumb32(c, hw1, hw2);
        } else {
            ret = THUMB_OK; /* IT condition fails: NOP, but PC advances */
        }
    } else {
        instr_size = 2;
        PC = pc_during;
        if(cond_ok) {
            ret = exec_thumb16(c, hw1);
        } else {
            ret = THUMB_OK; /* IT condition fails: NOP, but PC advances */
        }
    }
    /* If the instruction did not branch, the next PC is instr + size. If it
     * branched, PC already landed at the target (branch_to set branched=1). */
    if(!c->branched) {
        PC = pc_instr + instr_size;
    }
    if(it_active) it_advance(c);

    c->cycles++;
    if(ret < 0) { c->last_fault = ret; }
    return ret;
}

uint64_t thumb_run(ThumbCore* c, uint64_t max) {
    uint64_t n = 0;
    while(n < max) {
        if(thumb_step(c) < 0) break;
        n++;
    }
    return n;
}

/* ========================================================================= */
/* Implementation of 16-bit instructions                                     */
/* ========================================================================= */

/* For correct handling of IT with the IT instruction itself, we use a flag:
 * when exec_thumb16 executes an IT, it leaves it_just_set=1 so that the step
 * does not call it_advance. We implement it with a static variable in the core. */

static int exec_thumb16(ThumbCore* c, uint16_t op) {
    uint32_t top6 = op >> 10;

    /* ---- Shift (imm), add, sub, mov, cmp  (00xxxx) ----
     * bits 13:11 (= g): 0,1,2 = LSL/LSR/ASR imm ; 3 = ADD/SUB reg/imm3 ;
     * 4,5,6,7 = MOV/CMP/ADD/SUB imm8. */
    if((op >> 14) == 0) {
        uint32_t g = (op >> 11) & 0x7;
        if(g <= 2) {
            /* LSL/LSR/ASR (immediate) : 000 op(2) imm5 Rm Rd */
            int type = (op >> 11) & 3; /* 0 lsl,1 lsr,2 asr */
            int imm5 = (op >> 6) & 0x1F;
            int rm = (op >> 3) & 7;
            int rd = op & 7;
            int shtype = (type == 0) ? SRTYPE_LSL : (type == 1) ? SRTYPE_LSR : SRTYPE_ASR;
            int amount = imm5;
            if((type == 1 || type == 2) && imm5 == 0) amount = 32; /* LSR/ASR #0 => 32 */
            int co;
            uint32_t res = shift_c(c->r[rm], shtype, amount, get_c(c), &co);
            c->r[rd] = res;
            if(!in_it_block(c)) { set_nz(c, res); set_c(c, co); }
            return THUMB_OK;
        } else if(g == 3) {
            /* 00011 xx : ADD/SUB register or 3-bit immediate */
            int sub = (op >> 9) & 1;
            int imm = (op >> 10) & 1; /* bit10: 1=imm3, 0=reg */
            int rn = (op >> 3) & 7;
            int rd = op & 7;
            uint32_t a = c->r[rn];
            uint32_t b = imm ? (uint32_t)((op >> 6) & 7) : c->r[(op >> 6) & 7];
            int co, vo;
            uint32_t res;
            if(sub) res = add_with_carry(a, ~b, 1, &co, &vo);
            else    res = add_with_carry(a, b, 0, &co, &vo);
            c->r[rd] = res;
            if(!in_it_block(c)) { set_nz(c, res); set_c(c, co); set_v(c, vo); }
            return THUMB_OK;
        }
        /* g = 4..7 : 001xx MOV/CMP/ADD/SUB (immediate 8) Rdn */
        {
            uint32_t sub2 = (op >> 11) & 3;
            int rdn = (op >> 8) & 7;
            uint32_t imm8 = op & 0xFF;
            int co, vo;
            uint32_t res;
            switch(sub2) {
            case 0: /* MOV imm */
                res = imm8;
                c->r[rdn] = res;
                if(!in_it_block(c)) { set_nz(c, res); }
                return THUMB_OK;
            case 1: /* CMP imm */
                res = add_with_carry(c->r[rdn], ~imm8, 1, &co, &vo);
                set_nz(c, res); set_c(c, co); set_v(c, vo);
                return THUMB_OK;
            case 2: /* ADD imm */
                res = add_with_carry(c->r[rdn], imm8, 0, &co, &vo);
                c->r[rdn] = res;
                if(!in_it_block(c)) { set_nz(c, res); set_c(c, co); set_v(c, vo); }
                return THUMB_OK;
            default: /* SUB imm */
                res = add_with_carry(c->r[rdn], ~imm8, 1, &co, &vo);
                c->r[rdn] = res;
                if(!in_it_block(c)) { set_nz(c, res); set_c(c, co); set_v(c, vo); }
                return THUMB_OK;
            }
        }
    }

    /* ---- Data processing (010000) ---- */
    if(top6 == 0x10) {
        int opc = (op >> 6) & 0xF;
        int rm = (op >> 3) & 7;
        int rdn = op & 7;
        int co, vo;
        uint32_t res;
        int setflags = !in_it_block(c);
        switch(opc) {
        case 0x0: /* AND */ res = c->r[rdn] & c->r[rm]; c->r[rdn] = res; if(setflags) set_nz(c, res); break;
        case 0x1: /* EOR */ res = c->r[rdn] ^ c->r[rm]; c->r[rdn] = res; if(setflags) set_nz(c, res); break;
        case 0x2: /* LSL reg */ {
            int amt = c->r[rm] & 0xFF;
            res = shift_c(c->r[rdn], SRTYPE_LSL, amt, get_c(c), &co);
            c->r[rdn] = res; if(setflags) { set_nz(c, res); set_c(c, co); }
            break; }
        case 0x3: /* LSR reg */ {
            int amt = c->r[rm] & 0xFF;
            res = shift_c(c->r[rdn], SRTYPE_LSR, amt, get_c(c), &co);
            c->r[rdn] = res; if(setflags) { set_nz(c, res); set_c(c, co); }
            break; }
        case 0x4: /* ASR reg */ {
            int amt = c->r[rm] & 0xFF;
            res = shift_c(c->r[rdn], SRTYPE_ASR, amt, get_c(c), &co);
            c->r[rdn] = res; if(setflags) { set_nz(c, res); set_c(c, co); }
            break; }
        case 0x5: /* ADC */ res = add_with_carry(c->r[rdn], c->r[rm], get_c(c), &co, &vo);
            c->r[rdn] = res; if(setflags) { set_nz(c, res); set_c(c, co); set_v(c, vo); } break;
        case 0x6: /* SBC */ res = add_with_carry(c->r[rdn], ~c->r[rm], get_c(c), &co, &vo);
            c->r[rdn] = res; if(setflags) { set_nz(c, res); set_c(c, co); set_v(c, vo); } break;
        case 0x7: /* ROR reg */ {
            int amt = c->r[rm] & 0xFF;
            res = shift_c(c->r[rdn], SRTYPE_ROR, amt, get_c(c), &co);
            c->r[rdn] = res; if(setflags) { set_nz(c, res); set_c(c, co); }
            break; }
        case 0x8: /* TST */ res = c->r[rdn] & c->r[rm]; set_nz(c, res); break;
        case 0x9: /* RSB #0 (NEG) */ res = add_with_carry(~c->r[rm], 0, 1, &co, &vo);
            c->r[rdn] = res; if(setflags) { set_nz(c, res); set_c(c, co); set_v(c, vo); } break;
        case 0xA: /* CMP */ res = add_with_carry(c->r[rdn], ~c->r[rm], 1, &co, &vo);
            set_nz(c, res); set_c(c, co); set_v(c, vo); break;
        case 0xB: /* CMN */ res = add_with_carry(c->r[rdn], c->r[rm], 0, &co, &vo);
            set_nz(c, res); set_c(c, co); set_v(c, vo); break;
        case 0xC: /* ORR */ res = c->r[rdn] | c->r[rm]; c->r[rdn] = res; if(setflags) set_nz(c, res); break;
        case 0xD: /* MUL */ res = c->r[rdn] * c->r[rm]; c->r[rdn] = res; if(setflags) set_nz(c, res); break;
        case 0xE: /* BIC */ res = c->r[rdn] & ~c->r[rm]; c->r[rdn] = res; if(setflags) set_nz(c, res); break;
        default:  /* 0xF MVN */ res = ~c->r[rm]; c->r[rdn] = res; if(setflags) set_nz(c, res); break;
        }
        return THUMB_OK;
    }

    /* ---- Special data / BX/BLX (010001) ---- */
    if(top6 == 0x11) {
        int opc = (op >> 8) & 3;
        if(opc == 3) {
            /* BX / BLX reg */
            int rm = (op >> 3) & 0xF;
            int link = (op >> 7) & 1;
            uint32_t target = c->r[rm];
            if(link) {
                /* BLX reg (16-bit): PC = pc_instr+4 during exec; return
                 * address = pc_instr+2 = PC-2. */
                LR = (PC - 2) | 1;
                branch_to(c, target);
            } else {
                branch_to(c, target); /* BX */
            }
            return THUMB_OK;
        }
        /* ADD/CMP/MOV (high registers) */
        int dn = ((op >> 7) & 1);
        int rm = (op >> 3) & 0xF;
        int rd = (op & 7) | (dn << 3);
        uint32_t rmv = (rm == 15) ? ((PC) & ~1u) : c->r[rm];
        uint32_t rdv = (rd == 15) ? ((PC) & ~1u) : c->r[rd];
        int co, vo;
        switch(opc) {
        case 0: { /* ADD */
            uint32_t res = rdv + rmv;
            if(rd == 15) { branch_to(c, res); }
            else c->r[rd] = res;
            break; }
        case 1: { /* CMP (always sets flags) */
            uint32_t res = add_with_carry(rdv, ~rmv, 1, &co, &vo);
            set_nz(c, res); set_c(c, co); set_v(c, vo);
            break; }
        case 2: { /* MOV */
            if(rd == 15) { branch_to(c, rmv); }
            else c->r[rd] = rmv;
            break; }
        default: break;
        }
        return THUMB_OK;
    }

    /* ---- LDR literal (01001) ---- */
    if((op >> 11) == 0x09) {
        int rt = (op >> 8) & 7;
        uint32_t imm8 = (op & 0xFF) << 2;
        uint32_t base = (PC) & ~3u; /* Align(PC,4); PC = instr+4 */
        c->r[rt] = mem_read(c, base + imm8, 4);
        return THUMB_OK;
    }

    /* ---- Load/store register offset & byte/halfword (0101) ---- */
    if((op >> 12) == 0x5) {
        int opb = (op >> 9) & 7;
        int rm = (op >> 6) & 7;
        int rn = (op >> 3) & 7;
        int rt = op & 7;
        uint32_t addr = c->r[rn] + c->r[rm];
        switch(opb) {
        case 0: mem_write(c, addr, c->r[rt], 4); break;             /* STR */
        case 1: mem_write(c, addr, c->r[rt] & 0xFFFF, 2); break;    /* STRH */
        case 2: mem_write(c, addr, c->r[rt] & 0xFF, 1); break;      /* STRB */
        case 3: c->r[rt] = (uint32_t)(int32_t)(int8_t)mem_read(c, addr, 1); break; /* LDRSB */
        case 4: c->r[rt] = mem_read(c, addr, 4); break;            /* LDR */
        case 5: c->r[rt] = mem_read(c, addr, 2); break;            /* LDRH */
        case 6: c->r[rt] = mem_read(c, addr, 1); break;            /* LDRB */
        default: c->r[rt] = (uint32_t)(int32_t)(int16_t)mem_read(c, addr, 2); break; /* LDRSH */
        }
        return THUMB_OK;
    }

    /* ---- LDR/STR word/byte immediate (011x) ---- */
    if((op >> 13) == 0x3) {
        int bflag = (op >> 12) & 1; /* 1 = byte */
        int load = (op >> 11) & 1;
        int imm5 = (op >> 6) & 0x1F;
        int rn = (op >> 3) & 7;
        int rt = op & 7;
        if(bflag) {
            uint32_t addr = c->r[rn] + imm5;
            if(load) c->r[rt] = mem_read(c, addr, 1);
            else mem_write(c, addr, c->r[rt] & 0xFF, 1);
        } else {
            uint32_t addr = c->r[rn] + (imm5 << 2);
            if(load) c->r[rt] = mem_read(c, addr, 4);
            else mem_write(c, addr, c->r[rt], 4);
        }
        return THUMB_OK;
    }

    /* ---- LDRH/STRH immediate (1000) ---- */
    if((op >> 12) == 0x8) {
        int load = (op >> 11) & 1;
        int imm5 = (op >> 6) & 0x1F;
        int rn = (op >> 3) & 7;
        int rt = op & 7;
        uint32_t addr = c->r[rn] + (imm5 << 1);
        if(load) c->r[rt] = mem_read(c, addr, 2);
        else mem_write(c, addr, c->r[rt] & 0xFFFF, 2);
        return THUMB_OK;
    }

    /* ---- LDR/STR SP relative (1001) ---- */
    if((op >> 12) == 0x9) {
        int load = (op >> 11) & 1;
        int rt = (op >> 8) & 7;
        uint32_t imm8 = (op & 0xFF) << 2;
        uint32_t addr = SP + imm8;
        if(load) c->r[rt] = mem_read(c, addr, 4);
        else mem_write(c, addr, c->r[rt], 4);
        return THUMB_OK;
    }

    /* ---- ADR / ADD SP (1010) ---- */
    if((op >> 12) == 0xA) {
        int sp_rel = (op >> 11) & 1;
        int rd = (op >> 8) & 7;
        uint32_t imm8 = (op & 0xFF) << 2;
        if(sp_rel) c->r[rd] = SP + imm8;          /* ADD (SP plus imm) */
        else c->r[rd] = ((PC) & ~3u) + imm8;      /* ADR (PC relative) */
        return THUMB_OK;
    }

    /* ---- Misc 16-bit (1011) ---- */
    if((op >> 12) == 0xB) {
        /* ADD/SUB SP imm7 : 1011 0000 0 imm7 / 1011 0000 1 imm7 */
        if((op >> 8) == 0xB0) {
            uint32_t imm7 = (op & 0x7F) << 2;
            if(op & 0x80) SP = SP - imm7; else SP = SP + imm7;
            return THUMB_OK;
        }
        /* CBZ/CBNZ : 1011 x0 i 1 imm5 Rn */
        if(((op >> 12) == 0xB) && ((op >> 8) & 0x5) == 0x1 && ((op >> 11) & 1) == 0) {
            /* match 1011 0i01 ... (CBZ) and 1011 1i01 ... (CBNZ) */
        }
        if((op & 0xF500) == 0xB100) {
            int nz = (op >> 11) & 1; /* 0 = CBZ, 1 = CBNZ */
            int rn = op & 7;
            uint32_t imm = (((op >> 9) & 1) << 5) | (((op >> 3) & 0x1F) << 0);
            uint32_t target = (PC) + (imm << 0); /* PC = instr+4; imm already *? */
            /* imm5 field is bits 7:3, i bit is bit9. offset = (i:imm5):0 (times 2) */
            uint32_t imm5 = (op >> 3) & 0x1F;
            uint32_t i = (op >> 9) & 1;
            uint32_t off = ((i << 5) | imm5) << 1;
            target = (PC) + off;
            int taken = nz ? (c->r[rn] != 0) : (c->r[rn] == 0);
            if(taken) branch_to(c, target);
            return THUMB_OK;
        }
        /* SXTH/SXTB/UXTH/UXTB : 1011 0010 xx Rm Rd */
        if((op & 0xFF00) == 0xB200) {
            int sub = (op >> 6) & 3;
            int rm = (op >> 3) & 7;
            int rd = op & 7;
            uint32_t v = c->r[rm];
            switch(sub) {
            case 0: c->r[rd] = (uint32_t)(int32_t)(int16_t)(v & 0xFFFF); break; /* SXTH */
            case 1: c->r[rd] = (uint32_t)(int32_t)(int8_t)(v & 0xFF); break;    /* SXTB */
            case 2: c->r[rd] = v & 0xFFFF; break;                              /* UXTH */
            default: c->r[rd] = v & 0xFF; break;                              /* UXTB */
            }
            return THUMB_OK;
        }
        /* PUSH : 1011 010 M reglist */
        if((op & 0xFE00) == 0xB400) {
            int m = (op >> 8) & 1;
            uint32_t list = op & 0xFF;
            int count = 0;
            for(int i = 0; i < 8; i++) if(list & (1 << i)) count++;
            if(m) count++;
            uint32_t addr = SP - 4 * count;
            uint32_t a = addr;
            for(int i = 0; i < 8; i++) if(list & (1 << i)) { mem_write(c, a, c->r[i], 4); a += 4; }
            if(m) { mem_write(c, a, LR, 4); a += 4; }
            SP = addr;
            return THUMB_OK;
        }
        /* POP : 1011 110 P reglist */
        if((op & 0xFE00) == 0xBC00) {
            int p = (op >> 8) & 1;
            uint32_t list = op & 0xFF;
            uint32_t a = SP;
            for(int i = 0; i < 8; i++) if(list & (1 << i)) { c->r[i] = mem_read(c, a, 4); a += 4; }
            if(p) { uint32_t v = mem_read(c, a, 4); a += 4; SP = a; branch_to(c, v); return THUMB_OK; }
            SP = a;
            return THUMB_OK;
        }
        /* REV/REV16/REVSH : 1011 1010 xx Rm Rd */
        if((op & 0xFF00) == 0xBA00) {
            int sub = (op >> 6) & 3;
            int rm = (op >> 3) & 7;
            int rd = op & 7;
            uint32_t v = c->r[rm];
            switch(sub) {
            case 0: c->r[rd] = __builtin_bswap32(v); break;                 /* REV */
            case 1: c->r[rd] = ((v & 0x00FF00FF) << 8) | ((v & 0xFF00FF00) >> 8); break; /* REV16 */
            case 3: { /* REVSH */
                uint32_t t = ((v & 0xFF) << 8) | ((v >> 8) & 0xFF);
                c->r[rd] = (uint32_t)(int32_t)(int16_t)t; break; }
            default: break;
            }
            return THUMB_OK;
        }
        /* IT and hints : 1011 1111 ... */
        if((op & 0xFF00) == 0xBF00) {
            int firstcond = (op >> 4) & 0xF;
            int mask = op & 0xF;
            if(mask == 0) {
                /* hints: NOP/YIELD/WFE/WFI/SEV */
                int hint = (op >> 4) & 0xF;
                if(hint == 2) c->sleeping = 1;  /* WFE */
                else if(hint == 3) c->sleeping = 1; /* WFI */
                return THUMB_OK;
            }
            /* IT block. ITSTATE<7:4>=firstcond, ITSTATE<3:0>=mask. */
            c->it_cond = (uint8_t)(((firstcond & 0xF) << 4) | (mask & 0xF));
            c->it_mask = 1; /* active block */
            return THUMB_OK;
        }
        /* BKPT : 1011 1110 */
        if((op & 0xFF00) == 0xBE00) {
            c->halted = 1;
            return THUMB_FAULT_HALT;
        }
        return THUMB_FAULT_UNDEF;
    }

    /* ---- LDM/STM (1100) ---- */
    if((op >> 12) == 0xC) {
        int load = (op >> 11) & 1;
        int rn = (op >> 8) & 7;
        uint32_t list = op & 0xFF;
        uint32_t a = c->r[rn];
        if(load) {
            for(int i = 0; i < 8; i++) if(list & (1 << i)) { c->r[i] = mem_read(c, a, 4); a += 4; }
            if(!(list & (1 << rn))) c->r[rn] = a; /* writeback if Rn not in list */
        } else {
            for(int i = 0; i < 8; i++) if(list & (1 << i)) { mem_write(c, a, c->r[i], 4); a += 4; }
            c->r[rn] = a;
        }
        return THUMB_OK;
    }

    /* ---- Conditional branch / SVC (1101) ---- */
    if((op >> 12) == 0xD) {
        int cond = (op >> 8) & 0xF;
        if(cond == 0xE) { return THUMB_FAULT_UNDEF; } /* permanently undefined */
        if(cond == 0xF) { return THUMB_OK; } /* SVC: stub (not used by the fw) */
        int32_t imm8 = (int32_t)(int8_t)(op & 0xFF);
        if(cond_passed(c, cond)) {
            branch_to(c, (uint32_t)((int32_t)PC + (imm8 << 1)));
        }
        return THUMB_OK;
    }

    /* ---- Unconditional branch B (11100) ---- */
    if((op >> 11) == 0x1C) {
        int32_t imm11 = op & 0x7FF;
        imm11 = (imm11 << 21) >> 21; /* sign extend 11 bits */
        branch_to(c, (uint32_t)((int32_t)PC + (imm11 << 1)));
        return THUMB_OK;
    }

    return THUMB_FAULT_UNDEF;
}

/* ========================================================================= */
/* Implementation of 32-bit instructions (Thumb-2)                           */
/* ========================================================================= */

static int exec_thumb32(ThumbCore* c, uint16_t hw1, uint16_t hw2) {
    uint32_t op1 = (hw1 >> 11) & 0x3; /* bits 12:11 of hw1 after the 11101.. */
    /* Classification per A5.3 using op1 (bits 12:11) and op2 fields */
    uint32_t op = ((uint32_t)hw1 << 16) | hw2;
    (void)op1;

    uint32_t cls = (hw1 >> 11) & 0x3; /* 01,10,11 */
    uint32_t op2 = (hw1 >> 4) & 0x7F;

    /* ===== Class 11110 (0x1E): data-processing imm + branches/misc control.
     * If hw2 bit15=1 -> branch (B/BL/BLX) or (when op high) MSR/MRS/misc.
     * If hw2 bit15=0 -> data-processing (modified/plain immediate) except the
     * control branches detected below. Classes 11101(0x1D) and
     * 11111(0x1F) are NEVER branches: they are handled further down. ===== */
    if(((hw1 >> 11) & 0x1F) == 0x1E && (hw2 & 0x8000)) {
        int b14 = (hw2 >> 14) & 1;  /* op2 bit of hw2 */
        int b12 = (hw2 >> 12) & 1;
        uint32_t cond_field = (hw1 >> 6) & 0xF;
        if(b14 == 0 && b12 == 0 && cond_field < 0xE) {
            /* B<c>.W : 11110 S cond imm6 10 J1 0 J2 imm11 */
            uint32_t s = (hw1 >> 10) & 1;
            uint32_t cond = cond_field;
            uint32_t imm6 = hw1 & 0x3F;
            uint32_t j1 = (hw2 >> 13) & 1;
            uint32_t j2 = (hw2 >> 11) & 1;
            uint32_t imm11 = hw2 & 0x7FF;
            uint32_t imm32 = (s << 20) | (j2 << 19) | (j1 << 18) | (imm6 << 12) | (imm11 << 1);
            if(imm32 & 0x00100000) imm32 |= 0xFFE00000;
            if(cond_passed(c, (int)cond)) branch_to(c, PC + imm32);
            return THUMB_OK;
        }
        if(b14 == 0 && b12 == 0 && cond_field >= 0xE) {
            /* MSR / MRS / hints / misc control (cond field = 111x) */
            uint32_t op1x = (hw1 >> 4) & 0x3F; /* bits 9:4 */
            if((op1x & 0x3E) == 0x38) {
                /* MSR (register) */
                int rn = hw1 & 0xF;
                int sysm = hw2 & 0xFF;
                uint32_t val = c->r[rn];
                switch(sysm) {
                case 0: c->xpsr = (c->xpsr & 0x07FFFFFF) | (val & 0xF8000000); break; /* APSR */
                case 8: c->msp = val; if(!(c->control & 2)) SP = val; break;           /* MSP */
                case 9: c->psp = val; if(c->control & 2) SP = val; break;              /* PSP */
                case 16: c->primask = val & 1; break;
                case 17: c->basepri = val & 0xFF; break;
                case 18: c->basepri = val & 0xFF; break;
                case 19: c->faultmask = val & 1; break;
                case 20: c->control = val & 3; break;
                default: break;
                }
                return THUMB_OK;
            }
            if((op1x & 0x3F) == 0x3B) {
                /* misc control: DSB/DMB/ISB/CLREX -> NOP */
                return THUMB_OK;
            }
            if((op1x & 0x3F) == 0x3A) {
                /* 32-bit hints (NOP.W/WFI.W/...) -> NOP (no effect on registers) */
                return THUMB_OK;
            }
            if((op1x & 0x3E) == 0x3E) {
                /* MRS : read special reg */
                int rd = (hw2 >> 8) & 0xF;
                int sysm = hw2 & 0xFF;
                uint32_t v = 0;
                switch(sysm) {
                case 0: case 1: case 2: case 3: v = c->xpsr & 0xF80000FF; break; /* xPSR */
                case 5: case 6: case 7: v = c->xpsr & 0x1FF; break;            /* IPSR/EPSR */
                case 8: v = c->msp; break;
                case 9: v = c->psp; break;
                case 16: v = c->primask; break;
                case 17: case 18: v = c->basepri; break;
                case 19: v = c->faultmask; break;
                case 20: v = c->control; break;
                default: v = 0; break;
                }
                c->r[rd] = v;
                return THUMB_OK;
            }
            return THUMB_OK;
        }
        /* b12==1 (or b14==1): unconditional B.W (b14=0) or BL (b14=1).
         * BLX (b12=0 with b14) does not occur on Thumb-only EFM32. */
        {
            uint32_t s = (hw1 >> 10) & 1;
            uint32_t imm10 = hw1 & 0x3FF;
            uint32_t j1 = (hw2 >> 13) & 1;
            uint32_t j2 = (hw2 >> 11) & 1;
            uint32_t imm11 = hw2 & 0x7FF;
            uint32_t i1 = (~(j1 ^ s)) & 1;
            uint32_t i2 = (~(j2 ^ s)) & 1;
            uint32_t imm32 = (s << 24) | (i1 << 23) | (i2 << 22) | (imm10 << 12) | (imm11 << 1);
            if(imm32 & 0x01000000) imm32 |= 0xFE000000;
            if(b14) {
                /* BL : return address = instr+4 = current PC */
                LR = (PC) | 1;
                branch_to(c, PC + imm32);
            } else {
                /* unconditional B.W */
                branch_to(c, PC + imm32);
            }
            return THUMB_OK;
        }
    }

    /* ===== Data processing (modified immediate / plain binary imm) =====
     * 11110 ... : cls==10? Actually hw1>>11 == 0x1E for these when bit15 of hw2
     * is 0 already handled. Data-processing imm has hw1 bits [15:11]=11110 and
     * hw2 bit15=0 -> handled above? No: those are only branch/msr when op2 high.
     * Let's dispatch by the main class.
     */

    /* Data-processing (modified/plain immediate): class 11110 with hw2 bit15=0
     * (branches/control have hw2 bit15=1 and were resolved above). */
    if(((hw1 >> 11) & 0x1F) == 0x1E && !(hw2 & 0x8000)) {
        int i = (hw1 >> 10) & 1;
        int bit_plain = (hw1 >> 9) & 1; /* distinguishes plain vs modified imm */
        int opc = (hw1 >> 5) & 0xF;
        int s = (hw1 >> 4) & 1;
        int rn = hw1 & 0xF;
        int imm3 = (hw2 >> 12) & 7;
        int rd = (hw2 >> 8) & 0xF;
        int imm8 = hw2 & 0xFF;
        uint32_t imm12 = ((uint32_t)i << 11) | ((uint32_t)imm3 << 8) | (uint32_t)imm8;

        if(!bit_plain) {
            /* Data processing (modified immediate) */
            int co = get_c(c), vo;
            uint32_t imm = thumb_expand_imm_c(imm12, get_c(c), &co);
            uint32_t a = c->r[rn];
            uint32_t res;
            switch(opc) {
            case 0x0: /* AND / TST */
                res = a & imm;
                if(rd == 15 && s) { set_nz(c, res); set_c(c, co); }
                else { c->r[rd] = res; if(s) { set_nz(c, res); set_c(c, co); } }
                return THUMB_OK;
            case 0x1: /* BIC */
                res = a & ~imm; c->r[rd] = res;
                if(s) { set_nz(c, res); set_c(c, co); } return THUMB_OK;
            case 0x2: /* ORR (rn!=15) / MOV(imm) (rn==15) */
                res = (rn == 15) ? imm : (a | imm); c->r[rd] = res;
                if(s) { set_nz(c, res); set_c(c, co); } return THUMB_OK;
            case 0x3: /* ORN (rn!=15) / MVN (rn==15) */
                res = (rn == 15) ? ~imm : (a | ~imm); c->r[rd] = res;
                if(s) { set_nz(c, res); set_c(c, co); } return THUMB_OK;
            case 0x4: /* EOR / TEQ */
                res = a ^ imm;
                if(rd == 15 && s) { set_nz(c, res); set_c(c, co); }
                else { c->r[rd] = res; if(s) { set_nz(c, res); set_c(c, co); } }
                return THUMB_OK;
            case 0x8: /* ADD / CMN */
                res = add_with_carry(a, imm, 0, &co, &vo);
                if(rd == 15 && s) { set_nz(c, res); set_c(c, co); set_v(c, vo); }
                else { c->r[rd] = res; if(s) { set_nz(c, res); set_c(c, co); set_v(c, vo); } }
                return THUMB_OK;
            case 0xA: /* ADC */
                res = add_with_carry(a, imm, get_c(c), &co, &vo);
                c->r[rd] = res; if(s) { set_nz(c, res); set_c(c, co); set_v(c, vo); } return THUMB_OK;
            case 0xB: /* SBC */
                res = add_with_carry(a, ~imm, get_c(c), &co, &vo);
                c->r[rd] = res; if(s) { set_nz(c, res); set_c(c, co); set_v(c, vo); } return THUMB_OK;
            case 0xD: /* SUB / CMP */
                res = add_with_carry(a, ~imm, 1, &co, &vo);
                if(rd == 15 && s) { set_nz(c, res); set_c(c, co); set_v(c, vo); }
                else { c->r[rd] = res; if(s) { set_nz(c, res); set_c(c, co); set_v(c, vo); } }
                return THUMB_OK;
            case 0xE: /* RSB */
                res = add_with_carry(~a, imm, 1, &co, &vo);
                c->r[rd] = res; if(s) { set_nz(c, res); set_c(c, co); set_v(c, vo); } return THUMB_OK;
            default: return THUMB_FAULT_UNDEF;
            }
        } else {
            /* Plain binary immediate (MOVW/MOVT/ADDW/SUBW/bitfields/ADR) */
            int op_p = (hw1 >> 4) & 0x1F; /* bits 8:4 incl op */
            /* Re-derive fields per ARMv7-M A5.3.3 */
            int subop = (hw1 >> 4) & 0x1F;
            (void)op_p;
            switch(subop) {
            case 0x00: { /* ADDW (ADD imm12, no flags) ; if rn==15 -> ADR */
                uint32_t imm = imm12;
                if(rn == 15) c->r[rd] = ((PC) & ~3u) + imm;
                else c->r[rd] = c->r[rn] + imm;
                return THUMB_OK; }
            case 0x04: { /* MOVW imm16 */
                uint32_t imm16 = ((uint32_t)(hw1 & 0xF) << 12) | imm12;
                c->r[rd] = imm16;
                return THUMB_OK; }
            case 0x0A: { /* SUBW (SUB imm12) ; rn==15 -> ADR sub */
                uint32_t imm = imm12;
                if(rn == 15) c->r[rd] = ((PC) & ~3u) - imm;
                else c->r[rd] = c->r[rn] - imm;
                return THUMB_OK; }
            case 0x0C: { /* MOVT imm16 */
                uint32_t imm16 = ((uint32_t)(hw1 & 0xF) << 12) | imm12;
                c->r[rd] = (c->r[rd] & 0x0000FFFF) | (imm16 << 16);
                return THUMB_OK; }
            case 0x14: { /* SBFX */
                int lsb = ((imm3 << 2) | ((hw2 >> 6) & 3));
                int widthm1 = hw2 & 0x1F;
                int width = widthm1 + 1;
                uint32_t v = c->r[rn];
                uint32_t field = (v >> lsb) & ((width >= 32) ? 0xFFFFFFFFu : ((1u << width) - 1));
                /* sign extend */
                if(width < 32 && (field & (1u << (width - 1)))) field |= ~((1u << width) - 1);
                c->r[rd] = field;
                return THUMB_OK; }
            case 0x16: { /* BFI / BFC */
                int lsb = ((imm3 << 2) | ((hw2 >> 6) & 3));
                int msb = hw2 & 0x1F;
                if(msb >= lsb) {
                    uint32_t width = msb - lsb + 1;
                    uint32_t mask = (width >= 32) ? 0xFFFFFFFFu : (((1u << width) - 1) << lsb);
                    if(rn == 15) {
                        /* BFC */
                        c->r[rd] &= ~mask;
                    } else {
                        uint32_t src = c->r[rn];
                        c->r[rd] = (c->r[rd] & ~mask) | ((src << lsb) & mask);
                    }
                }
                return THUMB_OK; }
            case 0x1C: { /* UBFX */
                int lsb = ((imm3 << 2) | ((hw2 >> 6) & 3));
                int widthm1 = hw2 & 0x1F;
                int width = widthm1 + 1;
                uint32_t v = c->r[rn];
                uint32_t field = (v >> lsb) & ((width >= 32) ? 0xFFFFFFFFu : ((1u << width) - 1));
                c->r[rd] = field;
                return THUMB_OK; }
            default:
                return THUMB_FAULT_UNDEF;
            }
        }
    }

    /* ===== Load/store, data-processing register, multiply (11101 / 11111) ===== */
    if(((hw1 >> 11) & 0x1F) == 0x1D || ((hw1 >> 11) & 0x1F) == 0x1F) {
        uint32_t op1x = (hw1 >> 9) & 0x3; /* bits 10:9 */
        /* --- Load/store multiple & dual --- */
        if(((hw1 >> 9) & 0x3F) == 0x22) { /* 11101 00 ... LDM/STM, PUSH/POP.W */
            /* handled below by specific pattern */
        }

        /* Load Store Multiple: 1110 100x x0xx */
        if((hw1 & 0xFE40) == 0xE800 && ((hw1 >> 11) & 0x1F) == 0x1D) {
            int l = (hw1 >> 4) & 1;
            int wback = (hw1 >> 5) & 1;
            int pu = (hw1 >> 7) & 0x3; /* bits 8:7: 01=IA, 10=DB */
            int rn = hw1 & 0xF;
            uint32_t list = hw2 & 0xFFFF;
            int count = 0;
            for(int i = 0; i < 16; i++) if(list & (1 << i)) count++;
            uint32_t base = c->r[rn];
            uint32_t a;
            int decrement = (pu == 0x2); /* DB */
            if(decrement) a = base - 4 * count; else a = base;
            uint32_t start = a;
            for(int i = 0; i < 16; i++) {
                if(list & (1 << i)) {
                    if(l) {
                        uint32_t v = mem_read(c, a, 4);
                        if(i == 15) branch_to(c, v); else c->r[i] = v;
                    } else {
                        mem_write(c, a, (i == 15) ? (PC) : c->r[i], 4);
                    }
                    a += 4;
                }
            }
            if(wback) {
                if(decrement) c->r[rn] = start; else c->r[rn] = a;
            }
            return THUMB_OK;
        }

        /* Load/store dual, exclusive, table branch : 1110 100x x1xx */
        if((hw1 & 0xFE40) == 0xE840 && ((hw1 >> 11) & 0x1F) == 0x1D) {
            int op1d = (hw1 >> 7) & 1; /* P */
            int op2d = (hw1 >> 4) & 0x3; /* bits 5:4 L/W-ish */
            int rn = hw1 & 0xF;
            /* TBB/TBH : 1110 1000 1101 Rn ; hw2 = 1111 0000 000 H Rm */
            if((hw1 & 0xFFF0) == 0xE8D0 && (hw2 & 0xFFE0) == 0xF000) {
                int h = (hw2 >> 4) & 1;
                int rm = hw2 & 0xF;
                uint32_t base = (rn == 15) ? (PC) : c->r[rn];
                uint32_t off;
                if(h) {
                    uint32_t a = base + (c->r[rm] << 1);
                    off = mem_read(c, a, 2);
                } else {
                    uint32_t a = base + c->r[rm];
                    off = mem_read(c, a, 1);
                }
                branch_to(c, PC + (off << 1));
                return THUMB_OK;
            }
            /* LDRD/STRD (immediate) : 1110 100 P U 1 W L Rn Rt Rt2 imm8 */
            {
                int p = (hw1 >> 8) & 1;
                int u = (hw1 >> 7) & 1;
                int w = (hw1 >> 5) & 1;
                int l = (hw1 >> 4) & 1;
                int rt = (hw2 >> 12) & 0xF;
                int rt2 = (hw2 >> 8) & 0xF;
                uint32_t imm8v = (hw2 & 0xFF) << 2;
                uint32_t base = c->r[rn];
                uint32_t addr = p ? (u ? base + imm8v : base - imm8v) : base;
                if(l) {
                    c->r[rt] = mem_read(c, addr, 4);
                    c->r[rt2] = mem_read(c, addr + 4, 4);
                } else {
                    mem_write(c, addr, c->r[rt], 4);
                    mem_write(c, addr + 4, c->r[rt2], 4);
                }
                if(w) c->r[rn] = u ? base + imm8v : base - imm8v;
                (void)op1d; (void)op2d;
                return THUMB_OK;
            }
        }

        /* --- Data processing (shifted register) : 1110 101x xxxx --- */
        if((hw1 & 0xFE00) == 0xEA00 && ((hw1 >> 11) & 0x1F) == 0x1D) {
            int opc = (hw1 >> 5) & 0xF;
            int s = (hw1 >> 4) & 1;
            int rn = hw1 & 0xF;
            int imm3 = (hw2 >> 12) & 7;
            int rd = (hw2 >> 8) & 0xF;
            int imm2 = (hw2 >> 6) & 3;
            int type = (hw2 >> 4) & 3;
            int rm = hw2 & 0xF;
            int shamt = (imm3 << 2) | imm2;
            int shtype = type;
            if(type == SRTYPE_LSR && shamt == 0) shamt = 32;
            else if(type == SRTYPE_ASR && shamt == 0) shamt = 32;
            else if(type == SRTYPE_ROR && shamt == 0) { shtype = SRTYPE_RRX; shamt = 1; }
            int co = get_c(c), vo;
            uint32_t shifted = shift_c(c->r[rm], shtype, shamt, get_c(c), &co);
            uint32_t a = c->r[rn];
            uint32_t res;
            switch(opc) {
            case 0x0: /* AND / TST */
                res = a & shifted;
                if(rd == 15 && s) { set_nz(c, res); set_c(c, co); }
                else { c->r[rd] = res; if(s) { set_nz(c, res); set_c(c, co); } }
                return THUMB_OK;
            case 0x1: /* BIC */
                res = a & ~shifted; c->r[rd] = res; if(s) { set_nz(c, res); set_c(c, co); } return THUMB_OK;
            case 0x2: /* ORR / MOV(shift) */
                res = (rn == 15) ? shifted : (a | shifted);
                c->r[rd] = res; if(s) { set_nz(c, res); set_c(c, co); } return THUMB_OK;
            case 0x3: /* ORN / MVN */
                res = (rn == 15) ? ~shifted : (a | ~shifted);
                c->r[rd] = res; if(s) { set_nz(c, res); set_c(c, co); } return THUMB_OK;
            case 0x4: /* EOR / TEQ */
                res = a ^ shifted;
                if(rd == 15 && s) { set_nz(c, res); set_c(c, co); }
                else { c->r[rd] = res; if(s) { set_nz(c, res); set_c(c, co); } }
                return THUMB_OK;
            case 0x8: /* ADD / CMN */
                res = add_with_carry(a, shifted, 0, &co, &vo);
                if(rd == 15 && s) { set_nz(c, res); set_c(c, co); set_v(c, vo); }
                else { c->r[rd] = res; if(s) { set_nz(c, res); set_c(c, co); set_v(c, vo); } }
                return THUMB_OK;
            case 0xA: /* ADC */
                res = add_with_carry(a, shifted, get_c(c), &co, &vo);
                c->r[rd] = res; if(s) { set_nz(c, res); set_c(c, co); set_v(c, vo); } return THUMB_OK;
            case 0xB: /* SBC */
                res = add_with_carry(a, ~shifted, get_c(c), &co, &vo);
                c->r[rd] = res; if(s) { set_nz(c, res); set_c(c, co); set_v(c, vo); } return THUMB_OK;
            case 0xD: /* SUB / CMP */
                res = add_with_carry(a, ~shifted, 1, &co, &vo);
                if(rd == 15 && s) { set_nz(c, res); set_c(c, co); set_v(c, vo); }
                else { c->r[rd] = res; if(s) { set_nz(c, res); set_c(c, co); set_v(c, vo); } }
                return THUMB_OK;
            case 0xE: /* RSB */
                res = add_with_carry(~a, shifted, 1, &co, &vo);
                c->r[rd] = res; if(s) { set_nz(c, res); set_c(c, co); set_v(c, vo); } return THUMB_OK;
            default: return THUMB_FAULT_UNDEF;
            }
        }

        /* --- Coprocessor / data proc register / multiply / long (11111) --- */
        if(((hw1 >> 11) & 0x1F) == 0x1F) {
            uint32_t op1f = (hw1 >> 4) & 0x7F;

            /* Data-processing (register): register-controlled shifts and extensions
             * = 1111 1010 0xxx (hw1 bit7=0, range 0xFA00..0xFA7F). The range
             * 0xFA80..0xFAFF (hw1 bit7=1) are CLZ/REV/RBIT and handled later. */
            if((hw1 & 0xFF80) == 0xFA00) {
                /* Register-controlled shift : 1111 1010 0TT S Rn 1111 Rd 0000 Rm */
                if((hw1 & 0xFFE0) == 0xFA00 || (hw1 & 0xFFE0) == 0xFA20 ||
                   (hw1 & 0xFFE0) == 0xFA40 || (hw1 & 0xFFE0) == 0xFA60) {
                    if((hw2 & 0xF0F0) == 0xF000) {
                        int type = (hw1 >> 5) & 3;
                        int s = (hw1 >> 4) & 1;
                        int rn = hw1 & 0xF;
                        int rd = (hw2 >> 8) & 0xF;
                        int rm = hw2 & 0xF;
                        int amt = c->r[rm] & 0xFF;
                        int co = get_c(c);
                        uint32_t res = shift_c(c->r[rn], type, amt, get_c(c), &co);
                        c->r[rd] = res;
                        if(s) { set_nz(c, res); set_c(c, co); }
                        return THUMB_OK;
                    }
                }
                /* Sign/zero extend (and add) : 1111 1010 0xxx Rn 1111 Rd 1(rot)(rot) Rm */
                if((hw2 & 0xF080) == 0xF080) {
                    int opx = (hw1 >> 4) & 0x7;
                    int rn = hw1 & 0xF;
                    int rd = (hw2 >> 8) & 0xF;
                    int rot = ((hw2 >> 4) & 3) * 8;
                    int rm = hw2 & 0xF;
                    uint32_t v = c->r[rm];
                    v = (v >> rot) | (v << ((32 - rot) & 31));
                    if(rot == 0) v = c->r[rm];
                    uint32_t res;
                    switch((hw1 >> 4) & 0x7) {
                    case 0: res = (uint32_t)(int32_t)(int16_t)(v & 0xFFFF); break; /* SXTH */
                    case 1: res = v & 0xFFFF; break;                             /* UXTH */
                    case 4: res = (uint32_t)(int32_t)(int8_t)(v & 0xFF); break;  /* SXTB */
                    case 5: res = v & 0xFF; break;                              /* UXTB */
                    default: res = v; break;
                    }
                    /* with Rn != 15 it's the "add" variant (SXTAB etc.) */
                    if(rn != 15) res = c->r[rn] + res;
                    c->r[rd] = res;
                    (void)opx;
                    return THUMB_OK;
                }
            }

            /* Misc data proc : 1111 1010 1xxx (CLZ, RBIT, REV, etc.) */
            if((hw1 & 0xFFF0) == 0xFAB0 && (hw2 & 0xF0C0) == 0xF080) {
                /* CLZ : 1111 1010 1011 Rm 1111 Rd 1000 Rm */
                int rm = hw2 & 0xF;
                int rd = (hw2 >> 8) & 0xF;
                uint32_t v = c->r[rm];
                c->r[rd] = v ? (uint32_t)__builtin_clz(v) : 32u;
                return THUMB_OK;
            }
            if((hw1 & 0xFFF0) == 0xFA90 && (hw2 & 0xF0C0) == 0xF080) {
                /* REV/REV16/RBIT/REVSH : 1111 1010 1001 Rm 1111 Rd 10xx Rm */
                int op_r = (hw2 >> 4) & 0x3;
                int rm = hw2 & 0xF;
                int rd = (hw2 >> 8) & 0xF;
                uint32_t v = c->r[rm];
                switch(op_r) {
                case 0: c->r[rd] = __builtin_bswap32(v); break; /* REV */
                case 1: c->r[rd] = ((v & 0x00FF00FF) << 8) | ((v & 0xFF00FF00) >> 8); break; /* REV16 */
                case 2: { /* RBIT */
                    uint32_t x = v, r = 0;
                    for(int i = 0; i < 32; i++) { r = (r << 1) | (x & 1); x >>= 1; }
                    c->r[rd] = r; break; }
                default: { /* REVSH */
                    uint32_t t = ((v & 0xFF) << 8) | ((v >> 8) & 0xFF);
                    c->r[rd] = (uint32_t)(int32_t)(int16_t)t; break; }
                }
                return THUMB_OK;
            }

            /* Multiply (32-bit result) : 1111 1011 0xxx */
            if((hw1 & 0xFF80) == 0xFB00) {
                int opm = (hw1 >> 4) & 0x7;
                int rn = hw1 & 0xF;
                int ra = (hw2 >> 12) & 0xF;
                int rd = (hw2 >> 8) & 0xF;
                int op2m = (hw2 >> 4) & 0xF;
                int rm = hw2 & 0xF;
                if(opm == 0) {
                    if(op2m == 0) {
                        if(ra == 15) { /* MUL */
                            c->r[rd] = c->r[rn] * c->r[rm];
                        } else { /* MLA */
                            c->r[rd] = c->r[rn] * c->r[rm] + c->r[ra];
                        }
                    } else if(op2m == 1) { /* MLS */
                        c->r[rd] = c->r[ra] - c->r[rn] * c->r[rm];
                    }
                    return THUMB_OK;
                }
                return THUMB_OK;
            }

            /* Long multiply / divide : 1111 1011 1xxx */
            if((hw1 & 0xFF80) == 0xFB80) {
                int opl = (hw1 >> 4) & 0x7;
                int rn = hw1 & 0xF;
                int rdlo = (hw2 >> 12) & 0xF;
                int rdhi = (hw2 >> 8) & 0xF;
                int op2l = (hw2 >> 4) & 0xF;
                int rm = hw2 & 0xF;
                switch(opl) {
                case 0: { /* SMULL */
                    int64_t r = (int64_t)(int32_t)c->r[rn] * (int64_t)(int32_t)c->r[rm];
                    c->r[rdlo] = (uint32_t)r; c->r[rdhi] = (uint32_t)(r >> 32);
                    return THUMB_OK; }
                case 1: { /* SDIV */
                    int32_t dn = (int32_t)c->r[rn], dm = (int32_t)c->r[rm];
                    c->r[rdhi] = dm ? (uint32_t)(dn / dm) : 0;
                    return THUMB_OK; }
                case 2: { /* UMULL */
                    uint64_t r = (uint64_t)c->r[rn] * (uint64_t)c->r[rm];
                    c->r[rdlo] = (uint32_t)r; c->r[rdhi] = (uint32_t)(r >> 32);
                    return THUMB_OK; }
                case 3: { /* UDIV */
                    uint32_t dn = c->r[rn], dm = c->r[rm];
                    c->r[rdhi] = dm ? (dn / dm) : 0;
                    return THUMB_OK; }
                case 4: { /* SMLAL */
                    int64_t acc = ((int64_t)(uint64_t)c->r[rdhi] << 32) | (uint64_t)c->r[rdlo];
                    acc += (int64_t)(int32_t)c->r[rn] * (int64_t)(int32_t)c->r[rm];
                    c->r[rdlo] = (uint32_t)acc; c->r[rdhi] = (uint32_t)((uint64_t)acc >> 32);
                    return THUMB_OK; }
                case 6: { /* UMLAL */
                    uint64_t acc = ((uint64_t)c->r[rdhi] << 32) | (uint64_t)c->r[rdlo];
                    acc += (uint64_t)c->r[rn] * (uint64_t)c->r[rm];
                    c->r[rdlo] = (uint32_t)acc; c->r[rdhi] = (uint32_t)(acc >> 32);
                    return THUMB_OK; }
                default: (void)op2l; return THUMB_OK;
                }
            }

            /* Load/store single (register/immediate) : 1111 100x xxxx */
            if((hw1 & 0xFE00) == 0xF800) {
                int size_bits = (hw1 >> 5) & 3; /* 0=byte,1=half,2=word */
                int load = (hw1 >> 4) & 1;
                int sx = (hw1 >> 8) & 1; /* sign extend (for LDRSB/LDRSH) when size<2 */
                int rn = hw1 & 0xF;
                int rt = (hw2 >> 12) & 0xF;
                int size = (size_bits == 0) ? 1 : (size_bits == 1) ? 2 : 4;

                if(rn == 15) {
                    /* PC-relative literal: 1111 100 x U 10 1111 Rt imm12 */
                    int u = (hw1 >> 7) & 1;
                    uint32_t imm12v = hw2 & 0xFFF;
                    uint32_t base = (PC) & ~3u;
                    uint32_t addr = u ? base + imm12v : base - imm12v;
                    uint32_t v = mem_read(c, addr, size);
                    if(load) {
                        if(sx && size == 1) v = (uint32_t)(int32_t)(int8_t)v;
                        else if(sx && size == 2) v = (uint32_t)(int32_t)(int16_t)v;
                        c->r[rt] = v;
                    }
                    return THUMB_OK;
                }

                if((hw2 & 0x0800) == 0x0800 || ((hw1 & 0x0080) == 0)) {
                    /* Could be imm8 forms (with P/U/W) when hw2 bit11==1, OR
                     * imm12 positive form when hw1 bit7 (U) set. Distinguish: */
                }

                int u = (hw1 >> 7) & 1; /* for imm12 form */
                if(u || (hw2 & 0x0800) == 0) {
                    /* imm12 form: 1111 100x x 1 0 1 Rn Rt imm12 (positive) */
                    if((hw1 & 0x0080)) {
                        uint32_t imm12v = hw2 & 0xFFF;
                        uint32_t addr = c->r[rn] + imm12v;
                        if(load) {
                            uint32_t v = mem_read(c, addr, size);
                            if(sx && size == 1) v = (uint32_t)(int32_t)(int8_t)v;
                            else if(sx && size == 2) v = (uint32_t)(int32_t)(int16_t)v;
                            c->r[rt] = v;
                        } else {
                            mem_write(c, addr, c->r[rt], size);
                        }
                        return THUMB_OK;
                    }
                }

                /* imm8 / register forms: hw2[11:6] determine */
                if((hw2 & 0x0F00) == 0x0E00) {
                    /* LDRT/STRT (unprivileged) -> treat as normal imm8 offset */
                    uint32_t imm8v = hw2 & 0xFF;
                    uint32_t addr = c->r[rn] + imm8v;
                    if(load) {
                        uint32_t v = mem_read(c, addr, size);
                        if(sx && size == 1) v = (uint32_t)(int32_t)(int8_t)v;
                        else if(sx && size == 2) v = (uint32_t)(int32_t)(int16_t)v;
                        c->r[rt] = v;
                    } else mem_write(c, addr, c->r[rt], size);
                    return THUMB_OK;
                }
                if((hw2 & 0x0800) == 0x0800) {
                    /* imm8 with P/U/W : 1111 100x x 0 0 0 Rn Rt 1 P U W imm8 */
                    int p = (hw2 >> 10) & 1;
                    int uu = (hw2 >> 9) & 1;
                    int w = (hw2 >> 8) & 1;
                    uint32_t imm8v = hw2 & 0xFF;
                    uint32_t base = c->r[rn];
                    uint32_t offaddr = uu ? base + imm8v : base - imm8v;
                    uint32_t addr = p ? offaddr : base;
                    if(load) {
                        uint32_t v = mem_read(c, addr, size);
                        if(sx && size == 1) v = (uint32_t)(int32_t)(int8_t)v;
                        else if(sx && size == 2) v = (uint32_t)(int32_t)(int16_t)v;
                        c->r[rt] = v;
                    } else {
                        mem_write(c, addr, c->r[rt], size);
                    }
                    if(w) c->r[rn] = offaddr;
                    return THUMB_OK;
                }
                /* register form: 1111 100x x 0 0 0 Rn Rt 000000 imm2 Rm */
                {
                    int imm2 = (hw2 >> 4) & 3;
                    int rm = hw2 & 0xF;
                    uint32_t addr = c->r[rn] + (c->r[rm] << imm2);
                    if(load) {
                        uint32_t v = mem_read(c, addr, size);
                        if(sx && size == 1) v = (uint32_t)(int32_t)(int8_t)v;
                        else if(sx && size == 2) v = (uint32_t)(int32_t)(int16_t)v;
                        c->r[rt] = v;
                    } else {
                        mem_write(c, addr, c->r[rt], size);
                    }
                    return THUMB_OK;
                }
            }
            (void)op1f;
        }
        (void)op1x;
    }

    (void)cls; (void)op2; (void)op;
    return THUMB_FAULT_UNDEF;
}

/* ========================================================================= */
/* API                                                                       */
/* ========================================================================= */

void thumb_init(ThumbCore* c) {
    memset(c, 0, sizeof(*c));
    c->xpsr = THUMB_PSR_T;
}

void thumb_reset(ThumbCore* c, uint32_t sp, uint32_t pc) {
    c->msp = sp & ~3u;
    c->psp = 0;
    c->control = 0;
    SP = c->msp;
    PC = pc & ~1u;
    LR = 0xFFFFFFFFu;
    c->xpsr = THUMB_PSR_T;
    c->it_cond = 0;
    c->it_mask = 0;
    c->primask = c->faultmask = c->basepri = 0;
    c->halted = 0;
    c->sleeping = 0;
    c->cycles = 0;
    c->last_fault = 0;
}

void thumb_set_mem_cb(ThumbCore* c, ThumbRead rd, ThumbWrite wr, void* ctx) {
    c->read_cb = rd; c->write_cb = wr; c->mem_ctx = ctx;
}

void thumb_set_regions(ThumbCore* c,
                       uint8_t* flash_ptr, uint32_t flash_base, uint32_t flash_size,
                       uint8_t* ram_ptr, uint32_t ram_base, uint32_t ram_size) {
    c->flash_ptr = flash_ptr; c->flash_base = flash_base; c->flash_size = flash_size;
    c->ram_ptr = ram_ptr; c->ram_base = ram_base; c->ram_size = ram_size;
}

void thumb_set_hook(ThumbCore* c, ThumbHook hook, void* ctx) {
    c->hook = hook; c->hook_ctx = ctx;
}

uint32_t thumb_get_reg(const ThumbCore* c, int n) { return c->r[n & 0xF]; }
void thumb_set_reg(ThumbCore* c, int n, uint32_t v) { c->r[n & 0xF] = v; }
uint32_t thumb_get_xpsr(const ThumbCore* c) { return c->xpsr; }
void thumb_set_xpsr(ThumbCore* c, uint32_t v) { c->xpsr = v; }

int thumb_is_exc_return(uint32_t val) { return (val & 0xFFFFFFF0u) == 0xFFFFFFF0u; }

int thumb_enter_exception(ThumbCore* c, uint32_t handler, uint32_t exc_return, uint32_t exc_num) {
    uint32_t sp = SP;
    uint32_t xpsr = c->xpsr | THUMB_PSR_T;
    sp -= 0x20;
    mem_write(c, sp + 0x00, c->r[0], 4);
    mem_write(c, sp + 0x04, c->r[1], 4);
    mem_write(c, sp + 0x08, c->r[2], 4);
    mem_write(c, sp + 0x0C, c->r[3], 4);
    mem_write(c, sp + 0x10, c->r[12], 4);
    mem_write(c, sp + 0x14, LR, 4);
    mem_write(c, sp + 0x18, PC & ~1u, 4);
    mem_write(c, sp + 0x1C, xpsr, 4);
    SP = sp;
    LR = exc_return;
    c->xpsr = (c->xpsr & ~0x1FFu) | (exc_num & 0x1FFu);
    branch_to(c, handler);
    return THUMB_OK;
}

int thumb_exc_return(ThumbCore* c, uint32_t exc_return) {
    /* stack selection per exc_return (bit2: 0=MSP,1=PSP) */
    uint32_t sp = SP;
    uint32_t r0 = mem_read(c, sp + 0x00, 4);
    uint32_t r1 = mem_read(c, sp + 0x04, 4);
    uint32_t r2 = mem_read(c, sp + 0x08, 4);
    uint32_t r3 = mem_read(c, sp + 0x0C, 4);
    uint32_t r12 = mem_read(c, sp + 0x10, 4);
    uint32_t lr = mem_read(c, sp + 0x14, 4);
    uint32_t pc = mem_read(c, sp + 0x18, 4);
    uint32_t xpsr = mem_read(c, sp + 0x1C, 4);
    c->r[0] = r0; c->r[1] = r1; c->r[2] = r2; c->r[3] = r3;
    c->r[12] = r12; LR = lr;
    SP = sp + 0x20;
    c->xpsr = (c->xpsr & ~(0x1FFu)) | 0; /* IPSR=0 (thread) */
    c->xpsr = (xpsr & 0xF8000000u) | (c->xpsr & 0x07FFFFFFu) | THUMB_PSR_T;
    branch_to(c, pc);
    (void)exc_return;
    return THUMB_OK;
}
