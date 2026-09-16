// SPDX-License-Identifier: GPL-2.0-only
#include <linux/sched/signal.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/unistd.h>
#include <asm/rstack.h>
#include <asm/ucontext.h>
#include "signal.h"
#include "user_entry.h"

struct mmix_signal_frame {
	siginfo_t info;
	struct ucontext uc;
};

static_assert(sizeof(struct mmix_signal_frame) == 10752);

/* Public context never supplies the privileged continuation or chain authority. */
struct mmix_signal_activation {
	struct mmix_signal_activation *parent;
	struct mmix_user_state state;
	struct pt_regs regs;
	stack_t altstack;
	unsigned long frame;
	u64 chain, handler_chain;
};

#ifdef CONFIG_MMIX_BOOT_TEST
static atomic_long_t live_activations = ATOMIC_LONG_INIT(0);

long mmix_signal_live(void)
{
	return atomic_long_read(&live_activations);
}
#endif

static void free_activation(struct mmix_signal_activation *activation)
{
	if (!activation)
		return;
#ifdef CONFIG_MMIX_BOOT_TEST
	atomic_long_dec(&live_activations);
#endif
	kvfree(activation);
}

static struct mmix_signal_activation *alloc_activation(void)
{
	struct mmix_signal_activation *activation =
		kvzalloc_obj(*activation);

#ifdef CONFIG_MMIX_BOOT_TEST
	if (activation)
		atomic_long_inc(&live_activations);
#endif
	return activation;
}

void mmix_signal_free(struct task_struct *task)
{
	struct mmix_signal_activation *activation;

	while ((activation = task->thread.signals)) {
		task->thread.signals = activation->parent;
		free_activation(activation);
	}
}

int mmix_signal_dup(struct task_struct *task)
{
	struct mmix_signal_activation *source = current->thread.signals;
	struct mmix_signal_activation **next = &task->thread.signals;

	while (source) {
		*next = alloc_activation();
		if (!*next) {
			mmix_signal_free(task);
			return -ENOMEM;
		}
		**next = *source;
		(*next)->parent = NULL;
		next = &(*next)->parent;
		source = source->parent;
	}
	return 0;
}

static bool user_pc(unsigned long pc)
{
	return pc && pc < TASK_SIZE && !(pc & 3);
}

static int setup_frame(struct mmix_user_entry *entry, struct ksignal *ksig,
		       bool materialized)
{
	struct mmix_user_state *state = mmix_user_rstack_state(entry->stack);
	struct mmix_signal_activation *activation;
	struct mmix_signal_frame *frame;
	unsigned long sp = state->regs.regs[254], address, base, zero = 0;
	unsigned long restorer = (unsigned long)ksig->ka.sa.sa_restorer;
	bool alternate = on_sig_stack(sp);
	int error = -EFAULT;

	if (!(ksig->ka.sa.sa_flags & SA_RESTORER) || !user_pc(restorer) ||
	    !user_pc(restorer + 4) ||
	    !user_pc((unsigned long)ksig->ka.sa.sa_handler))
		return -EFAULT;
	if ((ksig->ka.sa.sa_flags & SA_ONSTACK) && !sas_ss_flags(sp)) {
		if (current->sas_ss_sp >= TASK_SIZE ||
		    current->sas_ss_size > TASK_SIZE - current->sas_ss_sp)
			return -EFAULT;
		sp = current->sas_ss_sp + current->sas_ss_size;
		alternate = true;
	}
	if (sp > TASK_SIZE || sp < sizeof(*frame))
		return -EFAULT;
	address = (sp - sizeof(*frame)) & ~7UL;
	if (alternate && (address < current->sas_ss_sp ||
			  sp - current->sas_ss_sp > current->sas_ss_size))
		return -EFAULT;
	activation = alloc_activation();
	frame = kvzalloc_obj(*frame);
	if (!activation || !frame) {
		error = -ENOMEM;
		goto out;
	}
	activation->state = *state;
	if (materialized) {
		/* Do not overwrite completed query output that aliases old backing. */
		memset(&activation->state.pending, 0,
		       sizeof(activation->state.pending));
		activation->state.pending.start = state->regs.r_o;
	}
	activation->regs = entry->regs;
	activation->frame = address;
	activation->altstack.ss_sp = (void __user *)current->sas_ss_sp;
	activation->altstack.ss_size = current->sas_ss_size;
	activation->altstack.ss_flags = current->sas_ss_flags;
	activation->chain = current->thread.rstack_chain;
	frame->uc.uc_stack.ss_sp = (void __user *)current->sas_ss_sp;
	frame->uc.uc_stack.ss_size = current->sas_ss_size;
	frame->uc.uc_stack.ss_flags = sas_ss_flags(state->regs.regs[254]) |
				      (current->sas_ss_flags & SS_AUTODISARM);
	frame->uc.uc_sigmask = *sigmask_to_save();
	frame->uc.uc_mcontext.sc_regs = state->regs;
	frame->uc.uc_mcontext.sc_rstack = activation->state.pending;
	error = mmix_rstack_domain_create(current->mm, activation->chain,
					  &activation->handler_chain, &base);
	if (error)
		goto out;
	if (copy_siginfo_to_user((siginfo_t __user *)address, &ksig->info) ||
	    copy_to_user((void __user *)(address + sizeof(siginfo_t)),
			 &frame->uc, sizeof(frame->uc)) ||
	    put_user(zero, (unsigned long __user *)base)) {
		error = -EFAULT;
		mmix_rstack_domain_release(current->mm,
					   activation->handler_chain);
		goto out;
	}
	/* The tail-entered handler consumes this independent synthetic zero hole. */
	memset(state, 0, sizeof(*state));
	state->regs = activation->state.regs;
	memset(state->regs.regs, 0, 230 * sizeof(unsigned long));
	state->regs.r_l = 0;
	state->regs.pc = restorer;
	state->regs.r_g = 230;
	state->regs.r_o = base + 8;
	state->pending.start = base + 8;
	state->regs.r_j = restorer + 4;
	state->regs.regs[231] = ksig->sig;
	state->regs.regs[232] = address;
	state->regs.regs[233] = address + sizeof(siginfo_t);
	state->regs.regs[234] = (unsigned long)ksig->ka.sa.sa_handler;
	state->regs.regs[254] = address;
	entry->regs.r_ww = restorer;
	entry->regs.r_xx = 1UL << 63;
	entry->regs.r_yy = 0;
	entry->regs.r_zz = 0;
	entry->regs.syscall_nr = -1;
	entry->regs.pc = restorer;
	activation->parent = current->thread.signals;
	current->thread.signals = activation;
	current->thread.rstack_chain = activation->handler_chain;
	signal_setup_done(0, ksig, 0);
	kvfree(frame);
	return 1;
out:
	kvfree(frame);
	free_activation(activation);
	return error;
}

static bool syscall_restart(struct mmix_user_entry *entry, struct ksignal *ksig)
{
	struct mmix_user_state *state = mmix_user_rstack_state(entry->stack);
	long result = state->regs.regs[231];
	bool restart = false;

	if (!entry->syscall_result)
		return false;
	entry->syscall_result = false;
	switch (result) {
	case -ERESTARTNOINTR:
		restart = true;
		break;
	case -ERESTARTSYS:
		restart = !ksig || (ksig->ka.sa.sa_flags & SA_RESTART);
		break;
	case -ERESTARTNOHAND:
	case -ERESTART_RESTARTBLOCK:
		restart = !ksig;
		break;
	default:
		return false;
	}
	state->regs.regs[231] = restart ? entry->regs.syscall_args[0] : -EINTR;
	if (restart) {
		state->regs.pc -= 4;
		state->regs.regs[237] = result == -ERESTART_RESTARTBLOCK ?
						__NR_restart_syscall :
						entry->regs.syscall_nr;
		entry->regs.r_ww = state->regs.pc;
		entry->regs.r_xx = 1UL << 63;
	}
	return true;
}

int mmix_signal_pending(struct mmix_user_entry *entry, bool materialized)
{
	struct ksignal ksig;
	bool deliver, restarted;
	int error;

	*task_pt_regs(current) = entry->regs;
	deliver = get_signal(&ksig);
	restarted = syscall_restart(entry, deliver ? &ksig : NULL);
	if (!deliver) {
		restore_saved_sigmask();
		/* Restart preparation may have changed the private return image. */
		return restarted;
	}
	error = setup_frame(entry, &ksig, materialized);
	if (error < 0)
		entry->fatal_signal = SIGSEGV;
	return error;
}

static bool valid_context(const struct mmix_signal_activation *activation,
			  const struct ucontext *uc)
{
	const struct user_regs_struct *old = &activation->state.regs;
	const struct user_regs_struct *regs = &uc->uc_mcontext.sc_regs;

	if (uc->uc_flags || uc->uc_link ||
	    memchr_inv(uc->__unused, 0, sizeof(uc->__unused)) ||
	    memchr_inv(uc->uc_mcontext.reserved, 0,
		       sizeof(uc->uc_mcontext.reserved)) ||
	    regs->r_g != old->r_g || regs->r_l != old->r_l ||
	    regs->r_o != old->r_o || regs->regs[230] != old->regs[230] ||
	    memcmp(&uc->uc_mcontext.sc_rstack, &activation->state.pending,
		   sizeof(activation->state.pending)))
		return false;
	/* Only a completed no-replay continuation admits machine-context edits. */
	if (!(activation->regs.r_xx >> 63) && memcmp(regs, old, sizeof(*regs)))
		return false;
	return user_pc(regs->pc);
}

static bool valid_altstack(const stack_t *stack)
{
	unsigned int mode = stack->ss_flags & ~SS_AUTODISARM;
	unsigned long base = (unsigned long)stack->ss_sp;
	stack_t canonical;

	memset(&canonical, 0, sizeof(canonical));
	canonical.ss_sp = stack->ss_sp;
	canonical.ss_flags = stack->ss_flags;
	canonical.ss_size = stack->ss_size;
	if (memcmp(&canonical, stack, sizeof(canonical)))
		return false;
	if (mode != 0 && mode != SS_ONSTACK && mode != SS_DISABLE)
		return false;
	if (mode == SS_DISABLE)
		return true;
	return base && base < TASK_SIZE && stack->ss_size >= MINSIGSTKSZ &&
	       stack->ss_size <= TASK_SIZE - base;
}

int mmix_signal_return(struct mmix_user_entry *entry)
{
	struct mmix_signal_activation *activation = current->thread.signals;
	struct mmix_user_state *state = mmix_user_rstack_state(entry->stack);
	struct ucontext *uc;
	unsigned long address = state->regs.regs[254];
	int error = -EFAULT;

	current->restart_block.fn = do_no_restart_syscall;
	if (!activation ||
	    activation->handler_chain != current->thread.rstack_chain ||
	    address != activation->frame)
		goto fatal;
	uc = kvzalloc_obj(*uc);
	if (!uc)
		goto fatal;
	if (copy_from_user(uc, (void __user *)(address + sizeof(siginfo_t)),
			   sizeof(*uc)) ||
	    !valid_context(activation, uc) || !valid_altstack(&uc->uc_stack))
		goto free;
	*state = activation->state;
	state->regs = uc->uc_mcontext.sc_regs;
	if (mmix_user_rstack_validate(state))
		goto free;
	/* Commit only the copied, validated frame; never fetch its fields twice. */
	if (uc->uc_stack.ss_flags & SS_DISABLE) {
		sas_ss_reset(current);
	} else {
		current->sas_ss_sp = (unsigned long)uc->uc_stack.ss_sp;
		current->sas_ss_size = uc->uc_stack.ss_size;
		current->sas_ss_flags = uc->uc_stack.ss_flags & SS_AUTODISARM;
	}
	set_current_blocked(&uc->uc_sigmask);
	entry->regs = activation->regs;
	entry->regs.pc = state->regs.pc;
	if (state->regs.pc != activation->state.regs.pc) {
		entry->regs.r_ww = state->regs.pc;
		entry->regs.r_xx = 1UL << 63;
		entry->regs.r_yy = 0;
		entry->regs.r_zz = 0;
	}
	entry->syscall_result = false;
	entry->rstack_sync_pending = false;
	entry->rstack_query_pending = false;
	current->thread.rstack_chain = activation->chain;
	current->thread.signals = activation->parent;
	error = mmix_rstack_domain_release(current->mm,
					   activation->handler_chain);
	free_activation(activation);
free:
	kvfree(uc);
fatal:
	if (error)
		entry->fatal_signal = SIGSEGV;
	return error;
}

/* The environment is opaque: ownership does not authenticate a C invocation. */
int mmix_signal_jump(struct mmix_user_entry *entry, unsigned long address)
{
	struct mmix_signal_activation *target = NULL, *activation;
	struct mmix_user_state *live = mmix_user_rstack_state(entry->stack);
	struct mmix_user_rstack_state *staged;
	struct mmix_user_state *state;
	struct mmix_rstack_domain *domain;
	struct mmix_rstack_jump request;
	unsigned long base, top;
	int error;

	if (address & 7)
		return -EINVAL;
	if (copy_from_user(&request, (void __user *)address, sizeof(request)))
		return -EFAULT;
	if (!request.chain_id || !request.result ||
	    request.result != (long)(int)request.result ||
	    request.flags & ~MMIX_RSTACK_JUMP_SETMASK ||
	    (!(request.flags & MMIX_RSTACK_JUMP_SETMASK) && request.sigmask) ||
	    (request.landing & 3) || (request.environment & 7))
		return -EINVAL;
	if (!user_pc(request.landing) || !request.environment ||
	    !access_ok((void __user *)request.environment, sizeof(unsigned long)))
		return -EFAULT;
	if (!current->thread.rstack_chain)
		return -ESRCH;
	if (request.chain_id != current->thread.rstack_chain) {
		for (target = current->thread.signals; target; target = target->parent)
			if (target->chain == request.chain_id)
				break;
		if (!target)
			return -ESRCH;
	}

	/* Allocation/materialization failures cannot pretend recovery succeeded. */
	staged = mmix_user_rstack_dup(entry->stack);
	if (!staged) {
		error = -ENOMEM;
		goto fatal;
	}
	state = mmix_user_rstack_state(staged);
	if (target) {
		*state = target->state;
		/* Leave-time globals/specials, surviving locals and temporary FP/SP. */
		state->regs = live->regs;
		memcpy(state->regs.regs, target->state.regs.regs,
		       230 * sizeof(unsigned long));
		state->regs.r_g = target->state.regs.r_g;
		state->regs.r_l = target->state.regs.r_l;
		state->regs.r_o = target->state.regs.r_o;
		state->regs.regs[253] = target->state.regs.regs[253];
		state->regs.regs[254] = target->state.regs.regs[254];
	}
	state->regs.pc = request.landing;
	state->regs.r_j = request.landing;
	state->regs.regs[231] = request.environment;
	state->regs.regs[232] = request.result;
	error = mmix_user_rstack_validate(state);
	if (error)
		goto free;
	domain = mmix_rstack_domain_begin(current->mm, request.chain_id,
					  state->pending.start,
					  state->pending.count * sizeof(unsigned long));
	if (IS_ERR(domain)) {
		error = PTR_ERR(domain);
		goto free;
	}
	error = mmix_user_rstack_materialize(staged, mmix_user_write, NULL);
	mmix_rstack_domain_end(current->mm, domain);
	if (error)
		goto free;
	memset(&state->pending, 0, sizeof(state->pending));
	state->pending.start = state->regs.r_o;
	error = mmix_user_rstack_prepare(staged, &base, &top);
	if (error)
		goto free;

	/* Commit is indivisible with respect to user execution and signal delivery. */
	*live = *state;
	if (request.flags & MMIX_RSTACK_JUMP_SETMASK) {
		sigset_t mask = { .sig = { request.sigmask } };

		set_current_blocked(&mask);
	}
	if (target) {
		current->sas_ss_sp = (unsigned long)target->altstack.ss_sp;
		current->sas_ss_size = target->altstack.ss_size;
		current->sas_ss_flags = target->altstack.ss_flags;
	}
	current->thread.rstack_chain = request.chain_id;
	current->restart_block.fn = do_no_restart_syscall;
	clear_restore_sigmask();
	entry->syscall_result = false;
	entry->rstack_sync_pending = false;
	entry->rstack_query_pending = false;
	entry->regs.pc = request.landing;
	entry->regs.r_ww = request.landing;
	entry->regs.r_xx = 1UL << 63;
	entry->regs.r_yy = 0;
	entry->regs.r_zz = 0;
	entry->regs.syscall_nr = -1;
	memset(entry->regs.syscall_args, 0, sizeof(entry->regs.syscall_args));
	while (target) {
		activation = current->thread.signals;
		current->thread.signals = activation->parent;
		if (activation == target)
			target = NULL;
		error = mmix_rstack_domain_release(current->mm, activation->handler_chain);
		free_activation(activation);
		if (error)
			break;
	}
free:
	mmix_user_rstack_free(&staged);
fatal:
	if (error)
		entry->fatal_signal = SIGSEGV;
	return error;
}
