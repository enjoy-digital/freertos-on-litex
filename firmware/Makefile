# Firmware build for litex_freertos.
#
# Invocation:
#   make BUILD_DIRECTORY=<litex_sim output dir>               # default demo
#   make BUILD_DIRECTORY=<...> DEMO=blinky_only               # smaller demo
#
# Copyright (c) 2026 EnjoyDigital <florent@enjoy-digital.fr>
# SPDX-License-Identifier: BSD-2-Clause

# --- Inputs ----------------------------------------------------------

# `clean` and `help` can run without a LiteX SoC build directory.
NO_BUILDDIR_TARGETS := clean help

ifeq ($(filter $(NO_BUILDDIR_TARGETS),$(MAKECMDGOALS)),)
ifeq ($(BUILD_DIRECTORY),)
$(error BUILD_DIRECTORY is not set. Point it at a litex_sim output directory \
(e.g. make BUILD_DIRECTORY=/path/to/sim-build). Run sim/gen_soc.py to \
create one.)
endif

BUILD_DIR := $(abspath $(BUILD_DIRECTORY))

include $(BUILD_DIR)/software/include/generated/variables.mak
include $(SOC_DIRECTORY)/software/common.mak
endif

# --- Paths -----------------------------------------------------------

FW_DIR      := $(abspath .)
RTOS_DIR    := $(abspath third_party/FreeRTOS-Kernel)
RTOS_PORT   := $(RTOS_DIR)/portable/GCC/RISC-V
RTOS_MM     := $(RTOS_DIR)/portable/MemMang
EXAMPLE_DIR := $(abspath ../examples)

# --- Demo selection --------------------------------------------------

# DEMO selects which file in examples/ provides `app_main(void)`.
# main.c calls app_main() after printing the banner but before starting
# the scheduler, so the demo's job is to create tasks.
DEMO ?= full_demo
DEMO_SRC := $(EXAMPLE_DIR)/$(DEMO).c

ifneq ($(wildcard $(DEMO_SRC)),$(DEMO_SRC))
$(error DEMO='$(DEMO)' not found. Expected $(DEMO_SRC). \
Available: $(notdir $(basename $(wildcard $(EXAMPLE_DIR)/*.c))))
endif

# --- Sources ---------------------------------------------------------

RTOS_OBJS := \
    tasks.o queue.o list.o timers.o event_groups.o stream_buffer.o \
    port.o portASM.o heap_4.o

FW_OBJS := main.o port_litex.o $(DEMO).o crt0.o

OBJECTS := $(FW_OBJS) $(RTOS_OBJS)

# Source lookup. FW_DIR must come first so our crt0.S beats LiteX's
# CPU_DIRECTORY version.
vpath %.c $(FW_DIR) $(EXAMPLE_DIR) \
          $(RTOS_DIR) $(RTOS_PORT) $(RTOS_MM) \
          $(BIOS_DIRECTORY) $(BIOS_DIRECTORY)/cmds $(CPU_DIRECTORY)
vpath %.S $(FW_DIR) $(RTOS_PORT) \
          $(BIOS_DIRECTORY) $(CPU_DIRECTORY)

# --- Flags -----------------------------------------------------------

RTOS_INC := \
    -I$(FW_DIR) \
    -I$(RTOS_DIR)/include \
    -I$(RTOS_PORT)

CFLAGS += $(RTOS_INC)

# The assembler must be able to find freertos_risc_v_chip_specific_extensions.h
# (and portContext.h) when building portASM.S. CFLAGS is passed to $(AS)
# via the compile-helper macros in common.mak, but -I works for both.
ASFLAGS += $(RTOS_INC)

# The LiteX CPU flags encode `-march=rv32i2p0_m`, which the gcc driver
# doesn't map to a multilib directory. Override for the libgcc lookup.
TARGET_LIBGCC := $(shell $(TARGET_PREFIX)gcc -march=rv32im -mabi=ilp32 \
                     -print-libgcc-file-name)

# FreeRTOS sources trigger a handful of pedantic warnings that aren't
# worth fixing upstream. Silence them locally.
RTOS_WARN_SILENCE := \
    -Wno-unused-parameter -Wno-unused-function \
    -Wno-sign-compare -Wno-cast-function-type

# --- Targets ---------------------------------------------------------

.PHONY: all clean help

all: firmware.bin

help:
	@echo "Usage:"
	@echo "  make BUILD_DIRECTORY=<litex_sim output dir>               # default (full_demo)"
	@echo "  make BUILD_DIRECTORY=<...> DEMO=blinky_only               # named demo"
	@echo "  make clean                                                # no BUILD_DIRECTORY needed"
	@echo ""
	@echo "Available demos:"
	@ls ../examples/*.c 2>/dev/null | xargs -n1 -I{} basename {} .c | sed 's/^/  /'

firmware.elf: $(OBJECTS) linker.ld
	$(CC) $(LDFLAGS) -T linker.ld -N -o $@ \
	    $(OBJECTS) \
	    $(PACKAGES:%=-L$(BUILD_DIR)/software/%) \
	    -Wl,--start-group \
	    $(LIBS:lib%=-l%) \
	    $(TARGET_LIBGCC) \
	    -Wl,--end-group \
	    -Wl,--gc-sections \
	    -Wl,-Map,$@.map
ifneq ($(OS),Windows_NT)
	chmod -x $@
endif

%.bin: %.elf
	$(OBJCOPY) -O binary $< $@
ifneq ($(OS),Windows_NT)
	chmod -x $@
endif

# Per-object overrides: suppress noisy warnings for upstream FreeRTOS
# sources (we build them verbatim from the submodule).
tasks.o queue.o list.o timers.o event_groups.o stream_buffer.o \
port.o heap_4.o: CFLAGS += $(RTOS_WARN_SILENCE)

# Inject the demo name into main.o so the banner can print it. The
# value of `DEMO` is a make variable, not a file dependency, so make
# won't notice when it changes between invocations. Force a rebuild on
# every invocation — main.o is cheap (<100 ms).
.PHONY: FORCE
FORCE:
main.o: CFLAGS += -DLITEX_FREERTOS_DEMO_NAME=\"$(DEMO)\"
main.o: FORCE

%.o: %.c
	$(compile)

%.o: %.S
	$(assemble)

clean:
	$(RM) *.o *.d *~ firmware.elf firmware.bin firmware.elf.map

-include $(wildcard *.d)
