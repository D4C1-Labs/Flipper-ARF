#!/usr/bin/env python3
"""Fuzz de instrucciones individuales: genera encodings de una lista de plantillas
(aritmetica, logica, shifts, bitfields, mul/div, ext, rev/clz) con operandos
aleatorios, los ejecuta en el core C y en Unicorn con registros aleatorios, y
compara r0..r15 + flags. Complementa la verificacion de boot y de funciones.

Uso: python3 diff_fuzz.py [N]
"""
import sys, ctypes, random
sys.path.insert(0, "/opt/postmarket/test/emu")
from unicorn import *
from unicorn.arm_const import *

LIB = "/opt/postmarket/test/Flipper-ARF/applications/system/PandoraARM/hosttest/libthumbshim.so"
CODE = 0x1000  # donde ponemos la instruccion a probar
REGS = [UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_R3,
        UC_ARM_REG_R4,UC_ARM_REG_R5,UC_ARM_REG_R6,UC_ARM_REG_R7,
        UC_ARM_REG_R8,UC_ARM_REG_R9,UC_ARM_REG_R10,UC_ARM_REG_R11,
        UC_ARM_REG_R12,UC_ARM_REG_SP,UC_ARM_REG_LR,UC_ARM_REG_PC]

# Plantillas de 16-bit (func -> genera bytes). rdn/rm limitados a r0..r7.
def t16_dp(opc):
    rdn = random.randint(0,7); rm = random.randint(0,7)
    return (0x4000 | (opc<<6) | (rm<<3) | rdn).to_bytes(2,'little')
def t16_shift_imm(ty):
    imm5=random.randint(0,31); rm=random.randint(0,7); rd=random.randint(0,7)
    return ((ty<<11)|(imm5<<6)|(rm<<3)|rd).to_bytes(2,'little')
def t16_addsub_reg(sub):
    rm=random.randint(0,7);rn=random.randint(0,7);rd=random.randint(0,7)
    return (0x1800|(sub<<9)|(rm<<6)|(rn<<3)|rd).to_bytes(2,'little')
def t16_mov_imm():
    rd=random.randint(0,7);imm=random.randint(0,255)
    return (0x2000|(rd<<8)|imm).to_bytes(2,'little')
def t16_cmp_imm():
    rd=random.randint(0,7);imm=random.randint(0,255)
    return (0x2800|(rd<<8)|imm).to_bytes(2,'little')
def t16_add_imm():
    rd=random.randint(0,7);imm=random.randint(0,255)
    return (0x3000|(rd<<8)|imm).to_bytes(2,'little')
def t16_sub_imm():
    rd=random.randint(0,7);imm=random.randint(0,255)
    return (0x3800|(rd<<8)|imm).to_bytes(2,'little')
def t16_ext(sub):
    rm=random.randint(0,7);rd=random.randint(0,7)
    return (0xB200|(sub<<6)|(rm<<3)|rd).to_bytes(2,'little')
def t16_rev(sub):
    rm=random.randint(0,7);rd=random.randint(0,7)
    return (0xBA00|(sub<<6)|(rm<<3)|rd).to_bytes(2,'little')

# 32-bit
def t32_dp_imm(op):
    # data-proc modified immediate, S=1
    rn=random.randint(0,7);rd=random.randint(0,7)
    i=random.randint(0,1);imm3=random.randint(0,7);imm8=random.randint(0,255)
    hw1=0xF000|(i<<10)|(op<<5)|(1<<4)|rn
    hw2=(imm3<<12)|(rd<<8)|imm8
    return hw1.to_bytes(2,'little')+hw2.to_bytes(2,'little')
def t32_dp_reg(op):
    rn=random.randint(0,7);rd=random.randint(0,7);rm=random.randint(0,7)
    imm3=random.randint(0,7);imm2=random.randint(0,3);ty=random.randint(0,3)
    hw1=0xEA00|(op<<5)|(1<<4)|rn
    hw2=(imm3<<12)|(rd<<8)|(imm2<<6)|(ty<<4)|rm
    return hw1.to_bytes(2,'little')+hw2.to_bytes(2,'little')
def t32_mul():
    rn=random.randint(0,7);rd=random.randint(0,7);rm=random.randint(0,7);ra=15
    hw1=0xFB00|rn; hw2=(ra<<12)|(rd<<8)|rm
    return hw1.to_bytes(2,'little')+hw2.to_bytes(2,'little')
def t32_mla():
    rn=random.randint(0,7);rd=random.randint(0,7);rm=random.randint(0,7);ra=random.randint(0,7)
    hw1=0xFB00|rn; hw2=(ra<<12)|(rd<<8)|rm
    return hw1.to_bytes(2,'little')+hw2.to_bytes(2,'little')
def t32_longmul(op):  # 0 smull,2 umull
    rn=random.randint(0,7);rm=random.randint(0,7)
    rdlo=random.randint(0,3);rdhi=random.randint(4,7)
    hw1=0xFB80|(op<<4)|rn; hw2=(rdlo<<12)|(rdhi<<8)|rm
    return hw1.to_bytes(2,'little')+hw2.to_bytes(2,'little')
def t32_div(sdiv):  # sdiv=1 => op=1, udiv => op=3
    op=1 if sdiv else 3
    rn=random.randint(0,7);rm=random.randint(1,7);rd=random.randint(0,7)
    hw1=0xFB80|(op<<4)|rn; hw2=(0xF<<12)|(rd<<8)|(0xF<<4)|rm
    return hw1.to_bytes(2,'little')+hw2.to_bytes(2,'little')
def t32_clz():
    rm=random.randint(0,7);rd=random.randint(0,7)
    hw1=0xFAB0|rm; hw2=(0xF<<12)|(rd<<8)|(0x8<<4)|rm
    return hw1.to_bytes(2,'little')+hw2.to_bytes(2,'little')
def t32_rbit():
    rm=random.randint(0,7);rd=random.randint(0,7)
    hw1=0xFA90|rm; hw2=(0xF<<12)|(rd<<8)|(0xA<<4)|rm
    return hw1.to_bytes(2,'little')+hw2.to_bytes(2,'little')
def t32_ubfx():
    rn=random.randint(0,7);rd=random.randint(0,7)
    lsb=random.randint(0,20);width=random.randint(1,11)
    imm3=(lsb>>2)&7;imm2=lsb&3;widthm1=width-1
    hw1=0xF3C0|rn; hw2=(imm3<<12)|(rd<<8)|(imm2<<6)|widthm1
    return hw1.to_bytes(2,'little')+hw2.to_bytes(2,'little')
def t32_sbfx():
    rn=random.randint(0,7);rd=random.randint(0,7)
    lsb=random.randint(0,20);width=random.randint(1,11)
    imm3=(lsb>>2)&7;imm2=lsb&3;widthm1=width-1
    hw1=0xF340|rn; hw2=(imm3<<12)|(rd<<8)|(imm2<<6)|widthm1
    return hw1.to_bytes(2,'little')+hw2.to_bytes(2,'little')
def t32_bfi():
    rn=random.randint(0,7);rd=random.randint(0,7)
    lsb=random.randint(0,20);msb=lsb+random.randint(0,11)
    if msb>31:msb=31
    imm3=(lsb>>2)&7;imm2=lsb&3
    hw1=0xF360|rn; hw2=(imm3<<12)|(rd<<8)|(imm2<<6)|msb
    return hw1.to_bytes(2,'little')+hw2.to_bytes(2,'little')
def t32_movw():
    rd=random.randint(0,7);imm16=random.randint(0,0xFFFF)
    i=(imm16>>11)&1;imm4=(imm16>>12)&0xF;imm3=(imm16>>8)&7;imm8=imm16&0xFF
    hw1=0xF240|(i<<10)|imm4; hw2=(imm3<<12)|(rd<<8)|imm8
    return hw1.to_bytes(2,'little')+hw2.to_bytes(2,'little')

TEMPLATES = []
for opc in range(16): TEMPLATES.append(("dp16_%x"%opc, lambda o=opc: t16_dp(o)))
for ty in range(3): TEMPLATES.append(("sh16_%d"%ty, lambda t=ty: t16_shift_imm(t)))
TEMPLATES += [("addreg",lambda:t16_addsub_reg(0)),("subreg",lambda:t16_addsub_reg(1)),
              ("movimm",t16_mov_imm),("cmpimm",t16_cmp_imm),("addimm",t16_add_imm),("subimm",t16_sub_imm)]
for s in range(4): TEMPLATES.append(("ext%d"%s, lambda x=s: t16_ext(x)))
for s in (0,1,3): TEMPLATES.append(("rev%d"%s, lambda x=s: t16_rev(x)))
for op in (0,1,2,3,4,8,0xA,0xB,0xD,0xE): TEMPLATES.append(("dpimm_%x"%op, lambda o=op:t32_dp_imm(o)))
for op in (0,1,2,3,4,8,0xA,0xB,0xD,0xE): TEMPLATES.append(("dpreg_%x"%op, lambda o=op:t32_dp_reg(o)))
TEMPLATES += [("mul",t32_mul),("mla",t32_mla),("smull",lambda:t32_longmul(0)),
              ("umull",lambda:t32_longmul(2)),("sdiv",lambda:t32_div(1)),("udiv",lambda:t32_div(0)),
              ("clz",t32_clz),("rbit",t32_rbit),("ubfx",t32_ubfx),("sbfx",t32_sbfx),
              ("bfi",t32_bfi),("movw",t32_movw)]

def main():
    N = int(sys.argv[1]) if len(sys.argv)>1 else 20000
    random.seed(99)
    lib = ctypes.CDLL(LIB)
    lib.shim_get_reg.restype=ctypes.c_uint32;lib.shim_get_xpsr.restype=ctypes.c_uint32
    lib.shim_get_pc.restype=ctypes.c_uint32;lib.shim_step.restype=ctypes.c_int

    mem = bytearray(0x2000)
    PR=ctypes.CFUNCTYPE(ctypes.c_uint32,ctypes.c_uint32,ctypes.c_int)
    PW=ctypes.CFUNCTYPE(None,ctypes.c_uint32,ctypes.c_uint32,ctypes.c_int)
    def rd(a,s):
        v=0
        for i in range(s):
            v|= (mem[(a+i)%0x2000])<<(8*i)
        return v
    def wr(a,val,s):
        for i in range(s): mem[(a+i)%0x2000]=(val>>(8*i))&0xFF
    cr=PR(rd);cw=PW(wr)
    lib.shim_init(); lib.shim_set_callbacks(cr,cw)

    fails = 0; perstat = {}
    for it in range(N):
        name, gen = random.choice(TEMPLATES)
        code = gen()
        regs = [random.choice([0,1,2,0xFFFFFFFF,0x80000000,0x7FFFFFFF,
                random.randint(0,0xFFFFFFFF)]) for _ in range(13)]
        flags = random.choice([0,0x20000000,0x60000000,0xA0000000,0xF0000000])
        # Unicorn: engine fresco por iteracion (evita cualquier bleed de estado)
        uc = Uc(UC_ARCH_ARM, UC_MODE_THUMB)
        try: uc.ctl_set_cpu_model(UC_CPU_ARM_CORTEX_M3)
        except Exception: pass
        uc.mem_map(0, 0x4000)
        uc.mem_write(CODE, code + b"\x00\xbf")  # + nop padding
        for i in range(13): uc.reg_write(REGS[i], regs[i])
        uc.reg_write(UC_ARM_REG_SP, 0x800); uc.reg_write(UC_ARM_REG_LR, 0x1)
        uc.reg_write(UC_ARM_REG_XPSR, flags | 0x01000000)
        uc.reg_write(UC_ARM_REG_PC, CODE|1)
        try: uc.emu_start(CODE|1, CODE+len(code), count=1)
        except UcError: continue
        uregs=[uc.reg_read(r) for r in REGS]; uf=uc.reg_read(UC_ARM_REG_XPSR)&0xF8000000
        # C
        for i in range(len(mem)): mem[i]=0
        mem[CODE:CODE+len(code)] = code
        lib.shim_reset(0x800, CODE)
        for i in range(13): lib.shim_set_reg(i, regs[i])
        lib.shim_set_reg(14,1)
        lib.shim_set_xpsr(flags|0x01000000)
        lib.shim_step()
        cregs=[lib.shim_get_reg(i) for i in range(16)]; cf=lib.shim_get_xpsr()&0xF8000000
        bad=False
        for i in range(13):
            if (cregs[i]&0xFFFFFFFF)!=(uregs[i]&0xFFFFFFFF): bad=True
        if cf!=uf: bad=True
        if bad:
            fails+=1
            perstat[name]=perstat.get(name,0)+1
            if fails<=12:
                print("FAIL %s code=%s regs_in=%s flags=%08x"%(name,code.hex(),
                      [hex(x) for x in regs],flags))
                print("  C  :"," ".join("%08x"%x for x in cregs[:13]),"f=%08x"%cf)
                print("  Uni:"," ".join("%08x"%x for x in uregs[:13]),"f=%08x"%uf)
    print("\nFUZZ: %d casos, %d fallos"%(N,fails))
    if perstat: print("  por plantilla:",perstat)
    return 0 if fails==0 else 1

if __name__=="__main__":
    sys.exit(main())
