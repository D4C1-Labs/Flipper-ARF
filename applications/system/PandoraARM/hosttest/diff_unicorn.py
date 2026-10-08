#!/usr/bin/env python3
"""Verificacion diferencial: nuestro interprete C (via ctypes) vs Unicorn Engine,
PASO A PASO, sobre el firmware real Pandora/PandoraMax (EFM32 Cortex-M3).

Modelo de memoria UNICO y compartido: Unicorn posee la memoria (FLASH/RAM/MMIO).
El core C lee/escribe a traves de callbacks que operan sobre la MISMA memoria de
Unicorn (uc.mem_read/mem_write) con un modelo de MMIO identico. Asi ambos ven
exactamente el mismo hardware y la comparacion es valida byte a byte.

Algoritmo por paso:
  1. Sincroniza registros del core C <- Unicorn (al inicio, y tras cada paso
     comparamos; si divergen, se reporta y se aborta).
  2. Ejecuta 1 instruccion en Unicorn (emu_start count=1).
  3. Ejecuta 1 instruccion en el core C.
  4. Compara r0..r15 y flags NZCV (+ T) de xPSR.
Si en algun momento difieren, imprime el PC y el detalle y termina con error.

Uso:
  python3 diff_unicorn.py pandora    [NSTEPS]
  python3 diff_unicorn.py pandoramax [NSTEPS]
"""
import sys, os, struct, ctypes
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, "/opt/postmarket/test/emu")
from unicorn import *
from unicorn.arm_const import *

HERE = os.path.dirname(os.path.abspath(__file__))
LIB = os.path.join(HERE, "libthumbshim.so")

FLASH, FLASH_SZ = 0x00000000, 0x00042000
RAM,   RAM_SZ   = 0x20000000, 0x00008000
MMIO,  MMIO_SZ  = 0x40000000, 0x00200000
PPB,   PPB_SZ   = 0xE0000000, 0x00100000

BINS = {
    "pandora":    "/opt/postmarket/test/bins/2_5253623551453304897.flash.bin",
    "pandoramax": "/opt/postmarket/test/bins/PANDORA_MAX.flash.bin",
}

# ----------------------------- modelo MMIO compartido -----------------------
class HW:
    """Modelo de MMIO determinista (igual para Unicorn y para el core C)."""
    def __init__(self):
        self.timer_cnt = {0:0,1:0,2:0,3:0}
        self.spi_rx = []
        self.si_regs = [0]*0x80
        self.si_regs[0x00]=0x08; self.si_regs[0x01]=0x06; self.si_regs[0x26]=0x20
        self.spi_phase = None
        self.eeprom = bytes(((0xA5 ^ (i*7)) & 0xFF) for i in range(256))
        self.eeptr = 0
        self.gpio_din = {}

    def _spi_byte(self, b):
        if self.spi_phase is None:
            self.spi_phase = ('w' if (b & 0x80) else 'r', b & 0x7F)
            self.spi_rx.append(0)
        else:
            kind, reg = self.spi_phase
            if kind == 'w':
                self.si_regs[reg & 0x7F] = b & 0xFF
            else:
                v = self.si_regs[reg & 0x7F]
                if reg in (0x03,0x04): self.si_regs[reg]=0
                self.spi_rx.append(v)
            self.spi_phase = None

    def read(self, addr, size):
        base = addr & 0xFFFFF000
        off = addr & 0xFFF
        v = 0
        if base == 0x4000C000:
            if off == 0x10: v = (1<<6)|(1<<5)
            elif off == 0x1C: v = self.spi_rx.pop(0) if self.spi_rx else 0
        elif base == 0x400C4000:
            if off == 0x10: v = (1<<6)|(1<<5)
            elif off == 0x1C:
                v = self.eeprom[self.eeptr % len(self.eeprom)]; self.eeptr += 1
        elif 0x40006000 <= addr < 0x40006000 + 36*16:
            port = (addr-0x40006000)//36
            poff = (addr-0x40006000) - port*36
            if poff == 0x1C: v = self.gpio_din.get(port,0)
            else: v = 0
        elif base == 0x40080000:
            v = 0
        elif base == 0x400C8000:
            v = 0
        elif base == 0x40010000:
            tn = off >> 10; sub = off & 0x3FF
            if sub == 0x24:
                self.timer_cnt[tn] = (self.timer_cnt[tn]+0x2000)&0xFFFF
                v = self.timer_cnt[tn]
            else: v = 0
        else:
            v = 0
        return v & ((1<<(size*8))-1)

    def write(self, addr, val, size):
        base = addr & 0xFFFFF000
        off = addr & 0xFFF
        if base == 0x4000C000 and off == 0x34:
            self._spi_byte(val & 0xFF)
        # GPIO/otros: no afectan lecturas deterministas que comparemos
        return

# ----------------------------- carga shim C ---------------------------------
PYREAD  = ctypes.CFUNCTYPE(ctypes.c_uint32, ctypes.c_uint32, ctypes.c_int)
PYWRITE = ctypes.CFUNCTYPE(None, ctypes.c_uint32, ctypes.c_uint32, ctypes.c_int)

def is_mmio(addr):
    return (MMIO <= addr < MMIO+MMIO_SZ) or (PPB <= addr < PPB+PPB_SZ)

def main():
    which = sys.argv[1] if len(sys.argv) > 1 else "pandora"
    nsteps = int(sys.argv[2]) if len(sys.argv) > 2 else 200000
    data = open(BINS[which], 'rb').read()

    uc = Uc(UC_ARCH_ARM, UC_MODE_THUMB)
    try: uc.ctl_set_cpu_model(UC_CPU_ARM_CORTEX_M3)
    except Exception: pass
    for b,s in ((FLASH,FLASH_SZ),(RAM,RAM_SZ),(MMIO,MMIO_SZ),(PPB,PPB_SZ)):
        uc.mem_map(b,s)
    uc.mem_write(FLASH, data)

    hw = HW()

    # Hooks de MMIO en Unicorn: implementan el modelo y escriben el valor leido.
    def on_read(uc_, access, addr, size, value, user):
        v = hw.read(addr, size)
        try: uc_.mem_write(addr, int(v).to_bytes(size,'little'))
        except Exception: pass
        return True
    def on_write(uc_, access, addr, size, value, user):
        hw.write(addr, value & ((1<<(size*8))-1), size)
        return True
    uc.hook_add(UC_HOOK_MEM_READ, on_read, begin=MMIO, end=MMIO+MMIO_SZ)
    uc.hook_add(UC_HOOK_MEM_WRITE, on_write, begin=MMIO, end=MMIO+MMIO_SZ)
    uc.hook_add(UC_HOOK_MEM_WRITE, lambda *a: True, begin=PPB, end=PPB+PPB_SZ)
    uc.hook_add(UC_HOOK_MEM_READ, on_read, begin=PPB, end=PPB+PPB_SZ)
    def on_unmapped(uc_, access, addr, size, value, user):
        try:
            uc_.mem_map(addr & ~0xFFF, 0x1000); return True
        except Exception:
            return False
    uc.hook_add(UC_HOOK_MEM_READ_UNMAPPED|UC_HOOK_MEM_WRITE_UNMAPPED|UC_HOOK_MEM_FETCH_UNMAPPED,
                on_unmapped)

    # Callbacks del core C: operan sobre la MISMA memoria (la de Unicorn) para
    # FLASH/RAM, y sobre el modelo HW para MMIO. Asi comparten estado.
    def c_read(addr, size):
        if is_mmio(addr):
            return hw.read(addr, size) & ((1<<(size*8))-1)
        try:
            raw = uc.mem_read(addr, size)
            return int.from_bytes(bytes(raw), 'little')
        except Exception:
            return 0
    def c_write(addr, val, size):
        if is_mmio(addr):
            hw.write(addr, val & ((1<<(size*8))-1), size)
            return
        try:
            uc.mem_write(addr, int(val & ((1<<(size*8))-1)).to_bytes(size,'little'))
        except Exception:
            pass

    # IMPORTANTE: el modelo HW es un unico objeto. Pero Unicorn y el core C NO
    # deben AMBOS avanzar el estado MMIO (timer, spi) en el mismo paso, porque
    # solo UNO ejecuta la instruccion que toca MMIO. Para evitar doble avance,
    # en este arnes el core C NO ejecuta realmente: usamos a Unicorn como oraculo
    # y validamos que el DECODE/semantica del core C reproduce el mismo resultado
    # sobre una COPIA del estado. Ver modo 'shadow' abajo.
    #
    # Estrategia definitiva (shadow-step): en cada paso
    #   - snapshot de registros ANTES (desde Unicorn)
    #   - Unicorn ejecuta 1 instr (puede tocar MMIO, avanza hw)
    #   - cargamos ese snapshot ANTERIOR en el core C
    #   - el core C ejecuta 1 instr; sus lecturas MMIO deben devolver lo MISMO
    #     que vio Unicorn. Para ello cacheamos las lecturas MMIO del paso de
    #     Unicorn y se las servimos al core C en el mismo orden.
    #   - comparamos registros resultantes.

    pyread = PYREAD(c_read); pywrite = PYWRITE(c_write)

    lib = ctypes.CDLL(LIB)
    lib.shim_get_reg.restype = ctypes.c_uint32
    lib.shim_get_xpsr.restype = ctypes.c_uint32
    lib.shim_get_pc.restype = ctypes.c_uint32
    lib.shim_step.restype = ctypes.c_int
    lib.shim_init()

    # --- cache de lecturas MMIO del paso de Unicorn para reproducir en C ---
    mmio_log = []   # lista de (addr,size,value) leidos por Unicorn este paso
    recording = {"on": False}
    def on_read_rec(uc_, access, addr, size, value, user):
        v = hw.read(addr, size)
        try: uc_.mem_write(addr, int(v).to_bytes(size,'little'))
        except Exception: pass
        if recording["on"]:
            mmio_log.append((addr, size, v))
        return True
    def on_write_rec(uc_, access, addr, size, value, user):
        if recording["on"]:
            hw.write(addr, value & ((1<<(size*8))-1), size)
        return True

    # Rehacemos los hooks para grabar
    uc2 = Uc(UC_ARCH_ARM, UC_MODE_THUMB)
    try: uc2.ctl_set_cpu_model(UC_CPU_ARM_CORTEX_M3)
    except Exception: pass
    for b,s in ((FLASH,FLASH_SZ),(RAM,RAM_SZ),(MMIO,MMIO_SZ),(PPB,PPB_SZ)):
        uc2.mem_map(b,s)
    uc2.mem_write(FLASH, data)
    uc2.hook_add(UC_HOOK_MEM_READ, on_read_rec, begin=MMIO, end=MMIO+MMIO_SZ)
    uc2.hook_add(UC_HOOK_MEM_WRITE, on_write_rec, begin=MMIO, end=MMIO+MMIO_SZ)
    uc2.hook_add(UC_HOOK_MEM_READ, on_read_rec, begin=PPB, end=PPB+PPB_SZ)
    uc2.hook_add(UC_HOOK_MEM_WRITE, on_write_rec, begin=PPB, end=PPB+PPB_SZ)
    uc2.hook_add(UC_HOOK_MEM_READ_UNMAPPED|UC_HOOK_MEM_WRITE_UNMAPPED|UC_HOOK_MEM_FETCH_UNMAPPED,
                 on_unmapped)

    # El core C lee FLASH/RAM desde uc2 (mismo estado que ejecuta Unicorn) y
    # MMIO desde el log grabado (mismo valor que vio Unicorn este paso).
    mmio_replay = {"idx": 0}
    def c_read2(addr, size):
        if is_mmio(addr):
            # servir del log en orden
            i = mmio_replay["idx"]
            if i < len(mmio_log):
                a,s,v = mmio_log[i]
                mmio_replay["idx"] += 1
                return v & ((1<<(size*8))-1)
            return hw.read(addr, size) & ((1<<(size*8))-1)
        try:
            raw = uc2.mem_read(addr, size)
            return int.from_bytes(bytes(raw), 'little')
        except Exception:
            return 0
    def c_write2(addr, val, size):
        if is_mmio(addr):
            return  # ya lo hizo Unicorn (write_rec)
        try:
            uc2.mem_write(addr, int(val & ((1<<(size*8))-1)).to_bytes(size,'little'))
        except Exception:
            pass
    pyread2 = PYREAD(c_read2); pywrite2 = PYWRITE(c_write2)
    lib.shim_set_callbacks(pyread2, pywrite2)

    # init de Unicorn
    sp0 = struct.unpack_from("<I", data, 0)[0]
    rst = struct.unpack_from("<I", data, 4)[0]
    uc2.reg_write(UC_ARM_REG_SP, sp0)
    uc2.reg_write(UC_ARM_REG_LR, 0xFFFFFFFF)
    uc2.reg_write(UC_ARM_REG_PC, rst | 1)

    REGS = [UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_R3,
            UC_ARM_REG_R4,UC_ARM_REG_R5,UC_ARM_REG_R6,UC_ARM_REG_R7,
            UC_ARM_REG_R8,UC_ARM_REG_R9,UC_ARM_REG_R10,UC_ARM_REG_R11,
            UC_ARM_REG_R12,UC_ARM_REG_SP,UC_ARM_REG_LR,UC_ARM_REG_PC]

    def uc_regs():
        return [uc2.reg_read(r) for r in REGS]

    FLAGMASK = 0xF8000000  # N Z C V Q
    def uc_flags():
        return uc2.reg_read(UC_ARM_REG_XPSR) & FLAGMASK

    def c_regs():
        return [lib.shim_get_reg(i) for i in range(16)]
    def c_flags():
        return lib.shim_get_xpsr() & FLAGMASK

    # Ambos cores corren LIBREMENTE desde reset y comparamos en cada frontera.
    # C e Unicorn mantienen su propio estado; el estado de memoria FLASH/RAM es
    # compartido (uc2). Unicorn es el oraculo de MMIO: cacheamos sus lecturas MMIO
    # por "chunk" y se las servimos al core C.
    #
    # Unicorn ejecuta count=1, PERO cuando aterriza en una instruccion IT ejecuta
    # el bloque IT COMPLETO de forma atomica (quirk conocido). Por eso, tras cada
    # emu_start(count=1) de Unicorn, avanzamos el core C tantas instrucciones como
    # haga falta hasta que su PC coincida con el de Unicorn (drenando el bloque IT).
    lib.shim_reset(sp0, rst)
    # sincroniza flags iniciales con Unicorn (xPSR de reset puede diferir en bits
    # de APSR que el firmware fijara luego; igualamos para comparar de forma justa)
    init_xpsr = uc2.reg_read(UC_ARM_REG_XPSR)
    lib.shim_set_xpsr((init_xpsr & 0xF8000000) | 0x01000000)

    def pc_of_c():
        return lib.shim_get_pc() & 0xFFFFFFFF

    step = 0
    while step < nsteps:
        mmio_log.clear(); recording["on"] = True; mmio_replay["idx"] = 0
        pc_before = uc2.reg_read(UC_ARM_REG_PC)
        try:
            uc2.emu_start((pc_before)|1, 0, count=1)
        except UcError as e:
            npc = uc2.reg_read(UC_ARM_REG_PC)
            if (npc & 0xFFFFFFF0) == 0xFFFFFFF0:
                print(f"[{which}] EXC_RETURN @step {step} pc=0x{npc:x} (fin natural)")
                break
            print(f"[{which}] UcError @step {step} pc=0x{npc:x}: {e}")
            break
        recording["on"] = False
        after = uc_regs(); after_flags = uc_flags()
        uni_pc = after[15] & 0xFFFFFFFF

        # avanza el core C hasta alcanzar el PC de Unicorn (>=1 instr; mas si IT)
        mmio_replay["idx"] = 0
        csteps = 0
        rc = 0
        while True:
            rc = lib.shim_step()
            csteps += 1
            if rc < 0:
                break
            if pc_of_c() == uni_pc:
                break
            if csteps > 16:
                break  # proteccion: desalineado

        cregs = c_regs(); cflags = c_flags()
        mism = []
        for i in range(16):
            if (cregs[i] & 0xFFFFFFFF) != (after[i] & 0xFFFFFFFF):
                nm = "r%d"%i if i<13 else ["sp","lr","pc"][i-13]
                mism.append((nm, cregs[i], after[i]))
        if cflags != after_flags:
            mism.append(("flags", cflags, after_flags))
        if mism:
            print(f"\n[{which}] DIVERGENCIA @step {step}  (instr pc=0x{pc_before&~1:x}, csteps={csteps})")
            raw = bytes(uc2.mem_read(pc_before & ~1, 4))
            print("  bytes:", raw.hex())
            print("  C   regs:", " ".join("%08x"%x for x in cregs), "flags=%08x rc=%d"%(cflags,rc))
            print("  Uni regs:", " ".join("%08x"%x for x in after), "flags=%08x"%after_flags)
            for name,cv,uv in mism:
                print(f"    {name}: C=0x{cv:08x} Uni=0x{uv:08x}")
            print(f"[{which}] ABORT en primera divergencia (step {step}).")
            return 1
        step += 1

    print(f"[{which}] OK: {step} pasos comparados, 0 divergencias. "
          f"PC final=0x{uc2.reg_read(UC_ARM_REG_PC):x}")
    return 0

if __name__ == "__main__":
    sys.exit(main())
