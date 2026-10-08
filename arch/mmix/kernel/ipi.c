// SPDX-License-Identifier: GPL-2.0-only
#include <linux/errno.h>
#include <linux/of_address.h>
#include <linux/smp.h>
#include <linux/timekeeping.h>
#include <asm/boot.h>
#include <asm/io.h>
#include <asm/irqflags.h>
#include "entry.h"
#include "ipi.h"
#include "smp_boot.h"

#define IPI_RESCHEDULE BIT(0)
#define IPI_CALL BIT(1)
#define IPI_STOP BIT(2)
#define IPI_FLUSH BIT(3)
#define WORK_IDLE 0UL
#define WORK_QUEUED 1UL
#define WORK_DONE 2UL

struct mmix_ipi_state {
	unsigned long ready;
	atomic_long_t messages;
	struct mmix_ipi_work *work;
	unsigned long interrupts, callbacks, rejected;
} __aligned(64);
static struct mmix_ipi_state ipi_state[MMIX_BOOT_CPU_SLOTS];
static void __iomem *ipi_global;
static void __iomem *ipi_context[MMIX_BOOT_CPU_SLOTS];
static unsigned int ipi_cpus;

struct mmix_flush_request {
	unsigned long request, ack, operations;
};

static struct mmix_flush_request flush_requests[MMIX_BOOT_CPU_SLOTS][MMIX_BOOT_CPU_SLOTS];
static unsigned long flush_generation[MMIX_BOOT_CPU_SLOTS];

static void local_flush(unsigned long operations)
{
	if (operations & MMIX_FLUSH_CACHE)
		/* Clean before clearing; IRQs cannot dirty this CPU's cache in between. */
		asm volatile("SYNC 5\n\tSYNC 7" ::: "memory");
	if (operations & MMIX_FLUSH_TLB)
		asm volatile("SYNC 3\n\tSYNC 6" ::: "memory");
}

void mmix_ipi_flush_poll(void)
{
	unsigned int cpu = mmix_cpu_id(), sender;

	for (sender = 0; sender < ipi_cpus; sender++) {
		struct mmix_flush_request *work = &flush_requests[cpu][sender];
		/* Acquire page-table publication before invalidating the local cache. */
		unsigned long generation = smp_load_acquire(&work->request);

		if (generation == READ_ONCE(work->ack))
			continue;
		local_flush(READ_ONCE(work->operations));
		/* No caller can recycle mapping storage until this invalidation completes. */
		smp_store_release(&work->ack, generation);
	}
}

void mmix_ipi_flush(unsigned long operations)
{
	unsigned long flags, generation, targets = 0;
	unsigned int cpu, self;

	local_irq_save(flags);
	self = mmix_cpu_id();
	generation = ++flush_generation[self];
	/* Code/data writes must reach memory before a receiver clears its caches. */
	if (operations & MMIX_FLUSH_CACHE)
		asm volatile("SYNC 5" ::: "memory");
	for_each_online_cpu(cpu) {
		if (cpu == self)
			continue;
		/* Fill this sender's operation before publishing the generation. */
		WRITE_ONCE(flush_requests[cpu][self].operations, operations);
		/* Release page-table/code updates and the operation to the receiver. */
		smp_store_release(&flush_requests[cpu][self].request, generation);
		atomic_long_or(IPI_FLUSH, &ipi_state[cpu].messages);
		targets |= BIT(cpu);
	}
	mmix_ipi_kick(targets);
	local_flush(operations);
	for_each_online_cpu(cpu) {
		unsigned long start = ktime_get_mono_fast_ns();

		if (!(targets & BIT(cpu)))
			continue;
		/* Acquire completion before reuse; poll peers to avoid masked-IRQ deadlock. */
		while (smp_load_acquire(&flush_requests[cpu][self].ack) != generation) {
			mmix_ipi_flush_poll();
			if (ktime_get_mono_fast_ns() - start > 5000000000UL)
				panic("MMIX: remote cache/TLB flush CPU %u timed out", cpu);
			cpu_relax();
		}
	}
	local_irq_restore(flags);
}

bool mmix_ipi_ready(unsigned int cpu)
{
	/* Acquire the target's initialized vector and mailbox state. */
	return cpu < ipi_cpus && smp_load_acquire(&ipi_state[cpu].ready);
}

void mmix_ipi_kick(unsigned long targets)
{
	if (ipi_global) {
		/* Software publication must precede the coalescing hardware doorbell. */
		mb();
		iowrite64be(targets & ((1UL << ipi_cpus) - 1), ipi_global + 8);
	}
}

void __init mmix_ipi_init(void)
{
	struct device_node *node;
	struct resource resource, contexts;
	u32 count, stride, bit;
	unsigned int cpu;

	node = of_find_compatible_node(NULL, NULL, "qemu,mmix-ipi");
	if (!node || !of_device_is_available(node) ||
	    of_property_read_u32(node, "qemu,context-count", &count) ||
	    count != mmix_get_boot_info()->nr_cpus || count > MMIX_BOOT_CPU_SLOTS ||
	    of_property_read_u32(node, "qemu,context-stride", &stride) || stride != 0x10000 ||
	    of_property_read_u32(node, "qemu,request-bit", &bit) || bit != 9 ||
	    of_address_to_resource(node, 0, &resource) || resource_size(&resource) < 16 ||
	    of_address_to_resource(node, 1, &contexts) || resource_size(&contexts) < count * stride)
		panic("MMIX: invalid IPI description");
	ipi_global = ioremap(resource.start, resource_size(&resource));
	if (!ipi_global || ioread64be(ipi_global) != (1UL << count) - 1)
		panic("MMIX: invalid IPI active population");
	for (cpu = 0; cpu < count; cpu++) {
		ipi_context[cpu] = ioremap(contexts.start + cpu * stride, stride);
		if (!ipi_context[cpu])
			panic("MMIX: cannot map IPI context");
		iowrite64be(1, ipi_context[cpu] + 8);
	}
	of_node_put(node);
	ipi_cpus = count;
	/* CPU 0 already has an ordinary task-owned exception entry. */
	smp_store_release(&ipi_state[0].ready, 1);
}

int mmix_ipi_work_init(struct mmix_ipi_work *work, void (*function)(void *),
		       void *argument, unsigned long generation)
{
	unsigned long state = READ_ONCE(work->state);

	if (!generation || (long)function >= 0 || ((unsigned long)function & 3))
		return -EINVAL;
	if (state == WORK_QUEUED)
		return -EBUSY;
	/* Acquire the final receiver access; an unsubmitted IDLE object is reusable. */
	if (state == WORK_DONE && smp_load_acquire(&work->ack) != work->generation)
		return -EBUSY;
	work->function = function;
	work->argument = argument;
	work->generation = generation;
	work->ack = 0;
	work->result = -EINPROGRESS;
	work->state = WORK_IDLE;
	return 0;
}

static void complete_work(struct mmix_ipi_work *work, int result)
{
	work->result = result;
	WRITE_ONCE(work->state, WORK_DONE);
	/* This is the receiver's last access; the owner may now reuse the object. */
	smp_store_release(&work->ack, work->generation);
}

int mmix_ipi_queue(unsigned int cpu, struct mmix_ipi_work *work)
{
	struct mmix_ipi_state *state;

	if (cpu >= ipi_cpus || !work->generation || (long)work->function >= 0 ||
	    ((unsigned long)work->function & 3))
		return -EINVAL;
	if (!mmix_ipi_ready(cpu) || READ_ONCE(mmix_secondary_abort))
		return -ESHUTDOWN;
	if (cmpxchg(&work->state, WORK_IDLE, WORK_QUEUED) != WORK_IDLE)
		return -EBUSY;
	state = &ipi_state[cpu];
	work->target = cpu;
	if (cmpxchg(&state->work, NULL, work)) {
		WRITE_ONCE(work->state, WORK_IDLE);
		return -EBUSY;
	}
	/* Close publication versus target retirement; only an unowned item cancels. */
	if (!mmix_ipi_ready(cpu) && cmpxchg(&state->work, work, NULL) == work)
		complete_work(work, -ESHUTDOWN);
	return 0;
}

int mmix_ipi_call(unsigned int cpu, struct mmix_ipi_work *work)
{
	int result = mmix_ipi_queue(cpu, work);

	if (!result)
		mmix_ipi_kick(BIT(cpu));
	return result;
}

static void send_message(unsigned int cpu, unsigned long message)
{
	if (mmix_ipi_ready(cpu)) {
		atomic_long_or(message, &ipi_state[cpu].messages);
		mmix_ipi_kick(BIT(cpu));
	}
}

void mmix_ipi_cpu_start(void)
{
	unsigned int cpu = mmix_cpu_id();

	if (READ_ONCE(mmix_secondary_abort))
		return;
	asm volatile("PUT rTT,%0" : : "r" (mmix_boot_ipi_entry) : "memory");
	/* Publish the resident boot IPI entry before accepting a remote descriptor. */
	smp_store_release(&ipi_state[cpu].ready, 1);
	arch_local_irq_restore(MMIX_KERNEL_FAULT_MASK | MMIX_IRQ_IPI);
}

void mmix_ipi_cpu_retire(void)
{
	struct mmix_ipi_state *state = &ipi_state[mmix_cpu_id()];
	struct mmix_ipi_work *work;

	arch_local_irq_restore(0);
	/* Late publishers recheck this gate and cancel anything not taken here. */
	smp_store_release(&state->ready, 0);
	work = xchg(&state->work, NULL);
	if (work)
		complete_work(work, -ESHUTDOWN);
	atomic_long_xchg(&state->messages, 0);
	if (ipi_context[mmix_cpu_id()])
		iowrite64be(1, ipi_context[mmix_cpu_id()] + 8);
	mmix_ack_requests(MMIX_IRQ_IPI);
}

static void __noreturn stop_self(void)
{
	unsigned int cpu = mmix_cpu_id();

	mmix_ipi_cpu_retire();
	if (cpu_online(cpu))
		set_cpu_online(cpu, false);
	/* All work has retired before the requester accepts the terminal state. */
	smp_store_release(&mmix_secondary_boot[cpu].status, MMIX_SECONDARY_STOPPED);
	for (;;)
		cpu_relax();
}

void mmix_ipi_interrupt(void)
{
	unsigned int cpu = mmix_cpu_id(), count;
	struct mmix_ipi_state *state = &ipi_state[cpu];

	state->interrupts++;
	for (count = 0; count < 64; count++) {
		struct mmix_ipi_work *work;
		unsigned long messages;

		/* Clear before draining: a later publication either drains or reasserts. */
		iowrite64be(1, ipi_context[cpu] + 8);
		mmix_ack_requests(MMIX_IRQ_IPI);
		messages = atomic_long_xchg(&state->messages, 0);
		if (messages & IPI_FLUSH)
			mmix_ipi_flush_poll();
		if ((messages & IPI_STOP) ||
		    (READ_ONCE(mmix_secondary_abort) && !cpu_online(cpu)))
			stop_self();
		work = xchg(&state->work, NULL);
		if (work) {
			int result = 0;

			if (READ_ONCE(mmix_secondary_abort))
				result = -ESHUTDOWN;
			else if (work->state != WORK_QUEUED || work->ack)
				result = -ESTALE;
			else if (work->target != cpu || !work->generation ||
				 (long)work->function >= 0 || ((unsigned long)work->function & 3))
				result = -EINVAL;
			else {
				work->function(work->argument);
				state->callbacks++;
			}
			if (result)
				state->rejected++;
			complete_work(work, result);
		}
		if (cpu_online(cpu)) {
			if (messages & IPI_CALL)
				generic_smp_call_function_single_interrupt();
			if (messages & IPI_RESCHEDULE)
				scheduler_ipi();
		}
		if (!READ_ONCE(state->work) && !atomic_long_read(&state->messages) &&
		    !ioread64be(ipi_context[cpu]))
			return;
	}
	/* Bound IRQ occupancy without leaving undrained software work unnotified. */
	mmix_ipi_kick(BIT(cpu));
}

void mmix_ipi_send_stop(unsigned int cpu)
{
	send_message(cpu, IPI_STOP);
}

void arch_smp_send_reschedule(int cpu)
{
	if (cpu_online(cpu))
		send_message(cpu, IPI_RESCHEDULE);
}

void arch_send_call_function_single_ipi(int cpu)
{
	if (cpu_online(cpu))
		send_message(cpu, IPI_CALL);
}

void arch_send_call_function_ipi_mask(const struct cpumask *mask)
{
	unsigned int cpu;
	unsigned long targets = 0;

	for_each_cpu_and(cpu, mask, cpu_online_mask) {
		if (!mmix_ipi_ready(cpu))
			continue;
		atomic_long_or(IPI_CALL, &ipi_state[cpu].messages);
		targets |= BIT(cpu);
	}
	mmix_ipi_kick(targets);
}
