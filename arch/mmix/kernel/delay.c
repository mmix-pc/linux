// SPDX-License-Identifier: GPL-2.0-only
#include <linux/delay.h>
#include <linux/export.h>
#include <linux/math64.h>
#include <linux/param.h>

void __delay(unsigned long loops)
{
	while (loops--)
		asm volatile("SWYM 0,0,0" ::: "memory");
}
EXPORT_SYMBOL(__delay);

void __const_udelay(unsigned long xloops)
{
	__delay(mul_u64_u64_shr(xloops, loops_per_jiffy * HZ, 32) + 1);
}
EXPORT_SYMBOL(__const_udelay);

void __udelay(unsigned long usecs)
{
	__const_udelay(usecs * UDELAY_CONST_MULT);
}
EXPORT_SYMBOL(__udelay);

void __ndelay(unsigned long nsecs)
{
	__const_udelay(nsecs * NDELAY_CONST_MULT);
}
EXPORT_SYMBOL(__ndelay);
