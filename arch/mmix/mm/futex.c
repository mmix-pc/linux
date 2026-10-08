// SPDX-License-Identifier: GPL-2.0-only
#include <linux/futex.h>
#include <linux/uaccess.h>
#include <asm/futex.h>

int mmix_futex_cmpxchg(u32 *value, u32 __user *address, u32 old, u32 new);

int futex_atomic_cmpxchg_inatomic(u32 *uval, u32 __user *uaddr, u32 oldval,
				  u32 newval)
{
	int error;

	if (!IS_ALIGNED((unsigned long)uaddr, sizeof(*uaddr)) ||
	    !access_ok(uaddr, sizeof(*uaddr)))
		return -EFAULT;
	pagefault_disable();
	error = mmix_futex_cmpxchg(uval, uaddr, oldval, newval);
	pagefault_enable();
	return error;
}

int arch_futex_atomic_op_inuser(int op, u32 oparg, int *oval, u32 __user *uaddr)
{
	u32 old, observed, new;
	int error;

	if (op < FUTEX_OP_SET || op > FUTEX_OP_XOR)
		return -ENOSYS;
	if (!IS_ALIGNED((unsigned long)uaddr, sizeof(*uaddr)) ||
	    !access_ok(uaddr, sizeof(*uaddr)))
		return -EFAULT;
	pagefault_disable();
	/* A no-change compare supplies one aligned atomic snapshot, including zero. */
	error = mmix_futex_cmpxchg(&old, uaddr, 0, 0);
	while (!error) {
		switch (op) {
		case FUTEX_OP_SET:
			new = oparg;
			break;
		case FUTEX_OP_ADD:
			new = old + oparg;
			break;
		case FUTEX_OP_OR:
			new = old | oparg;
			break;
		case FUTEX_OP_ANDN:
			new = old & ~oparg;
			break;
		default:
			new = old ^ oparg;
			break;
		}
		error = mmix_futex_cmpxchg(&observed, uaddr, old, new);
		if (error || observed == old)
			break;
		old = observed;
	}
	pagefault_enable();
	if (!error)
		*oval = (int)old;
	return error;
}
