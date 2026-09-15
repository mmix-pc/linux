/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _MMIX_KERNEL_USER_ENTRY_TEST_H
#define _MMIX_KERNEL_USER_ENTRY_TEST_H

extern const unsigned char mmix_user_test_start[], mmix_user_test_end[];
extern const unsigned char mmix_user_test_deep[], mmix_user_test_kernel[];
extern const unsigned char mmix_user_test_rg[], mmix_user_test_pointer[];
extern const unsigned char mmix_user_test_save[], mmix_user_test_tp[];
extern const unsigned char mmix_user_test_spin[];
extern const unsigned char mmix_user_test_privileged[], mmix_user_test_unsave[];
extern const unsigned char mmix_user_test_restore[], mmix_user_test_pending[];

extern const unsigned char mmix_user_test_direct[];

#endif
