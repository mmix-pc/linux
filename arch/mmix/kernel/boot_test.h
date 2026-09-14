/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _MMIX_KERNEL_BOOT_TEST_H
#define _MMIX_KERNEL_BOOT_TEST_H

#define MMIX_BOOT_TEST_PROGRESS 0
#define MMIX_BOOT_TEST_STOP 8
#define MMIX_BOOT_TEST_ERROR 16

#ifndef __ASSEMBLY__
struct mmix_boot_register_state {
	unsigned long progress, stop, error;
};

unsigned long mmix_boot_register_test(struct mmix_boot_register_state *state,
				      void *address);
extern char mmix_boot_test_store[];
#endif
#endif
