/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _ASM_MMIX_CPU_LOCAL_H
#define _ASM_MMIX_CPU_LOCAL_H

/* Resident vector record: two branch instructions followed by trusted pointers. */
#define MMIX_CPU_VECTOR_ENTRY	8
#define MMIX_CPU_VECTOR_MMU	16
#define MMIX_CPU_VECTOR_USER	24
#define MMIX_CPU_VECTOR_IRQ	32
#define MMIX_CPU_VECTOR_SHADOW	40
#define MMIX_CPU_VECTOR_PREV	48
#define MMIX_CPU_VECTOR_CONT	56
#define MMIX_CPU_VECTOR_STATE	64
#define MMIX_CPU_VECTOR_ID	72
#define MMIX_CPU_VECTOR_SIZE	80
#define MMIX_BOOT_CPU_SLOTS	4

#ifdef __ASSEMBLER__
.macro MMIX_CPU_PTR reg, offset
	GET \reg, rT
	LDOU \reg, \reg, \offset
.endm
#else
static __always_inline unsigned int mmix_cpu_id(void)
{
	unsigned long id;

	asm volatile("GET %0,rT\n\tLDOU %0,%0,72\n\tLDOU %0,%0,0"
		     : "=r" (id) :: "memory");
	return id;
}

/* The caller must prevent migration while retaining the returned pointer. */
static __always_inline unsigned long *mmix_cpu_irq_mask(void)
{
	unsigned long *mask;

	asm volatile("GET %0,rT\n\tLDOU %0,%0,32" : "=r" (mask) :: "memory");
	return mask;
}
#endif
#endif
