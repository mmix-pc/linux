// SPDX-License-Identifier: GPL-2.0-only
#include <linux/errno.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include "user_rstack.h"

struct mmix_user_rstack_state {
	struct mmix_user_state state;
	void *shadow;
	void *continuation;
};

#if defined(CONFIG_MMIX_BOOT_TEST) || defined(CONFIG_MMIX_USER_TEST)
static atomic_long_t live = ATOMIC_LONG_INIT(0);

long mmix_user_rstack_live(void)
{
	return atomic_long_read(&live);
}
#endif

#if defined(CONFIG_MMIX_BOOT_TEST) || defined(CONFIG_MMIX_USER_TEST)
static atomic_t fail_step = ATOMIC_INIT(-1);

#ifdef CONFIG_MMIX_BOOT_TEST
static atomic_long_t write_limit = ATOMIC_LONG_INIT(-1);

void mmix_user_rstack_write_limit(long bytes)
{
	atomic_long_set(&write_limit, bytes);
}
#endif

void mmix_user_rstack_fail_after(int step)
{
	atomic_set(&fail_step, step);
}

static bool fail_allocation(void)
{
	return atomic_read(&fail_step) >= 0 && atomic_dec_return(&fail_step) < 0;
}
#else
static bool fail_allocation(void)
{
	return false;
}
#endif

void mmix_user_rstack_free(struct mmix_user_rstack_state **owner)
{
	struct mmix_user_rstack_state *stack = *owner;

	*owner = NULL;
	if (!stack)
		return;
	if (stack->shadow)
		free_pages((unsigned long)stack->shadow,
			   get_order(MMIX_USER_SHADOW_PAGES * PAGE_SIZE));
	if (stack->continuation)
		free_page((unsigned long)stack->continuation);
#if defined(CONFIG_MMIX_BOOT_TEST) || defined(CONFIG_MMIX_USER_TEST)
	atomic_long_dec(&live);
#endif
	kfree(stack);
}

struct mmix_user_rstack_state *mmix_user_rstack_alloc(void)
{
	struct mmix_user_rstack_state *stack;

	if (fail_allocation())
		return NULL;
	stack = kzalloc_obj(*stack);
	if (!stack)
		return NULL;
#if defined(CONFIG_MMIX_BOOT_TEST) || defined(CONFIG_MMIX_USER_TEST)
	atomic_long_inc(&live);
#endif
	if (fail_allocation())
		goto fail;
	stack->shadow = (void *)__get_free_pages(GFP_KERNEL | __GFP_ZERO,
						 get_order(MMIX_USER_SHADOW_PAGES * PAGE_SIZE));
	if (!stack->shadow || fail_allocation())
		goto fail;
	stack->continuation = (void *)get_zeroed_page(GFP_KERNEL);
	if (!stack->continuation)
		goto fail;
	return stack;
fail:
	mmix_user_rstack_free(&stack);
	return NULL;
}

struct mmix_user_rstack_state *mmix_user_rstack_dup(const struct mmix_user_rstack_state *source)
{
	struct mmix_user_rstack_state *stack;

	if (mmix_user_rstack_validate(&source->state))
		return NULL;
	stack = mmix_user_rstack_alloc();
	if (stack)
		stack->state = source->state;
	return stack;
}

struct mmix_user_state *mmix_user_rstack_state(struct mmix_user_rstack_state *stack)
{
	return &stack->state;
}

void *mmix_user_rstack_shadow(struct mmix_user_rstack_state *stack)
{
	return stack->shadow;
}

void *mmix_user_rstack_continuation(struct mmix_user_rstack_state *stack)
{
	return stack->continuation;
}

int mmix_user_rstack_window(const struct mmix_user_capture *c, unsigned long *base,
			    unsigned long *size)
{
	unsigned long end;

	if (c->r_g != 230 || c->r_l > c->r_g || ((c->start | c->r_s | c->r_o) & 7) ||
	    c->start > c->r_s || c->r_s > c->r_o || c->r_o >= MMIX_TASK_SIZE ||
	    c->r_o - c->start > MMIX_RSTACK_MAX_WORDS * sizeof(unsigned long) ||
	    (c->start != c->r_s && (c->start & PAGE_MASK) != ((c->r_s - 1) & PAGE_MASK)))
		return -EINVAL;
	end = c->r_o + (c->r_l + MMIX_USER_SAVE_WORDS) * sizeof(unsigned long);
	if (end > MMIX_TASK_SIZE)
		return -EINVAL;
	*base = c->start & PAGE_MASK;
	*size = PAGE_ALIGN(end) - *base;
	return 0;
}

/* SAVE order differs from the logical view: rR precedes rP in the image. */
static void decode_specials(struct user_regs_struct *r, const unsigned long *p)
{
	r->r_b = p[0];
	r->r_d = p[1];
	r->r_e = p[2];
	r->r_h = p[3];
	r->r_j = p[4];
	r->r_m = p[5];
	r->r_r = p[6];
	r->r_p = p[7];
	r->r_w = p[8];
	r->r_x = p[9];
	r->r_y = p[10];
	r->r_z = p[11];
}

int mmix_user_rstack_capture(struct mmix_user_rstack_state *stack,
			     const struct mmix_user_capture *c, unsigned long pc)
{
	struct mmix_user_state *s = &stack->state;
	unsigned long base, size, ga, i;
	unsigned long *locals, *globals, *specials;

	if (mmix_user_rstack_window(c, &base, &size) || pc >= MMIX_TASK_SIZE || (pc & 3))
		return -EINVAL;
	locals = stack->shadow + c->r_o - base;
	globals = locals + c->r_l + 1;
	specials = globals + 26;
	ga = specials[12];
	if (locals[c->r_l] != c->r_l || (ga >> 56) != c->r_g || (ga & ~(0xffUL << 56 | 0x3ffffUL)))
		return -EINVAL;
	/* Nothing below can fail; publish only after all metadata is validated. */
	memset(s, 0, sizeof(*s));
	s->regs.pc = pc;
	s->regs.r_g = c->r_g;
	s->regs.r_l = c->r_l;
	s->regs.r_o = c->r_o;
	s->regs.r_a = ga & 0x3ffff;
	memcpy(s->regs.regs, locals, c->r_l * sizeof(unsigned long));
	memcpy(s->regs.regs + 230, globals, 26 * sizeof(unsigned long));
	decode_specials(&s->regs, specials);
	s->pending.start = c->start;
	s->pending.count = (c->r_o - c->start) / sizeof(unsigned long);
	for (i = 0; i < s->pending.count; i++) {
		unsigned long address = c->start + i * sizeof(unsigned long);
		const unsigned long *word;

		if (address < c->r_s)
			word = stack->continuation + (address & ~PAGE_MASK);
		else
			word = stack->shadow + address - base;
		s->pending.data[i] = *word;
	}
	return 0;
}

int mmix_user_rstack_validate(const struct mmix_user_state *s)
{
	const struct user_regs_struct *r = &s->regs;
	const struct mmix_user_rstack *p = &s->pending;
	struct mmix_user_capture c = { r->r_g, r->r_l, r->r_o, r->r_o, p->start };
	unsigned long base, size;

	/* Logical pending values can span pages; they are no longer a raw rC prefix. */
	c.r_s = p->start;
	if (p->count > MMIX_RSTACK_MAX_WORDS || mmix_user_rstack_window(&c, &base, &size) ||
	    r->r_o - p->start != p->count * sizeof(unsigned long) || r->pc >= MMIX_TASK_SIZE ||
	    (r->pc & 3) || r->regs[254] > MMIX_TASK_SIZE || (r->regs[254] & 7) ||
	    (r->r_a & ~0x3ffffUL) ||
	    memchr_inv(r->regs + r->r_l, 0, (230 - r->r_l) * sizeof(unsigned long)) ||
	    memchr_inv(p->data + p->count, 0,
		       (MMIX_RSTACK_MAX_WORDS - p->count) * sizeof(unsigned long)))
		return -EINVAL;
	return 0;
}

int mmix_user_rstack_materialize(struct mmix_user_rstack_state *stack,
				 int (*write)(void *, unsigned long, const void *, size_t),
				 void *arg)
{
	struct mmix_user_state *s = &stack->state;

	if (mmix_user_rstack_validate(s) || (!write && s->pending.count))
		return -EINVAL;
	if (!s->pending.count)
		return 0;
#ifdef CONFIG_MMIX_BOOT_TEST
	{
		long limit = atomic_long_xchg(&write_limit, -1);
		size_t size = s->pending.count * sizeof(unsigned long);

		if (limit >= 0 && limit < size) {
			if (limit)
				write(arg, s->pending.start, s->pending.data, limit);
			return -EFAULT;
		}
	}
#endif
	return write(arg, s->pending.start, s->pending.data,
		     s->pending.count * sizeof(unsigned long));
}

int mmix_user_rstack_prepare(struct mmix_user_rstack_state *stack,
			     unsigned long *base, unsigned long *top)
{
	struct mmix_user_state *s = &stack->state;
	struct user_regs_struct *r = &s->regs;
	unsigned long *p;
	unsigned long start = s->pending.start & PAGE_MASK;

	if (mmix_user_rstack_validate(s))
		return -EINVAL;
	memset(stack->shadow, 0, MMIX_USER_SHADOW_PAGES * PAGE_SIZE);
	p = stack->shadow + r->r_o - start;
	memcpy(p, r->regs, r->r_l * sizeof(unsigned long));
	p += r->r_l;
	*p++ = r->r_l;
	memcpy(p, r->regs + 230, 26 * sizeof(unsigned long));
	p += 26;
	*p++ = r->r_b;
	*p++ = r->r_d;
	*p++ = r->r_e;
	*p++ = r->r_h;
	*p++ = r->r_j;
	*p++ = r->r_m;
	*p++ = r->r_r;
	*p++ = r->r_p;
	*p++ = r->r_w;
	*p++ = r->r_x;
	*p++ = r->r_y;
	*p++ = r->r_z;
	*p = r->r_g << 56 | r->r_a;
	*base = start;
	*top = r->r_o + (r->r_l + MMIX_USER_SAVE_WORDS - 1) * sizeof(unsigned long);
	return 0;
}

int mmix_user_rstack_restore(struct mmix_user_rstack_state *stack,
			     int (*write)(void *, unsigned long, const void *, size_t),
			     void *arg, unsigned long *base, unsigned long *top)
{
	int error = mmix_user_rstack_materialize(stack, write, arg);

	return error ? error : mmix_user_rstack_prepare(stack, base, top);
}
