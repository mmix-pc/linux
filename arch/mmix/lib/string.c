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
		/* Copy eight octas per branch to reduce loop overhead. */
		while (count >= 8 * sizeof(long)) {
			unsigned long *out = (unsigned long *)d;
			const unsigned long *in = (const unsigned long *)s;

			out[0] = in[0];
			out[1] = in[1];
			out[2] = in[2];
			out[3] = in[3];
			out[4] = in[4];
			out[5] = in[5];
			out[6] = in[6];
			out[7] = in[7];
			d += 8 * sizeof(long);
			s += 8 * sizeof(long);
			count -= 8 * sizeof(long);
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
	/* Store eight octas per branch to reduce loop overhead. */
	while (count >= 8 * sizeof(long)) {
		unsigned long *out = (unsigned long *)d;

		out[0] = word;
		out[1] = word;
		out[2] = word;
		out[3] = word;
		out[4] = word;
		out[5] = word;
		out[6] = word;
		out[7] = word;
		d += 8 * sizeof(long);
		count -= 8 * sizeof(long);
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
