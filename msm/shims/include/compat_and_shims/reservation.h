#ifndef _MSM_RESERVATION_COMPAT_H_
#define _MSM_RESERVATION_COMPAT_H_

#include <linux/version.h>
#include <linux/rcupdate.h>
#include <linux/seq_file.h>
#include <linux/dma-fence.h>

/*
 * The reservation object was renamed at v5.4 (struct reservation_object ->
 * struct dma_resv, reservation_object_* -> dma_resv_*). Pull whichever system
 * header carries the definitions for the kernel we are building against.
 */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 4, 0)
#include <linux/dma-resv.h>
#else
#include <linux/reservation.h>
#endif

/*
 * reservation_object_describe(): 4.19's debugfs helper that mainline never
 * shipped. Only valid on the reservation_object era (< 5.4); on newer kernels
 * the struct/fields don't exist and msm_gem's debugfs describe path is itself
 * gated to that range.
 */
#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 4, 0)
static inline void reservation_object_describe(struct reservation_object *robj,
					       struct seq_file *m)
{
	struct dma_fence *fence;
	struct reservation_object_list *fobj;
	unsigned int i;

	seq_puts(m, "fences:\n");

	fence = robj->fence_excl;
	if (fence)
		seq_printf(m, "\texcl: %s %llu\n",
			   fence->ops->get_driver_name(fence),
			   fence->context);

	fobj = robj->fence;
	if (fobj) {
		for (i = 0; i < fobj->shared_count; i++) {
			fence = rcu_dereference_protected(fobj->shared[i], 1);
			if (fence)
				seq_printf(m, "\tshared: %s %llu\n",
					   fence->ops->get_driver_name(fence),
					   fence->context);
		}
	}
}
#endif /* < 5.4 */

/*
 * ===========================================================================
 * DRM scheduler implicit-dependency backport (< 5.14)
 * ===========================================================================
 *
 * The backported drm_sched_job_add_implicit_dependencies() wants to walk a
 * reservation object's fences with dma_resv_for_each_fence(), but that iterator
 * (struct dma_resv_iter) only landed at v5.14. For everything older we do the
 * walk by hand. The fence-list access changed names at v5.4 but not shape, so a
 * single body covers both eras via the accessor macros below:
 *
 *   < 5.4   : struct reservation_object / reservation_object_get_excl|_list
 *   5.4..   : struct dma_resv           / dma_resv_get_excl|_list
 *
 * (dma_resv_get_list() was removed at v5.14 in favour of the iterator, which is
 *  exactly why this helper is only compiled for < 5.14.)
 *
 * Semantics mirror dma_resv_usage_rw(write): a reader (write == false) only has
 * to wait on the exclusive (writer) fence; a writer (write == true) waits on the
 * exclusive fence plus every shared (reader) fence.
 *
 * A NULL @resv succeeds trivially. drm_gem_object only gained an embedded ->resv
 * at v5.2; on older kernels (including the 4.19 base) the GEM object carries no
 * reservation object and msm drives implicit sync through msm_gem_sync_object()
 * instead, so the scheduler path is never reached with anything to walk.
 */
#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 14, 0)

/* Avoid pulling the whole scheduler header into this low-level compat shim. */
struct drm_sched_job;
int drm_sched_job_add_dependency(struct drm_sched_job *job,
				 struct dma_fence *fence);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 4, 0)
# define MSM_RESV_T		struct dma_resv
# define MSM_RESV_LIST_T	struct dma_resv_list
# define msm_resv_get_excl	dma_resv_get_excl
# define msm_resv_get_list	dma_resv_get_list
# define msm_resv_held		dma_resv_held
#else
# define MSM_RESV_T		struct reservation_object
# define MSM_RESV_LIST_T	struct reservation_object_list
# define msm_resv_get_excl	reservation_object_get_excl
# define msm_resv_get_list	reservation_object_get_list
# define msm_resv_held		reservation_object_held
#endif

static inline int drm_sched_add_resv_dependencies(struct drm_sched_job *job,
						  MSM_RESV_T *resv, bool write)
{
	MSM_RESV_LIST_T *list;
	struct dma_fence *fence;
	unsigned int i;
	int ret;

	if (!resv)
		return 0;

	/* Always wait on the exclusive (writer) fence. */
	fence = msm_resv_get_excl(resv);
	if (fence) {
		dma_fence_get(fence);
		ret = drm_sched_job_add_dependency(job, fence);
		if (ret)
			return ret;
	}

	/* Readers are only relevant when we ourselves are going to write. */
	if (!write)
		return 0;

	list = msm_resv_get_list(resv);
	for (i = 0; list && i < list->shared_count; i++) {
		fence = rcu_dereference_protected(list->shared[i],
						  msm_resv_held(resv));
		dma_fence_get(fence);
		ret = drm_sched_job_add_dependency(job, fence);
		if (ret)
			return ret;
	}

	return 0;
}

#undef MSM_RESV_T
#undef MSM_RESV_LIST_T
#undef msm_resv_get_excl
#undef msm_resv_get_list
#undef msm_resv_held

#endif /* < 5.14 */

#endif /* _MSM_RESERVATION_COMPAT_H_ */
