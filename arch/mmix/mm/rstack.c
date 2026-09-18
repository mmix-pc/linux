// SPDX-License-Identifier: GPL-2.0-only
#include <linux/err.h>
#include <linux/list.h>
#include <linux/mm.h>
#include <linux/mman.h>
#include <linux/mutex.h>
#include <linux/refcount.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/sizes.h>
#include <asm/rstack.h>

#define RSTACK_BACKING_SIZE SZ_1M
#define RSTACK_DOMAIN_SIZE (RSTACK_BACKING_SIZE + 3 * PAGE_SIZE)
#define RSTACK_MAX_DOMAINS 256
#define RSTACK_MAX_DEPTH 32

struct mmix_rstack_domain {
	struct list_head list;
	refcount_t refs;
	u64 id, parent;
	unsigned long start;
	unsigned int depth;
	unsigned int claims;
	bool managed;
};

struct mmix_rstack_owner {
	/* No mm reference: a task or unpublished-child caller holds mm_users. */
	struct mm_struct *mm;
	unsigned int count;
	u64 ids[RSTACK_MAX_DOMAINS];
};

struct mmix_rstack_registry {
	/* Always acquired before mmap_lock; never acquired by the fault path. */
	struct mutex lock;
	struct list_head domains;
	u64 next_id;
	unsigned int count;
};

#ifdef CONFIG_MMIX_USER_TEST
static atomic_long_t live_domains = ATOMIC_LONG_INIT(0);

long mmix_rstack_domains_live(void)
{
	return atomic_long_read(&live_domains);
}
#endif

static void account_domain(int delta)
{
#ifdef CONFIG_MMIX_USER_TEST
	atomic_long_add(delta, &live_domains);
#endif
}

#ifdef CONFIG_MMIX_BOOT_TEST
static atomic_t allocation_failure = ATOMIC_INIT(-1);
static atomic_t release_failure = ATOMIC_INIT(0);

void mmix_rstack_domain_fail_release(void)
{
	atomic_set(&release_failure, 1);
}

void mmix_rstack_domain_fail_after(int step)
{
	atomic_set(&allocation_failure, step);
}

static bool fail_allocation(void)
{
	return atomic_read(&allocation_failure) >= 0 &&
		atomic_dec_return(&allocation_failure) < 0;
}
#else
static bool fail_allocation(void)
{
	return false;
}
#endif

static struct mmix_rstack_domain *find_domain(struct mm_struct *mm, u64 id)
{
	struct mmix_rstack_domain *domain;

	/* Caller holds the registry mutex or the mm write lock (fork). */
	list_for_each_entry(domain, &mm->context.rstacks->domains, list)
		if (domain->id == id)
			return domain;
	return NULL;
}

static bool owner_has(struct mmix_rstack_owner *owner, u64 id)
{
	unsigned int i;

	for (i = 0; i < owner->count; i++)
		if (owner->ids[i] == id)
			return true;
	return false;
}

int mmix_rstack_mm_init(struct mm_struct *mm)
{
	struct mmix_rstack_registry *registry;

	/* mm_init copied the parent's context; never retain its ownership pointer. */
	mm->context.rstacks = NULL;
	registry = kzalloc_obj(*registry);
	if (!registry)
		return -ENOMEM;
	mutex_init(&registry->lock);
	INIT_LIST_HEAD(&registry->domains);
	registry->next_id = 1;
	mm->context.rstacks = registry;
	return 0;
}

void mmix_rstack_mm_destroy(struct mm_struct *mm)
{
	struct mmix_rstack_registry *registry = mm->context.rstacks;
	struct mmix_rstack_domain *domain, *next;

	if (!registry)
		return;
	list_for_each_entry_safe(domain, next, &registry->domains, list) {
		WARN_ON_ONCE(refcount_read(&domain->refs) != 1 || domain->claims);
		list_del(&domain->list);
		account_domain(-1);
		kfree(domain);
	}
	kfree(registry);
	mm->context.rstacks = NULL;
}

static void mark_domain(struct mm_struct *mm, struct mmix_rstack_domain *domain,
			bool owned)
{
	VMA_ITERATOR(vmi, mm, domain->start);
	unsigned long end = domain->start + RSTACK_DOMAIN_SIZE;
	struct vm_area_struct *vma;

	mmap_assert_write_locked(mm);
	for_each_vma_range(vmi, vma, end) {
		if (owned) {
			/* READ_IMPLIES_EXEC must not make register backing executable. */
			vm_flags_clear(vma, VM_EXEC | VM_MAYEXEC);
			vm_flags_set(vma, VM_ARCH_1);
			vma_set_page_prot(vma);
		} else {
			vm_flags_clear(vma, VM_ARCH_1);
		}
	}
}

int mmix_rstack_domain_create(struct mm_struct *mm, u64 parent, u64 *id,
			      unsigned long *base)
{
	struct mmix_rstack_registry *registry = mm->context.rstacks;
	struct mmix_rstack_domain *domain, *ancestor;
	struct mmix_rstack_owner *owner = current->thread.rstack_owner;
	unsigned long address, backing, populate;
	vma_flags_t flags = legacy_to_vma_flags(VM_DONTEXPAND);
	int error = -ENOMEM;

	if (mm != current->mm || (owner && owner->mm != mm))
		return -EINVAL;
	if (fail_allocation())
		return -ENOMEM;
	domain = kzalloc_obj(*domain);
	if (!domain)
		return -ENOMEM;
	mutex_lock(&registry->lock);
	if (registry->count == RSTACK_MAX_DOMAINS || !registry->next_id)
		goto out;
	if (owner && (owner->count == RSTACK_MAX_DOMAINS ||
		      (parent && !owner_has(owner, parent)))) {
		error = -EPERM;
		goto out;
	}
	if (parent) {
		ancestor = find_domain(mm, parent);
		error = -ESRCH;
		if (!ancestor)
			goto out;
		error = -EOVERFLOW;
		if (ancestor->depth == RSTACK_MAX_DEPTH)
			goto out;
		domain->depth = ancestor->depth + 1;
	}
	/* Reserve the slot and identity before allocation; IDs are never reused. */
	mmap_write_lock(mm);
	domain->id = registry->next_id++;
	domain->parent = parent;
	registry->count++;
	if (fail_allocation()) {
		error = -ENOMEM;
		goto rollback_slot;
	}
	address = do_mmap(NULL, 0, RSTACK_DOMAIN_SIZE, PROT_NONE,
			  MAP_PRIVATE | MAP_ANONYMOUS, flags, 0, &populate, NULL);
	if (IS_ERR_VALUE(address)) {
		error = address;
		goto rollback_slot;
	}
	domain->start = address;
	if (fail_allocation()) {
		error = -ENOMEM;
		do_munmap(mm, address, RSTACK_DOMAIN_SIZE, NULL);
		goto rollback_slot;
	}
	backing = do_mmap(NULL, address + PAGE_SIZE, RSTACK_BACKING_SIZE,
			  PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED,
			  flags, 0, &populate, NULL);
	if (IS_ERR_VALUE(backing)) {
		error = backing;
		do_munmap(mm, address, RSTACK_DOMAIN_SIZE, NULL);
		goto rollback_slot;
	}
	mark_domain(mm, domain, true);
	refcount_set(&domain->refs, 1);
	if (owner) {
		owner->ids[owner->count++] = domain->id;
		domain->claims = 1;
		domain->managed = true;
	}
	list_add_tail(&domain->list, &registry->domains);
	account_domain(1);
	*id = domain->id;
	*base = backing;
	mmap_write_unlock(mm);
	mutex_unlock(&registry->lock);
	return 0;
rollback_slot:
	mmap_write_unlock(mm);
	registry->count--;
out:
	mutex_unlock(&registry->lock);
	kfree(domain);
	return error;
}

static bool has_children(struct mm_struct *mm, u64 id)
{
	struct mmix_rstack_domain *child;

	list_for_each_entry(child, &mm->context.rstacks->domains, list)
		if (child->parent == id)
			return true;
	return false;
}

/* Both registry and mmap write locks are held. Keep protection on failure. */
static int unmap_domain(struct mm_struct *mm, struct mmix_rstack_domain *domain)
{
	int error;

	/* Generic munmap accounting still uses current->mm in this baseline. */
	if (mm != current->mm)
		return -EINVAL;

	#ifdef CONFIG_MMIX_BOOT_TEST
	if (atomic_xchg(&release_failure, 0))
		return -ENOMEM;
#endif
	mark_domain(mm, domain, false);
	error = do_munmap(mm, domain->start, RSTACK_DOMAIN_SIZE, NULL);
	if (error) {
		mark_domain(mm, domain, true);
		return error;
	}
	list_del(&domain->list);
	account_domain(-1);
	mm->context.rstacks->count--;
	kfree(domain);
	return 0;
}

/* Reclaim descendants first, including ancestors whose last owner has exited. */
static int collect_domains(struct mm_struct *mm)
{
	struct mmix_rstack_domain *domain;
	int error;

	/* Unpublished foreign-mm rollback is completed by its ordinary mmput. */
	if (mm != current->mm)
		return 0;

restart:
	list_for_each_entry(domain, &mm->context.rstacks->domains, list) {
		if (!domain->managed || domain->claims ||
		    refcount_read(&domain->refs) != 1 || has_children(mm, domain->id))
			continue;
		error = unmap_domain(mm, domain);
		if (error)
			return error;
		goto restart;
	}
	return 0;
}

struct mmix_rstack_owner *mmix_rstack_owner_alloc(struct mm_struct *mm, u64 chain)
{
	struct mmix_rstack_registry *registry = mm->context.rstacks;
	struct mmix_rstack_owner *owner;
	struct mmix_rstack_domain *domain;
	unsigned int i;
	int error = -ENOMEM;

	if (!chain)
		return ERR_PTR(-EINVAL);
	if (fail_allocation())
		return ERR_PTR(-ENOMEM);
	owner = kzalloc_obj(*owner);
	if (!owner)
		return ERR_PTR(-ENOMEM);
	owner->mm = mm;
	mutex_lock(&registry->lock);
	/* Prepare every acquisition before changing any existing ownership. */
	while (chain) {
		domain = find_domain(mm, chain);
		if (!domain || owner->count > RSTACK_MAX_DEPTH) {
			error = -EINVAL;
			goto fail;
		}
		if (fail_allocation())
			goto fail;
		if (domain->claims == UINT_MAX) {
			error = -EOVERFLOW;
			goto fail;
		}
		owner->ids[owner->count++] = chain;
		chain = domain->parent;
	}
	mmap_write_lock(mm);
	for (i = 0; i < owner->count; i++) {
		domain = find_domain(mm, owner->ids[i]);
		domain->claims++;
		domain->managed = true;
	}
	mmap_write_unlock(mm);
	mutex_unlock(&registry->lock);
	return owner;
fail:
	mutex_unlock(&registry->lock);
	kfree(owner);
	return ERR_PTR(error);
}

int mmix_rstack_owner_free(struct mmix_rstack_owner *owner)
{
	struct mm_struct *mm;
	struct mmix_rstack_domain *domain;
	unsigned int i;
	int error;

	if (!owner)
		return 0;
	mm = owner->mm;
	mutex_lock(&mm->context.rstacks->lock);
	mmap_write_lock(mm);
	for (i = 0; i < owner->count; i++) {
		domain = find_domain(mm, owner->ids[i]);
		if (WARN_ON_ONCE(!domain || !domain->claims))
			continue;
		domain->claims--;
	}
	error = collect_domains(mm);
	mmap_write_unlock(mm);
	mutex_unlock(&mm->context.rstacks->lock);
	kfree(owner);
	return error;
}

void mmix_rstack_detach(struct task_struct *task, struct mm_struct *mm)
{
	struct mmix_rstack_owner *owner = task->thread.rstack_owner;

	if (!owner || owner->mm != mm)
		return;
	task->thread.rstack_owner = NULL;
	/* Failed unmaps retain protected, zero-claim metadata for later collection. */
	mmix_rstack_owner_free(owner);
}

int mmix_rstack_domain_release(struct mm_struct *mm, u64 id)
{
	struct mmix_rstack_registry *registry = mm->context.rstacks;
	struct mmix_rstack_owner *owner = current->thread.rstack_owner;
	struct mmix_rstack_domain *domain;
	unsigned int i;
	int error = -ESRCH;

	mutex_lock(&registry->lock);
	domain = find_domain(mm, id);
	if (!domain)
		goto out;
	if (domain->managed) {
		error = -EPERM;
		if (!owner || owner->mm != mm || !owner_has(owner, id))
			goto out;
		mmap_write_lock(mm);
		for (i = 0; owner->ids[i] != id; i++)
			;
		owner->ids[i] = owner->ids[--owner->count];
		domain->claims--;
		error = collect_domains(mm);
		mmap_write_unlock(mm);
	} else {
		error = -EBUSY;
		if (refcount_read(&domain->refs) != 1 || has_children(mm, id))
			goto out;
		mmap_write_lock(mm);
		error = unmap_domain(mm, domain);
		mmap_write_unlock(mm);
	}
out:
	mutex_unlock(&registry->lock);
	return error;
}

struct mmix_rstack_domain *mmix_rstack_domain_begin(struct mm_struct *mm, u64 id,
						    unsigned long start, size_t len)
{
	struct mmix_rstack_registry *registry = mm->context.rstacks;
	struct mmix_rstack_domain *domain;
	struct vm_area_struct *vma;
	unsigned long base, end;

	if (mm != current->mm)
		return ERR_PTR(-EFAULT);
	mutex_lock(&registry->lock);
	if (current->thread.rstack_owner &&
	    (current->thread.rstack_owner->mm != mm ||
	     !owner_has(current->thread.rstack_owner, id)))
		goto invalid;
	domain = find_domain(mm, current->thread.rstack_chain);
	while (domain && domain->id != id)
		domain = find_domain(mm, domain->parent);
	if (!domain)
		goto invalid;
	base = domain->start + PAGE_SIZE;
	end = base + RSTACK_BACKING_SIZE;
	if (start < base || start > end || len > end - start)
		goto invalid;
	mmap_read_lock(mm);
	end = start + len;
	vma = find_vma(mm, start == end && start == base + RSTACK_BACKING_SIZE ? start - 1 : start);
	for (;;) {
		if (!vma || !vma_is_arch_owned(vma) || !(vma->vm_flags & VM_WRITE) ||
		    vma->vm_start > start) {
			mmap_read_unlock(mm);
			goto invalid;
		}
		if (vma->vm_end >= end)
			break;
		start = vma->vm_end;
		vma = find_vma(mm, start);
	}
	refcount_inc(&domain->refs);
	mmap_read_unlock(mm);
	return domain;
invalid:
	mutex_unlock(&registry->lock);
	return ERR_PTR(-EFAULT);
}

void mmix_rstack_domain_end(struct mm_struct *mm, struct mmix_rstack_domain *domain)
{
	lockdep_assert_held(&mm->context.rstacks->lock);
	refcount_dec(&domain->refs);
	mutex_unlock(&mm->context.rstacks->lock);
}

int mmix_rstack_dup_mmap(struct mm_struct *oldmm, struct mm_struct *mm)
{
	VMA_ITERATOR(vmi, mm, 0);
	struct mmix_rstack_registry *registry = mm->context.rstacks;
	struct mmix_rstack_domain *source, *domain;
	struct vm_area_struct *vma;
	u64 id = current->thread.rstack_chain;

	/*
	 * The calling thread cannot materialize while executing fork. Suspended
	 * ancestors are immutable to other threads. Other domains are not copied
	 * as active chains. mmap_lock serializes registry publication/removal;
	 * taking the transaction mutex here would invert the fault lock order.
	 */
	mmap_assert_write_locked(oldmm);
	registry->next_id = oldmm->context.rstacks->next_id;
	/* Preserve unrelated inherited memory, but strip its chain authority. */
	for_each_vma(vmi, vma)
		if (vma_is_arch_owned(vma))
			vm_flags_clear(vma, VM_ARCH_1);
	while (id) {
		source = find_domain(oldmm, id);
		if (!source)
			return -EINVAL;
		domain = kmemdup(source, sizeof(*domain), GFP_KERNEL);
		if (!domain)
			return -ENOMEM;
		refcount_set(&domain->refs, 1);
		domain->claims = 0;
		domain->managed = false;
		list_add_tail(&domain->list, &registry->domains);
		account_domain(1);
		registry->count++;
		mark_domain(mm, domain, true);
		id = domain->parent;
	}
	return 0;
}
