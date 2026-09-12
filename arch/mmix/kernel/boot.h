/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _MMIX_KERNEL_BOOT_H
#define _MMIX_KERNEL_BOOT_H

/* Written by the low assembly entry; C only reads the raw loader handoff. */
struct mmix_boot_handoff {
	unsigned long fdt;
	unsigned long cpu;
	unsigned long loader_rs;
};

extern const struct mmix_boot_handoff mmix_boot_handoff;

/* Written only by the stackless emergency entry, independently of boot info. */
struct mmix_boot_fault {
	unsigned long sp;
	unsigned long pc;
	unsigned long r_xx;
	unsigned long r_yy;
	unsigned long r_zz;
	unsigned long r_o;
	unsigned long r_s;
	unsigned long r_bb;
};

#endif
