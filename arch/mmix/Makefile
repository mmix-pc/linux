# SPDX-License-Identifier: GPL-2.0-only
KBUILD_DEFCONFIG := mmix_virt_defconfig

KBUILD_CFLAGS += -ffreestanding -fno-asynchronous-unwind-tables -fno-unwind-tables
KBUILD_CFLAGS += -fno-omit-frame-pointer -fno-optimize-sibling-calls
KBUILD_AFLAGS += -fno-asynchronous-unwind-tables -fno-unwind-tables

core-y += arch/mmix/kernel/
core-y += arch/mmix/mm/
