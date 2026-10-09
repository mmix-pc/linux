// SPDX-License-Identifier: GPL-2.0-only
#define __NO_FORTIFY
#include <linux/export.h>
#include <linux/string.h>

void *memcpy(void *dest, const void *src, size_t count)
{
	unsigned char *d = dest;
	const unsigned char *s = src;

	/* MMIX octa accesses round down: both addresses must be aligned. */
	if (!(((unsigned long)d ^ (unsigned long)s) & (sizeof(long) - 1))) {
		while (count && ((unsigned long)d & (sizeof(long) - 1))) {
			*d++ = *s++;
			count--;
		}
		while (count >= sizeof(long)) {
			*(unsigned long *)d = *(const unsigned long *)s;
			d += sizeof(long);
			s += sizeof(long);
			count -= sizeof(long);
		}
	}
	while (count--)
		*d++ = *s++;
	return dest;
}
EXPORT_SYMBOL(memcpy);

void *memset(void *dest, int value, size_t count)
{
	unsigned char *d = dest;
	unsigned long word = (unsigned char)value;

	word |= word << 8;
	word |= word << 16;
	word |= word << 32;
	while (count && ((unsigned long)d & (sizeof(long) - 1))) {
		*d++ = value;
		count--;
	}
	while (count >= sizeof(long)) {
		*(unsigned long *)d = word;
		d += sizeof(long);
		count -= sizeof(long);
	}
	while (count--)
		*d++ = value;
	return dest;
}
EXPORT_SYMBOL(memset);
