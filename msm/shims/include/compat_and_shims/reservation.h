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
