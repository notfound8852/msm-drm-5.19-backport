#include <linux/slab.h>
#include <linux/dma-fence.h>

static DEFINE_SPINLOCK(dma_fence_stub_lock);
static struct dma_fence dma_fence_stub;

static const char *dma_fence_stub_get_name(struct dma_fence *fence)
{
        return "stub";
}

static const struct dma_fence_ops dma_fence_stub_ops = {
	.get_driver_name = dma_fence_stub_get_name,
	.get_timeline_name = dma_fence_stub_get_name,
};

/**
 * dma_fence_get_stub - return a signaled fence
 *
 * Return a stub fence which is already signaled. The fence's
 * timestamp corresponds to the first time after boot this
 * function is called.
 */
struct dma_fence *dma_fence_get_stub(void)
{
	spin_lock(&dma_fence_stub_lock);
	if (!dma_fence_stub.ops) {
		dma_fence_init(&dma_fence_stub,
			       &dma_fence_stub_ops,
			       &dma_fence_stub_lock,
			       0, 0);
		dma_fence_signal_locked(&dma_fence_stub);
	}
	spin_unlock(&dma_fence_stub_lock);

	return dma_fence_get(&dma_fence_stub);
}

/**
 * dma_fence_allocate_private_stub - return a private, signaled fence
 *
 * Return a newly allocated and signaled stub fence.
 */
struct dma_fence *dma_fence_allocate_private_stub(void)
{
	struct dma_fence *fence;

	fence = kzalloc(sizeof(*fence), GFP_KERNEL);
	if (fence == NULL)
		return ERR_PTR(-ENOMEM);

	dma_fence_init(fence,
		       &dma_fence_stub_ops,
		       &dma_fence_stub_lock,
		       0, 0);
	dma_fence_signal(fence);

	return fence;
}
