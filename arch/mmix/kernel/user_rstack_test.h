/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _MMIX_USER_RSTACK_TEST_H
#define _MMIX_USER_RSTACK_TEST_H

#define MMIX_TEST_SAVE_DATA 48

#ifndef __ASSEMBLER__
#include <linux/build_bug.h>
#include <linux/stddef.h>

struct mmix_test_save {
	unsigned long r_o, r_s, r_l, r_g, top, base;
	unsigned long data[2048];
};

static_assert(offsetof(struct mmix_test_save, data) == MMIX_TEST_SAVE_DATA);

unsigned long mmix_test_user_save(struct mmix_test_save *fixture);
unsigned long mmix_test_user_restore(unsigned long top);
#endif
#endif
