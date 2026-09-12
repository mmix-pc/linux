/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _ASM_MMIX_CMPXCHG_H
#define _ASM_MMIX_CMPXCHG_H

#include <linux/build_bug.h>

#define arch_xchg(ptr, value) ({					\
	BUILD_BUG_ON(sizeof(*(ptr)) > sizeof(unsigned long));	\
	__atomic_exchange_n((ptr), (value), __ATOMIC_SEQ_CST);	\
})

#define arch_cmpxchg(ptr, old, new) ({				\
	__typeof__(*(ptr)) __old = (old);				\
	BUILD_BUG_ON(sizeof(*(ptr)) > sizeof(unsigned long));	\
	__atomic_compare_exchange_n((ptr), &__old, (new), 0,	\
				    __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST); \
	__old;							\
})

#define arch_cmpxchg64 arch_cmpxchg
#define arch_cmpxchg_local arch_cmpxchg
#define arch_cmpxchg64_local arch_cmpxchg
#endif
