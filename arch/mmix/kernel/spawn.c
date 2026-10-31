// SPDX-License-Identifier: GPL-2.0-only
#include <linux/anon_inodes.h>
#include <linux/file.h>
#include <linux/poll.h>
#include <linux/refcount.h>
#include <linux/sched/task.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <asm/spawn.h>
#include <uapi/asm/spawn.h>

struct mmix_spawn_event {
	refcount_t refs;
	wait_queue_head_t wait;
	int released;
};

#ifdef CONFIG_MMIX_USER_TEST
static atomic_long_t events_live = ATOMIC_LONG_INIT(0);
static atomic_long_t producers_live = ATOMIC_LONG_INIT(0);
static atomic_t fail_after = ATOMIC_INIT(-1);

long mmix_spawn_events_live(void)
{
	return atomic_long_read(&events_live);
}

long mmix_spawn_producers_live(void)
{
	return atomic_long_read(&producers_live);
}

void mmix_spawn_fail_after(int steps)
{
	atomic_set(&fail_after, steps);
}

static bool event_fail(void)
{
	return atomic_read(&fail_after) >= 0 &&
		atomic_dec_return(&fail_after) < 0;
}
#else
static bool event_fail(void)
{
	return false;
}
#endif

static void event_put(struct mmix_spawn_event *event)
{
	if (refcount_dec_and_test(&event->refs)) {
#ifdef CONFIG_MMIX_USER_TEST
		atomic_long_dec(&events_live);
#endif
		kfree(event);
	}
}

static int event_release(struct inode *inode, struct file *file)
{
	event_put(file->private_data);
	return 0;
}

static __poll_t event_poll(struct file *file, poll_table *wait)
{
	struct mmix_spawn_event *event = file->private_data;

	poll_wait(file, &event->wait, wait);
	/* Pair with release publication before reading the shared diagnostic. */
	return smp_load_acquire(&event->released) ? EPOLLIN | EPOLLRDNORM : 0;
}

static const struct file_operations event_fops = {
	.release = event_release,
	.poll = event_poll,
	.llseek = noop_llseek,
};

static long event_observe(unsigned long number, bool wait)
{
	struct mmix_spawn_event *event;
	struct fd f;
	long ret;

	if (number > INT_MAX)
		return -EBADF;
	f = fdget(number);
	if (fd_empty(f))
		return -EBADF;
	if (fd_file(f)->f_op != &event_fops) {
		ret = -EINVAL;
		goto out;
	}
	event = fd_file(f)->private_data;
	if (wait) {
		/* Acquire the old image's final writes on completion. */
		ret = wait_event_interruptible(event->wait, smp_load_acquire(&event->released));
	} else {
		/* Pair with mmix_spawn_release, including the completed fast path. */
		ret = smp_load_acquire(&event->released);
	}
out:
	fdput(f);
	return ret;
}

void mmix_spawn_attach(struct task_struct *task)
{
	struct mmix_spawn_event *event = current->thread.spawn_prepare;

	if (event) {
		refcount_inc(&event->refs);
		task->thread.spawn_event = event;
#ifdef CONFIG_MMIX_USER_TEST
		atomic_long_inc(&producers_live);
#endif
	}
}

/* Never publish completion for an unpublished child on fork unwind. */
void mmix_spawn_drop(struct task_struct *task)
{
	struct mmix_spawn_event *event = task->thread.spawn_event;

	task->thread.spawn_event = NULL;
	if (event) {
#ifdef CONFIG_MMIX_USER_TEST
		atomic_long_dec(&producers_live);
#endif
		event_put(event);
	}
}

void mmix_spawn_release(struct task_struct *task)
{
	struct mmix_spawn_event *event = task->thread.spawn_event;

	if (!event)
		return;
	task->thread.spawn_event = NULL;
	/* The old user image can no longer execute or change its diagnostic. */
	smp_store_release(&event->released, MMIX_SPAWN_RELEASED);
	wake_up_interruptible_poll(&event->wait, EPOLLIN | EPOLLRDNORM);
#ifdef CONFIG_MMIX_USER_TEST
	atomic_long_dec(&producers_live);
#endif
	event_put(event);
}

long mmix_spawn(unsigned long operation, unsigned long request,
		unsigned long size, unsigned long reserved1,
		unsigned long reserved2, unsigned long reserved3)
{
	struct kernel_clone_args clone = {
		.flags = CLONE_PIDFD,
		.exit_signal = SIGCHLD,
	};
	struct mmix_spawn_args args;
	struct mmix_spawn_event *event;
	struct file *file;
	int fd;
	long ret;

	if (reserved1 || reserved2 || reserved3)
		return -EINVAL;
	if (operation == MMIX_SPAWN_QUERY)
		return request || size ? -EINVAL : MMIX_SPAWN_CAP_V1;
	if (operation == MMIX_SPAWN_EVENT_QUERY || operation == MMIX_SPAWN_EVENT_WAIT) {
		if (size)
			return -EINVAL;
		return event_observe(request, operation == MMIX_SPAWN_EVENT_WAIT);
	}
	if (operation != MMIX_SPAWN_CREATE || size != sizeof(args))
		return -EINVAL;
	if (current->thread.spawn_event)
		return -EOPNOTSUPP;
	if (copy_from_user(&args, (void __user *)request, sizeof(args)))
		return -EFAULT;
	if (args.reserved || args.reserved2 || args.event_fd == args.pidfd ||
	    (args.event_fd & 3) || (args.pidfd & 3))
		return -EINVAL;
	if (args.version != MMIX_SPAWN_VERSION)
		return -EOPNOTSUPP;
	if (!access_ok(u64_to_user_ptr(args.event_fd), sizeof(int)) ||
	    !access_ok(u64_to_user_ptr(args.pidfd), sizeof(int)))
		return -EFAULT;
	if (event_fail())
		return -ENOMEM;
	event = kzalloc_obj(*event);
	if (!event)
		return -ENOMEM;
#ifdef CONFIG_MMIX_USER_TEST
	atomic_long_inc(&events_live);
#endif
	refcount_set(&event->refs, 1);
	init_waitqueue_head(&event->wait);
	if (event_fail()) {
		event_put(event);
		return -ENOMEM;
	}
	file = anon_inode_getfile("[mmix-spawn]", &event_fops, event, O_RDONLY);
	if (IS_ERR(file)) {
		event_put(event);
		return PTR_ERR(file);
	}
	if (event_fail()) {
		ret = -ENOMEM;
		goto put_file;
	}
	fd = get_unused_fd_flags(O_CLOEXEC);
	if (fd < 0) {
		ret = fd;
		goto put_file;
	}
	ret = put_user(fd, (int __user *)u64_to_user_ptr(args.event_fd));
	if (ret)
		goto put_fd;
	if (event_fail()) {
		ret = -ENOMEM;
		goto put_fd;
	}
	clone.pidfd = u64_to_user_ptr(args.pidfd);
	current->thread.spawn_prepare = event;
	ret = kernel_clone(&clone);
	current->thread.spawn_prepare = NULL;
	if (ret < 0)
		goto put_fd;
	/* No fallible operation remains after the child becomes runnable. */
	fd_install(fd, file);
	return ret;

put_fd:
	put_unused_fd(fd);
put_file:
	fput(file);
	return ret;
}
