# Makefile del hosttest del core Thumb-2 (PandoraARM).
# NO entra al build del Flipper (vive fuera de lib/, igual que PandoraPIC/hosttest).
#
# Targets:
#   make shim      -> libthumbshim.so (core + shim ctypes para los diffs Python)
#   make arm       -> compila el core para Cortex-M4 (-Werror) y reporta tamano
#   make verify    -> corre las 2 verificaciones (boot vs Unicorn + funciones)
#   make fuzz      -> fuzz de instrucciones individuales vs Unicorn
#   make all       -> shim + arm

CORE_DIR   := ../lib/thumbcore
CORE_SRC   := $(CORE_DIR)/thumb_core.c
CFLAGS     := -std=c11 -Wall -Wextra -Werror -O2 -I$(CORE_DIR)
ARMCC      := arm-none-eabi-gcc
ARMFLAGS   := -mcpu=cortex-m4 -mthumb -Os -std=c11 -Wall -Wextra -Werror -I$(CORE_DIR)

.PHONY: all shim arm verify fuzz clean

all: shim arm

shim: libthumbshim.so

libthumbshim.so: ctypes_shim.c $(CORE_SRC)
	gcc -std=c11 -O2 -fPIC -shared -I$(CORE_DIR) ctypes_shim.c $(CORE_SRC) -o $@

arm:
	$(ARMCC) $(ARMFLAGS) -c $(CORE_SRC) -o /tmp/thumb_core_arm.o
	@arm-none-eabi-size /tmp/thumb_core_arm.o

verify: shim
	python3 diff_unicorn.py pandora 200000
	python3 diff_unicorn.py pandoramax 200000
	python3 diff_funcs.py

fuzz: shim
	python3 diff_fuzz.py 50000

clean:
	rm -f libthumbshim.so libthumbshim_dbg.so /tmp/thumb_core_arm.o
