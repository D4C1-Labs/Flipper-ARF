
#include "nfc_sniffer_hf.h"

#include <furi.h>
#include <furi_hal.h>
#include <furi_hal_resources.h>
#include <furi_hal_spi.h>
#include <furi_hal_bus.h>
#include <furi_hal_gpio.h>

#include <lib/drivers/st25r3916.h>
#include <lib/drivers/st25r3916_reg.h>

#include <stm32wbxx.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define TAG "NfcSnifferHf"

static const FuriHalSpiBusHandle* h(void) {
    return &furi_hal_spi_bus_handle_nfc;
}

static void reg_write(uint8_t addr, uint8_t val) {
    st25r3916_write_reg(h(), addr, val);
}
static uint8_t reg_read(uint8_t addr) {
    uint8_t v = 0;
    st25r3916_read_reg(h(), addr, &v);
    return v;
}
static void reg_set(uint8_t a, uint8_t m) {
    st25r3916_set_reg_bits(h(), a, m);
}
static void reg_clear(uint8_t a, uint8_t m) {
    st25r3916_clear_reg_bits(h(), a, m);
}
static void direct_cmd(uint8_t cmd) {
    st25r3916_direct_cmd(h(), cmd);
}

#define REG_OP_CONTROL   ST25R3916_REG_OP_CONTROL
#define REG_AUX          ST25R3916_REG_AUX
#define REG_RX_CONF1     ST25R3916_REG_RX_CONF1
#define REG_RX_CONF2     ST25R3916_REG_RX_CONF2
#define REG_RX_CONF3     ST25R3916_REG_RX_CONF3
#define REG_RX_CONF4     ST25R3916_REG_RX_CONF4
#define REG_IO_CONF2     ST25R3916_REG_IO_CONF2
#define REG_EFD_THRESH   ST25R3916_REG_FIELD_THRESHOLD_ACTV
#define REG_EFD_THRESH_D ST25R3916_REG_FIELD_THRESHOLD_DEACTV
#define REG_AUX_DISP     ST25R3916_REG_AUX_DISPLAY

#define OP_CONTROL_en         ST25R3916_REG_OP_CONTROL_en
#define OP_CONTROL_rx_en      ST25R3916_REG_OP_CONTROL_rx_en
#define OP_CONTROL_tx_en      ST25R3916_REG_OP_CONTROL_tx_en
#define OP_CONTROL_en_fd_auto ST25R3916_REG_OP_CONTROL_en_fd_auto_efd
#define OP_CONTROL_en_fd_peer ST25R3916_REG_OP_CONTROL_en_fd_manual_efd_pdt
#define AUX_dis_corr          ST25R3916_REG_AUX_dis_corr
#define IO_CONF2_miso_pd      (ST25R3916_REG_IO_CONF2_miso_pd1 | ST25R3916_REG_IO_CONF2_miso_pd2)
#define EFD_O                 ST25R3916_REG_AUX_DISPLAY_efd_o

#define CMD_STOP             ST25R3916_CMD_STOP
#define CMD_TRANSPARENT_MODE ST25R3916_CMD_TRANSPARENT_MODE
#define CMD_UNMASK_RECEIVE   ST25R3916_CMD_UNMASK_RECEIVE_DATA
#define CMD_RESET_RX_GAIN    ST25R3916_CMD_RESET_RXGAIN
#define CMD_CLEAR_FIFO       ST25R3916_CMD_CLEAR_FIFO

static volatile bool s_stop;

void nfc_sniffer_hf_request_stop(void) {
    s_stop = true;
}

#define MISO_PIN 4

#define RING_SAMPLES 65500u

#define RING_MARGIN 1500u
#define PROCESS_MIN 4000u

#define MAX_SCAN 8000u
#define QUIET_MS 250u

#define FRAME_GAP 81
#define CAP_ARR   25u

static void miso_as_input(void) {
    GPIOB->MODER &= ~GPIO_MODER_MODE4;
}

static void miso_as_spi(void) {
    GPIOB->MODER = (GPIOB->MODER & ~GPIO_MODER_MODE4) | (2u << GPIO_MODER_MODE4_Pos);
}

static void nfc_sniffer_st25_reader_prep(void) {
    direct_cmd(CMD_STOP);
    direct_cmd(ST25R3916_CMD_SET_DEFAULT);
    furi_delay_ms(1);
    reg_set(REG_IO_CONF2, ST25R3916_REG_IO_CONF2_io_drv_lvl);
    reg_set(REG_OP_CONTROL, OP_CONTROL_en);
    for(int i = 0; i < 50; i++) {
        if(reg_read(REG_AUX_DISP) & ST25R3916_REG_AUX_DISPLAY_osc_ok) break;
        furi_delay_ms(1);
    }
    direct_cmd(ST25R3916_CMD_ADJUST_REGULATORS);
    furi_delay_ms(6);
    reg_clear(
        ST25R3916_REG_MODE,
        ST25R3916_REG_MODE_om_mask | ST25R3916_REG_MODE_tr_am | ST25R3916_REG_MODE_targ);
    reg_set(ST25R3916_REG_MODE, ST25R3916_REG_MODE_om_iso14443a);
    reg_write(ST25R3916_REG_OVERSHOOT_CONF1, 0x40);
    reg_write(ST25R3916_REG_OVERSHOOT_CONF2, 0x03);
    reg_write(ST25R3916_REG_UNDERSHOOT_CONF1, 0x40);
    reg_write(ST25R3916_REG_UNDERSHOOT_CONF2, 0x03);
    reg_write(ST25R3916_REG_CORR_CONF1, 0x51);
    reg_write(ST25R3916_REG_CORR_CONF2, 0x00);
    reg_write(ST25R3916_REG_BIT_RATE, 0x00);
}

#define SNIFF_RG_MID   0x78
#define SNIFF_RG_BOOST 0xF8

static void sniff_config(uint8_t rg1) {
    direct_cmd(CMD_STOP);
    reg_clear(REG_OP_CONTROL, OP_CONTROL_tx_en);
    reg_set(REG_OP_CONTROL, OP_CONTROL_en | OP_CONTROL_rx_en | OP_CONTROL_en_fd_auto);

    reg_write(REG_RX_CONF1, 0x03);
    reg_write(REG_RX_CONF2, 0x40);
    reg_write(REG_RX_CONF3, rg1);
    reg_write(REG_RX_CONF4, 0x00);
    reg_write(REG_AUX, AUX_dis_corr);
    reg_clear(REG_IO_CONF2, IO_CONF2_miso_pd);
    direct_cmd(CMD_RESET_RX_GAIN);
    direct_cmd(CMD_CLEAR_FIFO);
    direct_cmd(CMD_UNMASK_RECEIVE);
}

static void efd_set_thresh(int th) {
    uint8_t v = (uint8_t)((th & 7) << 4);
    reg_write(REG_EFD_THRESH, (uint8_t)((reg_read(REG_EFD_THRESH) & ~0x70) | v));
    reg_write(REG_EFD_THRESH_D, (uint8_t)((reg_read(REG_EFD_THRESH_D) & ~0x70) | v));
}

static int efd_wait_measure(uint32_t timeout_ms) {
    reg_set(REG_OP_CONTROL, OP_CONTROL_en);
    reg_clear(REG_OP_CONTROL, 0x03);
    reg_set(REG_OP_CONTROL, OP_CONTROL_en_fd_peer);
    efd_set_thresh(0);
    uint32_t t0 = furi_get_tick();

    while(reg_read(REG_AUX_DISP) & EFD_O) {
        if(s_stop) return -1;
        if((furi_get_tick() - t0) >= timeout_ms) return -1;
        furi_delay_ms(2);
    }
    while(!(reg_read(REG_AUX_DISP) & EFD_O)) {
        if(s_stop) return -1;
        if((furi_get_tick() - t0) >= timeout_ms) return -1;
        furi_delay_ms(1);
    }

    int level = 0;
    for(int th = 0; th <= 7; th++) {
        efd_set_thresh(th);
        for(volatile int d = 0; d < 300; d++) {
        }
        int on = (reg_read(REG_AUX_DISP) & EFD_O) ? 1 : 0;
        if(!on) {
            for(volatile int d = 0; d < 900; d++) {
            }
            on = (reg_read(REG_AUX_DISP) & EFD_O) ? 1 : 0;
        }
        if(on) level = th;
    }
    return level;
}

static void dma_cap_go(void) {
    TIM2->CNT = 0;
    TIM2->CR1 |= TIM_CR1_CEN;
}
static void dma_cap_stop(void) {
    TIM2->CR1 &= ~TIM_CR1_CEN;
    TIM2->DIER = 0;
    DMA1_Channel1->CCR = 0;
    DMAMUX1_Channel0->CCR = 0;
}

static volatile uint32_t* s_cap_idr = &GPIOB->IDR;
static uint8_t s_cap_shift = MISO_PIN;
static uint32_t s_cap_arr = CAP_ARR;

static void dma_circ_arm(uint8_t* buf, uint32_t n) {
    if(furi_hal_bus_is_enabled(FuriHalBusTIM2)) {
        furi_hal_bus_disable(FuriHalBusTIM2);
    }
    furi_hal_bus_enable(FuriHalBusTIM2);
    RCC->AHB1ENR |= RCC_AHB1ENR_DMA1EN | RCC_AHB1ENR_DMAMUX1EN;
    (void)RCC->AHB1ENR;

    DMA1_Channel1->CCR = 0;
    DMA1_Channel1->CPAR = (uint32_t)(uintptr_t)s_cap_idr;
    DMA1_Channel1->CMAR = (uint32_t)(uintptr_t)buf;
    DMA1_Channel1->CNDTR = n;
    DMA1_Channel1->CCR = (2u << 12) | (1u << 7) | (1u << 5) | (1u << 0);

    DMAMUX1_Channel0->CCR = 32;

    TIM2->CR1 = 0;
    TIM2->SMCR = 0;
    TIM2->CCER = 0;
    TIM2->DIER = 0;
    TIM2->CCMR1 = 0;
    TIM2->CCMR2 = 0;
    TIM2->PSC = 0;
    TIM2->ARR = s_cap_arr;
    TIM2->CNT = 0;
#ifdef TIM_OR_TI4_RMP
    TIM2->OR = 0;
#endif
    TIM2->EGR = TIM_EGR_UG;
    TIM2->SR = 0;
    TIM2->DIER = TIM_DIER_UDE;
}

#define ETU100  2324
#define ETUSAMP 23

static uint16_t* s_tr;
static uint8_t* s_seq;
static int s_ntr;
static int s_ongrid, s_offgrid;
static int s_card_samp;

static uint32_t s_diag_dense;
static int s_diag_maxrun;
static int s_tr_init;

static int s_lvl;

__attribute__((optimize("O2"))) static int
    miller_decode(const uint16_t* ps, int np, uint8_t* out, uint32_t* par) {
    *par = 0;
    uint8_t* seq = s_seq;
    __builtin_memset(seq, 0, 160);
    long base = (long)ps[0] * 100 - ETU100 / 4;
    int maxi = 0;
    for(int i = 0; i < np; i++) {
        long rel = (long)ps[i] * 100 - base;
        if(rel < 0) rel = 0;
        int idx = (int)(rel / ETU100);
        long within = rel - (long)idx * ETU100;
        long dZ = within - ETU100 / 4, dX = within - 3 * ETU100 / 4;
        if(dZ < 0) dZ = -dZ;
        if(dX < 0) dX = -dX;
        int half = (dX < dZ) ? 1 : 0;
        if((half ? dX : dZ) > ETU100 * 16 / 100) {
            s_offgrid++;
            continue;
        }
        s_ongrid++;
        if(idx >= 0 && idx < 160) {
            if(seq[idx] == 0) seq[idx] = half ? 2 : 1;
            if(idx > maxi) maxi = idx;
        }
        base = (long)ps[i] * 100 - (long)idx * ETU100 - (half ? 3 * ETU100 / 4 : ETU100 / 4);
    }
    int sr = 0, nb = 0, n = 0;
    for(int k = 1; k <= maxi && n < 32; k++) {
        int bit = (seq[k] == 2) ? 1 : 0;
        sr = (sr >> 1) | (bit << 8);
        nb++;
        if(nb == 9) {
            int v = sr & 0xFF;
            if((sr >> 8 & 1) == __builtin_parity(v)) *par |= (1u << n);
            out[n++] = (uint8_t)v;
            sr = 0;
            nb = 0;
        }
    }

    if(n == 0 && nb >= 6 && nb <= 7) out[n++] = (uint8_t)((sr >> (9 - nb)) & ((1 << nb) - 1));
    return n;
}

__attribute__((optimize("O2"))) static int
    manch_decode(int s, int e, uint8_t* out, uint32_t* par) {
    *par = 0;
    const uint16_t* tr = s_tr;
    int nt = s_ntr;

    int st = 0, hi = nt;
    while(st < hi) {
        int m = (st + hi) / 2;
        if(tr[m] < s)
            st = m + 1;
        else
            hi = m;
    }
    if(st >= nt) return 0;
    int t0 = tr[st];
    uint8_t bits[200];
    int nbits = 0;
    int p = st;

    int h1 = t0, mid = t0 + ETUSAMP / 2, end = t0 + ETUSAMP, frac = 0;
    for(int k = 0; k < 200; k++) {
        if(h1 > e + ETUSAMP) break;
        int c1 = 0;
        while(p < nt && tr[p] < mid) {
            p++;
            c1++;
        }
        int c2 = 0;
        while(p < nt && tr[p] < end) {
            p++;
            c2++;
        }
        if(c1 < 3 && c2 < 3) break;
        bits[nbits++] = (c1 >= 3) ? 1 : 0;
        int adv = ETU100 / 100;
        frac += ETU100 % 100;
        if(frac >= 100) {
            adv++;
            frac -= 100;
        }
        h1 += adv;
        mid += adv;
        end += adv;
    }
    int n = 0, i = 1;
    while(i + 8 <= nbits && n < 32) {
        int v = 0;
        for(int j = 0; j < 8; j++)
            v |= (bits[i + j] & 1) << j;
        if(i + 8 < nbits && (bits[i + 8] & 1) == __builtin_parity(v)) *par |= (1u << n);
        out[n++] = (uint8_t)v;
        i += 9;
    }

    if(n == 0 && nbits >= 5 && nbits <= 6) {
        int v = 0;
        for(int j = 1; j <= 4; j++)
            v |= (bits[j] & 1) << (j - 1);
        out[n++] = (uint8_t)(v & 0x0F);
    }
    return n;
}

struct sfr {
    uint16_t t, tend;
    char dir;
    uint8_t n;
    uint32_t par;
    uint8_t b[24];
};
static struct sfr* s_fr;
static int s_nfr;

static void add_frame(int t, int tend, char dir, const uint8_t* b, int n, uint32_t par) {
    if(s_nfr >= 48 || n <= 0) return;
    int nz = 0;
    for(int i = 0; i < n; i++)
        if(b[i]) nz = 1;
    if(!nz) return;
    if(dir == 'C') {
        int ff = 0;
        for(int i = 0; i < n; i++)
            if(b[i] == 0xFF) ff++;
        if(ff * 4 >= n * 3) return;
    }
    if(n > 24) n = 24;
    s_fr[s_nfr].t = (uint16_t)t;
    s_fr[s_nfr].tend = (uint16_t)tend;
    s_fr[s_nfr].dir = dir;
    s_fr[s_nfr].n = (uint8_t)n;
    s_fr[s_nfr].par = par;
    for(int i = 0; i < n; i++)
        s_fr[s_nfr].b[i] = b[i];
    s_nfr++;
}

#define S_TXT_CAP 4096
static char* s_txt;

static uint16_t* s_paus;

__attribute__((optimize("O2"))) static int sniff_decode(uint32_t got) {
    s_nfr = 0;
    s_ongrid = 0;
    s_offgrid = 0;
    s_card_samp = 0;
    if(s_ntr < 4 || got < 4) return 0;
    int init = s_tr_init;
    (void)got;

    uint16_t* paus = s_paus;
    int np = 0;
    int dense = 0, in_burst = 0, bs = 0, be = 0;
    for(int i = 1; i < s_ntr; i++) {
        int w = s_tr[i] - s_tr[i - 1];
        if(w <= 4) {
            s_diag_dense++;
            if(++dense == 5 && !in_burst) {
                in_burst = 1;
                bs = s_tr[i - 5];
            }
            if(dense > s_diag_maxrun) s_diag_maxrun = dense;
            if(in_burst) be = s_tr[i];
        } else {
            if(in_burst) {
                if(s_tr[i] - be > 4 * ETUSAMP) {
                    uint8_t by[32];
                    uint32_t par;
                    int nn = manch_decode(bs, be, by, &par);
                    add_frame(bs, be, 'C', by, nn, par);
                    s_card_samp += be - bs;
                    in_burst = 0;
                }
            } else if(w >= 5 && (init ^ (i & 1)) == 0 && np < 600) {
                paus[np++] = (uint16_t)((s_tr[i - 1] + s_tr[i]) / 2);
            }

            if(w > ETUSAMP) dense = 0;
        }
    }
    if(in_burst) {
        uint8_t by[32];
        uint32_t par;
        int nn = manch_decode(bs, be, by, &par);
        add_frame(bs, be, 'C', by, nn, par);
        s_card_samp += be - bs;
    }

    for(int fs = 0, i = 1; i <= np; i++)
        if(i == np || paus[i] - paus[i - 1] >= FRAME_GAP) {
            if(i - fs >= 4) {
                uint8_t by[32];
                uint32_t par;
                int nn = miller_decode(paus + fs, i - fs, by, &par);
                add_frame(paus[fs], paus[i - 1], 'R', by, nn, par);
            }
            fs = i;
        }

    for(int i = 0; i < s_nfr; i++)
        for(int j = i + 1; j < s_nfr; j++)
            if(s_fr[j].t < s_fr[i].t) {
                struct sfr t = s_fr[i];
                s_fr[i] = s_fr[j];
                s_fr[j] = t;
            }

    return s_nfr;
}

static int put_u32(char* out, uint32_t v) {
    char t[10];
    int k = 0;
    do {
        uint32_t q = v / 10;
        t[k++] = (char)('0' + (v - q * 10));
        v = q;
    } while(v);
    for(int j = 0; j < k; j++)
        out[j] = t[k - 1 - j];
    return k;
}

static int fmt_frames(char* out, int cap, uint32_t base) {
    static const char HEX[16] = "0123456789ABCDEF";
    int p = 0;
    for(int i = 0; i < s_nfr && p < cap - 128; i++) {
        struct sfr* f = &s_fr[i];
        out[p++] = f->dir;
        out[p++] = ' ';
        p += put_u32(out + p, base + f->t);
        out[p++] = ' ';
        p += put_u32(out + p, base + f->tend);
        for(int j = 0; j < f->n && p < cap - 8; j++) {
            uint8_t v = f->b[j];
            out[p++] = ' ';
            out[p++] = HEX[v >> 4];
            out[p++] = HEX[v & 0xF];
            if(f->par & (1u << j)) out[p++] = '!';
        }
        out[p++] = '\n';
    }
    return p;
}

__attribute__((optimize("O2"))) static void scan_span(const uint8_t* seg, int len, int* pos_io) {
    int lvl = s_lvl, ntr = s_ntr, tr_init = s_tr_init, pos = *pos_io;
    uint16_t* tr = s_tr;
    if(ntr == 0) pos = 0;
    int i = 0;

#define STEPB(b, idx)                                           \
    do {                                                        \
        int _b = (int)(b);                                      \
        if(_b != lvl) {                                         \
            if(ntr == 0) tr_init = lvl;                         \
            if(ntr < 6000) tr[ntr++] = (uint16_t)(pos + (idx)); \
            lvl = _b;                                           \
        }                                                       \
    } while(0)
#define STEP(idx) STEPB((seg[idx] >> s_cap_shift) & 1, idx)

    while(i < len && ((uintptr_t)(seg + i) & 3u)) {
        STEP(i);
        i++;
    }

    for(; i + 4 <= len; i += 4) {
        uint32_t raw = *(const uint32_t*)(seg + i);
        uint32_t bits = (raw >> s_cap_shift) & 0x01010101u;
        uint32_t edg = (bits ^ ((bits << 8) | (uint32_t)lvl)) & 0x01010101u;
        if(edg) {
            do {
                int idx = __builtin_ctz(edg) >> 3;
                if(ntr == 0) tr_init = lvl;
                if(ntr < 6000) tr[ntr++] = (uint16_t)(pos + i + idx);
                edg &= edg - 1;
            } while(edg);
            lvl = (int)((bits >> 24) & 1u);
        }
    }
    for(; i < len; i++)
        STEP(i);
#undef STEP
#undef STEPB

    s_lvl = lvl;
    s_ntr = ntr;
    s_tr_init = tr_init;
    *pos_io = pos + len;
}

static bool region_valid(void) {
    return (s_ongrid >= 4 && s_ongrid >= s_offgrid) || (s_card_samp >= 246);
}

__attribute__((optimize("O2"))) static int
    find_cut(int start, int* cp, int* mgv, int* mgn, int* mgp) {
    const uint16_t* tr = s_tr;
    int nt = s_ntr;
    int cut_ne = 0, cut_pos = 0, mv = *mgv, mn = *mgn, mp = *mgp, prev = tr[start - 1];
    for(int i = start; i < nt; i++) {
        int cur = tr[i], gap = cur - prev;
        if(gap >= FRAME_GAP) {
            cut_ne = i;
            cut_pos = cur;
        }
        if(gap > mv) {
            mv = gap;
            mn = i;
            mp = cur;
        }
        prev = cur;
    }
    *cp = cut_pos;
    *mgv = mv;
    *mgn = mn;
    *mgp = mp;
    return cut_ne;
}

int nfc_sniffer_hf_capture(char* out_buf, size_t out_cap, uint32_t quiet_ms, uint32_t max_ms) {
    s_stop = false;
    uint32_t timeout_ms = max_ms ? max_ms : 1500;
    uint32_t quiet = quiet_ms ? quiet_ms : QUIET_MS;
    if(!out_buf || out_cap < 64) return NFC_SNIFFER_HF_ERR;

    nfc_sniffer_st25_reader_prep();

    int fld = efd_wait_measure(timeout_ms);
    if(fld < 0) {
        out_buf[0] = '\0';
        return NFC_SNIFFER_HF_TIMEOUT;
    }
    uint8_t gain = (fld <= 2) ? SNIFF_RG_BOOST : SNIFF_RG_MID;
    int couple = (fld >= 6) ? 0 : (fld >= 4) ? 1 : 2;

    uint32_t ring_n = RING_SAMPLES;
    uint8_t* ring = malloc(ring_n);
    s_tr = malloc(6000 * sizeof *s_tr);
    s_fr = malloc(48 * sizeof *s_fr);
    s_txt = malloc(S_TXT_CAP);
    s_paus = malloc(600 * sizeof *s_paus);
    s_seq = malloc(160);
    if(!ring || !s_tr || !s_fr || !s_txt || !s_paus || !s_seq) {
        free(ring);
        free(s_tr);
        free(s_fr);
        free(s_txt);
        free(s_paus);
        free(s_seq);
        s_tr = 0;
        s_fr = 0;
        s_txt = 0;
        s_paus = 0;
        s_seq = 0;
        out_buf[0] = '\0';
        return NFC_SNIFFER_HF_ERR;
    }
    FURI_LOG_I(TAG, "capture field=%d gain=%02X ring=%lu", fld, gain, (unsigned long)ring_n);

    s_cap_idr = &GPIOB->IDR;
    s_cap_shift = MISO_PIN;
    sniff_config(gain);
    furi_hal_gpio_disable_int_callback(&gpio_nfc_irq_rfid_pull);
    st25r3916_mask_irq(h(), ST25R3916_IRQ_MASK_ALL);

    furi_hal_gpio_write(&gpio_nfc_cs, true);
    furi_hal_gpio_init(&gpio_nfc_cs, GpioModeOutputPushPull, GpioPullNo, GpioSpeedVeryHigh);
    direct_cmd(CMD_TRANSPARENT_MODE);
    miso_as_input();
    furi_delay_ms(1);
    dma_circ_arm(ring, ring_n);
    dma_cap_go();

    s_ntr = 0;
    s_lvl = (ring[0] >> MISO_PIN) & 1;
    s_tr_init = s_lvl;
    int pos = 0;

    int plen = 0, saw_activity = 0, rounds = 0, overrun = 0, valid = 0;
    uint32_t read_abs = 0, wr_hi = 0, last_wpos = 0, max_bl = 0;
    s_diag_dense = 0;
    s_diag_maxrun = 0;
    uint32_t abs_base = 0;

    int mg_val = 0, mg_ne = 0, mg_pos = 0;
    FuriThreadPriority base_prio = furi_thread_get_current_priority();
    bool boosted = false;
    uint32_t t0 = furi_get_tick(), last_active = t0;

    for(;;) {
        if(s_stop) break;

        uint32_t write_pos = ring_n - DMA1_Channel1->CNDTR;
        if(write_pos < last_wpos) wr_hi += ring_n;
        last_wpos = write_pos;
        uint32_t backlog = (wr_hi + write_pos) - read_abs;
        if(backlog > max_bl) max_bl = backlog;

        bool active = saw_activity && (furi_get_tick() - last_active) < 40;
        if(active && !boosted) {
            furi_thread_set_current_priority(FuriThreadPriorityHighest);
            boosted = true;
        } else if(!active && boosted) {
            furi_thread_set_current_priority(base_prio);
            boosted = false;
        }

        if(backlog > 9 * ring_n / 10) {
            read_abs = wr_hi + write_pos;
            overrun++;
            s_ntr = 0;
            pos = 0;
            s_lvl = (ring[read_abs % ring_n] >> MISO_PIN) & 1;
            s_tr_init = s_lvl;
            continue;
        }
        if(backlog < PROCESS_MIN) {
            uint32_t now = furi_get_tick();
            if((now - t0) >= timeout_ms) break;
            if(saw_activity && s_ntr == 0 && (now - last_active) >= quiet) break;
            furi_delay_ms(1);
            continue;
        }
        uint32_t take = backlog - RING_MARGIN;
        if(take > MAX_SCAN) take = MAX_SCAN;
        rounds++;

        int ntr0 = s_ntr;
        if(ntr0 == 0) abs_base = read_abs;

        uint32_t read_pos = read_abs % ring_n;
        uint32_t first = ring_n - read_pos;
        if(first > take) first = take;
        scan_span(ring + read_pos, (int)first, &pos);
        if(take > first) scan_span(ring, (int)(take - first), &pos);
        read_abs += take;

        int cut_pos = 0;
        int cut_ne = find_cut((ntr0 > 1 ? ntr0 : 1), &cut_pos, &mg_val, &mg_ne, &mg_pos);
        if(s_ntr > 0 && pos - s_tr[s_ntr - 1] >= FRAME_GAP) {
            cut_ne = s_ntr;
            cut_pos = pos;
        }

        if(cut_ne == 0 && s_ntr >= 3000 && mg_ne > 0) {
            cut_ne = mg_ne;
            cut_pos = mg_pos;
        }
        if(cut_ne == 0 && s_ntr >= 5990) {
            cut_ne = s_ntr;
            cut_pos = pos;
        }

        if(cut_ne > 0) {
            int full = s_ntr;
            s_ntr = cut_ne;
            sniff_decode((uint32_t)cut_pos);
            if(region_valid()) {
                valid++;
                saw_activity = 1;
                last_active = furi_get_tick();
                if(s_nfr && plen < (int)S_TXT_CAP - 160) {
                    plen += fmt_frames(s_txt + plen, (int)S_TXT_CAP - plen, abs_base);
                }
            }
            s_ntr = full - cut_ne;
            for(int i = 0; i < s_ntr; i++)
                s_tr[i] = s_tr[i + cut_ne] - (uint16_t)cut_pos;
            s_tr_init ^= (cut_ne & 1);
            pos -= cut_pos;
            abs_base += cut_pos;
            mg_val = 0;
            mg_ne = 0;
            mg_pos = 0;
        }

        if(backlog < 3u * PROCESS_MIN && (rounds & 3) == 0) furi_thread_yield();

        uint32_t now = furi_get_tick();
        if((now - t0) >= timeout_ms || plen >= (int)S_TXT_CAP - 160) break;

        if(saw_activity && (now - last_active) >= quiet) break;
    }

    if(boosted) furi_thread_set_current_priority(base_prio);

    dma_cap_stop();
    if(s_ntr > 0) {
        sniff_decode((uint32_t)pos);
        if(region_valid() && s_nfr && plen < (int)S_TXT_CAP - 160) {
            valid++;
            plen += fmt_frames(s_txt + plen, (int)S_TXT_CAP - plen, abs_base);
        }
    }

    miso_as_spi();
    direct_cmd(CMD_STOP);
    furi_hal_bus_disable(FuriHalBusTIM2);
    free(ring);

    char hdr[72];
    int hl = snprintf(
        hdr,
        sizeof hdr,
        "L%d f%d o%d v%d m%lu p%u d%lu k%d\n",
        couple,
        fld,
        overrun,
        valid,
        (unsigned long)max_bl,
        (unsigned)((CAP_ARR + 1) * 1000u / 64u),
        (unsigned long)s_diag_dense,
        s_diag_maxrun);
    size_t body = (size_t)((plen > 0) ? plen : 0);
    if((size_t)hl + body >= out_cap) body = out_cap - 1 - (size_t)hl;
    memcpy(out_buf, hdr, (size_t)hl);
    if(body) memcpy(out_buf + hl, s_txt, body);
    out_buf[hl + body] = '\0';

    int ret = (plen > 0) ? valid : NFC_SNIFFER_HF_TIMEOUT;
    free(s_tr);
    free(s_fr);
    free(s_txt);
    free(s_paus);
    free(s_seq);
    s_tr = 0;
    s_fr = 0;
    s_txt = 0;
    s_paus = 0;
    s_seq = 0;
    return ret;
}
