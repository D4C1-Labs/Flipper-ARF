#!/usr/bin/env python3
"""Verificacion independiente #2: ejecuta FUNCIONES AISLADAS del firmware real en
nuestro interprete C (via ctypes) y en Unicorn, con las MISMAS entradas, hasta un
retorno (LR sentinela). Compara registros r0..r15 + flags y un hash de la RAM
tocada. Deben coincidir exactamente.

Se eligen funciones que ejercitan instrucciones variadas (multiplicacion/division,
bitfields, tablas TBB, extensiones, shifts, loops condicionales IT).

Uso: python3 diff_funcs.py
"""
import sys, os, struct, ctypes, hashlib, random
sys.path.insert(0, "/opt/postmarket/test/emu")
from unicorn import *
from unicorn.arm_const import *

HERE = os.path.dirname(os.path.abspath(__file__))
LIB = os.path.join(HERE, "libthumbshim.so")

FLASH, FLASH_SZ = 0x00000000, 0x00042000
RAM,   RAM_SZ   = 0x20000000, 0x00008000
SENTINEL = 0x00001000  # direccion de retorno (LR); al saltar ahi, paramos

BINS = {
    "pandora":    "/opt/postmarket/test/bins/2_5253623551453304897.flash.bin",
    "pandoramax": "/opt/postmarket/test/bins/PANDORA_MAX.flash.bin",
}

PYREAD  = ctypes.CFUNCTYPE(ctypes.c_uint32, ctypes.c_uint32, ctypes.c_int)
PYWRITE = ctypes.CFUNCTYPE(None, ctypes.c_uint32, ctypes.c_uint32, ctypes.c_int)

REGS = [UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_R3,
        UC_ARM_REG_R4,UC_ARM_REG_R5,UC_ARM_REG_R6,UC_ARM_REG_R7,
        UC_ARM_REG_R8,UC_ARM_REG_R9,UC_ARM_REG_R10,UC_ARM_REG_R11,
        UC_ARM_REG_R12,UC_ARM_REG_SP,UC_ARM_REG_LR,UC_ARM_REG_PC]

MMIO, MMIO_SZ = 0x40000000, 0x00200000
PPB,  PPB_SZ  = 0xE0000000, 0x00100000

def run_case(which, func_addr, args, max_insn=200000, ram_init=None):
    data = open(BINS[which],'rb').read()
    # RAM inicial determinista (misma para ambos): patron pseudoaleatorio fijo.
    if ram_init is None:
        ram_init = bytes(((i*131 + 7) & 0xFF) for i in range(RAM_SZ))
    # ----- Unicorn -----
    uc = Uc(UC_ARCH_ARM, UC_MODE_THUMB)
    try: uc.ctl_set_cpu_model(UC_CPU_ARM_CORTEX_M3)
    except Exception: pass
    uc.mem_map(FLASH, FLASH_SZ); uc.mem_map(RAM, RAM_SZ)
    uc.mem_map(MMIO, MMIO_SZ); uc.mem_map(PPB, PPB_SZ)
    uc.mem_write(FLASH, data)
    uc.mem_write(RAM, ram_init)
    # MMIO: lecturas devuelven 0 (stub), escrituras ignoradas (igual en C)
    def uc_mmio_rd(uc_,acc,addr,size,val,u):
        try: uc_.mem_write(addr, (0).to_bytes(size,'little'))
        except Exception: pass
        return True
    def uc_mmio_wr(uc_,acc,addr,size,val,u):
        # anula el efecto: reescribe 0 para que las lecturas sigan dando 0
        try: uc_.mem_write(addr, (0).to_bytes(size,'little'))
        except Exception: pass
        return True
    uc.hook_add(UC_HOOK_MEM_READ, uc_mmio_rd, begin=MMIO, end=MMIO+MMIO_SZ)
    uc.hook_add(UC_HOOK_MEM_READ, uc_mmio_rd, begin=PPB, end=PPB+PPB_SZ)
    uc.hook_add(UC_HOOK_MEM_WRITE, uc_mmio_wr, begin=MMIO, end=MMIO+MMIO_SZ)
    uc.hook_add(UC_HOOK_MEM_WRITE, uc_mmio_wr, begin=PPB, end=PPB+PPB_SZ)
    # Lecturas/escrituras a direcciones no mapeadas (p.ej. punteros-argumento con
    # valores arbitrarios): mapear la pagina a 0 para que devuelva 0, igual que el
    # core C (cuyo callback devuelve 0 fuera de FLASH/RAM). Mantiene la comparacion
    # valida en funciones que desreferencian punteros.
    def uc_unmapped(uc_,acc,addr,size,val,u):
        try:
            uc_.mem_map(addr & ~0xFFF, 0x1000)
            return True
        except Exception:
            return False
    uc.hook_add(UC_HOOK_MEM_READ_UNMAPPED|UC_HOOK_MEM_WRITE_UNMAPPED, uc_unmapped)
    sp = RAM + RAM_SZ - 0x100
    uc.reg_write(UC_ARM_REG_SP, sp)
    uc.reg_write(UC_ARM_REG_LR, SENTINEL | 1)
    for i,a in enumerate(args): uc.reg_write(REGS[i], a)
    uc.reg_write(UC_ARM_REG_PC, func_addr | 1)
    init_xpsr = uc.reg_read(UC_ARM_REG_XPSR) & 0xF8000000
    # step-by-step hasta el sentinela (evita artefactos de traduccion de bloque
    # cuando interactuan hooks de MMIO/unmapped con emu_start(count=N))
    n = 0
    while n < max_insn:
        pc = uc.reg_read(UC_ARM_REG_PC) & ~1
        if pc == SENTINEL: break
        try:
            uc.emu_start(pc | 1, SENTINEL, count=1)
        except UcError:
            break
        n += 1
    uregs = [uc.reg_read(r) for r in REGS]
    uflags = uc.reg_read(UC_ARM_REG_XPSR) & 0xF8000000
    uram = bytes(uc.mem_read(RAM, RAM_SZ))

    # ----- core C (misma memoria base: FLASH data + RAM inicial) -----
    ram = bytearray(ram_init)
    extra = {}  # bytes escritos a direcciones fuera de FLASH/RAM/MMIO (punteros)
    def is_mmio(addr):
        return (MMIO <= addr < MMIO+MMIO_SZ) or (PPB <= addr < PPB+PPB_SZ)
    def rd(a, s):
        v = 0
        for i in range(s):
            addr = a+i
            if FLASH <= addr < FLASH+len(data): x = data[addr]
            elif RAM <= addr < RAM+RAM_SZ: x = ram[addr-RAM]
            elif is_mmio(addr): x = 0  # MMIO/PPB stub = 0 (igual que Unicorn)
            else: x = extra.get(addr, 0)  # paginas mapeadas a 0 por Unicorn
            v |= x << (8*i)
        return v
    def wr(a, val, s):
        for i in range(s):
            addr = a+i
            b = (val >> (8*i)) & 0xFF
            if RAM <= addr < RAM+RAM_SZ: ram[addr-RAM] = b
            elif is_mmio(addr): pass
            else: extra[addr] = b
    cr = PYREAD(rd); cw = PYWRITE(wr)

    lib = ctypes.CDLL(LIB)
    lib.shim_get_reg.restype = ctypes.c_uint32
    lib.shim_get_xpsr.restype = ctypes.c_uint32
    lib.shim_get_pc.restype = ctypes.c_uint32
    lib.shim_step.restype = ctypes.c_int
    lib.shim_init(); lib.shim_set_callbacks(cr, cw)
    lib.shim_reset(sp, func_addr)
    lib.shim_set_reg(14, SENTINEL | 1)
    for i,a in enumerate(args): lib.shim_set_reg(i, a)
    # sincroniza flags iniciales con Unicorn
    lib.shim_set_xpsr(init_xpsr | 0x01000000)
    n = 0
    while n < max_insn:
        pc = lib.shim_get_pc()
        if (pc & ~1) == SENTINEL: break
        if lib.shim_step() < 0: break
        n += 1
    cregs = [lib.shim_get_reg(i) for i in range(16)]
    cflags = lib.shim_get_xpsr() & 0xF8000000
    cram = bytes(ram)

    ok = True
    detail = []
    for i in range(15):  # no comparamos PC (ambos en sentinela)
        if (cregs[i]&0xFFFFFFFF) != (uregs[i]&0xFFFFFFFF):
            ok = False; detail.append("r%d C=0x%08x U=0x%08x"%(i,cregs[i],uregs[i]))
    if cflags != uflags:
        ok = False; detail.append("flags C=0x%08x U=0x%08x"%(cflags,uflags))
    if hashlib.sha256(cram).digest() != hashlib.sha256(uram).digest():
        ok = False
        # localizar primer byte distinto
        for j in range(RAM_SZ):
            if cram[j] != uram[j]:
                detail.append("RAM@0x%08x C=0x%02x U=0x%02x"%(RAM+j,cram[j],uram[j])); break
    return ok, detail, (cregs, uregs)

def main():
    random.seed(1234)
    cases = []
    # Funciones del app region de pandoramax que ejercitan udiv/clz/tbb/ubfx.
    # Ejecutamos un rango de direcciones como "funciones" probando varias entradas.
    # Cada caso: (nombre, which, addr, [args...])
    # Entradas REALES de funcion (prologo push{..,lr}) del app de pandoramax.
    entries = [0x8298,0x829c,0x8342,0x8376,0x83a0,0x8448,0x84ec,0x8560,0x85e0,
               0x8630,0x8700,0x87d8,0x893c,0x8a5c,0x8ae4,0x8cc6,0x8df0,0x8f24,
               0x8fa8,0x901c]
    for addr in entries:
        for trial in range(3):
            args = [random.randint(0,0xFFFFFFFF) for _ in range(4)]
            cases.append((f"fn_0x{addr:x}#{trial}", "pandoramax", addr, args))

    npass = 0; nfail = 0
    for name, which, addr, args in cases:
        ok, detail, _ = run_case(which, addr, args)
        if ok:
            npass += 1
            print(f"  [OK] {name:14s} @0x{addr:x} args={[hex(a) for a in args]}")
        else:
            nfail += 1
            print(f"  [FAIL] {name:14s} @0x{addr:x} args={[hex(a) for a in args]}")
            for d in detail[:5]: print("       ", d)
    print(f"\nRESULTADO: {npass} OK, {nfail} FAIL de {len(cases)} casos")
    return 0 if nfail == 0 else 1

if __name__ == "__main__":
    sys.exit(main())
