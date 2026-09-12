/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _ASM_MMIX_ATOMIC_H
#define _ASM_MMIX_ATOMIC_H

#include <linux/types.h>
#include <asm/cmpxchg.h>
#include <asm/barrier.h>

#define ATOMIC64_INIT(i) { (i) }

/* The backend lowers these operations to CSWAP, including narrow counters. */
#define arch_atomic_read(v) __atomic_load_n(&(v)->counter, __ATOMIC_RELAXED)
#define arch_atomic_set(v, i) __atomic_store_n(&(v)->counter, (i), __ATOMIC_RELAXED)
#define arch_atomic_cmpxchg(v, old, new) arch_cmpxchg(&(v)->counter, old, new)
#define arch_atomic_xchg(v, new) arch_xchg(&(v)->counter, new)

static inline int arch_atomic_fetch_add(int i, atomic_t *v)
{
	return __atomic_fetch_add(&v->counter, i, __ATOMIC_SEQ_CST);
}
#define arch_atomic_fetch_add arch_atomic_fetch_add
#define arch_atomic_add(i, v) ((void)arch_atomic_fetch_add(i, v))

static inline int arch_atomic_fetch_sub(int i, atomic_t *v)
{
	return __atomic_fetch_sub(&v->counter, i, __ATOMIC_SEQ_CST);
}
#define arch_atomic_fetch_sub arch_atomic_fetch_sub
#define arch_atomic_sub(i, v) ((void)arch_atomic_fetch_sub(i, v))

static inline int arch_atomic_fetch_and(int i, atomic_t *v)
{
	return __atomic_fetch_and(&v->counter, i, __ATOMIC_SEQ_CST);
}
#define arch_atomic_fetch_and arch_atomic_fetch_and
#define arch_atomic_and(i, v) ((void)arch_atomic_fetch_and(i, v))

static inline int arch_atomic_fetch_or(int i, atomic_t *v)
{
	return __atomic_fetch_or(&v->counter, i, __ATOMIC_SEQ_CST);
}
#define arch_atomic_fetch_or arch_atomic_fetch_or
#define arch_atomic_or(i, v) ((void)arch_atomic_fetch_or(i, v))

static inline int arch_atomic_fetch_xor(int i, atomic_t *v)
{
	return __atomic_fetch_xor(&v->counter, i, __ATOMIC_SEQ_CST);
}
#define arch_atomic_fetch_xor arch_atomic_fetch_xor
#define arch_atomic_xor(i, v) ((void)arch_atomic_fetch_xor(i, v))

#define arch_atomic64_read(v) __atomic_load_n(&(v)->counter, __ATOMIC_RELAXED)
#define arch_atomic64_set(v, i) __atomic_store_n(&(v)->counter, (i), __ATOMIC_RELAXED)
#define arch_atomic64_cmpxchg(v, old, new) arch_cmpxchg(&(v)->counter, old, new)
#define arch_atomic64_xchg(v, new) arch_xchg(&(v)->counter, new)

static inline s64 arch_atomic64_fetch_add(s64 i, atomic64_t *v)
{
	return __atomic_fetch_add(&v->counter, i, __ATOMIC_SEQ_CST);
}
#define arch_atomic64_fetch_add arch_atomic64_fetch_add
#define arch_atomic64_add(i, v) ((void)arch_atomic64_fetch_add(i, v))

static inline s64 arch_atomic64_fetch_sub(s64 i, atomic64_t *v)
{
	return __atomic_fetch_sub(&v->counter, i, __ATOMIC_SEQ_CST);
}
#define arch_atomic64_fetch_sub arch_atomic64_fetch_sub
#define arch_atomic64_sub(i, v) ((void)arch_atomic64_fetch_sub(i, v))

static inline s64 arch_atomic64_fetch_and(s64 i, atomic64_t *v)
{
	return __atomic_fetch_and(&v->counter, i, __ATOMIC_SEQ_CST);
}
#define arch_atomic64_fetch_and arch_atomic64_fetch_and
#define arch_atomic64_and(i, v) ((void)arch_atomic64_fetch_and(i, v))

static inline s64 arch_atomic64_fetch_or(s64 i, atomic64_t *v)
{
	return __atomic_fetch_or(&v->counter, i, __ATOMIC_SEQ_CST);
}
#define arch_atomic64_fetch_or arch_atomic64_fetch_or
#define arch_atomic64_or(i, v) ((void)arch_atomic64_fetch_or(i, v))

static inline s64 arch_atomic64_fetch_xor(s64 i, atomic64_t *v)
{
	return __atomic_fetch_xor(&v->counter, i, __ATOMIC_SEQ_CST);
}
#define arch_atomic64_fetch_xor arch_atomic64_fetch_xor
#define arch_atomic64_xor(i, v) ((void)arch_atomic64_fetch_xor(i, v))

static inline int arch_atomic_add_return(int i, atomic_t *v)
{
	return __atomic_add_fetch(&v->counter, i, __ATOMIC_SEQ_CST);
}
#define arch_atomic_add_return arch_atomic_add_return

static inline int arch_atomic_sub_return(int i, atomic_t *v)
{
	return __atomic_sub_fetch(&v->counter, i, __ATOMIC_SEQ_CST);
}
#define arch_atomic_sub_return arch_atomic_sub_return

static inline s64 arch_atomic64_add_return(s64 i, atomic64_t *v)
{
	return __atomic_add_fetch(&v->counter, i, __ATOMIC_SEQ_CST);
}
#define arch_atomic64_add_return arch_atomic64_add_return

static inline s64 arch_atomic64_sub_return(s64 i, atomic64_t *v)
{
	return __atomic_sub_fetch(&v->counter, i, __ATOMIC_SEQ_CST);
}
#define arch_atomic64_sub_return arch_atomic64_sub_return

#endif
