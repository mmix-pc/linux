// SPDX-License-Identifier: GPL-2.0-only
#include <kunit/test.h>
#include <linux/elf.h>
#include <linux/fdtable.h>
#include <linux/file.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/mman.h>
#include <linux/mm.h>
#include <linux/sched/mm.h>
#include <linux/sched/signal.h>
#include <linux/sched/task.h>
#include <linux/shmem_fs.h>
#include <linux/uaccess.h>
#include <asm/cacheflush.h>
#include <asm/unistd.h>
#include "user_entry.h"
#include "exec_test.h"

#define CODE (4UL << 20)
#define DATA (6UL << 20)
#define STACK (8UL << 20)
#define FILE_MAP (10UL << 20)

struct exec_test {
	int fd;
	int entries;
	int allocation_step;
	unsigned long entry, bss;
	unsigned long calls;
	unsigned int post_failures;
	unsigned int concurrent_faults;
	struct mm_struct *old_mm;
};

static int check_initial(struct mmix_user_entry *entry, struct exec_test *t)
{
	struct user_regs_struct *regs = &mmix_user_rstack_state(entry->stack)->regs;
	unsigned long words[16], value, sp = regs->regs[254];
	unsigned long cursor, tag, auxval;
	unsigned int i, tags = 0;
	char string[16];

	if (regs->pc != t->entry + 4 || regs->r_g != 230 || regs->r_l || regs->r_a || regs->r_b ||
	    regs->r_d || regs->r_e || regs->r_h || regs->r_j || regs->r_m || regs->r_p ||
	    regs->r_r || regs->r_w || regs->r_x || regs->r_y || regs->r_z || (sp & 7) || !regs->r_o)
		return 1;
	for (i = 0; i < 256; i++)
		if (i != 254 && regs->regs[i])
			return 2;
	if (copy_from_user(words, (void __user *)sp, sizeof(words)) || words[0] != 2 || words[3] ||
	    words[5])
		return 3;
	if (strncpy_from_user(string, (void __user *)words[1], sizeof(string)) != 9 ||
	    strcmp(string, "mmix-exec"))
		return 4;
	if (strncpy_from_user(string, (void __user *)words[2], sizeof(string)) != 5 ||
	    strcmp(string, "probe"))
		return 5;
	if (strncpy_from_user(string, (void __user *)words[4], sizeof(string)) != 11 ||
	    strcmp(string, "MMIX_EXEC=1"))
		return 6;
	cursor = sp + 6 * sizeof(unsigned long);
	for (i = 0; i < 64; i++, cursor += 16) {
		if (get_user(tag, (unsigned long __user *)cursor) ||
		    get_user(auxval, (unsigned long __user *)(cursor + 8)))
			return 7;
		if (!tag)
			break;
		if (tag == AT_ENTRY && auxval == t->entry)
			tags |= 1;
		if (tag == AT_PAGESZ && auxval == PAGE_SIZE)
			tags |= 2;
		if (tag == AT_CLKTCK && auxval == 100)
			tags |= 4;
		if (tag == AT_PHENT && auxval == sizeof(struct elf64_phdr))
			tags |= 8;
		if (tag == AT_PHDR && auxval == 0x10000 + sizeof(struct elf64_hdr))
			tags |= 16;
		if (tag == AT_RANDOM && !copy_from_user(words, (void __user *)auxval, 16))
			tags |= 32;
		if (tag == AT_MINSIGSTKSZ && auxval == 32768)
			tags |= 128;
		if (tag == AT_HWCAP && !auxval)
			tags |= 256;
		if (tag == AT_BASE && !auxval)
			tags |= 64;
	}
	if (tags != 511 || i == 64)
		return 8;
	if (get_user(value, (unsigned long __user *)t->bss) || value)
		return 9;
	if (put_user(1UL, (unsigned long __user *)regs->r_o) ||
	    get_user(value, (unsigned long __user *)(regs->r_o - 8)) != -EFAULT ||
	    get_user(value, (unsigned long __user *)(regs->r_o + (64UL << 10))) != -EFAULT)
		return 10;
	return 0;
}

static int exec_event(struct mmix_user_entry *entry, void *data)
{
	struct exec_test *t = data;
	struct user_regs_struct *regs = &mmix_user_rstack_state(entry->stack)->regs;
	int error;

	if (entry->event == MMIX_USER_FAULT) {
		if (entry->regs.r_xx >> 32 == 0x03000000UL &&
		    (entry->regs.r_q & MMIX_IRQ_CONTROLLER))
			t->concurrent_faults++;
		error = mmix_user_fault(entry, NULL);
		if (error)
			pr_err("MMIX_EXEC fault pc=%lx xx=%lx q=%lx entries=%d calls=%lu\n",
			       entry->regs.pc, entry->regs.r_xx, entry->regs.r_q,
			       t->entries, t->calls);
		return error;
	}
	if (entry->regs.pc == t->entry + 4) {
		local_irq_enable();
		error = check_initial(entry, t);
		local_irq_disable();
		if (error) {
			pr_err("MMIX_EXEC initial check %d failed\n", error);
			return -EINVAL;
		}
		if (++t->entries == 2)
			return 1;
		regs->regs[239] = t->fd;
		return 0;
	}
	if (entry->regs.pc == CODE + 8) {
		/* A pre-commit loader rejection must resume the unchanged caller. */
		return (long)regs->regs[231] == -ENOEXEC && current->mm == t->old_mm &&
				       regs->regs[230] == 0xdeadbeef ?
			       1 :
			       -EINVAL;
	}
	if (entry->regs.syscall_nr != __NR_execveat)
		return -EINVAL;
	if (!t->calls++ && t->allocation_step >= 0)
		mmix_user_rstack_fail_after(t->allocation_step);
	error = mmix_user_syscall(entry, NULL);
	if (current->thread.exec_committed && !current->thread.exec_pending && error) {
		t->post_failures++;
		if (error != -EFAULT || entry->fatal_signal != SIGSEGV)
			return -EINVAL;
	}
	return error;
}

static const struct mmix_user_entry_ops exec_ops = {
	.event = exec_event,
	.write = mmix_user_write,
};

static int exec_child(void *data)
{
	struct exec_test *t = data;
	struct mmix_user_state *state;

	t->old_mm = current->mm;
	current->thread.user_state = mmix_user_rstack_alloc();
	if (!current->thread.user_state)
		return 1;
	state = mmix_user_rstack_state(current->thread.user_state);
	state->regs.pc = CODE;
	state->regs.r_g = 230;
	state->regs.r_o = STACK;
	state->pending.start = STACK;
	state->regs.regs[254] = STACK + PAGE_SIZE;
	state->regs.regs[230] = 0xdeadbeef;
	state->regs.regs[231] = t->fd;
	state->regs.regs[232] = DATA;
	state->regs.regs[233] = DATA + 8;
	state->regs.regs[234] = DATA + 32;
	state->regs.regs[235] = AT_EMPTY_PATH;
	state->regs.regs[237] = __NR_execveat;
	return mmix_user_enter(current->thread.user_state, &exec_ops, t) ? 1 : 0;
}

static void exec_images(struct kunit *test)
{
	static const char strings[] = "mmix-exec\0probe\0MMIX_EXEC=1";
	__sighandler_t handler = current->sighand->action[SIGCHLD - 1].sa.sa_handler;
	struct mm_struct *mm;
	size_t length = mmix_exec_test_end - mmix_exec_test_image;
	long baseline = mmix_user_rstack_live();
	unsigned char *image = kunit_kmalloc(test, length, GFP_KERNEL);
	struct kernel_clone_args args = { .fn = exec_child, .exit_signal = SIGCHLD };
	unsigned long pointers[] = { 0, DATA + 48, DATA + 58, 0, DATA + 64, 0 };
	struct exec_test t;
	struct elf64_hdr *hdr = (void *)image;
	struct elf64_phdr *phdr;
	struct file *file;
	unsigned int variant, i, trial, concurrent_faults = 0;
	pid_t child;
	int status, error, expected_status;

	KUNIT_ASSERT_NOT_NULL(test, image);
	mm = mm_alloc();
	KUNIT_ASSERT_NOT_NULL(test, mm);
	kthread_use_mm(mm);
	kernel_sigaction(SIGCHLD, SIG_DFL);
	for (i = 0; i < 3; i++)
		KUNIT_EXPECT_EQ(test,
				vm_mmap(NULL, CODE + i * (2UL << 20), PAGE_SIZE,
					PROT_READ | PROT_WRITE | PROT_EXEC,
					MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, 0),
				CODE + i * (2UL << 20));
	KUNIT_EXPECT_EQ(test,
			copy_to_user((void __user *)CODE, mmix_exec_test_launch,
				     mmix_exec_test_launch_end - mmix_exec_test_launch),
			0UL);
	flush_icache_range(CODE, CODE + PAGE_SIZE);
	KUNIT_EXPECT_EQ(test, copy_to_user((void __user *)DATA, pointers, sizeof(pointers)), 0UL);
	KUNIT_EXPECT_EQ(test, copy_to_user((void __user *)(DATA + 48), strings, sizeof(strings)),
			0UL);
	for (trial = 0; trial < 39; trial++) {
		variant = trial < 19 ? trial : trial % 2;
		memcpy(image, mmix_exec_test_image, length);
		phdr = (void *)(image + hdr->e_phoff);
		memset(&t, 0, sizeof(t));
		t.allocation_step = -1;
		t.entry = hdr->e_entry;
		for (i = 0; i < hdr->e_phnum; i++)
			if (phdr[i].p_type == PT_LOAD && phdr[i].p_memsz > phdr[i].p_filesz)
				t.bss = PAGE_ALIGN(phdr[i].p_vaddr + phdr[i].p_filesz);
		switch (variant) {
		case 1:
			hdr->e_shoff = 0;
			hdr->e_shnum = 0;
			hdr->e_shstrndx = 0;
			for (i = 0; i < hdr->e_phnum; i++)
				phdr[i].p_paddr = ~0UL;
			break;
		case 2:
			hdr->e_ident[EI_CLASS] = ELFCLASS32;
			break;
		case 3:
			hdr->e_ident[EI_DATA] = ELFDATA2LSB;
			break;
		case 4:
			hdr->e_entry |= 1;
			break;
		case 5:
			hdr->e_entry = MMIX_TASK_SIZE;
			break;
		case 6:
			hdr->e_phentsize = 1;
			break;
		case 7:
			phdr[0].p_filesz = phdr[0].p_memsz + 1;
			break;
		case 8:
			hdr->e_entry = t.bss;
			t.entry = hdr->e_entry;
			break;
		case 9:
		case 10:
		case 11:
			t.allocation_step = variant - 9;
			break;
		case 12:
			hdr->e_version = 0;
			break;
		case 13:
			hdr->e_type = ET_DYN;
			break;
		case 14:
			phdr[0].p_offset = 1;
			break;
		case 15:
			hdr->e_entry = 0;
			break;
		case 16:
			phdr[1].p_vaddr = 0;
			break;
		case 17:
			hdr->e_flags = 1;
			break;
		case 18:
			hdr->e_machine = 0;
			break;
		}
		expected_status = ((variant >= 7 && variant <= 11) ||
				   variant == 14 || variant == 16) ? SIGSEGV : 0;
		file = shmem_file_setup("mmix-exec", length, EMPTY_VMA_FLAGS);
		if (IS_ERR(file)) {
			KUNIT_FAIL(test, "create ELF file");
			break;
		}
		t.fd = get_unused_fd_flags(0);
		if (t.fd < 0) {
			fput(file);
			KUNIT_FAIL(test, "allocate fd");
			break;
		}
		KUNIT_EXPECT_EQ(test,
				vm_mmap(file, FILE_MAP, PAGE_ALIGN(length), PROT_READ | PROT_WRITE,
					MAP_SHARED | MAP_FIXED, 0),
				FILE_MAP);
		KUNIT_EXPECT_EQ(test, copy_to_user((void __user *)FILE_MAP, image, length), 0UL);
		KUNIT_EXPECT_EQ(test, vm_munmap(FILE_MAP, PAGE_ALIGN(length)), 0);
		fd_install(t.fd, file);
		args.fn_arg = &t;
		child = kernel_clone(&args);
		KUNIT_EXPECT_GT(test, child, 0);
		if (child > 0) {
			status = 0;
			error = kernel_wait(child, &status);
			KUNIT_EXPECT_EQ(test, error, child);
			KUNIT_EXPECT_EQ_MSG(test, status, expected_status,
					    "ELF variant %u", variant);
		}
		if (variant < 2) {
			KUNIT_EXPECT_EQ(test, t.entries, 2);
			KUNIT_EXPECT_EQ(test, t.calls, 2UL);
		}
		KUNIT_EXPECT_EQ(test, t.post_failures,
				expected_status ? 1U : 0U);
		concurrent_faults += t.concurrent_faults;
		mmix_user_rstack_fail_after(-1);
		KUNIT_EXPECT_EQ(test, close_fd(t.fd), 0);
		for (i = 0; i < 100 && mmix_user_rstack_live() != baseline; i++)
			usleep_range(10000, 11000);
		KUNIT_EXPECT_EQ(test, mmix_user_rstack_live(), baseline);
	}
	kernel_sigaction(SIGCHLD, handler);
	kthread_unuse_mm(mm);
	mmput(mm);
	kunit_info(test, "MMIX_EXEC concurrent_faults=%u\n", concurrent_faults);
	kunit_info(test, "MMIX_EXEC static=ok sectionless=ok replacement=ok invalid=ok\n");
}

static struct kunit_case exec_cases[] = { KUNIT_CASE(exec_images), {} };
static struct kunit_suite exec_suite = { .name = "mmix_exec", .test_cases = exec_cases };
kunit_test_suite(exec_suite);
